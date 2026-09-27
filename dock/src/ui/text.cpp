#include "ui/text.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <map>

namespace dock {
namespace {

using Microsoft::WRL::ComPtr;

constexpr float kFontSize = 13;
constexpr float kPaddingX = 10;
constexpr float kPaddingY = 5;

IDWriteFactory* Factory() {
  static ComPtr<IDWriteFactory> factory;
  if (!factory)
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
  return factory.Get();
}

// Un formato por escala, no uno global: compartido entre monitores, las píldoras salían de
// 42 px en todos; tienen que medir 29 a 100 % y 35 a 125 %.
IDWriteTextFormat* FormatFor(float scale) {
  static std::map<float, ComPtr<IDWriteTextFormat>> formats;
  auto& format = formats[scale];
  if (!format) {
    // Segoe UI Variable es la de Windows 11; si falta, DirectWrite cae a la del sistema.
    Factory()->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kFontSize * scale, L"",
                                &format);
  }
  return format.Get();
}

ComPtr<IDWriteTextLayout> LayoutOf(const std::wstring& text, float scale) {
  ComPtr<IDWriteTextLayout> layout;
  Factory()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), FormatFor(scale), 1000, 100, &layout);
  return layout;
}

}  // namespace

TextSize MeasureLabel(const std::wstring& text, float scale) {
  DWRITE_TEXT_METRICS metrics{};
  if (auto layout = LayoutOf(text, scale)) layout->GetMetrics(&metrics);
  return {std::ceil(metrics.width + kPaddingX * 2 * scale), std::ceil(metrics.height + kPaddingY * 2 * scale)};
}

void DrawLabel(ID2D1DeviceContext* context, const std::wstring& text, float scale, TextSize size, POINT at) {
  ComPtr<ID2D1SolidColorBrush> chip, ink;
  context->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.10f, 0.92f), &chip);
  context->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &ink);
  const float radius = size.height * 0.32f;
  // Medio píxel hacia dentro: el borde antialias no se sale de la superficie.
  const D2D1_ROUNDED_RECT pill{D2D1::RectF(at.x + 0.5f, at.y + 0.5f, at.x + size.width - 0.5f, at.y + size.height - 0.5f),
                               radius, radius};
  context->FillRoundedRectangle(pill, chip.Get());
  if (auto layout = LayoutOf(text, scale))
    context->DrawTextLayout(D2D1::Point2F(at.x + kPaddingX * scale, at.y + kPaddingY * scale), layout.Get(), ink.Get());
}

void DrawRow(ID2D1DeviceContext* context, const std::wstring& text, float scale, float x, float top, float rowHeight) {
  auto layout = LayoutOf(text, scale);
  if (!layout) return;
  DWRITE_TEXT_METRICS metrics{};
  layout->GetMetrics(&metrics);
  ComPtr<ID2D1SolidColorBrush> ink;
  context->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &ink);
  context->DrawTextLayout(D2D1::Point2F(x, top + (rowHeight - metrics.height) / 2), layout.Get(), ink.Get());
}

}  // namespace dock
