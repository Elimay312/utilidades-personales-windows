#include "compositor/Material.h"

#include "compositor/Motion.h"
#include "compositor/Paint.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Gfx {

bool Material::Create(const wuc::Compositor& compositor, const wuc::ContainerVisual& parent,
                      float radiusDip) {
    m_compositor = compositor;

    m_geometry = compositor.CreateRoundedRectangleGeometry();
    m_geometry.CornerRadius({radiusDip, radiusDip});
    m_brush = compositor.CreateColorBrush();

    auto shape = compositor.CreateSpriteShape(m_geometry);
    shape.FillBrush(m_brush);

    m_visual = compositor.CreateShapeVisual();
    m_visual.RelativeSizeAdjustment({1.0f, 1.0f});
    m_visual.Shapes().Append(shape);
    // Abajo del todo: el material es el fondo de su elemento, y encima van los hijos y
    // el contenido dibujado. En Morph da igual porque es el primero en entrar.
    parent.Children().InsertAtBottom(m_visual);
    return true;
}

void Material::SetOutset(float outsetDip) {
    if (!m_visual) return;
    m_outset = outsetDip;
    // El ajuste relativo sigue a uno: el tamaño que se escribe aquí se SUMA al del padre,
    // no lo sustituye. Por eso el anillo sigue rodeando al control aunque el control esté
    // creciendo con un muelle.
    m_visual.Size({outsetDip * 2.0f, outsetDip * 2.0f});
    m_visual.Offset({-outsetDip, -outsetDip, 0.0f});
}

void Material::SetSize(float widthDip, float heightDip) {
    if (!m_geometry) return;
    m_geometry.StopAnimation(L"Size");
    m_geometry.Size({widthDip + m_outset * 2.0f, heightDip + m_outset * 2.0f});
}

void Material::AnimateSize(const Motion::Animator& animator, float widthDip, float heightDip,
                           Motion::Kind kind) {
    if (!m_geometry) return;
    animator.GeometrySize(m_geometry, {widthDip + m_outset * 2.0f, heightDip + m_outset * 2.0f},
                          kind);
}

void Material::SetBounds(float xDip, float yDip, float widthDip, float heightDip) {
    if (!m_visual) return;
    // Se corta el ajuste relativo: a partir de aquí el material tiene vida propia y ya no
    // sigue al padre.
    m_visual.RelativeSizeAdjustment({0.0f, 0.0f});
    m_visual.StopAnimation(L"Offset");
    m_visual.StopAnimation(L"Size");
    m_visual.Offset({xDip, yDip, 0.0f});
    m_visual.Size({widthDip, heightDip});
    m_geometry.StopAnimation(L"Size");
    m_geometry.Size({widthDip, heightDip});
}

void Material::AnimateBounds(const Motion::Animator& animator, float xDip, float yDip,
                             float widthDip, float heightDip, Motion::Kind kind) {
    if (!m_visual) return;
    m_visual.RelativeSizeAdjustment({0.0f, 0.0f});
    animator.Offset(m_visual, {xDip, yDip, 0.0f}, kind);
    // Los dos tamaños con la misma animación: si la forma y el visual se movieran por
    // separado, el relleno asomaría o se quedaría corto durante el viaje.
    animator.SizeTogether(m_visual, m_geometry, {widthDip, heightDip}, kind);
}

void Material::SetRadius(float radiusDip) {
    if (!m_geometry) return;
    m_geometry.StopAnimation(L"CornerRadius");
    m_geometry.CornerRadius({radiusDip, radiusDip});
}

void Material::AnimateRadius(const Motion::Animator& animator, float radiusDip,
                             Motion::Kind kind) {
    if (!m_geometry) return;
    animator.CornerRadius(m_geometry, {radiusDip, radiusDip}, kind);
}

void Material::SetColor(Theme::Color color, const Motion::Animator& animator, float durationMs) {
    if (!m_brush) return;
    if (durationMs > 0.0f) {
        animator.Color(m_brush, ToUi(color), durationMs);
    } else {
        m_brush.StopAnimation(L"Color");
        m_brush.Color(ToUi(color));
    }
}

bool Material::CreateStroke(float thicknessDip) {
    if (!m_visual || m_strokeBrush) return m_strokeBrush != nullptr;

    m_strokeBrush = m_compositor.CreateColorBrush();
    auto shape = m_compositor.CreateSpriteShape(m_geometry);
    shape.StrokeBrush(m_strokeBrush);
    shape.StrokeThickness(thicknessDip);
    // Sin FillBrush: el relleno ya lo pone la otra forma, y ponerlo dos veces oscurece el
    // material al doble de lo que dice el token.
    m_visual.Shapes().Append(shape);
    return true;
}

void Material::SetStroke(Theme::Color color, const Motion::Animator& animator,
                         float durationMs) {
    if (!m_strokeBrush) return;
    if (durationMs > 0.0f) {
        animator.Color(m_strokeBrush, ToUi(color), durationMs);
    } else {
        m_strokeBrush.StopAnimation(L"Color");
        m_strokeBrush.Color(ToUi(color));
    }
}

void Material::SetStrokeThickness(float thicknessDip) {
    if (!m_visual || m_visual.Shapes().Size() < 2) return;
    m_visual.Shapes().GetAt(1).as<wuc::CompositionSpriteShape>().StrokeThickness(thicknessDip);
}

void Material::Close() {
    m_strokeBrush = nullptr;
    m_brush = nullptr;
    m_geometry = nullptr;
    m_visual = nullptr;
    m_compositor = nullptr;
}

}  // namespace Gfx
