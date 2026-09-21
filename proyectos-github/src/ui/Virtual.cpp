#include "ui/Virtual.h"

#include <algorithm>
#include <cmath>

namespace Ui {

namespace {

// Una fila de alto cero daría una división por cero y un rango infinito. No debería
// llegar nunca, pero el precio de comprobarlo es una comparación y el de no hacerlo es
// colgar la aplicación al pintar.
constexpr float kMinRow = 1.0f;

}  // namespace

float ContentHeight(int itemCount, float rowDip) {
    if (itemCount <= 0 || rowDip <= 0.0f) return 0.0f;
    return static_cast<float>(itemCount) * rowDip;
}

float MaxScroll(int itemCount, float rowDip, float viewportDip) {
    return std::max(ContentHeight(itemCount, rowDip) - viewportDip, 0.0f);
}

float ClampScroll(float scrollDip, float maxScroll) {
    return std::clamp(scrollDip, 0.0f, std::max(maxScroll, 0.0f));
}

float RowTop(int index, float rowDip) { return static_cast<float>(index) * rowDip; }

Slice Visible(float scrollDip, float viewportDip, float rowDip, int itemCount, int overscan) {
    Slice slice;
    if (itemCount <= 0 || viewportDip <= 0.0f) return slice;

    const float row = std::max(rowDip, kMinRow);
    const int margin = std::max(overscan, 0);

    // El suelo y no el redondeo: con un desplazamiento de 1,9 alturas de fila, la primera
    // que se ve es la 1 —asomando— y no la 2.
    const float top = std::max(scrollDip, 0.0f);
    int first = static_cast<int>(std::floor(top / row)) - margin;
    first = std::max(first, 0);

    // Y el techo por abajo, más una: la última puede estar entrando por el borde.
    const int lastVisible = static_cast<int>(std::floor((top + viewportDip) / row));
    int last = std::min(lastVisible + margin, itemCount - 1);

    if (last < first) return slice;

    slice.first = first;
    slice.count = last - first + 1;
    // Negativo o cero: el primero de la tanda casi siempre está medio salido por arriba.
    slice.offset = RowTop(first, row) - top;
    return slice;
}

float ScrollToShow(int index, float rowDip, float scrollDip, float viewportDip,
                   float paddingDip) {
    if (index < 0 || viewportDip <= 0.0f) return scrollDip;

    const float row = std::max(rowDip, kMinRow);
    const float top = RowTop(index, row) - paddingDip;
    const float bottom = RowTop(index, row) + row + paddingDip;

    // Ya se ve entera: no moverse. Desplazar cuando no hace falta es lo que hace que
    // bajar por la lista con las flechas dé tirones en vez de deslizarse.
    if (top >= scrollDip && bottom <= scrollDip + viewportDip) return scrollDip;

    // Por arriba se alinea arriba; por abajo, abajo. Es lo mínimo que hace falta, y lo
    // mínimo es lo que se lee como "ha seguido al cursor" en vez de "ha saltado".
    if (top < scrollDip) return std::max(top, 0.0f);
    return bottom - viewportDip;
}

float RubberBand(float overshootDip, float viewportDip) {
    if (overshootDip == 0.0f || viewportDip <= 0.0f) return 0.0f;

    // La curva de iOS: c*v*d / (c*v + d). Al principio sigue al dedo uno a uno —su
    // derivada en cero vale 1— y tiende a c*v por mucho que se tire, así que el tope
    // blando sale solo y no hace falta ponerle un límite aparte.
    const float sign = overshootDip < 0.0f ? -1.0f : 1.0f;
    const float d = std::fabs(overshootDip);
    constexpr float kResistance = 0.55f;
    const float limit = kResistance * viewportDip;
    return sign * (limit * d) / (limit + d);
}

void Wheel::SetLinesPerNotch(int lines) {
    // Windows codifica "una página" con -1. Cero no significa nada y llega a veces; se
    // trata como el valor de fábrica en vez de dejar la rueda muerta.
    m_lines = lines == 0 ? 3 : lines;
}

float Wheel::Take(int delta, float rowDip, float viewportDip) {
    if (delta == 0) return 0.0f;

    const float row = std::max(rowDip, kMinRow);
    // Una página deja una fila de solape, que es lo que permite seguir leyendo sin
    // perder el hilo entre una página y la siguiente.
    const float perNotch =
        m_lines < 0 ? std::max(viewportDip - row, row) : static_cast<float>(m_lines) * row;

    constexpr float kNotch = 120.0f;
    const float wanted = (static_cast<float>(delta) / kNotch) * perNotch + m_remainder;

    // Hasta el DIP entero, y el resto se guarda: un panel táctil de precisión manda
    // deltas de 8 unidades, que son menos de un DIP, y truncarlos uno a uno los convierte
    // todos en cero.
    const float whole = std::trunc(wanted);
    m_remainder = wanted - whole;
    return whole;
}

void Wheel::Reset() { m_remainder = 0.0f; }

float StaggerMs(int indexInBatch, float stepMs, float capMs) {
    if (indexInBatch <= 0) return 0.0f;
    return std::min(static_cast<float>(indexInBatch) * stepMs, capMs);
}

}  // namespace Ui
