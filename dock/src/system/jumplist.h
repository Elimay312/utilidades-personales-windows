#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "model/config.h"

namespace dock {

struct JumpItem {
  std::wstring name;
  std::wstring path;
};

// El AppUserModelID con el que el shell guarda los recientes de una app. Una ruta de .exe no
// vale (el Bloc de notas por ruta da la lista vacía; "Brave" y "Microsoft.Windows.Explorer"
// dan 7 y 8): se lee de una ventana viva de la app y se recuerda, para que el menú funcione
// también con la app cerrada. Solo en el hilo de UI.
std::optional<std::wstring> AppIdOf(const DockApp& app, HWND window);

// Los recientes (o, si no hay, los frecuentes) de ese AppID. Solo la mitad de la lista de
// saltos que da la API: los anclados y las tareas no se pueden leer con ella. COM: desde un
// hilo STA (el worker).
std::vector<JumpItem> ReadJumpList(const std::wstring& appId, size_t limit);

}  // namespace dock
