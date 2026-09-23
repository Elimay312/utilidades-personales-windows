// Instalar-Panel.exe: one file with Panel.exe inside it as RCDATA. It installs for the current
// user only, in %LOCALAPPDATA%\Programs\Panel, so it never asks for an administrator. The same
// executable is left behind as Desinstalar.exe and removes everything with --uninstall.
//
//   Instalar-Panel.exe                    install or upgrade, with dialogs
//   Instalar-Panel.exe --silent           the same with no dialogs; does not start Panel
//   Desinstalar.exe --uninstall           remove, asking first
//   Desinstalar.exe --uninstall --silent  remove with no dialogs, keeping panel.json
//
// Exit code 0 on success (or on a cancel), 1 on failure.
//
// It is Agenda's installer (calendario/src/installer) with what SEGURIDAD.md 2.8 changes: a
// running Panel is asked to close and never terminated, Panel is started with CreateProcessW and
// not ShellExecute, and the folder is removed by a copy of this exe in %TEMP%, not by cmd.exe.
//
// Paths come from %LOCALAPPDATA% and %APPDATA%, so a test can point both at a scratch folder.
// PANEL_INSTALLER_NO_REGISTRY=1 skips every registry write for the same test: HKCU cannot be
// redirected.

#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "installer_config.h"

namespace {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
constexpr wchar_t kUninstallKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Uninstall\Panel)";
constexpr wchar_t kValueName[] = L"Panel";
constexpr wchar_t kVersion[] = L"" PANEL_VERSION_STR;
constexpr int kActionButton = 100;

struct HandleCloser {
  void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

fs::path EnvDir(const wchar_t* name) {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
  return (length > 0 && length < MAX_PATH) ? fs::path(buffer) : fs::path();
}

fs::path InstallDir() { return EnvDir(L"LOCALAPPDATA") / L"Programs" / L"Panel"; }
fs::path DataDir() { return EnvDir(L"LOCALAPPDATA") / L"Panel"; }
fs::path ShortcutPath() {
  return EnvDir(L"APPDATA") / LR"(Microsoft\Windows\Start Menu\Programs\Panel.lnk)";
}

fs::path SelfPath() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return (length > 0 && length < MAX_PATH) ? fs::path(buffer) : fs::path();
}

bool SamePath(const fs::path& a, const fs::path& b) {
  return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

// --- Dialogs --------------------------------------------------------------------------------

struct Answer {
  int button = 0;
  bool checked = false;
};

// One TaskDialog shape for everything: an action button next to Cancel, or a lone OK when
// `action` is null, and an optional checkbox under it.
Answer Ask(const std::wstring& title, const std::wstring& content, PCWSTR icon,
           const wchar_t* action = nullptr, const wchar_t* checkbox = nullptr,
           bool checked = false) {
  const TASKDIALOG_BUTTON button{kActionButton, action};
  TASKDIALOGCONFIG config{};
  config.cbSize = sizeof(config);
  config.hInstance = GetModuleHandleW(nullptr);
  config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | (checked ? TDF_VERIFICATION_FLAG_CHECKED : 0);
  config.pszWindowTitle = L"Panel";
  config.pszMainIcon = icon;
  config.pszMainInstruction = title.c_str();
  config.pszContent = content.c_str();
  config.pszVerificationText = checkbox;
  if (action != nullptr) {
    config.pButtons = &button;
    config.cButtons = 1;
    config.nDefaultButton = kActionButton;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  } else {
    config.dwCommonButtons = TDCBF_OK_BUTTON;
  }

  Answer answer;
  BOOL verified = FALSE;
  if (FAILED(TaskDialogIndirect(&config, &answer.button, nullptr, &verified))) return answer;
  answer.checked = verified != FALSE;
  return answer;
}

// --- Registry (SEGURIDAD.md 1.7: the Run value and this Uninstall key, nothing else) ---------

bool NoRegistry() {
  wchar_t value[4]{};
  return GetEnvironmentVariableW(L"PANEL_INSTALLER_NO_REGISTRY", value, 4) > 0;
}

bool SetString(const wchar_t* key, const wchar_t* name, const std::wstring& value) {
  if (NoRegistry()) return true;
  return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_SZ, value.c_str(),
                         static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ==
         ERROR_SUCCESS;
}

bool SetDword(const wchar_t* key, const wchar_t* name, DWORD value) {
  if (NoRegistry()) return true;
  return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_DWORD, &value, sizeof(value)) ==
         ERROR_SUCCESS;
}

bool HasValue(const wchar_t* key, const wchar_t* name) {
  return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_ANY, nullptr, nullptr, nullptr) ==
         ERROR_SUCCESS;
}

void DeleteRegistry() {
  if (NoRegistry()) return;
  RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
  RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
}

// --- A running Panel ------------------------------------------------------------------------

struct Running {
  fs::path exe;                        // the installed one: only that one is asked to close
  std::vector<UniqueHandle> ours;      // asked, to be waited for
  int elsewhere = 0;                   // a Panel from another folder (a build), left alone
};

BOOL CALLBACK CollectPanel(HWND hwnd, LPARAM param) {
  // The hidden window the hotkey arrives at, one per instance. Its WM_CLOSE ends Panel the way
  // "Salir" does: DefWindowProc destroys it and WM_DESTROY posts the quit (src/main.cpp).
  wchar_t name[32]{};
  if (GetClassNameW(hwnd, name, 32) == 0 || std::wstring_view(name) != L"PanelDeControlHost") {
    return TRUE;
  }
  auto& running = *reinterpret_cast<Running*>(param);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process) return TRUE;

  wchar_t image[MAX_PATH]{};
  DWORD size = MAX_PATH;
  if (!QueryFullProcessImageNameW(process.get(), 0, image, &size) || !SamePath(image, running.exe)) {
    ++running.elsewhere;
    return TRUE;
  }
  // SEGURIDAD.md 1.5 and 2.8: asked, never terminated.
  PostMessageW(hwnd, WM_CLOSE, 0, 0);
  running.ours.push_back(std::move(process));
  return TRUE;
}

// False if the installed Panel is still running 5 s after being asked to close. `elsewhere`
// counts the ones from other folders, which are not ours to close.
bool ClosePanel(int* elsewhere = nullptr) {
  Running running{InstallDir() / L"Panel.exe"};
  EnumWindows(CollectPanel, reinterpret_cast<LPARAM>(&running));
  if (elsewhere != nullptr) *elsewhere = running.elsewhere;
  for (const UniqueHandle& process : running.ours) {
    if (WaitForSingleObject(process.get(), 5000) != WAIT_OBJECT_0) return false;
  }
  return true;
}

// --- Files ----------------------------------------------------------------------------------

// Written next to the target and then moved over it, so a full disk never leaves half a
// Panel.exe. The move retries because a process that just ended can hold its image a moment.
bool WriteEmbeddedPanel(const fs::path& target) {
  const HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(PANEL_EXE_RESOURCE), RT_RCDATA);
  const HGLOBAL loaded = found != nullptr ? LoadResource(nullptr, found) : nullptr;
  const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
  const DWORD size = found != nullptr ? SizeofResource(nullptr, found) : 0;
  if (bytes == nullptr || size == 0) return false;

  const fs::path temporary = fs::path(target).concat(L".new");
  {
    // std::ofstream and not CreateFileW: the audit keeps CreateFile for bt_audio's devices (2.6).
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(static_cast<const char*>(bytes), size);
    file.close();
    if (!file) return false;
  }
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    Sleep(250);
  }
  DeleteFileW(temporary.c_str());
  return false;
}

bool CopySelf(const fs::path& target) {
  const fs::path self = SelfPath();
  std::error_code error;
  if (self.empty()) return false;
  if (fs::equivalent(self, target, error)) return true;  // upgrading from Desinstalar.exe itself
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (CopyFileW(self.c_str(), target.c_str(), FALSE)) return true;
    Sleep(250);
  }
  return false;
}

bool CreateShortcut(const fs::path& link, const fs::path& exe) {
  ComPtr<IShellLinkW> shell;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&shell)))) {
    return false;
  }
  shell->SetPath(exe.c_str());
  shell->SetWorkingDirectory(exe.parent_path().c_str());
  shell->SetDescription(L"Centro de control con Ctrl+Alt+A");
  ComPtr<IPersistFile> file;
  if (FAILED(shell.As(&file))) return false;
  std::error_code error;
  fs::create_directories(link.parent_path(), error);
  return SUCCEEDED(file->Save(link.c_str(), TRUE));
}

// SEGURIDAD.md 1.6: the path as the application name, and `arguments` fixed by the caller.
bool Start(const fs::path& exe, std::wstring arguments = L"") {
  std::wstring command = L"\"" + exe.wstring() + L"\"";
  if (!arguments.empty()) command += L" " + arguments;
  std::error_code error;
  const fs::path temp = fs::temp_directory_path(error);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  // The working directory is %TEMP%, or the new process would hold the install folder open.
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      temp.empty() ? nullptr : temp.c_str(), &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

// --- Install --------------------------------------------------------------------------------

int Fail(bool silent, const std::wstring& what, const fs::path& dir) {
  if (!silent) {
    Ask(L"No se pudo instalar Panel", what + L"\n\nCarpeta: " + dir.wstring(), TD_ERROR_ICON);
  }
  return 1;
}

int Install(bool silent) {
  const fs::path dir = InstallDir();
  if (EnvDir(L"LOCALAPPDATA").empty() || EnvDir(L"APPDATA").empty()) {
    return Fail(silent, L"Windows no dice dónde está la carpeta de programas del usuario.", dir);
  }
  const fs::path exe = dir / L"Panel.exe";
  const bool upgrading = fs::exists(exe);
  // An upgrade keeps what the user chose about starting with Windows; a first install starts on.
  bool autostart = !upgrading || HasValue(kRunKey, kValueName);

  if (!silent) {
    const std::wstring title = std::wstring(upgrading ? L"Actualizar Panel a la versión "
                                                      : L"Instalar Panel ") +
                               kVersion;
    const std::wstring content =
        L"Panel se instala solo para tu usuario, en\n" + dir.wstring() +
        L"\n\nNo hace falta ser administrador. Si Panel está abierto, se le pide que se cierre "
        L"antes de copiar y se vuelve a abrir al terminar.";
    const Answer answer = Ask(title, content, TD_INFORMATION_ICON,
                              upgrading ? L"Actualizar" : L"Instalar",
                              L"Iniciar Panel con Windows", autostart);
    if (answer.button != kActionButton) return 0;
    autostart = answer.checked;
  }

  int elsewhere = 0;
  if (!ClosePanel(&elsewhere)) {
    return Fail(silent, L"Panel sigue abierto. Ciérralo con clic derecho › Salir y vuelve a probar.",
                dir);
  }

  std::error_code error;
  fs::create_directories(dir, error);
  if (!WriteEmbeddedPanel(exe)) return Fail(silent, L"No se pudo escribir Panel.exe.", dir);
  const fs::path uninstaller = dir / L"Desinstalar.exe";
  if (!CopySelf(uninstaller)) return Fail(silent, L"No se pudo escribir el desinstalador.", dir);
  if (!CreateShortcut(ShortcutPath(), exe)) {
    return Fail(silent, L"No se pudo crear el acceso directo del menú Inicio.", dir);
  }

  const std::wstring quotedUninstaller = L"\"" + uninstaller.wstring() + L"\"";
  const DWORD sizeKb = static_cast<DWORD>(fs::file_size(exe, error) / 1024);
  bool registered = SetString(kUninstallKey, L"DisplayName", L"Panel") &&
                    SetString(kUninstallKey, L"DisplayVersion", kVersion) &&
                    SetString(kUninstallKey, L"Publisher", L"Panel") &&
                    SetString(kUninstallKey, L"InstallLocation", dir.wstring()) &&
                    SetString(kUninstallKey, L"UninstallString", quotedUninstaller + L" --uninstall") &&
                    SetString(kUninstallKey, L"QuietUninstallString",
                              quotedUninstaller + L" --uninstall --silent") &&
                    SetDword(kUninstallKey, L"NoModify", 1) &&
                    SetDword(kUninstallKey, L"NoRepair", 1) &&
                    SetDword(kUninstallKey, L"EstimatedSize", sizeKb);
  if (autostart) {
    registered = SetString(kRunKey, kValueName, L"\"" + exe.wstring() + L"\"") && registered;
  } else if (!NoRegistry()) {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
  }
  if (!registered) return Fail(silent, L"No se pudo registrar Panel en Windows.", dir);

  if (silent) return 0;
  if (elsewhere > 0) {
    // Only one Panel runs at a time, so starting this one now would do nothing.
    Ask(L"Panel está instalado",
        L"Hay otro Panel abierto desde otra carpeta, y solo puede haber uno. Ciérralo con clic "
        L"derecho › Salir y abre Panel desde el menú Inicio.",
        TD_INFORMATION_ICON);
    return 0;
  }
  Start(exe);
  Ask(L"Panel está listo",
      L"Se abre con Ctrl+Alt+A, abajo a la derecha de la pantalla donde tengas el ratón. No tiene "
      L"icono en la bandeja: el clic derecho sobre el panel trae su menú. También está en el "
      L"menú Inicio.",
      TD_INFORMATION_ICON);
  return 0;
}

// --- Uninstall ------------------------------------------------------------------------------

// Run from the copy in %TEMP%: waits for the Desinstalar.exe that started it to exit, then
// removes the install folder. The folder is worked out here, never taken from the command line.
int RemoveFolder(DWORD parent) {
  if (const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parent)) {
    WaitForSingleObject(process, 30000);
    CloseHandle(process);
  }
  const fs::path dir = InstallDir();
  if (EnvDir(L"LOCALAPPDATA").empty() || SamePath(SelfPath().parent_path(), dir)) return 1;
  std::error_code error;
  for (int attempt = 0; attempt < 20; ++attempt) {
    fs::remove_all(dir, error);
    if (!fs::exists(dir, error)) return 0;
    Sleep(250);
  }
  return 1;
}

int Uninstall(bool silent) {
  const fs::path dir = InstallDir();
  if (EnvDir(L"LOCALAPPDATA").empty()) return 1;

  bool wipeData = false;
  if (!silent) {
    const Answer answer = Ask(
        L"¿Desinstalar Panel?",
        L"Se quitan el programa, su acceso en el menú Inicio, el arranque con Windows y su "
        L"entrada en Aplicaciones instaladas.\n\npanel.json se queda en %LOCALAPPDATA%\\Panel, "
        L"por si vuelves a instalarlo.",
        TD_WARNING_ICON, L"Desinstalar", L"Borrar también panel.json y los logs");
    if (answer.button != kActionButton) return 0;
    wipeData = answer.checked;
  }

  if (!ClosePanel()) {
    if (!silent) {
      Ask(L"No se pudo desinstalar Panel",
          L"Panel sigue abierto. Ciérralo con clic derecho › Salir y vuelve a probar.", TD_ERROR_ICON);
    }
    return 1;
  }

  std::error_code error;
  fs::remove(ShortcutPath(), error);
  DeleteRegistry();
  // The night light backup stays: it is the only copy of the user's original setting.
  if (wipeData) {
    fs::remove(DataDir() / L"panel.json", error);
    fs::remove_all(DataDir() / L"logs", error);
  }

  // Desinstalar.exe cannot remove the folder its own image is in, so a copy in %TEMP% does it
  // once this process has exited (SEGURIDAD.md 2.8). The copy stays in %TEMP%.
  if (SamePath(SelfPath().parent_path(), dir)) {
    const fs::path copy = fs::temp_directory_path(error) /
                          (L"Panel-desinstalar-" + std::to_wstring(GetCurrentProcessId()) + L".exe");
    if (!CopyFileW(SelfPath().c_str(), copy.c_str(), FALSE) ||
        !Start(copy, L"--remove-folder " + std::to_wstring(GetCurrentProcessId()))) {
      if (!silent) Ask(L"Panel se ha desinstalado a medias", L"Queda la carpeta " + dir.wstring(), TD_WARNING_ICON);
      return 1;
    }
  } else {
    fs::remove_all(dir, error);
  }

  if (!silent) {
    Ask(L"Panel se ha desinstalado",
        wipeData ? L"Se borró también panel.json." : L"panel.json sigue en %LOCALAPPDATA%\\Panel.",
        TD_INFORMATION_ICON);
  }
  return 0;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  bool uninstall = false;
  bool silent = false;
  DWORD removeFor = 0;
  int argc = 0;
  if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    for (int i = 1; i < argc; ++i) {
      const std::wstring_view arg = argv[i];
      uninstall = uninstall || arg == L"--uninstall";
      silent = silent || arg == L"--silent";
      if (arg == L"--remove-folder" && i + 1 < argc) removeFor = wcstoul(argv[i + 1], nullptr, 10);
    }
    LocalFree(argv);
  }
  if (removeFor != 0) return RemoveFolder(removeFor);

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
    return 1;
  }
  const int code = uninstall ? Uninstall(silent) : Install(silent);
  CoUninitialize();
  return code;
}
