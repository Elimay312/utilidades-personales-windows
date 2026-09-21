#pragma once

// La aritmética de la lista virtualizada: qué filas hay que materializar, hasta dónde
// llega el desplazamiento y cuánto mueve una muesca de rueda.
//
// Está aparte y es pura porque es justo lo que decide mal sin que se vea: una lista con
// un rango mal calculado no da error, simplemente crea quinientas superficies en vez de
// veinticinco y tira el criterio de los 60 fps sin que nada parpadee.
//
// Filas de altura fija. CLAUDE.md pide lista compacta y cuadrícula, y las dos son
// rejillas regulares; la altura variable de la fase 4, si llega, será otra función y no
// un parámetro más aquí.

namespace Ui {

// El tramo de índices que hay que tener vivos, y dónde cae el primero.
struct Slice {
    int first = 0;
    int count = 0;
    // DIP del primer elemento respecto al borde de arriba de la ventanilla. Es cero o
    // negativo: el primero casi siempre está medio salido por arriba.
    float offset = 0.0f;

    constexpr bool operator==(const Slice&) const = default;
};

// overscan son las filas de más por arriba y por abajo. Sin ellas, la que entra se ve
// aparecer en el borde en vez de venir de fuera, que es lo contrario de lo que pide
// CLAUDE.md para los elementos que entran.
Slice Visible(float scrollDip, float viewportDip, float rowDip, int itemCount, int overscan);

float ContentHeight(int itemCount, float rowDip);

// Lo máximo que se puede bajar. Cero si el contenido cabe entero: una lista corta no se
// desplaza, ni siquiera un poco.
float MaxScroll(int itemCount, float rowDip, float viewportDip);

float ClampScroll(float scrollDip, float maxScroll);

float RowTop(int index, float rowDip);

// Cuánto hay que desplazarse para que una fila entre entera, contando un margen. Cero si
// ya se ve: moverse cuando no hace falta es lo que hace que navegar con el teclado dé
// tirones.
float ScrollToShow(int index, float rowDip, float scrollDip, float viewportDip,
                   float paddingDip);

// El tirón elástico al pasarse de los topes. Amortiguado y no lineal: cuanto más se tira,
// menos cede, y por eso no hace falta ponerle un límite duro.
float RubberBand(float overshootDip, float viewportDip);

// La rueda y el panel táctil de precisión.
//
// Windows manda 120 unidades por muesca y dice cuántas líneas vale una muesca
// (SPI_GETWHEELSCROLLLINES, o -1 para "una página"). Un panel táctil de precisión manda
// deltas sueltos de 8 o de 12, y ahí está el detalle: truncar cada uno por separado los
// convierte todos en cero y el desplazamiento suave no existe. Por eso se guarda el resto.
class Wheel {
public:
    // lines < 0 significa una página por muesca, que es lo que Windows codifica con -1.
    void SetLinesPerNotch(int lines);
    int LinesPerNotch() const { return m_lines; }

    // Devuelve los DIP que hay que desplazar, acumulando lo que no llegó a una fila.
    float Take(int delta, float rowDip, float viewportDip);
    void Reset();

private:
    int m_lines = 3;
    float m_remainder = 0.0f;
};

// El escalonado de los que entran: 20 ms por elemento, con techo. Sin el techo, una lista
// de 500 tardaría diez segundos en terminar de aparecer.
float StaggerMs(int indexInBatch, float stepMs = 20.0f, float capMs = 200.0f);

}  // namespace Ui
