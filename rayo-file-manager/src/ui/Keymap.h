#pragma once

#include <string>
#include <utility>
#include <vector>

// Comandos que la UI emite y App ejecuta. El dibujo no decide nada.
enum class Command {
    None,
    MoveDown,
    MoveUp,
    MoveTop,
    MoveBottom,
    HalfPageDown,
    HalfPageUp,
    Open,        // entrar en la carpeta o abrir el archivo con la app predeterminada
    GoParent,    // subir a la carpeta padre
    GoHome,      // carpeta de usuario
    ToggleMark,  // marca/desmarca lo que hay bajo el cursor y baja una fila
    Copy,        // al portapapeles interno
    Cut,
    Paste,         // en la carpeta actual
    Recycle,       // a la Papelera
    DeleteForever, // sin Papelera, tras confirmar
    Rename,
    Create,      // archivo, o carpeta si el nombre acaba en barra
    Filter,      // abre el filtro de la columna central
    ClearFilter, // Esc sin ningun campo abierto
    Goto,        // abre el campo de escribir una ruta
    ToggleHidden,
    NewTab,
    SelectTab,    // cual, en State::letter ('1'..'9')
    CloseTab,
    SetBookmark,  // los dos esperan a la tecla siguiente, que llega en State::letter
    GotoBookmark,
    Quit,
};

namespace Keymap {

// Lo unico con memoria entre frames: la primera 'g' de "gg" y la espera de la letra del
// marcador.
struct State {
    bool pendingG = false;
    Command pending = Command::None;  // m / ' a la espera de su letra
    char letter = 0;                  // la letra del marcador o el digito de la pestana
};

// Lee el input de ImGui y devuelve el comando que toca (None si ninguno).
Command Poll(State& state);

// Lo que se escribe en el [keys] del config la primera vez: "J=MoveDown" por linea, UTF-8.
std::string Defaults();

// Sustituye la tabla de fabrica por la del config (si viene vacia no se toca nada).
// Devuelve cuantas lineas no se entendieron.
int Load(const std::vector<std::pair<std::string, std::string>>& entries);

}  // namespace Keymap
