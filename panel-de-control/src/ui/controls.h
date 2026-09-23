#pragma once

// What the mouse is on and what the keyboard walks through, worked out from the layout alone.
// Pure, like the layout: the drawing and the input read the same rectangles, and the tests
// check both without a window.

#include <d2d1.h>

#include <algorithm>
#include <vector>

#include "model/state.h"
#include "ui/layout.h"

namespace panel {

enum class Part {
  None,
  Tile,              // index 0..3: Wi-Fi, Bluetooth, night light, settings
  BrightnessHeader,  // the title line: opens and closes the card
  BrightnessSlider,  // closed: the monitor the panel opened on
  DisplaySlider,     // open: index is the display
  AudioHeader,
  VolumeSlider,
  Mute,              // the glyph at the left end of the volume slider
  Output,            // index is the output
  App,               // index is the utility
};

struct Target {
  Part part = Part::None;
  size_t index = 0;
  bool operator==(const Target&) const = default;
  bool empty() const { return part == Part::None; }
};

inline bool IsSlider(Part part) {
  return part == Part::BrightnessSlider || part == Part::DisplaySlider ||
         part == Part::VolumeSlider;
}

inline bool Inside(const D2D1_RECT_F& rect, float x, float y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// A row that is still sliding into its card is not there yet for the mouse: only what the card
// shows whole can be clicked.
inline bool Shown(const D2D1_RECT_F& row, const D2D1_RECT_F& card) {
  return row.bottom <= card.bottom + 0.5f;
}

// The value a point on a slider means. The fill is drawn as the pill's height plus the level's
// share of the rest (panel_view.cpp), so its round end is centred on this value: a click puts
// the end of the fill under the pointer, not beside it.
inline float SliderValueAt(const D2D1_RECT_F& slider, float x) {
  const float height = slider.bottom - slider.top;
  const float travel = (slider.right - slider.left) - height;
  if (travel <= 0.0f) return 0.0f;
  return std::clamp((x - slider.left - height / 2.0f) / travel, 0.0f, 1.0f);
}

// The glyph's circle at the left end of the volume slider, which mutes instead of dragging.
inline D2D1_RECT_F MuteRect(const D2D1_RECT_F& slider) {
  return D2D1_RECT_F{slider.left, slider.top, slider.left + (slider.bottom - slider.top),
                     slider.bottom};
}

inline bool Reachable(const PanelState& state, size_t display) {
  return display < state.displays.size() && state.displays[display].reachable;
}

inline bool HereReachable(const PanelState& state) {
  const DisplayState* here = HereDisplay(state);
  return here != nullptr && here->reachable;
}

// Where a target is drawn, for the focus ring. Empty for None.
inline D2D1_RECT_F RectOf(const PanelLayout& layout, Target target) {
  switch (target.part) {
    case Part::Tile:
      return target.index < layout.tiles.size() ? layout.tiles[target.index] : D2D1_RECT_F{};
    case Part::BrightnessHeader:
      return layout.brightnessHeader;
    case Part::BrightnessSlider:
      return layout.brightnessSlider;
    case Part::DisplaySlider:
      return target.index < layout.displayRows.size() ? layout.displayRows[target.index].slider
                                                      : D2D1_RECT_F{};
    case Part::AudioHeader:
      return layout.audioHeader;
    case Part::VolumeSlider:
      return layout.audioSlider;
    case Part::Mute:
      return MuteRect(layout.audioSlider);
    case Part::Output:
      return target.index < layout.outputRows.size() ? layout.outputRows[target.index]
                                                     : D2D1_RECT_F{};
    case Part::App:
      return target.index < layout.apps.size() ? layout.apps[target.index] : D2D1_RECT_F{};
    case Part::None:
    default:
      return D2D1_RECT_F{};
  }
}

inline Target HitTest(const PanelLayout& layout, const PanelState& state, float x, float y) {
  for (size_t i = 0; i < layout.tiles.size(); ++i) {
    if (Inside(layout.tiles[i], x, y)) return {Part::Tile, i};
  }
  if (Inside(layout.brightnessHeader, x, y)) return {Part::BrightnessHeader};
  if (!layout.displayRows.empty()) {
    for (size_t i = 0; i < layout.displayRows.size(); ++i) {
      const D2D1_RECT_F& slider = layout.displayRows[i].slider;
      if (Reachable(state, i) && Shown(slider, layout.brightnessCard) && Inside(slider, x, y)) {
        return {Part::DisplaySlider, i};
      }
    }
  } else if (HereReachable(state) && Inside(layout.brightnessSlider, x, y)) {
    return {Part::BrightnessSlider};
  }
  if (Inside(layout.audioHeader, x, y)) return {Part::AudioHeader};
  if (state.audio.available && Inside(layout.audioSlider, x, y)) {
    return Inside(MuteRect(layout.audioSlider), x, y) ? Target{Part::Mute} : Target{Part::VolumeSlider};
  }
  for (size_t i = 0; i < layout.outputRows.size(); ++i) {
    const D2D1_RECT_F& row = layout.outputRows[i];
    if (Shown(row, layout.audioCard) && Inside(row, x, y)) return {Part::Output, i};
  }
  for (size_t i = 0; i < layout.apps.size() && i < state.apps.size(); ++i) {
    if (state.apps[i].installed && Inside(layout.apps[i], x, y)) return {Part::App, i};
  }
  return {};
}

// What Tab walks through, in reading order. Mute is not in it: on the volume slider, Space
// mutes, so the keyboard reaches it without a stop of its own.
inline std::vector<Target> FocusOrder(const PanelLayout& layout, const PanelState& state) {
  std::vector<Target> order;
  for (size_t i = 0; i < layout.tiles.size(); ++i) order.push_back({Part::Tile, i});
  order.push_back({Part::BrightnessHeader});
  if (!layout.displayRows.empty()) {
    for (size_t i = 0; i < layout.displayRows.size(); ++i) {
      if (Reachable(state, i) && Shown(layout.displayRows[i].slider, layout.brightnessCard)) {
        order.push_back({Part::DisplaySlider, i});
      }
    }
  } else if (HereReachable(state)) {
    order.push_back({Part::BrightnessSlider});
  }
  order.push_back({Part::AudioHeader});
  if (state.audio.available) order.push_back({Part::VolumeSlider});
  for (size_t i = 0; i < layout.outputRows.size(); ++i) {
    if (Shown(layout.outputRows[i], layout.audioCard)) order.push_back({Part::Output, i});
  }
  for (size_t i = 0; i < layout.apps.size() && i < state.apps.size(); ++i) {
    if (state.apps[i].installed) order.push_back({Part::App, i});
  }
  return order;
}

// The next (or previous) stop after `from`, wrapping around. From nothing, the first (or last).
inline Target NextFocus(const std::vector<Target>& order, Target from, bool backwards) {
  if (order.empty()) return {};
  const auto found = std::find(order.begin(), order.end(), from);
  if (found == order.end()) return backwards ? order.back() : order.front();
  const size_t at = static_cast<size_t>(found - order.begin());
  const size_t count = order.size();
  return order[backwards ? (at + count - 1) % count : (at + 1) % count];
}

}  // namespace panel
