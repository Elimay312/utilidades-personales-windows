#include "github/Gh.h"

#include <cstdlib>
#include <vector>

#include "model/Utf.h"

namespace Github {
namespace Gh {
namespace {

// Una credencial son decenas de caracteres. Cuatro kilobytes es tres órdenes de magnitud
// más que lo que puede llegar; si llegara más, no es una credencial.
constexpr DWORD kMaxOutput = 4096;
// gh tarda ~570 ms medidos. Cinco segundos es margen de sobra, y un tope hace falta: si se
// quedara esperando, se quedaría esperando el hilo de sincronización con él.
constexpr DWORD kTimeoutMs = 5000;

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : m_handle(handle) {}
    ~Handle() { Close(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    void Reset(HANDLE handle) {
        Close();
        m_handle = handle;
    }
    void Close() {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
        m_handle = nullptr;
    }
    HANDLE Get() const { return m_handle; }
    bool Valid() const { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle = nullptr;
};

// La lista de handles que el hijo puede heredar, y solo esos. Heredar todo lo que haya
// abierto es filtrarle handles a un proceso ajeno.
class AttributeList {
public:
    ~AttributeList() {
        if (m_list != nullptr) {
            DeleteProcThreadAttributeList(m_list);
            std::free(m_list);
        }
    }

    bool Build(HANDLE* handles, DWORD count) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (size == 0) return false;

        m_list = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(std::malloc(size));
        if (m_list == nullptr) return false;

        if (InitializeProcThreadAttributeList(m_list, 1, 0, &size) == FALSE) {
            std::free(m_list);
            m_list = nullptr;
            return false;
        }
        if (UpdateProcThreadAttribute(m_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                      count * sizeof(HANDLE), nullptr, nullptr) == FALSE) {
            return false;
        }
        return true;
    }

    LPPROC_THREAD_ATTRIBUTE_LIST Get() const { return m_list; }

private:
    LPPROC_THREAD_ATTRIBUTE_LIST m_list = nullptr;
};

Model::Error Nope(std::wstring what) {
    Model::Error error;
    error.kind = Model::Fail::Auth;
    error.code = static_cast<int>(GetLastError());
    error.detail = std::move(what);
    return error;
}

}  // namespace

bool Find(std::wstring& pathOut) {
    // La extensión va explícita y es .exe y solo .exe. Un .cmd necesitaría un intérprete, y
    // lanzar un intérprete con una línea de órdenes construida es exactamente lo que la
    // regla 7 de SEGURIDAD.md evita.
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = SearchPathW(nullptr, L"gh", L".exe", static_cast<DWORD>(buffer.size()),
                               buffer.data(), nullptr);
    if (length == 0) return false;
    if (length > buffer.size()) {
        buffer.resize(length);
        length = SearchPathW(nullptr, L"gh", L".exe", static_cast<DWORD>(buffer.size()),
                             buffer.data(), nullptr);
        if (length == 0) return false;
    }
    buffer.resize(length);
    pathOut = buffer;
    return true;
}

Model::Result<Secret> AskForCredential() {
    std::wstring exe;
    if (!Find(exe)) {
        // No está instalado. No es un error: es que toca la hoja de bienvenida.
        return Secret();
    }

    SECURITY_ATTRIBUTES inheritable = {};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HANDLE rawRead = nullptr;
    HANDLE rawWrite = nullptr;
    if (CreatePipe(&rawRead, &rawWrite, &inheritable, 0) == FALSE) {
        return Nope(L"No se pudo hablar con GitHub CLI");
    }
    Handle readEnd(rawRead);
    Handle writeEnd(rawWrite);

    // Nuestro extremo NO se hereda: si se heredara, el hijo tendría abierto el lado de
    // lectura y nunca veríamos el fin de archivo.
    if (SetHandleInformation(readEnd.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE) {
        return Nope(L"No se pudo hablar con GitHub CLI");
    }

    // Sin stdin, gh podría quedarse esperando una respuesta que nadie va a dar. Y su stderr
    // tampoco nos interesa: lo que dice cuando falla ya lo resume el código de salida.
    Handle nul(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0,
                           nullptr));
    if (!nul.Valid()) return Nope(L"No se pudo hablar con GitHub CLI");

    HANDLE inherited[] = {writeEnd.Get(), nul.Get()};
    AttributeList attributes;
    if (!attributes.Build(inherited, 2)) {
        return Nope(L"No se pudo hablar con GitHub CLI");
    }

    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = writeEnd.Get();
    startup.StartupInfo.hStdError = nul.Get();
    startup.StartupInfo.hStdInput = nul.Get();
    startup.lpAttributeList = attributes.Get();

    // CreateProcessW escribe dentro de la línea de órdenes, así que tiene que ser un búfer
    // nuestro y modificable. El nombre de la aplicación va con la ruta ya resuelta: así no
    // hay una segunda búsqueda por PATH que alguien pueda secuestrar.
    std::wstring commandLine = L"gh auth token";

    PROCESS_INFORMATION started = {};
    // CREATE_NO_WINDOW: sin esto parpadea una consola negra cada vez que se abre Brújula.
    const BOOL launched =
        CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                       &startup.StartupInfo, &started);
    if (launched == FALSE) {
        return Nope(L"No se pudo ejecutar GitHub CLI");
    }
    Handle process(started.hProcess);
    Handle thread(started.hThread);

    // EL detalle de esta rutina: cerrar nuestro extremo de escritura AHORA. Mientras siga
    // abierto aquí, la tubería no ve nunca el fin de archivo y el ReadFile de abajo se queda
    // esperando para siempre — con el hilo de sincronización dentro.
    writeEnd.Close();

    std::string raw;
    std::vector<char> chunk(1024);
    for (;;) {
        DWORD read = 0;
        if (ReadFile(readEnd.Get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                     nullptr) == FALSE) {
            break;  // ERROR_BROKEN_PIPE: el hijo cerró, que es el final normal.
        }
        if (read == 0) break;
        raw.append(chunk.data(), read);
        if (raw.size() > kMaxOutput) break;
    }
    SecureZeroMemory(chunk.data(), chunk.size());

    if (WaitForSingleObject(process.Get(), kTimeoutMs) != WAIT_OBJECT_0) {
        TerminateProcess(process.Get(), 1);
        SecureZeroMemory(raw.data(), raw.size());
        return Model::Oops(Model::Fail::Auth, L"GitHub CLI no respondió a tiempo");
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.Get(), &exitCode);

    std::wstring value = Model::ToWide(raw);
    SecureZeroMemory(raw.data(), raw.size());
    Scrub wipeValue(value);

    // Se recorta el salto de línea y cualquier espacio suelto.
    while (!value.empty() && (value.back() == L'\n' || value.back() == L'\r' ||
                              value.back() == L' ' || value.back() == L'\t')) {
        value.pop_back();
    }

    if (exitCode != 0 || value.empty()) {
        // gh está instalado pero sin sesión. Tampoco es un error: toca la hoja.
        return Secret();
    }
    if (!LooksLikeCredential(value)) {
        // Salió algo que no tiene forma de credencial. Se trata como "no hay" en vez de
        // intentar usarlo: un 401 dentro de un rato explicaría mucho peor lo que pasa.
        return Secret();
    }

    Secret credential;
    credential.Adopt(value);
    return credential;
}

}  // namespace Gh
}  // namespace Github
