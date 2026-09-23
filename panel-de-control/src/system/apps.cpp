#include "system/apps.h"

#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>

#include "core/i18n.h"
#include "core/log.h"
#include "ui/glyphs.h"

namespace panel {
namespace {

constexpr wchar_t kGenericGlyph = 0xE71D;  // "all apps": a utility the config gave no icon

std::wstring Widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
  return out;
}

std::wstring Lower(std::wstring_view text) {
  std::wstring out(text);
  for (wchar_t& c : out) c = c == L'/' ? L'\\' : static_cast<wchar_t>(towlower(c));
  return out;
}

std::wstring FileName(std::wstring_view path) {
  const size_t slash = path.find_last_of(L"\\/");
  return std::wstring(slash == std::wstring_view::npos ? path : path.substr(slash + 1));
}

bool Exists(const std::wstring& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

}  // namespace

std::vector<Utility> DefaultUtilities() {
  // The ones that live in the background, where actualizar.ps1 installs them. Brújula, Rayo and
  // Renombrar are apps with a window: closing them from here could lose work, and the launcher
  // opens them. Anybody who wants them in the row adds them to panel.json.
  //
  // The window classes are the ones whose WM_CLOSE ends each app: the dock's one per screen
  // (all of them get it), the island's, the HUD's, QuickLook's hidden host (not its preview
  // panel), the launcher's, and Agenda's hidden app window (its popup would only hide).
  return {
      {L"Dock", L"%LOCALAPPDATA%\\Dock\\app\\Dock.exe", glyph::kDock, L"DockWindowClass"},
      {L"Isla", L"%LOCALAPPDATA%\\Isla\\app\\Isla.exe", glyph::kIsla, L"IslaDinamica"},
      {L"HUD", L"%LOCALAPPDATA%\\Hud\\app\\Hud.exe", glyph::kHud, L"HudVolumen"},
      {L"QuickLook", L"%LOCALAPPDATA%\\QuickLook\\app\\QuickLook.exe", glyph::kQuickLook,
       L"QuickLookHostClass"},
      {L"Lanzador", L"%LOCALAPPDATA%\\Lanzador\\app\\Lanzador.exe", glyph::kLauncher, L"LanzadorVentana"},
      {L"Agenda", L"%LOCALAPPDATA%\\Programs\\Agenda\\Agenda.exe", glyph::kAgenda, L"AgendaApp"},
  };
}

std::vector<Utility> ReadUtilities(const nlohmann::json& config, std::wstring (*expand)(std::wstring_view)) {
  std::vector<Utility> utilities;
  const auto list = config.find("utilidades");
  if (list != config.end() && list->is_array()) {
    for (const nlohmann::json& entry : *list) {
      if (!entry.is_object()) continue;
      const auto text = [&entry](const char* key) {
        const auto found = entry.find(key);
        return found != entry.end() && found->is_string() ? found->get<std::string>() : std::string();
      };
      Utility utility{Widen(text("nombre")), Widen(text("exe")), GlyphFromHex(text("icono")),
                      Widen(text("ventana"))};
      if (utility.name.empty() || utility.exe.empty()) continue;
      if (utility.glyph == 0) utility.glyph = kGenericGlyph;
      utilities.push_back(std::move(utility));
    }
  }
  // A list that says nothing usable is a mistake in the file, not a wish for an empty row.
  if (utilities.empty()) utilities = DefaultUtilities();
  if (expand != nullptr) {
    for (Utility& utility : utilities) utility.exe = expand(utility.exe);
  }
  return utilities;
}

bool LooksLikeExe(std::wstring_view path) {
  const std::wstring lower = Lower(path);
  const bool drive = lower.size() > 3 && iswalpha(lower[0]) && lower[1] == L':' && lower[2] == L'\\';
  const bool unc = lower.starts_with(L"\\\\");
  if (!drive && !unc) return false;
  if (lower.find(L' ') != std::wstring::npos && lower.find(L".exe ") != std::wstring::npos) return false;
  return lower.ends_with(L".exe");
}

bool SamePath(std::wstring_view a, std::wstring_view b) { return Lower(a) == Lower(b); }

std::wstring ExpandVariables(std::wstring_view text) {
  const std::wstring input(text);
  const DWORD size = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
  if (size == 0) return input;
  std::wstring out(size, L'\0');
  const DWORD written = ExpandEnvironmentStringsW(input.c_str(), out.data(), size);
  if (written == 0 || written > size) return input;
  out.resize(written - 1);
  return out;
}

wchar_t GlyphFromHex(std::string_view hex) {
  if (hex.empty() || hex.size() > 4) return 0;
  unsigned value = 0;
  for (const char c : hex) {
    value <<= 4;
    if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
    else return 0;
  }
  return static_cast<wchar_t>(value);
}

void Apps::Start(HWND window, Worker& worker, std::vector<Utility> utilities) {
  window_ = window;
  worker_ = &worker;
  utilities_ = std::move(utilities);
  std::lock_guard lock(mutex_);
  snapshot_.apps.clear();
  for (const Utility& utility : utilities_) {
    snapshot_.apps.push_back(AppEntry{utility.name, utility.glyph, Exists(utility.exe), false});
  }
}

void Apps::Refresh() {
  if (worker_ != nullptr) worker_->Post("apps-look", [this] { Look(); });
}

Apps::Snapshot Apps::Current() {
  std::lock_guard lock(mutex_);
  Snapshot out = snapshot_;
  snapshot_.problem.clear();
  return out;
}

void Apps::Look() {
  std::vector<AppEntry> apps;
  for (const Utility& utility : utilities_) {
    apps.push_back(AppEntry{utility.name, utility.glyph, Exists(utility.exe) && LooksLikeExe(utility.exe), false});
  }
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
      for (size_t i = 0; i < utilities_.size(); ++i) {
        if (apps[i].running || !SamePath(entry.szExeFile, FileName(utilities_[i].exe))) continue;
        // The name matches; only the whole path makes it ours (another program can be called
        // Dock.exe too).
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (process == nullptr) continue;
        wchar_t image[MAX_PATH * 2]{};
        DWORD size = static_cast<DWORD>(std::size(image));
        if (QueryFullProcessImageNameW(process, 0, image, &size) && SamePath(image, utilities_[i].exe)) {
          apps[i].running = true;
        }
        CloseHandle(process);
      }
    }
    CloseHandle(snapshot);
  }
  {
    std::lock_guard lock(mutex_);
    snapshot_.apps = std::move(apps);
  }
  PostMessageW(window_, kAppsMessage, 0, 0);
}

void Apps::Launch(size_t index) {
  if (worker_ == nullptr || index >= utilities_.size()) return;
  worker_->Post("apps-launch", [this, index] {
    const std::wstring exe = utilities_[index].exe;
    // SEGURIDAD.md 1.6: an absolute path to an .exe that exists, passed as the application
    // name and with no command line put together around it.
    if (LooksLikeExe(exe) && Exists(exe)) {
      const std::wstring folder = std::filesystem::path(exe).parent_path().wstring();
      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      PROCESS_INFORMATION process{};
      if (CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup,
                         &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        LogInfo(L"apps: started {}", utilities_[index].name);
      } else {
        LogError(L"apps: {} did not start (error {})", utilities_[index].name, GetLastError());
        std::lock_guard lock(mutex_);
        snapshot_.problem = T(L"No se pudo abrir esa utilidad.", L"That utility could not be opened.");
      }
    }
    Look();
  });
}

namespace {

// The processes running `exe`, opened to wait for them: only a whole-path match is one of ours.
std::vector<std::pair<DWORD, HANDLE>> Running(const std::wstring& exe) {
  std::vector<std::pair<DWORD, HANDLE>> found;
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return found;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
    if (!SamePath(entry.szExeFile, FileName(exe))) continue;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
    if (process == nullptr) continue;
    wchar_t image[MAX_PATH * 2]{};
    DWORD size = static_cast<DWORD>(std::size(image));
    if (QueryFullProcessImageNameW(process, 0, image, &size) && SamePath(image, exe)) {
      found.emplace_back(entry.th32ProcessID, process);
    } else {
      CloseHandle(process);
    }
  }
  CloseHandle(snapshot);
  return found;
}

struct CloseRequest {
  const std::vector<std::pair<DWORD, HANDLE>>* processes;
  const std::wstring* windowClass;
  int sent = 0;
};

BOOL CALLBACK AskToClose(HWND window, LPARAM context) {
  auto& request = *reinterpret_cast<CloseRequest*>(context);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  const bool ours = std::any_of(request.processes->begin(), request.processes->end(),
                                [pid](const auto& process) { return process.first == pid; });
  if (!ours) return TRUE;
  wchar_t name[256]{};
  if (GetClassNameW(window, name, static_cast<int>(std::size(name))) == 0 || *request.windowClass != name) return TRUE;
  // SEGURIDAD.md 1.5: asking. The app closes the way its own "Salir" closes it.
  PostMessageW(window, WM_CLOSE, 0, 0);
  ++request.sent;
  return TRUE;
}

}  // namespace

void Apps::Close(size_t index) {
  if (worker_ == nullptr || index >= utilities_.size()) return;
  worker_->Post("apps-close", [this, index] {
    const Utility& utility = utilities_[index];
    std::wstring problem;
    if (utility.window.empty()) {
      problem = std::wstring(T(L"El panel no sabe cerrar ", L"The panel does not know how to close ")) + utility.name +
                std::wstring(T(L": falta «ventana» en panel.json.", L": \"ventana\" is missing in panel.json."));
    } else {
      std::vector<std::pair<DWORD, HANDLE>> processes = Running(utility.exe);
      CloseRequest request{&processes, &utility.window};
      EnumWindows(&AskToClose, reinterpret_cast<LPARAM>(&request));
      bool closed = !processes.empty() && request.sent > 0;
      if (closed) {
        std::vector<HANDLE> handles;
        for (const auto& process : processes) handles.push_back(process.second);
        closed = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, 3000) != WAIT_TIMEOUT;
      }
      for (const auto& process : processes) CloseHandle(process.second);
      LogInfo(L"apps: asked {} to close through {} window(s): {}", utility.name, request.sent,
              closed ? L"closed" : L"still running");
      if (!closed) {
        // Not insisted on: whatever kept it open is the app's business (SEGURIDAD.md 1.5).
        problem = utility.name + std::wstring(T(L" no se cerró. Ciérrala desde ella misma.",
                                                L" did not close. Close it from itself."));
      }
    }
    if (!problem.empty()) {
      std::lock_guard lock(mutex_);
      snapshot_.problem = problem;
    }
    Look();
  });
}

}  // namespace panel
