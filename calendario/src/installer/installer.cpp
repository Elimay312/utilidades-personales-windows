// Instalar-Agenda.exe: one file with Agenda.exe inside it as RCDATA. It installs for the current
// user only, in %LOCALAPPDATA%\Programs\Agenda, so it never asks for an administrator. The same
// executable is left behind as Desinstalar.exe and removes everything with --uninstall.
//
//   Instalar-Agenda.exe                 install or upgrade, with dialogs
//   Instalar-Agenda.exe --silent        the same with no dialogs; does not launch Agenda
//   Desinstalar.exe --uninstall         remove, asking first
//   Desinstalar.exe --uninstall --silent  remove with no dialogs, keeping the user's data
//
// Exit code 0 on success (or on a cancel), 1 on failure.
//
// Paths come from %LOCALAPPDATA% and %APPDATA%, the same variables src/core/paths.h reads, so a
// test can point both at a scratch folder. AGENDA_INSTALLER_NO_REGISTRY=1 skips every registry
// write for the same test: HKCU cannot be redirected.

#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/aumid.h"
#include "installer_config.h"

namespace {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
constexpr wchar_t kUninstallKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Uninstall\Agenda)";
constexpr wchar_t kValueName[] = L"Agenda";
constexpr wchar_t kVersion[] = L"" AGENDA_VERSION_STR;
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

fs::path InstallDir() { return EnvDir(L"LOCALAPPDATA") / L"Programs" / L"Agenda"; }
fs::path DataDir() { return EnvDir(L"LOCALAPPDATA") / L"Agenda"; }
fs::path ShortcutPath() {
  return EnvDir(L"APPDATA") / LR"(Microsoft\Windows\Start Menu\Programs\Agenda.lnk)";
}

fs::path SelfPath() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return (length > 0 && length < MAX_PATH) ? fs::path(buffer) : fs::path();
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
  config.pszWindowTitle = L"Agenda";
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

// --- Registry -------------------------------------------------------------------------------

bool NoRegistry() {
  wchar_t value[4]{};
  return GetEnvironmentVariableW(L"AGENDA_INSTALLER_NO_REGISTRY", value, 4) > 0;
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

// --- A running Agenda -----------------------------------------------------------------------

BOOL CALLBACK CollectAgenda(HWND hwnd, LPARAM param) {
  // The hidden message window every instance has. It is a 0x0 tool window, so it is found by
  // its class and never by visibility.
  wchar_t name[32]{};
  if (GetClassNameW(hwnd, name, 32) == 0 || std::wstring_view(name) != L"AgendaApp") return TRUE;

  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  UniqueHandle process(
      OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process) return TRUE;

  wchar_t image[MAX_PATH]{};
  DWORD size = MAX_PATH;
  if (!QueryFullProcessImageNameW(process.get(), 0, image, &size) ||
      _wcsicmp(fs::path(image).filename().c_str(), L"Agenda.exe") != 0) {
    return TRUE;
  }
  // DefWindowProc destroys the window, WM_DESTROY posts the quit, and the app stops the sync on
  // its way out: nothing half written.
  PostMessageW(hwnd, WM_CLOSE, 0, 0);
  reinterpret_cast<std::vector<UniqueHandle>*>(param)->push_back(std::move(process));
  return TRUE;
}

bool CloseAgenda() {
  std::vector<UniqueHandle> running;
  EnumWindows(CollectAgenda, reinterpret_cast<LPARAM>(&running));
  bool closed = true;
  for (const UniqueHandle& process : running) {
    if (WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0) continue;
    TerminateProcess(process.get(), 1);
    closed = WaitForSingleObject(process.get(), 2000) == WAIT_OBJECT_0 && closed;
  }
  return closed;
}

// --- Files ----------------------------------------------------------------------------------

// Written next to the target and then moved over it, so a full disk never leaves half an
// Agenda.exe. The move retries because a process that just ended can hold its image a moment.
bool WriteEmbeddedAgenda(const fs::path& target) {
  const HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(AGENDA_EXE_RESOURCE), RT_RCDATA);
  const HGLOBAL loaded = found != nullptr ? LoadResource(nullptr, found) : nullptr;
  const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
  const DWORD size = found != nullptr ? SizeofResource(nullptr, found) : 0;
  if (bytes == nullptr || size == 0) return false;

  const fs::path temporary = fs::path(target).concat(L".new");
  {
    const HANDLE raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) return false;
    const UniqueHandle file(raw);
    DWORD written = 0;
    if (!WriteFile(file.get(), bytes, size, &written, nullptr) || written != size) return false;
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

// The Start Menu shortcut, carrying the AppUserModelID: without it Windows drops every toast an
// unpackaged app raises.
bool CreateShortcut(const fs::path& link, const fs::path& exe) {
  ComPtr<IShellLinkW> shell;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&shell)))) {
    return false;
  }
  shell->SetPath(exe.c_str());
  shell->SetWorkingDirectory(exe.parent_path().c_str());
  shell->SetDescription(L"Calendario con atajo global");

  ComPtr<IPropertyStore> properties;
  if (FAILED(shell.As(&properties))) return false;
  PROPVARIANT id{};
  if (FAILED(InitPropVariantFromString(agenda::kAppUserModelId, &id))) return false;
  const HRESULT set = properties->SetValue(PKEY_AppUserModel_ID, id);
  PropVariantClear(&id);
  if (FAILED(set) || FAILED(properties->Commit())) return false;

  ComPtr<IPersistFile> file;
  if (FAILED(shell.As(&file))) return false;
  std::error_code error;
  fs::create_directories(link.parent_path(), error);
  return SUCCEEDED(file->Save(link.c_str(), TRUE));
}

bool Launch(const fs::path& exe) {
  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
      nullptr, L"open", exe.c_str(), nullptr, exe.parent_path().c_str(), SW_SHOWNORMAL));
  return result > 32;
}

// --- Install --------------------------------------------------------------------------------

int Fail(bool silent, const std::wstring& what, const fs::path& dir) {
  if (!silent) {
    Ask(L"No se pudo instalar Agenda", what + L"\n\nCarpeta: " + dir.wstring(), TD_ERROR_ICON);
  }
  return 1;
}

int Install(bool silent) {
  const fs::path dir = InstallDir();
  if (EnvDir(L"LOCALAPPDATA").empty() || EnvDir(L"APPDATA").empty()) {
    return Fail(silent, L"Windows no dice dónde está la carpeta de programas del usuario.", dir);
  }
  const fs::path exe = dir / L"Agenda.exe";
  const bool upgrading = fs::exists(exe);
  // An upgrade keeps what the user chose about starting with Windows; a first install starts on.
  bool autostart = !upgrading || HasValue(kRunKey, kValueName);

  if (!silent) {
    const std::wstring title = std::wstring(upgrading ? L"Actualizar Agenda a la versión "
                                                      : L"Instalar Agenda ") +
                               kVersion;
    const std::wstring content =
        L"Agenda se instala solo para tu usuario, en\n" + dir.wstring() +
        L"\n\nNo hace falta ser administrador. Si Agenda está abierta, se cierra antes de "
        L"copiar y se vuelve a abrir al terminar.";
    const Answer answer = Ask(title, content, TD_INFORMATION_ICON,
                              upgrading ? L"Actualizar" : L"Instalar",
                              L"Iniciar Agenda con Windows", autostart);
    if (answer.button != kActionButton) return 0;
    autostart = answer.checked;
  }

  if (!CloseAgenda()) return Fail(silent, L"Agenda sigue abierta y no se pudo cerrar.", dir);

  std::error_code error;
  fs::create_directories(dir, error);
  if (!WriteEmbeddedAgenda(exe)) return Fail(silent, L"No se pudo escribir Agenda.exe.", dir);
  const fs::path uninstaller = dir / L"Desinstalar.exe";
  if (!CopySelf(uninstaller)) return Fail(silent, L"No se pudo escribir el desinstalador.", dir);
  if (!CreateShortcut(ShortcutPath(), exe)) {
    return Fail(silent, L"No se pudo crear el acceso directo del menú Inicio.", dir);
  }

  const std::wstring quotedUninstaller = L"\"" + uninstaller.wstring() + L"\"";
  const DWORD sizeKb = static_cast<DWORD>(fs::file_size(exe, error) / 1024);
  bool registered = SetString(kUninstallKey, L"DisplayName", L"Agenda") &&
                    SetString(kUninstallKey, L"DisplayVersion", kVersion) &&
                    SetString(kUninstallKey, L"Publisher", L"Agenda") &&
                    SetString(kUninstallKey, L"DisplayIcon", exe.wstring() + L",0") &&
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
  if (!registered) return Fail(silent, L"No se pudo registrar Agenda en Windows.", dir);

  if (silent) return 0;
  Launch(exe);
  Ask(L"Agenda está lista",
      L"Ya está funcionando en la bandeja del sistema. Se abre con Alt+Shift+C, o con un clic en "
      L"su icono junto al reloj.\n\nWindows 11 esconde los iconos nuevos detrás de la flecha ^ de "
      L"la barra de tareas; desde ahí se puede arrastrar a la barra para tenerlo siempre a la "
      L"vista. También está en el menú Inicio.",
      TD_INFORMATION_ICON);
  return 0;
}

// --- Uninstall ------------------------------------------------------------------------------

int Uninstall(bool silent) {
  const fs::path dir = InstallDir();
  if (EnvDir(L"LOCALAPPDATA").empty()) return 1;

  bool wipeData = false;
  if (!silent) {
    const Answer answer = Ask(
        L"¿Desinstalar Agenda?",
        L"Se quitan el programa, su acceso en el menú Inicio, el arranque con Windows y su "
        L"entrada en Aplicaciones instaladas.\n\nTus eventos, la conexión con Google y la "
        L"configuración se quedan en %LOCALAPPDATA%\\Agenda, por si vuelves a instalarla.",
        TD_WARNING_ICON, L"Desinstalar",
        L"Borrar también mis datos (lo que no haya subido a Google se pierde)");
    if (answer.button != kActionButton) return 0;
    wipeData = answer.checked;
  }

  if (!CloseAgenda()) {
    if (!silent) Ask(L"No se pudo desinstalar Agenda", L"Agenda sigue abierta.", TD_ERROR_ICON);
    return 1;
  }

  std::error_code error;
  fs::remove(ShortcutPath(), error);
  DeleteRegistry();
  if (wipeData) fs::remove_all(DataDir(), error);

  // Running from inside the folder (the usual Desinstalar.exe) the folder cannot go while this
  // process holds its image, so a detached cmd removes it once this one has exited. Its working
  // directory is %TEMP%, or the cmd would be the next thing holding the folder open.
  if (fs::equivalent(SelfPath().parent_path(), dir, error)) {
    std::wstring command = L"cmd.exe /d /c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" +
                           dir.wstring() + L"\"";
    const fs::path temp = fs::temp_directory_path(error);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr,
                       temp.empty() ? nullptr : temp.c_str(), &startup, &process)) {
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
  } else {
    fs::remove_all(dir, error);
  }

  if (!silent) {
    Ask(L"Agenda se ha desinstalado",
        wipeData ? L"Se borraron también tus datos."
                 : L"Tus datos siguen en %LOCALAPPDATA%\\Agenda.",
        TD_INFORMATION_ICON);
  }
  return 0;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  bool uninstall = false;
  bool silent = false;
  int argc = 0;
  if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    for (int i = 1; i < argc; ++i) {
      const std::wstring_view arg = argv[i];
      uninstall = uninstall || arg == L"--uninstall";
      silent = silent || arg == L"--silent";
    }
    LocalFree(argv);
  }

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
    return 1;
  }
  const int code = uninstall ? Uninstall(silent) : Install(silent);
  CoUninitialize();
  return code;
}
