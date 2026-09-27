#include "system/launch.h"

#include <objbase.h>
#include <shellapi.h>

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

void BringToFront(HWND window) {
  if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
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
