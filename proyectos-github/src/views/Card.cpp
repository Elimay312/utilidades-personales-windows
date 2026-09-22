#include "views/Card.h"

#include <algorithm>

#include "compositor/Paint.h"
#include "ui/Controls.h"
#include "ui/Host.h"
#include "ui/Text.h"

namespace Views {

namespace {

using Ui::Rect;

constexpr float kPad = Metrics::kSpace2;
// Lo que crece la tarjeta al levantarla, de la fase 6. Poco y con sombra: lo que dice es
// "esto está por encima de lo demás", no "esto ha cambiado de tamaño".
constexpr float kLiftScale = 1.03f;
constexpr float kDotColumn = 16.0f;
constexpr float kLine = 20.0f;
constexpr float kMetaLine = 16.0f;

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f);
    return color;
}

void Fill(const Ui::Paint& paint, const Rect& box, Theme::Color color, float radius) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(color), brush.put());
    paint.dc->FillRoundedRectangle(D2D1::RoundedRect(ToBox(box), radius, radius), brush.get());
}

void Line(const Ui::Paint& paint, std::wstring_view text, const Rect& box, Ui::Style style,
          Ui::Weight weight, Theme::Color color, Ui::Align align = Ui::Align::Leading,
          Ui::Figures figures = Ui::Figures::Proportional) {
    if (text.empty() || box.width <= 0.0f) return;
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(color), brush.put());

    Ui::Run run;
    run.text = text;
    run.style = style;
    run.weight = weight;
    run.align = align;
    run.figures = figures;
    paint.text->Draw(paint.dc, run, ToBox(box), brush.get());
}

// La línea de abajo: actividad, lenguaje y cuándo fue el último push, separados por puntos
// medios. Se construye entera y se dibuja de una vez para que el recorte con elipsis caiga
// donde tenga que caer y no deje un separador colgando al final.
std::wstring Meta(const App::Entry& entry) {
    std::wstring text = Ui::NameOf(entry.activity);
    if (!entry.repo.language.empty()) text += L" · " + entry.repo.language;
    text += L" · ";
    text += entry.daysSincePush.has_value() ? App::AgoDays(*entry.daysSincePush)
                                            : L"sin actividad";
    if (entry.repo.isPrivate) text += L" · privado";
    if (entry.repo.isArchived) text += L" · archivado en GitHub";
    if (entry.repo.goneAt.has_value()) text += L" · ya no está en la cuenta";
    return text;
}

// El renglón que va justo debajo del nombre. El siguiente paso es lo que esta aplicación
// existe para enseñar, así que va en tinta principal; cuando no lo hay, se cede el sitio a
// la descripción en tinta secundaria, y cuando tampoco la hay se dice, porque un hueco en
// blanco no invita a escribir nada.
struct Subtitle {
    std::wstring_view text;
    bool primary = false;
    bool faint = false;
};

Subtitle SubtitleOf(const App::Entry& entry) {
    if (!entry.local.nextStep.empty()) return Subtitle{entry.local.nextStep, true, false};
    if (!entry.repo.description.empty()) return Subtitle{entry.repo.description, false, false};
    return Subtitle{L"Sin siguiente paso", false, true};
}

Theme::Color InkOf(const Ui::Paint& paint, const Subtitle& subtitle) {
    if (subtitle.faint) return paint.tokens->textDisabled;
    return subtitle.primary ? paint.tokens->textPrimary : paint.tokens->textSecondary;
}

// El fondo, el resalte y el canto. Lo mismo en las dos disposiciones: una tarjeta
// seleccionada tiene que reconocerse igual sea cual sea la forma de la rejilla.
void PaintSurface(const Ui::Paint& paint, const Rect& box, bool hovered, bool selected) {
    const float radius = Metrics::RadiusOf(Metrics::Radius::Card);
    Fill(paint, box, paint.tokens->cardSurface, radius);
    if (selected) {
        Fill(paint, box, paint.tokens->selectionRow, radius);
    } else if (hovered) {
        Fill(paint, box, paint.tokens->controlHover, radius);
    }

    // El canto va SIEMPRE, y con el acento cuando está elegida. Un borde que aparece solo
    // al seleccionar cambia el tamaño aparente de la tarjeta, y entonces al mover la
    // selección parece que las tarjetas se hinchan.
    winrt::com_ptr<ID2D1SolidColorBrush> stroke;
    paint.dc->CreateSolidColorBrush(
        Gfx::ToD2D(selected ? paint.tokens->accent : paint.tokens->separator), stroke.put());
    const float hairline = Metrics::Hairline(paint.scale);
    const float thickness = selected ? hairline * 2.0f : hairline;
    // Media línea hacia dentro: un trazo se dibuja centrado en la geometría, y sin esto la
    // mitad de fuera cae en el píxel del vecino y sale a medio tono.
    const Rect inner = box.Inset(thickness * 0.5f);
    paint.dc->DrawRoundedRectangle(D2D1::RoundedRect(ToBox(inner), radius, radius),
                                   stroke.get(), thickness);
}

}  // namespace

void PaintCard(const Ui::Paint& paint, const Rect& box, const App::Entry& entry,
               CardLayout layout, bool hovered, bool selected) {
    PaintSurface(paint, box, hovered, selected);

    // Un repositorio que ya no está en la cuenta se pinta apagado. Sigue estando porque sus
    // notas siguen estando, pero no puede tener el mismo peso que uno vivo.
    const bool gone = entry.repo.goneAt.has_value();
    const Theme::Color name = gone ? paint.tokens->textSecondary : paint.tokens->textPrimary;
    const Subtitle subtitle = SubtitleOf(entry);
    const std::wstring meta = Meta(entry);

    const float left = box.x + kPad;
    const float right = box.Right() - kPad;

    // Sin clasificar NO lleva píldora. No es una prioridad, es la falta de una, y ponerle
    // etiqueta significa escribir "Sin clasificar" ciento nueve veces en la primera
    // pantalla que ve el usuario —la más ancha de todas las etiquetas, además, comiéndole
    // el sitio al nombre— para decir exactamente nada. Lo que hay que mirar es cuáles SÍ
    // tienen una; cuántos faltan lo dice el contador de la barra lateral.
    const bool hasPill = entry.local.priority != Model::Priority::Unsorted;
    const float pill = hasPill ? Ui::PillWidth(*paint.text, entry.local.priority) : 0.0f;
    const float pillGap = hasPill ? Metrics::kSpace2 : 0.0f;

    if (layout == CardLayout::List) {
        // Dos renglones a la izquierda, píldora y metadatos a la derecha. El ancho de la
        // derecha se mide y se resta: a ojo, un nombre largo se le mete debajo.
        const float metaWidth =
            std::min(paint.text->Measure(meta, Ui::Style::Footnote, Ui::Weight::Regular).width,
                     std::max(box.width * 0.45f, 1.0f));
        const float rightWidth = std::max(pill, metaWidth);
        const float textRight = right - rightWidth - Metrics::kSpace3;
        const float textLeft = left + kDotColumn;
        const float textWidth = std::max(textRight - textLeft, 1.0f);

        Ui::DrawDot(paint, left + 4.0f, box.y + 11.0f + kLine * 0.5f, entry.activity);
        Line(paint, entry.repo.name, Rect{textLeft, box.y + 11.0f, textWidth, kLine},
             Ui::Style::Body, Ui::Weight::Semibold, name);
        Line(paint, subtitle.text, Rect{textLeft, box.y + 32.0f, textWidth, kMetaLine},
             Ui::Style::Caption, Ui::Weight::Regular, InkOf(paint, subtitle));

        if (hasPill) {
            Ui::DrawPill(paint, Rect{right - pill, box.y + 10.0f, pill, 18.0f},
                         entry.local.priority);
        }
        Line(paint, meta, Rect{right - rightWidth, box.y + 32.0f, rightWidth, kMetaLine},
             Ui::Style::Footnote, Ui::Weight::Regular, paint.tokens->textSecondary,
             Ui::Align::Trailing, Ui::Figures::Tabular);
        return;
    }

    // Cuadrícula: todo en columna y con más aire. El orden es el mismo —nombre, siguiente
    // paso, contexto— porque el orden de lectura no puede depender de la disposición.
    //
    // La píldora sube al renglón del nombre, y no se queda abajo al lado de los metadatos,
    // por una razón medida en pantalla: una celda de cuadrícula es tres veces más estrecha
    // que una fila, y con las dos cosas en el mismo renglón la línea de "Activo · C++ ·
    // hace 10 días · privado" se recortaba siempre por el mismo sitio.
    const float width = std::max(right - left, 1.0f);
    const float textLeft = left + kDotColumn;
    const float nameWidth = std::max(right - textLeft - pill - pillGap, 1.0f);

    Ui::DrawDot(paint, left + 4.0f, box.y + 14.0f + kLine * 0.5f, entry.activity);
    Line(paint, entry.repo.name, Rect{textLeft, box.y + 14.0f, nameWidth, kLine},
         Ui::Style::Body, Ui::Weight::Semibold, name);
    if (hasPill) {
        Ui::DrawPill(paint, Rect{right - pill, box.y + 15.0f, pill, 18.0f},
                     entry.local.priority);
    }

    Line(paint, subtitle.text, Rect{left, box.y + 40.0f, width, kLine}, Ui::Style::Body,
         Ui::Weight::Regular, InkOf(paint, subtitle));
    if (!entry.local.nextStep.empty() && !entry.repo.description.empty()) {
        Line(paint, entry.repo.description, Rect{left, box.y + 62.0f, width, kMetaLine},
             Ui::Style::Caption, Ui::Weight::Regular, paint.tokens->textSecondary);
    }

    Line(paint, meta, Rect{left, box.Bottom() - 26.0f, width, kMetaLine}, Ui::Style::Footnote,
         Ui::Weight::Regular, paint.tokens->textSecondary, Ui::Align::Leading,
         Ui::Figures::Tabular);

    // El desajuste solo se marca en cuadrícula, donde hay sitio. En la lista ya lo dice la
    // vista "Necesita decisión", y meterlo en dos renglones apretados sería ruido.
    if (entry.mismatch != Model::Mismatch::None) {
        Fill(paint, Rect{box.x, box.y + 10.0f, 3.0f, box.height - 20.0f},
             WithAlpha(paint.tokens->priorityFocus, 0.9f), 1.5f);
    }
}

// ======================================================================== DragCard ==

bool DragCard::OnAttach() {
    // La sombra ANTES que nada: mete todo lo demás dentro de su capa para sacar la máscara
    // del alfa, y lo que se cree antes se queda fuera (ui/Element.h).
    CreateShadow(Metrics::kElevationSheet);
    if (!CreateLayer()) return false;
    // El centro atado al tamaño: sin esto, crecer a 1,03 tira de la tarjeta hacia su esquina
    // superior izquierda en vez de hincharla desde el medio.
    HostRef().Animator().BindCenterPoint(Visual());
    SetVisible(false);
    return true;
}

void DragCard::Lift(const App::Entry& entry, CardLayout layout, const Rect& from) {
    m_entry = entry;
    m_layout = layout;

    SetVisible(true);
    SetOpacity(1.0f, 0.0f);
    SetFrame(from);
    Invalidate();

    Motion::Animator& animator = HostRef().Animator();
    // El valor inicial se escribe a mano: el muelle retoma desde donde esté, y si se quedó
    // en 1,03 del último arrastre no habría nada que animar.
    Visual().StopAnimation(L"Scale");
    Visual().Scale({1.0f, 1.0f, 1.0f});
    animator.Scale(Visual(), {kLiftScale, kLiftScale, 1.0f}, Motion::Kind::Snappy);
}

void DragCard::MoveTo(float xDip, float yDip) {
    SetFrame(Rect{xDip, yDip, Frame().width, Frame().height});
}

void DragCard::FlyTo(const Rect& target) {
    if (!Visible()) return;
    Motion::Animator& animator = HostRef().Animator();
    // Muelle suave: es "mover una tarjeta entre grupos", que es literalmente para lo que
    // está ese muelle en la tabla de CLAUDE.md.
    SlideTo(target.x, target.y, Motion::Kind::Smooth);
    animator.Scale(Visual(), {1.0f, 1.0f, 1.0f}, Motion::Kind::Smooth);
    SetOpacity(0.0f, animator.FadeMs(Motion::Kind::Smooth));
}

void DragCard::Refuse() {
    if (!Visible()) return;
    Motion::Animator& animator = HostRef().Animator();
    animator.Shake(Visual(), {Frame().x, Frame().y, 0.0f}, Motion::kShakeDip);
    // Se apaga DESPUÉS de temblar, no mientras: un temblor a medio desvanecer no se ve, y lo
    // que tiene que quedar claro es que ese sitio no la admite. El retardo va dentro de la
    // animación, así que lo lleva DWM y no hace falta un temporizador en este hilo.
    //
    // Y sin animaciones del sistema no hay temblor, así que tampoco hay nada que esperar:
    // el retardo se quita. Dejarlo dejaba la tarjeta un tercio de segundo parada en el aire
    // sin que pasara nada, que es peor que no animar — es no animar Y hacer esperar.
    const float delay = animator.Enabled() ? Motion::kShakeMs : 0.0f;
    animator.OpacityDelayed(Visual(), 0.0f, animator.FadeMs(Motion::Kind::Smooth), delay);
}

void DragCard::HideNow() {
    SetOpacity(0.0f, 0.0f);
    SetVisible(false);
}

void DragCard::OnPaint(const Ui::Paint& paint, const Rect& box) {
    // Opaca, al revés que las de la lista: una tarjeta en el aire que deja ver las de debajo
    // se lee como un fantasma, y además es de donde sale la máscara de la sombra.
    Theme::Color solid = paint.tokens->cardSurface;
    solid.a = 255;
    Fill(paint, box, solid, Metrics::RadiusOf(Metrics::Radius::Card));
    // Elegida: la que se está arrastrando es, por definición, la que se está tocando.
    PaintCard(paint, box, m_entry, m_layout, false, true);
}

void DragCard::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Shadow* shadow = ShadowOf()) {
        shadow->SetColor(tokens.shadow, HostRef().Animator(), crossfadeMs);
    }
    Ui::Element::OnTheme(tokens, crossfadeMs);
}

}  // namespace Views
