#include "fs/DirectoryWatcher.h"

#include <algorithm>
#include <utility>

#include "fs/DirectoryReader.h"

// Una carpeta vigilada. OVERLAPPED va el primero a proposito: la rutina de terminacion solo
// recibe el LPOVERLAPPED y de ahi vuelve al Entry con un cast.
struct DirectoryWatcher::Entry {
    OVERLAPPED overlapped{};
    DirectoryWatcher* owner = nullptr;
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring path;
    // Cancelado: ya no esta en m_entries y lo posee la E/S pendiente. Hace falta la marca
    // porque CancelIoEx puede llegar tarde y la terminacion venir con exito, no con ABORTED.
    bool abandoned = false;
    // El contenido no se lee nunca, asi que da igual que se desborde (llega con 0 bytes, y
    // eso tambien significa "algo cambio"). Un array de DWORD garantiza la alineacion.
    DWORD buffer[256];
};

namespace {

// Lo que viaja en la APC. La APC lo destruye; si QueueUserAPC falla, Watch.
struct ApplyRequest {
    DirectoryWatcher* self;
    std::vector<std::wstring> paths;
};

// Sin FILE_NOTIFY_CHANGE_LAST_WRITE a proposito: la fecha no se pinta y un log creciendo
// dispararia un refresco tras otro. El tamano si se pinta, asi que ese entra.
constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                          FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE;

}  // namespace

DirectoryWatcher::DirectoryWatcher() = default;

DirectoryWatcher::~DirectoryWatcher() {
    Stop();
}

void DirectoryWatcher::Start(std::function<void(const std::wstring&)> onChanged) {
    m_onChanged = std::move(onChanged);
    m_thread = std::thread([this] {
        // Espera alertable: el hilo duerme al 0 % de CPU y solo despierta para ejecutar las
        // APCs con las que la UI le pasa trabajo y las terminaciones de las lecturas.
        while (!m_quit || m_alive > 0) SleepEx(INFINITE, TRUE);
    });
}

void DirectoryWatcher::Stop() {
    if (!m_thread.joinable()) return;
    // Por APC y no desde aqui: cancelar una E/S overlapped y cerrar su handle desde otro
    // hilo seria una carrera con la rutina de terminacion, que corre en el del vigilante.
    if (QueueUserAPC(&DirectoryWatcher::StopApc, m_thread.native_handle(),
                     reinterpret_cast<ULONG_PTR>(this))) {
        m_thread.join();
    } else {
        m_thread.detach();  // el hilo ya no es alcanzable; join se colgaria para siempre
    }
}

void DirectoryWatcher::Watch(std::vector<std::wstring> paths) {
    if (!m_thread.joinable()) return;
    auto request = std::make_unique<ApplyRequest>(this, std::move(paths));
    if (QueueUserAPC(&DirectoryWatcher::ApplyApc, m_thread.native_handle(),
                     reinterpret_cast<ULONG_PTR>(request.get())))
        (void)request.release();  // a partir de aqui lo posee la APC
}

void CALLBACK DirectoryWatcher::ApplyApc(ULONG_PTR param) {
    const std::unique_ptr<ApplyRequest> request(reinterpret_cast<ApplyRequest*>(param));
    request->self->Apply(request->paths);
}

void CALLBACK DirectoryWatcher::StopApc(ULONG_PTR param) {
    DirectoryWatcher* self = reinterpret_cast<DirectoryWatcher*>(param);
    while (!self->m_entries.empty()) self->Abandon(self->m_entries.size() - 1);
    self->m_quit = true;
}

void CALLBACK DirectoryWatcher::Completion(DWORD error, DWORD, LPOVERLAPPED overlapped) {
    Entry* entry = reinterpret_cast<Entry*>(overlapped);
    DirectoryWatcher* self = entry->owner;

    // Este es el unico sitio donde se puede cerrar el handle: hasta que llega la
    // terminacion la E/S sigue viva y el kernel puede estar escribiendo en el Entry.
    if (entry->abandoned || error == ERROR_OPERATION_ABORTED) {
        self->Destroy(entry);
        return;
    }

    self->m_onChanged(entry->path);
    if (!self->Issue(entry)) self->Destroy(entry);  // carpeta borrada o desconectada
}

void DirectoryWatcher::Apply(const std::vector<std::wstring>& paths) {
    if (m_quit) return;

    // Hacia atras porque Abandon borra del vector.
    for (size_t i = m_entries.size(); i-- > 0;)
        if (std::find(paths.begin(), paths.end(), m_entries[i]->path) == paths.end())
            Abandon(i);

    for (const std::wstring& path : paths) {
        if (path.empty()) continue;  // raiz virtual: no hay carpeta que abrir
        const bool known = std::any_of(
            m_entries.begin(), m_entries.end(),
            [&path](const std::unique_ptr<Entry>& entry) { return entry->path == path; });
        if (!known) Open(path);
    }
}

void DirectoryWatcher::Open(const std::wstring& path) {
    // Compartido para todo: vigilar una carpeta no puede impedir que otros la usen, ni
    // siquiera que la borren.
    const HANDLE handle =
        CreateFileW(LongPath(path).c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    // Sin permiso, desconectada o un sistema de archivos que no avisa: se queda sin vigilar
    // y la app sigue comportandose como en la fase 4.
    if (handle == INVALID_HANDLE_VALUE) return;

    auto entry = std::make_unique<Entry>();
    entry->owner = this;
    entry->handle = handle;
    entry->path = path;
    if (!Issue(entry.get())) {
        CloseHandle(handle);
        return;
    }
    ++m_alive;
    m_entries.push_back(std::move(entry));
}

bool DirectoryWatcher::Issue(Entry* entry) {
    entry->overlapped = OVERLAPPED{};
    return ReadDirectoryChangesW(entry->handle, entry->buffer, sizeof(entry->buffer), FALSE,
                                 kFilter, nullptr, &entry->overlapped,
                                 &DirectoryWatcher::Completion) != 0;
}

void DirectoryWatcher::Abandon(size_t index) {
    Entry* entry = m_entries[index].release();  // ahora lo posee la E/S pendiente
    m_entries.erase(m_entries.begin() + static_cast<ptrdiff_t>(index));
    entry->abandoned = true;
    // Si falla con ERROR_NOT_FOUND es que la lectura ya habia terminado: su terminacion
    // esta encolada y vera la marca. En los dos casos el Entry muere en Completion.
    CancelIoEx(entry->handle, &entry->overlapped);
}

void DirectoryWatcher::Destroy(Entry* entry) {
    const HANDLE handle = entry->handle;
    const auto it = std::find_if(
        m_entries.begin(), m_entries.end(),
        [entry](const std::unique_ptr<Entry>& item) { return item.get() == entry; });
    if (it != m_entries.end())
        m_entries.erase(it);
    else
        delete entry;  // abandonado: no queda ningun unique_ptr que lo haga
    CloseHandle(handle);
    --m_alive;
}
