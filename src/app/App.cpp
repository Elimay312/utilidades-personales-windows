#include "app/App.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
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
    m_path = path;
    m_pathUtf8 = ToUtf8(m_path);
    m_status.clear();

    const unsigned long long generation = ++m_generation;
    // El HWND se copia por valor: la UI lo pone a null en WM_DESTROY y un hilo de
    // trabajo no debe leer ese miembro mientras tanto.
    const HWND hwnd = m_window.Handle();
    m_pool.Submit([this, path = std::move(path), generation, hwnd] {
        DirectoryListing listing = ReadDirectory(path, generation);
        {
            std::lock_guard<std::mutex> lock(m_inboxMutex);
            m_inbox.push_back(std::move(listing));
        }
        PostMessageW(hwnd, WM_APP_WAKE, 0, 0);
    });
}

void App::DrainResults() {
    std::vector<DirectoryListing> ready;
    {
        std::lock_guard<std::mutex> lock(m_inboxMutex);
        if (m_inbox.empty()) return;
        ready.swap(m_inbox);
    }

    for (DirectoryListing& listing : ready) {
        if (listing.generation != m_generation) continue;  // el usuario ya se fue a otro sitio

        if (listing.error != ERROR_SUCCESS) {
            // Se queda el listado anterior en pantalla: el error va a la barra de estado.
            m_status = FormatWin32Error(listing.error);
            continue;
        }
        m_entries = std::move(listing.entries);
        m_cursor = 0;
        m_scrollToCursor = true;
        m_status.clear();
    }
}

void App::Execute(Command command) {
    const int count = static_cast<int>(m_entries.size());
    const int halfPage = std::max(1, m_visibleRows / 2);
    int cursor = m_cursor;

    switch (command) {
    case Command::None:
        return;
    case Command::Quit:
        m_running = false;
        return;
    case Command::MoveDown:
        cursor += 1;
        break;
    case Command::MoveUp:
        cursor -= 1;
        break;
    case Command::MoveTop:
        cursor = 0;
        break;
    case Command::MoveBottom:
        cursor = count - 1;
        break;
    case Command::HalfPageDown:
        cursor += halfPage;
        break;
    case Command::HalfPageUp:
        cursor -= halfPage;
        break;
    }

    cursor = std::clamp(cursor, 0, std::max(0, count - 1));
    if (cursor != m_cursor) {
        m_cursor = cursor;
        m_scrollToCursor = true;
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
    ImGui::TextColored(Theme::kTextDim, "Fase 3");
    ImGui::End();
    x += parentWidth;

    BeginPanel("##current", ImVec2(x, top), ImVec2(currentWidth, bodyHeight));
    MillerView::DrawEntries(m_entries, m_cursor, m_scrollToCursor, m_visibleRows);
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
    ImGui::TextColored(Theme::kTextDim, "%d/%d", m_entries.empty() ? 0 : m_cursor + 1,
                       static_cast<int>(m_entries.size()));
    if (!m_status.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kAccent);
        ImGui::TextUnformatted(m_status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
}
