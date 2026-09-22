#include "views/Review.h"

#include <Windows.h>

#include <algorithm>
#include <utility>

#include "compositor/Paint.h"
#include "shell/Caption.h"
#include "ui/Controls.h"
#include "ui/Field.h"
#include "ui/Host.h"
#include "ui/Overlays.h"
#include "ui/Text.h"

namespace Views {

namespace {

using Ui::Rect;

// La barra de progreso, fina de verdad: tres DIP. Más gruesa deja de ser una raya de
// arriba y pasa a ser un elemento con el que hay que contar.
constexpr float kProgress = 3.0f;
constexpr float kHeaderHeight = 30.0f;
constexpr float kMargin = Metrics::kSpace5;
// La tarjeta no crece con la ventana. Una tarjeta de mil DIP de ancho obliga a barrer la
// pantalla con los ojos para leer tres renglones, y lo que esta pantalla pide es leer y
// decidir sin mover la cabeza.
constexpr float kCardMaxWidth = 620.0f;
// El alto da para las tres novedades y para de crecer. Fijo y no ajustado a lo que traiga
// cada repositorio: una tarjeta que cambia de alto en cada decisión hace que la pila y el
// pie den un salto por debajo, y lo que se está mirando es lo de dentro.
constexpr float kCardMinHeight = 280.0f;
constexpr float kCardMaxHeight = 364.0f;
// Cuánto asoma cada fantasma de debajo. Dos, y lo justo para que se lea "hay más detrás":
// menos de una docena de DIP no se ve, y más convierte la pila en tres tarjetas.
constexpr float kStackStep = 13.0f;
constexpr float kStackInset = 18.0f;
// El pie: la fila de dianas y el renglón de ayuda.
constexpr float kChipHeight = 34.0f;
constexpr float kChipGap = Metrics::kSpace1;
constexpr float kHintHeight = 18.0f;
constexpr float kFooterHeight = kChipHeight + Metrics::kSpace1 + kHintHeight;
// Las cuatro teclas de la tabla de atajos. "Sin clasificar" no tiene diana por lo mismo
// que no tiene tecla en la lista: es la ausencia de una prioridad, y de aquí se sale
// poniendo una o saltando.
constexpr int kChips = 4;

constexpr float kCardPad = Metrics::kSpace4;

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

void Fill(const Ui::Paint& paint, const Rect& box, Theme::Color color, float radius) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(color), brush.put());
    paint.dc->FillRoundedRectangle(D2D1::RoundedRect(ToBox(box), radius, radius), brush.get());
}

void Line(const Ui::Paint& paint, std::wstring_view text, const Rect& box, Ui::Style style,
          Ui::Weight weight, Theme::Color color, Ui::Align align = Ui::Align::Leading,
          bool wrap = false) {
    if (text.empty() || box.width <= 0.0f) return;
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(color), brush.put());

    Ui::Run run;
    run.text = text;
    run.style = style;
    run.weight = weight;
    run.align = align;
    run.wrap = wrap;
    paint.text->Draw(paint.dc, run, ToBox(box), brush.get());
}

// Las cuatro dianas dentro de la caja del pie. Sale de UNA función y no de dos cuentas
// iguales: la tarjeta vuela exactamente hacia la que se pinta, y con la aritmética escrita
// dos veces llega un día en que vuela hacia el hueco de al lado.
Rect ChipRect(const Rect& box, int index) {
    const float width = (box.width - kChipGap * (kChips - 1)) / kChips;
    return Rect{box.x + (width + kChipGap) * static_cast<float>(index), box.y, width,
                kChipHeight};
}

// Cuánto se espera antes de esconder la pantalla al terminar: lo que tarda el muelle
// expresivo en asentarse, con un poco de margen. Sale de la tabla y no de un número
// escrito a mano, así que afinar el muelle en la fase 8 lo arrastra solo.
int HideDelayMs() {
    return static_cast<int>(Motion::SettleMs(Motion::SpringFor(Motion::Kind::Expressive))) + 80;
}

}  // namespace

// ============================================================================ Board ==

// Una pizarra que pinta lo que le digan. Ui::Slate no pinta nada por definición —es un
// sitio donde caen etiquetas— y aquí hacen falta cuatro dianas de color y dos renglones de
// ayuda dentro de UNA textura y no de seis elementos.
class Review::Board : public Ui::Element {
public:
    using Painter = std::function<void(const Ui::Paint&, const Rect&)>;
    explicit Board(Painter painter) : m_painter(std::move(painter)) {}

    bool HitTest(float, float) const override { return false; }

protected:
    bool OnAttach() override { return CreateLayer(); }
    void OnPaint(const Ui::Paint& paint, const Rect& box) override {
        if (m_painter) m_painter(paint, box);
    }

private:
    Painter m_painter;
};

// ============================================================================== Bar ==

// Un rectángulo de color y nada más: la barra de progreso, los fantasmas de la pila y las
// barras del resumen. Es un Gfx::Material suelto, así que no gasta ni una textura y crece
// y se mueve en la GPU.
//
// El color se lo pone su dueño desde OnTheme y no se lo busca él en los tokens: unos son
// del acento, otros de la prioridad y otros de la superficie de una tarjeta, y una tabla
// aquí dentro sería una segunda tabla de colores.
class Review::Bar : public Ui::Element {
public:
    explicit Bar(float radiusDip) : m_radius(radiusDip) {}

    void SetColor(Theme::Color color, float crossfadeMs) {
        m_color = color;
        if (Gfx::Material* material = MaterialOf()) {
            material->SetColor(color, HostRef().Animator(), crossfadeMs);
        }
    }

    // Qué parte del ancho del marco se rellena, del 0 al 1.
    void SetFill(float fraction, bool animate) {
        m_fill = std::clamp(fraction, 0.0f, 1.0f);
        Apply(animate);
    }

    // Entra desde la izquierda y con retardo: es el escalonado del resumen. El retardo va
    // DENTRO de la animación, así que lo lleva DWM y no hace falta un temporizador aquí.
    void Enter(float delayMs) {
        if (!Attached()) return;
        const Motion::Animator& animator = HostRef().Animator();
        const Rect& frame = Frame();
        SetOpacity(0.0f, 0.0f);
        Visual().StopAnimation(L"Offset");
        Visual().Offset({frame.x - Metrics::kSpace3, frame.y, 0.0f});
        animator.OffsetDelayed(Visual(), {frame.x, frame.y, 0.0f}, Motion::Kind::Expressive,
                               delayMs);
        animator.OpacityDelayed(Visual(), 1.0f, animator.FadeMs(Motion::Kind::Expressive),
                                delayMs);
    }

    bool HitTest(float, float) const override { return false; }

protected:
    bool OnAttach() override { return CreateMaterial(m_radius); }
    // Recolocar reescribe el tamaño del material por el camino de Element::SetFrame, así
    // que el relleno se vuelve a aplicar aquí. Sin esto, la barra de progreso se pone al
    // cien por cien en cuanto alguien cambia el tamaño de la ventana.
    void OnArrange() override { Apply(false); }
    // El color lo reparte el dueño: ver la cabecera de la clase.
    void OnTheme(const Theme::Tokens&, float) override {}

private:
    void Apply(bool animate) {
        Gfx::Material* material = MaterialOf();
        if (material == nullptr || !Attached()) return;
        const float width = std::max(Frame().width * m_fill, 0.0f);
        if (animate) {
            material->AnimateBounds(HostRef().Animator(), 0.0f, 0.0f, width, Frame().height,
                                    Motion::Kind::Smooth);
        } else {
            material->SetBounds(0.0f, 0.0f, width, Frame().height);
        }
    }

    float m_radius = 0.0f;
    float m_fill = 1.0f;
    Theme::Color m_color;
};

// ========================================================================= CardView ==

// Una de las dos caras de la pila. Dueña de su superficie porque vuela por su cuenta, y
// con sombra porque está por encima de todo lo demás de la pantalla.
class Review::CardView : public Ui::Element {
public:
    void Show(const Card& card) {
        m_card = card;
        Invalidate();
    }
    const std::string& RepoId() const { return m_card.repoId; }

    // Mientras se edita, la tarjeta NO pinta su renglón del siguiente paso. El campo de
    // texto es un pozo translúcido que se coloca justo encima, así que lo de debajo se
    // sigue leyendo a través: se veía «Sin siguiente paso — pulsa E para escribirlo»
    // cruzado con el texto que se estaba escribiendo.
    void SetEditing(bool editing) {
        if (m_editing == editing) return;
        m_editing = editing;
        Invalidate();
    }

    // Llega desde abajo con el muelle expresivo, que es el de "hojas modales y revisión
    // semanal" de la tabla de CLAUDE.md.
    void Enter(const Rect& frame, bool animate) {
        SetVisible(true);
        Motion::Animator& animator = HostRef().Animator();
        if (!animate) {
            SetFrame(frame);
            SetOpacity(1.0f, 0.0f);
            return;
        }
        SetFrame(Rect{frame.x, frame.y + Metrics::kSpace5, frame.width, frame.height});
        SetOpacity(0.0f, 0.0f);
        SlideTo(frame.x, frame.y, Motion::Kind::Expressive);
        SetOpacity(1.0f, animator.FadeMs(Motion::Kind::Expressive));
    }

    // Sale hacia su diana: se va a su sitio y se apaga por el camino.
    //
    // **Sin escala, y eso NO es una simplificación.** Un visual con una animación de escala
    // encima se rasteriza filtrado aunque la animación acabe en 1,0 exacto, y el texto de
    // la tarjeta salía con halos grises donde el de la barra de título sale con el píxel
    // limpio — comparado a 1:1, no a ojo. Lo que la tarjeta tiene que decir al salir es
    // hacia dónde va, y eso lo dice el viaje; el tamaño no dice nada más.
    void FlyTo(const Rect& target) {
        if (!Visible()) return;
        Motion::Animator& animator = HostRef().Animator();
        // Hacia el CENTRO de la diana, no hacia su esquina: la tarjeta es mucho más ancha,
        // y apuntar la esquina la manda a la diana de al lado.
        const float x = target.x + (target.width - Frame().width) * 0.5f;
        const float y = target.y + (target.height - Frame().height) * 0.5f;
        SlideTo(x, y, Motion::Kind::Expressive);
        SetOpacity(0.0f, animator.FadeMs(Motion::Kind::Expressive));
    }

    // El "no" del límite de Enfoque, igual que en la lista: se para, tiembla y se queda.
    // Aquí NO se apaga después, porque la tarjeta sigue estando por decidir.
    void Refuse() {
        HostRef().Animator().Shake(Visual(), {Frame().x, Frame().y, 0.0f}, Motion::kShakeDip);
    }

    void HideNow() {
        SetOpacity(0.0f, 0.0f);
        SetVisible(false);
    }

    // Dónde cae el renglón del siguiente paso dentro de una tarjeta de esta caja. Estático
    // porque lo pregunta el campo de texto, que no es hijo de la tarjeta: la tarjeta vuela
    // y un campo de texto dentro se iría volando con lo escrito a medias.
    static Rect StepRect(const Rect& box) {
        return Rect{box.x + kCardPad, box.y + 142.0f,
                    std::max(box.width - kCardPad * 2.0f, 1.0f), 46.0f};
    }

    bool HitTest(float, float) const override { return false; }

protected:
    bool OnAttach() override {
        // La sombra ANTES que nada: mete el resto dentro de su capa para sacar la máscara
        // del alfa, y lo que se cree antes se queda fuera (ui/Element.h).
        CreateShadow(Metrics::kElevationSheet);
        if (!CreateLayer()) return false;
        SetVisible(false);
        return true;
    }

    void OnPaint(const Ui::Paint& paint, const Rect& box) override;

    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override {
        if (Gfx::Shadow* shadow = ShadowOf()) {
            shadow->SetColor(tokens.shadow, HostRef().Animator(), crossfadeMs);
        }
        Ui::Element::OnTheme(tokens, crossfadeMs);
    }

private:
    Card m_card;
    bool m_editing = false;
};

void Review::CardView::OnPaint(const Ui::Paint& paint, const Rect& box) {
    // Opaca: es lo único que se está mirando, y además es de donde sale la máscara de la
    // sombra. Una tarjeta translúcida aquí dejaría ver la lista apagada de debajo.
    Theme::Color surface = paint.tokens->cardSurface;
    surface.a = 255;
    Fill(paint, box, surface, Metrics::RadiusOf(Metrics::Radius::Panel));

    const float left = box.x + kCardPad;
    const float right = box.Right() - kCardPad;
    const float width = std::max(right - left, 1.0f);

    // Por qué está aquí, arriba del todo y en el color de Enfoque: es la pregunta que la
    // tarjeta viene a hacer, y sin ella una tarjeta es solo un repositorio más.
    Line(paint, m_card.why, Rect{left, box.y + kCardPad, width, 16.0f}, Ui::Style::Footnote,
         Ui::Weight::Semibold, paint.tokens->priorityFocus);

    const bool hasPill = m_card.priority != Model::Priority::Unsorted;
    const float pill = hasPill ? Ui::PillWidth(*paint.text, m_card.priority) : 0.0f;
    Line(paint, m_card.name,
         Rect{left, box.y + 46.0f, std::max(width - pill - Metrics::kSpace2, 1.0f), 34.0f},
         Ui::Style::Title, Ui::Weight::Semibold, paint.tokens->textPrimary);
    if (hasPill) {
        Ui::DrawPill(paint, Rect{right - pill, box.y + 54.0f, pill, 18.0f}, m_card.priority);
    }
    Line(paint, m_card.where, Rect{left, box.y + 82.0f, width, 18.0f}, Ui::Style::Caption,
         Ui::Weight::Regular, paint.tokens->textSecondary);

    Line(paint, L"SIGUIENTE PASO", Rect{left, box.y + 118.0f, width, 14.0f},
         Ui::Style::Footnote, Ui::Weight::Semibold, paint.tokens->textSecondary);
    // El campo de texto de la edición se coloca EXACTAMENTE encima de este renglón, así
    // que los dos salen de StepRect y no de dos números parecidos.
    const Rect step = StepRect(box);
    if (m_editing) {
        // Ese renglón lo lleva ahora el campo de texto, que está justo encima.
    } else if (m_card.nextStep.empty()) {
        Line(paint, L"Sin siguiente paso — pulsa E para escribirlo", step, Ui::Style::Body,
             Ui::Weight::Regular, paint.tokens->textDisabled);
    } else {
        Line(paint, m_card.nextStep, step, Ui::Style::Heading, Ui::Weight::Regular,
             paint.tokens->textPrimary, Ui::Align::Leading, /*wrap*/ true);
    }

    Line(paint, L"ACTIVIDAD", Rect{left, box.y + 200.0f, width, 14.0f}, Ui::Style::Footnote,
         Ui::Weight::Semibold, paint.tokens->textSecondary);
    Ui::DrawDot(paint, left + 4.0f, box.y + 228.0f, m_card.activity);
    Line(paint, m_card.meta, Rect{left + 16.0f, box.y + 218.0f, width - 16.0f, 20.0f},
         Ui::Style::Body, Ui::Weight::Regular, paint.tokens->textPrimary);
    Line(paint, m_card.commit, Rect{left, box.y + 240.0f, width, 18.0f}, Ui::Style::Caption,
         Ui::Weight::Regular, paint.tokens->textSecondary);

    Line(paint, L"NOVEDADES", Rect{left, box.y + 270.0f, width, 14.0f}, Ui::Style::Footnote,
         Ui::Weight::Semibold, paint.tokens->textSecondary);
    if (m_card.novedades.empty()) {
        Line(paint, L"Ninguna todavía", Rect{left, box.y + 288.0f, width, 18.0f},
             Ui::Style::Caption, Ui::Weight::Regular, paint.tokens->textDisabled);
    }
    for (std::size_t i = 0; i < m_card.novedades.size(); ++i) {
        const float y = box.y + 288.0f + static_cast<float>(i) * 19.0f;
        if (y + 18.0f > box.Bottom() - Metrics::kSpace2) break;
        Line(paint, m_card.novedades[i], Rect{left, y, width, 18.0f}, Ui::Style::Caption,
             Ui::Weight::Regular, paint.tokens->textSecondary);
    }
}

// =========================================================================== Review ==

bool Review::OnAttach() {
    // Sin material propio: lo que queda detrás cuando la lista se aleja es la Mica, que es
    // exactamente el fondo que esta pantalla quiere. Un velo encima solo la ensuciaría.
    SetVisible(false);

    m_track = Add<Bar>(kProgress * 0.5f);
    m_fill = Add<Bar>(kProgress * 0.5f);
    m_header = Add<Board>([this](const Ui::Paint& paint, const Rect& box) {
        PaintHeader(paint, box);
    });
    m_footer = Add<Board>([this](const Ui::Paint& paint, const Rect& box) {
        PaintFooter(paint, box);
    });

    // Los fantasmas primero: van debajo de las caras, y el orden en que se añaden es el
    // orden en que se pintan.
    const float cardRadius = Metrics::RadiusOf(Metrics::Radius::Panel);
    m_stack[1] = Add<Bar>(cardRadius);
    m_stack[0] = Add<Bar>(cardRadius);
    m_faces[0] = Add<CardView>();
    m_faces[1] = Add<CardView>();

    // El campo vive FUERA de la tarjeta: la tarjeta vuela, y un campo de texto dentro se
    // iría volando con lo escrito a medias. Se coloca encima del renglón del siguiente
    // paso y solo existe mientras se edita.
    m_step = Add<Ui::Field>(L"Cuál es el siguiente paso");
    m_step->SetVisible(false);
    m_step->OnSubmit([this] {
        if (Attached()) HostRef().Input().Focus(nullptr, false);
    });
    m_step->OnBlur([this] { CommitEdit(); });
    return true;
}

void Review::Begin(std::vector<Card> cards) {
    if (cards.empty()) return;

    m_cards = std::move(cards);
    m_at = 0;
    m_decided = 0;
    m_postponed = 0;
    m_total = static_cast<int>(m_cards.size());
    for (int& one : m_tally) one = 0;
    m_running = true;
    m_done = false;
    if (m_hide) m_hide.Stop();

    if (m_summary) {
        m_summary->SetVisible(false);
        m_summary->SetOpacity(0.0f, 0.0f);
    }
    for (CardView* face : m_faces) face->HideNow();
    m_front = 0;
    if (m_footer) m_footer->SetVisible(true);

    SetVisible(true);
    SetOpacity(1.0f, 0.0f);
    Relayout();
    UpdateProgress(false);
    ShowCurrent(true);
}

void Review::Finish() {
    if (!m_running) return;
    m_running = false;
    CancelEdit();

    SetOpacity(0.0f, HostRef().Animator().FadeMs(Motion::Kind::Expressive));
    if (!m_hide && Attached()) {
        if (const auto queue = HostRef().Queue()) {
            m_hide = queue.CreateTimer();
            m_hide.Interval(std::chrono::milliseconds(HideDelayMs()));
            m_hide.IsRepeating(false);
            m_hide.Tick([this](auto&&, auto&&) {
                // Se comprueba otra vez: entre el fundido y el temporizador puede haberse
                // vuelto a empezar, y esconderla entonces la dejaría invisible y mandando.
                if (!m_running) SetVisible(false);
            });
        }
    }
    if (m_hide) {
        m_hide.Stop();
        m_hide.Start();
    } else {
        SetVisible(false);
    }
    if (m_finished) m_finished();
}

// ------------------------------------------------------------------------ La pila --

void Review::ShowCurrent(bool animate) {
    if (m_at >= static_cast<int>(m_cards.size())) {
        ShowSummary();
        return;
    }
    CardView* face = m_faces[m_front];
    face->Show(m_cards[static_cast<std::size_t>(m_at)]);
    face->Enter(CardFrame(), animate);

    const int left = static_cast<int>(m_cards.size()) - m_at;
    // Los fantasmas solo se ven si de verdad hay algo detrás. Una pila de tres cuando
    // queda una es una promesa que no se cumple.
    for (int i = 0; i < 2; ++i) {
        m_stack[i]->SetVisible(left > i + 1);
    }
    if (m_header) m_header->Invalidate();
}

void Review::Decide(Model::Priority priority) {
    if (!m_running || m_done || m_at >= static_cast<int>(m_cards.size())) return;
    const Card& card = m_cards[static_cast<std::size_t>(m_at)];
    const std::string repoId = card.repoId;

    // Quien decide es App: es quien conoce el límite de Enfoque y quien escribe. Un "no"
    // deja la tarjeta donde está —temblando— y la hoja que explica por qué sale por encima.
    if (m_decide && !m_decide(repoId, priority)) {
        m_faces[m_front]->Refuse();
        return;
    }
    Advance(priority);
}

void Review::Accepted(const std::string& repoId, Model::Priority priority) {
    if (!m_running || m_at >= static_cast<int>(m_cards.size())) return;
    if (m_cards[static_cast<std::size_t>(m_at)].repoId != repoId) return;
    Advance(priority);
}

void Review::Advance(Model::Priority priority) {
    const int index = static_cast<int>(priority);
    if (index >= 0 && index < 5) ++m_tally[index];
    ++m_decided;

    // La tarjeta sale hacia SU diana. Las cuatro salen del mismo sitio que las pintadas.
    Rect target = CardFrame();
    if (m_footer && index >= 0 && index < kChips) {
        const Rect chip = ChipRect(Rect{0.0f, 0.0f, m_footer->Frame().width, kChipHeight},
                                   index);
        target = chip.Moved(m_footer->Frame().x, m_footer->Frame().y);
    }
    CancelEdit();
    m_faces[m_front]->FlyTo(target);
    m_front = 1 - m_front;
    ++m_at;

    UpdateProgress(true);
    ShowCurrent(true);
}

// Aplazar no es saltar: saltar manda la tarjeta al final de ESTA sesión, y aplazar la saca
// de la pila hasta que venza el plazo. Cuenta como decidida —porque se decidió no decidir
// todavía, que es una decisión— y por eso mueve la barra de progreso.
void Review::Postpone() {
    if (!m_running || m_done || m_at >= static_cast<int>(m_cards.size())) return;
    if (m_snooze) m_snooze(m_cards[static_cast<std::size_t>(m_at)].repoId);

    ++m_decided;
    ++m_postponed;
    CancelEdit();
    // Sale por abajo, como la saltada: no va a ningún grupo, así que no tiene diana a la
    // que ir. Lo que dice el viaje es «esta se aparta», y eso es hacia fuera.
    m_faces[m_front]->FlyTo(CardFrame().Moved(0.0f, Frame().height));
    m_front = 1 - m_front;
    ++m_at;
    UpdateProgress(true);
    ShowCurrent(true);
}

void Review::Skip() {
    if (!m_running || m_done) return;
    const int count = static_cast<int>(m_cards.size());
    if (m_at >= count) return;
    // Al final de la pila, no fuera de ella: saltar no es decidir, y un repositorio que se
    // salta tiene que volver a pasar por delante antes de que la revisión termine.
    if (count - m_at > 1) {
        Card moved = m_cards[static_cast<std::size_t>(m_at)];
        m_cards.erase(m_cards.begin() + m_at);
        m_cards.push_back(std::move(moved));
    }
    CancelEdit();
    m_faces[m_front]->FlyTo(CardFrame().Moved(0.0f, Frame().height));
    m_front = 1 - m_front;
    ShowCurrent(true);
}

void Review::UpdateProgress(bool animate) {
    if (m_fill == nullptr) return;
    const float fraction =
        m_total > 0 ? static_cast<float>(m_decided) / static_cast<float>(m_total) : 0.0f;
    m_fill->SetFill(fraction, animate);
}

// --------------------------------------------------------------- El siguiente paso --

void Review::BeginEdit() {
    if (m_step == nullptr || !Attached() || m_done) return;
    if (m_at >= static_cast<int>(m_cards.size())) return;
    m_step->SetText(m_cards[static_cast<std::size_t>(m_at)].nextStep);
    m_step->SetVisible(true);
    m_step->SetFrame(CardView::StepRect(CardFrame()));
    m_faces[m_front]->SetEditing(true);
    HostRef().Input().Focus(m_step, true);
}

bool Review::Editing() const {
    return Attached() && m_step != nullptr && HostRef().Input().Focused() == m_step;
}

void Review::CommitEdit() {
    if (m_step == nullptr || !m_step->Visible()) return;
    const std::wstring text = m_step->Text();
    m_step->SetVisible(false);
    m_faces[m_front]->SetEditing(false);
    if (m_at >= static_cast<int>(m_cards.size())) return;

    Card& card = m_cards[static_cast<std::size_t>(m_at)];
    if (text == card.nextStep) return;
    // La copia de la pila se actualiza también, y no solo SQLite: la tarjeta se repinta
    // desde aquí, y si se volviera a leer del estado habría que esperar a que App la
    // guardara para ver lo que se acaba de escribir.
    card.nextStep = text;
    m_faces[m_front]->Show(card);
    if (m_nextStep) m_nextStep(card.repoId, text);
}

bool Review::CancelEdit() {
    if (m_step == nullptr || !m_step->Visible()) return false;
    // Se descarta lo escrito, como Esc en cualquier otro sitio. El orden importa: primero
    // se devuelve el texto guardado y después se suelta el foco, porque soltarlo dispara
    // el OnBlur que guarda.
    if (m_at < static_cast<int>(m_cards.size())) {
        m_step->SetText(m_cards[static_cast<std::size_t>(m_at)].nextStep);
    }
    m_step->SetVisible(false);
    m_faces[m_front]->SetEditing(false);
    if (Attached() && HostRef().Input().Focused() == m_step) {
        HostRef().Input().Focus(nullptr, false);
    }
    return true;
}

// ------------------------------------------------------------------- El teclado --

bool Review::Keys(const Input::Key& e) {
    if (!e.down || !m_running) return false;
    // Lo único que no es nuestro: Alt+F4 y Alt+Espacio son de Windows, y comérselas dejaría
    // una pantalla de la que no se puede salir cerrando la ventana. Y una letra con Control
    // tampoco es nuestra: aquí la E y la P son letras sueltas, y dejar que Ctrl+E abriera
    // el campo de texto sería el mismo descuido que tenía Ui::List con la J y la K.
    if (e.system || Input::Has(e.modifiers, Input::Modifiers::Alt) ||
        Input::Has(e.modifiers, Input::Modifiers::Control)) {
        return false;
    }

    // Mientras se escribe, las teclas son letras. El campo ya se ha quedado las suyas antes
    // de llegar aquí —el enrutador va primero al que tiene el foco—, así que lo que cae por
    // aquí es todo lo que el campo no quiso.
    //
    // Y lo que no quiso se devuelve SIN CONSUMIR, que es lo contrario de lo que hace el
    // resto de esta función. Consumir una tecla hace que Shell::Window se coma su WM_CHAR
    // —es lo que evita desde la fase 5 que un atajo de una letra se escriba además dentro
    // del campo que acaba de abrir— así que una revisión que se quedara también las teclas
    // mientras se edita sería un campo de texto en el que no se puede escribir ni una
    // letra. Es exactamente lo que pasaba: el campo se abría, se escribía y salía vacío.
    if (Editing()) {
        if (e.virtualKey == VK_ESCAPE) {
            CancelEdit();
            return true;
        }
        return false;
    }

    if (e.virtualKey == VK_ESCAPE) {
        Finish();
        return true;
    }
    // Con el resumen puesto no quedan decisiones: solo se sale.
    if (m_done) {
        if (e.virtualKey == VK_RETURN || e.virtualKey == VK_SPACE) {
            Finish();
            return true;
        }
        return true;
    }
    if (e.virtualKey == VK_SPACE) {
        Skip();
        return true;
    }
    if (e.virtualKey == 'E') {
        BeginEdit();
        return true;
    }
    if (e.virtualKey == 'P') {
        Postpone();
        return true;
    }
    const int digit = e.virtualKey - '1';
    if (digit >= 0 && digit < kChips) {
        Decide(static_cast<Model::Priority>(digit));
        return true;
    }
    // Todo lo demás se lo queda igual: esto es un modo, no una capa por la que se cuelan
    // los atajos de la lista de detrás.
    return true;
}

// --------------------------------------------------------------------- El resumen --

void Review::ShowSummary() {
    m_done = true;
    for (CardView* face : m_faces) face->HideNow();
    for (Bar* ghost : m_stack) ghost->SetVisible(false);
    // Y el pie con las dianas: ya no queda nada que decidir, y cuatro teclas ofrecidas que
    // no hacen nada son cuatro teclas que alguien va a pulsar.
    if (m_footer) m_footer->SetVisible(false);
    CancelEdit();

    if (m_summary == nullptr) {
        m_summary = Add<Ui::Panel>(Ui::Panel::Surface::Sheet, Metrics::Radius::Sheet,
                                   Metrics::kElevationSheet);
        // Las barras primero y la pizarra después: dentro de un elemento los hijos se
        // pintan en el orden en que se añaden, y el texto tiene que quedar por encima.
        for (Bar*& bar : m_summaryBars) bar = m_summary->Add<Bar>(4.0f);
        m_summaryBoard = m_summary->Add<Board>([this](const Ui::Paint& paint, const Rect& box) {
            PaintSummary(paint, box);
        });
        // Un hijo que nace después del reparto del tema hereda los tokens al engancharse
        // pero no recibe un OnTheme, y Ui::Panel pone el color de su material justo ahí:
        // sin esto, el resumen sale sin fondo, con su texto flotando sobre la Mica.
        ApplyTheme(Tokens(), 0.0f);
    }
    m_summary->SetVisible(true);
    m_summary->SetOpacity(0.0f, 0.0f);
    if (m_summaryBoard) m_summaryBoard->Invalidate();
    Relayout();
    m_summary->Appear(Motion::Kind::Expressive);
    // Y las barras entran escalonadas, veinte milisegundos una detrás de otra: es el mismo
    // escalonado con el que entran las filas de una lista en la fase 4.
    for (int i = 0; i < 5; ++i) {
        m_summaryBars[i]->Enter(static_cast<float>(i) * 20.0f);
    }
    if (m_header) m_header->Invalidate();
}

// ------------------------------------------------------------------ La maquetación --

Rect Review::CardFrame() const {
    const float width = Frame().width;
    const float height = Frame().height;
    const float top = Caption::kBarHeight + kHeaderHeight + Metrics::kSpace4;
    const float bottom = height - kMargin;
    // Lo que ocupa todo menos la tarjeta: los dos fantasmas que asoman, el aire y el pie.
    const float below = kStackStep * 2.0f + Metrics::kSpace4 + kFooterHeight;

    const float cardWidth = std::clamp(width - kMargin * 2.0f, 240.0f, kCardMaxWidth);
    const float cardHeight =
        std::clamp(bottom - top - below, kCardMinHeight, kCardMaxHeight);
    // Y el bloque entero va CENTRADO en lo que queda, no pegado al título. En una ventana
    // alta la tarjeta se quedaba arriba con media pantalla de hueco negro debajo del pie, y
    // lo que se lee entonces no es una pila de tarjetas, es una pantalla a medio cargar.
    const float y = top + std::max((bottom - top - cardHeight - below) * 0.5f, 0.0f);
    return Rect{(width - cardWidth) * 0.5f, y, cardWidth, cardHeight};
}

void Review::OnArrange() {
    if (Frame().width <= 0.0f || Frame().height <= 0.0f) return;
    const float width = Frame().width;
    const float height = Frame().height;

    m_track->SetFrame(Rect{0.0f, 0.0f, width, kProgress});
    m_fill->SetFrame(Rect{0.0f, 0.0f, width, kProgress});
    m_header->SetFrame(Rect{kMargin, Caption::kBarHeight, std::max(width - kMargin * 2.0f, 1.0f),
                            kHeaderHeight});

    const Rect card = CardFrame();
    for (int i = 0; i < 2; ++i) {
        const float inset = kStackInset * static_cast<float>(i + 1);
        m_stack[i]->SetFrame(Rect{card.x + inset, card.y + kStackStep * static_cast<float>(i + 1),
                                  std::max(card.width - inset * 2.0f, 1.0f), card.height});
    }
    // Solo la de delante se coloca: la otra está volando o escondida, y escribirle el marco
    // a mitad del vuelo la plantaría en el destino y se comería la salida.
    if (m_running && !m_done && m_faces[m_front]->Visible()) {
        m_faces[m_front]->SetFrame(card);
    }
    if (m_step && m_step->Visible()) m_step->SetFrame(CardView::StepRect(card));

    m_footer->SetFrame(Rect{card.x, card.y + card.height + kStackStep * 2.0f + Metrics::kSpace4,
                            card.width, kFooterHeight});

    if (m_summary) {
        const float panelWidth = std::clamp(width - kMargin * 2.0f, 240.0f, 460.0f);
        const float panelHeight = Metrics::kSpace4 * 2.0f + 34.0f + 22.0f + Metrics::kSpace3 +
                                  5.0f * 26.0f + Metrics::kSpace3 + kHintHeight;
        const Rect panel{(width - panelWidth) * 0.5f, (height - panelHeight) * 0.5f, panelWidth,
                         panelHeight};
        m_summary->SetFrame(panel);
        if (m_summaryBoard) {
            m_summaryBoard->SetFrame(Rect{0.0f, 0.0f, panelWidth, panelHeight});
        }

        const float rowTop = Metrics::kSpace4 + 34.0f + 22.0f + Metrics::kSpace3;
        const float barMax = std::max(panelWidth - Metrics::kSpace4 * 2.0f, 1.0f);
        int most = 1;
        for (const int count : m_tally) most = std::max(most, count);
        for (int i = 0; i < 5; ++i) {
            m_summaryBars[i]->SetFrame(
                Rect{Metrics::kSpace4, rowTop + static_cast<float>(i) * 26.0f + 3.0f, barMax,
                     18.0f});
            // Un cero no dibuja nada: una barra mínima para decir "ninguno" se lee como
            // "uno" de un vistazo, que es justo lo contrario.
            m_summaryBars[i]->SetFill(
                m_tally[i] == 0 ? 0.0f
                                : static_cast<float>(m_tally[i]) / static_cast<float>(most),
                false);
        }
    }
}

void Review::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (m_track) m_track->SetColor(tokens.separator, crossfadeMs);
    if (m_fill) m_fill->SetColor(tokens.accent, crossfadeMs);
    // Los fantasmas, cada vez más apagados: es lo que se lee como profundidad sin tener que
    // dibujarles un borde. Opacos y no translúcidos, al revés que una tarjeta de la lista:
    // debajo de la de delante está su sombra, y un fantasma translúcido bajo una sombra es
    // exactamente el mismo gris que el fondo.
    for (int i = 0; i < 2; ++i) {
        if (m_stack[i] == nullptr) continue;
        Theme::Color ghost = tokens.cardSurface;
        ghost.a = static_cast<std::uint8_t>(i == 0 ? 255 : 190);
        m_stack[i]->SetColor(ghost, crossfadeMs);
    }
    for (int i = 0; i < 5; ++i) {
        if (m_summaryBars[i] == nullptr) continue;
        Theme::Color bar = Ui::ColorOf(static_cast<Model::Priority>(i), tokens);
        bar.a = 56;
        m_summaryBars[i]->SetColor(bar, crossfadeMs);
    }
    Ui::Element::OnTheme(tokens, crossfadeMs);
}

// ---------------------------------------------------------------------- El dibujo --

void Review::PaintHeader(const Ui::Paint& paint, const Rect& box) {
    Line(paint, L"Revisión semanal", box, Ui::Style::Heading, Ui::Weight::Semibold,
         paint.tokens->textPrimary);

    const int left = std::max(static_cast<int>(m_cards.size()) - m_at, 0);
    std::wstring right;
    if (m_done) {
        right = L"Terminada";
    } else {
        right = std::to_wstring(m_decided) + L" de " + std::to_wstring(m_total);
        if (left != m_total - m_decided) {
            // Hay saltadas dando vueltas: se dice, porque si no el contador parece atascado.
            right += L" · " + std::to_wstring(left) + L" en la pila";
        }
    }
    Line(paint, right, box, Ui::Style::Caption, Ui::Weight::Regular, paint.tokens->textSecondary,
         Ui::Align::Trailing);
}

void Review::PaintFooter(const Ui::Paint& paint, const Rect& box) {
    const Rect chips{0.0f, 0.0f, box.width, kChipHeight};
    for (int i = 0; i < kChips; ++i) {
        const Model::Priority priority = static_cast<Model::Priority>(i);
        const Rect chip = ChipRect(chips, i).Moved(box.x, box.y);
        Theme::Color color = Ui::ColorOf(priority, *paint.tokens);

        Theme::Color back = color;
        back.a = 40;
        Fill(paint, chip, back, Metrics::RadiusOf(Metrics::Radius::Control));
        Line(paint, std::to_wstring(i + 1) + L"  " + Ui::NameOf(priority),
             chip.Inset(Metrics::kSpace1), Ui::Style::Caption, Ui::Weight::Semibold, color,
             Ui::Align::Center);
    }

    Line(paint,
         L"E  editar     P  posponer     Espacio  saltar al final     Esc  terminar",
         Rect{box.x, box.y + kChipHeight + Metrics::kSpace1, box.width, kHintHeight},
         Ui::Style::Footnote, Ui::Weight::Regular, paint.tokens->textSecondary,
         Ui::Align::Center);
}

void Review::PaintSummary(const Ui::Paint& paint, const Rect& box) {
    const float left = box.x + Metrics::kSpace4;
    const float width = std::max(box.width - Metrics::kSpace4 * 2.0f, 1.0f);

    Line(paint, L"Revisión terminada", Rect{left, box.y + Metrics::kSpace4, width, 34.0f},
         Ui::Style::Title, Ui::Weight::Semibold, paint.tokens->textPrimary);

    const int pending = m_total - m_decided;
    const int sorted = m_decided - m_postponed;
    std::wstring sub = std::to_wstring(sorted) +
                       (sorted == 1 ? L" repositorio decidido" : L" repositorios decididos");
    if (m_postponed > 0) {
        sub += L" · " + std::to_wstring(m_postponed) +
               (m_postponed == 1 ? L" pospuesto" : L" pospuestos");
    }
    if (pending > 0) {
        sub += L" · " + std::to_wstring(pending) + L" quedan como estaban";
    }
    Line(paint, sub, Rect{left, box.y + Metrics::kSpace4 + 34.0f, width, 22.0f},
         Ui::Style::Caption, Ui::Weight::Regular, paint.tokens->textSecondary);

    const float rowTop = box.y + Metrics::kSpace4 + 34.0f + 22.0f + Metrics::kSpace3;
    for (int i = 0; i < 5; ++i) {
        const Rect row{left + Metrics::kSpace1, rowTop + static_cast<float>(i) * 26.0f,
                       width - Metrics::kSpace2, 24.0f};
        Line(paint, Ui::NameOf(static_cast<Model::Priority>(i)), row, Ui::Style::Body,
             Ui::Weight::Regular, paint.tokens->textPrimary);
        Line(paint, std::to_wstring(m_tally[i]), row, Ui::Style::Body, Ui::Weight::Semibold,
             m_tally[i] > 0 ? paint.tokens->textPrimary : paint.tokens->textDisabled,
             Ui::Align::Trailing);
    }

    Line(paint, L"Esc o Enter para volver a la lista",
         Rect{left, box.Bottom() - Metrics::kSpace4 - kHintHeight, width, kHintHeight},
         Ui::Style::Footnote, Ui::Weight::Regular, paint.tokens->textSecondary,
         Ui::Align::Center);
}

}  // namespace Views
