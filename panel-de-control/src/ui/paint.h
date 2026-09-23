#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string_view>

namespace panel {

// The text formats the panel draws with. They belong to DirectWrite rather than to a render
// target, so the live window and the offscreen snapshot each build one set and the drawing
// does not care which of the two it paints into. Adapted from calendario/src/ui/paint.h.
struct Fonts {
  Microsoft::WRL::ComPtr<IDWriteFactory> factory;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> title;     // 13 semibold: tile names, card headers
  Microsoft::WRL::ComPtr<IDWriteTextFormat> body;      // 13: output names
  Microsoft::WRL::ComPtr<IDWriteTextFormat> caption;   // 11: subtitles, utility names, percents
  Microsoft::WRL::ComPtr<IDWriteTextFormat> note;      // 11 on up to two lines: the notice
  Microsoft::WRL::ComPtr<IDWriteTextFormat> icon;      // 16 Segoe Fluent Icons
  Microsoft::WRL::ComPtr<IDWriteTextFormat> iconSmall; // 12, the chevrons and the check

  bool Create();
  bool ok() const { return iconSmall != nullptr; }
};

constexpr float Lerp(float from, float to, float t) { return from + (to - from) * t; }

constexpr D2D1_COLOR_F Mix(const D2D1_COLOR_F& from, const D2D1_COLOR_F& to, float t) {
  return D2D1_COLOR_F{Lerp(from.r, to.r, t), Lerp(from.g, to.g, t), Lerp(from.b, to.b, t),
                      Lerp(from.a, to.a, t)};
}

constexpr D2D1_COLOR_F Fade(const D2D1_COLOR_F& color, float alpha) {
  return D2D1_COLOR_F{color.r, color.g, color.b, color.a * alpha};
}

constexpr D2D1_RECT_F Inset(const D2D1_RECT_F& rect, float dx, float dy) {
  return D2D1_RECT_F{rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy};
}

void FillRound(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float radius,
               ID2D1Brush* brush);
void StrokeRound(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float radius,
                 ID2D1Brush* brush, float width = 1.0f);
void FillCircle(ID2D1RenderTarget* target, D2D1_POINT_2F center, float radius,
                ID2D1Brush* brush);

enum class Align { Left, Center, Right };

// One line of text, vertically centred in rect and ellipsised when it does not fit.
void DrawTextIn(ID2D1RenderTarget* target, IDWriteTextFormat* format, std::wstring_view text,
                const D2D1_RECT_F& rect, ID2D1Brush* brush, Align align = Align::Left);

// One icon glyph centred on `center`.
void DrawGlyph(ID2D1RenderTarget* target, IDWriteTextFormat* format, wchar_t glyph,
               D2D1_POINT_2F center, ID2D1Brush* brush);

}  // namespace panel
