#include <windows.h>

#include <shellapi.h>  // CommandLineToArgvW

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "core/config.h"
#include "core/hotkey.h"
#include "core/log.h"
#include "core/options.h"
#include "core/paths.h"
#include "ui/panel_window.h"
#include "ui/snapshot.h"

using namespace panel;

namespace {

constexpr int kHotkeyId = 1;
constexpr wchar_t kHostClassName[] = L"PanelDeControlHost";

// Shortcut names are ASCII, so widening them is this and nothing more.
std::wstring Widen(std::string_view ascii) { return std::wstring(ascii.begin(), ascii.end()); }

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_HOTKEY && wparam == kHotkeyId) {
    auto* window = reinterpret_cast<PanelWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window != nullptr) window->Toggle();
    return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  LogInit();

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const Options options = ParseOptions(argc, argv, EnvVar(L"PANEL_DEV_MONITOR"));
  LocalFree(argv);
  if (!options.error.empty()) {
    LogError(L"{}", options.error);
    return 2;
  }

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
    LogError(L"CoInitializeEx failed");
    return 2;
  }

  // Read before anything is drawn, the snapshot included: the language is part of the design.
  const Preferences prefs = ReadPreferences(LoadConfig());
  CurrentLang() = prefs.lang;
  const std::wstring theme = options.theme.empty() ? prefs.theme : options.theme;

  // The snapshot draws offscreen, so it needs neither a monitor nor the instance lock: it has
  // to work while the panel is running.
  if (!options.snapshotView.empty()) {
    const bool written = RenderSnapshot(options.snapshotView, theme, options.snapshotOut);
    CoUninitialize();
    return written ? 0 : 2;
  }

  const HANDLE onlyInstance = CreateMutexW(nullptr, TRUE, L"Local\\PanelSingleInstance");
  if (onlyInstance == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
    LogInfo(L"another instance is already running, exiting");
    return 1;
  }

  // Pinned only when --monitor asks for one; otherwise each opening follows the mouse.
  HMONITOR monitor = nullptr;
  if (options.monitor != 0) {
    const std::optional<HMONITOR> found = FindMonitorByDisplayNumber(options.monitor);
    if (!found) {
      // No silent fallback to another monitor (CLAUDE.md, monitor rule).
      LogError(L"\\\\.\\DISPLAY{} is not connected, refusing to start", options.monitor);
      return 2;
    }
    monitor = *found;
  }

  PanelWindow window;
  if (!window.Create(instance, monitor, theme)) return 2;

  // A window with no size and no taskbar button, so the hotkey has somewhere to arrive that is
  // not the panel itself.
  WNDCLASSEXW hostClass{};
  hostClass.cbSize = sizeof(hostClass);
  hostClass.lpfnWndProc = HostWndProc;
  hostClass.hInstance = instance;
  hostClass.lpszClassName = kHostClassName;
  if (RegisterClassExW(&hostClass) == 0) {
    LogError(L"RegisterClassExW failed with error {}", GetLastError());
    return 2;
  }
  HWND host = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClassName, L"Panel", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, instance, nullptr);
  if (host == nullptr) {
    LogError(L"CreateWindowExW failed with error {}", GetLastError());
    return 2;
  }
  SetWindowLongPtrW(host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&window));

  std::string shortcut = prefs.hotkey;
  std::optional<Hotkey> hotkey = ParseHotkey(shortcut);
  if (!hotkey) {
    LogError(L"hotkey: '{}' is not a shortcut I understand, using {}", Widen(shortcut),
             Widen(kDefaultHotkey));
    shortcut = std::string(kDefaultHotkey);
    hotkey = ParseHotkey(shortcut);
  }
  if (!RegisterHotKey(host, kHotkeyId, hotkey->mods, hotkey->vk)) {
    // Nothing would open the panel, so it opens once, now, and says why inside itself. The
    // right-click menu is right there to fix panel.json.
    LogError(L"hotkey: RegisterHotKey({}) failed with error {}", Widen(shortcut), GetLastError());
    const std::wstring taken = Widen(shortcut);
    window.SetNotice(std::vformat(T(L"Otra aplicación ya usa {}. Cámbialo en panel.json (clic derecho).",
                                    L"Another app already uses {}. Change it in panel.json (right click)."),
                                  std::make_wformat_args(taken)));
    window.Show();
  } else {
    LogInfo(L"hotkey: {} registered", Widen(shortcut));
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  UnregisterHotKey(host, kHotkeyId);
  window.Shutdown();
  CoUninitialize();
  LogInfo(L"exit");
  return 0;
}
