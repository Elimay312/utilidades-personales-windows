#pragma once

// Los colores de Theme.h en los dos formatos que piden las dos bibliotecas que dibujan.
// Está aquí y no en Theme.h para que Theme siga sin saber nada de Windows y las pruebas
// puedan enlazarlo solo.

#include <d2d1_1.h>
#include "compositor/Winrt.h"

#include "shell/Theme.h"

namespace Gfx {

inline winrt::Windows::UI::Color ToUi(Theme::Color color) {
    return winrt::Windows::UI::Color{color.a, color.r, color.g, color.b};
}

// Direct2D trabaja con alfa recto; la premultiplicación de la superficie la hace él.
inline D2D1_COLOR_F ToD2D(Theme::Color color, float opacity = 1.0f) {
    return D2D1::ColorF(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                        (color.a / 255.0f) * opacity);
}

}  // namespace Gfx
