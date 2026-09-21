#pragma once

// La rejilla, los radios y las elevaciones de CLAUDE.md. No son colores y no cambian con
// el tema, así que no tienen sitio en Theme::Tokens: son constantes.
//
// Existe este archivo porque la fase 1 ya había empezado a repartirlos. Demo.cpp tiene
// seis constantes locales y tres de ellas no están en la rejilla de 4. Con ocho familias
// de componentes por delante eso deja de ser un detalle.
//
// Sin Windows ni WinRT: vive en brujula_core y lo usan las pruebas.

namespace Metrics {

// La rejilla de 4 px. Los cinco valores habituales de CLAUDE.md y ni uno más: un sexto
// sería una excepción disfrazada de token.
inline constexpr float kSpace1 = 8.0f;
inline constexpr float kSpace2 = 12.0f;
inline constexpr float kSpace3 = 16.0f;
inline constexpr float kSpace4 = 24.0f;
inline constexpr float kSpace5 = 32.0f;

enum class Radius {
    Control,  // controles pequeños
    Card,     // tarjetas
    Panel,    // paneles
    Sheet,    // hojas modales
};

constexpr float RadiusOf(Radius radius) {
    switch (radius) {
    case Radius::Control: return 6.0f;
    case Radius::Card:    return 10.0f;
    case Radius::Panel:   return 14.0f;
    case Radius::Sheet:   return 20.0f;
    }
    return 10.0f;
}

// Estos no salen de la tabla, se derivan de ella: un cuerpo de 15 con 8 de aire arriba y
// abajo son 31, y a la rejilla, 32.
inline constexpr float kControlHeight = 32.0f;
inline constexpr float kRowHeight = 36.0f;
inline constexpr float kIconSize = 16.0f;  // "iconos 16 px" de CLAUDE.md
inline constexpr float kFocusRing = 2.0f;
inline constexpr float kFocusGap = 2.0f;

// La línea más fina que sabe dibujar la pantalla. Se pide en DIP y sale en DIP, que es
// lo que hay que escribir para que salga un píxel físico. Es el `1.0f / scale` que ya
// usa el separador de la barra lateral, con nombre.
constexpr float Hairline(float scale) {
    return scale > 0.0f ? 1.0f / scale : 1.0f;
}

// Dos niveles de sombra y no más. El desenfoque va en DIP; la sombra la calcula la GPU.
struct Elevation {
    float blur = 0.0f;
    float offsetY = 0.0f;
};

inline constexpr Elevation kElevationMenu{24.0f, 8.0f};
inline constexpr Elevation kElevationSheet{48.0f, 16.0f};

}  // namespace Metrics

namespace Ui {

// El rectángulo de layout, en DIP. Aparte de Caption::Rect a propósito: aquel describe
// la geometría de la barra de título y se prueba contra ella; este lo usan los elementos
// del kit y necesita operaciones que allí no pintan nada.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr float Right() const { return x + width; }
    constexpr float Bottom() const { return y + height; }

    // Medio abierto por la derecha y por abajo: dos rectángulos pegados no pueden
    // contener los dos el punto de la junta, o el hit-test devuelve el de abajo.
    constexpr bool Contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    constexpr Rect Inset(float d) const {
        return Rect{x + d, y + d, width - d - d, height - d - d};
    }

    constexpr Rect Moved(float dx, float dy) const { return Rect{x + dx, y + dy, width, height}; }

    // Composition rechaza una superficie de lado 0, y un panel estrujado puede pedirlo.
    constexpr Rect AtLeast(float minimum) const {
        return Rect{x, y, width > minimum ? width : minimum, height > minimum ? height : minimum};
    }

    constexpr bool Empty() const { return width <= 0.0f || height <= 0.0f; }

    constexpr bool operator==(const Rect&) const = default;
};

}  // namespace Ui
