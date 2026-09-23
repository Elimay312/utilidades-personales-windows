#include <windows.h>

#include <commctrl.h>  // TaskDialog
#include <shellapi.h>  // CommandLineToArgvW, Shell_NotifyIconW constants
#include <shobjidl.h>  // SetCurrentProcessExplicitAppUserModelID
#include <windowsx.h>  // GET_X_LPARAM

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "app/hotkey.h"
#include "app/isla.h"
#include "app/monitors.h"
#include "app/toast.h"
#include "app/tray.h"
#include "core/aumid.h"
#include "core/config.h"
#include "core/log.h"
#include "core/paths.h"
#include "data/store.h"
#include "sync/google.h"
#include "ui/popup_window.h"
#include "ui/settings_window.h"
#include "ui/snapshot.h"

using namespace agenda;

namespace {

constexpr int kHotkeyId = 1;
constexpr wchar_t kAppClassName[] = L"AgendaApp";
constexpr UINT_PTR kReminderTimer = 1;
// Often enough that a reminder is never more than this late, rarely enough to cost nothing.
constexpr UINT kReminderCheckMs = 20'000;
// A reminder that fell due longer ago than this -- the machine was asleep -- is not said any
// more: a night's worth of "en 10 min" on waking up would be noise about things already over.
constexpr long long kStaleReminderMinutes = 15;

// Owns everything the message loop needs. The hidden window carries a pointer to it.
struct App {
  PopupWindow popup;
  Tray tray;
  Store store;
  // Only built when there are credentials, and it holds a reference to the store, so it cannot
  // be a plain member: it has to be born after the cache is open.
  std::optional<sync::GoogleSync> sync;
  SettingsWindow settings;
  Preferences prefs;
  // The island next door (isla/), when it is running; the toast when it is not.
  Isla isla;
  // Snoozed from the island: said again at `first`, by whichever of the two is there then.
  std::vector<std::pair<long long, Reminder>> snoozed;
  HWND hwnd = nullptr;
  HMONITOR monitor = nullptr;
  long long remindedUpTo = 0;  // the WallMinute the reminders have been checked up to
};

// Shortcut names and config keys are ASCII, so widening them is this and nothing more.
std::wstring Widen(std::string_view ascii) { return std::wstring(ascii.begin(), ascii.end()); }

UINT ReadMilliseconds(const nlohmann::json& parent, const char* key, UINT fallback) {
  const auto found = parent.find(key);
  if (found == parent.end() || !found->is_number_unsigned()) return fallback;
  return found->get<UINT>();
}

bool RegisterShortcut(const App& app, const std::string& shortcut) {
  const std::optional<Hotkey> hotkey = ParseHotkey(shortcut);
  return hotkey && RegisterHotKey(app.hwnd, kHotkeyId, hotkey->mods, hotkey->vk) != FALSE;
}

long long NowWall() {
  SYSTEMTIME local{};
  GetLocalTime(&local);
  const Date today{std::chrono::year{local.wYear}, std::chrono::month{local.wMonth},
                   std::chrono::day{local.wDay}};
  return WallMinute(today, local.wHour * 60 + local.wMinute);
}

// Said without the island: a toast when Windows takes one, and the tray balloon otherwise -- a
// build run from its folder has no Start Menu shortcut, and Windows shows the balloon as a
// notification all the same.
void Toast(App& app, const Reminder& reminder, long long now) {
  const bool toast = ShowReminderToast(reminder, now, app.hwnd);
  if (!toast) {
    const std::wstring text = ReminderWhen(reminder, now) + L"\n" + ReminderSoon(reminder, now);
    app.tray.Notify(reminder.title.c_str(), text.c_str());
  }
  LogInfo(L"reminder: {} ({} min antes, {})", reminder.title, reminder.minutesBefore,
          toast ? L"toast" : L"globo de la bandeja");
}

// The island first, when it is there: never both, so one reminder is one notice.
void Deliver(App& app, const Reminder& reminder, long long now) {
  if (app.isla.Offer(reminder, now)) {
    LogInfo(L"reminder: {} ({} min antes, isla)", reminder.title, reminder.minutesBefore);
    return;
  }
  Toast(app, reminder, now);
}

// Everything that fell due since the last look, said once, and whatever was snoozed until now.
void CheckReminders(App& app) {
  if (!app.store.IsOpen()) return;
  const long long now = NowWall();
  app.isla.Withdraw(now);
  if (now <= app.remindedUpTo) return;
  const long long from = (std::max)(app.remindedUpTo, now - kStaleReminderMinutes);
  for (const Reminder& reminder : app.store.DueReminders(from, now)) Deliver(app, reminder, now);
  std::erase_if(app.snoozed, [&app, now](const std::pair<long long, Reminder>& snoozed) {
    if (snoozed.first > now) return false;
    Deliver(app, snoozed.second, now);
    return true;
  });
  app.remindedUpTo = now;
}

// What the person pressed in the island. The island only hands back the id of a button this
// side gave it; what it means is decided here.
void OnIslaAnswers(App& app) {
  const long long now = NowWall();
  for (const Isla::Answer& answer : app.isla.TakeAnswers()) {
    const Reminder& reminder = answer.reminder;
    if (answer.button.empty()) {
      // The island went away -- closed, or full -- with the reminder unanswered. It is not
      // lost: it is said the other way.
      Toast(app, reminder, now);
    } else if (answer.button == Isla::kSnooze5 || answer.button == Isla::kSnooze10) {
      app.snoozed.emplace_back(now + (answer.button == Isla::kSnooze5 ? 5 : 10), reminder);
    } else if (answer.button == Isla::kOpen) {
      app.popup.ShowEvent(reminder.uid, reminder.day);
    }
    // "hecho" is the reminder attended to: nothing more is said. The notes of an event will
    // hang from here when that module exists.
    LogInfo(L"isla: {} -> {}", reminder.title,
            answer.button.empty() ? std::wstring(L"sin respuesta") : Widen(answer.button));
  }
}

// The one thing CLAUDE.md says to stop and ask about before doing: opening the browser. The
// asking happens here, on the interface thread, in front of a window. The synchronisation
// thread is only told once the answer is yes -- it has no window to ask with, and that is why
// GoogleSync::Connect does not ask.
void ConnectToGoogle(HWND owner, App& app) {
  if (!app.sync) return;

  const std::wstring title(T(L"Conectar Agenda con Google", L"Connect Agenda to Google"));
  const std::wstring main(T(L"Se va a abrir tu navegador", L"Your browser is about to open"));
  const std::wstring body(T(
      L"Agenda abrirá la página de permisos de Google y esperará la respuesta en 127.0.0.1. "
      L"Pide leer y escribir eventos y tareas, y leer la lista de calendarios para saber sus "
      L"nombres y sus colores.\n\n"
      L"Como la aplicación es tuya y no está verificada, Google mostrará un aviso: entra en "
      L"«Configuración avanzada» y continúa.",
      L"Agenda will open Google's permission page and wait for the answer on 127.0.0.1. It "
      L"asks to read and write events and tasks, and to read the list of calendars to learn "
      L"their names and colours.\n\n"
      L"As the app is yours and not verified, Google will show a warning: open "
      L"“Advanced” and continue."));

  int pressed = 0;
  const HRESULT asked = TaskDialog(owner, nullptr, title.c_str(), main.c_str(), body.c_str(),
                                   TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, TD_INFORMATION_ICON,
                                   &pressed);
  if (FAILED(asked)) {
    // TaskDialog needs common controls v6 in the manifest. If that ever goes away the question
    // still gets asked: what must not happen is the browser opening without one.
    LogError(L"google: TaskDialog falló (0x{:08X}), se pregunta con un MessageBox",
             static_cast<unsigned>(asked));
    pressed = MessageBoxW(owner, body.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
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
            if (state.connected) {
              state.calendars = app->store.Calendars(/*tasklists=*/false);
              state.accounts = app->store.Accounts();
            }
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
            case kTraySettings:
              app->settings.Show(TargetMonitor(app->monitor));
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
      app->tray.Warn(T(L"Agenda: se acabó el permiso de Google",
                       L"Agenda: Google's permission ran out").data(),
                     T(L"Vuelve a conectar desde este icono. Lo que hayas escrito está guardado "
                       L"y subirá en cuanto vuelvas a dar permiso.",
                       L"Connect again from this icon. What you wrote is saved and goes up as "
                       L"soon as you give permission again.").data());
      LogInfo(L"google: permiso caducado o revocado, hay que volver a conectar");
      return 0;

    case kIslaAnswerMessage:
      OnIslaAnswers(*app);
      return 0;

    case kReminderOpenMessage:
      app->popup.ShowDay(DayOfWall(static_cast<long long>(wparam) * 1440));
      return 0;

    case WM_TIMER:
      if (wparam == kReminderTimer) {
        CheckReminders(*app);
        return 0;
      }
      break;

    case WM_POWERBROADCAST:
      // Back from sleep: look now rather than up to twenty seconds from now.
      if (wparam == PBT_APMRESUMEAUTOMATIC) CheckReminders(*app);
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

  // Read before anything is drawn, the snapshot included: the language is part of the design.
  const nlohmann::json config = LoadConfig();
  const Preferences prefs = ReadPreferences(config);
  CurrentLang() = prefs.lang;

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

  // The id the installer's Start Menu shortcut carries, so the toasts, the settings window's
  // taskbar button and the shortcut are one application to Windows.
  SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);

  PopupWindow::Timing timing;
  if (const auto popup = config.find("popup"); popup != config.end() && popup->is_object()) {
    timing.openMs = ReadMilliseconds(*popup, "openMs", timing.openMs);
    timing.closeMs = ReadMilliseconds(*popup, "closeMs", timing.closeMs);
  }

  // Pinned only when --monitor asks for one; otherwise nullptr, and each opening follows the
  // mouse to whichever monitor it is on.
  HMONITOR monitor = nullptr;
  if (options.monitor != 0) {
    const std::optional<HMONITOR> found = FindMonitorByDisplayNumber(options.monitor);
    if (!found) {
      // No silent fallback to another monitor: CLAUDE.md forbids it.
      LogError(L"\\\\.\\DISPLAY{} is not connected, refusing to start", options.monitor);
      return 2;
    }
    monitor = *found;
  }

  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(TargetMonitor(monitor), &info)) {
    LogError(L"GetMonitorInfoW failed with error {}", GetLastError());
    return 2;
  }

  const RECT& work = info.rcWork;
  LogInfo(L"monitor {}: work area {},{} {}x{}", std::wstring_view(info.szDevice), work.left,
          work.top, work.right - work.left, work.bottom - work.top);

  App app;
  app.prefs = prefs;
  app.monitor = monitor;

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
  app.hwnd = hwnd;
  app.isla.SetNotifyWindow(hwnd);
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
  ShowWindow(hwnd, SW_SHOWNA);

  app.popup.SetPanelOverride(D2D1_SIZE_F{static_cast<float>(options.panelWidth),
                                         static_cast<float>(options.panelHeight)});
  app.popup.SetPreferences(&app.prefs);
  app.popup.SetOpenSettings([&app] { app.settings.Show(TargetMonitor(app.monitor)); });
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
      const auto text = [&google](const char* key) {
        const auto found = google->find(key);
        return found != google->end() && found->is_string() ? found->get<std::string>()
                                                             : std::string();
      };
      credentials.clientId = text("clientId");
      credentials.clientSecret = text("clientSecret");
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
    app.tray.Warn(T(L"Agenda: no se pudo abrir la agenda", L"Agenda: the agenda could not open")
                      .data(),
                  T(L"Lo que escribas no se guardará. El motivo está en el log, dentro de "
                    L"%LOCALAPPDATA%\\Agenda\\logs.",
                    L"What you write will not be saved. The reason is in the log, inside "
                    L"%LOCALAPPDATA%\\Agenda\\logs.")
                      .data());
  }

  app.settings.Init(
      instance, &app.prefs, &app.store, app.sync ? &*app.sync : nullptr,
      SettingsWindow::Hooks{
          [&app](const std::string& shortcut) {
            UnregisterHotKey(app.hwnd, kHotkeyId);
            if (RegisterShortcut(app, shortcut)) return true;
            RegisterShortcut(app, app.prefs.hotkey);
            return false;
          },
          [&app](bool paused) {
            UnregisterHotKey(app.hwnd, kHotkeyId);
            if (!paused) RegisterShortcut(app, app.prefs.hotkey);
          },
          [&app] { app.popup.PreferencesChanged(); },
          [&app](HWND owner) { ConnectToGoogle(owner, app); }});

  if (!ParseHotkey(app.prefs.hotkey)) {
    LogError(L"hotkey: '{}' is not a shortcut I understand, using {}", Widen(app.prefs.hotkey),
             Widen(kDefaultHotkey));
    app.prefs.hotkey = std::string(kDefaultHotkey);
  }
  if (!RegisterShortcut(app, app.prefs.hotkey)) {
    LogError(L"hotkey: RegisterHotKey({}) failed with error {}", Widen(app.prefs.hotkey),
             GetLastError());
    const std::wstring shortcut = Widen(app.prefs.hotkey);
    const std::wstring text = std::vformat(
        T(L"Otra aplicación ya usa {}. Abre Agenda desde este icono y elige otro atajo en "
          L"Configuración.",
          L"Another app already uses {}. Open Agenda from this icon and pick another shortcut "
          L"in Settings."),
        std::make_wformat_args(shortcut));
    app.tray.Warn(T(L"Agenda: el atajo está ocupado", L"Agenda: the shortcut is taken").data(),
                  text.c_str());
  } else {
    LogInfo(L"hotkey: {} registered", Widen(app.prefs.hotkey));
  }

  // One minute back, so something due the minute Agenda starts -- at logon, say -- is not
  // missed by starting a few seconds too late.
  app.remindedUpTo = NowWall() - 1;
  SetTimer(hwnd, kReminderTimer, kReminderCheckMs, nullptr);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  // The network goes first. Cancelling closes the request handle from this thread, so quitting
  // in the middle of a pass is a moment and not the thirty seconds of a receive timeout.
  if (app.sync) app.sync->Stop();
  app.isla.Stop();
  UnregisterHotKey(hwnd, kHotkeyId);
  app.tray.Remove();
  CoUninitialize();
  LogInfo(L"exit");
  return 0;
}
