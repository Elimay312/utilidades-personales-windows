#pragma once

// La lista virtualizada, en una columna o en varias.
//
// Las filas NO son Ui::Element. Son un grupo de superficies que se reciclan, y es la
// decisión que hace que el criterio de los 60 fps con 500 elementos se pueda cumplir: un
// Gfx::Layer son dos superficies de Direct2D, y quinientas filas serían mil texturas —la
// fase 1 midió que cincuenta repintados dejaban 105 MB—. Vivas hay unas quince, las que
// se ven más un margen, y al desplazarse las que salen por arriba se reutilizan para las
// que entran por abajo.
//
// El movimiento no lo hace el hilo de UI. El contenido va atado al InteractionTracker con
// una expresión, así que desplazarse es trabajo de DWM; este hilo solo se despierta para
// repintar la fila o dos que se reciclan en cada fotograma. Cuánto tarda en hacerlo está
// medido y se puede leer: es lo que decide si el criterio se cumple.
//
// **La lista se actualiza por CLAVE, no por número.** Update() recibe quiénes hay ahora, y
// cada fila viva busca dónde ha ido a parar el suyo: si sigue, se desliza; si no, se
// desvanece. Es lo que pide la fase 4 en dos sitios —filtrar con animación y deslizar los
// repositorios que cambian de grupo tras sincronizar— y es lo que no se puede hacer con un
// contador, porque dos listas de 40 elementos no dicen nada de si son los mismos 40.
//
// Y por eso el índice de la lista SIEMPRE es el de la pantalla. La alternativa —mantener
// el orden de los datos y barajar solo las posiciones— parece más barata hasta que se mira
// el teclado: con las posiciones barajadas, «el siguiente» de la flecha abajo es el
// siguiente del array y no el de debajo, y la selección va dando saltos por la lista.

#include <cstdint>
#include <functional>
#include <vector>

#include "compositor/Scroller.h"
#include "ui/Element.h"
#include "ui/Virtual.h"

namespace Ui {

class List : public Element {
public:
    // Lo que se le da para pintar una fila. El rectángulo ya viene en coordenadas de la
    // superficie de esa celda, que empieza en cero.
    struct RowState {
        int index = 0;
        bool hovered = false;
        bool selected = false;
    };
    using RowPainter = std::function<void(const Paint&, const Rect&, const RowState&)>;

    void SetRowPainter(RowPainter painter) { m_painter = std::move(painter); }

    // Una columna y sin hueco: la lista compacta de siempre.
    void SetRowHeight(float heightDip);
    // La disposición entera. columns a 1 es lista; más, cuadrícula. 'gap' es el hueco
    // entre celdas por los dos ejes, y 'animate' hace que las que ya están se deslicen a
    // su celda nueva en vez de aparecer allí.
    void SetLayout(int columns, float cellHeightDip, float gapDip, bool animate);
    // El aire alrededor de las celdas, dentro de la ventanilla.
    void SetPadding(float horizontalDip, float verticalDip);

    int Columns() const { return m_columns; }
    float CellHeight() const { return m_cellHeight; }
    float CellWidth() const;

    // Quiénes hay ahora, en el orden en que se enseñan. Una clave repetida entre dos
    // tandas es el mismo elemento; una que desaparece se va y una nueva entra.
    void Update(std::vector<std::uint64_t> keys, bool animate);
    // Sin identidad: los elementos son «el 0, el 1, el 2…». Lo usa el catálogo, donde el
    // contenido de una fila es función de su número y no hay nada que seguir.
    void SetCount(int count, bool animateEntry);
    int Count() const { return m_count; }

    // La PRÓXIMA recolocación se anima: las celdas se deslizan a su celda nueva en vez de
    // aparecer ya en ella. De un solo uso y a propósito — recolocar es casi siempre la
    // realidad nueva, como al cambiar el tamaño de la ventana, y solo a veces una
    // transición, como cuando el inspector estrecha la columna.
    //
    // El TAMAÑO sigue sin animarse: hacerlo reasignaría la textura de cada celda en cada
    // fotograma. Cambia de golpe y se mueve con muelle, que es la misma decisión que la
    // fase 4 tomó para el paso de lista a cuadrícula.
    void AnimateNextArrange() { m_animateArrange = true; }

    // Repinta las filas vivas sin moverlas. El contenido cambió —una selección, un tema—
    // pero no quiénes son ni dónde están.
    void Refresh();

    void SetSelected(int index);
    int Selected() const { return m_selected; }

    // Dónde está una celda en coordenadas de VENTANA, o un rectángulo vacío si esa celda no
    // se ve ahora mismo. De aquí sale el punto de partida de la transición compartida: la
    // tarjeta que se convierte en el inspector. Vacío no es un fallo — una tarjeta que se
    // desplazó fuera de la ventanilla no es de donde sale nada, y quien llama cierra con un
    // fundido en vez de con un viaje desde ninguna parte.
    Rect RowRect(int index) const;

    // Para que un ancestro pueda mandarle las teclas de navegación cuando el foco está en
    // otra parte —el campo de búsqueda, por ejemplo— sin duplicar lo que la lista ya sabe
    // hacer. Duplicarlo sería tener dos ideas de qué significa "el siguiente".
    bool Navigate(const Input::Key& e) { return OnKey(e); }
    void OnActivate(std::function<void(int)> handler) { m_activate = std::move(handler); }
    // Un clic sobre una celda, sea o no la que ya estaba elegida. Es distinto de
    // OnSelectionChanged —que también salta con las flechas— y de OnActivate —que es Enter y
    // el doble clic—, y hace falta para «Enter o clic abre el inspector» sin que moverse con
    // las flechas lo abra también.
    void OnClicked(std::function<void(int)> handler) { m_clicked = std::move(handler); }
    void OnSelectionChanged(std::function<void(int)> handler) {
        m_selectionChanged = std::move(handler);
    }
    // Avisa después de cada tanda de reciclado. Lo usa el catálogo para enseñar cuánto
    // costó, que es la medida que decide si el criterio de los 60 fps se cumple.
    void OnRecycled(std::function<void()> handler) { m_recycled = std::move(handler); }

    // --- Arrastrar ----------------------------------------------------------------------
    //
    // La lista no sabe qué significa soltar. Sabe de celdas: quién se está llevando, dónde
    // caería y cuándo se soltó, todo en coordenadas de VENTANA para quien tenga que mirar
    // fuera de ella —la barra lateral, sin ir más lejos—. Lo que significa cada cosa lo
    // decide quien escucha, que es la regla 2 de arquitectura otra vez.
    //
    // El hueco sí es suyo, porque es su maquetación: mientras se arrastra, las demás celdas
    // se colocan como si la que viaja ya no estuviera, y se deslizan con el muelle suave —el
    // de reordenar de la tabla de CLAUDE.md— hasta el sitio que les toca.
    struct Drop {
        int from = -1;
        // Dónde caería dentro de la lista, o -1 si se soltó fuera de ella.
        int to = -1;
        float x = 0.0f;  // en coordenadas de ventana
        float y = 0.0f;
        bool inside = false;
    };

    // Devuelve false para negarse: una lista que no se reordena no engancha esto, y una que
    // sí puede decir que esa celda concreta no se mueve.
    void OnDragBegin(std::function<bool(int index, float x, float y)> handler) {
        m_dragBegin = std::move(handler);
    }
    void OnDragMove(std::function<void(float x, float y)> handler) {
        m_dragMove = std::move(handler);
    }
    // Se llama SIEMPRE que un arrastre termina, incluso cuando termina mal: soltar fuera,
    // Esc, o que otra ventana se lleve la captura. Es lo que hace imposible que quede una
    // tarjeta levantada sin nadie que la baje.
    void OnDragEnd(std::function<void(const Drop&)> handler) { m_dragEnd = std::move(handler); }
    // El botón derecho sobre una celda, con el punto donde abrir el menú.
    void OnContextMenu(std::function<void(int index, float x, float y)> handler) {
        m_context = std::move(handler);
    }

    bool Dragging() const { return m_dragIndex >= 0; }
    // Lo deja donde estaba. La usa Esc, y el aviso de fin sale igual.
    void CancelDrag();

    // Lo que hace falta para juzgar el criterio: cuánto costó la última tanda de
    // reciclado y cuántas superficies hay vivas. Muy por debajo de 16,6 ms con 500
    // elementos es la prueba de que el desplazamiento no puede tirar fotogramas.
    float LastRecycleMs() const { return m_lastRecycleMs; }
    int LiveRows() const { return static_cast<int>(m_rows.size()); }
    float ScrollPosition() const { return m_scroller.Position(); }

    bool Focusable() const override { return true; }
    bool ClipsInput() const override { return true; }

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;
    bool OnPointer(const Input::Pointer& e) override;
    bool OnKey(const Input::Key& e) override;

private:
    struct Row {
        winrt::Windows::UI::Composition::ContainerVisual holder{nullptr};
        Gfx::Layer layer;
        int index = -1;
        // Se está yendo: libre para reutilizar, pero la última en la cola. Sin esto, la
        // que acaba de desvanecerse es la primera candidata y el fundido no se ve nunca.
        bool leaving = false;
        float x = 0.0f;
        float y = 0.0f;
        // A qué tamaño está reservada su textura. Es lo que decide si hay que repintarla al
        // recolocarla: reservar la VACÍA, y una celda que cambia de ancho sin repintarse se
        // queda en blanco hasta que algo la toque (ver PlaceRow).
        float width = 0.0f;
        float height = 0.0f;
    };

    void OnScroll(float position);
    void Recycle(bool animateEntry);
    void PaintRow(Row& row);
    void PlaceRow(Row& row, int index, bool slide);
    void Release(Row& row, bool fade);
    Row* FreeRow();

    // Dónde se DIBUJA el elemento 'index'. Sin arrastre es él mismo; con arrastre, el que
    // viaja va al hueco y los de en medio se corren uno. Es la única cuenta de todo esto:
    // el resto de la lista sigue hablando de índices y no se entera.
    int DisplaySlot(int index) const;
    // Entre qué dos caería un punto. A diferencia de IndexAtLocal, el hueco entre celdas no
    // es tierra de nadie: soltar ahí tiene que significar algo.
    int InsertIndexAt(float localX, float localY) const;
    void BeginDrag(int index);
    void SetDropIndex(int index);
    void EndDrag(int to, float localX, float localY, bool inside);
    // Esconde la que se está llevando el usuario: lo que se ve viajar es la copia que
    // levanta quien escucha, y las dos a la vez serían dos tarjetas iguales.
    void UpdateDragVisibility();

    float Pitch() const { return m_cellHeight + m_gap; }
    int RowCount() const { return RowsFor(m_count, m_columns); }
    float ContentDip() const;
    float CellX(int index) const;
    float CellY(int index) const;
    int IndexAtLocal(float localX, float localY) const;
    int IndexOfKey(std::uint64_t key) const;
    void Reextend();
    void MoveSelection(int index);

    winrt::Windows::UI::Composition::ContainerVisual m_content{nullptr};
    Gfx::Scroller m_scroller;
    Wheel m_wheel;

    std::vector<Row> m_rows;
    std::vector<std::uint64_t> m_keys;
    RowPainter m_painter;
    std::function<void(int)> m_activate;
    std::function<void(int)> m_clicked;
    std::function<void(int)> m_selectionChanged;
    std::function<void()> m_recycled;
    std::function<bool(int, float, float)> m_dragBegin;
    std::function<void(float, float)> m_dragMove;
    std::function<void(const Drop&)> m_dragEnd;
    std::function<void(int, float, float)> m_context;

    int m_count = 0;
    int m_selected = -1;
    int m_hovered = -1;
    // Sobre cuál se pulsó y dónde, mientras el botón sigue abajo. Es lo que separa un clic
    // de un arrastre: hasta que no se recorren unos pocos DIP, esto es un clic.
    int m_pressIndex = -1;
    float m_pressX = 0.0f;
    float m_pressY = 0.0f;
    // El que viaja, y dónde caería. Los dos a -1 cuando no se está arrastrando.
    int m_dragIndex = -1;
    int m_dropIndex = -1;
    int m_columns = 1;
    float m_cellHeight = Metrics::kRowHeight;
    float m_gap = 0.0f;
    float m_padX = 0.0f;
    float m_padY = 0.0f;
    float m_lastRecycleMs = 0.0f;
    bool m_animateArrange = false;
};

}  // namespace Ui
