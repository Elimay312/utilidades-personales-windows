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
    // El kit se monta DESPUÉS de la demo: sus dos raíces entran por encima, y así el
    // catálogo y las capas flotantes quedan sobre la vista de la fase 1.
    if (!m_host.Create(m_scene, m_device, m_animator, m_text, m_window.Handle())) return false;
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);

    m_window.callbacks.onLayout = [this](float width, float height, float scale) {
        m_scene.Layout(width, height, scale);
        m_demo.Layout(width, height, scale, m_window.CaptionLayout(), m_window.HasMica());
        // Sin cruce: redimensionar o cambiar de monitor no es una transición entre dos
        // estados, es el mismo estado en otro tamaño. Cruzarlo se vería como un rastro.
        m_demo.Repaint(m_theme.Tokens(), 0.0f);
        m_host.Layout(width, height, scale);
        // El repintado que deja el layout no puede esperar al siguiente mensaje: entre
        // medias se vería un fotograma con las texturas del tamaño viejo.
        m_host.FlushNow();
    };

    m_window.callbacks.onTheme = [this] {
        m_theme.Refresh();
        ApplyTheme(Motion::kThemeCrossfadeMs);
    };

    m_window.callbacks.onMotionSetting = [this] { m_animator.RefreshSystemPreference(); };

    m_window.callbacks.onCaptionState = [this] {
        m_demo.SetCaptionState(m_window.Hovered(), m_window.Pressed());
    };

    WireInput();

    // El primer fotograma completo, con la ventana todavía escondida.
    m_scene.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_demo.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale(),
                  m_window.CaptionLayout(), m_window.HasMica());
    ApplyTheme(0.0f);
    return true;
}

void Application::WireInput() {
    // El reparto: con el catálogo abierto manda el kit; con él cerrado, la vista de la
    // fase 1, que conserva su interfaz de siempre y no se ha tocado.
    m_window.callbacks.onPointer = [this](const Input::Pointer& pointer) {
        if (CatalogOpen()) {
            m_host.Input().Pointer(pointer);
            return;
        }
        if (pointer.button != Input::Button::Left) return;
        if (pointer.action == Input::Action::Down) m_demo.PointerDown(pointer.x, pointer.y);
        if (pointer.action == Input::Action::Up) m_demo.PointerUp(pointer.x, pointer.y);
    };

    m_window.callbacks.onKey = [this](const Input::Key& key) {
        if (!key.down) return false;
#if BRUJULA_CATALOGO
        if (key.virtualKey == VK_F12) {
            ToggleCatalog();
            return true;
        }
#endif
        if (CatalogOpen()) return m_host.Input().Key(key);
        m_demo.KeyDown(key.virtualKey);
        return true;
    };

    m_window.callbacks.onChar = [this](wchar_t unit) {
        if (CatalogOpen()) m_host.Input().Char(unit);
    };

    m_window.callbacks.onSetCursor = [this](float x, float y) {
        if (!CatalogOpen()) return false;
        const wchar_t* cursor = m_host.Input().CursorAt(x, y);
        if (!cursor) return false;
        SetCursor(LoadCursorW(nullptr, cursor));
        return true;
    };

    m_window.callbacks.onWindowFocus = [this](bool focused) {
        m_host.Input().WindowFocus(focused);
    };

    m_window.callbacks.onDeactivate = [this] { m_host.PopAllLayers(); };

    m_window.callbacks.onContextMenu = [this](float x, float y, bool keyboard) {
        if (!CatalogOpen() || keyboard) return;
        Input::Pointer pointer;
        pointer.action = Input::Action::Down;
        pointer.button = Input::Button::Right;
        pointer.x = x;
        pointer.y = y;
        m_host.Input().Pointer(pointer);
    };

    m_window.callbacks.onCaretRect = [this](float& x, float& y, float& height) {
        Ui::Rect caret;
        if (!m_host.CaretRect(caret)) return false;
        x = caret.x;
        y = caret.y;
        height = caret.height;
        return true;
    };

    m_window.callbacks.onFlush = [this] { m_host.FlushNow(); };
}

#if BRUJULA_CATALOGO
void Application::ToggleCatalog() {
    if (m_catalog) {
        m_host.PopAllLayers();
        m_host.ClearRoot();
        m_catalog = nullptr;
        m_demo.ShowContent(true);
        return;
    }
    m_demo.ShowContent(false);
    m_catalog = m_host.SetRoot<Views::Catalog>();
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}
#endif

void Application::ApplyTheme(float crossfadeMs) {
    // El marco lo pinta DWM, así que hay que decírselo aparte. Sin esto, una aplicación
    // entera en oscuro lleva una raya clara de borde.
    Shell::Backdrop::SetDarkFrame(m_window.Handle(),
                                  m_theme.Appearance() == Theme::Appearance::Dark);
    m_demo.Repaint(m_theme.Tokens(), crossfadeMs);
    m_host.ApplyTheme(m_theme.Tokens(), crossfadeMs);
    m_host.FlushNow();
}

int Application::Run() {
    m_window.Show(SW_SHOW);
    return m_window.Run();
}

void Application::Shutdown() {
    m_host.Close();
    m_demo.Close();
    m_theme.Close();
    m_scene.Close();
}

}  // namespace App
