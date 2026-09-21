#include "ui/Text.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace Ui {

namespace {

constexpr wchar_t kVariable[] = L"Segoe UI Variable";
constexpr wchar_t kClassic[] = L"Segoe UI";
constexpr wchar_t kIcons[] = L"Segoe Fluent Icons";
constexpr wchar_t kLocale[] = L"es-ES";

// Los ejes de Segoe UI Variable van de 8 a 36. Dejar que el eje óptico siga al cuerpo es
// lo que hace que un título de 26 se dibuje con los remates de "Display" y una nota de 11
// con los de "Text", sin elegir familia a mano.
constexpr float kOpticalMin = 8.0f;
constexpr float kOpticalMax = 36.0f;

// El carácter de elipsis. Uno solo, no tres puntos: tres puntos ocupan más y se leen como
// una pausa, no como un recorte.
constexpr wchar_t kEllipsis = 0x2026;

bool FamilyExists(IDWriteFactory6* factory, const wchar_t* name) {
    winrt::com_ptr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(collection.put(), FALSE))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(collection->FindFamilyName(name, &index, &exists))) return false;
    return exists != FALSE;
}

}  // namespace

float SizeOf(Style style) {
    switch (style) {
    case Style::Title:    return 26.0f;
    case Style::Heading:  return 20.0f;
    case Style::Body:     return 15.0f;
    case Style::Caption:  return 13.0f;
    case Style::Footnote: return 11.0f;
    }
    return 15.0f;
}

bool Text::Create(IDWriteFactory6* factory) {
    if (!factory) return false;
    m_factory.copy_from(factory);
    m_variable = FamilyExists(factory, kVariable);
    return true;
}

IDWriteTextFormat* Text::Format(Style style, Weight weight) {
    const auto key = std::make_pair(style, weight);
    if (const auto found = m_formats.find(key); found != m_formats.end()) {
        return found->second.get();
    }

    const float size = SizeOf(style);
    const float wght = weight == Weight::Semibold ? 600.0f : 400.0f;
    winrt::com_ptr<IDWriteTextFormat> format;

    if (m_variable) {
        const DWRITE_FONT_AXIS_VALUE axes[] = {
            {DWRITE_FONT_AXIS_TAG_WEIGHT, wght},
            {DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE, std::clamp(size, kOpticalMin, kOpticalMax)},
        };
        winrt::com_ptr<IDWriteTextFormat3> variable;
        if (SUCCEEDED(m_factory->CreateTextFormat(kVariable, nullptr, axes,
                                                  static_cast<UINT32>(std::size(axes)), size,
                                                  kLocale, variable.put()))) {
            format = variable;
        }
    }

    if (!format) {
        m_factory->CreateTextFormat(
            kClassic, nullptr,
            weight == Weight::Semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, kLocale, format.put());
    }
    if (!format) return nullptr;

    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const auto inserted = m_formats.emplace(key, format);
    return inserted.first->second.get();
}

IDWriteTextFormat* Text::Icon(float sizeDip) {
    const int key = static_cast<int>(std::lround(sizeDip * 100.0f));
    if (const auto found = m_icons.find(key); found != m_icons.end()) return found->second.get();

    winrt::com_ptr<IDWriteTextFormat> format;
    if (FAILED(m_factory->CreateTextFormat(kIcons, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           sizeDip, kLocale, format.put()))) {
        return nullptr;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const auto inserted = m_icons.emplace(key, format);
    return inserted.first->second.get();
}

winrt::com_ptr<IDWriteTextLayout> Text::Lay(std::wstring_view text, float maxWidth,
                                            IDWriteTextFormat* format) {
    winrt::com_ptr<IDWriteTextLayout> layout;
    if (!format) return layout;
    m_factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                std::max(maxWidth, 0.0f), 4096.0f, layout.put());
    return layout;
}

winrt::com_ptr<IDWriteTextLayout> Text::Fit(std::wstring_view text, float maxWidth,
                                            IDWriteTextFormat* format) {
    const auto tooWide = [&](const winrt::com_ptr<IDWriteTextLayout>& layout) {
        if (!layout) return false;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return metrics.width > maxWidth;
    };

    auto layout = Lay(text, maxWidth, format);
    if (!tooWide(layout)) return layout;

    // Midiendo, no contando letras: "Revision_2026-03-12.md" y "IIIIIIIIIIIIIIIIIIIIII"
    // tienen las mismas letras y no ocupan ni parecido. Búsqueda binaria: con un nombre
    // de cuarenta caracteres son seis medidas en vez de cuarenta, y esto corre una vez
    // por elemento visible.
    std::wstring candidate;
    std::size_t low = 0;
    std::size_t high = text.size();
    while (low < high) {
        std::size_t middle = (low + high + 1) / 2;
        // Sin partir un par suplente por la mitad: cortar entre las dos mitades de un
        // emoji deja un carácter inválido que se dibuja como un cuadro.
        if (middle < text.size() && middle > 0 && (text[middle] & 0xFC00) == 0xDC00) --middle;
        if (middle <= low) break;

        candidate.assign(text.substr(0, middle));
        candidate.push_back(kEllipsis);
        if (tooWide(Lay(candidate, maxWidth, format))) {
            high = middle - 1;
        } else {
            low = middle;
        }
    }

    candidate.assign(text.substr(0, low));
    candidate.push_back(kEllipsis);
    return Lay(candidate, maxWidth, format);
}

IDWriteTypography* Text::Tabular() {
    if (m_tabular) return m_tabular.get();
    if (FAILED(m_factory->CreateTypography(m_tabular.put()))) return nullptr;
    // 'tnum'. No se puede poner en un IDWriteTextFormat: es propiedad de un RANGO de una
    // maquetación, y por eso los números tabulares pasan obligatoriamente por aquí.
    const DWRITE_FONT_FEATURE feature{DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1};
    if (FAILED(m_tabular->AddFontFeature(feature))) {
        m_tabular = nullptr;
        return nullptr;
    }
    return m_tabular.get();
}

void Text::ApplyTabular(IDWriteTextLayout* layout) {
    IDWriteTypography* typography = Tabular();
    if (!layout || !typography) return;
    // Todo el rango. IDWriteTextLayout no sabe decir cuánto texto tiene, pero sí sujeta
    // los rangos a la longitud real, así que pedir el máximo es la forma de decir
    // "entero" sin tener que llevar la cuenta por fuera —y la cuenta cambiaría cuando
    // Fit recorta con elipsis—.
    const DWRITE_TEXT_RANGE range{0, UINT32_MAX};
    layout->SetTypography(typography, range);
}

Extent Text::Measure(std::wstring_view text, Style style, Weight weight, Figures figures,
                     float maxWidthDip) {
    Extent extent;
    if (text.empty()) {
        // Sin texto no hay anchura, pero sí altura de línea: una etiqueta vacía tiene que
        // ocupar su renglón o el layout de al lado se descuadra al vaciarse.
        const auto empty = Lay(L" ", maxWidthDip, Format(style, weight));
        if (empty) {
            DWRITE_TEXT_METRICS metrics{};
            empty->GetMetrics(&metrics);
            extent.height = metrics.height;
            DWRITE_LINE_METRICS line{};
            UINT32 count = 1;
            if (SUCCEEDED(empty->GetLineMetrics(&line, 1, &count)) && count > 0) {
                extent.baseline = line.baseline;
            }
        }
        return extent;
    }

    auto layout = Lay(text, maxWidthDip, Format(style, weight));
    if (!layout) return extent;
    if (figures == Figures::Tabular) ApplyTabular(layout.get());

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    extent.width = metrics.width;
    extent.height = metrics.height;

    DWRITE_LINE_METRICS line{};
    UINT32 count = 1;
    if (SUCCEEDED(layout->GetLineMetrics(&line, 1, &count)) && count > 0) {
        extent.baseline = line.baseline;
    }
    return extent;
}

void Text::Draw(ID2D1DeviceContext* dc, const Run& run, const D2D1_RECT_F& box,
                ID2D1Brush* brush) {
    if (!dc || !brush || run.text.empty()) return;

    IDWriteTextFormat* format = Format(run.style, run.weight);
    const float width = box.right - box.left;
    auto layout = run.trim ? Fit(run.text, width, format) : Lay(run.text, width, format);
    if (!layout) return;

    // La alineación va en la MAQUETACIÓN y nunca en el formato: el formato está cacheado
    // y compartido, y centrarlo aquí centraría todas las etiquetas de la aplicación.
    if (run.align != Align::Leading) {
        layout->SetTextAlignment(run.align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                            : DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if (run.figures == Figures::Tabular) {
        ApplyTabular(layout.get());
    }

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float y = box.top + ((box.bottom - box.top) - metrics.height) * 0.5f;

    dc->DrawTextLayout(D2D1::Point2F(box.left, y), layout.get(), brush,
                       D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Text::DrawLine(ID2D1DeviceContext* dc, std::wstring_view text, Style style, Weight weight,
                    const D2D1_RECT_F& box, ID2D1Brush* brush) {
    Run run;
    run.text = text;
    run.style = style;
    run.weight = weight;
    Draw(dc, run, box, brush);
}

bool Text::Lay(Layout& layout, std::wstring_view text, Style style, Weight weight,
               float maxWidthDip, bool tabular) {
    // Solo si cambió algo. El campo de texto pide esto en cada pulsación y en cada
    // movimiento del cursor, y rehacer la maquetación para mover el cursor sería tirar
    // el trabajo de DirectWrite sesenta veces por segundo.
    if (layout.m_layout && layout.m_style == style && layout.m_weight == weight &&
        layout.m_tabular == tabular && layout.m_maxWidth == maxWidthDip &&
        layout.m_text.size() == text.size() && layout.m_text == text) {
        return true;
    }

    layout.m_text.assign(text);
    layout.m_style = style;
    layout.m_weight = weight;
    layout.m_maxWidth = maxWidthDip;
    layout.m_tabular = tabular;
    // Sin recorte: en un campo de texto lo que no cabe se sale, y el desplazamiento lo
    // lleva el campo. Una elipsis cambiaría los índices y el cursor caería donde no es.
    layout.m_layout = Lay(layout.m_text, maxWidthDip, Format(style, weight));
    if (layout.m_layout && tabular) {
        ApplyTabular(layout.m_layout.get());
    }
    return layout.m_layout != nullptr;
}

void Text::DrawLayout(ID2D1DeviceContext* dc, const Layout& layout, float xDip, float yDip,
                      ID2D1Brush* brush) {
    if (!dc || !brush || !layout.Valid()) return;
    dc->DrawTextLayout(D2D1::Point2F(xDip, yDip), layout.Raw(), brush,
                       D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Text::Layout::Reset() {
    m_layout = nullptr;
    m_text.clear();
}

std::size_t Text::Layout::IndexAt(float xDip, float yDip) const {
    if (!m_layout) return 0;

    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(m_layout->HitTestPoint(xDip, yDip, &trailing, &inside, &metrics))) return 0;

    // Sumar metrics.length y no 1: así el índice cae siempre en un límite legal aunque
    // debajo hubiera un par suplente o un grupo de letra más tilde. Es el mismo invariante
    // que Ui::PrevBoundary defiende por el lado del modelo, y los dos tienen que coincidir
    // o el cursor se pondría donde el modelo no lo deja estar.
    return metrics.textPosition + (trailing ? metrics.length : 0);
}

Text::Layout::Caret Text::Layout::CaretAt(std::size_t index) const {
    Caret caret;
    if (!m_layout) return caret;

    DWRITE_HIT_TEST_METRICS metrics{};
    float x = 0.0f;
    float y = 0.0f;
    if (FAILED(m_layout->HitTestTextPosition(static_cast<UINT32>(index), FALSE, &x, &y,
                                             &metrics))) {
        return caret;
    }
    caret.x = x;
    caret.y = y;
    caret.height = metrics.height;
    return caret;
}

std::size_t Text::Layout::SelectionBoxes(std::size_t begin, std::size_t end, D2D1_RECT_F* out,
                                         std::size_t max) const {
    if (!m_layout || end <= begin) return 0;

    const UINT32 position = static_cast<UINT32>(begin);
    const UINT32 length = static_cast<UINT32>(end - begin);

    // Dos llamadas: la primera dice cuántos hay —devuelve E_NOT_SUFFICIENT_BUFFER— y la
    // segunda los trae. Preguntar con cero es la forma documentada de contar.
    UINT32 actual = 0;
    m_layout->HitTestTextRange(position, length, 0.0f, 0.0f, nullptr, 0, &actual);
    if (actual == 0) return 0;

    std::vector<DWRITE_HIT_TEST_METRICS> hits(actual);
    if (FAILED(m_layout->HitTestTextRange(position, length, 0.0f, 0.0f, hits.data(), actual,
                                          &actual))) {
        return 0;
    }

    const std::size_t copied = std::min(static_cast<std::size_t>(actual), max);
    for (std::size_t i = 0; i < copied; ++i) {
        out[i] = D2D1::RectF(hits[i].left, hits[i].top, hits[i].left + hits[i].width,
                             hits[i].top + hits[i].height);
    }
    return actual;
}

float Text::Layout::Width() const {
    if (!m_layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    m_layout->GetMetrics(&metrics);
    return metrics.width;
}

float Text::Layout::Height() const {
    if (!m_layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    m_layout->GetMetrics(&metrics);
    return metrics.height;
}

void Text::DrawGlyph(ID2D1DeviceContext* dc, std::wstring_view glyph, float sizeDip,
                     const D2D1_RECT_F& box, ID2D1Brush* brush) {
    if (!dc || !brush || glyph.empty()) return;
    IDWriteTextFormat* format = Icon(sizeDip);
    if (!format) return;

    const auto layout = Lay(glyph, box.right - box.left, format);
    if (!layout) return;

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float x = box.left + ((box.right - box.left) - metrics.width) * 0.5f;
    const float y = box.top + ((box.bottom - box.top) - metrics.height) * 0.5f;
    dc->DrawTextLayout(D2D1::Point2F(x, y), layout.get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

}  // namespace Ui
