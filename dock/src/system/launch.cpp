#include "system/launch.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <thread>

#include "core/log.h"

namespace dock {

void LaunchDetached(const DockApp& app) {
  std::thread([app] {
    // Algunos manejadores del shell (apps de la Store, protocolos) son COM de apartamento.
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    SHELLEXECUTEINFOW info{sizeof(info)};
    // NOASYNC porque el hilo espera a que acabe; NO_UI para que un fallo no plante un
    // diálogo modal delante del usuario: se anota y ya. Sin verbo: el de por defecto.
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpFile = app.target.c_str();
    info.lpParameters = app.arguments.empty() ? nullptr : app.arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) LogInfo(L"[dock] abierta '{}'", app.name);
    else LogError(L"[dock] no se pudo abrir '{}': error {}", app.name, GetLastError());
    if (com) CoUninitialize();
  }).detach();
}

namespace {

std::wstring Quoted(const std::vector<std::wstring>& paths) {
  std::wstring out;
  for (const auto& path : paths) out += (out.empty() ? L"\"" : L" \"") + path + L"\"";
  return out;
}

// App empaquetada: activación oficial por fichero, que acaba en su evento FileActivated. Un
// IShellItemArray con varios ficheros sueltos solo se puede montar desde PIDLs.
void ActivateForFiles(const DockApp& app, const std::vector<std::wstring>& paths) {
  const size_t slash = app.target.find_last_of(L'\\');
  if (slash == std::wstring::npos) return;
  const std::wstring aumid = app.target.substr(slash + 1);
  std::vector<PIDLIST_ABSOLUTE> pidls;
  for (const auto& path : paths) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    // Uno que no se pueda resolver no tumba a los demás.
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) pidls.push_back(pidl);
  }
  IShellItemArray* array = nullptr;
  if (!pidls.empty())
    SHCreateShellItemArrayFromIDLists(static_cast<UINT>(pidls.size()), const_cast<PCIDLIST_ABSOLUTE_ARRAY>(pidls.data()), &array);
  for (auto pidl : pidls) CoTaskMemFree(pidl);  // el array se queda con su copia
  if (!array) return;

  // In-proc a propósito: CLSCTX_LOCAL_SERVER es para procesos que nacen solo para lanzar algo.
  IApplicationActivationManager* manager = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&manager)))) {
    DWORD pid = 0;
    HRESULT hr = manager->ActivateForFile(aumid.c_str(), array, L"open", &pid);
    // 0x80270254: "no es compatible con el contrato". Paint es un Win32 EMPAQUETADO: no
    // declara la activación por fichero y coge la ruta por línea de comandos.
    if (FAILED(hr)) hr = manager->ActivateApplication(aumid.c_str(), Quoted(paths).c_str(), AO_NONE, &pid);
    if (FAILED(hr)) LogError(L"[dock] '{}' no pudo abrir los ficheros: {:#010x}", app.name, static_cast<unsigned>(hr));
    manager->Release();
  }
  array->Release();
}

}  // namespace

void OpenWithDetached(const DockApp& app, std::vector<std::wstring> paths) {
  // Las rutas vienen de una suelta o de la lista de recientes, pero van dentro de un
  // lpParameters entrecomillado: una comilla suelta lo partiría en dos, así que se descartan.
  std::vector<std::wstring> safe;
  for (const auto& path : paths) {
    if (path.find(L'"') != std::wstring::npos) continue;
    wchar_t full[MAX_PATH * 4]{};
    if (GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr)) safe.push_back(full);
  }
  if (safe.empty()) return;
  std::thread([app, safe] {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    if (_wcsnicmp(app.target.c_str(), L"shell:", 6) == 0) {
      ActivateForFiles(app, safe);
    } else {
      const std::wstring arguments = Quoted(safe);
      SHELLEXECUTEINFOW info{sizeof(info)};
      info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
      info.lpFile = app.target.c_str();
      info.lpParameters = arguments.c_str();
      info.nShow = SW_SHOWNORMAL;
      if (!ShellExecuteExW(&info)) LogError(L"[dock] '{}' no pudo abrir {}: error {}", app.name, arguments, GetLastError());
    }
    LogInfo(L"[dock] '{}' abre {} fichero(s)", app.name, safe.size());
    if (com) CoUninitialize();
  }).detach();
}

void RecycleDetached(std::vector<std::wstring> paths) {
  std::thread([paths] {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    IFileOperation* operation = nullptr;
    size_t queued = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation)))) {
      // ALLOWUNDO + RECYCLEONDELETE: a la papelera y no borrado. Sin NOCONFIRMATION a propósito:
      // si algo no cabe en la papelera, el shell avisa de que lo borraría del todo.
      operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD);
      for (const auto& path : paths) {
        IShellItem* item = nullptr;
        HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (SUCCEEDED(hr)) {
          hr = operation->DeleteItem(item, nullptr);
          item->Release();
        }
        if (SUCCEEDED(hr)) queued++;
        else LogError(L"[papelera] '{}' no se puede tirar ({:#010x})", path, static_cast<unsigned>(hr));
      }
      const HRESULT hr = queued ? operation->PerformOperations() : S_FALSE;
      BOOL aborted = FALSE;
      operation->GetAnyOperationsAborted(&aborted);
      if (FAILED(hr)) LogError(L"[papelera] falló ({:#010x})", static_cast<unsigned>(hr));
      else LogInfo(L"[papelera] {} de {} elemento(s){}", queued, paths.size(), aborted ? L", cancelado por el usuario" : L"");
      operation->Release();
    }
    if (com) CoUninitialize();
  }).detach();
}

void BringToFront(HWND window) {
  if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
  Activate(window);
}

void Activate(HWND window) {
  const DWORD target = GetWindowThreadProcessId(window, nullptr);
  const DWORD self = GetCurrentThreadId();
  // AttachThreadInput puede fallar (con las apps UWP falla: su hilo vive en otro contenedor)
  // y aun así SetForegroundWindow funciona. El permiso viene del clic, no del adjuntado.
  const bool attached = target && target != self && AttachThreadInput(self, target, TRUE);
  SetForegroundWindow(window);
  if (attached) AttachThreadInput(self, target, FALSE);
}

void Minimize(HWND window) { ShowWindow(window, SW_MINIMIZE); }

}  // namespace dock
