#include "views/Palette.h"

#include <Windows.h>

#include <algorithm>

#include "app/State.h"
#include "compositor/Paint.h"
#include "ui/Controls.h"
#include "ui/Field.h"
#include "ui/Host.h"
#include "ui/List.h"
#include "ui/Overlays.h"
#include "ui/Text.h"

namespace Views {

namespace {

using Ui::Rect;

constexpr float kWidth = 560.0f;
constexpr float kPad = Metrics::kSpace2;
constexpr float kFieldHeight = 36.0f;
constexpr float kRowHeight = Metrics::kRowHeight;
// Ocho filas y a desplazarse. Una paleta tan alta como la ventana deja de ser una paleta.
constexpr int kMaxRows = 8;
// Desde arriba, pero no desde fuera de la pantalla: entra desde justo encima del borde de
// la ventana, que es de donde se lee que viene «de arriba» sin que el viaje se alargue.
constexpr float kTop = 88.0f;

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

}  // namespace

Palette::Palette(std::vector<Action> actions) : m_actions(std::move(actions)) {
    // Plegado UNA vez, al abrir. Filtrar en cada pulsación vuelve a recorrer esto, y
    // volver a plegar ciento veinte cadenas en cada letra es exactamente lo que haría que
    // escribir en la paleta dejara de ser instantáneo.
    m_haystacks.reserve(m_actions.size());
    for (const Action& action : m_actions) {
        std::wstring both = action.label;
        if (!action.hint.empty()) {
            both.push_back(L' ');
            both += action.hint;
        }
        m_haystacks.push_back(App::Fold(both));
    }
}

bool Palette::OnAttach() {
    m_panel = Add<Ui::Panel>(Ui::Panel::Surface::Sheet, Metrics::Radius::Panel,
                             Metrics::kElevationSheet);

    m_field = m_panel->Add<Ui::Field>(L"Busca un repositorio o escribe una acción");
    m_field->OnChanged([this](const std::wstring& text) { Filter(text); });
    m_field->OnSubmit([this] { Run(m_list ? m_list->Selected() : -1); });

    m_list = m_panel->Add<Ui::List>();
    m_list->SetRowHeight(kRowHeight);
    m_list->SetPadding(Metrics::kSpace1 * 0.5f, Metrics::kSpace1 * 0.5f);
    m_list->SetRowPainter(
        [this](const Ui::Paint& paint, const Rect& box, const Ui::List::RowState& state) {
            PaintRow(paint, box, state.index, state.hovered, state.selected);
        });
    // Un clic ejecuta. Aquí no hay diferencia entre elegir y abrir: no hay nada más que
    // hacer con una opción de la paleta que ejecutarla.
    m_list->OnClicked([this](int index) { Run(index); });
    m_list->OnActivate([this](int index) { Run(index); });

    m_empty = m_panel->Add<Ui::Label>(L"Nada con esas palabras", Ui::Style::Body);
    m_empty->UseSecondary();
    m_empty->SetAlign(Ui::Align::Center);
    m_empty->SetVisible(false);

    Filter(std::wstring());
    return true;
}

void Palette::Filter(const std::wstring& query) {
    const std::vector<std::wstring> terms = App::Terms(query);

    m_shown.clear();
    for (std::size_t i = 0; i < m_actions.size(); ++i) {
        bool all = true;
        for (const std::wstring& term : terms) {
            if (m_haystacks[i].find(term) == std::wstring::npos) {
                all = false;
                break;
            }
        }
        if (all) m_shown.push_back(static_cast<int>(i));
    }

    if (m_list) {
        m_list->SetCount(static_cast<int>(m_shown.size()), false);
        // Siempre hay una elegida: Enter tiene que ejecutar algo sin haber tocado las
        // flechas, que es como se usa una paleta.
        m_list->SetSelected(m_shown.empty() ? -1 : 0);
    }
    if (m_empty) m_empty->SetVisible(m_shown.empty());
    // El panel crece y encoge con lo que hay dentro. Es la misma cuenta que el alto de la
    // hoja: un panel de alto fijo con un solo resultado es un bloque de aire vacío.
    if (Attached()) Relayout();
}

void Palette::Run(int shown) {
    if (shown < 0 || shown >= static_cast<int>(m_shown.size())) return;
    const std::function<void()> action = m_actions[static_cast<std::size_t>(
                                                       m_shown[static_cast<std::size_t>(shown)])]
                                             .run;

    // Cerrar destruye la paleta, y con ella el campo de texto desde cuyo OnKey estamos
    // llegando aquí. Así que las dos cosas se aplazan al siguiente turno de la cola, y en
    // este orden: primero se cierra y después se ejecuta. Al revés, el PopModal del cierre
    // le quitaría el foco a lo que la acción acabara de enfocar —«Editar el siguiente
    // paso» es exactamente eso— y la acción se quedaría a medias sin dar ningún error.
    Ui::Host& host = HostRef();
    Ui::Element* self = this;
    if (const auto queue = host.Queue()) {
        queue.TryEnqueue([&host, self, action] {
            host.PopLayer(self);
            if (action) action();
        });
    }
}

void Palette::OnArrange() {
    if (!m_panel || !Attached()) return;
    // Todavía sin tamaño: el primer filtrado corre dentro de OnAttach, antes de que el Host
    // le dé a la capa el tamaño de la ventana. Colocar aquí con cero de ancho gastaría la
    // animación de entrada contra una esquina, y la buena llegaría ya sin viaje.
    if (Frame().width <= 0.0f || Frame().height <= 0.0f) return;

    const float width = std::min(kWidth, std::max(Frame().width - Metrics::kSpace5 * 2.0f,
                                                  240.0f));
    const float inner = std::max(width - kPad * 2.0f, 1.0f);

    const int rows = std::clamp(static_cast<int>(m_shown.size()), 1, kMaxRows);
    const float listHeight = static_cast<float>(rows) * kRowHeight + Metrics::kSpace1;
    const float height = kPad + kFieldHeight + Metrics::kSpace1 + listHeight + kPad;

    const float x = (Frame().width - width) * 0.5f;
    const float y = std::min(kTop, std::max(Frame().height - height - Metrics::kSpace4,
                                            Metrics::kSpace2));

    m_panelRect = Rect{x, y, width, height};
    // Entra desde arriba: se coloca por encima del borde y viaja hasta su sitio con el
    // muelle estándar, que es el de los paneles en la tabla de CLAUDE.md.
    m_panel->SetFrame(m_placed ? m_panelRect : Rect{x, -height, width, height});

    m_field->SetFrame(Rect{kPad, kPad, inner, kFieldHeight});
    const float listTop = kPad + kFieldHeight + Metrics::kSpace1;
    m_list->SetFrame(Rect{kPad, listTop, inner, listHeight});
    m_empty->SetFrame(Rect{kPad, listTop, inner, kRowHeight});

    if (!m_placed) {
        m_placed = true;
        m_panel->SetOpacity(0.0f, 0.0f);
        m_panel->SlideTo(x, y, Motion::Kind::Standard);
        m_panel->SetOpacity(1.0f, HostRef().Animator().FadeMs(Motion::Kind::Standard));
    }
}

void Palette::PaintRow(const Ui::Paint& paint, const Rect& box, int shown, bool hovered,
                       bool selected) {
    if (shown < 0 || shown >= static_cast<int>(m_shown.size())) return;
    const Action& action = m_actions[static_cast<std::size_t>(
        m_shown[static_cast<std::size_t>(shown)])];

    if (selected || hovered) {
        winrt::com_ptr<ID2D1SolidColorBrush> veil;
        paint.dc->CreateSolidColorBrush(
            Gfx::ToD2D(selected ? paint.tokens->selectionRow : paint.tokens->controlHover),
            veil.put());
        const float radius = Metrics::RadiusOf(Metrics::Radius::Control);
        paint.dc->FillRoundedRectangle(D2D1::RoundedRect(ToBox(box), radius, radius),
                                       veil.get());
    }

    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    winrt::com_ptr<ID2D1SolidColorBrush> dim;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textPrimary), ink.put());
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), dim.put());

    // La pista se mide y se le resta al rótulo. A ojo, un nombre largo se le mete debajo.
    const float hintWidth =
        action.hint.empty()
            ? 0.0f
            : std::min(paint.text->Measure(action.hint, Ui::Style::Caption,
                                           Ui::Weight::Regular).width,
                       std::max(box.width * 0.45f, 1.0f));
    const float labelWidth =
        std::max(box.width - Metrics::kSpace2 * 2.0f - hintWidth - Metrics::kSpace2, 1.0f);

    paint.text->DrawLine(paint.dc, action.label, Ui::Style::Body, Ui::Weight::Regular,
                         ToBox(Rect{box.x + Metrics::kSpace2, box.y, labelWidth, box.height}),
                         ink.get());
    if (hintWidth > 0.0f) {
        Ui::Run hint;
        hint.text = action.hint;
        hint.style = Ui::Style::Caption;
        hint.align = Ui::Align::Trailing;
        paint.text->Draw(paint.dc, hint,
                         ToBox(Rect{box.Right() - Metrics::kSpace2 - hintWidth, box.y,
                                    hintWidth, box.height}),
                         dim.get());
    }
}

bool Palette::HitTest(float lx, float ly) const { return m_panelRect.Contains(lx, ly); }

bool Palette::OnKey(const Input::Key& e) {
    if (!e.down || m_list == nullptr) return false;
    // Las flechas mueven la lista aunque el foco esté en el campo, que es donde tiene que
    // estar: escribir y elegir son la misma cosa aquí. Duplicar lo que la lista ya sabe
    // hacer sería tener dos ideas de qué es "el siguiente".
    switch (e.virtualKey) {
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_HOME:
    case VK_END:
        return m_list->Navigate(e);
    case VK_RETURN:
        Run(m_list->Selected());
        return true;
    default:
        return false;
    }
}

}  // namespace Views
