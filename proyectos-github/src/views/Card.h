#pragma once

// La tarjeta de un repositorio, en sus dos disposiciones.
//
// Es una FUNCIÓN y no un elemento, y eso no es una simplificación: las filas de Ui::List
// son superficies recicladas, no árboles. Una tarjeta hecha de seis etiquetas y una píldora
// serían siete visuales por fila y, con veinticinco vivas, ciento setenta y cinco visuales
// que crear y destruir mientras se desplaza. Aquí se dibuja dentro de la superficie que la
// lista ya tiene.
//
// Las dos disposiciones son la misma tarjeta con los mismos datos en otro sitio, y están en
// el mismo archivo a propósito: separarlas es cómo una de las dos deja de enseñar el
// siguiente paso y nadie se entera hasta que abre la otra.

#include "app/State.h"
#include "ui/Element.h"

namespace Views {

enum class CardLayout { List, Grid };

// Los altos de cada disposición, y el ancho mínimo de una celda de la cuadrícula. Los usa
// quien reparte las celdas, que es Views::RepoList.
inline constexpr float kListCardHeight = 60.0f;
inline constexpr float kGridCardHeight = 118.0f;
inline constexpr float kGridCardMinWidth = 240.0f;
inline constexpr float kCardGap = Metrics::kSpace1;

void PaintCard(const Ui::Paint& paint, const Ui::Rect& box, const App::Entry& entry,
               CardLayout layout, bool hovered, bool selected);

}  // namespace Views
