#pragma once

#include <windows.h>

#include <d2d1_1.h>

#include <string>

namespace dock {

struct TextSize {
  float width = 0;
  float height = 0;
};

// La etiqueta con el nombre: una píldora oscura con el texto en blanco. scale es dpi/96: la
// misma para la etiqueta y el menú. El dock de C# usaba iconSize/48 aquí y dpi/96 al contar
// las filas del menú, y solo cuadraban con iconos de 48.
TextSize MeasureLabel(const std::wstring& text, float scale);
void DrawLabel(ID2D1DeviceContext* context, const std::wstring& text, float scale, TextSize size, POINT at);

}  // namespace dock
