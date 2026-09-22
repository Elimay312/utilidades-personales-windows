#include "ui/Sidebar.h"

#include <algorithm>

#include "compositor/Paint.h"
#include "ui/Host.h"
#include "ui/Text.h"

namespace Ui {

namespace {

constexpr float kItemHeight = Metrics::kRowHeight;
constexpr float kItemGap = 2.0f;
constexpr float kGlyphWidth = 22.0f;
constexpr float kCountWidth = 36.0f;
// El título de sección y el aire que deja debajo. Es la altura de una mayúscula de 11 DIP
// más los 8 de la rejilla, no un número a ojo.
constexpr float kHeaderHeight = 26.0f;

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

}  // namespace

// =========================================================================== Item ==

SidebarItem::SidebarItem(std::wstring glyph, std::wstring label, int count)
    : m_glyph(std::move(glyph)), m_label(std::move(label)), m_count(count) {}

SidebarGroup* SidebarItem::Group() const {
    return static_cast<SidebarGroup*>(Parent());
}

void SidebarItem::SetCount(int count) {
    if (m_count == count) return;
    m_count = count;
    Invalidate();
}

void SidebarItem::SetDropTarget(bool value) {
    if (m_drop == value) return;
    m_drop = value;
    Invalidate();
}

void SidebarItem::OnStateChanged() { Invalidate(); }

void SidebarItem::OnPaint(const Paint& paint, const Rect& box) {
    const bool selected = Group() && Group()->Selected() == Group()->IndexOf(this);

    // El hover se pinta aquí y no con un material propio: un material por fila serían
    // seis visuales más en la barra lateral para algo que solo se ve uno a la vez. La
    // selección sí es material, porque tiene que DESLIZARSE.
    // El velo de "estás señalando esto". El de soltar se pinta TAMBIÉN sobre el elegido:
    // arrastrar desde Todos hasta el grupo que ya está seleccionado es un caso normal, y sin
    // esto sería el único sitio de la barra que no contesta.
    if (m_drop || (Hovered() && !selected)) {
        winrt::com_ptr<ID2D1SolidColorBrush> veil;
        paint.dc->CreateSolidColorBrush(
            Gfx::ToD2D(m_drop ? paint.tokens->selectionRow : paint.tokens->controlHover),
            veil.put());
        const float radius = Metrics::RadiusOf(Metrics::Radius::Control);
        paint.dc->FillRoundedRectangle(D2D1::RoundedRect(ToBox(box), radius, radius),
                                       veil.get());
    }

    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    winrt::com_ptr<ID2D1SolidColorBrush> dim;
    paint.dc->CreateSolidColorBrush(
        Gfx::ToD2D(selected ? paint.tokens->textPrimary : paint.tokens->textSecondary), ink.put());
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), dim.put());

    if (!m_glyph.empty()) {
        paint.text->DrawGlyph(paint.dc, m_glyph, Metrics::kIconSize,
                              ToBox(Rect{box.x + Metrics::kSpace1, box.y, kGlyphWidth,
                                         box.height}),
                              ink.get());
    }

    Run label;
    label.text = m_label;
    label.style = Style::Body;
    label.weight = selected ? Weight::Semibold : Weight::Regular;
    const float labelX = box.x + Metrics::kSpace1 + kGlyphWidth;
    const float labelWidth =
        std::max(box.width - (labelX - box.x) - kCountWidth - Metrics::kSpace1, 1.0f);
    paint.text->Draw(paint.dc, label, ToBox(Rect{labelX, box.y, labelWidth, box.height}),
                     ink.get());

    if (m_count > 0) {
        const std::wstring count = std::to_wstring(m_count);
        Run number;
        number.text = count;
        number.style = Style::Caption;
        number.align = Align::Trailing;
        // Tabulares: es una columna de números que cambia al sincronizar, y con cifras de
        // anchura variable el contador baila cada vez que pasa de 9 a 10.
        number.figures = Figures::Tabular;
        paint.text->Draw(paint.dc, number,
                         ToBox(Rect{box.Right() - kCountWidth - Metrics::kSpace1, box.y,
                                    kCountWidth, box.height}),
                         dim.get());
    }
}

bool SidebarItem::OnPointer(const Input::Pointer& e) {
    if (e.button != Input::Button::Left) return false;
    if (e.action != Input::Action::Down) return e.action == Input::Action::Up;
    if (SidebarGroup* group = Group()) group->Select(group->IndexOf(this), true);
    return true;
}

bool SidebarItem::OnKey(const Input::Key& e) {
    if (!e.down) return false;
    if (e.virtualKey != VK_SPACE && e.virtualKey != VK_RETURN) return false;
    if (SidebarGroup* group = Group()) group->Select(group->IndexOf(this), true);
    return true;
}

// ========================================================================== Group ==

bool SidebarGroup::OnAttach() {
    // El material del grupo ES la píldora de selección: se suelta del padre y se coloca
    // donde esté el elegido.
    if (!CreateMaterial(Metrics::RadiusOf(Metrics::Radius::Control))) return false;
    return CreateLayer();
}

SidebarItem* SidebarGroup::AddItem(std::wstring glyph, std::wstring label, int count) {
    SidebarItem* item = Add<SidebarItem>(std::move(glyph), std::move(label), count);
    m_items.push_back(item);
    if (Attached()) OnArrange();
    return item;
}

int SidebarGroup::IndexOf(const SidebarItem* item) const {
    const auto found = std::find(m_items.begin(), m_items.end(), item);
    return found == m_items.end() ? -1 : static_cast<int>(found - m_items.begin());
}

void SidebarGroup::SetHeader(std::wstring text) {
    if (m_header == text) return;
    m_header = std::move(text);
    if (Attached()) {
        OnArrange();
        Invalidate();
    }
}

float SidebarGroup::HeaderHeight() const { return m_header.empty() ? 0.0f : kHeaderHeight; }

float SidebarGroup::PreferredHeight() const {
    if (m_items.empty()) return HeaderHeight();
    return HeaderHeight() + static_cast<float>(m_items.size()) * (kItemHeight + kItemGap) -
           kItemGap;
}

void SidebarGroup::OnArrange() {
    float y = HeaderHeight();
    for (SidebarItem* item : m_items) {
        item->SetFrame(Rect{0.0f, y, Frame().width, kItemHeight});
        y += kItemHeight + kItemGap;
    }
    MoveSelection(false);
}

void SidebarGroup::OnPaint(const Paint& paint, const Rect& box) {
    if (m_header.empty()) return;

    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), ink.put());
    // Alineado con el glifo de las filas, no con el borde: si el título empieza antes que
    // los iconos, la columna se ve escalonada.
    paint.text->DrawLine(paint.dc, m_header, Style::Footnote, Weight::Semibold,
                         ToBox(Rect{box.x + Metrics::kSpace1, box.y, box.width, kHeaderHeight}),
                         ink.get());
}

void SidebarGroup::MoveSelection(bool animate) {
    Gfx::Material* material = MaterialOf();
    if (!material || !Attached()) return;

    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size())) {
        material->Visual().Opacity(0.0f);
        return;
    }

    material->Visual().Opacity(1.0f);
    const Rect frame = m_items[static_cast<std::size_t>(m_selected)]->Frame();
    if (animate) {
        // Muelle estándar: es un panel moviéndose, no una interacción pequeña. Y al ser
        // muelle, cambiar de elemento a mitad de camino retoma valor y velocidad en vez
        // de saltar.
        material->AnimateBounds(HostRef().Animator(), frame.x, frame.y, frame.width,
                                frame.height, Motion::Kind::Standard);
    } else {
        material->SetBounds(frame.x, frame.y, frame.width, frame.height);
    }
}

void SidebarGroup::Select(int index, bool animate) {
    const int next = std::clamp(index, 0, static_cast<int>(m_items.size()) - 1);
    if (m_selected == next) return;

    m_selected = next;
    MoveSelection(animate);
    Invalidate();
    if (m_select) m_select(m_selected);
}

void SidebarGroup::Deselect() {
    if (m_selected < 0) return;
    m_selected = -1;
    // Sin animar la píldora: no se va a ningún sitio, se apaga. MoveSelection la esconde
    // poniéndole opacidad cero, que es lo que hace que la del otro grupo parezca la misma
    // píldora que se ha mudado.
    MoveSelection(false);
    Invalidate();
}

SidebarItem* SidebarGroup::ItemAt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return nullptr;
    return m_items[static_cast<std::size_t>(index)];
}

void SidebarGroup::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(tokens.selectionRow, HostRef().Animator(), crossfadeMs);
    }
    Element::OnTheme(tokens, crossfadeMs);
}

bool SidebarGroup::OnKey(const Input::Key& e) {
    if (!e.down) return false;
    switch (e.virtualKey) {
    case VK_DOWN:
    case 'J':
        Select(m_selected + 1, true);
        return true;
    case VK_UP:
    case 'K':
        Select(m_selected - 1, true);
        return true;
    default:
        return false;
    }
}

}  // namespace Ui
