#include "app/App.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <optional>
#include <thread>
#include <utility>

#include "ui/MillerView.h"
#include "ui/Theme.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

// Tras cada evento renderizamos unos frames de mas: ImGui necesita un par de
// pasadas para asentar tamanos y animaciones antes de volver a dormir.
constexpr int kFramesPerEvent = 3;

constexpr ImGuiWindowFlags kPanelFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
    ImGuiWindowFlags_NoNavFocus;

void BeginPanel(const char* id, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(id, nullptr, kPanelFlags);
}

// ponytail: USERPROFILE en vez de SHGetKnownFolderPath: tres lineas y cero librerias
// nuevas. Si hiciera falta el perfil real de una cuenta redirigida, subir a
// SHGetKnownFolderPath(FOLDERID_Profile) + ole32.
std::wstring UserFolder() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"C:\\";
    return std::wstring(buffer, length);
}

int IndexOfName(const std::vector<DirectoryEntry>& entries, const std::wstring& name) {
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name) return static_cast<int>(i);
    return -1;
}

}  // namespace

App::~App() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    m_gfx.Destroy();
    m_window.Destroy();
}

bool App::Init(const wchar_t* startPath) {
    if (!m_window.Create(L"Rayo", 1280, 800)) return false;
    if (!m_gfx.Create(m_window.Handle())) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // nada de imgui.ini

    if (!ImGui_ImplWin32_Init(m_window.Handle())) return false;
    if (!ImGui_ImplDX11_Init(m_gfx.Device(), m_gfx.Context())) return false;

    Theme::Apply(m_window.DpiScale());
    Theme::LoadFont();

    m_window.onMessage = [](HWND h, UINT m, WPARAM w, LPARAM l) {
        return ImGui_ImplWin32_WndProcHandler(h, m, w, l) != 0;
    };
    m_window.onWake = [this] { RequestFrames(); };
    m_window.onResize = [this](int w, int h) {
        m_gfx.Resize(static_cast<UINT>(w), static_cast<UINT>(h));
        // Durante el resize modal el bucle esta bloqueado: repintar aqui mismo
        // es lo que evita el estirado y el parpadeo.
        RenderFrame();
        RequestFrames();
    };
    m_window.onDpiChanged = [this](float scale) {
        Theme::Apply(scale);  // atlas dinamico: no hay nada que reconstruir
        RequestFrames();
    };

    m_pool.Start(std::thread::hardware_concurrency() / 2);
    Navigate(startPath && startPath[0] ? std::wstring(startPath) : UserFolder());

    RenderFrame();  // un frame ya pintado antes de mostrar: sin flash blanco
    m_window.Show();
    RequestFrames();
    return true;
}

int App::Run() {
    while (m_running) {
        // Sin frames pendientes dormimos aqui: 0 % de CPU hasta que llegue algo.
        if (m_pendingFrames == 0)
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!m_running) break;

        DrainResults();

        if (m_pendingFrames > 0) {
            RenderFrame();
            --m_pendingFrames;
        }
    }
    return 0;
}

void App::RequestFrames() {
    m_pendingFrames = kFramesPerEvent;
}

void App::Navigate(std::wstring path) {
    path = NormalizePath(path);
    m_pathUtf8 = path.empty() ? "Unidades" : ToUtf8(path);
    m_status.clear();

    const std::optional<std::wstring> parent = ParentPath(path);
    if (parent) {
        // Bajar deja anotado por donde se bajo: subir luego restaura el cursor solo,
        // por el mismo camino que cualquier otra vuelta a una carpeta ya visitada.
        m_cursorMemory[*parent] = LastComponent(path);
        SetPane(m_parent, *parent, LastComponent(path));
    } else {
        m_parent = Pane{};  // la raiz virtual no tiene columna izquierda
    }

    const auto remembered = m_cursorMemory.find(path);
    SetPane(m_current, std::move(path),
            remembered == m_cursorMemory.end() ? std::wstring() : remembered->second);
}

void App::GoParent() {
    if (const std::optional<std::wstring> parent = ParentPath(m_current.path))
        Navigate(*parent);
}

void App::Open() {
    const DirectoryEntry* entry = Selected();
    if (!entry) return;

    std::wstring target = JoinPath(m_current.path, entry->name);
    if (entry->IsDirectory()) {
        Navigate(std::move(target));
        return;
    }

    const HWND hwnd = m_window.Handle();
    m_pool.Submit([this, target = std::move(target), hwnd] {
        // ShellExecuteExW delega en extensiones del shell que usan COM, asi que el hilo
        // tiene que estar inicializado en STA.
        const HRESULT com =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        // NOASYNC: este hilo vuelve al pool en cuanto acabe y deja de bombear mensajes.
        info.fMask = SEE_MASK_NOASYNC;
        info.lpFile = target.c_str();  // lpVerb nulo: el verbo predeterminado
        info.nShow = SW_SHOWNORMAL;
        // Sin hwnd padre: los dialogos del shell ("abrir con") son de otro hilo y no
        // queremos que deshabiliten nuestra ventana desde fuera del hilo de UI.
        if (!ShellExecuteExW(&info)) Report(FormatWin32Error(GetLastError()), hwnd);

        if (SUCCEEDED(com)) CoUninitialize();
    });
}

void App::SetPane(Pane& pane, std::wstring path, std::wstring select) {
    pane.active = true;
    pane.path = std::move(path);
    pane.select = std::move(select);
    pane.scrollToCursor = true;

    // La cache se sirve al instante para que ir y volver no parpadee...
    if (const EntryList cached = m_cache.Get(pane.path)) {
        ApplyListing(pane, cached);
    } else {
        pane.entries.reset();
        pane.cursor = 0;
    }
    Request(pane.path);  // ...y a la vez se relee por detras para refrescarla
}

void App::ApplyListing(Pane& pane, const EntryList& entries) {
    pane.entries = entries;

    // El cursor sigue al nombre, no al indice: un refresco con entradas nuevas o
    // borradas no mueve la seleccion de sitio.
    const int found = IndexOfName(*entries, pane.select);
    pane.cursor = found >= 0
                      ? found
                      : std::clamp(pane.cursor, 0, std::max(0, static_cast<int>(entries->size()) - 1));
    if (found < 0 && !entries->empty()) pane.select = (*entries)[static_cast<size_t>(pane.cursor)].name;
    pane.scrollToCursor = true;
}

// ponytail: sin dedupe por tiempo. Volver a una carpeta la relee aunque se acabe de leer; con
// la cache sirviendo al instante no se nota, y la fase 5 (vigilante de disco) sustituye este
// refresco entero. Si hiciera falta antes, un sello de tiempo por ruta en la cache.
void App::Request(const std::wstring& path) {
    if (std::find(m_inFlight.begin(), m_inFlight.end(), path) != m_inFlight.end()) return;
    m_inFlight.push_back(path);

    // El HWND se copia por valor: la UI lo pone a null en WM_DESTROY y un hilo de
    // trabajo no debe leer ese miembro mientras tanto.
    const HWND hwnd = m_window.Handle();
    m_pool.Submit([this, path, hwnd] {
        DirectoryListing listing = ReadDirectory(path);
        {
            std::lock_guard<std::mutex> lock(m_inboxMutex);
            m_inbox.push_back(std::move(listing));
        }
        PostMessageW(hwnd, WM_APP_WAKE, 0, 0);
    });
}

void App::Report(std::string message, HWND hwnd) {
    {
        std::lock_guard<std::mutex> lock(m_inboxMutex);
        m_messages.push_back(std::move(message));
    }
    PostMessageW(hwnd, WM_APP_WAKE, 0, 0);
}

void App::DrainResults() {
    std::vector<DirectoryListing> ready;
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_inboxMutex);
        if (m_inbox.empty() && m_messages.empty()) return;
        ready.swap(m_inbox);
        messages.swap(m_messages);
    }

    for (DirectoryListing& listing : ready) {
        std::erase(m_inFlight, listing.path);

        if (listing.error != ERROR_SUCCESS) {
            // Se queda el listado anterior en pantalla: el error va a la barra de estado.
            if (listing.path == m_current.path) m_status = FormatWin32Error(listing.error);
            continue;
        }

        const EntryList entries =
            std::make_shared<const std::vector<DirectoryEntry>>(std::move(listing.entries));
        m_cache.Put(listing.path, entries);

        // La ruta identifica el resultado. Si el usuario ya se fue a otro sitio ninguna
        // columna esta en esa ruta y el listado solo engorda la cache.
        for (Pane* pane : {&m_current, &m_parent})
            if (pane->active && pane->path == listing.path) ApplyListing(*pane, entries);
    }

    for (std::string& message : messages) m_status = std::move(message);
}

void App::SetCursor(int cursor) {
    const std::vector<DirectoryEntry>& rows = Rows(m_current.entries);
    cursor = std::clamp(cursor, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    if (cursor == m_current.cursor) return;

    m_current.cursor = cursor;
    m_current.scrollToCursor = true;
    if (rows.empty()) return;
    m_current.select = rows[static_cast<size_t>(cursor)].name;
    m_cursorMemory[m_current.path] = m_current.select;
}

const DirectoryEntry* App::Selected() const {
    const std::vector<DirectoryEntry>& rows = Rows(m_current.entries);
    if (rows.empty()) return nullptr;
    const int cursor = std::clamp(m_current.cursor, 0, static_cast<int>(rows.size()) - 1);
    return &rows[static_cast<size_t>(cursor)];
}

void App::Execute(Command command) {
    const int halfPage = std::max(1, m_visibleRows / 2);

    switch (command) {
    case Command::None:
        return;
    case Command::Quit:
        m_running = false;
        return;
    case Command::Open:
        Open();
        return;
    case Command::GoParent:
        GoParent();
        return;
    case Command::GoHome:
        Navigate(UserFolder());
        return;
    case Command::MoveDown:
        SetCursor(m_current.cursor + 1);
        return;
    case Command::MoveUp:
        SetCursor(m_current.cursor - 1);
        return;
    case Command::MoveTop:
        SetCursor(0);
        return;
    case Command::MoveBottom:
        SetCursor(static_cast<int>(Rows(m_current.entries).size()) - 1);
        return;
    case Command::HalfPageDown:
        SetCursor(m_current.cursor + halfPage);
        return;
    case Command::HalfPageUp:
        SetCursor(m_current.cursor - halfPage);
        return;
    }
}

void App::ProcessInput() {
    Execute(Keymap::Poll(m_keys));
}

void App::RenderFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ProcessInput();  // los comandos se aplican antes de dibujar: BuildUi solo lee
    BuildUi();
    ImGui::Render();

    m_gfx.Clear(&Theme::kBackground.x);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_gfx.Present();
}

void App::BuildUi() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Una linea exacta: alto del texto mas el padding de la ventana.
    const float statusHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    const float bodyHeight = vp->WorkSize.y - statusHeight;
    const float parentWidth = vp->WorkSize.x * 0.20f;
    const float currentWidth = vp->WorkSize.x * 0.40f;
    const float previewWidth = vp->WorkSize.x - parentWidth - currentWidth;
    const float top = vp->WorkPos.y;
    float x = vp->WorkPos.x;

    BeginPanel("##parent", ImVec2(x, top), ImVec2(parentWidth, bodyHeight));
    if (m_parent.active)
        MillerView::DrawEntries(Rows(m_parent.entries), m_parent.cursor, m_parent.scrollToCursor);
    ImGui::End();
    x += parentWidth;

    BeginPanel("##current", ImVec2(x, top), ImVec2(currentWidth, bodyHeight));
    m_visibleRows =
        MillerView::DrawEntries(Rows(m_current.entries), m_current.cursor, m_current.scrollToCursor);
    ImGui::End();
    x += currentWidth;

    BeginPanel("##preview", ImVec2(x, top), ImVec2(previewWidth, bodyHeight));
    ImGui::TextColored(Theme::kTextDim, "Vista previa");
    ImGui::Separator();
    ImGui::TextColored(Theme::kTextDim, "Fase 4");
    ImGui::End();

    BeginPanel("##status", ImVec2(vp->WorkPos.x, top + bodyHeight),
               ImVec2(vp->WorkSize.x, statusHeight));
    // TextUnformatted y no Text: una ruta o un nombre pueden llevar un % dentro.
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
    ImGui::TextUnformatted(m_pathUtf8.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const int count = static_cast<int>(Rows(m_current.entries).size());
    ImGui::TextColored(Theme::kTextDim, "%d/%d", count == 0 ? 0 : m_current.cursor + 1, count);
    if (!m_status.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kAccent);
        ImGui::TextUnformatted(m_status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
}
