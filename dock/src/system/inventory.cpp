#include "system/inventory.h"

#include <appmodel.h>
#include <dwmapi.h>

#include <algorithm>
#include <filesystem>
#include <set>

#include "system/steam.h"

namespace dock {
namespace {

bool Same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

bool IsShellItem(const std::wstring& target) { return _wcsnicmp(target.c_str(), L"shell:", 6) == 0; }

std::wstring ClassOf(HWND window) {
  wchar_t name[64]{};
  return GetClassNameW(window, name, 64) > 0 ? name : L"";
}

// La criba de "una ventana del usuario": visible, sin dueño (diálogos y flotantes), con
// título, sin WS_EX_TOOLWINDOW y sin encubrir por DWM (el shell deja ventanas fantasma así).
// La barra de tareas no tiene título y se cae sola: si contara, el autoocultar inteligente
// vería siempre algo debajo del dock.
bool IsRealWindow(HWND window, HWND desktop) {
  // El escritorio es de explorer y TIENE título ("Program Manager"): el Explorador salía
  // siempre abierto y el clic minimizaba el escritorio entero.
  if (window == desktop || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return false;
  if (GetWindowTextLengthW(window) == 0) return false;
  // La isla dinámica salía como app abierta. Medido: las únicas con el bit eran la isla y
  // los tres docks; las diez apps de verdad lo tenían a cero. Es el filtro de la barra.
  if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
  BOOL cloaked = FALSE;
  DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  return !cloaked;
}

// Las apps UWP no poseen su ventana: la visible es un ApplicationFrameWindow del host y la
// app vive en una CoreWindow hija. Al minimizarse, Windows saca la CoreWindow del marco, así
// que se recuerda el PID de cuando sí se pudo resolver; sin eso la Calculadora parecía
// cerrada y el siguiente clic abría OTRA. Solo el hilo de UI toca este mapa.
DWORD RealOwnerOf(HWND window, DWORD hostPid) {
  static std::map<HWND, DWORD> uwpOwners;
  if (ClassOf(window) != L"ApplicationFrameWindow") return hostPid;
  if (HWND core = FindWindowExW(window, nullptr, L"Windows.UI.Core.CoreWindow", nullptr)) {
    DWORD pid = 0;
    GetWindowThreadProcessId(core, &pid);
    if (pid) {
      uwpOwners[window] = pid;
      return pid;
    }
  }
  const auto remembered = uwpOwners.find(window);
  return remembered != uwpOwners.end() ? remembered->second : hostPid;
}

std::optional<std::wstring> FamilyOfProcess(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return std::nullopt;
  wchar_t family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1]{};
  UINT32 length = static_cast<UINT32>(std::size(family));
  // Un proceso sin empaquetar devuelve APPMODEL_ERROR_NO_PACKAGE y se descarta solo.
  const LONG status = GetPackageFamilyName(process, &length, family);
  CloseHandle(process);
  return status == ERROR_SUCCESS ? std::optional<std::wstring>(family) : std::nullopt;
}

const std::optional<std::wstring>& PathOf(DWORD pid, Snapshot& snapshot) {
  auto [it, inserted] = snapshot.paths.try_emplace(pid);
  if (!inserted) return it->second;
  if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(process, 0, path, &size) && size > 0) it->second = std::wstring(path, size);
    CloseHandle(process);
  }
  return it->second;
}

// De shell:AppsFolder\Familia_hash!App saca la familia: lo que va antes del "!".
std::optional<std::wstring> FamilyOfTarget(const std::wstring& target) {
  const size_t slash = target.find_last_of(L'\\');
  if (slash == std::wstring::npos) return std::nullopt;
  const std::wstring aumid = target.substr(slash + 1);
  const size_t bang = aumid.find(L'!');
  return bang != std::wstring::npos && bang > 0 ? std::optional(aumid.substr(0, bang)) : std::nullopt;
}

// Qué proceso es de qué app: los Win32 por nombre del ejecutable; los MSIX por familia de
// paquete (no hay .exe que sacar del AUMID); los juegos de Steam por carpeta de instalación.
std::map<DWORD, size_t> MapProcesses(const std::vector<DockApp>& apps, Snapshot& snapshot) {
  std::map<std::wstring, size_t, std::less<>> byName, byFamily;
  auto lower = [](std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), ::towlower);
    return text;
  };
  std::vector<std::pair<std::wstring, size_t>> byFolder;
  for (size_t i = 0; i < apps.size(); i++) {
    const DockApp& app = apps[i];
    if (app.separator) continue;
    // Antes del corte de IsApp: un steam:// es una URL y ahí se quedaría fuera.
    if (auto folder = SteamFolderOf(app.target)) {
      byFolder.emplace_back(*folder, i);
      continue;
    }
    if (!IsApp(app)) continue;
    if (IsShellItem(app.target)) {
      if (auto family = FamilyOfTarget(app.target)) byFamily[lower(*family)] = i;
    } else {
      byName[lower(std::filesystem::path(app.target).stem().wstring())] = i;
    }
  }

  std::map<DWORD, size_t> owners;
  for (const auto& [pid, name] : snapshot.names) {
    if (auto direct = byName.find(lower(name)); direct != byName.end()) {
      owners[pid] = direct->second;
      continue;
    }
    if (byFamily.empty()) continue;
    auto [cached, fresh] = snapshot.families.try_emplace(pid);
    if (fresh) cached->second = FamilyOfProcess(pid);
    if (cached->second)
      if (auto packaged = byFamily.find(lower(*cached->second)); packaged != byFamily.end()) owners[pid] = packaged->second;
  }
  // Los juegos, por la ruta del ejecutable. Solo de procesos CON ventana: resolver la de
  // todos serían cientos de OpenProcess por barrido para descartar casi todos.
  if (!byFolder.empty()) {
    for (const auto& [window, pid] : snapshot.windows) {
      if (owners.contains(pid)) continue;
      const auto& path = PathOf(pid, snapshot);
      if (!path) continue;
      for (const auto& [folder, index] : byFolder) {
        if (path->size() > folder.size() && _wcsnicmp(path->c_str(), folder.c_str(), folder.size()) == 0 &&
            (*path)[folder.size()] == L'\\') {
          owners[pid] = index;
          break;
        }
      }
    }
  }
  return owners;
}

}  // namespace

bool IsApp(const DockApp& app) {
  if (app.separator || app.target == kTrashTarget || IsUrl(app.target)) return false;
  return IsShellItem(app.target) ||
         (app.target.size() > 4 && _wcsicmp(app.target.c_str() + app.target.size() - 4, L".exe") == 0);
}

Snapshot TakeSnapshot() {
  Snapshot snapshot;
  struct Walk {
    HWND desktop;
    Snapshot* snapshot;
  } walk{GetShellWindow(), &snapshot};
  EnumWindows(
      [](HWND window, LPARAM param) -> BOOL {
        auto* w = reinterpret_cast<Walk*>(param);
        if (!IsRealWindow(window, w->desktop)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid) w->snapshot->windows.emplace_back(window, RealOwnerOf(window, pid));
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&walk));

  // Solo los procesos que TIENEN ventana, no los ~430 del sistema: con CreateToolhelp32Snapshot
  // el barrido tardaba 13-16 ms en release, 12-14 de ellos en recorrer todos los procesos.
  for (const auto& [window, pid] : snapshot.windows) {
    if (snapshot.names.contains(pid)) continue;
    const auto& path = PathOf(pid, snapshot);
    snapshot.names[pid] = path ? std::filesystem::path(*path).stem().wstring() : std::wstring{};
  }
  return snapshot;
}

std::vector<AppState> CheckApps(const std::vector<DockApp>& apps, Snapshot& snapshot) {
  const auto owners = MapProcesses(apps, snapshot);
  std::vector<AppState> states(apps.size());
  for (const auto& [window, pid] : snapshot.windows)
    if (auto owner = owners.find(pid); owner != owners.end()) states[owner->second].windows.push_back(window);
  return states;
}

std::vector<DockApp> UnpinnedApps(const std::vector<DockApp>& pinned, Snapshot& snapshot) {
  const auto owners = MapProcesses(pinned, snapshot);
  const DWORD own = GetCurrentProcessId();
  // Las ya ancladas también por ruta: el cruce por nombre no pilla la misma app anclada con
  // otra ruta.
  std::set<std::wstring> anchored;
  for (const DockApp& app : pinned)
    if (!app.separator && !app.target.empty()) anchored.insert(std::filesystem::path(app.target).wstring());

  std::vector<DockApp> found;
  for (const auto& [window, pid] : snapshot.windows) {
    if (pid == own || owners.contains(pid)) continue;
    const auto& path = PathOf(pid, snapshot);
    if (!path) continue;
    // Un marco UWP cuyo dueño no se pudo resolver (la app ya estaba minimizada cuando arrancó
    // el dock: la CoreWindow no está dentro y aún no había PID recordado) saldría como
    // "ApplicationFrameHost". No se sabe qué app es: fuera.
    // ponytail: la identificación buena es por AppUserModelID de la ventana (F9).
    if (_wcsicmp(std::filesystem::path(*path).filename().c_str(), L"ApplicationFrameHost.exe") == 0) continue;
    const bool already = std::any_of(anchored.begin(), anchored.end(), [&](const std::wstring& a) { return Same(a, *path); }) ||
                         std::any_of(found.begin(), found.end(), [&](const DockApp& f) { return Same(f.target, *path); });
    if (already) continue;
    const auto name = snapshot.names.find(pid);
    found.push_back(DockApp{name != snapshot.names.end() && !name->second.empty()
                                ? name->second
                                : std::filesystem::path(*path).stem().wstring(),
                            *path});
  }
  std::sort(found.begin(), found.end(), [](const DockApp& a, const DockApp& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
  return found;
}

bool AnythingOver(const RECT& area) {
  struct Walk {
    HWND desktop;
    DWORD own;
    RECT area;
    bool found = false;
  } walk{GetShellWindow(), GetCurrentProcessId(), area};
  EnumWindows(
      [](HWND window, LPARAM param) -> BOOL {
        auto* w = reinterpret_cast<Walk*>(param);
        // Las minimizadas siguen teniendo rectángulo, pero no tapan nada.
        if (!IsRealWindow(window, w->desktop) || IsIconic(window)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == w->own) return TRUE;
        RECT r{};
        if (!GetWindowRect(window, &r)) return TRUE;
        if (r.right <= w->area.left || r.left >= w->area.right || r.bottom <= w->area.top || r.top >= w->area.bottom)
          return TRUE;
        w->found = true;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&walk));
  return walk.found;
}

}  // namespace dock
