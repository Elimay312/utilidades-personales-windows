#include "compositor/Morph.h"

#include "compositor/Device.h"
#include "compositor/Motion.h"
#include "compositor/Paint.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Gfx {

bool Morph::Create(const wuc::Compositor& compositor, const wuc::ContainerVisual& parent) {
    m_compositor = compositor;

    m_root = compositor.CreateContainerVisual();
    // Recorte al propio tamaño: el contenido del panel es más grande que la tarjeta, y
    // sin esto asomaría por fuera mientras el elemento todavía es pequeño.
    m_root.Clip(compositor.CreateInsetClip());
    parent.Children().InsertAtTop(m_root);

    m_geometry = compositor.CreateRoundedRectangleGeometry();
    m_materialBrush = compositor.CreateColorBrush();

    auto shape = compositor.CreateSpriteShape(m_geometry);
    shape.FillBrush(m_materialBrush);

    m_material = compositor.CreateShapeVisual();
    m_material.RelativeSizeAdjustment({1.0f, 1.0f});
    m_material.Shapes().Append(shape);
    m_root.Children().InsertAtTop(m_material);

    if (!m_collapsed.Create(compositor)) return false;
    if (!m_expanded.Create(compositor)) return false;
    m_root.Children().InsertAtTop(m_collapsed.Visual());
    m_root.Children().InsertAtTop(m_expanded.Visual());

    m_collapsed.Visual().Opacity(1.0f);
    m_expanded.Visual().Opacity(0.0f);
    return true;
}

void Morph::SetFrames(const Frame& collapsed, const Frame& expanded) {
    m_collapsedFrame = collapsed;
    m_expandedFrame = expanded;

    // Cada contenido se dibuja para SU tamaño, no para el del otro. Durante el viaje, el
    // recorte del contenedor enseña la parte que cabe, que es justo como se lee que uno
    // se está convirtiendo en el otro.
    m_collapsed.Place(0.0f, 0.0f, collapsed.width, collapsed.height);
    m_expanded.Place(0.0f, 0.0f, expanded.width, expanded.height);
}

void Morph::Snap() {
    const Frame& frame = Current();
    m_root.StopAnimation(L"Offset");
    m_root.StopAnimation(L"Size");
    m_geometry.StopAnimation(L"Size");
    m_geometry.StopAnimation(L"CornerRadius");

    m_root.Offset({frame.x, frame.y, 0.0f});
    m_root.Size({frame.width, frame.height});
    m_geometry.Size({frame.width, frame.height});
    m_geometry.CornerRadius({frame.radius, frame.radius});

    m_collapsed.Visual().Opacity(m_isExpanded ? 0.0f : 1.0f);
    m_expanded.Visual().Opacity(m_isExpanded ? 1.0f : 0.0f);
}

void Morph::Go(bool expanded, const Motion::Animator& animator, Motion::Kind kind) {
    m_isExpanded = expanded;
    const Frame& frame = Current();

    // Las cuatro a la vez y con el mismo muelle: posición, tamaño del visual, tamaño de
    // la geometría y radio. Si el radio fuese con fotogramas clave, interrumpir a mitad
    // lo haría reempezar desde el radio inicial y se vería el mordisco.
    animator.Offset(m_root, {frame.x, frame.y, 0.0f}, kind);
    animator.SizeTogether(m_root, m_geometry, {frame.width, frame.height}, kind);
    animator.CornerRadius(m_geometry, {frame.radius, frame.radius}, kind);

    // El contenido se cruza más deprisa que la forma, para que ya se lea mientras el
    // muelle todavía se está asentando.
    const float fade = animator.FadeMs(kind);
    animator.Opacity(m_collapsed.Visual(), expanded ? 0.0f : 1.0f, fade);
    animator.Opacity(m_expanded.Visual(), expanded ? 1.0f : 0.0f, fade);
}

void Morph::SetMaterial(Theme::Color color, const Motion::Animator& animator, float durationMs) {
    if (!m_materialBrush) return;
    if (durationMs > 0.0f) {
        animator.Color(m_materialBrush, ToUi(color), durationMs);
    } else {
        m_materialBrush.StopAnimation(L"Color");
        m_materialBrush.Color(ToUi(color));
    }
}

bool Morph::Resize(Device& device, float scale) {
    const bool a = m_collapsed.Resize(device, scale);
    const bool b = m_expanded.Resize(device, scale);
    return a && b;
}

void Morph::Close() {
    m_collapsed.Close();
    m_expanded.Close();
    m_material = nullptr;
    m_geometry = nullptr;
    m_materialBrush = nullptr;
    m_root = nullptr;
}

}  // namespace Gfx
