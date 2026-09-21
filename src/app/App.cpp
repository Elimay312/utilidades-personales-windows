#include "app/App.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

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

bool App::Init() {
    if (!m_window.Create(L"Rayo", 1280, 800)) return false;
    if (!m_gfx.Create(m_window.Handle())) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // nada de imgui.ini

    if (!ImGui_ImplWin32_Init(m_window.Handle())) return false;
    if (!ImGui_ImplDX11_Init(m_gfx.Device(), m_gfx.Context())) return false;

    Theme::Apply(m_window.DpiScale());
    Theme::LoadFont(m_window.DpiScale());

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
        Theme::Apply(scale);
        Theme::LoadFont(scale);
        ImGui_ImplDX11_InvalidateDeviceObjects();  // el atlas se recrea en el siguiente frame
        RequestFrames();
    };

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

void App::RenderFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    BuildUi();
    ImGui::Render();

    m_gfx.Clear(&Theme::kBackground.x);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_gfx.Present();
}

void App::BuildUi() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        m_running = false;

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
    ImGui::TextColored(Theme::kTextDim, "Users");
    ImGui::TextColored(Theme::kAccent, "elima");
    ImGui::TextColored(Theme::kTextDim, "Public");
    ImGui::End();
    x += parentWidth;

    BeginPanel("##current", ImVec2(x, top), ImVec2(currentWidth, bodyHeight));
    static const char* kSample[] = {"Desktop", "Documents", "Downloads", "Pictures", "notas.txt"};
    for (int i = 0; i < IM_ARRAYSIZE(kSample); ++i)
        ImGui::Selectable(kSample[i], i == 2);
    ImGui::End();
    x += currentWidth;

    BeginPanel("##preview", ImVec2(x, top), ImVec2(previewWidth, bodyHeight));
    ImGui::TextColored(Theme::kTextDim, "Vista previa");
    ImGui::Separator();
    ImGui::TextUnformatted("Fase 1: ventana, D3D11, ImGui y bucle por eventos.");
    ImGui::End();

    BeginPanel("##status", ImVec2(vp->WorkPos.x, top + bodyHeight),
               ImVec2(vp->WorkSize.x, statusHeight));
    ImGui::TextColored(Theme::kTextDim, R"(C:\Users\elima   3/5   DPI %.0f%%   q = salir)",
                       m_window.DpiScale() * 100.0f);
    ImGui::End();
}
