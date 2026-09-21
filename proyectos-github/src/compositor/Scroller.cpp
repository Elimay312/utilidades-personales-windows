#include "compositor/Scroller.h"

#include <algorithm>

#include "compositor/Motion.h"

namespace wuc = winrt::Windows::UI::Composition;
namespace wuci = winrt::Windows::UI::Composition::Interactions;

namespace Gfx {

namespace {

// Cuánto frena la inercia por fotograma. Con 0,92 una muesca se para en medio segundo
// largo; con 0,98 la lista sigue viajando sola y se siente resbaladiza.
constexpr float kDecay = 0.92f;

// De DIP a velocidad. Con decaimiento exponencial por fotograma, lo que recorre un
// impulso es v/60/(1-decay), así que para recorrer D hay que empujar con D*60*(1-decay).
constexpr float kVelocityPerDip = 60.0f * (1.0f - kDecay);

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
    m_tracker.PositionInertiaDecayRate(Motion::Vec3{kDecay, kDecay, kDecay});

    animator.BindScroll(content, m_tracker);
    return true;
}

void Scroller::SetExtent(float contentDip, float viewportDip) {
    if (!m_tracker) return;
    m_max = std::max(contentDip - viewportDip, 0.0f);
    m_tracker.MaxPosition({0.0f, m_max, 0.0f});

    // Si la lista encogió por debajo de donde estábamos mirando, hay que subir o se ve
    // un hueco al final.
    if (m_position > m_max) To(m_max, false);
}

void Scroller::By(float deltaDip) {
    if (!m_tracker || deltaDip == 0.0f) return;
    // Impulso y no destino: dos muescas seguidas suman velocidad y la lista llega más
    // lejos que el doble de una. Eso es la inercia, y no hay que programarla.
    m_tracker.TryUpdatePositionWithAdditionalVelocity({0.0f, deltaDip * kVelocityPerDip, 0.0f});
}

void Scroller::To(float positionDip, bool animate) {
    if (!m_tracker) return;
    const float target = std::clamp(positionDip, 0.0f, m_max);

    if (!animate) {
        m_tracker.TryUpdatePosition({0.0f, target, 0.0f});
        return;
    }

    // Con muelle: navegar con el teclado tiene que deslizarse, no saltar. Y como es un
    // muelle, reapuntarlo a mitad de camino retoma valor y velocidad.
    const Motion::Spring spring = Motion::SpringFor(Motion::Kind::Smooth);
    auto animation = m_compositor.CreateSpringVector3Animation();
    animation.DampingRatio(spring.dampingRatio);
    animation.Period(std::chrono::milliseconds(static_cast<long long>(spring.periodMs)));
    // Con el tipo escrito: FinalValue toma un IReference y una lista entre llaves no
    // sabe a cuál de las sobrecargas ir.
    animation.FinalValue(Motion::Vec3{0.0f, target, 0.0f});
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
