#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string_view>

#include "ui/layout.h"

namespace agenda {

// The text formats the popup draws with. They belong to DirectWrite rather than to a render
// target, so the live window and the offscreen snapshot each build one and nothing below has
// to care which of the two it is painting into.
struct Fonts {
  Microsoft::WRL::ComPtr<IDWriteFactory> factory;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> label;  // 11, weekday initials
  Microsoft::WRL::ComPtr<IDWriteTextFormat> day;    // 12, day numbers
  Microsoft::WRL::ComPtr<IDWriteTextFormat> event;  // 13, event cards and the input
  Microsoft::WRL::ComPtr<IDWriteTextFormat> title;  // 15 semibold, the month
  float scale = 0.0f;  // the panel scale these were built at, so a resize knows to rebuild

  bool Create(const PanelLayout& layout);
  bool ok() const { return title != nullptr; }
};

constexpr float Lerp(float from, float to, float t) { return from + (to - from) * t; }

constexpr D2D1_COLOR_F Lerp(const D2D1_COLOR_F& from, const D2D1_COLOR_F& to, float t) {
  return D2D1_COLOR_F{Lerp(from.r, to.r, t), Lerp(from.g, to.g, t), Lerp(from.b, to.b, t),
                      Lerp(from.a, to.a, t)};
}

constexpr D2D1_COLOR_F Fade(const D2D1_COLOR_F& color, float alpha) {
  return D2D1_COLOR_F{color.r, color.g, color.b, color.a * alpha};
}

constexpr D2D1_RECT_F Inset(const D2D1_RECT_F& rect, float by) {
  return D2D1_RECT_F{rect.left + by, rect.top + by, rect.right - by, rect.bottom - by};
}

// Walks a nought to one fade towards its target and says whether it still has ground to cover.
// Every hover and focus state in the popup and the app moves this way.
inline bool Settle(float& value, bool on, float step) {
  const float target = on ? 1.0f : 0.0f;
  if (value == target) return false;
  if (step <= 0.0f) return true;  // a tick too short to measure; the next one will move it
  if (step >= 1.0f) {
    value = target;
    return false;
  }
  value = value < target ? (value + step < target ? value + step : target)
                         : (value - step > target ? value - step : target);
  return value != target;
}

// The same curve DirectComposition runs for the popup's open and close, so the content and
// the window move with one motion language.
constexpr float EaseOutCubic(float t) {
  const float left = 1.0f - t;
  return 1.0f - left * left * left;
}

constexpr D2D1_POINT_2F Center(const D2D1_RECT_F& rect) {
  return D2D1_POINT_2F{(rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f};
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

}  // namespace agenda
