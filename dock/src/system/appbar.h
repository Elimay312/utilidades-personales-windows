#pragma once

#include <windows.h>

#include <optional>

namespace dock {

// Cada dock se registra como appbar del borde inferior: para recibir los avisos de la
// barra (posición, pantalla completa) y, sin autoocultar, para reservar su franja y que las
// ventanas maximizadas no lo tapen.
constexpr UINT kAppBarCallback = WM_APP + 7;

bool AppBarRegister(HWND hwnd);
void AppBarRemove(HWND hwnd);

// Pide la franja [bottom - height, bottom] del monitor. Devuelve la que Windows concede, que
// es la que hay que usar: recalcularla desde el área de trabajo subía el dock un poco en
// cada aviso (una escalera infinita).
std::optional<RECT> AppBarReserve(HWND hwnd, const RECT& monitor, int height);

// Hacen falta para que el z-order con la barra autoocultada sea el correcto.
void AppBarActivate(HWND hwnd);
void AppBarWindowPosChanged(HWND hwnd);

}  // namespace dock
