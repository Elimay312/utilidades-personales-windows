#pragma once

// Texto con DirectWrite: la escala tipográfica de CLAUDE.md, recorte con elipsis, números
// tabulares y los iconos de Segoe Fluent Icons.
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
//
// DOS COSAS QUE NO PUEDEN IR EN EL FORMATO, y por eso existen Run y Layout:
//
//   - La ALINEACIÓN. Format() devuelve un objeto COMPARTIDO y cacheado; llamarle a
//     SetTextAlignment centraría todas las etiquetas de la aplicación a la vez. Va en la
//     maquetación, que es de usar y tirar.
//   - Los NÚMEROS TABULARES. 'tnum' es una propiedad de un rango de una maquetación, no
//     de un formato: se aplica con IDWriteTypography sobre el texto ya maquetado.

#include <d2d1_1.h>
#include <dwrite_3.h>

#include <cstddef>
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

enum class Align { Leading, Center, Trailing };

// Tabular: todas las cifras miden lo mismo. Para contadores, fechas y cualquier columna
// de números que se desplace, donde la anchura variable del 1 hace que el texto baile.
enum class Figures { Proportional, Tabular };

float SizeOf(Style style);

// Lo que ocupa de verdad, que no es la caja que se le dio.
struct Extent {
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;  // para alinear un punto o un icono con la línea del texto
};

struct Run {
    std::wstring_view text;
    Style style = Style::Body;
    Weight weight = Weight::Regular;
    Align align = Align::Leading;
    Figures figures = Figures::Proportional;
    // Recortar con elipsis si no cabe. Un campo de texto dice que no: ahí lo que no cabe
    // se sale, y el desplazamiento lo lleva el propio campo.
    bool trim = true;
    // Varias líneas. Va en la MAQUETACIÓN y no en el formato, por lo mismo que la
    // alineación y los números tabulares: el formato está cacheado y compartido, y
    // ponerle ajuste de línea aquí se lo pondría a todas las etiquetas de la aplicación
    // —incluidos los nombres de repositorio de las tarjetas, que tienen que recortarse—.
    bool wrap = false;
};

class Text {
public:
    bool Create(IDWriteFactory6* factory);

    IDWriteTextFormat* Format(Style style, Weight weight);
    IDWriteTextFormat* Icon(float sizeDip);

    // Medir sin dibujar. La píldora de prioridad y el contador de la barra lateral se
    // dimensionan con esto y no con un ancho a ojo.
    Extent Measure(std::wstring_view text, Style style, Weight weight,
                   Figures figures = Figures::Proportional, float maxWidthDip = 1.0e6f);

    void Draw(ID2D1DeviceContext* dc, const Run& run, const D2D1_RECT_F& box,
              ID2D1Brush* brush);

    // Una línea, recortada con elipsis si no cabe, centrada verticalmente en la caja.
    void DrawLine(ID2D1DeviceContext* dc, std::wstring_view text, Style style, Weight weight,
                  const D2D1_RECT_F& box, ID2D1Brush* brush);

    // Un glifo centrado en la caja. Para los botones de la barra de título y los iconos.
    void DrawGlyph(ID2D1DeviceContext* dc, std::wstring_view glyph, float sizeDip,
                   const D2D1_RECT_F& box, ID2D1Brush* brush);

    // --- Maquetación viva, para el campo de texto ------------------------------------
    //
    // Es propiedad de quien la pide y dura lo que él quiera. Las de DrawLine son de usar
    // y tirar y aquí no sirven: hay que medir y dibujar EXACTAMENTE la misma, porque una
    // elipsis cambia los índices y entonces el cursor caería donde no es.
    class Layout {
    public:
        bool Valid() const { return m_layout != nullptr; }
        void Reset();

        // El índice UTF-16 bajo el punto, en coordenadas de la caja.
        std::size_t IndexAt(float xDip, float yDip) const;

        struct Caret {
            float x = 0.0f;
            float y = 0.0f;
            float height = 0.0f;
        };
        Caret CaretAt(std::size_t index) const;

        // Los rectángulos que cubre una selección. En una línea casi siempre uno; dos si
        // hay un tramo de derecha a izquierda por medio. Devuelve cuántos hay de verdad,
        // que puede ser más que max.
        std::size_t SelectionBoxes(std::size_t begin, std::size_t end, D2D1_RECT_F* out,
                                   std::size_t max) const;

        float Width() const;
        float Height() const;
        IDWriteTextLayout* Raw() const { return m_layout.get(); }

    private:
        friend class Text;
        winrt::com_ptr<IDWriteTextLayout> m_layout;
        std::wstring m_text;
        Style m_style = Style::Body;
        Weight m_weight = Weight::Regular;
        float m_maxWidth = 0.0f;
        bool m_tabular = false;
    };

    // Rehace la maquetación solo si cambió algo. El DPI no entra: la maquetación va en
    // DIP y el DPI se lo pone Gfx::Surface al contexto.
    bool Lay(Layout& layout, std::wstring_view text, Style style, Weight weight,
             float maxWidthDip, bool tabular = false);

    void DrawLayout(ID2D1DeviceContext* dc, const Layout& layout, float xDip, float yDip,
                    ID2D1Brush* brush);

private:
    winrt::com_ptr<IDWriteTextLayout> Lay(std::wstring_view text, float maxWidth,
                                          IDWriteTextFormat* format);
    winrt::com_ptr<IDWriteTextLayout> Fit(std::wstring_view text, float maxWidth,
                                          IDWriteTextFormat* format);
    // Uno solo y cacheado.
    IDWriteTypography* Tabular();
    void ApplyTabular(IDWriteTextLayout* layout);

    winrt::com_ptr<IDWriteFactory6> m_factory;
    // Si es false, los formatos se piden por la vía clásica sobre Segoe UI.
    bool m_variable = false;
    std::map<std::pair<Style, Weight>, winrt::com_ptr<IDWriteTextFormat>> m_formats;
    std::map<int, winrt::com_ptr<IDWriteTextFormat>> m_icons;
    winrt::com_ptr<IDWriteTypography> m_tabular;
};

}  // namespace Ui
