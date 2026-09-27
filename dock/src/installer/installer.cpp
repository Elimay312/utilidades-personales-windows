// Instalar-Dock.exe: un solo fichero con Dock.exe dentro como RCDATA. Instala solo para el
// usuario, en %LOCALAPPDATA%\Programs\Dock, así que nunca pide administrador. El mismo exe se
// queda como Desinstalar.exe y lo quita todo con --uninstall.
//
//   Instalar-Dock.exe                    instalar o actualizar, con diálogos
//   Instalar-Dock.exe --silent           lo mismo sin diálogos; no arranca el dock
//   Desinstalar.exe --uninstall          quitar, preguntando antes
//   Desinstalar.exe --uninstall --silent quitar sin diálogos, conservando la config
//
// Código de salida 0 si fue bien (o se canceló), 1 si falló.
//
// Copiado de panel-de-control/src/installer (y aquel de Agenda). Lo que cambia para el dock:
// - El arranque con Windows no lo escribe el instalador: lo decide "autoStart" en dock.json y
//   lo sincroniza el propio dock al arrancar (system/autostart.h). Al desinstalar solo se
//   borra el valor Run si apunta a ESTA instalación: el dock de C# usa el mismo nombre.
// - Al dock en marcha se le pide cerrarse con WM_CLOSE a sus DockWindowClass (una por
//   pantalla, un proceso); nunca se le mata.
// - La config (dock.json, dock.local.json) vive en %LOCALAPPDATA%\Dock, compartida con el de C#.
//
// Las rutas salen de %LOCALAPPDATA% y %APPDATA%, así que una prueba puede apuntarlas a una
// carpeta de usar y tirar. DOCK_INSTALLER_NO_REGISTRY=1 se salta toda escritura en el registro
// para esa misma prueba: HKCU no se puede redirigir.

#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "installer_config.h"

namespace {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
constexpr wchar_t kUninstallKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Uninstall\Dock)";
constexpr wchar_t kValueName[] = L"Dock";
constexpr wchar_t kVersion[] = L"" DOCK_VERSION_STR;
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

fs::path InstallDir() { return EnvDir(L"LOCALAPPDATA") / L"Programs" / L"Dock"; }
fs::path DataDir() { return EnvDir(L"LOCALAPPDATA") / L"Dock"; }
fs::path ShortcutPath() { return EnvDir(L"APPDATA") / LR"(Microsoft\Windows\Start Menu\Programs\Dock.lnk)"; }

fs::path SelfPath() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return (length > 0 && length < MAX_PATH) ? fs::path(buffer) : fs::path();
}

bool SamePath(const fs::path& a, const fs::path& b) {
  return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

// --- Diálogos -------------------------------------------------------------------------------

struct Answer {
  int button = 0;
  bool checked = false;
};

// Un solo TaskDialog para todo: un botón de acción junto a Cancelar, o un Aceptar solo si
// `action` es nulo, y una casilla opcional debajo.
Answer Ask(const std::wstring& title, const std::wstring& content, PCWSTR icon, const wchar_t* action = nullptr,
           const wchar_t* checkbox = nullptr, bool checked = false) {
  const TASKDIALOG_BUTTON button{kActionButton, action};
  TASKDIALOGCONFIG config{};
  config.cbSize = sizeof(config);
  config.hInstance = GetModuleHandleW(nullptr);
  config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | (checked ? TDF_VERIFICATION_FLAG_CHECKED : 0);
  config.pszWindowTitle = L"Dock";
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

// --- Registro: esta clave de desinstalación y, al quitar, el valor Run si es nuestro --------

bool NoRegistry() {
  wchar_t value[4]{};
  return GetEnvironmentVariableW(L"DOCK_INSTALLER_NO_REGISTRY", value, 4) > 0;
}

bool SetString(const wchar_t* key, const wchar_t* name, const std::wstring& value) {
  if (NoRegistry()) return true;
  return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_SZ, value.c_str(),
                         static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool SetDword(const wchar_t* key, const wchar_t* name, DWORD value) {
  if (NoRegistry()) return true;
  return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_DWORD, &value, sizeof(value)) == ERROR_SUCCESS;
}

void DeleteRegistry() {
  if (NoRegistry()) return;
  // El valor "Dock" lo comparte con el de C#: solo se borra si arranca ESTE.
  wchar_t current[MAX_PATH * 2]{};
  DWORD size = sizeof(current);
  if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, current, &size) == ERROR_SUCCESS) {
    std::wstring command = current;
    if (command.size() >= 2 && command.front() == L'"') command = command.substr(1, command.find(L'"', 1) - 1);
    if (SamePath(fs::path(command), InstallDir() / L"Dock.exe")) RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
  }
  RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
}

// --- Un dock en marcha ----------------------------------------------------------------------

struct Running {
  fs::path exe;                    // el instalado: solo a ese se le pide cerrarse
  std::set<DWORD> seen;            // una DockWindowClass por pantalla, un proceso
  std::vector<UniqueHandle> ours;  // a los que se les pidió, para esperarlos
  int elsewhere = 0;               // un dock de otra carpeta (una build, el de C#): se deja
};

BOOL CALLBACK CollectDock(HWND hwnd, LPARAM param) {
  wchar_t name[32]{};
  if (GetClassNameW(hwnd, name, 32) == 0 || std::wstring_view(name) != L"DockWindowClass") return TRUE;
  auto& running = *reinterpret_cast<Running*>(param);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!running.seen.insert(pid).second) return TRUE;
  UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process) return TRUE;
  wchar_t image[MAX_PATH]{};
  DWORD size = MAX_PATH;
  if (!QueryFullProcessImageNameW(process.get(), 0, image, &size) || !SamePath(image, running.exe)) {
    ++running.elsewhere;
    return TRUE;
  }
  // Se le pide, nunca se le mata: su WM_CLOSE cierra el proceso entero (App::Quit).
  PostMessageW(hwnd, WM_CLOSE, 0, 0);
  running.ours.push_back(std::move(process));
  return TRUE;
}

// false si el dock instalado sigue en marcha 5 s después de pedirle que se cierre.
bool CloseDock(int* elsewhere = nullptr) {
  Running running{InstallDir() / L"Dock.exe"};
  EnumWindows(CollectDock, reinterpret_cast<LPARAM>(&running));
  if (elsewhere != nullptr) *elsewhere = running.elsewhere;
  for (const UniqueHandle& process : running.ours)
    if (WaitForSingleObject(process.get(), 5000) != WAIT_OBJECT_0) return false;
  return true;
}

// --- Ficheros -------------------------------------------------------------------------------

// Se escribe al lado y luego se mueve encima, así un disco lleno nunca deja medio Dock.exe. El
// movimiento reintenta porque un proceso que acaba de salir puede retener su imagen un momento.
bool WriteEmbeddedDock(const fs::path& target) {
  const HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(DOCK_EXE_RESOURCE), RT_RCDATA);
  const HGLOBAL loaded = found != nullptr ? LoadResource(nullptr, found) : nullptr;
  const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
  const DWORD size = found != nullptr ? SizeofResource(nullptr, found) : 0;
  if (bytes == nullptr || size == 0) return false;
  const fs::path temporary = fs::path(target).concat(L".new");
  {
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
  if (fs::equivalent(self, target, error)) return true;  // actualizando desde el propio Desinstalar.exe
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (CopyFileW(self.c_str(), target.c_str(), FALSE)) return true;
    Sleep(250);
  }
  return false;
}

bool CreateShortcut(const fs::path& link, const fs::path& exe) {
  ComPtr<IShellLinkW> shell;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell)))) return false;
  shell->SetPath(exe.c_str());
  shell->SetWorkingDirectory(exe.parent_path().c_str());
  shell->SetDescription(L"El dock");
  ComPtr<IPersistFile> file;
  if (FAILED(shell.As(&file))) return false;
  std::error_code error;
  fs::create_directories(link.parent_path(), error);
  return SUCCEEDED(file->Save(link.c_str(), TRUE));
}

// La ruta como nombre de aplicación y los argumentos fijos de quien llama.
bool Start(const fs::path& exe, std::wstring arguments = L"") {
  std::wstring command = L"\"" + exe.wstring() + L"\"";
  if (!arguments.empty()) command += L" " + arguments;
  std::error_code error;
  const fs::path temp = fs::temp_directory_path(error);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  // El directorio de trabajo es %TEMP%: si no, el proceso nuevo retendría la carpeta instalada.
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      temp.empty() ? nullptr : temp.c_str(), &startup, &process))
    return false;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

// --- Instalar -------------------------------------------------------------------------------

int Fail(bool silent, const std::wstring& what, const fs::path& dir) {
  if (!silent) Ask(L"No se pudo instalar el dock", what + L"\n\nCarpeta: " + dir.wstring(), TD_ERROR_ICON);
  return 1;
}

int Install(bool silent) {
  const fs::path dir = InstallDir();
  if (EnvDir(L"LOCALAPPDATA").empty() || EnvDir(L"APPDATA").empty())
    return Fail(silent, L"Windows no dice dónde está la carpeta de programas del usuario.", dir);
  const fs::path exe = dir / L"Dock.exe";
  const bool upgrading = fs::exists(exe);

  if (!silent) {
    const std::wstring title =
        std::wstring(upgrading ? L"Actualizar el dock a la versión " : L"Instalar el dock ") + kVersion;
    const std::wstring content =
        L"El dock se instala solo para tu usuario, en\n" + dir.wstring() +
        L"\n\nNo hace falta ser administrador. Si está abierto, se le pide que se cierre antes de copiar y "
        L"se vuelve a abrir al terminar. Arrancar con Windows lo decide \"autoStart\" en dock.json.";
    const Answer answer = Ask(title, content, TD_INFORMATION_ICON, upgrading ? L"Actualizar" : L"Instalar");
    if (answer.button != kActionButton) return 0;
  }

  int elsewhere = 0;
  if (!CloseDock(&elsewhere))
    return Fail(silent, L"El dock sigue abierto. Ciérralo con clic derecho › Salir del dock y vuelve a probar.", dir);

  std::error_code error;
  fs::create_directories(dir, error);
  if (!WriteEmbeddedDock(exe)) return Fail(silent, L"No se pudo escribir Dock.exe.", dir);
  const fs::path uninstaller = dir / L"Desinstalar.exe";
  if (!CopySelf(uninstaller)) return Fail(silent, L"No se pudo escribir el desinstalador.", dir);
  if (!CreateShortcut(ShortcutPath(), exe)) return Fail(silent, L"No se pudo crear el acceso directo del menú Inicio.", dir);

  const std::wstring quotedUninstaller = L"\"" + uninstaller.wstring() + L"\"";
  const DWORD sizeKb = static_cast<DWORD>(fs::file_size(exe, error) / 1024);
  const bool registered = SetString(kUninstallKey, L"DisplayName", L"Dock") &&
                          SetString(kUninstallKey, L"DisplayVersion", kVersion) &&
                          SetString(kUninstallKey, L"Publisher", L"Dock") &&
                          SetString(kUninstallKey, L"InstallLocation", dir.wstring()) &&
                          SetString(kUninstallKey, L"UninstallString", quotedUninstaller + L" --uninstall") &&
                          SetString(kUninstallKey, L"QuietUninstallString", quotedUninstaller + L" --uninstall --silent") &&
                          SetDword(kUninstallKey, L"NoModify", 1) && SetDword(kUninstallKey, L"NoRepair", 1) &&
                          SetDword(kUninstallKey, L"EstimatedSize", sizeKb);
  if (!registered) return Fail(silent, L"No se pudo registrar el dock en Windows.", dir);

  if (silent) return 0;
  if (elsewhere > 0) {
    // Solo puede haber un dock: arrancar este ahora no haría nada.
    Ask(L"El dock está instalado",
        L"Hay otro dock abierto desde otra carpeta (el de C#, o una build), y solo puede haber uno. "
        L"Ciérralo y abre el dock desde el menú Inicio.",
        TD_INFORMATION_ICON);
    return 0;
  }
  Start(exe);
  Ask(L"El dock está listo", L"Está en todas las pantallas. Clic derecho sobre él para salir; también está en el menú Inicio.",
      TD_INFORMATION_ICON);
  return 0;
}

// --- Desinstalar ----------------------------------------------------------------------------

// Desde la copia en %TEMP%: espera a que salga el Desinstalar.exe que la lanzó y borra la
// carpeta instalada. La carpeta se calcula aquí, nunca se toma de la línea de órdenes.
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
        L"¿Desinstalar el dock?",
        L"Se quitan el programa, su acceso en el menú Inicio, el arranque con Windows y su entrada en "
        L"Aplicaciones instaladas.\n\ndock.json y dock.local.json se quedan en %LOCALAPPDATA%\\Dock, por si "
        L"vuelves a instalarlo.",
        TD_WARNING_ICON, L"Desinstalar", L"Borrar también la configuración y los logs");
    if (answer.button != kActionButton) return 0;
    wipeData = answer.checked;
  }

  if (!CloseDock()) {
    if (!silent)
      Ask(L"No se pudo desinstalar el dock",
          L"El dock sigue abierto. Ciérralo con clic derecho › Salir del dock y vuelve a probar.", TD_ERROR_ICON);
    return 1;
  }

  std::error_code error;
  fs::remove(ShortcutPath(), error);
  DeleteRegistry();
  if (wipeData) {
    fs::remove(DataDir() / L"dock.json", error);
    fs::remove(DataDir() / L"dock.local.json", error);
    fs::remove_all(DataDir() / L"logs", error);
  }

  // Desinstalar.exe no puede borrar la carpeta en la que está su propia imagen: lo hace una
  // copia en %TEMP% cuando este proceso ya ha salido. La copia se queda en %TEMP%.
  if (SamePath(SelfPath().parent_path(), dir)) {
    const fs::path copy =
        fs::temp_directory_path(error) / (L"Dock-desinstalar-" + std::to_wstring(GetCurrentProcessId()) + L".exe");
    if (!CopyFileW(SelfPath().c_str(), copy.c_str(), FALSE) ||
        !Start(copy, L"--remove-folder " + std::to_wstring(GetCurrentProcessId()))) {
      if (!silent) Ask(L"El dock se ha desinstalado a medias", L"Queda la carpeta " + dir.wstring(), TD_WARNING_ICON);
      return 1;
    }
  } else {
    fs::remove_all(dir, error);
  }

  if (!silent)
    Ask(L"El dock se ha desinstalado",
        wipeData ? L"Se borró también la configuración." : L"La configuración sigue en %LOCALAPPDATA%\\Dock.",
        TD_INFORMATION_ICON);
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
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
  const int code = uninstall ? Uninstall(silent) : Install(silent);
  CoUninitialize();
  return code;
}
