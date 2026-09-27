#pragma once

#include <windows.h>

#include "model/config.h"

namespace dock {

// Abre la entrada con el shell: un .exe, un elemento del shell (apps de la Store, la
// papelera), una carpeta, un documento o una URL, todo por el mismo camino. En un hilo
// propio: ShellExecuteEx puede tardar segundos y el hilo de UI atiende el ratón y el
// compositor.
void LaunchDetached(const DockApp& app);

// Las únicas operaciones sobre ventanas ajenas, siempre como respuesta a un clic.
// SetForegroundWindow solo funciona porque el dock acaba de recibir la entrada (el clic):
// un clic simulado con PostMessage no cambiaba el foco en las pruebas del de C#.
void BringToFront(HWND window);
void Minimize(HWND window);

}  // namespace dock
