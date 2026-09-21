#include <Windows.h>

#include "app/App.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    App app;
    if (!app.Init()) {
        MessageBoxW(nullptr, L"No se pudo inicializar DirectX 11.", L"Rayo", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
