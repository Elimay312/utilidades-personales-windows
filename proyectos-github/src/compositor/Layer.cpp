#include "compositor/Layer.h"

#include "compositor/Device.h"
#include "compositor/Motion.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Gfx {

bool Layer::Create(const wuc::Compositor& compositor) {
    m_compositor = compositor;
    m_root = compositor.CreateContainerVisual();

    for (int i = 0; i < 2; ++i) {
        m_faces[i] = compositor.CreateSpriteVisual();
        m_faces[i].RelativeSizeAdjustment({1.0f, 1.0f});
        m_faces[i].Opacity(i == 0 ? 1.0f : 0.0f);
        m_root.Children().InsertAtTop(m_faces[i]);
    }
    return true;
}

void Layer::Place(float xDip, float yDip, float widthDip, float heightDip) {
    if (!m_root) return;
    m_root.Offset({xDip, yDip, 0.0f});
    m_root.Size({widthDip, heightDip});
    m_widthDip = widthDip;
    m_heightDip = heightDip;
}

bool Layer::Resize(Device& device, float scale) {
    m_scale = scale;
    if (!m_surfaces[m_front].Resize(device, m_widthDip, m_heightDip, scale)) return false;

    // La de atrás solo si ya existía: mantenerla al día cuesta memoria que casi siempre
    // no se usa, y si no existe se creará en el primer fundido.
    if (m_backReady && !m_surfaces[1 - m_front].Resize(device, m_widthDip, m_heightDip, scale)) {
        m_backReady = false;
    }
    return true;
}

bool Layer::Redraw(Device& device, const Surface::Painter& painter, float durationMs,
                   const Motion::Animator& animator) {
    if (!m_root) return false;

    const bool crossfade = durationMs > 0.0f;

    if (!crossfade) {
        if (!m_surfaces[m_front].Draw(painter)) return false;
        m_faces[m_front].Brush(m_compositor.CreateSurfaceBrush(m_surfaces[m_front].Handle()));
        m_faces[m_front].Opacity(1.0f);
        m_faces[1 - m_front].Opacity(0.0f);
        return true;
    }

    const int back = 1 - m_front;
    if (!m_backReady) {
        if (!m_surfaces[back].Resize(device, m_widthDip, m_heightDip, m_scale)) return false;
        m_backReady = true;
    }
    if (!m_surfaces[back].Draw(painter)) return false;

    m_faces[back].Brush(m_compositor.CreateSurfaceBrush(m_surfaces[back].Handle()));
    animator.Opacity(m_faces[back], 1.0f, durationMs);
    animator.Opacity(m_faces[m_front], 0.0f, durationMs);
    m_front = back;
    return true;
}

void Layer::Close() {
    for (int i = 0; i < 2; ++i) {
        m_surfaces[i].Close();
        m_faces[i] = nullptr;
    }
    m_root = nullptr;
    m_backReady = false;
}

}  // namespace Gfx
