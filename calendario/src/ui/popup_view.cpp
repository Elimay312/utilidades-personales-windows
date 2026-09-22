#include "ui/popup_view.h"

#include <wrl/client.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "ui/components.h"
#include "ui/layout.h"

using Microsoft::WRL::ComPtr;

namespace agenda {
namespace {

// Chevron metrics at the size the design system is written at.
constexpr float kChevronHalfWidth = 3.5f;
constexpr float kChevronHalfHeight = 6.0f;
constexpr float kChevronStroke = 1.5f;

void DrawChevron(ID2D1RenderTarget* target, const Theme& theme, const PanelLayout& layout,
                 ID2D1SolidColorBrush* brush, const D2D1_RECT_F& rect, bool pointsLeft,
                 float hover, ID2D1StrokeStyle* style) {
  if (hover > 0.0f) {
    brush->SetColor(Fade(theme.hover, hover));
    FillCircle(target, Center(rect), layout.arrowSize / 2.0f, brush);
  }

  // Two strokes instead of a glyph: at this size a drawn chevron lands on the pixel grid the
  // same way every time, whatever font the machine ended up with.
  const float type = layout.type;
  const D2D1_POINT_2F center = Center(rect);
  const float side = (pointsLeft ? kChevronHalfWidth : -kChevronHalfWidth) * type;
  const float reach = kChevronHalfHeight * type;
  brush->SetColor(Lerp(theme.textSecondary, theme.textPrimary, hover));
  target->DrawLine(D2D1_POINT_2F{center.x + side, center.y - reach},
                   D2D1_POINT_2F{center.x - side, center.y}, brush, kChevronStroke * type, style);
  target->DrawLine(D2D1_POINT_2F{center.x - side, center.y},
                   D2D1_POINT_2F{center.x + side, center.y + reach}, brush, kChevronStroke * type,
                   style);
}

void DrawHeader(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                const PanelLayout& layout, ID2D1SolidColorBrush* brush, const PopupModel& model,
                ID2D1StrokeStyle* style) {
  const std::wstring title = std::format(L"{} {}", MonthName(model.month.month()),
                                         static_cast<int>(model.month.year()));
  brush->SetColor(theme.textPrimary);
  DrawTextIn(target, fonts.title.Get(), title,
             D2D1_RECT_F{layout.contentLeft, layout.headerTop,
                         layout.prevArrow().left - layout.gap,
                         layout.headerTop + layout.headerHeight},
             brush);

  DrawChevron(target, theme, layout, brush, layout.prevArrow(), true, model.prevHover, style);
  DrawChevron(target, theme, layout, brush, layout.nextArrow(), false, model.nextHover, style);
}

void DrawWeekdays(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                  const PanelLayout& layout, ID2D1SolidColorBrush* brush) {
  brush->SetColor(theme.textSecondary);
  for (int column = 0; column < kGridCols; ++column) {
    const float left = layout.contentLeft + static_cast<float>(column) * layout.cellWidth;
    DrawTextIn(target, fonts.label.Get(), kWeekdayInitials[column],
               D2D1_RECT_F{left, layout.weekdayTop, left + layout.cellWidth,
                           layout.weekdayTop + layout.weekdayHeight},
               brush, Align::Center);
  }
}

void DrawGrid(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
              const PanelLayout& layout, ID2D1SolidColorBrush* brush, const PopupModel& model) {
  target->PushAxisAlignedClip(layout.grid(), D2D1_ANTIALIAS_MODE_ALIASED);

  if (model.slideDir != 0 && model.slideT < 1.0f) {
    // Both months are on screen: the one arriving walks in from the side the arrow points to,
    // the one leaving walks out the other way, one content width apart the whole time.
    const float width = static_cast<float>(model.slideDir) * layout.contentWidth;
    const float offset = width * (1.0f - EaseOutCubic(model.slideT));
    DrawMonthGrid(target, fonts, theme, layout, brush, model.slideFrom, model.today,
                  model.selected, model.dots, nullptr, offset - width);
    DrawMonthGrid(target, fonts, theme, layout, brush, model.month, model.today, model.selected,
                  model.dots, nullptr, offset);
  } else {
    DrawMonthGrid(target, fonts, theme, layout, brush, model.month, model.today, model.selected,
                  model.dots, model.dayHover, 0.0f);
  }

  target->PopAxisAlignedClip();
}

// `list` is the room the cards actually get: the whole list normally, and whatever is left
// above the preview card while someone is typing.
void DrawEventList(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                   const PopupModel& model, const D2D1_RECT_F& list) {
  const int total = static_cast<int>(model.day.size());
  if (total == 0) {
    brush->SetColor(theme.textMuted);
    DrawTextIn(target, fonts.event.Get(), L"Sin eventos", list, brush, Align::Center);
    return;
  }

  // Whatever was just created has to be one of the cards on screen, or its arrival would be
  // animated where nobody can see it.
  int entering = -1;
  for (int i = 0; i < total; ++i) {
    if (!model.enterUid.empty() && model.day[i].uid == model.enterUid) entering = i;
  }

  const CardSlots slots = PlaceCards(layout, list, total, entering);
  if (slots.shown == 0) return;

  for (int position = 0; position < slots.shown; ++position) {
    const int index = slots.first + position;
    const DayItem& item = model.day[index];
    D2D1_RECT_F rect = CardRect(layout, list, slots, position);

    // A finished task is already struck through; the one that was just ticked is the only one
    // whose line is still drawing itself.
    const float strike =
        item.uid == model.strikeUid ? model.strikeT : (item.done ? 1.0f : 0.0f);
    // The last card carries the count of whatever did not fit.
    const int more = position == slots.shown - 1 ? total - slots.shown : 0;

    const bool arriving = index == entering && model.enterT < 1.0f;
    if (!arriving) {
      DrawEventCard(target, fonts, theme, layout, brush, rect, item, more, strike);
      continue;
    }

    // It rises into place and fades in, the same eight DIP and the same curve the whole panel
    // uses when it opens: one vocabulary of movement, not two.
    const float eased = EaseOutCubic(model.enterT);
    const float lift = kPopupSlideDip * layout.type * (1.0f - eased);
    rect.top += lift;
    rect.bottom += lift;

    ComPtr<ID2D1Layer> layer;
    if (FAILED(target->CreateLayer(nullptr, &layer))) {
      DrawEventCard(target, fonts, theme, layout, brush, rect, item, more, strike);
      continue;
    }
    target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                            D2D1::IdentityMatrix(), eased),
                      layer.Get());
    DrawEventCard(target, fonts, theme, layout, brush, rect, item, more, strike);
    target->PopLayer();
  }
}

}  // namespace

void DrawPopup(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PanelLayout& layout, const PopupModel& model, bool acrylic) {
  ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(theme.panel, &brush))) return;

  // A brush of its own, because the input hands it to DirectWrite as a drawing effect and it
  // has to still be the accent colour by the time the layout is drawn.
  ComPtr<ID2D1SolidColorBrush> accent;
  if (FAILED(target->CreateSolidColorBrush(theme.accent, &accent))) return;

  // ClearType needs opaque pixels under the glyphs, and this panel is translucent over the
  // acrylic, so both the window and the snapshot antialias text in greyscale.
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

  const D2D1_RECT_F panel = layout.panel();
  brush->SetColor(acrylic ? theme.panel : theme.panelOpaque);
  FillRound(target, panel, layout.panelRadius, brush.Get());
  brush->SetColor(theme.border);
  StrokeRound(target, panel, layout.panelRadius, brush.Get(), 1.0f);

  if (!fonts.ok()) return;  // no text formats means an empty panel, not a crash

  ComPtr<ID2D1Factory> factory;
  target->GetFactory(&factory);
  ComPtr<ID2D1StrokeStyle> rounded;
  factory->CreateStrokeStyle(
      D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                  D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
      nullptr, 0, &rounded);

  DrawHeader(target, fonts, theme, layout, brush.Get(), model, rounded.Get());
  DrawWeekdays(target, fonts, theme, layout, brush.Get());
  DrawGrid(target, fonts, theme, layout, brush.Get(), model);
  // While there is something written, the list gives way to the preview: the grid stays put
  // and only the cards nobody is looking at move out of the way.
  const D2D1_RECT_F list = DayListRect(layout, model);

  DrawEventList(target, fonts, theme, layout, brush.Get(), model, list);
  if (ShowingPreview(model)) DrawPreviewCard(target, fonts, theme, layout, brush.Get(), model);
  if (ShowingToast(model)) DrawToast(target, fonts, theme, layout, brush.Get(), model);
  DrawTextInput(target, fonts, theme, layout, brush.Get(), accent.Get(), model);
}

}  // namespace agenda
