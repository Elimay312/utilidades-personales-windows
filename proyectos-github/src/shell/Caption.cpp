#include "shell/Caption.h"

namespace Caption {

Layout Compute(float clientWidthDip, float clientHeightDip, bool maximized) {
    Layout layout;
    layout.width = clientWidthDip;
    layout.height = clientHeightDip;
    layout.maximized = maximized;

    // Pegados a la derecha y en el orden de Windows: minimizar, maximizar, cerrar. Los
    // botones ocupan la franja entera de alto; el hueco vertical lo da el dibujo, no el
    // área sensible, porque una diana de 48 DIP de alto es la que hace que cerrar no
    // falle cuando la ventana está maximizada y el cursor topa con el borde.
    const float right = clientWidthDip;
    layout.close = Rect{right - kButtonWidth, 0.0f, kButtonWidth, kBarHeight};
    layout.maximize = Rect{right - kButtonWidth * 2.0f, 0.0f, kButtonWidth, kBarHeight};
    layout.minimize = Rect{right - kButtonWidth * 3.0f, 0.0f, kButtonWidth, kBarHeight};
    return layout;
}

const Rect& ButtonRect(const Layout& layout, Zone button) {
    switch (button) {
    case Zone::Minimize: return layout.minimize;
    case Zone::Maximize: return layout.maximize;
    default:             return layout.close;
    }
}

Zone HitTest(const Layout& layout, float xDip, float yDip) {
    // Los bordes primero, y ocupando también la parte de arriba de los botones: es como
    // se comporta una ventana de Windows, y quitarlos de ahí deja una ventana que no se
    // puede estirar desde la esquina superior derecha.
    if (!layout.maximized) {
        const bool left = xDip < kResizeBorder;
        const bool right = xDip >= layout.width - kResizeBorder;
        const bool top = yDip < kResizeBorder;
        const bool bottom = yDip >= layout.height - kResizeBorder;

        if (top && left) return Zone::TopLeft;
        if (top && right) return Zone::TopRight;
        if (bottom && left) return Zone::BottomLeft;
        if (bottom && right) return Zone::BottomRight;
        if (top) return Zone::Top;
        if (bottom) return Zone::Bottom;
        if (left) return Zone::Left;
        if (right) return Zone::Right;
    }

    if (layout.close.Contains(xDip, yDip)) return Zone::Close;
    if (layout.maximize.Contains(xDip, yDip)) return Zone::Maximize;
    if (layout.minimize.Contains(xDip, yDip)) return Zone::Minimize;

    // El resto de la franja arrastra. Devolver Caption (que se traduce a HTCAPTION) es
    // lo que regala, sin escribir una línea, el arrastre, el doble clic para maximizar y
    // el menú del sistema con el botón derecho.
    if (yDip < kBarHeight) return Zone::Caption;

    return Zone::Client;
}

}  // namespace Caption
