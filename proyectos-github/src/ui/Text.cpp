#include "ui/Text.h"

#include <algorithm>
#include <cmath>
#include <iterator>

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

void Text::DrawLine(ID2D1DeviceContext* dc, std::wstring_view text, Style style, Weight weight,
                    const D2D1_RECT_F& box, ID2D1Brush* brush) {
    if (!dc || !brush || text.empty()) return;
    IDWriteTextFormat* format = Format(style, weight);
    const auto layout = Fit(text, box.right - box.left, format);
    if (!layout) return;

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float y = box.top + ((box.bottom - box.top) - metrics.height) * 0.5f;

    dc->DrawTextLayout(D2D1::Point2F(box.left, y), layout.get(), brush,
                       D2D1_DRAW_TEXT_OPTIONS_NONE);
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
