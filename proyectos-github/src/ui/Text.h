#pragma once

// Texto con DirectWrite: la escala tipográfica de CLAUDE.md, recorte con elipsis y los
// iconos de Segoe Fluent Icons.
//
// Segoe UI Variable es UNA familia variable, no dos. "Display" y "Text" son valores del
// eje óptico (opsz), así que en vez de elegir familia por tamaño se deja que el eje siga
// al cuerpo, que es para lo que está. Si la fuente no estuviera —Windows 10 sin
// actualizar—, se cae a Segoe UI y no se nota más que en el detalle de los remates.
//
// La caché de formatos NO lleva la escala en la clave, al revés que la de renombrar, y
// es a propósito: allí el contexto dibuja siempre a 96 ppp y hay que hornear la escala en
// el cuerpo de letra; aquí Surface le pone al contexto el DPI de la ventana, así que el
// formato se mide en DIP y vale para cualquier monitor. Un formato por estilo y peso.

#include <d2d1_1.h>
#include <dwrite_3.h>

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <winrt/base.h>

namespace Ui {

enum class Style {
    Title,     // 26 DIP
    Heading,   // 20 DIP
    Body,      // 15 DIP
    Caption,   // 13 DIP
    Footnote,  // 11 DIP
};

enum class Weight { Regular, Semibold };

float SizeOf(Style style);

class Text {
public:
    bool Create(IDWriteFactory6* factory);

    IDWriteTextFormat* Format(Style style, Weight weight);
    IDWriteTextFormat* Icon(float sizeDip);

    // Una línea, recortada con elipsis si no cabe, centrada verticalmente en la caja.
    void DrawLine(ID2D1DeviceContext* dc, std::wstring_view text, Style style, Weight weight,
                  const D2D1_RECT_F& box, ID2D1Brush* brush);

    // Un glifo centrado en la caja. Para los botones de la barra de título.
    void DrawGlyph(ID2D1DeviceContext* dc, std::wstring_view glyph, float sizeDip,
                   const D2D1_RECT_F& box, ID2D1Brush* brush);

private:
    winrt::com_ptr<IDWriteTextLayout> Lay(std::wstring_view text, float maxWidth,
                                          IDWriteTextFormat* format);
    winrt::com_ptr<IDWriteTextLayout> Fit(std::wstring_view text, float maxWidth,
                                          IDWriteTextFormat* format);

    winrt::com_ptr<IDWriteFactory6> m_factory;
    // Si es false, los formatos se piden por la vía clásica sobre Segoe UI.
    bool m_variable = false;
    std::map<std::pair<Style, Weight>, winrt::com_ptr<IDWriteTextFormat>> m_formats;
    std::map<int, winrt::com_ptr<IDWriteTextFormat>> m_icons;
};

}  // namespace Ui
