#pragma once

#include <windows.h>

#include <d2d1.h>

#include "core/dates.h"
#include "ui/theme.h"

namespace agenda {

// Popup geometry from the design system in CLAUDE.md, in DIPs.
inline constexpr int kPopupWidthDip = 340;
inline constexpr int kPopupHeightDip = 420;
inline constexpr int kPopupMarginDip = 12;
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

// --- Inside the panel -------------------------------------------------------------------
// Everything below is measured from the panel's top left corner, on the four DIP grid. The
// drawing and the hit testing both read these, so a click always lands where the pixel is.

inline constexpr float kContentLeft = kPaddingDip;                            // 16
inline constexpr float kContentRight = kPopupWidthDip - kPaddingDip;          // 324
inline constexpr float kContentWidth = kContentRight - kContentLeft;          // 308

inline constexpr float kHeaderTop = 16.0f;
inline constexpr float kHeaderHeight = 28.0f;
inline constexpr float kArrowSize = 28.0f;

inline constexpr float kWeekdayTop = 48.0f;
inline constexpr float kWeekdayHeight = 16.0f;

inline constexpr float kGridTop = 68.0f;
inline constexpr float kCellWidth = kContentWidth / kGridCols;                // 44
inline constexpr float kCellHeight = 36.0f;
inline constexpr float kGridHeight = kCellHeight * kGridRows;                 // 216

inline constexpr float kListTop = 292.0f;
inline constexpr float kListHeight = 68.0f;   // two cards of 32 with four of air
inline constexpr float kCardHeight = 32.0f;
inline constexpr int kVisibleCards = 2;

inline constexpr float kInputTop = 368.0f;
inline constexpr float kInputHeight = 36.0f;
inline constexpr float kInputRadius = kInputHeight / 2.0f;  // a full capsule

inline constexpr D2D1_RECT_F GridRect() {
  return D2D1_RECT_F{kContentLeft, kGridTop, kContentRight, kGridTop + kGridHeight};
}

inline constexpr D2D1_RECT_F CellRect(int cell) {
  const float left = kContentLeft + static_cast<float>(cell % kGridCols) * kCellWidth;
  const float top = kGridTop + static_cast<float>(cell / kGridCols) * kCellHeight;
  return D2D1_RECT_F{left, top, left + kCellWidth, top + kCellHeight};
}

inline constexpr D2D1_RECT_F NextArrowRect() {
  return D2D1_RECT_F{kContentRight - kArrowSize, kHeaderTop, kContentRight,
                     kHeaderTop + kArrowSize};
}

inline constexpr D2D1_RECT_F PrevArrowRect() {
  const float right = kContentRight - kArrowSize - kGapDip;
  return D2D1_RECT_F{right - kArrowSize, kHeaderTop, right, kHeaderTop + kArrowSize};
}

inline constexpr D2D1_RECT_F ListRect() {
  return D2D1_RECT_F{kContentLeft, kListTop, kContentRight, kListTop + kListHeight};
}

inline constexpr D2D1_RECT_F InputRect() {
  return D2D1_RECT_F{kContentLeft, kInputTop, kContentRight, kInputTop + kInputHeight};
}

inline constexpr bool Inside(const D2D1_RECT_F& rect, float x, float y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

}  // namespace agenda
