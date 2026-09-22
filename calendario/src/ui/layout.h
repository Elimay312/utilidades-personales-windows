#pragma once

#include <windows.h>

namespace agenda {

// Popup geometry from the design system in CLAUDE.md, in DIPs.
inline constexpr int kPopupWidthDip = 340;
inline constexpr int kPopupHeightDip = 420;
inline constexpr int kPopupMarginDip = 12;
inline constexpr float kPopupRadiusDip = 14.0f;
inline constexpr float kPopupSlideDip = 8.0f;  // how far the popup rises while opening

inline int ScaleDip(int dip, UINT dpi) {
  return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

// The popup sits in the bottom right corner of the work area, one margin away from both
// edges, which puts it over the taskbar corner without covering it.
inline RECT PopupRect(const RECT& work, UINT dpi) {
  const int margin = ScaleDip(kPopupMarginDip, dpi);
  const int right = work.right - margin;
  const int bottom = work.bottom - margin;
  return RECT{right - ScaleDip(kPopupWidthDip, dpi), bottom - ScaleDip(kPopupHeightDip, dpi),
              right, bottom};
}

}  // namespace agenda
