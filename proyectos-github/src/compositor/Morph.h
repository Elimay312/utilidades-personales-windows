#pragma once

// La transición compartida: un mismo elemento que cambia de sitio, de tamaño y de radio
// mientras su contenido se cruza. En la fase 1 era la tarjeta que se convertía en panel.
//
// **Y NO es lo que usa el inspector de la fase 5**, aunque esta cabecera lo prometiera. Un
// Morph lleva dos Gfx::Layer, o sea dos trozos de píxeles; el inspector tiene un campo de
// texto, botones y una lista, que son elementos con entrada, así que usarlo obligaría a
// dibujar el panel dos veces: una como textura para el viaje y otra como árbol al aterrizar.
// Lo que hace la fase 5 es Element::MorphTo, que anima la forma de un elemento del kit y
// cruza su contenido con SetContentOpacity.
//
// Esto se queda porque sigue siendo la única manera de morfear algo que NO es un elemento
// —píxeles contra píxeles— y la revisión semanal de la fase 7 va de eso: una pila de
// tarjetas que no reciben entrada mientras vuelan.
//
// El material va aparte del contenido: es un ShapeVisual con una geometría de rectángulo
// redondeado rellena de un color. Se probó lo evidente —meter el contenido dentro de la
// propia forma— y no sirve: las formas de Composition rellenan con color y degradados,
// no con superficies. Separarlos tiene además la ventaja de que las esquinas las
// redondea el rasterizador de formas, con suavizado, y no un recorte geométrico, que
// tiene el borde duro.

#include "compositor/Layer.h"
#include "compositor/Material.h"
#include "compositor/MotionSpec.h"
#include "compositor/Winrt.h"
#include "shell/Theme.h"

namespace Motion {
class Animator;
}

namespace Gfx {

class Device;

class Morph {
public:
    // Dónde y cómo está el elemento en cada uno de sus dos estados.
    struct Frame {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float radius = 0.0f;
    };

    bool Create(const winrt::Windows::UI::Composition::Compositor& compositor,
                const winrt::Windows::UI::Composition::ContainerVisual& parent);

    const winrt::Windows::UI::Composition::ContainerVisual& Visual() const { return m_root; }
    Layer& Collapsed() { return m_collapsed; }
    Layer& Expanded() { return m_expanded; }

    // Coloca los dos estados y deja el elemento en el que toque, sin animar.
    void SetFrames(const Frame& collapsed, const Frame& expanded);
    void Snap();

    // Va al otro estado. Llamarlo a mitad de camino es la forma de interrumpir: los
    // muelles retoman valor y velocidad, así que no hay salto ni hay que parar nada.
    void Go(bool expanded, const Motion::Animator& animator, Motion::Kind kind);
    bool IsExpanded() const { return m_isExpanded; }

    void SetMaterial(Theme::Color color, const Motion::Animator& animator, float durationMs);

    bool Resize(Device& device, float scale);
    void Close();

private:
    const Frame& Current() const { return m_isExpanded ? m_expandedFrame : m_collapsedFrame; }

    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual m_root{nullptr};
    // El trío de la fase 1 —forma, geometría y brocha— ahora es Gfx::Material, que lo
    // comparte con los ocho componentes del kit.
    Material m_material;

    Layer m_collapsed;
    Layer m_expanded;

    Frame m_collapsedFrame;
    Frame m_expandedFrame;
    bool m_isExpanded = false;
};

}  // namespace Gfx
