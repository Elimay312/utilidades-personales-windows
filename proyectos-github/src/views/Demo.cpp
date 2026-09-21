#include "views/Demo.h"

#include <algorithm>

#include "compositor/Device.h"
#include "compositor/Motion.h"
#include "compositor/Paint.h"
#include "compositor/Scene.h"
#include "ui/Text.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Views {

namespace {

constexpr float kSidebarWidth = 260.0f;
constexpr float kGutter = 24.0f;
constexpr float kPadding = 20.0f;
constexpr float kCardWidth = 340.0f;
constexpr float kCardHeight = 128.0f;
constexpr float kPanelWidth = 380.0f;
constexpr float kCardRadius = 10.0f;   // radio de tarjeta, tabla de CLAUDE.md
constexpr float kPanelRadius = 14.0f;  // radio de panel
constexpr float kGlyphSize = 10.0f;    // el de los botones de Windows, no el de 16 de la app
constexpr float kDotSize = 8.0f;

// Segoe Fluent Icons. Los mismos glifos que usa Windows en su propia barra de título.
// Por número y no pegando el carácter: viven en el área de uso privado de Unicode,
// así que en el editor son un hueco en blanco y cualquiera los borraría sin verlos.
constexpr wchar_t kMinimizeGlyph[] = {0xE921, 0};  // ChromeMinimize
constexpr wchar_t kMaximizeGlyph[] = {0xE922, 0};  // ChromeMaximize
constexpr wchar_t kRestoreGlyph[] = {0xE923, 0};   // ChromeRestore
constexpr wchar_t kCloseGlyph[] = {0xE8BB, 0};     // ChromeClose

// El rojo de cerrar de Windows 11. No sale de la tabla de CLAUDE.md porque no es un color
// de la aplicación: es el del sistema, y cambiarlo haría que cerrar no se reconociera.
constexpr Theme::Color kCloseHover = Theme::Rgb(0xc42b1c);

struct Row {
    const wchar_t* name;
    const wchar_t* count;
};

constexpr Row kRows[] = {
    {L"Enfoque", L"3"},   {L"Secundario", L"11"},     {L"Algún día", L"27"},
    {L"Archivado", L"8"}, {L"Sin clasificar", L"71"},
};

D2D1_RECT_F Box(float x, float y, float width, float height) {
    return D2D1::RectF(x, y, x + width, y + height);
}

}  // namespace

bool Demo::Create(Gfx::Scene& scene, Gfx::Device& device, Motion::Animator& animator,
                  Ui::Text& text) {
    m_device = &device;
    m_animator = &animator;
    m_text = &text;
    m_compositor = scene.Compositor();

    m_root = m_compositor.CreateContainerVisual();
    m_root.RelativeSizeAdjustment({1.0f, 1.0f});
    scene.Content().Children().InsertAtTop(m_root);

    // El respaldo de Windows 10. En Windows 11 queda a alfa cero y no cuesta nada.
    m_fallbackBrush = m_compositor.CreateColorBrush();
    m_fallback = m_compositor.CreateSpriteVisual();
    m_fallback.RelativeSizeAdjustment({1.0f, 1.0f});
    m_fallback.Brush(m_fallbackBrush);
    m_root.Children().InsertAtTop(m_fallback);

    // La barra lateral es un velo de color sobre la Mica, NO acrílico. La isla midió que
    // CreateHostBackdropBrush se pinta negro en una aplicación Win32 sin empaquetar: se
    // crea sin error y no muestrea nada. Cuatro vecinos lo usan creyendo que funciona.
    m_sidebarBrush = m_compositor.CreateColorBrush();
    m_sidebar = m_compositor.CreateSpriteVisual();
    m_sidebar.Brush(m_sidebarBrush);
    m_root.Children().InsertAtTop(m_sidebar);

    m_separatorBrush = m_compositor.CreateColorBrush();
    m_separator = m_compositor.CreateSpriteVisual();
    m_separator.Brush(m_separatorBrush);
    m_root.Children().InsertAtTop(m_separator);

    if (!m_sidebarText.Create(m_compositor)) return false;
    m_root.Children().InsertAtTop(m_sidebarText.Visual());

    if (!m_title.Create(m_compositor)) return false;
    m_root.Children().InsertAtTop(m_title.Visual());

    constexpr Caption::Zone kZones[] = {Caption::Zone::Minimize, Caption::Zone::Maximize,
                                        Caption::Zone::Close};
    for (std::size_t i = 0; i < m_buttons.size(); ++i) {
        Button& button = m_buttons[i];
        button.zone = kZones[i];
        button.brush = m_compositor.CreateColorBrush();
        button.background = m_compositor.CreateSpriteVisual();
        button.background.Brush(button.brush);
        button.background.Opacity(0.0f);
        m_root.Children().InsertAtTop(button.background);
        if (!button.glyph.Create(m_compositor)) return false;
        m_root.Children().InsertAtTop(button.glyph.Visual());
    }

    if (!m_morph.Create(m_compositor, m_root)) return false;
    // El centro de escalado atado al centro del propio visual: así el "pulsar encoge"
    // sigue siendo correcto aunque el tamaño esté a mitad de la transición.
    animator.BindCenterPoint(m_morph.Visual());
    return true;
}

void Demo::Layout(float widthDip, float heightDip, float scale, const Caption::Layout& caption,
                  bool hasMica) {
    if (!m_root || !m_device) return;
    m_width = widthDip;
    m_height = heightDip;
    m_scale = scale;
    m_caption = caption;

    m_fallback.Opacity(hasMica ? 0.0f : 1.0f);

    m_sidebar.Offset({0.0f, 0.0f, 0.0f});
    m_sidebar.Size({kSidebarWidth, heightDip});

    // Un píxel de verdad, no un DIP: a 150 % un separador de 1 DIP son 1,5 px y sale
    // borroso. El separador es la línea más fina que la pantalla sabe dibujar.
    m_separator.Offset({kSidebarWidth, 0.0f, 0.0f});
    m_separator.Size({1.0f / scale, heightDip});

    m_sidebarText.Place(0.0f, Caption::kBarHeight, kSidebarWidth, std::max(heightDip - Caption::kBarHeight, 1.0f));
    m_sidebarText.Resize(*m_device, scale);

    m_title.Place(kPadding, 0.0f, std::max(kSidebarWidth - kPadding, 1.0f), Caption::kBarHeight);
    m_title.Resize(*m_device, scale);

    for (Button& button : m_buttons) {
        const Caption::Rect& rect = Caption::ButtonRect(caption, button.zone);
        button.background.Offset({rect.x, rect.y, 0.0f});
        button.background.Size({rect.width, rect.height});
        button.glyph.Place(rect.x, rect.y, rect.width, rect.height);
        button.glyph.Resize(*m_device, scale);
    }

    const float contentTop = Caption::kBarHeight + kGutter;
    const float panelHeight = std::max(heightDip - contentTop - kGutter, 80.0f);

    Gfx::Morph::Frame card;
    card.x = kSidebarWidth + kGutter;
    card.y = contentTop;
    card.width = std::min(kCardWidth, std::max(widthDip - card.x - kGutter, 120.0f));
    card.height = kCardHeight;
    card.radius = kCardRadius;

    Gfx::Morph::Frame panel;
    panel.width = std::min(kPanelWidth, std::max(widthDip - kSidebarWidth - kGutter * 2.0f, 160.0f));
    panel.x = std::max(widthDip - panel.width - kGutter, card.x);
    panel.y = contentTop;
    panel.height = panelHeight;
    panel.radius = kPanelRadius;

    m_morph.SetFrames(card, panel);
    m_morph.Resize(*m_device, scale);
    // Sin animar: redimensionar la ventana no es una transición, es la nueva realidad.
    m_morph.Snap();
}

void Demo::Repaint(const Theme::Tokens& tokens, float crossfadeMs) {
    if (!m_root || !m_device || !m_animator || !m_text) return;
    m_tokens = tokens;

    const bool animate = crossfadeMs > 0.0f;

    // Los materiales de color se cruzan solos: una brocha que se va al color nuevo es un
    // fundido de verdad, en la GPU y sin repintar un píxel.
    const auto setColor = [&](const wuc::CompositionColorBrush& brush, Theme::Color color) {
        if (animate) {
            m_animator->Color(brush, Gfx::ToUi(color), crossfadeMs);
        } else {
            brush.StopAnimation(L"Color");
            brush.Color(Gfx::ToUi(color));
        }
    };

    // En Windows 10 el fondo lo ponemos nosotros. El mismo color de la tarjeta pero
    // opaco: sin Mica detrás, translúcido solo enseñaría el escritorio.
    Theme::Color base = tokens.cardSurface;
    base.a = 255;
    setColor(m_fallbackBrush, base);
    setColor(m_sidebarBrush, tokens.sidebarVeil);
    setColor(m_separatorBrush, tokens.separator);
    m_morph.SetMaterial(tokens.cardSurface, *m_animator, crossfadeMs);

    Ui::Text& text = *m_text;
    const Theme::Tokens palette = tokens;

    m_title.Redraw(*m_device, [&text, palette](const Gfx::Surface::Canvas& canvas) {
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.textPrimary), brush.put());
        text.DrawLine(canvas.dc, L"Brújula", Ui::Style::Body, Ui::Weight::Semibold,
                      Box(0.0f, 0.0f, canvas.width, canvas.height), brush.get());
    }, crossfadeMs, *m_animator);

    m_sidebarText.Redraw(*m_device, [&text, palette](const Gfx::Surface::Canvas& canvas) {
        winrt::com_ptr<ID2D1SolidColorBrush> primary;
        winrt::com_ptr<ID2D1SolidColorBrush> secondary;
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.textPrimary), primary.put());
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.textSecondary), secondary.put());

        float y = 8.0f;
        for (const Row& row : kRows) {
            constexpr float kRowHeight = 34.0f;
            text.DrawLine(canvas.dc, row.name, Ui::Style::Body, Ui::Weight::Regular,
                          Box(kPadding, y, canvas.width - kPadding * 2.0f - 40.0f, kRowHeight),
                          primary.get());
            text.DrawLine(canvas.dc, row.count, Ui::Style::Caption, Ui::Weight::Regular,
                          Box(canvas.width - kPadding - 36.0f, y, 36.0f, kRowHeight),
                          secondary.get());
            y += kRowHeight;
        }
    }, crossfadeMs, *m_animator);

    m_morph.Collapsed().Redraw(*m_device, [&text, palette](const Gfx::Surface::Canvas& canvas) {
        winrt::com_ptr<ID2D1SolidColorBrush> primary;
        winrt::com_ptr<ID2D1SolidColorBrush> secondary;
        winrt::com_ptr<ID2D1SolidColorBrush> dot;
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.textPrimary), primary.put());
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.textSecondary), secondary.put());
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.activityActive), dot.put());

        const float inner = canvas.width - kPadding * 2.0f;
        text.DrawLine(canvas.dc, L"brujula", Ui::Style::Heading, Ui::Weight::Semibold,
                      Box(kPadding, 14.0f, inner, 28.0f), primary.get());
        // El siguiente paso es lo más visible después del nombre, que es el motivo de ser
        // de toda la aplicación.
        text.DrawLine(canvas.dc, L"Terminar la fase 1 y medir los destellos", Ui::Style::Body,
                      Ui::Weight::Regular, Box(kPadding, 46.0f, inner, 22.0f), primary.get());

        const float dotY = canvas.height - kPadding - kDotSize;
        canvas.dc->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(kPadding + kDotSize * 0.5f, dotY + kDotSize * 0.5f),
                          kDotSize * 0.5f, kDotSize * 0.5f),
            dot.get());
        text.DrawLine(canvas.dc, L"Activo · C++ · hace 2 días", Ui::Style::Footnote,
                      Ui::Weight::Regular,
                      Box(kPadding + kDotSize + 8.0f, dotY - 4.0f, inner - kDotSize - 8.0f, 16.0f),
                      secondary.get());
    }, crossfadeMs, *m_animator);

    m_morph.Expanded().Redraw(*m_device, [&text, palette](const Gfx::Surface::Canvas& canvas) {
        winrt::com_ptr<ID2D1SolidColorBrush> primary;
        winrt::com_ptr<ID2D1SolidColorBrush> secondary;
        winrt::com_ptr<ID2D1SolidColorBrush> accent;
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.textPrimary), primary.put());
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.textSecondary), secondary.put());
        canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(palette.accent), accent.put());

        const float inner = canvas.width - kPadding * 2.0f;
        text.DrawLine(canvas.dc, L"brujula", Ui::Style::Title, Ui::Weight::Semibold,
                      Box(kPadding, 20.0f, inner, 36.0f), primary.get());
        text.DrawLine(canvas.dc, L"Siguiente paso", Ui::Style::Footnote, Ui::Weight::Semibold,
                      Box(kPadding, 68.0f, inner, 16.0f), accent.get());
        text.DrawLine(canvas.dc, L"Terminar la fase 1 y medir los destellos", Ui::Style::Body,
                      Ui::Weight::Regular, Box(kPadding, 88.0f, inner, 22.0f), primary.get());
        text.DrawLine(canvas.dc, L"Novedades", Ui::Style::Footnote, Ui::Weight::Semibold,
                      Box(kPadding, 128.0f, inner, 16.0f), accent.get());
        text.DrawLine(canvas.dc, L"21 sep — Ventana con Mica y barra propia.",
                      Ui::Style::Caption, Ui::Weight::Regular, Box(kPadding, 148.0f, inner, 20.0f),
                      secondary.get());
        text.DrawLine(canvas.dc, L"Pulsa Esc para volver a la tarjeta.", Ui::Style::Caption,
                      Ui::Weight::Regular, Box(kPadding, 172.0f, inner, 20.0f), secondary.get());
    }, crossfadeMs, *m_animator);

    PaintButtons(crossfadeMs);
}

void Demo::PaintButtons(float crossfadeMs) {
    for (Button& button : m_buttons) {
        const bool hovered = m_hovered == button.zone;
        const bool isClose = button.zone == Caption::Zone::Close;

        Theme::Color background = isClose ? kCloseHover : m_tokens.textPrimary;
        if (!isClose) background.a = 28;  // un velo, no un bloque
        button.brush.StopAnimation(L"Color");
        button.brush.Color(Gfx::ToUi(background));

        // Sobre el rojo de cerrar, el glifo se vuelve blanco. Es lo único que obliga a
        // repintar al pasar el ratón, y es una superficie de 46x48.
        Theme::Color ink = m_tokens.textPrimary;
        if (isClose && hovered) ink = Theme::Rgb(0xffffff);
        if (m_pressed == button.zone) ink.a = 180;

        const wchar_t* glyph = kCloseGlyph;
        if (button.zone == Caption::Zone::Minimize) glyph = kMinimizeGlyph;
        // Maximizada, el botón enseña restaurar. Es el mismo botón y la misma acción de
        // Windows; lo único nuestro es el dibujo, y si no cambia, miente.
        if (button.zone == Caption::Zone::Maximize) {
            glyph = m_caption.maximized ? kRestoreGlyph : kMaximizeGlyph;
        }

        Ui::Text& text = *m_text;
        button.glyph.Redraw(*m_device, [&text, ink, glyph](const Gfx::Surface::Canvas& canvas) {
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            canvas.dc->CreateSolidColorBrush(Gfx::ToD2D(ink), brush.put());
            text.DrawGlyph(canvas.dc, glyph, kGlyphSize, Box(0.0f, 0.0f, canvas.width, canvas.height),
                           brush.get());
        }, crossfadeMs, *m_animator);
    }
}

void Demo::SetCaptionState(Caption::Zone hovered, Caption::Zone pressed) {
    if (!m_animator) return;
    const bool hoverChanged = m_hovered != hovered;
    m_hovered = hovered;
    m_pressed = pressed;

    for (Button& button : m_buttons) {
        const bool on = m_hovered == button.zone;
        // Muelle rígido: es una interacción pequeña, la de la primera fila de la tabla.
        m_animator->Opacity(button.background, on ? 1.0f : 0.0f, m_animator->FadeMs(Motion::Kind::Snappy));
    }

    if (hoverChanged) PaintButtons(m_animator->FadeMs(Motion::Kind::Snappy));
}

bool Demo::InContent(float xDip, float yDip) const {
    return xDip > kSidebarWidth && yDip > Caption::kBarHeight;
}

void Demo::ShowPanel(bool panel) {
    if (!m_animator) return;
    m_panel = panel;
    // Muelle estándar: paneles e inspector, segunda fila de la tabla. Llamar a esto a
    // mitad del viaje es exactamente cómo se interrumpe: el muelle retoma valor y
    // velocidad y se va al otro lado sin pasar por ningún salto.
    m_morph.Go(panel, *m_animator, Motion::Kind::Standard);
}

void Demo::PointerDown(float xDip, float yDip) {
    if (!m_animator || !InContent(xDip, yDip)) return;
    m_cardPressed = true;
    m_animator->Scale(m_morph.Visual(), {0.97f, 0.97f, 1.0f}, Motion::Kind::Snappy);
}

void Demo::PointerUp(float xDip, float yDip) {
    if (!m_animator) return;
    if (m_cardPressed) {
        m_cardPressed = false;
        m_animator->Scale(m_morph.Visual(), {1.0f, 1.0f, 1.0f}, Motion::Kind::Snappy);
    }
    if (InContent(xDip, yDip)) ShowPanel(!m_panel);
}

void Demo::KeyDown(int virtualKey) {
    if (virtualKey == VK_ESCAPE && m_panel) ShowPanel(false);
}

void Demo::Close() {
    m_morph.Close();
    for (Button& button : m_buttons) {
        button.glyph.Close();
        button.background = nullptr;
        button.brush = nullptr;
    }
    m_title.Close();
    m_sidebarText.Close();
    m_separator = nullptr;
    m_sidebar = nullptr;
    m_fallback = nullptr;
    m_root = nullptr;
}

}  // namespace Views
