#pragma once

// Los eventos de entrada, ya traducidos: coordenadas en DIP, modificadores en un juego
// de banderas y el número de clics contado. Shell::Window los produce a partir de los
// mensajes de Windows y Ui::Router los reparte; ninguno de los dos vuelve a mirar un
// WPARAM.
//
// Sin Windows aquí dentro: vive en brujula_core, y así el contador de clics —que es
// aritmética con dos umbrales y se equivoca en silencio— se prueba sin abrir una ventana.

#include <cstdint>

namespace Input {

enum class Modifiers : unsigned {
    None = 0,
    Shift = 1 << 0,
    Control = 1 << 1,
    Alt = 1 << 2,
};

constexpr Modifiers operator|(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr Modifiers& operator|=(Modifiers& a, Modifiers b) {
    a = a | b;
    return a;
}

constexpr bool Has(Modifiers set, Modifiers flag) {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0;
}

enum class Button { None, Left, Right, Middle };

enum class Action {
    Move,
    Down,
    Up,
    // La captura se fue a otra ventana a mitad de una pulsación. Sin esto, el botón se
    // queda hundido para siempre.
    Cancel,
    Leave,
    Wheel,
};

struct Pointer {
    Action action = Action::Move;
    Button button = Button::None;
    float x = 0.0f;
    float y = 0.0f;
    // Ya en DIP de desplazamiento, no en muescas: la traducción de las 120 unidades y de
    // las líneas por muesca la hace Ui::Wheel, que se prueba.
    float wheelX = 0.0f;
    float wheelY = 0.0f;
    int clicks = 1;
    Modifiers modifiers = Modifiers::None;
};

struct Key {
    int virtualKey = 0;
    bool down = true;
    bool repeat = false;
    // Llegó como WM_SYSKEY*, o sea con Alt metido. Si nadie la quiere hay que dejarla
    // bajar a DefWindowProc o se pierden Alt+F4 y Alt+Espacio.
    bool system = false;
    Modifiers modifiers = Modifiers::None;
};

// Windows solo sabe contar hasta dos: manda WM_LBUTTONDBLCLK y se acabó. El triple clic
// que selecciona la línea entera en el campo de texto lo contamos nosotros, y son dos
// umbrales —tiempo y distancia— que es justo lo que se equivoca sin que se note.
class Clicks {
public:
    void Configure(unsigned intervalMs, float slopDip);

    // Devuelve 1, 2 o 3. No sigue subiendo: nada de lo que hay reacciona a un cuádruple
    // clic, y dejarlo crecer solo daría números que nadie comprueba.
    int Count(std::uint64_t timeMs, float x, float y);

    void Reset();

private:
    unsigned m_intervalMs = 500;
    float m_slop = 4.0f;
    std::uint64_t m_last = 0;
    float m_lastX = 0.0f;
    float m_lastY = 0.0f;
    int m_count = 0;
};

}  // namespace Input
