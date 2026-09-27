#include "app.h"

#include "core/log.h"

namespace dock {
namespace {

constexpr wchar_t kHostClass[] = L"DockHost";
constexpr UINT_PTR kDisplayTimer = 1;
// Enchufar una pantalla manda varios WM_DISPLAYCHANGE seguidos; se reconstruye una vez.
constexpr UINT kDisplayDebounceMs = 400;

BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM out) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(handle, &info)) {
    reinterpret_cast<std::vector<Monitor>*>(out)->push_back({handle, info.szDevice, info.rcMonitor, info.rcWork});
  }
  return TRUE;
}

}  // namespace

App::App(std::filesystem::path configPath) : configPath_(std::move(configPath)) {}

int App::Run() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = HostProc;
  wc.hInstance = instance;
  wc.lpszClassName = kHostClass;
  if (!RegisterClassExW(&wc)) {
    LogError(L"[dock] RegisterClassExW(anfitriona) falló: {}", GetLastError());
    return 2;
  }
  // De nivel superior y oculta, no message-only: las difusiones (WM_DISPLAYCHANGE,
  // TaskbarCreated) no llegan a las message-only.
  host_ = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"Dock", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                          instance, this);
  if (!host_) {
    LogError(L"[dock] CreateWindowExW(anfitriona) falló: {}", GetLastError());
    return 2;
  }

  shellHookMessage_ = RegisterWindowMessageW(L"SHELLHOOK");
  taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
  if (!RegisterShellHookWindow(host_)) LogError(L"[shell] RegisterShellHookWindow falló: {}", GetLastError());

  Rebuild();

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  LogInfo(L"[dock] salida limpia");
  return 0;
}

void App::Quit() {
  if (host_) PostMessageW(host_, WM_CLOSE, 0, 0);
}

void App::Rebuild() {
  // La config se relee aquí: el de C# reconstruía con la que había leído al arrancar y
  // perdía lo que se hubiera editado desde entonces.
  config_ = LoadConfig(configPath_);
  docks_.clear();

  std::vector<Monitor> monitors;
  EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
  for (const Monitor& monitor : monitors) {
    auto window = std::make_unique<DockWindow>(*this, monitor, config_);
    if (window->Create()) docks_.push_back(std::move(window));
  }
  LogInfo(L"[dock] {} monitor(es)", docks_.size());
}

LRESULT CALLBACK App::HostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self || !self->host_) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->HandleHost(message, wparam, lparam);
}

LRESULT App::HandleHost(UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == shellHookMessage_ && shellHookMessage_) {
    // El bit 0x8000 es "RUDEAPPACTIVATED": a efectos del dock, una activación más.
    if ((wparam & 0x7FFF) == HSHELL_WINDOWACTIVATED)
      for (auto& dockWindow : docks_) dockWindow->OnWindowActivated();
    return 0;
  }
  if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_) {
    for (auto& dockWindow : docks_) dockWindow->OnTaskbarCreated();
    return 0;
  }

  switch (message) {
    case WM_DISPLAYCHANGE:
      SetTimer(host_, kDisplayTimer, kDisplayDebounceMs, nullptr);
      return 0;

    case WM_TIMER:
      if (wparam == kDisplayTimer) {
        KillTimer(host_, kDisplayTimer);
        LogInfo(L"[dock] cambiaron las pantallas, reconstruyendo");
        Rebuild();
      }
      return 0;

    // Cerrar sesión no pasa por WM_CLOSE: sin esto las franjas reservadas se quedan.
    case WM_ENDSESSION:
      if (wparam) docks_.clear();
      return 0;

    case WM_CLOSE:
      docks_.clear();
      DeregisterShellHookWindow(host_);
      DestroyWindow(host_);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(host_, message, wparam, lparam);
}

}  // namespace dock
