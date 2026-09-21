#pragma once

// Un trozo de la pantalla dibujado con Direct2D que sabe cambiar de contenido fundiendo
// en vez de parpadear.
//
// Lleva dos superficies porque un fundido cruzado necesita los píxeles viejos y los
// nuevos a la vez, y una superficie redibujada ya no tiene los viejos. La segunda se
// crea solo cuando hace falta un fundido de verdad —o sea, al cambiar el tema—; al
// redimensionar o cambiar de DPI se repinta la de delante y se acabó.

#include "compositor/Winrt.h"

#include "compositor/Surface.h"

namespace Motion {
class Animator;
}

namespace Gfx {

class Device;

class Layer {
public:
    bool Create(const winrt::Windows::UI::Composition::Compositor& compositor);

    const winrt::Windows::UI::Composition::ContainerVisual& Visual() const { return m_root; }

    void Place(float xDip, float yDip, float widthDip, float heightDip);

    // Reserva las texturas al tamaño físico que toque. Después hay que repintar.
    bool Resize(Device& device, float scale);

    // durationMs a 0 repinta sin fundir. Con duración, dibuja en la superficie de atrás
    // y cruza. Devuelve false si se perdió el dispositivo.
    bool Redraw(Device& device, const Surface::Painter& painter, float durationMs,
                const Motion::Animator& animator);

    void Close();

private:
    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual m_root{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual m_faces[2]{nullptr, nullptr};
    Surface m_surfaces[2];
    int m_front = 0;
    float m_widthDip = 0.0f;
    float m_heightDip = 0.0f;
    float m_scale = 1.0f;
    bool m_backReady = false;
};

}  // namespace Gfx
