#include "ui/Overlays.h"

#include <algorithm>

#include "compositor/Paint.h"
#include "ui/Controls.h"
#include "ui/Host.h"
#include "ui/Text.h"

namespace Ui {

namespace {

constexpr float kMenuItemHeight = 30.0f;
constexpr float kMenuPadding = Metrics::kSpace1 * 0.5f;
constexpr float kMenuMinWidth = 180.0f;
constexpr float kMenuMaxWidth = 320.0f;
constexpr float kEdge = Metrics::kSpace1;

constexpr float kToastHeight = 44.0f;
constexpr float kToastBottom = Metrics::kSpace5;

constexpr float kSheetWidth = 420.0f;
constexpr float kSheetPadding = Metrics::kSpace4;

// Lo que encoge un panel flotante antes de asentarse. Poco: es una aparición, no un
// rebote.
constexpr float kAppearScale = 0.97f;

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

}  // namespace

// ============================================================================ Panel ==

Panel::Panel(Surface surface, Metrics::Radius radius, Metrics::Elevation elevation)
    : m_surface(surface), m_radius(radius), m_elevation(elevation) {}

bool Panel::OnAttach() {
    // El orden importa: la sombra primero, porque mete todo lo demás dentro de su capa
    // para sacar la máscara del alfa. Lo que se cree antes se queda fuera.
    CreateShadow(m_elevation);
    if (!CreateMaterial(Metrics::RadiusOf(m_radius))) return false;
    // Un canto fino además de la sombra. Sin él, sobre un fondo del mismo tono el panel
    // no tiene borde; y si la sombra no estuviera disponible, es lo único que lo separa.
    MaterialOf()->CreateStroke(1.0f);
    if (!CreateLayer()) return false;
    HostRef().Animator().BindCenterPoint(Visual());
    return true;
}

void Panel::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    Theme::Color surface = tokens.menuSurface;
    if (m_surface == Surface::Toast) surface = tokens.toastSurface;

    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(surface, HostRef().Animator(), crossfadeMs);
        material->SetStroke(tokens.controlStroke, HostRef().Animator(), crossfadeMs);
    }
    if (Gfx::Shadow* shadow = ShadowOf()) {
        shadow->SetColor(tokens.shadow, HostRef().Animator(), crossfadeMs);
    }
    Element::OnTheme(tokens, crossfadeMs);
}

void Panel::Appear(Motion::Kind kind) {
    if (!Attached()) return;
    Motion::Animator& animator = HostRef().Animator();

    Visual().StopAnimation(L"Scale");
    Visual().Scale({kAppearScale, kAppearScale, 1.0f});
    Visual().Opacity(0.0f);

    animator.Scale(Visual(), {1.0f, 1.0f, 1.0f}, kind);
    animator.Opacity(Visual(), 1.0f, animator.FadeMs(kind));
}

// ============================================================================= Menu ==

// Una opción. No tiene superficie propia: se pinta en la del panel, que es una sola
// textura para todo el menú.
class Menu::Item : public Element {
public:
    Item(Menu* menu, int index, std::wstring label)
        : m_menu(menu), m_index(index), m_label(std::move(label)) {}

    // Público: el menú lo llama para saber de qué ancho tiene que ser.
    float Measure() {
        return HostRef().Text().Measure(m_label, Style::Body, Weight::Regular).width;
    }

    void SetHighlighted(bool value) {
        if (m_highlighted == value) return;
        m_highlighted = value;
        Invalidate();
    }

protected:
    void OnPaint(const Paint& paint, const Rect& box) override {
        if (m_highlighted) {
            winrt::com_ptr<ID2D1SolidColorBrush> veil;
            paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->controlHover), veil.put());
            const float radius = Metrics::RadiusOf(Metrics::Radius::Control);
            paint.dc->FillRoundedRectangle(D2D1::RoundedRect(ToBox(box), radius, radius),
                                           veil.get());
        }
        winrt::com_ptr<ID2D1SolidColorBrush> ink;
        paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textPrimary), ink.put());
        paint.text->DrawLine(paint.dc, m_label, Style::Body, Weight::Regular,
                             ToBox(Rect{box.x + Metrics::kSpace2, box.y,
                                        std::max(box.width - Metrics::kSpace2 * 2.0f, 1.0f),
                                        box.height}),
                             ink.get());
    }

    void OnStateChanged() override {
        if (Hovered()) m_menu->Highlight(m_index);
        Invalidate();
    }

    bool OnPointer(const Input::Pointer& e) override {
        if (e.button != Input::Button::Left) return false;
        // Se activa al SOLTAR, no al pulsar: así arrastrar por el menú y soltar fuera es
        // arrepentirse, que es como funciona un menú en todas partes.
        if (e.action == Input::Action::Up) m_menu->Activate(m_index);
        return true;
    }

private:
    Menu* m_menu = nullptr;
    int m_index = 0;
    std::wstring m_label;
    bool m_highlighted = false;
};

Menu::Menu(std::vector<Entry> entries, float xDip, float yDip)
    : m_entries(std::move(entries)), m_x(xDip), m_y(yDip) {}

bool Menu::OnAttach() {
    m_panel = Add<Panel>(Panel::Surface::Menu, Metrics::Radius::Card, Metrics::kElevationMenu);
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        m_items.push_back(
            m_panel->Add<Item>(this, static_cast<int>(i), m_entries[i].label));
    }
    return true;
}

void Menu::OnArrange() {
    if (!m_panel || m_items.empty()) return;

    float width = kMenuMinWidth;
    for (Item* item : m_items) {
        width = std::max(width, item->Measure() + Metrics::kSpace2 * 2.0f + Metrics::kSpace4);
    }
    width = std::min(width, kMenuMaxWidth);

    const float height =
        static_cast<float>(m_items.size()) * kMenuItemHeight + kMenuPadding * 2.0f;

    // Sujeto a la ventana: un menú que se abre junto al borde derecho tiene que caber, no
    // salirse.
    const float x = std::clamp(m_x, kEdge, std::max(Frame().width - width - kEdge, kEdge));
    const float y = std::clamp(m_y, kEdge, std::max(Frame().height - height - kEdge, kEdge));

    m_panelRect = Rect{x, y, width, height};
    m_panel->SetFrame(m_panelRect);

    float itemY = kMenuPadding;
    for (Item* item : m_items) {
        item->SetFrame(Rect{kMenuPadding, itemY, width - kMenuPadding * 2.0f, kMenuItemHeight});
        itemY += kMenuItemHeight;
    }

    if (!m_placed) {
        m_placed = true;
        m_panel->Appear(Motion::Kind::Standard);
    }
}

bool Menu::HitTest(float lx, float ly) const { return m_panelRect.Contains(lx, ly); }

void Menu::Highlight(int index) {
    if (m_highlight == index) return;
    m_highlight = index;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        m_items[i]->SetHighlighted(static_cast<int>(i) == index);
    }
}

void Menu::Activate(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    // La acción se copia antes de cerrar: cerrar destruye el menú, y con él la lambda que
    // estamos a punto de llamar.
    const std::function<void()> action = m_entries[static_cast<std::size_t>(index)].action;
    HostRef().PopLayer(this);
    if (action) action();
}

bool Menu::OnKey(const Input::Key& e) {
    if (!e.down) return false;
    const int count = static_cast<int>(m_items.size());
    if (count == 0) return false;

    switch (e.virtualKey) {
    case VK_DOWN:
        Highlight(m_highlight + 1 >= count ? 0 : m_highlight + 1);
        return true;
    case VK_UP:
        Highlight(m_highlight <= 0 ? count - 1 : m_highlight - 1);
        return true;
    case VK_RETURN:
    case VK_SPACE:
        Activate(m_highlight);
        return true;
    default:
        return false;
    }
}

// ============================================================================ Toast ==

Toast::Toast(std::wstring message, int millis)
    : m_message(std::move(message)), m_millis(millis) {}

Toast::~Toast() {
    if (m_timer) m_timer.Stop();
}

bool Toast::OnAttach() {
    m_panel = Add<Panel>(Panel::Surface::Toast, Metrics::Radius::Card, Metrics::kElevationMenu);
    m_label = m_panel->Add<Label>(m_message, Style::Body, Weight::Regular);
    m_label->SetAlign(Align::Center);

    // Se va solo con un temporizador de la cola del hilo, no con un WM_TIMER: es la misma
    // cola que ya lleva el compositor y no hace falta otro mensaje en la ventana.
    if (const auto queue = HostRef().Queue()) {
        m_timer = queue.CreateTimer();
        m_timer.Interval(std::chrono::milliseconds(m_millis));
        m_timer.IsRepeating(false);
        m_timer.Tick([this](auto&&, auto&&) { Dismiss(); });
        m_timer.Start();
    }
    return true;
}

void Toast::Dismiss() {
    if (m_timer) m_timer.Stop();
    Host& host = HostRef();
    Element* self = this;
    // Aplazado a la siguiente vuelta de la cola. Cerrar destruye el aviso, y con él el
    // temporizador desde cuyo Tick estamos: destruirlo aquí mismo sería tirar del suelo
    // que se está pisando.
    if (const auto queue = host.Queue()) {
        queue.TryEnqueue([&host, self] { host.PopLayer(self); });
    }
}

void Toast::OnArrange() {
    if (!m_panel || !m_label) return;

    const float width = std::min(std::max(m_label->PreferredWidth() + Metrics::kSpace4 * 2.0f,
                                          220.0f),
                                 std::max(Frame().width - Metrics::kSpace5 * 2.0f, 220.0f));
    const float x = (Frame().width - width) * 0.5f;
    const float y = Frame().height - kToastHeight - kToastBottom;

    m_panelRect = Rect{x, y, width, kToastHeight};
    m_panel->SetFrame(m_panelRect);
    m_label->SetFrame(Rect{Metrics::kSpace2, 0.0f,
                           std::max(width - Metrics::kSpace2 * 2.0f, 1.0f), kToastHeight});
    m_panel->Appear(Motion::Kind::Standard);
}

bool Toast::HitTest(float lx, float ly) const { return m_panelRect.Contains(lx, ly); }

bool Toast::OnPointer(const Input::Pointer& e) {
    // Pulsarlo lo cierra: un aviso que estorba tiene que poder quitarse de en medio.
    if (e.action == Input::Action::Down) Dismiss();
    return true;
}

// ============================================================================ Sheet ==

Sheet::Sheet(std::wstring title, std::wstring body)
    : m_title(std::move(title)),
      m_body(std::move(body)),
      m_acceptText(L"Cerrar") {}

void Sheet::SetActions(std::wstring accept, std::wstring cancel) {
    m_acceptText = std::move(accept);
    m_cancelText = std::move(cancel);
    if (m_acceptButton) m_acceptButton->SetLabel(m_acceptText);
    if (m_cancelButton) {
        m_cancelButton->SetLabel(m_cancelText);
        m_cancelButton->SetVisible(!m_cancelText.empty());
    }
    if (Attached()) Relayout();
}

bool Sheet::OnAttach() {
    m_panel = Add<Panel>(Panel::Surface::Sheet, Metrics::Radius::Sheet, Metrics::kElevationSheet);
    m_titleLabel = m_panel->Add<Label>(m_title, Style::Heading, Weight::Semibold);
    m_bodyLabel = m_panel->Add<Label>(m_body, Style::Body, Weight::Regular);
    m_bodyLabel->UseSecondary();
    // En varias líneas: es una explicación, y una elipsis a mitad de una explicación la
    // convierte en nada.
    m_bodyLabel->SetWrap(true);

    m_cancelButton = m_panel->Add<Button>(m_cancelText, ButtonKind::Plain);
    m_cancelButton->SetVisible(!m_cancelText.empty());
    m_cancelButton->OnActivate([this] { Leave(false); });

    m_acceptButton = m_panel->Add<Button>(m_acceptText, ButtonKind::Primary);
    m_acceptButton->OnActivate([this] { Leave(true); });
    return true;
}

void Sheet::Leave(bool accepted) {
    // La acción se copia ANTES de cerrar: cerrar destruye la hoja, y con ella la lambda
    // desde la que estamos. Es lo mismo que hace Ui::Menu::Activate.
    const std::function<void()> action = accepted ? m_accepted : m_cancelled;
    Host& host = HostRef();
    Element* self = this;
    if (const auto queue = host.Queue()) {
        queue.TryEnqueue([&host, self] { host.PopLayer(self); });
    } else {
        host.PopLayer(self);
    }
    if (action) action();
}

bool Sheet::OnKey(const Input::Key& e) {
    if (!e.down) return false;
    // Enter acepta y Esc se va. Una hoja que pregunta si se puede escribir en el repositorio
    // de alguien tiene que poder contestarse sin buscar el ratón.
    if (e.virtualKey == VK_RETURN) {
        Leave(true);
        return true;
    }
    if (e.virtualKey == VK_ESCAPE) {
        Leave(false);
        return true;
    }
    return false;
}

void Sheet::OnArrange() {
    if (!m_panel || !Attached()) return;

    const float width = std::min(kSheetWidth, std::max(Frame().width - Metrics::kSpace5 * 2.0f,
                                                       240.0f));
    const float inner = std::max(width - kSheetPadding * 2.0f, 1.0f);

    // El alto sale del texto MEDIDO. Con un número fijo, una explicación de cuatro renglones
    // se queda con dos dentro del panel y dos fuera — y esta hoja es justo la que no puede
    // salir recortada.
    const float bodyHeight = std::max(
        HostRef().Text().Measure(m_body, Style::Body, Weight::Regular, Figures::Proportional,
                                 inner).height,
        20.0f);

    constexpr float kTitleHeight = 30.0f;
    const float height = kSheetPadding + kTitleHeight + Metrics::kSpace2 + bodyHeight +
                         Metrics::kSpace4 + Metrics::kControlHeight + kSheetPadding;

    const float x = (Frame().width - width) * 0.5f;
    const float y = std::max((Frame().height - height) * 0.5f, Metrics::kSpace4);

    m_panelRect = Rect{x, y, width, height};
    m_panel->SetFrame(m_panelRect);

    float cursor = kSheetPadding;
    m_titleLabel->SetFrame(Rect{kSheetPadding, cursor, inner, kTitleHeight});
    cursor += kTitleHeight + Metrics::kSpace2;
    m_bodyLabel->SetFrame(Rect{kSheetPadding, cursor, inner, bodyHeight});

    const float buttonsTop = height - kSheetPadding - Metrics::kControlHeight;
    const float acceptWidth =
        std::min(std::max(m_acceptButton->PreferredWidth(), 110.0f), inner);
    m_acceptButton->SetFrame(Rect{width - kSheetPadding - acceptWidth, buttonsTop, acceptWidth,
                                  Metrics::kControlHeight});
    if (m_cancelButton->Visible()) {
        const float cancelWidth =
            std::min(std::max(m_cancelButton->PreferredWidth(), 90.0f), inner - acceptWidth);
        m_cancelButton->SetFrame(Rect{width - kSheetPadding - acceptWidth - Metrics::kSpace1 -
                                          cancelWidth,
                                      buttonsTop, cancelWidth, Metrics::kControlHeight});
    }

    // Expresivo: es una hoja modal, y es el muelle que pide la tabla de CLAUDE.md.
    m_panel->Appear(Motion::Kind::Expressive);
}

bool Sheet::HitTest(float lx, float ly) const { return m_panelRect.Contains(lx, ly); }

}  // namespace Ui
