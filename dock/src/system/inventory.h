#pragma once

#include <windows.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/config.h"

namespace dock {

// "Abierta" quiere decir que tiene ventana, no que haya un proceso vivo: con procesos a
// secas el Explorador salía siempre abierto, porque explorer.exe es el shell y no se va.
struct AppState {
  std::vector<HWND> windows;  // todas, no solo la primera: la rueda y las miniaturas las usan
  bool Open() const { return !windows.empty(); }
};

// Lo caro, hecho UNA vez por barrido para todos los docks: los procesos y las ventanas de
// primer nivel. Las familias MSIX y las rutas se resuelven como mucho una vez por PID.
struct Snapshot {
  std::vector<std::pair<HWND, DWORD>> windows;  // ventana y PID de quien de verdad la posee
  std::map<DWORD, std::wstring> names;          // PID con ventana -> ejecutable sin ".exe"
  std::map<DWORD, std::optional<std::wstring>> families;
  std::map<DWORD, std::optional<std::wstring>> paths;
};

Snapshot TakeSnapshot();

// El estado de cada entrada de apps, en su mismo orden.
std::vector<AppState> CheckApps(const std::vector<DockApp>& apps, Snapshot& snapshot);

// Las apps con ventana que NO están ancladas, lo que la barra enseña sin anclar. Por ruta
// del ejecutable (un navegador tiene treinta procesos y una sola entrada) y por nombre, para
// que no bailen al abrir cosas.
std::vector<DockApp> UnpinnedApps(const std::vector<DockApp>& pinned, Snapshot& snapshot);

// Si alguna ventana del usuario tapa ese rectángulo de pantalla: lo que decide el
// autoocultar inteligente. Se pregunta en el momento: mover una ventana no genera ningún
// aviso del shell, así que una lista guardada estaría desfasada.
bool AnythingOver(const RECT& area);

// Una app puede estar "abierta"; un documento, una carpeta o una URL no tienen proceso
// propio. Sin este corte un notas.txt cruzaría con cualquier proceso llamado "notas".
// ponytail: la regla es "elemento del shell o .exe"; un .bat quedaría como documento.
bool IsApp(const DockApp& app);

}  // namespace dock
