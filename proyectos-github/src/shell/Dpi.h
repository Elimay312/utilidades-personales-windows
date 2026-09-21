#pragma once

// Conversión entre unidades lógicas (DIP) y píxeles. Todo el layout de Brújula se
// escribe en DIP y solo se convierte en el borde: al crear superficies, que tienen que
// salir del tamaño físico exacto o el texto se emborrona, y al hablar con Win32, que
// siempre habla en píxeles.
//
// Sin cabeceras de Windows a propósito: esto vive en brujula_core y lo usan las pruebas.

#include <cmath>

namespace Dpi {

inline constexpr unsigned kDefault = 96;

inline constexpr float ScaleFor(unsigned dpi) {
    return static_cast<float>(dpi) / static_cast<float>(kDefault);
}

// Redondeo al píxel más cercano, no truncado: a 150 % una franja de 48 DIP son 72 px
// justos, pero un borde de 8,5 DIP son 12,75, y truncar acumula el error hacia arriba a
// lo largo de la ventana hasta que la última columna no cuadra.
inline int ToPixels(float dip, float scale) {
    return static_cast<int>(std::lround(dip * scale));
}

inline float ToDip(float pixels, float scale) {
    return pixels / scale;
}

// El tamaño físico de una superficie. Hacia arriba siempre: quedarse corto medio píxel
// recorta la última fila de glifos.
inline int SurfaceSide(float dip, float scale) {
    return static_cast<int>(std::ceil(dip * scale));
}

}  // namespace Dpi
