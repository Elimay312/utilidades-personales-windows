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

// Lo que hay que recorrer con el botón abajo para que deje de ser un clic y pase a ser un
// arrastre. Sin este margen, cualquier clic con el pulso normal de una mano levanta la
// tarjeta, y entonces abrir el inspector se convierte en una lotería.
constexpr float kDragSlop = 4.0f;

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

// Una sola animación de opacidad sobre el contenedor del contenido, en vez de una por
// celda. Se usa cuando lo que cambia es la PANTALLA —la forma de la rejilla, o una lista
// entera por otra— y no dónde está cada tarjeta.
void List::FadeInContent() {
    if (!Attached() || !m_content) return;
    Motion::Animator& animator = HostRef().Animator();
    // El cero se escribe a mano porque el fundido retoma desde donde esté: sin esto, dos
    // cambios seguidos no tendrían nada que animar el segundo.
    m_content.StopAnimation(L"Opacity");
    m_content.Opacity(0.0f);
    animator.Opacity(m_content, 1.0f, animator.FadeMs(Motion::Kind::Standard));
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

    // **Cambiar la FORMA de la rejilla no se desliza: se funde.** Y esto deshace la
    // decisión de la fase 4, que dijo «el tamaño de una celda no se anima, y la posición
    // sí», con una razón buena —animar el tamaño reasigna la textura de cada celda en cada
    // fotograma— y una consecuencia que no se vio hasta usarlo.
    //
    // Entre lista y cuadrícula la celda pasa a un TERCIO de ancho. Con el ancho puesto ya y
    // la posición viajando, las que van a la segunda y la tercera columna cruzan por encima
    // de las de la primera, y como las tarjetas son translúcidas se leen tres textos
    // superpuestos. Medido: **1,4 segundos** de tarjetas cruzándose, y el estado final es
    // correcto pero por el camino parece que la vista se ha roto. Lo reportó quien la usa,
    // con una captura de en medio del viaje.
    //
    // Lo que cambia aquí no es dónde está CADA tarjeta: es la forma de toda la rejilla. Eso
    // se lee como un cambio de plano, no como un viaje, así que se recoloca de golpe y lo
    // que se anima es la columna entera apareciendo. Es una sola animación de opacidad
    // sobre un visual en vez de una por celda, y dura lo que dura un fundido.
    for (Row& row : m_rows) {
        if (row.index < 0) continue;
        // Sin PaintRow detrás: la celda cambia de tamaño siempre que se llega aquí —es lo
        // que hace esta función— y de repintarla se encarga ya PlaceRow.
        PlaceRow(row, row.index, false);
    }
    Recycle(false);

    if (animate) FadeInContent();
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
    // Una sincronización puede terminar mientras alguien arrastra, y entonces los índices de
    // antes ya no señalan a los mismos repositorios. El arrastre se acaba aquí y la tarjeta
    // vuelve a su sitio: soltarla después escribiría la prioridad del repositorio equivocado
    // sin dar el menor error.
    CancelDrag();

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

    int quedan = 0;
    int soltadas = 0;
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
            ++soltadas;
            continue;
        }
        PlaceRow(row, next, animate);
        PaintRow(row);
        ++quedan;
    }

    // **Si no sobrevivió NI UNA fila, esto no es gente llegando: es otra pantalla.**
    //
    // Y eso cambia cómo tiene que aparecer. El escalonado existe para que unas pocas filas
    // que llegan se lean como una secuencia; con la lista entera cambiando, lo que hace es
    // dejar la columna casi vacía mientras se rellena — medido en el modo lento de esta
    // fase: al pulsar «Dormidos», a los 130 ms había CUATRO tarjetas de diez y la columna no
    // estaba llena hasta pasados casi quinientos. Se ve como que la vista se ha quedado a
    // medias, que es justo lo que la fase 8 tenía que quitar.
    //
    // El criterio no es un número inventado sino lo que de verdad distingue los dos casos:
    // **se fueron TODAS las que había y no se quedó ninguna**. Entonces nada se ha movido,
    // no hay nada que seguir con la vista, y lo que toca es un cambio de plano — igual que
    // entre lista y cuadrícula.
    //
    // Y las dos mitades hacen falta. Sin «se fueron todas», una lista que todavía no tiene
    // ninguna fila viva —el primer arranque, o volver de una vista vacía— contaría como
    // cambio de pantalla y se fundiría por encima de su propia entrada.
    const bool pantallaNueva = quedan == 0 && soltadas > 0 && m_count > 0;
    Recycle(animate && !pantallaNueva);
    if (animate && pantallaNueva) FadeInContent();

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
    const bool slide = m_animateArrange;
    m_animateArrange = false;

    Reextend();
    // El ancho de la celda cambió con la ventanilla: hay que recolocar las vivas, no solo
    // reciclar. Sin esto, las columnas se quedan con el ancho de antes hasta que la fila
    // sale de la pantalla y vuelve.
    for (Row& row : m_rows) {
        if (row.index >= 0) PlaceRow(row, row.index, slide);
    }
    Recycle(slide);
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
    const int slot = DisplaySlot(index);
    row.x = CellX(slot);
    row.y = CellY(slot);

    if (row.leaving) {
        // Vuelve del fundido. El muelle de opacidad no se puede dejar corriendo: se la
        // llevaría a cero por debajo del contenido nuevo.
        row.leaving = false;
        row.holder.StopAnimation(L"Opacity");
        row.holder.Opacity(1.0f);
    }

    // Reservar una textura la VACÍA —Composition devuelve un hueco del atlas con los
    // píxeles de quien estuviera antes, y por eso Surface::Draw empieza por un Clear—, así
    // que una celda que cambia de tamaño hay que repintarla. Se decide AQUÍ y no en cada uno
    // de los cinco sitios que recolocan, porque dos de ellos se olvidaban: al abrir el
    // inspector la columna se estrecha, y hasta aquí eso dejaba TODAS las tarjetas en blanco
    // menos la que tuviera el ratón encima —que se repintaba por el hover—. Es el mismo
    // fallo que la fase 4 encontró en Element::SetFrame, un piso más abajo.
    const float width = CellWidth();
    const bool reserved = row.width != width || row.height != m_cellHeight;
    row.width = width;
    row.height = m_cellHeight;
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
    row.holder.IsVisible(index != m_dragIndex);
    if (reserved) PaintRow(row);
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

Rect List::RowRect(int index) const {
    if (index < 0 || index >= m_count || m_cellHeight <= 0.0f) return Rect{};

    // CellY ya lleva el relleno de arriba; lo que falta es lo desplazado. Es la misma cuenta
    // de IndexAtLocal, del derecho en vez de del revés.
    const int slot = DisplaySlot(index);
    const float localY = CellY(slot) - m_scroller.Position();
    if (localY + m_cellHeight <= 0.0f || localY >= Frame().height) return Rect{};

    const Rect box = WindowRect();
    return Rect{box.x + CellX(slot), box.y + localY, CellWidth(), m_cellHeight};
}

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

// ------------------------------------------------------------------- El arrastre --

int List::DisplaySlot(int index) const {
    if (m_dragIndex < 0 || m_dropIndex < 0) return index;
    if (index == m_dragIndex) return m_dropIndex;
    // Sacar el que viaja y volver a meterlo en el hueco. Los dos pasos por separado, porque
    // escritos como una sola cuenta con signos se equivocan justo en los bordes.
    const int without = index - (index > m_dragIndex ? 1 : 0);
    return without + (without >= m_dropIndex ? 1 : 0);
}

int List::InsertIndexAt(float localX, float localY) const {
    if (m_count <= 0 || m_cellHeight <= 0.0f) return -1;

    const float absolute = localY + m_scroller.Position() - m_padY;
    // Sin el hueco entre celdas: soltar en el aire que hay entre dos tarjetas tiene que
    // significar "aquí", que es justo lo contrario de lo que decide IndexAtLocal para un
    // clic. Un arrastre que no cae en ninguna parte no se lee como precisión, se lee como
    // que la aplicación se ha quedado colgada.
    int row = static_cast<int>(absolute / Pitch());
    if (absolute < 0.0f) row = 0;
    row = std::clamp(row, 0, std::max(RowCount() - 1, 0));

    int column = 0;
    if (m_columns > 1) {
        const float x = localX - m_padX;
        column = x <= 0.0f ? 0 : static_cast<int>(x / (CellWidth() + m_gap));
        column = std::clamp(column, 0, m_columns - 1);
    }
    return std::clamp(row * m_columns + column, 0, m_count - 1);
}

void List::UpdateDragVisibility() {
    for (Row& row : m_rows) {
        // Sin arrastre, TODAS se ven — incluidas las que se están yendo con un fundido, que
        // esconder de golpe sería el fundido que nadie llega a ver.
        if (row.holder) row.holder.IsVisible(m_dragIndex < 0 || row.index != m_dragIndex);
    }
}

void List::BeginDrag(int index) {
    // El hueco nace justo donde estaba la tarjeta, así que en este fotograma no se mueve
    // nada: lo único que cambia es que la de verdad se esconde y la copia se levanta.
    m_dragIndex = index;
    m_dropIndex = index;
    UpdateDragVisibility();
}

void List::SetDropIndex(int index) {
    if (m_dropIndex == index) return;
    m_dropIndex = index;
    // Las demás se apartan. El muelle suave es el de "reordenar y mover tarjetas entre
    // grupos" de la tabla de CLAUDE.md, y es el mismo que usa el cambio de disposición.
    for (Row& row : m_rows) {
        if (row.index >= 0) PlaceRow(row, row.index, true);
    }
}

void List::EndDrag(int to, float localX, float localY, bool inside) {
    const Drop drop{m_dragIndex, to, WindowRect().x + localX, WindowRect().y + localY, inside};

    m_dragIndex = -1;
    m_dropIndex = -1;
    m_pressIndex = -1;
    if (Attached()) HostRef().Input().Release(this);

    // Las celdas vuelven a su sitio de verdad ANTES de avisar: quien escuche va a recargar
    // la lista, y hacerlo con el hueco todavía puesto dejaría una columna corrida.
    for (Row& row : m_rows) {
        if (row.index >= 0) PlaceRow(row, row.index, true);
    }
    UpdateDragVisibility();

    if (m_dragEnd) m_dragEnd(drop);
}

void List::CancelDrag() {
    if (m_dragIndex < 0) return;
    // Sin destino: quien escucha lo entiende como "déjalo donde estaba".
    EndDrag(-1, 0.0f, 0.0f, false);
}

bool List::OnPointer(const Input::Pointer& e) {
    if (e.action == Input::Action::Wheel) {
        const float delta = m_wheel.Take(static_cast<int>(e.wheelY), Pitch(), Frame().height);
        // La rueda hacia delante desplaza hacia ARRIBA, y la posición crece hacia abajo.
        if (delta != 0.0f) m_scroller.By(-delta);
        return true;
    }

    if (e.action == Input::Action::Leave || e.action == Input::Action::Cancel) {
        // La captura se fue a otra ventana a mitad de un arrastre. Se termina aquí, y por
        // eso el aviso de fin sale igual: el que levantó la tarjeta tiene que bajarla.
        if (e.action == Input::Action::Cancel) {
            CancelDrag();
            m_pressIndex = -1;
        }
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

    const Rect local{0.0f, 0.0f, Frame().width, Frame().height};

    if (e.action == Input::Action::Move) {
        if (m_dragIndex >= 0) {
            const Rect box = WindowRect();
            if (m_dragMove) m_dragMove(box.x + e.x, box.y + e.y);
            // Fuera de la lista no se enseña hueco: la tarjeta se está yendo a otro grupo, y
            // un hueco abierto diría que va a volver a caer aquí.
            SetDropIndex(local.Contains(e.x, e.y) ? InsertIndexAt(e.x, e.y) : -1);
            return true;
        }
        if (m_pressIndex >= 0 && m_dragBegin) {
            const float dx = e.x - m_pressX;
            const float dy = e.y - m_pressY;
            if (dx * dx + dy * dy >= kDragSlop * kDragSlop) {
                const Rect box = WindowRect();
                if (m_dragBegin(m_pressIndex, box.x + e.x, box.y + e.y)) {
                    BeginDrag(m_pressIndex);
                } else {
                    // Dijo que no. No se vuelve a preguntar en cada píxel del recorrido.
                    m_pressIndex = -1;
                }
                return true;
            }
        }
        if (index == m_hovered) return true;
        const int previous = m_hovered;
        m_hovered = index;
        // Solo las dos que cambian, no la lista entera.
        for (Row& row : m_rows) {
            if (row.index == previous || row.index == index) PaintRow(row);
        }
        return true;
    }

    if (e.action == Input::Action::Down && e.button == Input::Button::Right) {
        // Elegir primero y abrir después: un menú que habla de "esta tarjeta" sin que la
        // tarjeta esté elegida no dice de cuál habla.
        if (index < 0) return true;
        MoveSelection(index);
        if (m_context) {
            const Rect box = WindowRect();
            m_context(index, box.x + e.x, box.y + e.y);
        }
        return true;
    }

    if (e.action == Input::Action::Down && e.button == Input::Button::Left) {
        // La selección sí es del PULSAR: es lo que hace que la tarjeta se ilumine bajo el
        // dedo, y es también de lo que tira el arrastre si acaba habiéndolo.
        if (index >= 0) MoveSelection(index);
        if (index >= 0 && e.clicks >= 2 && m_activate) m_activate(index);

        // Todavía no es un arrastre: es una pulsación que quizá llegue a serlo. La captura se
        // pide YA, porque el movimiento que lo decide puede caer fuera de la lista —y al
        // arrastrar hacia la barra lateral, cae fuera siempre.
        m_pressIndex = index;
        m_pressX = e.x;
        m_pressY = e.y;
        if (index >= 0 && m_dragBegin && Attached()) HostRef().Input().Capture(this);
        return true;
    }

    if (e.action == Input::Action::Up) {
        if (m_dragIndex >= 0) {
            EndDrag(m_dropIndex, e.x, e.y, local.Contains(e.x, e.y));
            return true;
        }
        // El aviso del clic es del SOLTAR desde que las tarjetas se arrastran, y tiene que
        // serlo: al pulsar todavía no se sabe si esto es un clic. Avisando al pulsar, cada
        // arrastre empezaba abriendo el inspector —que estrecha la columna y recoloca las
        // tarjetas— justo debajo de la que se estaba levantando.
        if (m_pressIndex >= 0) {
            const int pressed = m_pressIndex;
            m_pressIndex = -1;
            if (Attached()) HostRef().Input().Release(this);
            if (m_clicked) m_clicked(pressed);
        }
        return true;
    }
    return false;
}

bool List::OnKey(const Input::Key& e) {
    if (!e.down) return false;

    // Esc a mitad de arrastrar deja la tarjeta donde estaba, que es lo que Esc significa en
    // todas partes. Llega hasta aquí porque mientras se arrastra no hay ninguna capa
    // flotante abierta que se lo quede antes.
    if (e.virtualKey == VK_ESCAPE && m_dragIndex >= 0) {
        CancelDrag();
        return true;
    }

    // En una columna, arriba y abajo son el anterior y el siguiente. En cuadrícula, el de
    // encima y el de debajo; izquierda y derecha son los de al lado. Así la navegación es
    // la de lo que se ve, que es de lo que iba todo esto.
    const int step = m_columns;

    // La J y la K son LETRAS, y una letra con Control es de otro. Sin esto, Ctrl+K con la
    // lista enfocada —que es como arranca la aplicación— subía la selección y se comía el
    // atajo de la paleta de comandos antes de que Views::Main llegara a verlo: la paleta no
    // se abría nunca desde la lista. Es la misma regla que las teclas 1-4 de la vista
    // principal ya cumplían, y que Ui::Field cumple en cada una de sus seis letras.
    const bool plain = !Input::Has(e.modifiers, Input::Modifiers::Control) &&
                       !Input::Has(e.modifiers, Input::Modifiers::Alt);

    switch (e.virtualKey) {
    case 'J':
        if (!plain) return false;
        [[fallthrough]];
    case VK_DOWN:
        MoveSelection(std::min(m_selected < 0 ? 0 : m_selected + step, m_count - 1));
        return true;
    case 'K':
        if (!plain) return false;
        [[fallthrough]];
    case VK_UP:
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
