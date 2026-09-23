#pragma once

#include <windows.h>

#include <d2d1.h>
#include <shellscalingapi.h>  // GetDpiForMonitor

#include <algorithm>
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
// Phase 5b: a tile that unfolds into a card of its own.
inline constexpr float kTileChevronDip = 34.0f;  // the strip at a tile's right end that unfolds it
inline constexpr float kModuleHeaderDip = 32.0f; // title and switch
inline constexpr float kModuleRowDip = 36.0f;    // a network, a device, or the one line saying why none
inline constexpr size_t kModuleMaxRows = 6;      // the rest are for Windows' own list, in the footer
inline constexpr float kSwitchWidthDip = 44.0f;  // Agenda's switch (calendario/CLAUDE.md)
inline constexpr float kSwitchHeightDip = 24.0f;

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

// How open each card is, 0 closed and 1 open, and anything in between while its spring runs
// (a hair over 1 on the overshoot). The only view state the layout depends on. Wi-Fi and
// Bluetooth are tiles morphing into cards (phase 5b); only one of them is ever open.
struct Expanded {
  float brightness = 0.0f;
  float audio = 0.0f;
  float wifi = 0.0f;
  float bluetooth = 0.0f;
};

// A tile on its way to being a card, or back. `card` is where the morphing shape is this
// frame; everything inside it is laid out where it will be once open, and clipped to `card`
// on the way, like the rows of the other cards.
struct ModuleLayout {
  int tile = -1;  // 0 Wi-Fi, 1 Bluetooth, -1 none
  float t = 0.0f;
  D2D1_RECT_F card{};
  D2D1_RECT_F header{};
  D2D1_RECT_F toggle{};
  std::vector<D2D1_RECT_F> rows;  // at least one: with nothing to list, it says why
  D2D1_RECT_F footer{};
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
  std::array<D2D1_RECT_F, 2> tileChevrons{};  // the unfolding strips of Wi-Fi and Bluetooth
  ModuleLayout module;

  D2D1_RECT_F brightnessCard{};
  D2D1_RECT_F brightnessHeader{};      // the title line; the chevron sits at its right end
  D2D1_RECT_F brightnessSlider{};      // closed: the one slider for the monitor it opened on
  std::vector<DisplayRow> displayRows; // opening or open: one per screen, laid out as if open
                                       // and clipped to the card while it grows

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
  for (size_t i = 0; i < layout.tileChevrons.size(); ++i) {
    const D2D1_RECT_F& tile = layout.tiles[i];
    layout.tileChevrons[i] = D2D1_RECT_F{tile.right - kTileChevronDip, tile.top, tile.right, tile.bottom};
  }

  // The tile grid, or the card a tile of it is becoming. The card takes the grid's full width
  // from its top, and the grid's height turns into the card's: what is below slides with it.
  const float gridTop = y;
  const float gridHeight = 2.0f * kTileHeightDip + kGapDip;
  float grid = gridHeight;
  ModuleLayout& module = layout.module;
  module.tile = open.wifi > 0.0f ? 0 : (open.bluetooth > 0.0f ? 1 : -1);
  if (module.tile >= 0) {
    module.t = std::max(module.tile == 0 ? open.wifi : open.bluetooth, 0.0f);
    const bool on = module.tile == 0 ? state.wifi.on : state.bluetooth.on;
    const size_t items = module.tile == 0 ? state.wifi.networks.size() : state.bluetooth.devices.size();
    const size_t rows = std::max<size_t>(on ? std::min(items, kModuleMaxRows) : 0, 1);

    float row = gridTop + kPadDip;
    module.header = D2D1_RECT_F{left + kPadDip, row, right - kPadDip, row + kModuleHeaderDip};
    const float mid = row + kModuleHeaderDip / 2.0f;
    module.toggle = D2D1_RECT_F{module.header.right - kSwitchWidthDip, mid - kSwitchHeightDip / 2.0f,
                                module.header.right, mid + kSwitchHeightDip / 2.0f};
    row += kModuleHeaderDip + 4.0f;
    for (size_t i = 0; i < rows; ++i) {
      module.rows.push_back(D2D1_RECT_F{left + 4.0f, row, right - 4.0f, row + kModuleRowDip});
      row += kModuleRowDip;
    }
    module.footer = D2D1_RECT_F{left + 4.0f, row, right - 4.0f, row + kModuleRowDip};
    row += kModuleRowDip + kGapDip;

    const D2D1_RECT_F full{left, gridTop, right, row};
    const D2D1_RECT_F& from = layout.tiles[static_cast<size_t>(module.tile)];
    const float t = module.t;
    module.card = D2D1_RECT_F{from.left + (full.left - from.left) * t, from.top + (full.top - from.top) * t,
                              from.right + (full.right - from.right) * t,
                              from.bottom + (full.bottom - from.bottom) * t};
    grid = gridHeight + ((row - gridTop) - gridHeight) * t;
  }
  y = gridTop + grid + kGapDip;

  // Brightness. Closed it is one slider; open, one row per screen in its place.
  const float inner = kPadDip;
  const float cardTop = y;
  layout.brightnessHeader =
      D2D1_RECT_F{left + inner, cardTop + inner, right - inner, cardTop + inner + kHeaderHeightDip};
  y = layout.brightnessHeader.bottom + kGapDip;
  layout.brightnessSlider = D2D1_RECT_F{left + inner, y, right - inner, y + kSliderHeightDip};
  const float closedBottom = y + kSliderHeightDip;
  float openBottom = closedBottom;
  if (open.brightness > 0.0f && displays > 0) {
    float row = y;
    for (size_t i = 0; i < displays; ++i) {
      DisplayRow r;
      r.label = D2D1_RECT_F{left + inner, row, right - inner, row + kHeaderHeightDip};
      const float sliderTop = row + kDisplayRowDip - kSliderHeightDip;
      r.slider = D2D1_RECT_F{left + inner, sliderTop, right - inner, sliderTop + kSliderHeightDip};
      layout.displayRows.push_back(r);
      row += kDisplayRowDip + (i + 1 < displays ? kDisplayRowGapDip : 0.0f);
    }
    openBottom = row;
  }
  y = closedBottom + (openBottom - closedBottom) * std::max(open.brightness, 0.0f);
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
  const float audioClosed = y + inner;
  float audioOpen = audioClosed;
  if (open.audio > 0.0f && outputs > 0) {
    float row = y + kGapDip;
    layout.outputsDivider = D2D1_RECT_F{left + inner, row, right - inner, row + 1.0f};
    row += 1.0f + 4.0f;
    for (size_t i = 0; i < outputs; ++i) {
      // The rows run the card's full width, so their hover wash reaches its edges.
      layout.outputRows.push_back(D2D1_RECT_F{left + 4.0f, row, right - 4.0f, row + kOutputRowDip});
      row += kOutputRowDip;
    }
    audioOpen = row + kGapDip / 2.0f;
  }
  y = audioClosed + (audioOpen - audioClosed) * std::max(open.audio, 0.0f);
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
