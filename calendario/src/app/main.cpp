#include <windows.h>

#include <commctrl.h>  // TaskDialog
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
#include "data/store.h"
#include "sync/google.h"
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
  Store store;
  // Only built when there are credentials, and it holds a reference to the store, so it cannot
  // be a plain member: it has to be born after the cache is open.
  std::optional<sync::GoogleSync> sync;
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

// The one thing CLAUDE.md says to stop and ask about before doing: opening the browser. The
// asking happens here, on the interface thread, in front of a window. The synchronisation
// thread is only told once the answer is yes -- it has no window to ask with, and that is why
// GoogleSync::Connect does not ask.
void ConnectToGoogle(HWND owner, App& app) {
  if (!app.sync) return;

  static constexpr wchar_t kTitle[] = L"Conectar Agenda con Google";
  static constexpr wchar_t kMain[] = L"Se va a abrir tu navegador";
  static constexpr wchar_t kBody[] =
      L"Agenda abrirá la página de permisos de Google y esperará la respuesta en 127.0.0.1. "
      L"Pide leer y escribir eventos y tareas, y leer la lista de calendarios para saber sus "
      L"nombres y sus colores.\n\n"
      L"Como la aplicación es tuya y no está verificada, Google mostrará un aviso: entra en "
      L"«Configuración avanzada» y continúa.";

  int pressed = 0;
  const HRESULT asked = TaskDialog(owner, nullptr, kTitle, kMain, kBody,
                                   TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, TD_INFORMATION_ICON,
                                   &pressed);
  if (FAILED(asked)) {
    // TaskDialog needs common controls v6 in the manifest. If that ever goes away the question
    // still gets asked: what must not happen is the browser opening without one.
    LogError(L"google: TaskDialog falló (0x{:08X}), se pregunta con un MessageBox",
             static_cast<unsigned>(asked));
    pressed = MessageBoxW(owner, kBody, kTitle, MB_YESNO | MB_ICONQUESTION);
  }
  if (pressed != IDYES) {
    LogInfo(L"google: el usuario dijo que no, el navegador no se abre");
    return;
  }
  app.sync->Connect();
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
          // Asked for at the moment of the click and not kept: which calendars there are, and
          // which one is the default, change underneath the menu on every pass.
          TrayState state;
          if (app->sync) {
            state.configured = app->sync->Configured();
            state.connected = app->sync->Connected();
            if (state.connected) state.calendars = app->store.Calendars(/*tasklists=*/false);
          }

          const UINT command = app->tray.ShowMenu(at, state);
          if (command >= kTrayCalendarFirst) {
            const size_t index = command - kTrayCalendarFirst;
            if (index < state.calendars.size()) {
              app->store.SetDefaultCalendar(state.calendars[index].id, /*isTask=*/false);
            }
            return 0;
          }
          switch (command) {
            case kTrayOpen:
              app->popup.Show();
              break;
            case kTrayConnect:
              ConnectToGoogle(hwnd, *app);
              break;
            case kTrayDisconnect:
              if (app->sync) app->sync->Disconnect();
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

    case sync::kSyncLostAccountMessage:
      // The one thing in this phase worth interrupting somebody for. Everything else -- a slow
      // pass, no network, a rejected change -- resolves itself or waits; this one stays broken
      // until a person clicks something, and until then nothing written here reaches the phone.
      app->tray.Warn(L"Agenda: se acabó el permiso de Google",
                     L"Vuelve a conectar desde este icono. Lo que hayas escrito está guardado "
                     L"y subirá en cuanto vuelvas a dar permiso.");
      LogInfo(L"google: permiso caducado o revocado, hay que volver a conectar");
      return 0;

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
                       options.text, options.snapshotOut);
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

  // The cache opens before the hotkey is even registered, so the first time the popup appears
  // it already has a day to paint. Its worker posts to the popup's window, which is the one
  // that has to redraw.
  if (app.store.Open(AppDataDir() / L"agenda.db")) {
    app.store.SetNotifyWindow(app.popup.hwnd());
    app.popup.SetStore(&app.store);

    // The credentials the user pasted into config.local.json. Missing is the normal state of a
    // fresh install: Agenda stays a calendar that lives on this machine, and the tray menu says
    // nothing about Google rather than offering something that cannot work.
    sync::OAuthConfig credentials;
    if (const auto google = config.find("google");
        google != config.end() && google->is_object()) {
      credentials.clientId = ReadString(*google, "clientId", "");
      credentials.clientSecret = ReadString(*google, "clientSecret", "");
    }
    app.sync.emplace(app.store, std::move(credentials));
    // To this window and not the popup's: what it has to say is said with a tray balloon, and
    // the tray icon hangs off this one.
    app.sync->SetNotifyWindow(hwnd);
    app.popup.SetSync(&*app.sync);
    app.sync->Start();
  } else {
    // Said out loud and not swallowed: a popup that quietly forgets everything typed into it
    // is worse than one that admits it cannot save.
    app.tray.Warn(L"Agenda: no se pudo abrir la agenda",
                  L"Lo que escribas no se guardará. El motivo está en el log, dentro de "
                  L"%LOCALAPPDATA%\\Agenda\\logs.");
  }

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

  // The network goes first. Cancelling closes the request handle from this thread, so quitting
  // in the middle of a pass is a moment and not the thirty seconds of a receive timeout.
  if (app.sync) app.sync->Stop();
  UnregisterHotKey(hwnd, kHotkeyId);
  app.tray.Remove();
  CoUninitialize();
  LogInfo(L"exit");
  return 0;
}
