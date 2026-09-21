#pragma once

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
    Quit,
};

namespace Keymap {

// Lo unico con memoria entre frames: la primera 'g' de "gg".
struct State {
    bool pendingG = false;
};

// Lee el input de ImGui y devuelve el comando que toca (None si ninguno).
Command Poll(State& state);

}  // namespace Keymap
