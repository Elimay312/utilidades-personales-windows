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
        // Sin frames pendientes dormimos aqui: 0 % de CPU hasta que llegue algo. El unico
        // plazo que puede acortar la espera es el de la vista previa; en reposo vuelve a
        // ser INFINITE. Se prefiere a SetTimer porque no toca Window ni deja nada que matar.
        if (m_pendingFrames == 0) {
            DWORD timeout = INFINITE;
            if (m_previewDue != 0) {
                const ULONGLONG now = GetTickCount64();
                timeout = m_previewDue > now ? static_cast<DWORD>(m_previewDue - now) : 0;
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

void App::Report(std::string message, HWND hwnd) {
    {
        std::lock_guard<std::mutex> lock(m_inboxMutex);
        m_messages.push_back(std::move(message));
    }
    PostMessageW(hwnd, WM_APP_WAKE, 0, 0);
}

void App::DrainResults() {
    std::vector<DirectoryListing> ready;
    std::vector<Preview> previews;
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_inboxMutex);
        if (m_inbox.empty() && m_previewInbox.empty() && m_messages.empty()) return;
        ready.swap(m_inbox);
        previews.swap(m_previewInbox);
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
    UpdatePreview();
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
    const float parentWidth = vp->WorkSize.x * kParentFraction;
    const float currentWidth = vp->WorkSize.x * kCurrentFraction;
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
    PreviewPane::Draw(Selected(), m_preview.entries, m_previewFile);
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
