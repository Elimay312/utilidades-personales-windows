#pragma once

#include <string>

// Campo de texto de una linea para renombrar (sobre la fila) y para crear (en la barra de
// estado). Es el mismo widget en los dos sitios; lo unico que cambia es donde se dibuja.
namespace EditField {

enum class Result {
    None,
    Confirm,  // Enter
    Cancel,   // Esc, clic fuera o perder el foco
};

struct State {
    int row = -1;             // fila sobre la que se dibuja; -1 = no va sobre ninguna
    std::string text;         // UTF-8, que es lo que come ImGui
    bool focus = false;       // primer frame: coger el foco (y preseleccionar)
    bool selectStem = false;  // renombrar preselecciona el nombre sin extension; crear no
    Result result = Result::None;
};

// Escribe la decision en state.result en vez de devolverla: quien dibuja (MillerView o la
// barra de estado) no tiene que reenviarla, y App la recoge una vez por frame.
void Draw(State& state, float width, const char* id);

}  // namespace EditField
