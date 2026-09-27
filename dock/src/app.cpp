#include "app.h"

#include <chrono>
#include <set>

#include "core/hotkey.h"
#include "core/log.h"
#include "system/autostart.h"
#include "system/icons.h"
#include "system/inventory.h"

namespace dock {

struct IconResult {
  unsigned round = 0;
  IconSet icons;
};

namespace {

constexpr wchar_t kHostClass[] = L"DockHost";
constexpr UINT kIconsReady = WM_APP + 1;
constexpr int kProfileHotkey = 1;  // el id del atajo en la anfitriona: solo hay uno
constexpr UINT_PTR kDisplayTimer = 1;
constexpr UINT_PTR kReloadTimer = 2;
constexpr UINT_PTR kRunningTimer = 3;
constexpr UINT_PTR kSafetyTimer = 4;
// Al abrir una app los avisos del shell llegan a rachas: se barre una vez al acabar.
constexpr UINT kRunningDebounceMs = 400;
// Red de seguridad para lo que los avisos no cubren: una ventana que nace sin título, se
// titula después y nadie la activa. Medido en C#: del reloj a 3 barridos por segundo a
// avisos, de 5,07 a 1,90% de un núcleo en reposo.
constexpr UINT kSafetyMs = 10000;
// Enchufar una pantalla manda varios WM_DISPLAYCHANGE seguidos; se reconstruye una vez.
constexpr UINT kDisplayDebounceMs = 400;
// Los editores no guardan de una vez: varios avisos por guardado, y a veces truncan antes de
// escribir. Sin esperar se leería un JSON a medias.
constexpr UINT kReloadDebounceMs = 250;

// Desde que arrancó el proceso, para dejar constancia de cuánto tardan los iconos en verse.
double MsSinceStart() {
  FILETIME created{}, exited{}, kernel{}, user{}, now{};
  GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  GetSystemTimePreciseAsFileTime(&now);
  const auto ticks = [](FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
  return (ticks(now) - ticks(created)) / 10000.0;
}

FILETIME Stamp(const std::filesystem::path& file) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) return {};
  return data.ftLastWriteTime;
}

BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM out) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(handle, &info)) {
    reinterpret_cast<std::vector<Monitor>*>(out)->push_back({handle, info.szDevice, info.rcMonitor, info.rcWork});
  }
  return TRUE;
}

}  // namespace

// dock.local.json vive junto a dock.json: con --config de pruebas, ninguno de los dos es el
// del usuario.
App::App(std::filesystem::path configPath)
    : configPath_(std::move(configPath)), localPath_(configPath_.parent_path() / L"dock.local.json") {}

App::~App() {
  if (watch_ != INVALID_HANDLE_VALUE) FindCloseChangeNotification(watch_);
}

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

  // Vigía de la carpeta de la config, sin hilo propio: su handle entra en el mismo bucle que
  // los mensajes. No recursivo: los logs viven en una subcarpeta y no deben despertarlo.
  watch_ = FindFirstChangeNotificationW(configPath_.parent_path().c_str(), FALSE,
                                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                                            FILE_NOTIFY_CHANGE_SIZE);
  worker_.Start();
  Rebuild();
  SyncAutoStart(config_.autoStart);
  SyncHotkey();
  SetTimer(host_, kSafetyTimer, kSafetyMs, nullptr);

  MSG message{};
  for (;;) {
    const DWORD handles = watch_ != INVALID_HANDLE_VALUE ? 1 : 0;
    const DWORD woke = MsgWaitForMultipleObjectsEx(handles, &watch_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (handles && woke == WAIT_OBJECT_0) {
      FindNextChangeNotification(watch_);
      SetTimer(host_, kReloadTimer, kReloadDebounceMs, nullptr);
    }
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        LogInfo(L"[dock] salida limpia");
        return 0;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
}

void App::SaveAndReload(const std::wstring& device, const std::vector<DockApp>& base,
                        const std::vector<DockApp>& current, UINT delayMs) {
  SaveLocal(localPath_, device, base, current);
  // El mismo temporizador que el vigía: la recarga se hace una vez aunque el vigía también
  // despierte por la escritura, y CheckFilesChanged ve el fichero cambiado.
  SetTimer(host_, kReloadTimer, std::max<UINT>(delayMs, 1), nullptr);
}

HWND App::ForeignForeground() const {
  // Un clic de ratón de verdad no activa el dock (MA_NOACTIVATE), pero una activación
  // programática sí: computer use activa la ventana antes de hacer clic, y el dock decidía
  // "no la tiene el foco" sobre una ventana que sí lo tenía (medido: primer plano = el propio
  // DockWindowClass). Se decide con la última ventana ajena que tuvo el foco.
  const HWND foreground = GetForegroundWindow();
  DWORD pid = 0;
  GetWindowThreadProcessId(foreground, &pid);
  return pid == GetCurrentProcessId() ? lastForeign_ : foreground;
}

void App::Quit() {
  if (host_) PostMessageW(host_, WM_CLOSE, 0, 0);
}

void App::LoadFiles() {
  config_ = LoadConfig(configPath_);
  local_ = LoadLocal(localPath_);
  configStamp_ = Stamp(configPath_);
  localStamp_ = Stamp(localPath_);
}

void App::Rebuild() {
  // La config se relee aquí: el de C# reconstruía con la que había leído al arrancar y
  // perdía lo que se hubiera editado desde entonces.
  LoadFiles();
  docks_.clear();

  std::vector<Monitor> monitors;
  EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
  for (const Monitor& monitor : monitors) {
    auto window = std::make_unique<DockWindow>(*this, monitor, config_, ResolveFor(config_, local_, monitor.device));
    if (window->Create()) docks_.push_back(std::move(window));
  }
  LogInfo(L"[dock] {} monitor(es)", docks_.size());
  RefreshRunning();
  RequestIcons();
}

bool App::RefreshRunning() {
  const auto start = std::chrono::steady_clock::now();
  // El barrido va en el hilo de UI: en C# iba al pool y escribía el estado sin sincronizar.
  // ponytail: cabe porque cuesta poco (medido en release: ~1-2 ms con 13 ventanas); si
  // pasara de unos 5 ms tocaría llevar la foto al worker.
  Snapshot snapshot = TakeSnapshot();
  bool newApps = false;
  for (auto& dockWindow : docks_) newApps |= dockWindow->UpdateRunning(snapshot, config_.showRunning);
  LogTrace(L"[barrido] {} procesos con ventana, {} ventanas en {:.1f} ms", snapshot.names.size(), snapshot.windows.size(),
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
  return newApps;
}

void App::Apply() {
  const auto start = std::chrono::steady_clock::now();
  LoadFiles();
  // Todo lo que depende de la config pasa por aquí: en C# el atajo, el autoarranque y la
  // altura de la ventana solo se leían al arrancar.
  SyncAutoStart(config_.autoStart);
  SyncHotkey();
  for (auto& dockWindow : docks_) dockWindow->Apply(config_, ResolveFor(config_, local_, dockWindow->Device()));
  RefreshRunning();
  RequestIcons();
  LogInfo(L"[config] recargado en {:.0f} ms",
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

void App::CheckFilesChanged() {
  // El vigía salta por cualquier cosa de la carpeta (un log nuevo, un fichero temporal del
  // editor): solo cuentan dock.json y dock.local.json. Que el dock escriba su propio
  // dock.local.json y se recargue es a propósito: así se enteran los docks de las otras
  // pantallas, que el de C# tenía que avisar a mano.
  const FILETIME config = Stamp(configPath_), local = Stamp(localPath_);
  if (CompareFileTime(&config, &configStamp_) == 0 && CompareFileTime(&local, &localStamp_) == 0) return;
  Apply();
}

void App::RequestIcons() {
  // Solo lo que no esté ya subido a ese tamaño: abrir una app sin anclar o recargar la
  // config no vuelve a extraer los iconos de siempre.
  std::set<std::wstring> keys;
  std::set<std::pair<std::wstring, int>> used;
  for (const auto& dockWindow : docks_)
    for (const DockApp& app : dockWindow->Apps()) {
      if (app.separator) continue;
      used.emplace(app.IconSource(), dockWindow->IconPx());
      if (!Visuals::HasIcon(app.IconSource(), dockWindow->IconPx())) keys.insert(app.IconSource());
    }
  const unsigned round = ++iconRound_;
  if (keys.empty()) {
    Visuals::KeepOnlyIcons(used);
    return;
  }
  const HWND host = host_;
  worker_.Post([keys = std::vector<std::wstring>(keys.begin(), keys.end()), round, host] {
    const auto start = std::chrono::steady_clock::now();
    auto* result = new IconResult{round, ExtractIconsOutOfProcess(keys)};
    for (const std::wstring& key : keys)
      if (!result->icons.contains(key)) LogError(L"[iconos] sin icono para {}", key);
    LogTrace(L"[iconos] {} extraídos en {:.0f} ms (proceso hijo)", result->icons.size(),
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    // Si la anfitriona ya no existe (saliendo), nadie va a recoger esto.
    if (!PostMessageW(host, kIconsReady, 0, reinterpret_cast<LPARAM>(result))) delete result;
  });
}

void App::OnIcons(IconResult* raw) {
  std::unique_ptr<IconResult> result(raw);
  if (result->round != iconRound_) return;  // la config cambió mientras se extraían
  std::set<std::pair<std::wstring, int>> used;
  for (auto& dockWindow : docks_) {
    dockWindow->ShowIcons(result->icons);
    for (const DockApp& app : dockWindow->Apps())
      if (!app.separator) used.emplace(app.IconSource(), dockWindow->IconPx());
  }
  Visuals::KeepOnlyIcons(used);
  LogInfo(L"[iconos] {} nuevos en pantalla ({:.0f} ms desde el arranque)", result->icons.size(), MsSinceStart());
  // Aquí se sueltan los píxeles: las superficies ya tienen su copia.
}

void App::SyncHotkey() {
  if (config_.profileHotkey == hotkey_) return;
  if (!hotkey_.empty()) UnregisterHotKey(host_, kProfileHotkey);
  hotkey_.clear();
  if (config_.profileHotkey.empty()) return;
  // Es ASCII ("Ctrl+Alt+P"): se estrecha carácter a carácter; lo que no lo sea, el parser lo
  // rechaza.
  std::string text;
  for (const wchar_t c : config_.profileHotkey) text.push_back(c < 128 ? static_cast<char>(c) : '?');
  const auto hotkey = ParseHotkey(text);
  // Sin modificador se quitaría esa tecla a todo el sistema mientras el dock viva.
  if (!hotkey || !(hotkey->mods & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN))) {
    LogError(L"[perfil] no entiendo el atajo '{}': hace falta un modificador y una tecla", config_.profileHotkey);
    return;
  }
  // No es un gancho de teclado: Windows avisa con WM_HOTKEY de ESA combinación y de nada más.
  if (!RegisterHotKey(host_, kProfileHotkey, hotkey->mods, hotkey->vk)) {
    LogError(L"[perfil] el atajo '{}' ya lo tiene otra app ({})", config_.profileHotkey, GetLastError());
    return;
  }
  hotkey_ = config_.profileHotkey;
  LogInfo(L"[perfil] atajo '{}' registrado", hotkey_);
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
    const WPARAM code = wparam & 0x7FFF;
    if (code == HSHELL_WINDOWACTIVATED) {
      DWORD pid = 0;
      GetWindowThreadProcessId(reinterpret_cast<HWND>(lparam), &pid);
      if (lparam && pid != GetCurrentProcessId()) lastForeign_ = reinterpret_cast<HWND>(lparam);
      for (auto& dockWindow : docks_) dockWindow->OnWindowActivated();
    }
    // Creada, destruida, activada o reemplazada: puede haber cambiado qué está abierto. Se
    // reprograma el mismo temporizador, que es el rebote de toda la vida.
    if (code == HSHELL_WINDOWCREATED || code == HSHELL_WINDOWDESTROYED || code == HSHELL_WINDOWACTIVATED ||
        code == HSHELL_WINDOWREPLACED)
      SetTimer(host_, kRunningTimer, kRunningDebounceMs, nullptr);
    return 0;
  }
  if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_) {
    for (auto& dockWindow : docks_) dockWindow->OnTaskbarCreated();
    return 0;
  }

  switch (message) {
    case kIconsReady:
      OnIcons(reinterpret_cast<IconResult*>(lparam));
      return 0;

    case WM_HOTKEY:
      if (wparam == kProfileHotkey) {
        // Se guarda y se recarga como cualquier cambio de dock.local.json: el perfil aplica a
        // todas las pantallas por el mismo camino que una edición a mano.
        const std::wstring next = NextProfile(config_, local_.profile);
        LogInfo(L"[perfil] '{}' -> '{}'", local_.profile, next);
        if (SaveProfile(localPath_, next)) SetTimer(host_, kReloadTimer, 1, nullptr);
      }
      return 0;

    case WM_DISPLAYCHANGE:
      SetTimer(host_, kDisplayTimer, kDisplayDebounceMs, nullptr);
      return 0;

    case WM_TIMER:
      if (wparam == kSafetyTimer) {
        if (RefreshRunning()) RequestIcons();
        return 0;
      }
      KillTimer(host_, wparam);
      if (wparam == kRunningTimer) {
        if (RefreshRunning()) RequestIcons();
      } else if (wparam == kDisplayTimer) {
        LogInfo(L"[dock] cambiaron las pantallas, reconstruyendo");
        Rebuild();
      } else if (wparam == kReloadTimer) {
        CheckFilesChanged();
      }
      return 0;

    // Cerrar sesión no pasa por WM_CLOSE: sin esto las franjas reservadas se quedan.
    case WM_ENDSESSION:
      if (wparam) docks_.clear();
      return 0;

    case WM_CLOSE:
      worker_.Stop();  // lo que responda ya no encuentra anfitriona y se borra solo
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
