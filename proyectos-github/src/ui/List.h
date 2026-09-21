#pragma once

// La lista virtualizada.
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
    // superficie de esa fila, que empieza en cero.
    struct RowState {
        int index = 0;
        bool hovered = false;
        bool selected = false;
    };
    using RowPainter = std::function<void(const Paint&, const Rect&, const RowState&)>;

    void SetRowPainter(RowPainter painter) { m_painter = std::move(painter); }
    void SetRowHeight(float heightDip);
    // animateEntry: los que aparecen entran con fundido y 8 px de desplazamiento,
    // escalonados 20 ms. Al recolocar por un cambio de tamaño, no.
    void SetCount(int count, bool animateEntry);
    int Count() const { return m_count; }

    void SetSelected(int index);
    int Selected() const { return m_selected; }
    void OnActivate(std::function<void(int)> handler) { m_activate = std::move(handler); }
    // Avisa después de cada tanda de reciclado. Lo usa el catálogo para enseñar cuánto
    // costó, que es la medida que decide si el criterio de los 60 fps se cumple.
    void OnRecycled(std::function<void()> handler) { m_recycled = std::move(handler); }

    // Baraja las posiciones sin cambiar el número: las filas que siguen a la vista se
    // deslizan a su sitio nuevo en vez de saltar. Es lo que hará la fase 4 cuando un repo
    // cambie de grupo tras sincronizar.
    void Reorder(std::vector<int> order);

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
        float y = 0.0f;
    };

    void OnScroll(float position);
    void Recycle(bool animateEntry);
    void PaintRow(Row& row);
    void PlaceRow(Row& row, int index, bool slide);
    float RowY(int index) const;
    int IndexAtLocal(float localY) const;
    int Slot(int index) const;  // el índice después de barajar

    winrt::Windows::UI::Composition::ContainerVisual m_content{nullptr};
    Gfx::Scroller m_scroller;
    Wheel m_wheel;

    std::vector<Row> m_rows;
    std::vector<int> m_order;  // vacío = orden natural
    RowPainter m_painter;
    std::function<void(int)> m_activate;
    std::function<void()> m_recycled;

    int m_count = 0;
    int m_selected = -1;
    int m_hovered = -1;
    float m_rowHeight = Metrics::kRowHeight;
    float m_lastRecycleMs = 0.0f;
    // Cuántas entraron en la última tanda, para escalonar solo a las nuevas.
    bool m_firstFill = true;
};

}  // namespace Ui
