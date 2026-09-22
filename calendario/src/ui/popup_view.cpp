#include "ui/popup_view.h"

#include <wrl/client.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "ui/components.h"
#include "ui/layout.h"
#include "ui/sample_data.h"

using Microsoft::WRL::ComPtr;

namespace agenda {
namespace {

constexpr float kChevronHalfWidth = 3.5f;
constexpr float kChevronHalfHeight = 6.0f;
constexpr float kChevronStroke = 1.5f;

void DrawChevron(ID2D1RenderTarget* target, const Theme& theme, ID2D1SolidColorBrush* brush,
                 const D2D1_RECT_F& rect, bool pointsLeft, float hover,
                 ID2D1StrokeStyle* style) {
  if (hover > 0.0f) {
    brush->SetColor(Fade(theme.hover, hover));
    FillCircle(target, Center(rect), kArrowSize / 2.0f, brush);
  }

  // Two strokes instead of a glyph: at this size a drawn chevron lands on the pixel grid the
  // same way every time, whatever font the machine ended up with.
  const D2D1_POINT_2F center = Center(rect);
  const float side = pointsLeft ? kChevronHalfWidth : -kChevronHalfWidth;
  brush->SetColor(Lerp(theme.textSecondary, theme.textPrimary, hover));
  target->DrawLine(D2D1_POINT_2F{center.x + side, center.y - kChevronHalfHeight},
                   D2D1_POINT_2F{center.x - side, center.y}, brush, kChevronStroke, style);
  target->DrawLine(D2D1_POINT_2F{center.x - side, center.y},
                   D2D1_POINT_2F{center.x + side, center.y + kChevronHalfHeight}, brush,
                   kChevronStroke, style);
}

void DrawHeader(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                ID2D1SolidColorBrush* brush, const PopupModel& model, ID2D1StrokeStyle* style) {
  const std::wstring title = std::format(L"{} {}", MonthName(model.month.month()),
                                         static_cast<int>(model.month.year()));
  brush->SetColor(theme.textPrimary);
  DrawTextIn(target, fonts.title.Get(), title,
             D2D1_RECT_F{kContentLeft, kHeaderTop, PrevArrowRect().left - kGapDip,
                         kHeaderTop + kHeaderHeight},
             brush);

  DrawChevron(target, theme, brush, PrevArrowRect(), true, model.prevHover, style);
  DrawChevron(target, theme, brush, NextArrowRect(), false, model.nextHover, style);
}

void DrawWeekdays(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                  ID2D1SolidColorBrush* brush) {
  brush->SetColor(theme.textSecondary);
  for (int column = 0; column < kGridCols; ++column) {
    const float left = kContentLeft + static_cast<float>(column) * kCellWidth;
    DrawTextIn(target, fonts.label.Get(), kWeekdayInitials[column],
               D2D1_RECT_F{left, kWeekdayTop, left + kCellWidth, kWeekdayTop + kWeekdayHeight},
               brush, Align::Center);
  }
}

void DrawGrid(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
              ID2D1SolidColorBrush* brush, const PopupModel& model) {
  target->PushAxisAlignedClip(GridRect(), D2D1_ANTIALIAS_MODE_ALIASED);

  if (model.slideDir != 0 && model.slideT < 1.0f) {
    // Both months are on screen: the one arriving walks in from the side the arrow points to,
    // the one leaving walks out the other way, one content width apart the whole time.
    const float width = static_cast<float>(model.slideDir) * kContentWidth;
    const float offset = width * (1.0f - EaseOutCubic(model.slideT));
    DrawMonthGrid(target, fonts, theme, brush, model.slideFrom, model.today, model.selected,
                  nullptr, offset - width);
    DrawMonthGrid(target, fonts, theme, brush, model.month, model.today, model.selected, nullptr,
                  offset);
  } else {
    DrawMonthGrid(target, fonts, theme, brush, model.month, model.today, model.selected,
                  model.dayHover, 0.0f);
  }

  target->PopAxisAlignedClip();
}

void DrawEventList(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   ID2D1SolidColorBrush* brush, const PopupModel& model) {
  const D2D1_RECT_F list = ListRect();
  const std::vector<SampleEvent> events = SampleEvents(model.selected);

  if (events.empty()) {
    brush->SetColor(theme.textMuted);
    DrawTextIn(target, fonts.event.Get(), L"Sin eventos", list, brush, Align::Center);
    return;
  }

  const int shown = (std::min)(static_cast<int>(events.size()), kVisibleCards);
  for (int i = 0; i < shown; ++i) {
    const float top = list.top + static_cast<float>(i) * (kCardHeight + kGapDip);
    // Only two cards fit between the grid and the input, so the last one carries the count of
    // whatever did not.
    const int more = i == shown - 1 ? static_cast<int>(events.size()) - shown : 0;
    DrawEventCard(target, fonts, theme, brush,
                  D2D1_RECT_F{list.left, top, list.right, top + kCardHeight}, events[i], more);
  }
}

}  // namespace

void DrawPopup(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PopupModel& model, D2D1_SIZE_F sizeDip, bool acrylic) {
  ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(theme.panel, &brush))) return;

  // ClearType needs opaque pixels under the glyphs, and this panel is translucent over the
  // acrylic, so both the window and the snapshot antialias text in greyscale.
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

  const D2D1_RECT_F panel{0.0f, 0.0f, sizeDip.width, sizeDip.height};
  brush->SetColor(acrylic ? theme.panel : theme.panelOpaque);
  FillRound(target, panel, kRadiusPanel, brush.Get());
  brush->SetColor(theme.border);
  StrokeRound(target, panel, kRadiusPanel, brush.Get(), 1.0f);

  if (!fonts.ok()) return;  // no text formats means an empty panel, not a crash

  ComPtr<ID2D1Factory> factory;
  target->GetFactory(&factory);
  ComPtr<ID2D1StrokeStyle> rounded;
  factory->CreateStrokeStyle(
      D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                  D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
      nullptr, 0, &rounded);

  DrawHeader(target, fonts, theme, brush.Get(), model, rounded.Get());
  DrawWeekdays(target, fonts, theme, brush.Get());
  DrawGrid(target, fonts, theme, brush.Get(), model);
  DrawEventList(target, fonts, theme, brush.Get(), model);
  DrawTextInput(target, fonts, theme, brush.Get(), model);
}

}  // namespace agenda
