#include <Windows.h>

#include "app/App.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    App::Application app;

    if (!app.Init(instance)) {
        // Un aviso dentro de la ventana no sirve cuando lo que ha fallado es la ventana.
        // Es la única excepción a "errores como avisos discretos" de CLAUDE.md, y solo
        // porque en este punto todavía no hay dónde enseñarlos.
        MessageBoxW(nullptr, L"No se pudo crear la ventana ni el dispositivo gráfico.", L"Brújula",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    const int code = app.Run();
    app.Shutdown();
    return code;
}
