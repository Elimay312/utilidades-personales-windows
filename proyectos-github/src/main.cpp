#include <Windows.h>

#include <shellapi.h>

#include "app/App.h"
#include "app/Launch.h"

namespace {

// Una sola Brújula a la vez, y no es un capricho de pulcritud: dos procesos serían dos
// conexiones de ESCRITURA a la misma caché y dos hilos sincronizando la misma cuenta. Y es
// lo que hace que el esquema de URL sirva de algo: quien pulsa un brujula:// casi siempre
// tiene la aplicación abierta, y abrir una segunda ventana al lado no es "abrir el
// repositorio", es perder de vista la que ya estaba.
constexpr wchar_t kSingleInstance[] = L"Local\\BrujulaUnaSola";

// Le pasa el nombre a la instancia que ya está corriendo y la trae al frente. Devuelve
// false si no hay ninguna a la que hablar — entonces el mutex se quedó huérfano y lo suyo
// es arrancar.
bool HandOff(const std::wstring& repo) {
    const HWND other = FindWindowW(L"BrujulaWindow", nullptr);
    if (other == nullptr) return false;

    if (!repo.empty()) {
        // WM_COPYDATA y no un mensaje propio con un puntero dentro: el puntero sería de
        // OTRO proceso y en el nuestro no apunta a nada. WM_COPYDATA es el único mensaje
        // que el sistema sabe copiar de un espacio de direcciones al otro.
        COPYDATASTRUCT data{};
        data.dwData = App::Application::kOpenRepoCopyData;
        data.cbData = static_cast<DWORD>((repo.size() + 1) * sizeof(wchar_t));
        data.lpData = const_cast<wchar_t*>(repo.c_str());
        SendMessageW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
    }

    // Y al frente. Windows solo deja robar el primer plano a quien acaba de recibir entrada
    // del usuario —que es justo nuestro caso, alguien pulsó algo— y aun así falla a veces;
    // si falla, el botón de la barra de tareas parpadea, que es lo que el sistema quiere.
    if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
    SetForegroundWindow(other);
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // La línea de órdenes se pide entera y se trocea con la del sistema, no con la cadena
    // que llega en el tercer parámetro: ahí las comillas están sin deshacer, y una ruta con
    // espacios se partiría en dos argumentos.
    int count = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    const App::Launch launch = App::ParseArguments(count, argv);
    if (argv) LocalFree(argv);

    // El mutex vive hasta que el proceso muere; no hace falta cerrarlo a mano.
    const HANDLE once = CreateMutexW(nullptr, TRUE, kSingleInstance);
    const bool already = once != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
    if (already && HandOff(launch.repo)) return 0;

    App::Application app;
    app.SetPendingRepo(launch.repo);

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
