#include "compositor/Scroller.h"

#include <algorithm>

#include "compositor/Motion.h"

namespace wuc = winrt::Windows::UI::Composition;
namespace wuci = winrt::Windows::UI::Composition::Interactions;

namespace Gfx {

namespace {

// **La rueda va con DURACIÓN FIJA y no con muelle, y es el único sitio de la aplicación
// donde eso es lo correcto.**
//
// La regla de la fase 1 dice muelles para todo lo interrumpible, y una ráfaga de muescas es
// justo eso. Pero un muelle dentro de un InteractionTracker no termina cuando su periodo
// dice. Medido en la fase 8, contando cada cuántos milisegundos cambian los píxeles de la
// columna:
//
//     sin animar                     se acabó antes del primer fotograma
//     muelle, periodo 60 ms          ~1000 ms de movimiento
//     muelle, periodo 180 ms         ~1500 ms de movimiento
//
// Tres veces el periodo no da tres veces el tiempo, así que ahí dentro no hay solo el
// muelle que le pasamos. Y milésima arriba o abajo, una sola muesca que deja la lista
// moviéndose un segundo y medio es exactamente lo que se sintió como deslizarse sobre el
// hielo. Con una duración escrita, termina cuando dice que termina.
//
// Lo que se pierde al no ser muelle —retomar la VELOCIDAD al reapuntar a mitad— aquí casi
// no se nota: cada muesca reapunta desde donde esté la lista, y doscientos milisegundos son
// más corto que el hueco entre dos muescas de una mano girando la rueda.
constexpr float kWheelMs = 200.0f;

}  // namespace

// El tracker exige un dueño que implemente su interfaz para poder avisar. Seis métodos
// de los que solo interesa uno; los otros cinco existen porque la interfaz los pide.
struct Scroller::Owner : winrt::implements<Owner, wuci::IInteractionTrackerOwner> {
    Scroller* scroller = nullptr;

    void ValuesChanged(const wuci::InteractionTracker&,
                       const wuci::InteractionTrackerValuesChangedArgs& args) {
        // El tracker puede sobrevivir al Scroller un instante: se comprueba siempre.
        if (scroller) scroller->OnValues(args.Position().y);
    }

    void IdleStateEntered(const wuci::InteractionTracker&,
                          const wuci::InteractionTrackerIdleStateEnteredArgs&) {}
    void InertiaStateEntered(const wuci::InteractionTracker&,
                             const wuci::InteractionTrackerInertiaStateEnteredArgs&) {}
    void InteractingStateEntered(const wuci::InteractionTracker&,
                                 const wuci::InteractionTrackerInteractingStateEnteredArgs&) {}
    void RequestIgnored(const wuci::InteractionTracker&,
                        const wuci::InteractionTrackerRequestIgnoredArgs&) {}
    void CustomAnimationStateEntered(
        const wuci::InteractionTracker&,
        const wuci::InteractionTrackerCustomAnimationStateEnteredArgs&) {}
};

Scroller::Scroller() = default;

Scroller::~Scroller() {
    // Por si nadie llamó a Close: dejar un aviso en vuelo apuntando a memoria liberada es
    // exactamente el fallo que este destructor existe para no tener.
    if (m_owner) m_owner->scroller = nullptr;
}

bool Scroller::Create(const wuc::Compositor& compositor, const Motion::Animator& animator,
                      const wuc::Visual& content, Listener listener) {
    m_compositor = compositor;
    m_animator = &animator;
    m_listener = std::move(listener);

    m_owner = winrt::make_self<Owner>();
    m_owner->scroller = this;

    try {
        m_tracker = wuci::InteractionTracker::CreateWithOwner(
            compositor, m_owner.as<wuci::IInteractionTrackerOwner>());
    } catch (const winrt::hresult_error&) {
        m_owner->scroller = nullptr;
        m_owner = nullptr;
        return false;
    }

    m_tracker.MinPosition({0.0f, 0.0f, 0.0f});
    m_tracker.MaxPosition({0.0f, 0.0f, 0.0f});
    // PositionInertiaDecayRate ya no se toca porque no queda inercia que decaer: desde la
    // fase 8 la rueda va a un destino y no a una velocidad. Ver Scroller::By.

    animator.BindScroll(content, m_tracker);
    return true;
}

void Scroller::SetExtent(float contentDip, float viewportDip) {
    if (!m_tracker) return;
    m_max = std::max(contentDip - viewportDip, 0.0f);
    m_tracker.MaxPosition({0.0f, m_max, 0.0f});

    // El destino también se recorta, y no sobra: es lo único que vuelve a atar m_target a
    // la realidad cuando la lista encoge debajo. Sin esto, filtrar hasta dejar cuatro
    // repositorios y dar una muesca hacia arriba partiría de un destino que ya no existe.
    m_target = std::clamp(m_target, 0.0f, m_max);

    // Si la lista encogió por debajo de donde estábamos mirando, hay que subir o se ve
    // un hueco al final.
    if (m_position > m_max) To(m_max, false);
}

void Scroller::By(float deltaDip) {
    if (!m_tracker || deltaDip == 0.0f) return;

    // **La rueda manda DESTINO y no velocidad, y esto deshace la decisión de la fase 2.**
    //
    // Aquella decía que un impulso da inercia de verdad y que dos muescas seguidas llegan
    // más lejos que el doble de una. Las dos cosas son ciertas, y las dos están mal para
    // una rueda: una rueda no tiene velocidad que medir, tiene muescas, y cada muesca es
    // una distancia que Windows ya define — SPI_GETWHEELSCROLLLINES, que es de donde sale
    // deltaDip. Convertirla en un empujón y dejar que DWM decidiera dónde parar es
    // exactamente lo que se sintió al usarla: se scrollea y se pierden los proyectos,
    // porque la lista sigue viajando después de soltar la rueda.
    //
    // Un panel táctil de precisión sí tiene velocidad de verdad que pasar, y entonces esto
    // sería otra rama. Pero esta aplicación no recibe esos eventos —le llegan muescas de
    // WM_MOUSEWHEEL— así que hoy esa rama no tendría a nadie dentro.
    //
    // Se suma sobre m_target y no sobre la posición actual: con cinco muescas seguidas,
    // partir de donde está la lista perdería lo que a las anteriores les queda por recorrer
    // y cinco muescas se quedarían en tres. Sumando sobre el destino, cinco muescas
    // recorren exactamente cinco muescas, que es lo único que se le pide a una rueda.
    Target(m_target + deltaDip, true);
}

void Scroller::To(float positionDip, bool animate) {
    // Navegar con el teclado y recolocar cuando la lista encoge. Va con la misma duración
    // fija que la rueda, y por el mismo motivo: lo que se mueve es la misma lista dentro
    // del mismo tracker, y darle dos maneras distintas de llegar sería que la flecha abajo
    // y la muesca de rueda se sintieran como dos aplicaciones.
    Target(positionDip, animate);
}

void Scroller::Target(float positionDip, bool animate) {
    if (!m_tracker) return;
    const float target = std::clamp(positionDip, 0.0f, m_max);
    m_target = target;

    // Sin animaciones del sistema, ir y estar: quien apaga "Mostrar animaciones" pide que
    // no se mueva nada, y una lista deslizándose es lo más que se mueve en esta pantalla.
    if (!animate || (m_animator != nullptr && !m_animator->Enabled())) {
        m_tracker.TryUpdatePosition({0.0f, target, 0.0f});
        return;
    }

    // Sale deprisa y frena largo, la misma curva que los fundidos del kit: es la que hace
    // que un movimiento parezca que LLEGA en vez de que aparece.
    //
    // La duración pasa por Motion::TimeScale porque esta animación se monta a mano y no por
    // Motion::Animator: sin esto, el modo lento de depuración dejaba el desplazamiento
    // corriendo a velocidad normal dentro de una pantalla a cámara lenta, que es justo el
    // sitio donde un salto se esconde.
    auto animation = m_compositor.CreateVector3KeyFrameAnimation();
    animation.Duration(
            std::chrono::milliseconds(static_cast<long long>(kWheelMs * Motion::TimeScale())));
    animation.InsertKeyFrame(1.0f, Motion::Vec3{0.0f, target, 0.0f},
                             m_compositor.CreateCubicBezierEasingFunction({0.16f, 1.0f},
                                                                          {0.3f, 1.0f}));
    m_tracker.TryUpdatePositionWithAnimation(animation);
}

void Scroller::OnValues(float position) {
    m_position = position;
    if (m_listener) m_listener(position);
}

void Scroller::Close() {
    // Primero cortar el aviso: el tracker puede tener un ValuesChanged en vuelo por la
    // cola del hilo, y llegaría a un Scroller ya destruido.
    if (m_owner) m_owner->scroller = nullptr;
    m_listener = nullptr;
    m_tracker = nullptr;
    m_owner = nullptr;
    m_compositor = nullptr;
}

}  // namespace Gfx
