#pragma once

// El material de la ventana lo pone DWM, no nosotros: el sistema ya sabe difuminar lo
// que hay detrás mejor y más barato de lo que lo haríamos aquí, y además Mica se atenúa
// sola cuando la ventana pierde el foco, que es medio efecto gratis.
//
// La condición para que se vea es que la ventana NO pinte un fondo opaco. Por eso
// hbrBackground es nulo, WM_ERASEBKGND devuelve 1 y no hay una sola llamada a GDI en
// todo el programa.

#include <Windows.h>

namespace Shell {

class Backdrop {
public:
    // Mica y esquinas redondeadas. Devuelve false en Windows 10, donde los dos atributos
    // no existen y DWM responde E_INVALIDARG: no es un error que haya que tratar, es la
    // degradación con elegancia que pide CLAUDE.md, y quien llama pinta entonces un color
    // de los tokens por debajo.
    static bool Apply(HWND hwnd);

    // El marco (borde y esquinas) a juego con el tema.
    static void SetDarkFrame(HWND hwnd, bool dark);

    // Un píxel de marco arriba, y solo uno.
    //
    // Al recuperar la franja del título en WM_NCCALCSIZE se pierde la línea de borde
    // superior que dibuja DWM, y la ventana queda con el borde abierto por arriba.
    // Extendiendo el marco un píxel, DWM vuelve a dibujarla.
    //
    // NO se extiende a toda la ventana (-1), y es una corrección medida: con el marco
    // entero extendido, DWM considera que toda la ventana es marco y dibuja ENCIMA sus
    // propios botones de minimizar, maximizar y cerrar. Se ven los seis a la vez,
    // desplazados unos cinco píxeles porque los suyos van en una franja de 32 y los
    // nuestros en una de 48. Un píxel es todo lo que hace falta y es todo lo que se pide.
    static void ExtendFrame(HWND hwnd);
};

}  // namespace Shell
