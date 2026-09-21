#pragma once

// El desplazamiento con inercia, sobre InteractionTracker.
//
// Está aquí y no en ui/ porque es animación, y la regla 3 de arquitectura dice que la
// animación vive en compositor/. La lista solo le manda impulsos y le pregunta dónde
// está.
//
// Por qué InteractionTracker y no un muelle reapuntado sobre el Offset:
//
//   - Da inercia de verdad, con decaimiento exponencial, y la corre DWM. El hilo de UI
//     no pinta ni un fotograma mientras la lista se desliza.
//   - Y —lo que de verdad decide— avisa en CADA FOTOGRAMA con la posición, por la cola
//     del hilo de UI. Sin esa lectura, una lista virtualizada no sabe qué filas tiene
//     que materializar y habría que realizar todo el tramo entre donde está y adonde va.
//
// El contenido se ata con una expresión (-tracker.Position), así que el enlace también
// lo evalúa DWM.

#include <functional>

#include "compositor/Winrt.h"

namespace Motion {
class Animator;
}

namespace Gfx {

class Scroller {
public:
    Scroller();
    // Fuera de línea a la fuerza: el dueño del tracker es un tipo incompleto en esta
    // cabecera, y un com_ptr necesita verlo entero para destruirlo. Con el destructor
    // implícito, quien tenga un Scroller por valor —Ui::List— no compila.
    ~Scroller();

    Scroller(const Scroller&) = delete;
    Scroller& operator=(const Scroller&) = delete;

    // Se llama en el hilo de UI, una vez por fotograma mientras haya movimiento.
    using Listener = std::function<void(float positionDip)>;

    bool Create(const winrt::Windows::UI::Composition::Compositor& compositor,
                const Motion::Animator& animator,
                const winrt::Windows::UI::Composition::Visual& content, Listener listener);

    // El tope de abajo. Con el contenido más corto que la ventanilla, cero: una lista que
    // cabe entera no se desplaza ni un poco.
    void SetExtent(float contentDip, float viewportDip);

    // Una muesca de rueda o un empujón del panel táctil, ya en DIP. Se traduce a
    // velocidad, así que dos seguidas suman y la lista llega más lejos: eso es la
    // inercia, y sale sola.
    void By(float deltaDip);

    // Sin inercia: ir y quedarse. Lo usa navegar con el teclado.
    void To(float positionDip, bool animate);

    float Position() const { return m_position; }
    float Max() const { return m_max; }

    void Close();

private:
    struct Owner;

    void OnValues(float position);

    winrt::com_ptr<Owner> m_owner;
    winrt::Windows::UI::Composition::Interactions::InteractionTracker m_tracker{nullptr};
    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    Listener m_listener;
    float m_position = 0.0f;
    float m_max = 0.0f;
};

}  // namespace Gfx
