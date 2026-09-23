#pragma once

#include <windows.h>

#include <d2d1.h>
#include <shellscalingapi.h>  // GetDpiForMonitor

#include <array>
#include <cmath>
#include <vector>

#include "model/state.h"

namespace panel {

// The design system in CLAUDE.md, in DIP. The panel is one fixed width; its height is whatever
// its content adds up to, so opening a card grows it upwards from the corner it sits in.
inline constexpr float kPanelWidthDip = 344.0f;
inline constexpr float kPadDip = 12.0f;
inline constexpr float kGapDip = 8.0f;
inline constexpr float kRadiusPanel = 14.0f;
inline constexpr float kRadiusCard = 10.0f;
inline constexpr float kTileHeightDip = 64.0f;
inline constexpr float kSliderHeightDip = 28.0f;
inline constexpr float kHeaderHeightDip = 16.0f;   // "Brillo · Portátil" line on a card
inline constexpr float kDisplayRowDip = 50.0f;     // name, then its slider
inline constexpr float kDisplayRowGapDip = 10.0f;
inline constexpr float kOutputRowDip = 32.0f;
inline constexpr float kAppsRowDip = 56.0f;
inline constexpr float kAppCircleDip = 36.0f;
inline constexpr float kNoticeDip = 32.0f;       // one line saying what went wrong

inline constexpr int kMarginDip = 12;           // from the work area's corner
inline constexpr float kSlideDip = 8.0f;        // how far the panel rises while opening

// The DPI of the monitor `hwnd` is on right now. Not GetDpiForWindow: Windows does not send a
// popup WM_DPICHANGED when the scale of its own monitor changes (measured in Agenda, phase 7).
inline UINT MonitorDpi(HWND hwnd) {
  UINT x = 0;
  UINT y = 0;
  if (FAILED(GetDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                              MDT_EFFECTIVE_DPI, &x, &y)) ||
      x == 0) {
    return GetDpiForWindow(hwnd);
  }
  return x;
}

// Where to open: `pinned` when --monitor chose one, otherwise the monitor the mouse is on.
inline HMONITOR TargetMonitor(HMONITOR pinned) {
  if (pinned != nullptr) return pinned;
  POINT cursor{};
  GetCursorPos(&cursor);
  return MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
}

inline int ScaleDip(float dip, UINT dpi) {
  return static_cast<int>(std::lround(dip * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
}

// Bottom right of the work area, one margin from both edges: above the tray, where Windows puts
// its own quick settings. `size` is in DIP, the result in physical pixels.
inline RECT PlaceRect(const RECT& work, D2D1_SIZE_F size, UINT dpi) {
  const int margin = ScaleDip(static_cast<float>(kMarginDip), dpi);
  const int right = work.right - margin;
  const int bottom = work.bottom - margin;
  return RECT{right - ScaleDip(size.width, dpi), bottom - ScaleDip(size.height, dpi), right,
              bottom};
}

// Which cards are open. The only view state the layout depends on.
struct Expanded {
  bool brightness = false;
  bool audio = false;
};

struct DisplayRow {
  D2D1_RECT_F label;
  D2D1_RECT_F slider;
};

// Every rectangle inside the panel, in DIP from its top left corner, worked out once. The
// drawing reads this and (from phase 2) so does the hit testing, so a click lands on the pixel.
struct PanelLayout {
  float width = kPanelWidthDip;
  float height = 0.0f;
  std::array<D2D1_RECT_F, 4> tiles{};  // Wi-Fi, Bluetooth, night light, settings

  D2D1_RECT_F brightnessCard{};
  D2D1_RECT_F brightnessHeader{};      // the title line; the chevron sits at its right end
  D2D1_RECT_F brightnessSlider{};      // closed: the one slider for the monitor it opened on
  std::vector<DisplayRow> displayRows; // open: one per screen

  D2D1_RECT_F audioCard{};
  D2D1_RECT_F audioHeader{};
  D2D1_RECT_F audioSlider{};
  D2D1_RECT_F outputsDivider{};        // open: the hairline above the list
  std::vector<D2D1_RECT_F> outputRows;

  std::vector<D2D1_RECT_F> apps;       // one column per utility
  D2D1_RECT_F notice{};                // empty unless the state carries one

  D2D1_SIZE_F size() const { return D2D1_SIZE_F{width, height}; }
};

inline PanelLayout MakeLayout(Expanded open, const PanelState& state) {
  const size_t displays = state.displays.size();
  const size_t outputs = state.audio.outputs.size();
  const size_t apps = state.apps.size();
  PanelLayout layout;
  const float left = kPadDip;
  const float right = kPanelWidthDip - kPadDip;
  float y = kPadDip;

  const float tileWidth = (right - left - kGapDip) / 2.0f;
  for (size_t i = 0; i < layout.tiles.size(); ++i) {
    const float x = left + static_cast<float>(i % 2) * (tileWidth + kGapDip);
    const float top = y + static_cast<float>(i / 2) * (kTileHeightDip + kGapDip);
    layout.tiles[i] = D2D1_RECT_F{x, top, x + tileWidth, top + kTileHeightDip};
  }
  y += 2.0f * kTileHeightDip + 2.0f * kGapDip;

  // Brightness. Closed it is one slider; open, one row per screen in its place.
  const float inner = kPadDip;
  const float cardTop = y;
  layout.brightnessHeader =
      D2D1_RECT_F{left + inner, cardTop + inner, right - inner, cardTop + inner + kHeaderHeightDip};
  y = layout.brightnessHeader.bottom + kGapDip;
  if (open.brightness && displays > 0) {
    for (size_t i = 0; i < displays; ++i) {
      DisplayRow row;
      row.label = D2D1_RECT_F{left + inner, y, right - inner, y + kHeaderHeightDip};
      const float sliderTop = y + kDisplayRowDip - kSliderHeightDip;
      row.slider = D2D1_RECT_F{left + inner, sliderTop, right - inner, sliderTop + kSliderHeightDip};
      layout.displayRows.push_back(row);
      y += kDisplayRowDip + (i + 1 < displays ? kDisplayRowGapDip : 0.0f);
    }
  } else {
    layout.brightnessSlider = D2D1_RECT_F{left + inner, y, right - inner, y + kSliderHeightDip};
    y += kSliderHeightDip;
  }
  y += inner;
  layout.brightnessCard = D2D1_RECT_F{left, cardTop, right, y};
  y += kGapDip;

  // Volume. The slider always stays; open, the outputs go under it.
  const float audioTop = y;
  layout.audioHeader =
      D2D1_RECT_F{left + inner, audioTop + inner, right - inner, audioTop + inner + kHeaderHeightDip};
  y = layout.audioHeader.bottom + kGapDip;
  layout.audioSlider = D2D1_RECT_F{left + inner, y, right - inner, y + kSliderHeightDip};
  y += kSliderHeightDip;
  if (open.audio && outputs > 0) {
    y += kGapDip;
    layout.outputsDivider = D2D1_RECT_F{left + inner, y, right - inner, y + 1.0f};
    y += 1.0f + 4.0f;
    for (size_t i = 0; i < outputs; ++i) {
      // The rows run the card's full width, so their hover wash reaches its edges.
      layout.outputRows.push_back(D2D1_RECT_F{left + 4.0f, y, right - 4.0f, y + kOutputRowDip});
      y += kOutputRowDip;
    }
    y += kGapDip / 2.0f;
  } else {
    y += inner;
  }
  layout.audioCard = D2D1_RECT_F{left, audioTop, right, y};
  y += kGapDip;

  // The utilities, spread evenly across the width.
  if (apps > 0) {
    const float column = (right - left) / static_cast<float>(apps);
    for (size_t i = 0; i < apps; ++i) {
      const float x = left + static_cast<float>(i) * column;
      layout.apps.push_back(D2D1_RECT_F{x, y, x + column, y + kAppsRowDip});
    }
    y += kAppsRowDip;
  } else {
    y -= kGapDip;
  }

  if (!state.notice.empty()) {
    y += kGapDip;
    layout.notice = D2D1_RECT_F{left, y, right, y + kNoticeDip};
    y += kNoticeDip;
  }

  layout.height = std::round(y + kPadDip);
  return layout;
}

}  // namespace panel
