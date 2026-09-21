#include "ui/Controls.h"

#include <algorithm>

#include "compositor/Paint.h"
#include "ui/Host.h"

namespace Ui {

namespace {

constexpr float kButtonPadding = Metrics::kSpace2;
constexpr float kPillPadding = Metrics::kSpace1;
constexpr float kDotSize = 8.0f;
constexpr float kPressScale = 0.97f;
// El relleno de la píldora: el mismo tono de la prioridad, muy rebajado.
constexpr float kPillFillAlpha = 0.15f;

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f);
    return color;
}

}  // namespace

// ============================================================================== Label ==

Label::Label(std::wstring text, Style style, Weight weight)
    : m_text(std::move(text)), m_style(style), m_weight(weight) {}

void Label::SetText(std::wstring text) {
    if (m_text == text) return;
    m_text = std::move(text);
    Invalidate();
}

void Label::SetColor(Theme::Color color) {
    m_color = color;
    m_custom = true;
    m_secondary = false;
    Invalidate();
}

void Label::UseSecondary() {
    m_secondary = true;
    m_custom = false;
    Invalidate();
}

float Label::PreferredWidth() {
    if (!Attached()) return 0.0f;
    return HostRef().Text().Measure(m_text, m_style, m_weight, m_figures).width;
}

void Label::OnPaint(const Paint& paint, const Rect& box) {
    if (m_text.empty()) return;

    Theme::Color color = paint.tokens->textPrimary;
    if (m_custom) color = m_color;
    if (m_secondary) color = paint.tokens->textSecondary;
    if (!Enabled()) color = paint.tokens->textDisabled;

    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(color), brush.put());

    Run run;
    run.text = m_text;
    run.style = m_style;
    run.weight = m_weight;
    run.align = m_align;
    run.figures = m_figures;
    run.wrap = m_wrap;
    paint.text->Draw(paint.dc, run, ToBox(box), brush.get());
}

// =============================================================================== Rule ==

bool Rule::OnAttach() {
    // Radio cero: una línea de un píxel con esquinas redondeadas es una línea con los
    // extremos a medio tono.
    return CreateMaterial(0.0f);
}

void Rule::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(tokens.separator, HostRef().Animator(), crossfadeMs);
    }
    Element::OnTheme(tokens, crossfadeMs);
}

// ============================================================================= Button ==

Button::Button(std::wstring label, ButtonKind kind)
    : m_kind(kind), m_label(std::move(label)) {}

void Button::SetLabel(std::wstring label) {
    if (m_label == label) return;
    m_label = std::move(label);
    Invalidate();
}

float Button::PreferredWidth() {
    if (!Attached()) return 0.0f;
    const float text = HostRef().Text().Measure(m_label, Style::Body, Weight::Semibold).width;
    return text + kButtonPadding * 2.0f + Metrics::kSpace1;
}

bool Button::OnAttach() {
    if (!CreateMaterial(Metrics::RadiusOf(Metrics::Radius::Control))) return false;
    if (!CreateRing(Metrics::RadiusOf(Metrics::Radius::Control), Metrics::kFocusGap,
                    Metrics::kFocusRing)) {
        return false;
    }
    if (!CreateLayer()) return false;
    // El centro atado al tamaño: sin esto, pulsar encoge el botón hacia su esquina
    // superior izquierda en vez de hundirlo en el sitio.
    HostRef().Animator().BindCenterPoint(Visual());
    return true;
}

Theme::Color Button::Fill(const Theme::Tokens& tokens) const {
    if (!Enabled()) {
        return m_kind == ButtonKind::Primary ? WithAlpha(tokens.accent, 0.35f)
                                             : tokens.controlFill;
    }
    switch (m_kind) {
    case ButtonKind::Primary:
        if (Pressed()) return tokens.accentPressed;
        return Hovered() ? tokens.accentHover : tokens.accent;
    case ButtonKind::Secondary:
        if (Pressed()) return tokens.controlPressed;
        return Hovered() ? tokens.controlHover : tokens.controlFill;
    case ButtonKind::Plain:
        if (Pressed()) return tokens.controlPressed;
        // Sin fondo en reposo: es un botón que solo existe cuando se le busca.
        return Hovered() ? tokens.controlHover : WithAlpha(tokens.controlFill, 0.0f);
    }
    return tokens.controlFill;
}

Theme::Color Button::Ink(const Theme::Tokens& tokens) const {
    if (!Enabled()) return tokens.textDisabled;
    return m_kind == ButtonKind::Primary ? tokens.textOnAccent : tokens.textPrimary;
}

void Button::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(Fill(tokens), HostRef().Animator(), crossfadeMs);
    }
    Element::OnTheme(tokens, crossfadeMs);
}

void Button::OnStateChanged() {
    if (!Attached()) return;
    Motion::Animator& animator = HostRef().Animator();

    // Hover y pulsado son propiedades de la GPU: el color del material y la escala. La
    // textura no se toca, y por eso pasar el ratón por una lista no repinta nada.
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(Fill(Tokens()), animator, animator.FadeMs(Motion::Kind::Snappy));
    }
    animator.Scale(Visual(), Pressed() ? Motion::Vec3{kPressScale, kPressScale, 1.0f}
                                       : Motion::Vec3{1.0f, 1.0f, 1.0f},
                   Motion::Kind::Snappy);

    // Lo único que sí cambia píxeles es habilitar o deshabilitar, que cambia el color del
    // texto.
    if (m_paintedEnabled != Enabled()) {
        m_paintedEnabled = Enabled();
        Invalidate();
    }
}

void Button::OnPaint(const Paint& paint, const Rect& box) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(Ink(*paint.tokens)), brush.put());

    Run run;
    run.text = m_label;
    run.style = Style::Body;
    run.weight = Weight::Semibold;
    run.align = Align::Center;
    paint.text->Draw(paint.dc, run, ToBox(box.Inset(kButtonPadding)), brush.get());
}

bool Button::OnPointer(const Input::Pointer& e) {
    if (!Enabled()) return false;
    if (e.button != Input::Button::Left) return false;

    if (e.action == Input::Action::Down) return true;
    if (e.action == Input::Action::Up) {
        // Solo activa si se suelta encima. Arrastrar fuera y soltar es la forma
        // universal de arrepentirse de un clic.
        if (HitTest(e.x, e.y)) Activate();
        return true;
    }
    return false;
}

bool Button::OnKey(const Input::Key& e) {
    if (!Enabled() || !e.down) return false;
    if (e.virtualKey != VK_SPACE && e.virtualKey != VK_RETURN) return false;
    Activate();
    return true;
}

void Button::Activate() {
    if (m_activate) m_activate();
}

// ========================================================================= IconButton ==

IconButton::IconButton(std::wstring glyph, ButtonKind kind)
    : Button(std::move(glyph), kind) {}

void IconButton::OnPaint(const Paint& paint, const Rect& box) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(Ink(*paint.tokens)), brush.put());
    paint.text->DrawGlyph(paint.dc, m_label, Metrics::kIconSize, ToBox(box), brush.get());
}

// =============================================================================== Pill ==

const wchar_t* NameOf(Priority priority) {
    switch (priority) {
    case Priority::Focus:     return L"Enfoque";
    case Priority::Secondary: return L"Secundario";
    case Priority::Someday:   return L"Algún día";
    case Priority::Archived:  return L"Archivado";
    case Priority::Unsorted:  return L"Sin clasificar";
    }
    return L"";
}

const wchar_t* NameOf(Model::State state) {
    switch (state) {
    case Model::State::Active:  return L"Activo";
    case Model::State::Blocked: return L"Bloqueado";
    case Model::State::Waiting: return L"En espera";
    case Model::State::Done:    return L"Terminado";
    }
    return L"";
}

Theme::Color ColorOf(Priority priority, const Theme::Tokens& tokens) {
    switch (priority) {
    case Priority::Focus:     return tokens.priorityFocus;
    case Priority::Secondary: return tokens.prioritySecondary;
    case Priority::Someday:   return tokens.prioritySomeday;
    case Priority::Archived:  return tokens.priorityArchived;
    case Priority::Unsorted:  return tokens.textSecondary;
    }
    return tokens.textSecondary;
}

float PillWidth(Ui::Text& text, Priority priority) {
    return text.Measure(NameOf(priority), Style::Footnote, Weight::Semibold).width +
           kPillPadding * 2.0f;
}

void DrawPill(const Paint& paint, const Rect& box, Priority priority) {
    const Theme::Color color = ColorOf(priority, *paint.tokens);

    winrt::com_ptr<ID2D1SolidColorBrush> fill;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(WithAlpha(color, kPillFillAlpha)), fill.put());
    const float radius = Metrics::RadiusOf(Metrics::Radius::Control);
    paint.dc->FillRoundedRectangle(D2D1::RoundedRect(ToBox(box), radius, radius), fill.get());

    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(color), ink.put());

    Run run;
    run.text = NameOf(priority);
    run.style = Style::Footnote;
    run.weight = Weight::Semibold;
    run.align = Align::Center;
    paint.text->Draw(paint.dc, run, ToBox(box), ink.get());
}

Pill::Pill(Priority priority) : m_priority(priority) {}

void Pill::SetPriority(Priority priority) {
    if (m_priority == priority) return;
    m_priority = priority;
    if (Attached()) {
        if (Gfx::Material* material = MaterialOf()) {
            material->SetColor(WithAlpha(ColorOf(m_priority, Tokens()), kPillFillAlpha),
                               HostRef().Animator(),
                               HostRef().Animator().FadeMs(Motion::Kind::Snappy));
        }
    }
    Invalidate();
}

bool Pill::OnAttach() {
    if (!CreateMaterial(Metrics::RadiusOf(Metrics::Radius::Control))) return false;
    return CreateLayer();
}

float Pill::PreferredWidth() {
    if (!Attached()) return 0.0f;
    return PillWidth(HostRef().Text(), m_priority);
}

void Pill::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(WithAlpha(ColorOf(m_priority, tokens), kPillFillAlpha),
                           HostRef().Animator(), crossfadeMs);
    }
    Element::OnTheme(tokens, crossfadeMs);
}

void Pill::OnPaint(const Paint& paint, const Rect& box) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(ColorOf(m_priority, *paint.tokens)), brush.put());

    Run run;
    run.text = NameOf(m_priority);
    run.style = Style::Footnote;
    run.weight = Weight::Semibold;
    run.align = Align::Center;
    paint.text->Draw(paint.dc, run, ToBox(box), brush.get());
}

// ================================================================================ Dot ==

const wchar_t* NameOf(Activity activity) {
    switch (activity) {
    case Activity::Active:  return L"Activo";
    case Activity::Paused:  return L"En pausa";
    case Activity::Dormant: return L"Dormido";
    }
    return L"";
}

Theme::Color ColorOf(Activity activity, const Theme::Tokens& tokens) {
    switch (activity) {
    case Activity::Active:  return tokens.activityActive;
    case Activity::Paused:  return tokens.activityPaused;
    case Activity::Dormant: return tokens.activityDormant;
    }
    return tokens.activityDormant;
}

void DrawDot(const Paint& paint, float cxDip, float cyDip, Activity activity) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(ColorOf(activity, *paint.tokens)), brush.put());
    const float radius = kDotSize * 0.5f;
    paint.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cxDip, cyDip), radius, radius),
                          brush.get());
}

Dot::Dot(Activity activity) : m_activity(activity) {}

void Dot::SetActivity(Activity activity) {
    if (m_activity == activity) return;
    m_activity = activity;
    Invalidate();
}

void Dot::OnPaint(const Paint& paint, const Rect& box) {
    DrawDot(paint, box.x + box.width * 0.5f, box.y + box.height * 0.5f, m_activity);
}

}  // namespace Ui
