#include "views/Chrome.h"

#include <algorithm>

#include "compositor/Motion.h"
#include "compositor/Paint.h"
#include "ui/Host.h"
#include "ui/Text.h"

namespace Views {

namespace {

using Ui::Rect;

// El mismo aire que usa la barra lateral por su izquierda: el nombre de la aplicación y el
// primer grupo tienen que caer en la misma vertical o la columna se ve torcida.
constexpr float kEdge = Metrics::kSpace4;
constexpr float kGlyphSize = 10.0f;  // el de los botones de Windows, no el de 16 de la app
constexpr float kBeaconSize = 8.0f;
constexpr float kBeaconGap = Metrics::kSpace1;
constexpr float kPulsePeriodMs = 1400.0f;

// Segoe Fluent Icons. Los mismos glifos que usa Windows en su propia barra de título.
// Por número y no pegando el carácter: viven en el área de uso privado de Unicode, así que
// en el editor son un hueco en blanco y cualquiera los borraría sin verlos.
constexpr wchar_t kMinimizeGlyph[] = {0xE921, 0};  // ChromeMinimize
constexpr wchar_t kMaximizeGlyph[] = {0xE922, 0};  // ChromeMaximize
constexpr wchar_t kRestoreGlyph[] = {0xE923, 0};   // ChromeRestore
constexpr wchar_t kCloseGlyph[] = {0xE8BB, 0};     // ChromeClose

// El rojo de cerrar de Windows 11. No sale de la tabla de CLAUDE.md porque no es un color
// de la aplicación: es el del sistema, y cambiarlo haría que cerrar no se reconociera.
constexpr Theme::Color kCloseHover = Theme::Rgb(0xc42b1c);

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

}  // namespace

// ================================================================== El botón de ventana ==

class Chrome::WindowButton : public Ui::Element {
public:
    explicit WindowButton(Caption::Zone zone) : m_zone(zone) {}

    void SetState(bool hovered, bool pressed, bool maximized) {
        const bool inkChanged = (m_hovered != hovered) || (m_pressed != pressed) ||
                                (m_maximized != maximized);
        m_hovered = hovered;
        m_pressed = pressed;
        m_maximized = maximized;
        if (!Attached()) return;

        if (Gfx::Material* material = MaterialOf()) {
            // El cruce de tinta, que no acompaña a ningún muelle: ver Motion::kInkMs.
            // Con FadeMs(Snappy) esto duraba 34 ms y el botón se encendía de golpe.
            HostRef().Animator().Opacity(material->Visual(), hovered ? 1.0f : 0.0f,
                                         HostRef().Animator().InkMs());
        }
        // Repintar solo cuando el glifo cambia de color o de forma. Sobre el rojo de cerrar
        // el glifo se vuelve blanco, y maximizada la ventana el botón enseña restaurar: si
        // no cambiara, mentiría.
        if (inkChanged) Invalidate();
    }

    // No recibe entrada: la franja de arriba es marco, no cliente.
    bool HitTest(float, float) const override { return false; }

protected:
    bool OnAttach() override {
        // Radio cero: los botones de la barra de Windows son rectángulos, y redondearlos
        // aquí los separaría de los de todas las demás ventanas.
        if (!CreateMaterial(0.0f)) return false;
        MaterialOf()->Visual().Opacity(0.0f);
        return CreateLayer();
    }

    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override {
        if (Gfx::Material* material = MaterialOf()) {
            Theme::Color fill = m_zone == Caption::Zone::Close ? kCloseHover
                                                              : tokens.textPrimary;
            // Un velo, no un bloque: debajo hay Mica.
            if (m_zone != Caption::Zone::Close) fill.a = 28;
            material->SetColor(fill, HostRef().Animator(), crossfadeMs);
        }
        Ui::Element::OnTheme(tokens, crossfadeMs);
    }

    void OnPaint(const Ui::Paint& paint, const Rect& box) override {
        Theme::Color ink = paint.tokens->textPrimary;
        if (m_zone == Caption::Zone::Close && m_hovered) ink = Theme::Rgb(0xffffff);
        if (m_pressed) ink.a = 180;

        const wchar_t* glyph = kCloseGlyph;
        if (m_zone == Caption::Zone::Minimize) glyph = kMinimizeGlyph;
        if (m_zone == Caption::Zone::Maximize) {
            glyph = m_maximized ? kRestoreGlyph : kMaximizeGlyph;
        }

        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        paint.dc->CreateSolidColorBrush(Gfx::ToD2D(ink), brush.put());
        paint.text->DrawGlyph(paint.dc, glyph, kGlyphSize, ToBox(box), brush.get());
    }

private:
    Caption::Zone m_zone = Caption::Zone::Close;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_maximized = false;
};

// ========================================================================== El indicador ==

// El punto del indicador. Es un elemento propio y no un círculo pintado en la superficie de
// la barra porque lo que late es su OPACIDAD, y eso es una propiedad de un visual: pintarlo
// dentro de la barra obligaría a repintar el título sesenta veces por segundo para animar
// un círculo de ocho píxeles.
class Chrome::Beacon : public Ui::Element {
public:
    void Show(bool visible, bool pulsing, Theme::Color color) {
        m_color = color;
        m_custom = true;
        SetVisible(visible);
        if (!Attached()) return;

        if (Gfx::Material* material = MaterialOf()) {
            material->SetColor(color, HostRef().Animator(), 0.0f);
        }
        if (pulsing) {
            HostRef().Animator().Pulse(Visual(), 0.3f, 1.0f, kPulsePeriodMs);
        } else {
            Visual().StopAnimation(L"Opacity");
            Visual().Opacity(1.0f);
        }
    }

    bool HitTest(float, float) const override { return false; }

protected:
    bool OnAttach() override { return CreateMaterial(kBeaconSize * 0.5f); }

    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override {
        if (Gfx::Material* material = MaterialOf()) {
            material->SetColor(m_custom ? m_color : tokens.textSecondary, HostRef().Animator(),
                               crossfadeMs);
        }
        Ui::Element::OnTheme(tokens, crossfadeMs);
    }

private:
    Theme::Color m_color;
    bool m_custom = false;
};

// ============================================================================== La barra ==

bool Chrome::OnAttach() {
    if (!CreateLayer()) return false;

    m_beacon = Add<Beacon>();
    m_beacon->SetVisible(false);

    // El orden de Caption::kButtons, que es el de Windows: minimizar, maximizar, cerrar.
    // Tomarlo de allí y no repetirlo aquí es lo que impide que un día el dibujo y el
    // hit-test dejen de estar de acuerdo sin que nada falle.
    for (std::size_t i = 0; i < 3; ++i) m_buttons[i] = Add<WindowButton>(Caption::kButtons[i]);
    return true;
}

void Chrome::OnArrange() {
    for (std::size_t i = 0; i < 3; ++i) {
        if (m_buttons[i] == nullptr) continue;
        const Caption::Rect& rect = Caption::ButtonRect(m_caption, Caption::kButtons[i]);
        m_buttons[i]->SetFrame(Rect{rect.x, rect.y, rect.width, rect.height});
    }

    if (m_beacon) {
        const float right = m_caption.minimize.x > 0.0f ? m_caption.minimize.x
                                                        : Frame().width;
        const float x = right - Metrics::kSpace4 - TextWidth() - kBeaconGap - kBeaconSize;
        m_beacon->SetFrame(Rect{x, (Caption::kBarHeight - kBeaconSize) * 0.5f, kBeaconSize,
                                kBeaconSize});
    }
}

void Chrome::SetCaption(const Caption::Layout& layout, Caption::Zone hovered,
                        Caption::Zone pressed) {
    const bool geometry = layout.width != m_caption.width ||
                          layout.maximized != m_caption.maximized;
    m_caption = layout;
    m_hovered = hovered;
    m_pressed = pressed;

    for (std::size_t i = 0; i < 3; ++i) {
        if (m_buttons[i] == nullptr) continue;
        const Caption::Zone zone = Caption::kButtons[i];
        m_buttons[i]->SetState(m_hovered == zone, m_pressed == zone, m_caption.maximized);
    }
    if (geometry) OnArrange();
}

void Chrome::SetTitle(std::wstring title) {
    if (m_title == title) return;
    m_title = std::move(title);
    Invalidate();
}

void Chrome::SetSync(const Sync& sync) {
    if (m_sync.running == sync.running && m_sync.problem == sync.problem &&
        m_sync.text == sync.text) {
        return;
    }
    m_sync = sync;

    if (m_beacon) {
        // Ámbar para un problema. No hay un token de error en la tabla de CLAUDE.md, y el
        // de Enfoque es exactamente el ámbar que se lee como aviso; inventarse un color
        // sería meter en la paleta uno que nadie eligió.
        const Theme::Color color = m_sync.problem ? Tokens().priorityFocus
                                  : m_sync.running ? Tokens().accent
                                                   : Tokens().activityActive;
        m_beacon->Show(!m_sync.text.empty(), m_sync.running, color);
    }
    // El texto cambia de ancho y el punto va pegado a su izquierda.
    OnArrange();
    Invalidate();
}

float Chrome::TextWidth() const {
    if (m_sync.text.empty() || !Attached()) return 0.0f;
    return HostRef().Text().Measure(m_sync.text, Ui::Style::Footnote, Ui::Weight::Regular).width;
}

void Chrome::OnPaint(const Ui::Paint& paint, const Rect& box) {
    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    winrt::com_ptr<ID2D1SolidColorBrush> dim;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textPrimary), ink.put());
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), dim.put());

    paint.text->DrawLine(paint.dc, m_title, Ui::Style::Body, Ui::Weight::Semibold,
                         ToBox(Rect{box.x + kEdge, box.y, 320.0f, box.height}), ink.get());

    if (m_sync.text.empty()) return;

    const float right = m_caption.minimize.x > 0.0f ? m_caption.minimize.x : box.Right();
    const float width = HostRef().Text()
                            .Measure(m_sync.text, Ui::Style::Footnote, Ui::Weight::Regular)
                            .width;
    const float x = right - Metrics::kSpace4 - width;
    paint.text->DrawLine(paint.dc, m_sync.text, Ui::Style::Footnote, Ui::Weight::Regular,
                         ToBox(Rect{x, box.y, width + 1.0f, box.height}), dim.get());
}

}  // namespace Views
