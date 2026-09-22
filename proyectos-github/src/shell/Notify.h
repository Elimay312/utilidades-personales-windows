#pragma once

// La notificación de Windows del recordatorio de la revisión semanal.
//
// Es un globo del área de notificación (`Shell_NotifyIconW` con `NIIF_INFO`) y NO una
// `ToastNotification` de WinRT, y no es por pereza: para que el sistema le acepte una
// toast a una aplicación sin empaquetar hace falta un AppUserModelID registrado en un
// acceso directo del menú Inicio, y para que al pulsarla la aplicación se entere, un
// servidor COM registrado. Eso es un instalador, y Brújula es un .exe que se copia.
//
// **El icono no se queda en la bandeja.** Se añade para enseñar el globo y se quita en
// cuanto el globo se va —lo pulsen, lo cierren o se canse de esperar—. Brújula no es una
// aplicación de bandeja, y un icono permanente ahí sería una segunda manera de abrirla que
// nadie ha pedido y que hay que mantener.
//
// **Y solo suena con la aplicación abierta**, que es la limitación honesta de esto: quien
// programa el aviso lo programa dentro de un proceso que tiene que estar vivo. La
// alternativa —una tarea del Programador de tareas que arranque `brujula.exe --revision`—
// necesita el argumento de línea de comandos que la fase 8 ya tiene en su lista.

#include <Windows.h>

#include <string>

namespace Shell {

class Balloon {
public:
    ~Balloon();

    Balloon() = default;
    Balloon(const Balloon&) = delete;
    Balloon& operator=(const Balloon&) = delete;

    // Enseña el globo. false si el shell no lo aceptó — que pasa, por ejemplo, con las
    // notificaciones desactivadas por directiva— y entonces quien llama da el aviso por
    // dentro de la aplicación, que es lo que hace el resto del programa de todos modos.
    bool Show(HWND owner, const std::wstring& title, const std::wstring& body);

    // Lo que llegó por `Window::kNotifyMessage`. Devuelve true si el usuario PULSÓ el
    // globo, que es lo único que significa "quiero eso ahora".
    bool OnMessage(LPARAM lparam);

    void Hide();

private:
    HWND m_owner = nullptr;
    bool m_shown = false;
};

}  // namespace Shell
