#include <Windows.h>
#include <stdlib.h>

#include "app/App.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    App app;
    // Ruta inicial opcional: sin ella se abre la carpeta de usuario.
    if (!app.Init(__argc > 1 ? __wargv[1] : nullptr)) {
        MessageBoxW(nullptr, L"No se pudo inicializar DirectX 11.", L"Rayo", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
