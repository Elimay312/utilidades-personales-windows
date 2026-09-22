#include <windows.h>

#include <shellapi.h>  // CommandLineToArgvW, Shell_NotifyIconW constants
#include <windowsx.h>  // GET_X_LPARAM

#include <optional>
#include <string>
#include <string_view>

#include "app/hotkey.h"
#include "app/monitors.h"
#include "app/tray.h"
#include "core/config.h"
#include "core/log.h"
#include "core/paths.h"
#include "ui/popup_window.h"
#include "ui/snapshot.h"

using namespace agenda;

namespace {

constexpr int kHotkeyId = 1;
constexpr wchar_t kAppClassName[] = L"AgendaApp";

// Owns everything the message loop needs. The hidden window carries a pointer to it.
struct App {
  PopupWindow popup;
  Tray tray;
};

// Shortcut names and config keys are ASCII, so widening them is this and nothing more.
std::wstring Widen(std::string_view ascii) { return std::wstring(ascii.begin(), ascii.end()); }

// nlohmann throws when value() finds the wrong type, and a hand-edited config is exactly where
// that happens, so every read checks the type first and falls back quietly.
std::string ReadString(const nlohmann::json& parent, const char* key, std::string_view fallback) {
  const auto found = parent.find(key);
  if (found == parent.end() || !found->is_string()) return std::string(fallback);
  return found->get<std::string>();
}

UINT ReadMilliseconds(const nlohmann::json& parent, const char* key, UINT fallback) {
  const auto found = parent.find(key);
  if (found == parent.end() || !found->is_number_unsigned()) return fallback;
  return found->get<UINT>();
}

LRESULT CALLBACK AppWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);

  switch (message) {
    case WM_HOTKEY:
      if (wparam == kHotkeyId) {
        app->popup.Toggle();
        return 0;
      }
      break;

    case kTrayMessage:
      switch (LOWORD(lparam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP:
          app->popup.Show();
          return 0;

        case WM_CONTEXTMENU: {
          const POINT at{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};
          switch (app->tray.ShowMenu(at)) {
            case kTrayOpen:
              app->popup.Show();
              break;
            case kTrayExit:
              PostQuitMessage(0);
              break;
            default:
              break;
          }
          return 0;
        }

        default:
          break;
      }
      break;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  LogInit();

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const Options options = ParseOptions(argc, argv, EnvVar(L"AGENDA_DEV_MONITOR"));
  LocalFree(argv);
  if (!options.error.empty()) {
    LogError(L"{}", options.error);
    return 2;
  }

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
    LogError(L"CoInitializeEx failed");
    return 2;
  }

  // The snapshot draws offscreen, so it needs neither a monitor nor the instance lock: it has
  // to work while the app is running.
  if (!options.snapshotView.empty()) {
    const bool written = RenderSnapshot(options.snapshotView, options.theme,
                       D2D1_SIZE_F{static_cast<float>(options.panelWidth),
                                   static_cast<float>(options.panelHeight)},
                       options.snapshotOut);
    CoUninitialize();
    return written ? 0 : 2;
  }

  const HANDLE onlyInstance = CreateMutexW(nullptr, TRUE, L"Local\\AgendaSingleInstance");
  const bool alreadyRunning = onlyInstance == nullptr || GetLastError() == ERROR_ALREADY_EXISTS;
  if (alreadyRunning) {
    LogInfo(L"another instance is already running, exiting");
    return 1;
  }

  const nlohmann::json config = LoadConfig();
  std::string shortcut = ReadString(config, "hotkey", kDefaultHotkey);

  PopupWindow::Timing timing;
  if (const auto popup = config.find("popup"); popup != config.end() && popup->is_object()) {
    timing.openMs = ReadMilliseconds(*popup, "openMs", timing.openMs);
    timing.closeMs = ReadMilliseconds(*popup, "closeMs", timing.closeMs);
  }

  HMONITOR monitor = nullptr;
  if (options.monitor == 0) {
    monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  } else if (const std::optional<HMONITOR> found = FindMonitorByDisplayNumber(options.monitor)) {
    monitor = *found;
  } else {
    // No silent fallback to another monitor: CLAUDE.md forbids it.
    LogError(L"\\\\.\\DISPLAY{} is not connected, refusing to start", options.monitor);
    return 2;
  }

  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    LogError(L"GetMonitorInfoW failed with error {}", GetLastError());
    return 2;
  }

  const RECT& work = info.rcWork;
  LogInfo(L"monitor {}: work area {},{} {}x{}", std::wstring_view(info.szDevice), work.left,
          work.top, work.right - work.left, work.bottom - work.top);

  App app;

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = AppWndProc;
  windowClass.hInstance = instance;
  windowClass.lpszClassName = kAppClassName;
  if (RegisterClassExW(&windowClass) == 0) {
    LogError(L"RegisterClassExW failed with error {}", GetLastError());
    return 2;
  }

  // A window with no size and no taskbar button. It exists so the hotkey and the tray icon
  // have somewhere to send their messages, and so the tray menu has a window it can bring to
  // the foreground without that showing the popup.
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kAppClassName, L"Agenda", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, instance, nullptr);
  if (hwnd == nullptr) {
    LogError(L"CreateWindowExW failed with error {}", GetLastError());
    return 2;
  }
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
  ShowWindow(hwnd, SW_SHOWNA);

  app.popup.SetPanelOverride(D2D1_SIZE_F{static_cast<float>(options.panelWidth),
                                         static_cast<float>(options.panelHeight)});
  if (!app.popup.Create(instance, monitor, timing, options.theme)) return 2;
  app.tray.Add(instance, hwnd);

  std::optional<Hotkey> hotkey = ParseHotkey(shortcut);
  if (!hotkey) {
    LogError(L"hotkey: '{}' is not a shortcut I understand, using {}", Widen(shortcut),
             Widen(kDefaultHotkey));
    shortcut = std::string(kDefaultHotkey);
    hotkey = ParseHotkey(shortcut);
  }
  if (!RegisterHotKey(hwnd, kHotkeyId, hotkey->mods, hotkey->vk)) {
    LogError(L"hotkey: RegisterHotKey({}) failed with error {}", Widen(shortcut), GetLastError());
    const std::wstring text = L"Otra aplicación ya usa " + Widen(shortcut) +
                              L". Abre Agenda desde este icono y cambia \"hotkey\" en "
                              L"config.json.";
    app.tray.Warn(L"Agenda: el atajo está ocupado", text.c_str());
  } else {
    LogInfo(L"hotkey: {} registered", Widen(shortcut));
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  UnregisterHotKey(hwnd, kHotkeyId);
  app.tray.Remove();
  CoUninitialize();
  LogInfo(L"exit");
  return 0;
}
