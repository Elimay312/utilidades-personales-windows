#pragma once

#include <windows.h>

#include <d2d1.h>

#include <algorithm>
#include <cmath>

#include "core/dates.h"
#include "ui/theme.h"

namespace agenda {

// The panel is not one fixed size. It takes a share of the monitor's work area so a laptop
// screen and an ultrawide both get a popup that feels the same size next to everything around
// it. The proportions are still the 340x420 of the design system, which is the size every
// number below is written at.
inline constexpr float kPanelBaseWidthDip = 340.0f;
inline constexpr float kPanelBaseHeightDip = 420.0f;
inline constexpr float kPanelAspect = kPanelBaseWidthDip / kPanelBaseHeightDip;
inline constexpr float kPanelHeightShare = 0.42f;
inline constexpr float kPanelMinHeightDip = 380.0f;
inline constexpr float kPanelMaxHeightDip = 560.0f;

// Text, cards and padding grow with the panel, one for one: a bigger popup has to mean bigger
// content, not the same content floating in more empty panel. The limits only catch the sizes
// --panel can force by hand, where the letters would stop being readable either way.
inline constexpr float kTypeMinScale = 0.80f;
inline constexpr float kTypeMaxScale = 2.00f;

// Card metrics at the size the design system is written at. They live here, with the rest of
// the measurements, because the checkbox on a task has to be drawn and hit-tested from the
// same numbers -- the rule the month grid already follows.
inline constexpr float kCardTextLeftDip = 14.0f;
inline constexpr float kCardTimeWidthDip = 40.0f;
inline constexpr float kCardTitleLeftDip = 62.0f;
inline constexpr float kCardRightPadDip = 12.0f;
inline constexpr float kCounterWidthDip = 30.0f;
inline constexpr float kCheckboxDip = 14.0f;

inline constexpr int kPopupMarginDip = 12;
inline constexpr float kPopupSlideDip = 8.0f;  // how far the popup rises while opening

inline int ScaleDip(int dip, UINT dpi) {
  return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

inline int ScaleDip(float dip, UINT dpi) {
  return static_cast<int>(std::lround(dip * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
}

// The panel size in DIP for a work area measured in physical pixels.
inline D2D1_SIZE_F PanelSize(const RECT& work, UINT dpi) {
  const float toDip = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi);
  const float workWidth = static_cast<float>(work.right - work.left) * toDip;
  const float workHeight = static_cast<float>(work.bottom - work.top) * toDip;
  const float margin = 2.0f * kPopupMarginDip;

  float height =
      std::clamp(workHeight * kPanelHeightShare, kPanelMinHeightDip, kPanelMaxHeightDip);
  height = std::min(height, workHeight - margin);
  const float width = std::min(height * kPanelAspect, workWidth - margin);
  return D2D1_SIZE_F{std::round(width), std::round(height)};
}

// The popup sits in the bottom right corner of the work area, one margin away from both
// edges, which puts it over the taskbar corner without covering it.
inline RECT PlaceRect(const RECT& work, D2D1_SIZE_F size, UINT dpi) {
  const int margin = ScaleDip(kPopupMarginDip, dpi);
  const int right = work.right - margin;
  const int bottom = work.bottom - margin;
  return RECT{right - ScaleDip(size.width, dpi), bottom - ScaleDip(size.height, dpi), right,
              bottom};
}

inline RECT PopupRect(const RECT& work, UINT dpi) {
  return PlaceRect(work, PanelSize(work, dpi), dpi);
}

// The expanded app: this share of the work area, centred on it.
inline constexpr float kAppShare = 0.80f;

inline RECT ExpandedRect(const RECT& work) {
  const int workWidth = work.right - work.left;
  const int workHeight = work.bottom - work.top;
  const int width = static_cast<int>(std::lround(static_cast<float>(workWidth) * kAppShare));
  const int height = static_cast<int>(std::lround(static_cast<float>(workHeight) * kAppShare));
  const int left = work.left + (workWidth - width) / 2;
  const int top = work.top + (workHeight - height) / 2;
  return RECT{left, top, left + width, top + height};
}

// Every rectangle inside the panel, worked out once from its size. The drawing and the hit
// testing both read this one, so a click always lands where the pixel is.
struct PanelLayout {
  float width = kPanelBaseWidthDip;
  float height = kPanelBaseHeightDip;
  float type = 1.0f;  // the panel size over the one the design was drawn at

  float padding = 0.0f;
  float gap = 0.0f;
  float contentLeft = 0.0f;
  float contentRight = 0.0f;
  float contentWidth = 0.0f;

  float headerTop = 0.0f;
  float headerHeight = 0.0f;
  float arrowSize = 0.0f;

  float weekdayTop = 0.0f;
  float weekdayHeight = 0.0f;

  float gridTop = 0.0f;
  float cellWidth = 0.0f;
  float cellHeight = 0.0f;
  float gridHeight = 0.0f;
  float dayCircle = 0.0f;
  float dayCenterY = 0.0f;  // both measured from the top of the cell
  float dotCenterY = 0.0f;
  float eventDot = 0.0f;

  float listTop = 0.0f;
  float listHeight = 0.0f;
  float cardHeight = 0.0f;
  int visibleCards = 2;

  float inputTop = 0.0f;
  // The capsule's own left and right. In the popup they are the content edges; the expansion
  // moves the capsule to the top of the app, and this is what lets it travel.
  float inputLeft = 0.0f;
  float inputRight = 0.0f;
  // The preview and the notice sit above the capsule in the popup. In the app the capsule is
  // at the top, so they hang below it instead.
  bool previewBelow = false;
  float inputHeight = 0.0f;
  float inputRadius = 0.0f;
  float inputPad = 0.0f;
  float previewHeight = 0.0f;

  float panelRadius = 0.0f;
  float cardRadius = 0.0f;
  float barWidth = 0.0f;

  float fontLabel = kFontLabel;
  float fontDay = kFontDay;
  float fontEvent = kFontEvent;
  float fontTitle = kFontTitle;

  D2D1_SIZE_F size() const { return D2D1_SIZE_F{width, height}; }
  D2D1_RECT_F panel() const { return D2D1_RECT_F{0.0f, 0.0f, width, height}; }

  D2D1_RECT_F grid() const {
    return D2D1_RECT_F{contentLeft, gridTop, contentRight, gridTop + gridHeight};
  }

  D2D1_RECT_F cell(int index) const {
    const float left = contentLeft + static_cast<float>(index % kGridCols) * cellWidth;
    const float top = gridTop + static_cast<float>(index / kGridCols) * cellHeight;
    return D2D1_RECT_F{left, top, left + cellWidth, top + cellHeight};
  }

  D2D1_RECT_F nextArrow() const {
    return D2D1_RECT_F{contentRight - arrowSize, headerTop, contentRight, headerTop + arrowSize};
  }

  D2D1_RECT_F prevArrow() const {
    const float right = contentRight - arrowSize - gap;
    return D2D1_RECT_F{right - arrowSize, headerTop, right, headerTop + arrowSize};
  }

  D2D1_RECT_F list() const {
    return D2D1_RECT_F{contentLeft, listTop, contentRight, listTop + listHeight};
  }

  D2D1_RECT_F input() const {
    return D2D1_RECT_F{inputLeft, inputTop, inputRight, inputTop + inputHeight};
  }

  // The live preview sits on top of the capsule, over the tail of the list. It is not given
  // room of its own: reserving a row would shrink the month grid for good, and the grid must
  // not move the moment someone starts typing.
  D2D1_RECT_F preview() const {
    if (previewBelow) {
      const float top = inputTop + inputHeight + gap;
      return D2D1_RECT_F{inputLeft, top, inputRight, top + previewHeight};
    }
    const float bottom = inputTop - gap;
    return D2D1_RECT_F{inputLeft, bottom - previewHeight, inputRight, bottom};
  }
};

inline constexpr bool Inside(const D2D1_RECT_F& rect, float x, float y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// The room the day list's cards get: the whole list normally, and whatever is left above the
// card-sized slot when something else is using it -- the live preview while somebody types, or
// the notice after Enter. Both live in the same rectangle and neither may sit on top of a card.
inline D2D1_RECT_F DayListRect(const PanelLayout& layout, bool slotTaken) {
  D2D1_RECT_F list = layout.list();
  if (slotTaken) list.bottom = std::max(list.top, layout.preview().top - layout.gap);
  return list;
}

// Which cards of the day are on screen and where they land. Worked out here and not inside the
// drawing, so a click on a checkbox cannot disagree with the box it looks like it hit.
struct CardSlots {
  int first = 0;  // index in the day's list of the first card shown
  int shown = 0;
  float top = 0.0f;
  float stride = 0.0f;
};

// `keep` is the index that has to stay on screen -- the card that was just created -- or -1.
// Without it, creating something on a day that was already full would animate a card nobody
// can see.
inline CardSlots PlaceCards(const PanelLayout& layout, const D2D1_RECT_F& list, int total,
                            int keep) {
  CardSlots out;
  out.stride = layout.cardHeight + layout.gap;
  const float room = list.bottom - list.top;
  const int fits =
      std::clamp(static_cast<int>((room + layout.gap) / out.stride), 0, layout.visibleCards);
  out.shown = std::min(total, fits);
  if (out.shown <= 0) return out;
  if (keep >= out.shown) out.first = std::min(keep - out.shown + 1, total - out.shown);
  // The cards sit in the middle of whatever room the grid left, so the leftover never piles up
  // against the capsule.
  out.top = list.top + std::max(0.0f, (room - (static_cast<float>(out.shown) * out.stride -
                                               layout.gap)) /
                                          2.0f);
  return out;
}

inline D2D1_RECT_F CardRect(const PanelLayout& layout, const D2D1_RECT_F& list,
                            const CardSlots& slots, int position) {
  const float top = slots.top + static_cast<float>(position) * slots.stride;
  return D2D1_RECT_F{list.left, top, list.right, top + layout.cardHeight};
}

// The tick box on a task, in the column an event puts its clock in.
inline D2D1_RECT_F CheckboxRect(const PanelLayout& layout, const D2D1_RECT_F& card) {
  const float side = std::round(kCheckboxDip * layout.type);
  const float left = card.left + std::round(kCardTextLeftDip * layout.type);
  const float top = std::round((card.top + card.bottom - side) / 2.0f);
  return D2D1_RECT_F{left, top, left + side, top + side};
}

// Worked out bottom up: the capsule is pinned above the lower padding, the list keeps room for
// at least two cards, and what is left over goes to the six rows of the month. At 340x420 every
// number below comes out exactly as the design system writes it.
inline PanelLayout MakeLayout(D2D1_SIZE_F size) {
  PanelLayout out;
  out.width = size.width;
  out.height = size.height;
  out.type = std::clamp(size.height / kPanelBaseHeightDip, kTypeMinScale, kTypeMaxScale);

  const float type = out.type;
  const auto at = [type](float dip) { return std::round(dip * type); };

  out.padding = at(kPaddingDip);
  out.gap = at(kGapDip);
  out.contentLeft = out.padding;
  out.contentRight = out.width - out.padding;
  out.contentWidth = out.contentRight - out.contentLeft;

  out.headerTop = out.padding;
  out.headerHeight = at(28.0f);
  out.arrowSize = at(28.0f);

  out.weekdayTop = out.headerTop + out.headerHeight + out.gap;
  out.weekdayHeight = at(16.0f);
  out.gridTop = out.weekdayTop + out.weekdayHeight + out.gap;

  out.cardHeight = at(32.0f);
  out.inputHeight = at(36.0f);
  out.inputRadius = out.inputHeight / 2.0f;
  out.inputPad = at(18.0f);
  out.inputTop = out.height - out.padding - out.inputHeight;
  out.inputLeft = out.contentLeft;
  out.inputRight = out.contentRight;
  out.previewHeight = at(30.0f);

  const float breath = at(8.0f);
  const float listBottom = out.inputTop - breath;
  const float twoCards = 2.0f * out.cardHeight + out.gap;
  const float gridRoom = listBottom - twoCards - breath - out.gridTop;

  out.cellWidth = out.contentWidth / kGridCols;
  out.cellHeight = std::clamp(
      std::floor(std::min({out.cellWidth * (36.0f / 44.0f), at(40.0f), gridRoom / kGridRows})),
      at(26.0f), at(56.0f));
  out.gridHeight = out.cellHeight * kGridRows;

  out.dayCircle = std::round(std::min(out.cellHeight - at(12.0f), out.cellWidth - at(14.0f)));
  out.dayCenterY = std::round(out.dayCircle / 2.0f + at(3.0f));
  out.eventDot = at(kEventDotDip);
  out.dotCenterY = out.dayCenterY + out.dayCircle / 2.0f + out.eventDot / 2.0f + at(1.0f);

  out.listTop = out.gridTop + out.gridHeight + breath;
  out.listHeight = std::max(0.0f, listBottom - out.listTop);
  out.visibleCards = std::clamp(
      static_cast<int>((out.listHeight + out.gap) / (out.cardHeight + out.gap)), 1, 6);

  out.panelRadius = at(kRadiusPanel);
  out.cardRadius = at(kRadiusCard);
  out.barWidth = std::max(3.0f, at(kEventBarDip));

  out.fontLabel = kFontLabel * type;
  out.fontDay = kFontDay * type;
  out.fontEvent = kFontEvent * type;
  out.fontTitle = kFontTitle * type;
  return out;
}

// The size the design was drawn at, which is what the snapshots render so a committed PNG never
// depends on the monitor that produced it.
inline PanelLayout BaseLayout() {
  return MakeLayout(D2D1_SIZE_F{kPanelBaseWidthDip, kPanelBaseHeightDip});
}

}  // namespace agenda
