#pragma once

// El rectángulo redondeado translúcido: la superficie de una tarjeta, el fondo de un
// botón, la píldora de selección de la barra lateral y el anillo de foco.
//
// Sale de Gfx::Morph, que ya tenía exactamente este trío —ShapeVisual, geometría de
// rectángulo redondeado y brocha de color— desde la fase 1. Estaba ahí porque las formas
// de Composition no se rellenan con superficies: hay que elegir entre forma con color o
// superficie con textura, y para el material se eligió forma. De paso salió mejor, que
// también quedó anotado: las esquinas las suaviza el rasterizador de formas, mientras que
// un recorte geométrico deja el borde duro.
//
// Con ocho familias de componentes por delante, ese trío se iba a escribir ocho veces.

#include "compositor/MotionSpec.h"
#include "compositor/Winrt.h"
#include "shell/Theme.h"

namespace Motion {
class Animator;
}

namespace Gfx {

class Material {
public:
    // El visual se cuelga de parent y se dimensiona con él. El radio se puede cambiar
    // luego; se pide aquí porque casi nadie lo cambia nunca.
    bool Create(const winrt::Windows::UI::Composition::Compositor& compositor,
                const winrt::Windows::UI::Composition::ContainerVisual& parent,
                float radiusDip);

    // Sobresalir del padre por los cuatro lados. Es el anillo de foco, que tiene que
    // rodear al control sin encogerlo.
    //
    // No es una cuenta a mano: Composition SUMA Visual.Size a
    // RelativeSizeAdjustment * padre.Size, así que con el ajuste relativo a uno y un
    // tamaño de 2d el visual mide siempre lo que el padre más 2d, aunque el padre esté
    // a mitad de una animación de tamaño.
    void SetOutset(float outsetDip);

    // El tamaño del PADRE en DIP. El desbordamiento se suma aquí dentro.
    void SetSize(float widthDip, float heightDip);

    // Soltarse del padre y ponerse donde se le diga. Es la píldora de selección de la
    // barra lateral: un solo material que se desliza entre elementos, que es lo que hace
    // que la selección se mueva en vez de encenderse y apagarse.
    void SetBounds(float xDip, float yDip, float widthDip, float heightDip);
    void AnimateBounds(const Motion::Animator& animator, float xDip, float yDip,
                       float widthDip, float heightDip, Motion::Kind kind);
    void AnimateSize(const Motion::Animator& animator, float widthDip, float heightDip,
                     Motion::Kind kind);

    void SetRadius(float radiusDip);
    void AnimateRadius(const Motion::Animator& animator, float radiusDip, Motion::Kind kind);

    // durationMs a 0 escribe el color directamente; con duración, lo funde.
    void SetColor(Theme::Color color, const Motion::Animator& animator, float durationMs);

    // Un segundo SpriteShape sobre la MISMA geometría: sin relleno y con trazo. Sobre una
    // segunda forma tiene la ventaja de que no hay dos tamaños que mantener a la par.
    bool CreateStroke(float thicknessDip);
    void SetStroke(Theme::Color color, const Motion::Animator& animator, float durationMs);
    void SetStrokeThickness(float thicknessDip);

    const winrt::Windows::UI::Composition::ShapeVisual& Visual() const { return m_visual; }
    const winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry& Geometry() const {
        return m_geometry;
    }
    const winrt::Windows::UI::Composition::CompositionColorBrush& Brush() const {
        return m_brush;
    }
    bool Ready() const { return m_visual != nullptr; }

    void Close();

private:
    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::ShapeVisual m_visual{nullptr};
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry m_geometry{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush m_brush{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush m_strokeBrush{nullptr};
    float m_outset = 0.0f;
};

}  // namespace Gfx
