#include "compositor/Motion.h"

#include <Windows.h>

#include <chrono>
#include <cmath>

namespace wuc = winrt::Windows::UI::Composition;
namespace num = winrt::Windows::Foundation::Numerics;

namespace Motion {

namespace {
std::chrono::milliseconds Ms(float value) {
    return std::chrono::milliseconds(static_cast<long long>(std::lround(value)));
}
}  // namespace

void Animator::Attach(const wuc::Compositor& compositor) {
    m_compositor = compositor;
    RefreshSystemPreference();
}

void Animator::RefreshSystemPreference() {
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        m_systemAnimations = enabled != FALSE;
    }
}

float Animator::FadeMs(Kind kind) const {
    return Resolve(kind, m_systemAnimations).fadeMs;
}

wuc::CompositionEasingFunction Animator::Ease() const {
    // Sale deprisa y frena largo. Es la curva que hace que un fundido parezca que llega
    // en vez de que aparece.
    return m_compositor.CreateCubicBezierEasingFunction({0.16f, 1.0f}, {0.3f, 1.0f});
}

void Animator::Offset(const wuc::Visual& visual, const num::float3& value, Kind kind) const {
    if (!m_systemAnimations) {
        visual.StopAnimation(L"Offset");
        visual.Offset(value);
        return;
    }
    const Spring spring = SpringFor(kind);
    auto animation = m_compositor.CreateSpringVector3Animation();
    animation.DampingRatio(spring.dampingRatio);
    animation.Period(Ms(spring.periodMs));
    animation.FinalValue(value);
    visual.StartAnimation(L"Offset", animation);
}

void Animator::Scale(const wuc::Visual& visual, const num::float3& value, Kind kind) const {
    if (!m_systemAnimations) {
        visual.StopAnimation(L"Scale");
        visual.Scale(value);
        return;
    }
    const Spring spring = SpringFor(kind);
    auto animation = m_compositor.CreateSpringVector3Animation();
    animation.DampingRatio(spring.dampingRatio);
    animation.Period(Ms(spring.periodMs));
    animation.FinalValue(value);
    visual.StartAnimation(L"Scale", animation);
}

void Animator::Opacity(const wuc::Visual& visual, float value, float durationMs) const {
    // La opacidad se funde siempre, también sin animaciones del sistema: quien las apaga
    // pide que no se mueva nada, no que las cosas parpadeen de golpe.
    const float duration = m_systemAnimations ? durationMs : kReducedFadeMs;
    auto animation = m_compositor.CreateScalarKeyFrameAnimation();
    animation.Duration(Ms(duration));
    animation.InsertKeyFrame(1.0f, value, Ease());
    visual.StartAnimation(L"Opacity", animation);
}

void Animator::Color(const wuc::CompositionColorBrush& brush,
                     const winrt::Windows::UI::Color& value, float durationMs) const {
    const float duration = m_systemAnimations ? durationMs : kReducedFadeMs;
    auto animation = m_compositor.CreateColorKeyFrameAnimation();
    animation.Duration(Ms(duration));
    animation.InsertKeyFrame(1.0f, value, Ease());
    brush.StartAnimation(L"Color", animation);
}

void Animator::SizeTogether(const wuc::Visual& visual,
                            const wuc::CompositionRoundedRectangleGeometry& geometry,
                            const num::float2& value, Kind kind) const {
    if (!m_systemAnimations) {
        visual.StopAnimation(L"Size");
        geometry.StopAnimation(L"Size");
        visual.Size(value);
        geometry.Size(value);
        return;
    }
    const Spring spring = SpringFor(kind);
    auto animation = m_compositor.CreateSpringVector2Animation();
    animation.DampingRatio(spring.dampingRatio);
    animation.Period(Ms(spring.periodMs));
    animation.FinalValue(value);
    // El mismo objeto de animación en los dos destinos. Dos animaciones iguales por
    // separado también coincidirían mientras nadie las interrumpa; con una sola no hay
    // que confiar en eso.
    visual.StartAnimation(L"Size", animation);
    geometry.StartAnimation(L"Size", animation);
}

void Animator::CornerRadius(const wuc::CompositionRoundedRectangleGeometry& geometry,
                            const num::float2& value, Kind kind) const {
    if (!m_systemAnimations) {
        geometry.StopAnimation(L"CornerRadius");
        geometry.CornerRadius(value);
        return;
    }
    const Spring spring = SpringFor(kind);
    auto animation = m_compositor.CreateSpringVector2Animation();
    animation.DampingRatio(spring.dampingRatio);
    animation.Period(Ms(spring.periodMs));
    animation.FinalValue(value);
    geometry.StartAnimation(L"CornerRadius", animation);
}

void Animator::BindCenterPoint(const wuc::Visual& visual) const {
    auto expression = m_compositor.CreateExpressionAnimation(
        L"Vector3(this.Target.Size.X * 0.5, this.Target.Size.Y * 0.5, 0)");
    visual.StartAnimation(L"CenterPoint", expression);
}

}  // namespace Motion
