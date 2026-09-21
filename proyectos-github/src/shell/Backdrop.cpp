#include "shell/Backdrop.h"

#include <dwmapi.h>

namespace Shell {

bool Backdrop::Apply(HWND hwnd) {
    // DWMSBT_MAINWINDOW y no TRANSIENTWINDOW: el lanzador eligió acrílico porque es una
    // pastilla que aparece y desaparece sobre lo que estés haciendo. Brújula es una
    // ventana que se queda abierta, y para eso está Mica, que tiñe del fondo de
    // escritorio sin arrastrar lo que haya debajo moviéndose.
    auto backdrop = DWMSBT_MAINWINDOW;
    const HRESULT micaResult =
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // DWMWCP_ROUND y no una región propia: el lanzador midió que SetWindowRgn NO recorta
    // el backdrop de DWM, así que con región las esquinas salían cuadradas.
    auto corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

    return SUCCEEDED(micaResult);
}

void Backdrop::SetDarkFrame(HWND hwnd, bool dark) {
    const BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}

void Backdrop::ExtendFrame(HWND hwnd) {
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

}  // namespace Shell
