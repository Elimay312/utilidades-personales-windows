#pragma once

// La demo de la fase 1, y se tira en la fase 4: una barra lateral translúcida, una
// tarjeta y el panel en que se convierte.
//
// Está aquí para poder mirar de frente las cuatro cosas que esta fase tenía que resolver
// —material, tipografía, muelles y transición compartida— antes de que haya datos que
// distraigan. Lo único que sobrevive de este archivo es lo que usa: Morph, Layer y
// Animator, que sí son definitivos.

#include <array>

#include "compositor/Layer.h"
#include "compositor/Morph.h"
#include "compositor/Winrt.h"
#include "shell/Caption.h"
#include "shell/Theme.h"

namespace Gfx {
class Device;
class Scene;
}  // namespace Gfx

namespace Motion {
class Animator;
}

namespace Ui {
class Text;
}

namespace Views {

class Demo {
public:
    bool Create(Gfx::Scene& scene, Gfx::Device& device, Motion::Animator& animator, Ui::Text& text);

    // Recoloca todo y vuelve a reservar las texturas. Al crear, al redimensionar y al
    // cambiar de monitor.
    void Layout(float widthDip, float heightDip, float scale, const Caption::Layout& caption,
                bool hasMica);

    // Repinta con los tokens del tema. Con crossfadeMs a 0 es instantáneo (redimensionar);
    // con duración, cruza (cambio de tema).
    void Repaint(const Theme::Tokens& tokens, float crossfadeMs);

    void SetCaptionState(Caption::Zone hovered, Caption::Zone pressed);
    void PointerDown(float xDip, float yDip);
    void PointerUp(float xDip, float yDip);
    void KeyDown(int virtualKey);

    void Close();

private:
    struct Button {
        Caption::Zone zone = Caption::Zone::Client;
        winrt::Windows::UI::Composition::SpriteVisual background{nullptr};
        winrt::Windows::UI::Composition::CompositionColorBrush brush{nullptr};
        Gfx::Layer glyph;
    };

    bool InContent(float xDip, float yDip) const;
    void ShowPanel(bool panel);
    void PaintButtons(float crossfadeMs);

    Gfx::Device* m_device = nullptr;
    Motion::Animator* m_animator = nullptr;
    Ui::Text* m_text = nullptr;

    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual m_root{nullptr};
    // Solo se usa en Windows 10, donde no hay Mica que enseñar.
    winrt::Windows::UI::Composition::SpriteVisual m_fallback{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush m_fallbackBrush{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual m_sidebar{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush m_sidebarBrush{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual m_separator{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush m_separatorBrush{nullptr};

    Gfx::Layer m_sidebarText;
    Gfx::Layer m_title;
    std::array<Button, 3> m_buttons;
    Gfx::Morph m_morph;

    Theme::Tokens m_tokens;
    Caption::Layout m_caption;
    Caption::Zone m_hovered = Caption::Zone::Client;
    Caption::Zone m_pressed = Caption::Zone::Client;
    float m_width = 0.0f;
    float m_height = 0.0f;
    float m_scale = 1.0f;
    bool m_panel = false;
    bool m_cardPressed = false;
};

}  // namespace Views
