#include "compositor/Shadow.h"

#include "compositor/Motion.h"
#include "compositor/Paint.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Gfx {

namespace {

// Se pregunta por la INTERFAZ y no por el nombre de la clase. Dos razones:
//
//   - Es la pregunta de verdad. ILayerVisual2 es quien trae Shadow y IDropShadow2 quien
//     trae SourcePolicy; si están las dos, esto funciona, y da igual cómo se llame la
//     versión de Windows que hay debajo.
//   - ApiInformation obliga a escribir "Windows.UI.Composition.LayerVisual" como cadena,
//     y auditar.ps1 la marca como incumplimiento de la regla 4: el ".Co" de
//     "Composition" se lee como un dominio. Antes que relajar una regla de seguridad por
//     un falso positivo, se quita la cadena.
bool ShadowApiPresent(const wuc::Compositor& compositor) {
    try {
        const auto layer = compositor.CreateLayerVisual();
        if (!layer.try_as<wuc::ILayerVisual2>()) return false;
        const auto shadow = compositor.CreateDropShadow();
        return shadow.try_as<wuc::IDropShadow2>() != nullptr;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

}  // namespace

bool Shadow::Create(const wuc::Compositor& compositor, const wuc::ContainerVisual& parent,
                    Metrics::Elevation elevation) {
    if (!ShadowApiPresent(compositor)) {
        // Sin sombra, pero con algo donde meter los visuales: quien llama no tiene que
        // saber en qué Windows está.
        m_content = compositor.CreateContainerVisual();
        m_content.RelativeSizeAdjustment({1.0f, 1.0f});
        parent.Children().InsertAtTop(m_content);
        return true;
    }

    m_shadow = compositor.CreateDropShadow();
    m_shadow.BlurRadius(elevation.blur);
    m_shadow.Offset({0.0f, elevation.offsetY, 0.0f});
    // La máscara sale del alfa de lo que haya dentro. Sin esto, la sombra sería el
    // rectángulo entero del visual y asomaría por las esquinas redondeadas.
    m_shadow.SourcePolicy(wuc::CompositionDropShadowSourcePolicy::InheritFromVisualContent);

    m_layer = compositor.CreateLayerVisual();
    m_layer.RelativeSizeAdjustment({1.0f, 1.0f});
    m_layer.Shadow(m_shadow);
    parent.Children().InsertAtTop(m_layer);

    m_content = m_layer;
    return true;
}

void Shadow::SetSize(float widthDip, float heightDip) {
    // Los hijos se dimensionan solos con RelativeSizeAdjustment; esto es para el caso en
    // que el padre no tenga tamaño propio todavía.
    if (m_content) m_content.Size({widthDip, heightDip});
}

void Shadow::SetColor(Theme::Color color, const Motion::Animator& animator, float durationMs) {
    if (!m_shadow) return;
    if (durationMs > 0.0f) {
        animator.ShadowColor(m_shadow, ToUi(color), durationMs);
    } else {
        m_shadow.StopAnimation(L"Color");
        m_shadow.Color(ToUi(color));
    }
    // El alfa del token va en el color; Opacity se queda en uno para no multiplicar dos
    // transparencias y acabar con una sombra que no se ve.
    m_shadow.Opacity(1.0f);
}

void Shadow::Close() {
    m_shadow = nullptr;
    m_layer = nullptr;
    m_content = nullptr;
}

}  // namespace Gfx
