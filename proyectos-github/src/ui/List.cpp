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

// ------------------------------------------------------------------- La geometría --

float List::CellWidth() const {
    const float inner = Frame().width - m_padX * 2.0f - m_gap * static_cast<float>(m_columns - 1);
    return std::max(inner / static_cast<float>(std::max(m_columns, 1)), 1.0f);
}

float List::CellX(int index) const {
    const int column = m_columns > 1 ? index % m_columns : 0;
    return m_padX + static_cast<float>(column) * (CellWidth() + m_gap);
}

float List::CellY(int index) const {
    const int row = m_columns > 1 ? index / m_columns : index;
    return m_padY + RowTop(row, Pitch());
}

float List::ContentDip() const {
    const int rows = RowCount();
    if (rows <= 0) return 0.0f;
    // El hueco va ENTRE celdas, así que el último no cuenta; el aire de abajo lo pone el
    // relleno. Sumarlos los dos dejaría un hueco de más al final que se ve al llegar al
    // tope y parece que falta una fila.
    return m_padY * 2.0f + ContentHeight(rows, Pitch()) - m_gap;
}

void List::Reextend() {
    if (!Attached()) return;
    m_scroller.SetExtent(ContentDip(), Frame().height);
}

void List::SetRowHeight(float heightDip) { SetLayout(1, heightDip, 0.0f, false); }

void List::SetLayout(int columns, float cellHeightDip, float gapDip, bool animate) {
    const int cols = std::max(columns, 1);
    const float height = std::max(cellHeightDip, 1.0f);
    const float gap = std::max(gapDip, 0.0f);
    if (m_columns == cols && m_cellHeight == height && m_gap == gap) return;

    m_columns = cols;
    m_cellHeight = height;
    m_gap = gap;
    if (!Attached()) return;

    Reextend();
    // Las que ya están se deslizan a su celda nueva. El TAMAÑO no se anima y es a
    // propósito: animarlo reasignaría la textura de cada celda en cada fotograma, que es
    // justo lo que la fase 1 midió que no había que hacer. Cambia de golpe y se mueve con
    // muelle, que es lo que se lee como "la rejilla ha fluido".
    for (Row& row : m_rows) {
        if (row.index < 0) continue;
        PlaceRow(row, row.index, animate);
        PaintRow(row);
    }
    Recycle(animate);
}

void List::SetPadding(float horizontalDip, float verticalDip) {
    if (m_padX == horizontalDip && m_padY == verticalDip) return;
    m_padX = std::max(horizontalDip, 0.0f);
    m_padY = std::max(verticalDip, 0.0f);
    if (!Attached()) return;
    Reextend();
    for (Row& row : m_rows) {
        if (row.index >= 0) PlaceRow(row, row.index, false);
    }
    Recycle(false);
}

// ------------------------------------------------------------------- El contenido --

int List::IndexOfKey(std::uint64_t key) const {
    if (key == 0) return -1;
    const auto found = std::find(m_keys.begin(), m_keys.end(), key);
    return found == m_keys.end() ? -1 : static_cast<int>(found - m_keys.begin());
}

void List::Update(std::vector<std::uint64_t> keys, bool animate) {
    std::vector<std::uint64_t> previous;
    previous.swap(m_keys);
    m_keys = std::move(keys);
    m_count = static_cast<int>(m_keys.size());

    // La selección sigue al repositorio, no a su número. Si el que estaba elegido sigue
    // ahí pero en otro sitio, se queda elegido; si se fue, no se hereda la selección de
    // quien haya caído en su número, que sería elegir algo que nadie eligió.
    int selected = -1;
    if (m_selected >= 0 && m_selected < static_cast<int>(previous.size())) {
        selected = IndexOfKey(previous[static_cast<std::size_t>(m_selected)]);
    }
    const bool selectionMoved = selected != m_selected;
    m_selected = selected;

    if (!Attached()) return;
    Reextend();

    const int rows = RowCount();
    const Slice slice = Ui::Visible(m_scroller.Position() - m_padY, Frame().height, Pitch(),
                                    rows, kOverscan);
    const int firstItem = slice.first * m_columns;
    const int lastItem = std::min((slice.first + slice.count) * m_columns, m_count) - 1;

    for (Row& row : m_rows) {
        if (row.index < 0) continue;
        const std::uint64_t key = row.index < static_cast<int>(previous.size())
                                      ? previous[static_cast<std::size_t>(row.index)]
                                      : 0;
        const int next = IndexOfKey(key);
        // Se fue, o se fue tan lejos que su celda nueva ya no se ve. Deslizarla hasta una
        // posición fuera de la pantalla sería una animación que nadie mira y una
        // superficie retenida mientras dura.
        if (next < 0 || next < firstItem || next > lastItem) {
            Release(row, animate);
            continue;
        }
        PlaceRow(row, next, animate);
        PaintRow(row);
    }

    Recycle(animate);
    if (selectionMoved && m_selectionChanged) m_selectionChanged(m_selected);
}

void List::SetCount(int count, bool animateEntry) {
    // Claves sintéticas: el elemento 0 es «el 0», y dos tandas del mismo tamaño son los
    // mismos elementos. Es lo que quiere quien pinta en función del número y no de un dato.
    // Empiezan en 1 porque el cero está reservado para «esta fila no tiene clave».
    std::vector<std::uint64_t> keys(static_cast<std::size_t>(std::max(count, 0)));
    for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = i + 1;
    Update(std::move(keys), animateEntry);
}

void List::Refresh() {
    for (Row& row : m_rows) {
        if (row.index >= 0) PaintRow(row);
    }
}

void List::OnArrange() {
    if (!Attached()) return;
    Reextend();
    // El ancho de la celda cambió con la ventanilla: hay que recolocar las vivas, no solo
    // reciclar. Sin esto, las columnas se quedan con el ancho de antes hasta que la fila
    // sale de la pantalla y vuelve.
    for (Row& row : m_rows) {
        if (row.index >= 0) PlaceRow(row, row.index, false);
    }
    Recycle(false);
}

void List::OnScroll(float) { Recycle(false); }

List::Row* List::FreeRow() {
    // Primero una que no se esté yendo. Reutilizar la que acaba de empezar a desvanecerse
    // sería no enseñar nunca ese fundido.
    auto free = std::find_if(m_rows.begin(), m_rows.end(),
                             [](const Row& row) { return row.index < 0 && !row.leaving; });
    if (free == m_rows.end()) {
        free = std::find_if(m_rows.begin(), m_rows.end(),
                            [](const Row& row) { return row.index < 0; });
    }
    if (free != m_rows.end()) return &*free;

    // Ninguna libre: el grupo crece hasta lo que se ve más el margen y ahí se queda para
    // siempre.
    Row row;
    row.holder = HostRef().Compositor().CreateContainerVisual();
    if (!row.layer.Create(HostRef().Compositor())) return nullptr;
    row.holder.Children().InsertAtTop(row.layer.Visual());
    m_content.Children().InsertAtTop(row.holder);
    m_rows.push_back(std::move(row));
    return &m_rows.back();
}

void List::Release(Row& row, bool fade) {
    row.index = -1;
    row.leaving = true;
    if (!row.holder) return;
    if (fade) {
        HostRef().Animator().Opacity(row.holder, 0.0f,
                                     HostRef().Animator().FadeMs(Motion::Kind::Standard));
    } else {
        row.holder.StopAnimation(L"Opacity");
        row.holder.Opacity(0.0f);
    }
}

void List::Recycle(bool animateEntry) {
    if (!Attached() || !m_painter) return;

    const double started = NowMs();

    // Cualificado: dentro de List, «Visible» a secas encuentra antes Element::Visible().
    const Slice slice = Ui::Visible(m_scroller.Position() - m_padY, Frame().height, Pitch(),
                                    RowCount(), kOverscan);
    const int firstItem = slice.first * m_columns;
    const int lastItem = std::min((slice.first + slice.count) * m_columns, m_count) - 1;

    // Lo que ya no se ve queda libre para reutilizarse.
    for (Row& row : m_rows) {
        if (row.index >= 0 && (row.index < firstItem || row.index > lastItem)) {
            Release(row, false);
        }
    }

    int entering = 0;
    for (int index = firstItem; index <= lastItem; ++index) {
        const bool alive = std::any_of(m_rows.begin(), m_rows.end(),
                                       [index](const Row& row) { return row.index == index; });
        if (alive) continue;

        Row* free = FreeRow();
        if (free == nullptr) continue;

        PlaceRow(*free, index, false);
        PaintRow(*free);

        if (animateEntry) {
            // Fundido más 8 px de subida, escalonados 20 ms con techo. El retardo va en
            // la propia animación, así que el escalonado también lo lleva DWM.
            Motion::Animator& animator = HostRef().Animator();
            const float delay = StaggerMs(entering);
            free->holder.Offset({free->x, free->y + kEntryRise, 0.0f});
            free->holder.Opacity(0.0f);
            animator.OffsetDelayed(free->holder, {free->x, free->y, 0.0f}, Motion::Kind::Smooth,
                                   delay);
            animator.OpacityDelayed(free->holder, 1.0f,
                                    animator.FadeMs(Motion::Kind::Smooth), delay);
            ++entering;
        }
    }

    m_lastRecycleMs = static_cast<float>(NowMs() - started);
    if (m_recycled) m_recycled();
}

void List::PlaceRow(Row& row, int index, bool slide) {
    row.index = index;
    row.x = CellX(index);
    row.y = CellY(index);

    if (row.leaving) {
        // Vuelve del fundido. El muelle de opacidad no se puede dejar corriendo: se la
        // llevaría a cero por debajo del contenido nuevo.
        row.leaving = false;
        row.holder.StopAnimation(L"Opacity");
        row.holder.Opacity(1.0f);
    }

    const float width = CellWidth();
    row.layer.Place(0.0f, 0.0f, width, m_cellHeight);
    row.layer.Resize(HostRef().Device(), HostRef().Scale());
    row.holder.Size({width, m_cellHeight});

    if (slide) {
        // Muelle suave: es reordenar, que es justo para lo que está ese muelle en la
        // tabla de CLAUDE.md.
        HostRef().Animator().Offset(row.holder, {row.x, row.y, 0.0f}, Motion::Kind::Smooth);
    } else {
        row.holder.StopAnimation(L"Offset");
        row.holder.Offset({row.x, row.y, 0.0f});
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
    const float width = CellWidth();
    const RowPainter& painter = m_painter;
    const float height = m_cellHeight;

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
    Refresh();
}

// ------------------------------------------------------------------- La selección --

void List::MoveSelection(int index) {
    const int next = std::clamp(index, -1, m_count - 1);
    if (m_selected == next) return;

    const int previous = m_selected;
    m_selected = next;
    for (Row& row : m_rows) {
        if (row.index == previous || row.index == m_selected) PaintRow(row);
    }
    if (m_selected >= 0) {
        // En unidades de FILA, no de elemento: en una cuadrícula, el elemento 7 y el 8
        // pueden estar en la misma fila y bajar hasta el 8 no tiene que mover nada.
        const int row = m_columns > 1 ? m_selected / m_columns : m_selected;
        const float wanted =
            ScrollToShow(row, Pitch(), m_scroller.Position() - m_padY, Frame().height, m_gap);
        // ScrollToShow no sabe del relleno y topa en cero. Cero ahí significa "pegado a la
        // primera fila", y lo que hay que enseñar entonces es el aire de arriba también:
        // sin esto, subir a la primera tarjeta la deja tocando el borde.
        m_scroller.To(wanted <= 0.0f ? 0.0f : wanted + m_padY, true);
    }
    if (m_selectionChanged) m_selectionChanged(m_selected);
}

void List::SetSelected(int index) { MoveSelection(index); }

int List::IndexAtLocal(float localX, float localY) const {
    if (m_cellHeight <= 0.0f || m_count <= 0) return -1;

    const float absolute = localY + m_scroller.Position() - m_padY;
    if (absolute < 0.0f) return -1;
    const int row = static_cast<int>(absolute / Pitch());
    // En el hueco entre dos filas no hay nadie. Devolver la de arriba haría que pulsar
    // justo en el aire entre dos tarjetas eligiera una de ellas sin haberla tocado.
    if (absolute - static_cast<float>(row) * Pitch() > m_cellHeight) return -1;

    int column = 0;
    if (m_columns > 1) {
        const float width = CellWidth();
        const float x = localX - m_padX;
        if (x < 0.0f) return -1;
        column = static_cast<int>(x / (width + m_gap));
        if (column >= m_columns) return -1;
        if (x - static_cast<float>(column) * (width + m_gap) > width) return -1;
    }

    const int index = row * m_columns + column;
    return index >= 0 && index < m_count ? index : -1;
}

bool List::OnPointer(const Input::Pointer& e) {
    if (e.action == Input::Action::Wheel) {
        const float delta = m_wheel.Take(static_cast<int>(e.wheelY), Pitch(), Frame().height);
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

    const int index = IndexAtLocal(e.x, e.y);

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
        if (index >= 0) MoveSelection(index);
        if (index >= 0 && e.clicks >= 2 && m_activate) m_activate(index);
        return true;
    }
    return e.action == Input::Action::Up;
}

bool List::OnKey(const Input::Key& e) {
    if (!e.down) return false;

    // En una columna, arriba y abajo son el anterior y el siguiente. En cuadrícula, el de
    // encima y el de debajo; izquierda y derecha son los de al lado. Así la navegación es
    // la de lo que se ve, que es de lo que iba todo esto.
    const int step = m_columns;

    switch (e.virtualKey) {
    case VK_DOWN:
    case 'J':
        MoveSelection(std::min(m_selected < 0 ? 0 : m_selected + step, m_count - 1));
        return true;
    case VK_UP:
    case 'K':
        MoveSelection(std::max(m_selected <= 0 ? 0 : m_selected - step, 0));
        return true;
    case VK_LEFT:
        if (m_columns <= 1) return false;
        MoveSelection(std::max(m_selected <= 0 ? 0 : m_selected - 1, 0));
        return true;
    case VK_RIGHT:
        if (m_columns <= 1) return false;
        MoveSelection(std::min(m_selected < 0 ? 0 : m_selected + 1, m_count - 1));
        return true;
    case VK_HOME:
        MoveSelection(0);
        return true;
    case VK_END:
        MoveSelection(m_count - 1);
        return true;
    case VK_NEXT:
        m_scroller.To(m_scroller.Position() + Frame().height - Pitch(), true);
        return true;
    case VK_PRIOR:
        m_scroller.To(m_scroller.Position() - Frame().height + Pitch(), true);
        return true;
    case VK_RETURN:
        if (m_selected >= 0 && m_activate) m_activate(m_selected);
        return true;
    default:
        return false;
    }
}

}  // namespace Ui
