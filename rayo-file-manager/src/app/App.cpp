#include "app/App.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <optional>
#include <thread>
#include <utility>

#include "core/Diag.h"
#include "ui/MillerView.h"
#include "ui/PreviewPane.h"
#include "ui/Theme.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

// Tras cada evento renderizamos unos frames de mas: ImGui necesita un par de
// pasadas para asentar tamanos y animaciones antes de volver a dormir.
constexpr int kFramesPerEvent = 3;

// Anchos de las tres columnas. El de la preview es lo que sobra.
constexpr float kParentFraction = 0.20f;
constexpr float kCurrentFraction = 0.40f;

// Lo que el cursor tiene que estar quieto antes de tocar el disco por la vista previa. Con
// j pulsado no se decodifica ni una imagen de las que se pasan de largo. La resolucion de
// GetTickCount64 (~15,6 ms) deja el retardo real en 60-76 ms.
constexpr unsigned long long kPreviewDelayMs = 60;

// El objetivo de decodificacion se redondea a esto: redimensionar la ventana pixel a pixel
// no puede estar rehaciendo la imagen.
constexpr int kPreviewSizeStep = 256;

// Lo que se agrupan los avisos del vigilante antes de releer. Ventana fija, no deslizante:
// copiar mil archivos refresca cada ~100 ms en vez de no refrescar hasta que termine.
//
// ponytail: agrupar por tiempo y nada mas. El coste sale proporcional al trajin real del
// disco, y con una carpeta que no para (medido con %TEMP%, 6.091 entradas) son ~250 ms de
// CPU cada 15 s frente a los ~40 de la fase 4. Si molestara, el paso siguiente es un minimo
// entre refrescos de la misma carpeta, no subir este numero.
constexpr unsigned long long kWatchDebounceMs = 100;

// Lo que dura un mensaje en la barra antes de borrarse solo. Sin plazo, el error de hace
// diez navegaciones sigue ahi como si acabara de pasar.
constexpr unsigned long long kStatusMs = 5000;

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

// Ordinal y sin distinguir mayusculas, que es como compara nombres el sistema de archivos:
// al completar una ruta la tilde si importa (otra carpeta), la caja no.
bool SameText(const wchar_t* a, const wchar_t* b, size_t length) {
    if (length == 0) return true;
    return CompareStringOrdinal(a, static_cast<int>(length), b, static_cast<int>(length),
                                TRUE) == CSTR_EQUAL;
}

bool StartsWith(const std::wstring& name, const std::wstring& prefix) {
    return name.size() >= prefix.size() && SameText(name.c_str(), prefix.c_str(), prefix.size());
}

size_t CommonLength(const std::wstring& a, const std::wstring& b) {
    const size_t limit = std::min(a.size(), b.size());
    size_t length = 0;
    while (length < limit && SameText(a.c_str() + length, b.c_str() + length, 1)) ++length;
    // Sin cortar un par suplente por la mitad: media pareja no es un caracter.
    if (length > 0 && IS_HIGH_SURROGATE(a[length - 1])) --length;
    return length;
}

COLORREF ToColorRef(const ImVec4& color) {
    const unsigned hex = Theme::ToHex(color);
    return RGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

// Un color mal escrito en el config se ignora y se queda el de fabrica: mas vale un tema a
// medias que un panel negro sin explicacion.
bool ParseHex(const std::wstring& text, unsigned& out) {
    const wchar_t* start = text.c_str() + (text.size() > 1 && text[0] == L'#' ? 1 : 0);
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(start, &end, 16);
    if (!end || end == start || *end != L'\0') return false;
    out = static_cast<unsigned>(value);
    return true;
}

// El archivo que se escribe la primera vez. Los colores y los atajos salen de las mismas
// tablas que usa el programa: no hay dos listas que desincronizar.
std::wstring DefaultConfig() {
    std::wstring text =
        L"; Rayo. Este archivo se crea solo la primera vez; borralo para volver a empezar.\r\n"
        L"; Colores en RRGGBB hexadecimal.\r\n"
        L"; [keys] es \"tecla=comando\". Si la seccion existe sustituye entera a los atajos de\r\n"
        L"; fabrica, asi que borrar una linea quita ese atajo. Un caracter suelto ('~', '/')\r\n"
        L"; es el caracter que produce el teclado, sea cual sea la distribucion; lo demas\r\n"
        L"; (J, 1, DownArrow, Ctrl+D) son teclas fisicas con los nombres de ImGui.\r\n"
        L"; [window], [state] y [bookmarks] los escribe la app al cerrarse.\r\n"
        L"\r\n[colors]\r\n";
    for (const Theme::NamedColor& color : Theme::Colors()) {
        wchar_t line[64];
        swprintf_s(line, L"%s=%06x\r\n", color.name, Theme::ToHex(*color.color));
        text += line;
    }
    text +=
        L"\r\n[options]\r\n"
        L"showHidden=0\r\n"
        L"; startPath: vacio = la carpeta de usuario, \"last\" = donde lo dejaste la vez\r\n"
        L"; anterior, o una ruta fija.\r\n"
        L"startPath=\r\n"
        L"\r\n[keys]\r\n";
    text += FromUtf8(Keymap::Defaults());
    return text;
}

int IndexOfName(const std::vector<DirectoryEntry>& entries, const std::wstring& name) {
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name) return static_cast<int>(i);
    return -1;
}

}  // namespace

App::~App() {
    // Lo primero: mientras haya tareas vivas pueden seguir llegando previews al buzon.
    m_pool.Stop();
    // Antes de destruir la ventana: su callback hace PostMessageW con el HWND.
    m_watcher.Stop();
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    // Las texturas de las previews son objetos de D3D y la cache es un miembro: sin
    // vaciarla aqui moririan despues del dispositivo y saldrian como fuga en el informe.
    m_previewFile.reset();
    m_previewInbox.clear();
    m_previewCache.Clear();
    m_gfx.Destroy();
    m_window.Destroy();
}

bool App::Init(const wchar_t* startPath) {
    Diag::Open(AppFile(L"rayo.log"));

    // El contexto de ImGui, lo primero: el keymap se lee del config con los nombres de
    // tecla de ImGui. Es memoria y nada mas, no toca ni la ventana ni D3D.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // nada de imgui.ini
    Diag::Mark("contexto");
    const int badKeys = LoadConfig();
    Diag::Mark("config");

    Window::Layout saved;
    saved.x = m_config.GetInt(L"window", L"x", 0);
    saved.y = m_config.GetInt(L"window", L"y", 0);
    saved.width = m_config.GetInt(L"window", L"width", 0);
    saved.height = m_config.GetInt(L"window", L"height", 0);
    saved.maximized = m_config.GetInt(L"window", L"maximized", 0) != 0;
    if (!m_window.Create(L"Rayo", 1280, 800, ToColorRef(Theme::kBackground), &saved))
        return false;
    Diag::Mark("ventana");
    // Visible ya, con el fondo pintado por GDI: crear el dispositivo D3D cuesta ~200 ms
    // (cargar el driver) y esperar a eso para ensenar la ventana se ve como un arranque
    // lento aunque no lo sea.
    m_window.Show();
    Diag::Mark("VISIBLE");

    m_pool.Start(std::thread::hardware_concurrency() / 2);

    // El vigilante avisa desde su propio hilo y en rafagas. Solo se despierta la UI con el
    // primer aviso de cada tanda: mil archivos copiandose no pueden ser mil repintados.
    m_watcher.Start([this, hwnd = m_window.Handle()](const std::wstring& path) {
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(m_inboxMutex);
            if (std::find(m_changed.begin(), m_changed.end(), path) == m_changed.end()) {
                m_changed.push_back(path);
                first = true;
            }
        }
        if (first) PostMessageW(hwnd, WM_APP_WAKE, 0, 0);
    });

    // La lectura de la carpeta se lanza antes de crear el dispositivo: mientras carga el
    // driver, un hilo de trabajo ya esta listando. El primer frame sale con la carpeta
    // dentro en vez de vacio.
    m_tabs.emplace_back();
    Navigate(StartFolder(startPath));
    // Despues de Navigate, que limpia la barra de estado.
    if (badKeys > 0)
        SetStatus("config: " + std::to_string(badKeys) +
                  (badKeys == 1 ? " atajo sin entender" : " atajos sin entender"));
    Diag::Mark("hilos");

    if (!m_gfx.Create(m_window.Handle())) return false;
    Diag::Mark("d3d");

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
    Diag::Mark("imgui");

    // El listado que se pidio antes de crear el dispositivo puede estar ya esperando: si
    // ha llegado, el primer frame es el primer frame util.
    DrainResults();
    RenderFrame();
    m_window.EndGdiBackground();  // a partir de aqui la ventana la pinta D3D
    Diag::Mark("frame");
    Diag::WriteStartup();

    RequestFrames();
    return true;
}

// Los colores y los atajos se aplican aqui y no se vuelven a mirar: cambiar el config
// pide reiniciar, que es lo que hace cualquier programa con un archivo de configuracion.
// Devuelve cuantas lineas de [keys] no se entendieron; el aviso lo da Init, porque
// Navigate limpia la barra de estado y se lo llevaria por delante.
int App::LoadConfig() {
    m_config.Load(DefaultConfig());

    for (const auto& [key, value] : m_config.Section(L"colors")) {
        unsigned hex = 0;
        if (!ParseHex(value, hex)) continue;
        for (const Theme::NamedColor& color : Theme::Colors())
            if (_wcsicmp(key.c_str(), color.name) == 0) *color.color = Theme::Rgb(hex);
    }

    std::vector<std::pair<std::string, std::string>> keys;
    for (const auto& [spec, command] : m_config.Section(L"keys"))
        keys.emplace_back(ToUtf8(spec), ToUtf8(command));
    const int bad = Keymap::Load(keys);

    for (const auto& [letter, path] : m_config.Section(L"bookmarks"))
        if (letter.size() == 1 && letter[0] < 128)
            m_bookmarks[static_cast<char>(letter[0])] = path;

    m_showHidden = m_config.GetInt(L"options", L"showHidden", 0) != 0;
    return bad;
}

// Donde abrir. La carpeta de usuario por defecto y no la ultima de la sesion anterior:
// ahi estan Descargas, Escritorio y Documentos, que es donde se trabaja, y si hace falta
// subir a la raiz de la unidad es una tecla. Quien prefiera lo contrario pone
// `startPath=last` en el config, o una ruta fija.
std::wstring App::StartFolder(const wchar_t* commandLine) const {
    if (commandLine && commandLine[0]) return commandLine;  // el menu contextual pasa por aqui

    const std::wstring configured = m_config.Get(L"options", L"startPath");
    if (configured == L"last") return m_config.Get(L"state", L"lastPath", UserFolder());
    return configured.empty() ? UserFolder() : configured;
}

// Al final de Run, con la ventana todavia viva: su posicion es parte de lo que se guarda.
void App::SaveConfig() {
    const Window::Layout layout = m_window.Placement();
    if (layout.width > 0 && layout.height > 0) {
        m_config.SetInt(L"window", L"x", layout.x);
        m_config.SetInt(L"window", L"y", layout.y);
        m_config.SetInt(L"window", L"width", layout.width);
        m_config.SetInt(L"window", L"height", layout.height);
        m_config.SetInt(L"window", L"maximized", layout.maximized ? 1 : 0);
    }
    m_config.Set(L"state", L"lastPath", m_current.path);
    m_config.SetInt(L"options", L"showHidden", m_showHidden ? 1 : 0);

    // La seccion entera y no clave a clave: un marcador puede haberse quedado sin letra.
    Config::Pairs bookmarks;
    for (const auto& [letter, path] : m_bookmarks)
        bookmarks.emplace_back(std::wstring(1, static_cast<wchar_t>(letter)), path);
    m_config.SetSection(L"bookmarks", bookmarks);
}

int App::Run() {
    while (m_running) {
        // Sin frames pendientes dormimos aqui: 0 % de CPU hasta que llegue algo. Lo unico
        // que acorta la espera son los dos plazos (vista previa y refresco del vigilante);
        // en reposo los dos estan a 0 y vuelve a ser INFINITE. Se prefiere a SetTimer
        // porque no toca Window ni deja nada que matar.
        if (m_pendingFrames == 0) {
            const ULONGLONG now = GetTickCount64();
            DWORD timeout = INFINITE;
            for (const unsigned long long due : {m_previewDue, m_refreshDue, m_statusUntil}) {
                if (due == 0) continue;
                const DWORD wait = due > now ? static_cast<DWORD>(due - now) : 0;
                if (timeout == INFINITE || wait < timeout) timeout = wait;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }

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

        if (m_previewDue != 0 && GetTickCount64() >= m_previewDue) {
            m_previewDue = 0;
            StartPreview();
            RequestFrames();  // una carpeta en cache se sirve en el acto y hay que pintarla
        }

        if (m_statusUntil != 0 && GetTickCount64() >= m_statusUntil) {
            SetStatus({});
            RequestFrames();
        }

        RefreshDirty();
        DrainResults();

        if (m_pendingFrames > 0) {
            RenderFrame();
            --m_pendingFrames;
        }
    }

    SaveConfig();  // aqui y no en el destructor: la ventana aun existe
    return 0;
}

void App::RequestFrames() {
    m_pendingFrames = kFramesPerEvent;
}

void App::Navigate(std::wstring path) {
    path = NormalizePath(path);
    m_pathUtf8 = path.empty() ? "Unidades" : ToUtf8(path);
    if (m_tab < m_tabs.size()) m_tabs[m_tab] = path;  // la pestana es su carpeta y ya
    SetStatus({});
    // El filtro es de la carpeta en la que se escribio: arrastrarlo a la siguiente
    // esconderia media carpeta sin que se vea por que.
    m_filter.clear();
    m_filterUtf8.clear();
    m_freeBytes = 0;  // hasta que llegue el listado de la carpeta nueva no se sabe

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

    // Solo las dos columnas navegables: la de la vista previa cambia con cada j/k y abrir y
    // cerrar un handle por pulsacion no compensa para algo que se mira de pasada.
    std::vector<std::wstring> watched;
    if (!m_current.path.empty()) watched.push_back(m_current.path);
    if (m_parent.active && !m_parent.path.empty()) watched.push_back(m_parent.path);
    m_watcher.Watch(std::move(watched));
}

// El refresco del vigilante pasa por Request como cualquier otro, asi que hereda el descarte
// por ruta y el dedupe de la fase 3. El cursor lo recoloca ApplyListing: por nombre si sigue
// ahi, y si no, en la misma posicion numerica.
void App::RefreshDirty() {
    if (m_refreshDue == 0 || GetTickCount64() < m_refreshDue) return;
    m_refreshDue = 0;
    for (const std::wstring& path : m_dirty) Request(path);
    m_dirty.clear();
}

// Las pestanas no guardan nada mas que su carpeta: el cursor lo restaura m_cursorMemory,
// que ya va por ruta, y el filtro se queda en la carpeta donde se escribio (como al
// navegar). Una pestana con su propio filtro seria estado duplicado para poco.
void App::NewTab() {
    m_tabs.insert(m_tabs.begin() + static_cast<ptrdiff_t>(m_tab) + 1, m_current.path);
    ++m_tab;
    SetStatus("pestana " + std::to_string(m_tab + 1));
}

void App::SelectTab(size_t index) {
    if (index >= m_tabs.size() || index == m_tab) return;
    m_tab = index;
    Navigate(m_tabs[m_tab]);
}

void App::CloseTab() {
    if (m_tabs.size() <= 1) return;  // cerrar la ultima seria salir, y para eso esta q
    m_tabs.erase(m_tabs.begin() + static_cast<ptrdiff_t>(m_tab));
    if (m_tab >= m_tabs.size()) m_tab = m_tabs.size() - 1;
    Navigate(m_tabs[m_tab]);
}

void App::SetBookmark(char letter) {
    if (!CanEdit()) return;  // la raiz virtual no es una carpeta a la que volver
    m_bookmarks[letter] = m_current.path;
    SetStatus(std::string("marcador ") + letter);
}

void App::GotoBookmark(char letter) {
    const auto found = m_bookmarks.find(letter);
    if (found == m_bookmarks.end()) {
        SetStatus(std::string("sin marcador ") + letter);
        return;
    }
    Navigate(found->second);
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
        pane.view.reset();
        pane.cursor = 0;
    }
    Request(pane.path);  // ...y a la vez se relee por detras para refrescarla
}

void App::ApplyListing(Pane& pane, const EntryList& entries) {
    pane.entries = entries;
    RebuildView(pane);
}

// `view` es lo unico que se pinta y sobre lo que se mueve el cursor. Si no sobra nada que
// esconder se comparte el mismo shared_ptr y no se copia ni una entrada.
//
// ponytail: cuando si sobra, copia las que pasan (12.000 entradas son ~3 ms en el hilo de
// UI, una vez por listado o por tecla del filtro). Si se notara, un vector de indices, a
// cambio de tocar todo lo que hoy recibe un vector plano de entradas.
void App::RebuildView(Pane& pane) {
    const bool filtered = &pane == &m_current && !m_filter.empty();
    if (pane.entries && (!m_showHidden || filtered)) {
        std::vector<DirectoryEntry> visible;
        visible.reserve(pane.entries->size());
        for (const DirectoryEntry& entry : *pane.entries) {
            if (!m_showHidden && entry.IsHidden()) continue;
            if (filtered && !NameContains(entry.name, m_filter)) continue;
            visible.push_back(entry);
        }
        pane.view = visible.size() == pane.entries->size()
                        ? pane.entries
                        : std::make_shared<const std::vector<DirectoryEntry>>(std::move(visible));
    } else {
        pane.view = pane.entries;
    }
    PlaceCursor(pane);
}

void App::RebuildViews() {
    for (Pane* pane : {&m_current, &m_parent, &m_preview}) RebuildView(*pane);
}

// El cursor sigue al nombre, no al indice: ni un refresco con entradas nuevas o borradas ni
// un filtro que se estrecha mueven la seleccion de sitio.
void App::PlaceCursor(Pane& pane) {
    const std::vector<DirectoryEntry>& rows = Rows(pane.view);
    const int found = IndexOfName(rows, pane.select);
    pane.cursor =
        found >= 0 ? found
                   : std::clamp(pane.cursor, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    if (found < 0 && !rows.empty()) pane.select = rows[static_cast<size_t>(pane.cursor)].name;
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

// Las unidades de ImGui aqui son pixeles fisicos (imgui_impl_win32 pone io.DisplaySize en
// pixeles y el DPI va por style.FontScaleDpi), asi que no hay conversion que hacer.
int App::PreviewTargetPx() const {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float width = vp->WorkSize.x * (1.0f - kParentFraction - kCurrentFraction);
    const int longest = static_cast<int>(std::max(width, vp->WorkSize.y));
    return ((longest + kPreviewSizeStep - 1) / kPreviewSizeStep) * kPreviewSizeStep;
}

void App::ArmPreview() {
    // Subir la generacion invalida de paso lo que haya en vuelo: el hilo lo comprueba antes
    // de empezar y DrainResults antes de mostrarlo.
    m_previewGen.fetch_add(1);
    m_previewDue = GetTickCount64() + kPreviewDelayMs;
}

// Se llama una vez por frame, que es lo que cubre los tres motivos por los que cambia lo que
// hay bajo el cursor: moverlo, navegar, y un listado que llega por detras.
void App::UpdatePreview() {
    const int targetPx = PreviewTargetPx();
    const DirectoryEntry* entry = Selected();
    std::wstring target = entry ? JoinPath(m_current.path, entry->name) : std::wstring();

    if (target != m_previewTarget) {
        m_previewTarget = std::move(target);
        m_previewTargetPx = targetPx;
        m_previewDue = 0;
        m_preview = Pane{};
        m_previewFile.reset();
        if (!entry) return;

        // Un acierto de cache se sirve en el acto: es lo que hace fluido volver sobre las
        // mismas fotos. Lo que cuesta un hilo (decodificar, listar) si espera al plazo.
        if (!entry->IsDirectory())
            m_previewFile = m_previewCache.Get(m_previewTarget, entry->modified);
        if (!m_previewFile) ArmPreview();
        return;
    }

    // La ventana crecio por encima de lo que se decodifico: rehacerla para que no se vea
    // borrosa. Solo si el original daba para mas.
    if (m_previewDue == 0 && m_previewFile && m_previewFile->kind == Preview::Kind::Image &&
        m_previewFile->downscaled && targetPx > m_previewFile->targetPx) {
        m_previewTargetPx = targetPx;
        ArmPreview();
    }
}

void App::StartPreview() {
    const DirectoryEntry* entry = Selected();
    if (!entry) return;

    // Una carpeta se previsualiza como cualquier otra columna: misma lectura, misma cache,
    // mismo descarte por ruta.
    if (entry->IsDirectory()) {
        SetPane(m_preview, m_previewTarget, std::wstring());
        return;
    }

    const unsigned long long gen = m_previewGen.load();
    const HWND hwnd = m_window.Handle();
    m_pool.Submit([this, path = m_previewTarget, modified = entry->modified,
                   targetPx = m_previewTargetPx, gen, hwnd] {
        // Cancelar antes de empezar es la unica cancelacion que hay: ni WIC ni ReadFile se
        // abortan a medias.
        if (m_previewGen.load() != gen) return;

        // WIC y el shell son COM, y en STA como en Open: hay proveedores que no admiten otra
        // cosa. No hace falta bombear mensajes para lo que se hace aqui.
        const HRESULT com =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        Preview preview = LoadPreview(path, modified, targetPx);
        if (SUCCEEDED(com)) CoUninitialize();

        preview.gen = gen;
        {
            std::lock_guard<std::mutex> lock(m_inboxMutex);
            m_previewInbox.push_back(std::move(preview));
        }
        PostMessageW(hwnd, WM_APP_WAKE, 0, 0);
    });
}

// La textura se crea en el hilo de UI y los pixeles se sueltan aqui mismo: en la cache solo
// queda la textura, que es lo unico que se cuenta contra el tope de memoria.
bool App::CreatePreviewTexture(Preview& preview) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(preview.width);
    desc.Height = static_cast<UINT>(preview.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = preview.pixels.data();
    data.SysMemPitch = static_cast<UINT>(preview.width) * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(m_gfx.Device()->CreateTexture2D(&desc, &data, &texture))) return false;
    if (FAILED(m_gfx.Device()->CreateShaderResourceView(texture.Get(), nullptr, &preview.texture)))
        return false;

    preview.pixels = std::vector<unsigned char>();  // asignar un vacio libera el buffer
    return true;
}

void App::Report(std::string message, HWND hwnd, std::wstring dirty) {
    {
        std::lock_guard<std::mutex> lock(m_inboxMutex);
        m_messages.push_back(std::move(message));
        // Por el buzon del vigilante: DrainResults ya sabe tirar la cache y agrupar el
        // refresco. Marcarla a mano ademas del vigilante es lo que hace que la vista se
        // actualice aunque ReadDirectoryChangesW no funcione (unidades de red).
        if (!dirty.empty() &&
            std::find(m_changed.begin(), m_changed.end(), dirty) == m_changed.end())
            m_changed.push_back(std::move(dirty));
    }
    PostMessageW(hwnd, WM_APP_WAKE, 0, 0);
}

void App::DrainResults() {
    std::vector<DirectoryListing> ready;
    std::vector<Preview> previews;
    std::vector<std::string> messages;
    std::vector<std::wstring> changed;
    {
        std::lock_guard<std::mutex> lock(m_inboxMutex);
        if (m_inbox.empty() && m_previewInbox.empty() && m_messages.empty() && m_changed.empty())
            return;
        ready.swap(m_inbox);
        previews.swap(m_previewInbox);
        messages.swap(m_messages);
        changed.swap(m_changed);
    }

    for (std::wstring& path : changed) {
        m_cache.Drop(path);  // lo que hay guardado de esa carpeta ya no vale
        if (std::find(m_dirty.begin(), m_dirty.end(), path) == m_dirty.end())
            m_dirty.push_back(std::move(path));
    }
    if (!m_dirty.empty() && m_refreshDue == 0)
        m_refreshDue = GetTickCount64() + kWatchDebounceMs;

    for (DirectoryListing& listing : ready) {
        std::erase(m_inFlight, listing.path);

        if (listing.error != ERROR_SUCCESS) {
            // Se queda el listado anterior en pantalla: el error va a la barra de estado.
            if (listing.path == m_current.path) SetStatus(FormatWin32Error(listing.error));
            continue;
        }
        if (listing.path == m_current.path) m_freeBytes = listing.freeBytes;

        const EntryList entries =
            std::make_shared<const std::vector<DirectoryEntry>>(std::move(listing.entries));
        m_cache.Put(listing.path, entries);

        // La ruta identifica el resultado. Si el usuario ya se fue a otro sitio ninguna
        // columna esta en esa ruta y el listado solo engorda la cache.
        for (Pane* pane : {&m_current, &m_parent, &m_preview})
            if (pane->active && pane->path == listing.path) ApplyListing(*pane, entries);
    }

    for (Preview& preview : previews) {
        if (preview.kind == Preview::Kind::Image && !CreatePreviewTexture(preview)) continue;

        // Se guarda siempre, aunque ya no sea lo que hay bajo el cursor: un decode pagado no
        // se tira. Lo que decide la generacion es si ademas se muestra.
        PreviewPtr ptr = std::make_shared<const Preview>(std::move(preview));
        m_previewCache.Put(ptr);
        if (ptr->gen == m_previewGen.load()) m_previewFile = std::move(ptr);
    }

    for (std::string& message : messages) SetStatus(std::move(message));
}

void App::SetCursor(int cursor) {
    const std::vector<DirectoryEntry>& rows = Rows(m_current.view);
    cursor = std::clamp(cursor, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    if (cursor == m_current.cursor) return;

    m_current.cursor = cursor;
    m_current.scrollToCursor = true;
    if (rows.empty()) return;
    m_current.select = rows[static_cast<size_t>(cursor)].name;
    m_cursorMemory[m_current.path] = m_current.select;
}

const DirectoryEntry* App::Selected() const {
    const std::vector<DirectoryEntry>& rows = Rows(m_current.view);
    if (rows.empty()) return nullptr;
    const int cursor = std::clamp(m_current.cursor, 0, static_cast<int>(rows.size()) - 1);
    return &rows[static_cast<size_t>(cursor)];
}

// En la raiz virtual las filas son unidades: ni se marcan ni se opera sobre ellas.
bool App::CanEdit() {
    if (!m_current.path.empty()) return true;
    SetStatus("Aqui no: elige una carpeta");
    return false;
}

void App::SetStatus(std::string message) {
    m_status = std::move(message);
    m_statusUntil = m_status.empty() ? 0 : GetTickCount64() + kStatusMs;
}

// Lo que va a la derecha de la barra: tamano y fecha de lo que hay bajo el cursor, y espacio
// libre de la unidad. Se arma cada frame; son tres concatenaciones cortas.
std::string App::StatusInfo() const {
    std::wstring info;
    if (const DirectoryEntry* entry = Selected()) {
        if (!entry->IsDirectory()) info += FormatBytes(entry->size) + L"   ";
        // Una unidad de la raiz virtual no trae fecha, y un FILETIME a cero seria 1601.
        if (entry->modified.dwLowDateTime != 0 || entry->modified.dwHighDateTime != 0)
            info += FormatTime(entry->modified);
    }
    if (m_freeBytes != 0) {
        if (!info.empty()) info += L"   ";
        info += L"libre " + FormatBytes(m_freeBytes);
    }
    return ToUtf8(info);
}

std::vector<std::wstring> App::Targets() const {
    if (!m_marked.empty()) return {m_marked.begin(), m_marked.end()};
    if (const DirectoryEntry* entry = Selected())
        return {JoinPath(m_current.path, entry->name)};
    return {};
}

// Unico sitio que habla con FileOps. El hilo del pool es "hilo propio" en lo que importa:
// no es el de UI. Una operacion larga ocupa uno de los 2-4 hilos y quedan libres para los
// listados.
//
// ponytail: sin hilo dedicado por operacion. Si algun dia varias copias a la vez dejaran sin
// hilos a los listados, la salida es un TaskPool aparte para el disco, no hilos sueltos que
// haya que hacer join al cerrar.
void App::Submit(FileOp op, std::vector<std::wstring> sources, std::wstring name) {
    const HWND hwnd = m_window.Handle();
    m_pool.Submit([this, op, sources = std::move(sources), dest = m_current.path,
                   name = std::move(name), hwnd] {
        // IFileOperation y sus dialogos son COM, y en STA como en Open: hay proveedores del
        // shell que no admiten otra cosa.
        const HRESULT com =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::string message = RunFileOp(op, sources, dest, name);
        if (SUCCEEDED(com)) CoUninitialize();

        Report(std::move(message), hwnd, dest);
    });

    // Las marcas ya han hecho su trabajo: copiar y cortar se llevaron las rutas, y borrar o
    // renombrar las dejarian apuntando a lo que ya no existe.
    m_marked.clear();
}

// El cursor sigue al nombre (fase 3): anotarlo antes de lanzar la operacion hace que el
// refresco caiga justo encima de lo que se acaba de crear o renombrar, en vez de dejar el
// cursor en la posicion numerica de antes.
void App::Follow(std::wstring name) {
    m_cursorMemory[m_current.path] = name;
    m_current.select = std::move(name);
}

void App::ToggleMark() {
    if (!CanEdit()) return;
    const DirectoryEntry* entry = Selected();
    if (!entry) return;

    const std::wstring path = JoinPath(m_current.path, entry->name);
    if (!m_marked.insert(path).second) m_marked.erase(path);
    SetCursor(m_current.cursor + 1);
}

void App::Yank(bool cut) {
    if (!CanEdit()) return;
    std::vector<std::wstring> targets = Targets();
    if (targets.empty()) return;

    m_clipboard = std::move(targets);
    m_clipboardCut = cut;
    m_marked.clear();
}

void App::Paste() {
    if (!CanEdit() || m_clipboard.empty()) return;
    Submit(m_clipboardCut ? FileOp::Move : FileOp::Copy, m_clipboard, {});
    // Mover dos veces lo mismo no significa nada; copiar otra vez si.
    if (m_clipboardCut) m_clipboard.clear();
}

void App::Remove(bool permanent) {
    if (!CanEdit()) return;
    std::vector<std::wstring> targets = Targets();
    if (targets.empty()) return;

    if (!permanent) {
        Submit(FileOp::Recycle, std::move(targets), {});
        return;
    }

    // D pregunta antes: el popup es la unica confirmacion que hay, porque a IFileOperation
    // se le pasa FOF_NOCONFIRMATION para que no vuelva a preguntar.
    m_pendingDelete = std::move(targets);
    m_confirmText =
        m_pendingDelete.size() == 1
            ? "Borrar definitivamente " + ToUtf8(LastComponent(m_pendingDelete.front())) + "?"
            : "Borrar definitivamente " + std::to_string(m_pendingDelete.size()) + " elementos?";
}

void App::BeginRename() {
    if (!CanEdit()) return;
    const DirectoryEntry* entry = Selected();
    if (!entry) return;

    m_editKind = EditKind::Rename;
    m_editTarget = JoinPath(m_current.path, entry->name);
    m_edit = EditField::State{};
    m_edit.row = m_current.cursor;
    m_edit.text = entry->nameUtf8;
    m_edit.focus = true;
    m_edit.selectStem = true;  // "foto.jpg" entra con "foto" preseleccionado
}

void App::BeginCreate() {
    if (!CanEdit()) return;
    m_editKind = EditKind::Create;
    m_edit = EditField::State{};
    m_edit.focus = true;
}

// El filtro se escribe en la barra de estado y se aplica mientras se escribe: lo recoge
// CommitEdits una vez por frame, como cualquier otra decision del campo.
void App::BeginFilter() {
    m_editKind = EditKind::Filter;
    m_edit = EditField::State{};
    m_edit.focus = true;
    m_edit.text = m_filterUtf8;  // reabrirlo continua el filtro que ya hubiera
}

void App::BeginGoto() {
    m_editKind = EditKind::Goto;
    m_edit = EditField::State{};
    m_edit.focus = true;
    // Se entra con la carpeta actual escrita: asi Tab completa desde el primer momento.
    m_edit.text = ToUtf8(m_current.path);
    if (!m_edit.text.empty() && m_edit.text.back() != '\\') m_edit.text.push_back('\\');
    m_edit.onTab = [this](std::string& text) { CompletePath(text); };
}

void App::SetFilter(std::wstring needle) {
    m_filterUtf8 = ToUtf8(needle);
    m_filter = std::move(needle);
    RebuildView(m_current);
}

// Tab: completa con el prefijo comun de las carpetas que encajan con lo escrito. Solo mira
// la cache de listados, porque esto corre dentro del callback de ImGui y el hilo de UI no
// toca el disco: una carpeta que no este se pide y el Tab siguiente ya completa.
//
// ponytail: prefijo comun y sin ciclar entre candidatos. Ciclar obliga a guardar la lista y
// por donde iba; el prefijo no guarda nada y con una sola coincidencia completa el nombre
// entero, que es el caso normal.
void App::CompletePath(std::string& text) {
    const std::wstring typed = FromUtf8(text);
    const size_t slash = typed.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;  // sin carpeta que listar no hay que completar

    std::wstring dir = NormalizePath(typed.substr(0, slash + 1));
    const std::wstring prefix = typed.substr(slash + 1);

    const EntryList entries = m_cache.Get(dir);
    if (!entries) {
        m_completePending = std::move(dir);
        return;
    }

    std::wstring common;
    int hits = 0;
    for (const DirectoryEntry& entry : *entries) {
        if (!entry.IsDirectory() || !StartsWith(entry.name, prefix)) continue;
        if (++hits == 1)
            common = entry.name;
        else
            common.resize(CommonLength(common, entry.name));
    }
    if (hits == 0 || common.size() < prefix.size()) return;

    std::wstring completed = JoinPath(dir, common);
    if (hits == 1) completed.push_back(L'\\');  // una sola: se entra y se sigue completando
    text = ToUtf8(completed);
}

// Lo que decidieron el campo de texto y el popup, fuera de las llamadas a ImGui: BuildUi solo
// marca la decision y aqui se ejecuta.
void App::CommitEdits() {
    if (m_answer != Answer::None) {
        if (m_answer == Answer::Yes) Submit(FileOp::Delete, std::move(m_pendingDelete), {});
        m_pendingDelete.clear();
        m_confirmText.clear();
        m_confirmOpen = false;
        m_answer = Answer::None;
        RequestFrames();
    }

    // El filtro no espera al Enter: cada tecla estrecha la lista.
    if (m_editKind == EditKind::Filter) {
        std::wstring needle = FromUtf8(m_edit.text);
        if (needle != m_filter) {
            SetFilter(std::move(needle));
            RequestFrames();
        }
    }
    // Lo que el autocompletado no encontro en la cache: se pide aqui, ya fuera de ImGui.
    if (!m_completePending.empty()) {
        Request(m_completePending);
        m_completePending.clear();
    }

    const EditField::Result result = m_edit.result;
    if (result == EditField::Result::None) return;

    const EditKind kind = m_editKind;
    const std::string text = std::move(m_edit.text);
    m_editKind = EditKind::None;
    m_edit = EditField::State{};
    RequestFrames();
    // Esc quita el filtro; Enter lo deja fijo (ya esta aplicado) y solo cierra el campo.
    if (kind == EditKind::Filter) {
        if (result == EditField::Result::Cancel) SetFilter({});
        return;
    }
    if (result == EditField::Result::Cancel || text.empty()) return;

    if (kind == EditKind::Goto) {
        Navigate(FromUtf8(text));  // si no existe, el error va a la barra como cualquier otro
        return;
    }

    std::wstring name = FromUtf8(text);
    if (kind != EditKind::Rename) {
        std::wstring created = name;
        SplitNewName(created);  // lo que se llama en disco no lleva la barra final
        Follow(std::move(created));
        Submit(FileOp::Create, {}, std::move(name));
    } else if (name != LastComponent(m_editTarget)) {
        Follow(name);
        Submit(FileOp::Rename, {m_editTarget}, std::move(name));
    }
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
        SetCursor(static_cast<int>(Rows(m_current.view).size()) - 1);
        return;
    case Command::HalfPageDown:
        SetCursor(m_current.cursor + halfPage);
        return;
    case Command::HalfPageUp:
        SetCursor(m_current.cursor - halfPage);
        return;
    case Command::ToggleMark:
        ToggleMark();
        return;
    case Command::Copy:
        Yank(false);
        return;
    case Command::Cut:
        Yank(true);
        return;
    case Command::Paste:
        Paste();
        return;
    case Command::Recycle:
        Remove(false);
        return;
    case Command::DeleteForever:
        Remove(true);
        return;
    case Command::Rename:
        BeginRename();
        return;
    case Command::Create:
        BeginCreate();
        return;
    case Command::Filter:
        BeginFilter();
        return;
    case Command::ClearFilter:
        if (!m_filter.empty()) SetFilter({});
        return;
    case Command::Goto:
        BeginGoto();
        return;
    case Command::ToggleHidden:
        m_showHidden = !m_showHidden;
        RebuildViews();
        return;
    case Command::NewTab:
        NewTab();
        return;
    case Command::SelectTab:
        // Una letra en vez de un digito da un indice enorme y SelectTab lo descarta.
        SelectTab(static_cast<size_t>(m_keys.letter - '1'));
        return;
    case Command::CloseTab:
        CloseTab();
        return;
    case Command::SetBookmark:
        SetBookmark(m_keys.letter);
        return;
    case Command::GotoBookmark:
        GotoBookmark(m_keys.letter);
        return;
    }
}

void App::ProcessInput() {
    // Con el popup de confirmacion abierto el teclado es suyo; el campo de texto ya lo cubre
    // io.WantTextInput dentro de Poll.
    if (!m_confirmText.empty()) return;
    Execute(Keymap::Poll(m_keys));
}

void App::RenderFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ProcessInput();  // los comandos se aplican antes de dibujar: BuildUi solo lee
    UpdatePreview();
    BuildUi();
    CommitEdits();  // y lo que BuildUi haya marcado se ejecuta aqui, no dentro de ImGui
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
    const float parentWidth = vp->WorkSize.x * kParentFraction;
    const float currentWidth = vp->WorkSize.x * kCurrentFraction;
    const float previewWidth = vp->WorkSize.x - parentWidth - currentWidth;
    const float top = vp->WorkPos.y;
    float x = vp->WorkPos.x;

    BeginPanel("##parent", ImVec2(x, top), ImVec2(parentWidth, bodyHeight));
    // Las marcas son globales, asi que ensenarlas tambien en la columna padre sale gratis.
    if (m_parent.active)
        MillerView::DrawEntries(Rows(m_parent.view), m_parent.cursor, m_parent.scrollToCursor,
                                m_parent.path, &m_marked);
    ImGui::End();
    x += parentWidth;

    BeginPanel("##current", ImVec2(x, top), ImVec2(currentWidth, bodyHeight));
    m_visibleRows = MillerView::DrawEntries(
        Rows(m_current.view), m_current.cursor, m_current.scrollToCursor, m_current.path,
        &m_marked, m_editKind == EditKind::Rename ? &m_edit : nullptr);
    ImGui::End();
    x += currentWidth;

    BeginPanel("##preview", ImVec2(x, top), ImVec2(previewWidth, bodyHeight));
    PreviewPane::Draw(Selected(), m_preview.view, m_previewFile);
    ImGui::End();

    BeginPanel("##status", ImVec2(vp->WorkPos.x, top + bodyHeight),
               ImVec2(vp->WorkSize.x, statusHeight));
    // Los numeros de pestana, solo si hay mas de una: la barra es una linea y la ruta
    // manda. El nombre de la activa ya se lee justo al lado, que es la ruta.
    if (m_tabs.size() > 1) {
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            ImGui::TextColored(i == m_tab ? Theme::kAccent : Theme::kTextDim, "%zu", i + 1);
            ImGui::SameLine();
        }
    }

    // TextUnformatted y no Text: una ruta o un nombre pueden llevar un % dentro.
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
    ImGui::TextUnformatted(m_pathUtf8.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const int count = static_cast<int>(Rows(m_current.view).size());
    ImGui::TextColored(Theme::kTextDim, "%d/%d", count == 0 ? 0 : m_current.cursor + 1, count);

    if (!m_marked.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(Theme::kAccent, "%zu marcados", m_marked.size());
    }
    if (!m_clipboard.empty()) {
        // "copiado: 3" y no "3 copiados": el resultado de la ultima operacion se pinta al
        // lado y con la misma forma se leerian como el mismo dato.
        ImGui::SameLine();
        ImGui::TextColored(Theme::kTextDim, "%s: %zu", m_clipboardCut ? "cortado" : "copiado",
                           m_clipboard.size());
    }
    if (m_showHidden) {
        ImGui::SameLine();
        ImGui::TextColored(Theme::kTextDim, "+ocultos");
    }
    if (!m_filter.empty() && m_editKind != EditKind::Filter) {
        ImGui::SameLine();
        ImGui::TextColored(Theme::kAccent, "filtro:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kAccent);
        ImGui::TextUnformatted(m_filterUtf8.c_str());  // texto del usuario: nunca como formato
        ImGui::PopStyleColor();
    }
    if (!m_status.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kAccent);
        ImGui::TextUnformatted(m_status.c_str());
        ImGui::PopStyleColor();
    }

    // El campo se come lo que queda de linea, asi que va el ultimo y se lleva por delante
    // los datos del elemento: mientras se escribe, lo que importa es lo que se escribe.
    if (m_editKind != EditKind::None && m_editKind != EditKind::Rename) {
        const char* label = m_editKind == EditKind::Create   ? "nuevo:"
                            : m_editKind == EditKind::Filter ? "buscar:"
                                                             : "ir a:";
        ImGui::SameLine();
        ImGui::TextColored(Theme::kAccent, "%s", label);
        ImGui::SameLine();
        // Al crear, acabar el nombre en barra hace carpeta; lo decide SplitNewName.
        EditField::Draw(m_edit, ImGui::GetContentRegionAvail().x, "##campo");
    } else if (const std::string info = StatusInfo(); !info.empty()) {
        // A la derecha, con coordenadas de pantalla como los tamanos de la lista, y si no
        // cabe no se pinta: manda la ruta.
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x -
                            ImGui::GetStyle().WindowPadding.x -
                            ImGui::CalcTextSize(info.c_str()).x;
        if (right > ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x) {
            ImGui::SetCursorScreenPos(ImVec2(right, ImGui::GetItemRectMin().y));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextUnformatted(info.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();

    if (m_confirmText.empty()) return;

    if (!m_confirmOpen) {
        ImGui::OpenPopup("##borrar");
        m_confirmOpen = true;
    }
    if (ImGui::BeginPopupModal("##borrar", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(m_confirmText.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Borrar") || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            m_answer = Answer::Yes;
        ImGui::SameLine();
        if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) m_answer = Answer::No;
        if (m_answer != Answer::None) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    } else {
        m_answer = Answer::No;  // ImGui lo cerro por su cuenta (Escape)
    }
}
