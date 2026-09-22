#pragma once

// El desplazamiento de la lista, sobre InteractionTracker.
//
// Está aquí y no en ui/ porque es animación, y la regla 3 de arquitectura dice que la
// animación vive en compositor/. La lista solo le manda impulsos y le pregunta dónde
// está.
//
// Por qué InteractionTracker y no un muelle reapuntado sobre el Offset: porque avisa en
// CADA FOTOGRAMA con la posición, por la cola del hilo de UI. Sin esa lectura, una lista
// virtualizada no sabe qué filas materializar y habría que materializar todo el tramo entre
// donde está y adonde va. Lo corre DWM, así que el hilo de UI no pinta ni un fotograma
// mientras la lista se desliza.
//
// **Lo que ya NO usa es la inercia del tracker**, que era el otro motivo de la fase 2. Ver
// Scroller::By: la rueda va a un destino exacto con un muelle sin rebote, porque una rueda
// da muescas y no velocidad, y dejar que DWM decidiera dónde parar hacía que la lista se
// pasara de largo el repositorio que se estaba buscando.
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

    // Una muesca de rueda, ya en DIP. Va a un destino exacto: cinco muescas recorren
    // cinco muescas, estén o no las anteriores todavía en el aire.
    void By(float deltaDip);

    // A una posición concreta. Lo usan navegar con el teclado y recolocar al encoger.
    void To(float positionDip, bool animate);

    float Position() const { return m_position; }
    float Max() const { return m_max; }

    void Close();

private:
    struct Owner;

    // El único camino que mueve el tracker; los dos de arriba solo eligen el muelle. Con
    // la elección copiada en cada uno, el que se olvidara del recorte a [0, max] mandaría
    // la lista a un sitio que no existe.
    void Target(float positionDip, bool animate);

    void OnValues(float position);

    // Prestado: el Animator es de App y vive más que cualquier lista. Se guarda para que
    // To() pueda mirar si el sistema quiere animaciones, que es una pregunta que si la
    // hicieran los llamantes serían tres sitios donde olvidarla.
    const Motion::Animator* m_animator = nullptr;
    winrt::com_ptr<Owner> m_owner;
    winrt::Windows::UI::Composition::Interactions::InteractionTracker m_tracker{nullptr};
    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    Listener m_listener;
    float m_position = 0.0f;
    // Adónde va, que no es lo mismo que dónde está. Es lo que permite que una ráfaga de
    // muescas sume distancias exactas en vez de perder por el camino lo que a las
    // anteriores les quedaba por recorrer.
    float m_target = 0.0f;
    float m_max = 0.0f;
};

}  // namespace Gfx
