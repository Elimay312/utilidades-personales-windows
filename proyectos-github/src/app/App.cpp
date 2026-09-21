#include "app/App.h"

#include "shell/Backdrop.h"

namespace App {

namespace {
constexpr float kInitialWidthDip = 1180.0f;
constexpr float kInitialHeightDip = 760.0f;
}  // namespace

bool Application::Init(HINSTANCE instance) {
    // STA antes que nada: el compositor se cuelga del apartamento de este hilo.
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // La ventana nace oculta. Todo lo que sigue ocurre antes de que se vea un píxel.
    if (!m_window.Create(instance, L"Brújula", kInitialWidthDip, kInitialHeightDip)) return false;
    if (!m_scene.Create(m_window.Handle())) return false;
    if (!m_device.Create(m_scene.Compositor())) return false;
    if (!m_text.Create(m_device.Write())) return false;

    m_animator.Attach(m_scene.Compositor());
    m_theme.Create(m_window.Handle(), Shell::Window::kThemeMessage);

    if (!m_demo.Create(m_scene, m_device, m_animator, m_text)) return false;

    m_window.callbacks.onLayout = [this](float width, float height, float scale) {
        m_scene.Layout(width, height, scale);
        m_demo.Layout(width, height, scale, m_window.CaptionLayout(), m_window.HasMica());
        // Sin cruce: redimensionar o cambiar de monitor no es una transición entre dos
        // estados, es el mismo estado en otro tamaño. Cruzarlo se vería como un rastro.
        m_demo.Repaint(m_theme.Tokens(), 0.0f);
    };

    m_window.callbacks.onTheme = [this] {
        m_theme.Refresh();
        ApplyTheme(Motion::kThemeCrossfadeMs);
    };

    m_window.callbacks.onMotionSetting = [this] { m_animator.RefreshSystemPreference(); };

    m_window.callbacks.onCaptionState = [this] {
        m_demo.SetCaptionState(m_window.Hovered(), m_window.Pressed());
    };

    m_window.callbacks.onPointerDown = [this](float x, float y) { m_demo.PointerDown(x, y); };
    m_window.callbacks.onPointerUp = [this](float x, float y) { m_demo.PointerUp(x, y); };
    m_window.callbacks.onKeyDown = [this](int key) { m_demo.KeyDown(key); };

    // El primer fotograma completo, con la ventana todavía escondida.
    m_scene.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_demo.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale(),
                  m_window.CaptionLayout(), m_window.HasMica());
    ApplyTheme(0.0f);
    return true;
}

void Application::ApplyTheme(float crossfadeMs) {
    // El marco lo pinta DWM, así que hay que decírselo aparte. Sin esto, una aplicación
    // entera en oscuro lleva una raya clara de borde.
    Shell::Backdrop::SetDarkFrame(m_window.Handle(),
                                  m_theme.Appearance() == Theme::Appearance::Dark);
    m_demo.Repaint(m_theme.Tokens(), crossfadeMs);
}

int Application::Run() {
    m_window.Show(SW_SHOW);
    return m_window.Run();
}

void Application::Shutdown() {
    m_demo.Close();
    m_theme.Close();
    m_scene.Close();
}

}  // namespace App
