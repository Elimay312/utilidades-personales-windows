#include "ui/popup_view.h"

#include <wrl/client.h>

#include "ui/layout.h"

namespace agenda {
namespace {

constexpr D2D1_COLOR_F Rgb(UINT32 rgb, float alpha) {
  return D2D1_COLOR_F{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                      static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

// Design system, dark theme: panel #1E1F24 at 85% over the acrylic, plus the hairline that
// Windows 11 draws around its own flyouts.
constexpr UINT32 kPanelColor = 0x1E1F24;
constexpr float kPanelAlpha = 0.85f;
constexpr float kBorderAlpha = 0.08f;

}  // namespace

void DrawPopup(ID2D1RenderTarget* target, D2D1_SIZE_F sizeDip, bool acrylic) {
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(Rgb(kPanelColor, acrylic ? kPanelAlpha : 1.0f),
                                           &brush))) {
    return;
  }

  target->FillRoundedRectangle(
      D2D1_ROUNDED_RECT{D2D1_RECT_F{0.0f, 0.0f, sizeDip.width, sizeDip.height}, kPopupRadiusDip,
                        kPopupRadiusDip},
      brush.Get());

  // Half a DIP inset so the one DIP stroke lands inside the panel instead of straddling it.
  brush->SetColor(Rgb(0xFFFFFF, kBorderAlpha));
  target->DrawRoundedRectangle(
      D2D1_ROUNDED_RECT{D2D1_RECT_F{0.5f, 0.5f, sizeDip.width - 0.5f, sizeDip.height - 0.5f},
                        kPopupRadiusDip - 0.5f, kPopupRadiusDip - 0.5f},
      brush.Get(), 1.0f);
}

}  // namespace agenda
