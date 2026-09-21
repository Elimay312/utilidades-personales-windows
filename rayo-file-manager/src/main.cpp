#include <Windows.h>
#include <stdlib.h>

#include "app/App.h"
#include "core/Diag.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Diag::Mark("wWinMain");  // el origen del cronometro de arranque

    App app;
    // Ruta inicial opcional (la pasa el menu contextual del Explorador): sin ella se abre
    // la carpeta de usuario, salvo que el config diga otra cosa.
    if (!app.Init(__argc > 1 ? __wargv[1] : nullptr)) {
        MessageBoxW(nullptr, L"No se pudo inicializar DirectX 11.", L"Rayo", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
