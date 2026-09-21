#include "ui/List.h"

#include <Windows.h>

#include <algorithm>

#include "compositor/Paint.h"
#include "ui/Host.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Ui {

namespace {

// Filas de más por arriba y por abajo. Sin ellas, la que entra se ve aparecer en el
// borde en vez de venir de fuera.
constexpr int kOverscan = 2;
// Lo que se desplaza una fila al entrar, de CLAUDE.md.
constexpr float kEntryRise = 8.0f;

// Un cronómetro de verdad: lo que mide es si el criterio de los 60 fps se cumple, y para
// eso hay que medir, no estimar.
double NowMs() {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart == 0) return 0.0;
    return (static_cast<double>(counter.QuadPart) * 1000.0) /
           static_cast<double>(frequency.QuadPart);
}

}  // namespace

bool List::OnAttach() {
    m_content = HostRef().Compositor().CreateContainerVisual();
    // Ancho relativo, alto libre: el contenido es más alto que la ventanilla, que es de
    // lo que va todo esto.
    m_content.RelativeSizeAdjustment({1.0f, 0.0f});
    Visual().Children().InsertAtTop(m_content);

    // Recorte al propio tamaño: las filas de margen están fuera de la ventanilla y sin
    // esto se verían asomar por arriba y por abajo.
    Visual().Clip(HostRef().Compositor().CreateInsetClip());

    if (!m_scroller.Create(HostRef().Compositor(), HostRef().Animator(), m_content,
                           [this](float position) { OnScroll(position); })) {
        return false;
    }
    m_wheel.SetLinesPerNotch([] {
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        // Windows manda 0xFFFFFFFF para «una pantalla»; Ui::Wheel lo entiende como -1.
        return lines == WHEEL_PAGESCROLL ? -1 : static_cast<int>(lines);
    }());
    return true;
}

void List::SetRowHeight(float heightDip) {
    if (m_rowHeight == heightDip) return;
    m_rowHeight = std::max(heightDip, 1.0f);
    if (Attached()) OnArrange();
}

void List::SetCount(int count, bool animateEntry) {
    m_count = std::max(count, 0);
    m_order.clear();
    if (m_selected >= m_count) m_selected = m_count - 1;
    if (!Attached()) return;

    m_scroller.SetExtent(ContentHeight(m_count, m_rowHeight), Frame().height);
    Recycle(animateEntry);
}

void List::Reorder(std::vector<int> order) {
    m_order = std::move(order);
    if (!Attached()) return;
    // Sin reciclar: las filas que ya están vivas siguen valiendo y solo cambian de sitio.
    // Deslizarse es precisamente no volver a crearlas.
    for (Row& row : m_rows) {
        if (row.index < 0) continue;
        PlaceRow(row, row.index, true);
    }
}

int List::Slot(int index) const {
    if (m_order.empty() || index < 0 || index >= static_cast<int>(m_order.size())) return index;
    return m_order[static_cast<std::size_t>(index)];
}

float List::RowY(int index) const { return RowTop(Slot(index), m_rowHeight); }

void List::OnArrange() {
    if (!Attached()) return;
    m_scroller.SetExtent(ContentHeight(m_count, m_rowHeight), Frame().height);
    Recycle(false);
}

void List::OnScroll(float) { Recycle(false); }

void List::Recycle(bool animateEntry) {
    if (!Attached() || !m_painter) return;

    const double started = NowMs();

    // Cualificado: dentro de List, «Visible» a secas encuentra antes Element::Visible().
    const Slice slice = Ui::Visible(m_scroller.Position(), Frame().height, m_rowHeight, m_count,
                                    kOverscan);

    // Lo que ya no se ve queda libre para reutilizarse.
    for (Row& row : m_rows) {
        if (row.index < slice.first || row.index >= slice.first + slice.count) row.index = -1;
    }

    int entering = 0;
    for (int i = 0; i < slice.count; ++i) {
        const int index = slice.first + i;

        const bool alive = std::any_of(m_rows.begin(), m_rows.end(),
                                       [index](const Row& row) { return row.index == index; });
        if (alive) continue;

        // Una libre, o una nueva si el grupo todavía no ha crecido del todo. El grupo
        // crece hasta lo que se ve más el margen y ahí se queda para siempre.
        auto free = std::find_if(m_rows.begin(), m_rows.end(),
                                 [](const Row& row) { return row.index < 0; });
        if (free == m_rows.end()) {
            Row row;
            row.holder = HostRef().Compositor().CreateContainerVisual();
            if (!row.layer.Create(HostRef().Compositor())) continue;
            row.holder.Children().InsertAtTop(row.layer.Visual());
            m_content.Children().InsertAtTop(row.holder);
            m_rows.push_back(std::move(row));
            free = std::prev(m_rows.end());
        }

        PlaceRow(*free, index, false);
        PaintRow(*free);

        if (animateEntry) {
            // Fundido más 8 px de subida, escalonados 20 ms con techo. El retardo va en
            // la propia animación, así que el escalonado también lo lleva DWM.
            Motion::Animator& animator = HostRef().Animator();
            const float delay = StaggerMs(entering);
            free->holder.Offset({0.0f, free->y + kEntryRise, 0.0f});
            free->holder.Opacity(0.0f);
            animator.OffsetDelayed(free->holder, {0.0f, free->y, 0.0f}, Motion::Kind::Smooth,
                                   delay);
            animator.OpacityDelayed(free->holder, 1.0f,
                                    animator.FadeMs(Motion::Kind::Smooth), delay);
            ++entering;
        }
    }

    m_lastRecycleMs = static_cast<float>(NowMs() - started);
    m_firstFill = false;
    if (m_recycled) m_recycled();
}

void List::PlaceRow(Row& row, int index, bool slide) {
    row.index = index;
    row.y = RowY(index);

    const float width = std::max(Frame().width, 1.0f);
    row.layer.Place(0.0f, 0.0f, width, m_rowHeight);
    row.layer.Resize(HostRef().Device(), HostRef().Scale());

    row.holder.Size({width, m_rowHeight});
    if (slide) {
        // Muelle suave: es reordenar, que es justo para lo que está ese muelle en la
        // tabla de CLAUDE.md.
        HostRef().Animator().Offset(row.holder, {0.0f, row.y, 0.0f}, Motion::Kind::Smooth);
    } else {
        row.holder.StopAnimation(L"Offset");
        row.holder.Offset({0.0f, row.y, 0.0f});
        row.holder.StopAnimation(L"Opacity");
        row.holder.Opacity(1.0f);
    }
}

void List::PaintRow(Row& row) {
    if (!m_painter || row.index < 0) return;

    const RowState state{row.index, row.index == m_hovered, row.index == m_selected};
    Ui::Text& text = HostRef().Text();
    const Theme::Tokens& tokens = Tokens();
    const float scale = HostRef().Scale();
    const float width = std::max(Frame().width, 1.0f);
    const RowPainter& painter = m_painter;
    const float height = m_rowHeight;

    // Sin cruce: reciclar no es una transición entre dos estados, es otra fila. Y de paso
    // la superficie de atrás no llega a existir, que son la mitad de las texturas.
    row.layer.Redraw(
        HostRef().Device(),
        [&](const Gfx::Surface::Canvas& canvas) {
            Paint paint;
            paint.dc = canvas.dc;
            paint.tokens = &tokens;
            paint.text = &text;
            paint.scale = scale;
            painter(paint, Rect{0.0f, 0.0f, width, height}, state);
        },
        0.0f, HostRef().Animator());
}

void List::OnTheme(const Theme::Tokens&, float) {
    // Las filas se repintan todas: su color lo pone el pintor y no un material animable.
    for (Row& row : m_rows) PaintRow(row);
}

void List::SetSelected(int index) {
    const int next = std::clamp(index, -1, m_count - 1);
    if (m_selected == next) return;

    const int previous = m_selected;
    m_selected = next;
    for (Row& row : m_rows) {
        if (row.index == previous || row.index == m_selected) PaintRow(row);
    }
    if (m_selected >= 0) {
        m_scroller.To(ScrollToShow(Slot(m_selected), m_rowHeight, m_scroller.Position(),
                                   Frame().height, 0.0f),
                      true);
    }
}

int List::IndexAtLocal(float localY) const {
    if (m_rowHeight <= 0.0f) return -1;
    const float absolute = localY + m_scroller.Position();
    const int slot = static_cast<int>(absolute / m_rowHeight);
    if (slot < 0 || slot >= m_count) return -1;
    if (m_order.empty()) return slot;
    // Al revés que Slot: de posición en pantalla a índice.
    const auto found = std::find(m_order.begin(), m_order.end(), slot);
    return found == m_order.end() ? -1 : static_cast<int>(found - m_order.begin());
}

bool List::OnPointer(const Input::Pointer& e) {
    if (e.action == Input::Action::Wheel) {
        const float delta = m_wheel.Take(static_cast<int>(e.wheelY), m_rowHeight, Frame().height);
        // La rueda hacia delante desplaza hacia ARRIBA, y la posición crece hacia abajo.
        if (delta != 0.0f) m_scroller.By(-delta);
        return true;
    }

    if (e.action == Input::Action::Leave || e.action == Input::Action::Cancel) {
        if (m_hovered >= 0) {
            const int previous = m_hovered;
            m_hovered = -1;
            for (Row& row : m_rows) {
                if (row.index == previous) PaintRow(row);
            }
        }
        return false;
    }

    const int index = IndexAtLocal(e.y);

    if (e.action == Input::Action::Move) {
        if (index == m_hovered) return true;
        const int previous = m_hovered;
        m_hovered = index;
        // Solo las dos que cambian, no la lista entera.
        for (Row& row : m_rows) {
            if (row.index == previous || row.index == index) PaintRow(row);
        }
        return true;
    }

    if (e.action == Input::Action::Down && e.button == Input::Button::Left) {
        if (index >= 0) SetSelected(index);
        if (index >= 0 && e.clicks >= 2 && m_activate) m_activate(index);
        return true;
    }
    return e.action == Input::Action::Up;
}

bool List::OnKey(const Input::Key& e) {
    if (!e.down) return false;

    switch (e.virtualKey) {
    case VK_DOWN:
    case 'J':
        SetSelected(std::min(m_selected + 1, m_count - 1));
        return true;
    case VK_UP:
    case 'K':
        SetSelected(std::max(m_selected - 1, 0));
        return true;
    case VK_HOME:
        SetSelected(0);
        return true;
    case VK_END:
        SetSelected(m_count - 1);
        return true;
    case VK_NEXT:
        m_scroller.To(m_scroller.Position() + Frame().height - m_rowHeight, true);
        return true;
    case VK_PRIOR:
        m_scroller.To(m_scroller.Position() - Frame().height + m_rowHeight, true);
        return true;
    case VK_RETURN:
        if (m_selected >= 0 && m_activate) m_activate(m_selected);
        return true;
    default:
        return false;
    }
}

}  // namespace Ui
