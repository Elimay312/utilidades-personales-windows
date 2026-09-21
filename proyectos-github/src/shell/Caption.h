#pragma once

// La geometría de la barra de título propia: dónde van los tres botones y qué hay bajo
// el cursor. Es aritmética pura y vive en brujula_core porque es justo lo que no se ve
// mal en pantalla hasta que ya te ha dejado una ventana que no se puede redimensionar
// por una esquina.
//
// Todo en DIP. Quien llama convierte con Dpi.h.

namespace Caption {

// 48 DIP y no los 32 de Windows: la barra lleva el título de la vista y el aviso de
// sincronización (fase 4), y con 32 el texto queda pegado al borde superior.
inline constexpr float kBarHeight = 48.0f;

// 46 DIP es el ancho de los botones de Windows 11. Se respeta aunque los dibujemos
// nosotros: el menú de ajuste que sale al pasar sobre maximizar lo coloca Windows
// contando con esa anchura, y con otra queda descentrado.
inline constexpr float kButtonWidth = 46.0f;

// Margen sensible al redimensionado. 8 DIP en vez de los 4 del sistema porque aquí no
// hay marco visible que ayude a apuntar.
inline constexpr float kResizeBorder = 8.0f;

enum class Zone {
    Client,
    Caption,
    Minimize,
    Maximize,
    Close,
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr bool Contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

struct Layout {
    float width = 0.0f;
    float height = 0.0f;
    bool maximized = false;
    Rect minimize;
    Rect maximize;
    Rect close;
};

Layout Compute(float clientWidthDip, float clientHeightDip, bool maximized);

Zone HitTest(const Layout& layout, float xDip, float yDip);

// Los tres botones, de izquierda a derecha, para dibujarlos sin repetir el orden.
inline constexpr Zone kButtons[] = {Zone::Minimize, Zone::Maximize, Zone::Close};

const Rect& ButtonRect(const Layout& layout, Zone button);

}  // namespace Caption
