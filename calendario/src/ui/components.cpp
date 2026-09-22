#include "ui/components.h"

#include <algorithm>
#include <format>
#include <string>

#include "ui/layout.h"

using Microsoft::WRL::ComPtr;

namespace agenda {
namespace {

constexpr float kInputPadDip = 18.0f;
constexpr float kCaretWidthDip = 1.0f;
constexpr float kCaretInsetDip = 9.0f;  // leaves an 18 DIP caret inside the 36 DIP capsule
constexpr float kCardTextLeft = 14.0f;
constexpr float kCardTimeWidth = 40.0f;
constexpr float kCardTitleLeft = 62.0f;
constexpr float kCardRightPad = 12.0f;
constexpr float kCounterWidth = 30.0f;
constexpr float kDayCenterY = 15.0f;  // from the top of the cell
constexpr float kDotCenterY = 30.0f;  // three DIP under its own circle, seven above the next

std::wstring FormatTime(int minutes) {
  return std::format(L"{:02}:{:02}", minutes / 60, minutes % 60);
}

// What the input shows: the text with any in-flight IME composition spliced in at the caret.
// The drawing and the hit testing both build one of these, so a click lands on the glyph it
// looks like it lands on even when the text is scrolled.
struct InputLayout {
  ComPtr<IDWriteTextLayout> layout;
  std::wstring display;
  UINT32 caretIndex = 0;
  float caretX = 0.0f;
  float scroll = 0.0f;  // how far the text is pushed left to keep the caret in view
  D2D1_RECT_F inner{};
};

bool BuildInputLayout(const Fonts& fonts, const PopupModel& model, InputLayout& out) {
  const D2D1_RECT_F rect = InputRect();
  out.inner =
      D2D1_RECT_F{rect.left + kInputPadDip, rect.top, rect.right - kInputPadDip, rect.bottom};
  if (!fonts.ok()) return false;

  const std::wstring& text = model.input.text();
  const size_t caret = model.input.caret();
  out.display = text.substr(0, caret) + model.composition + text.substr(caret);
  out.caretIndex = static_cast<UINT32>(caret + model.composition.size());
  if (out.display.empty()) return true;  // no layout needed; the caret sits at the left edge

  if (FAILED(fonts.factory->CreateTextLayout(
          out.display.c_str(), static_cast<UINT32>(out.display.size()), fonts.event.Get(),
          4096.0f, rect.bottom - rect.top, &out.layout))) {
    return false;
  }
  // The shared format carries whatever alignment and trimming the last DrawTextIn left on it,
  // so the layout says what it wants: one line, from the left, never ellipsised.
  out.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  const DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
  out.layout->SetTrimming(&none, nullptr);
  if (!model.composition.empty()) {
    out.layout->SetUnderline(
        TRUE, DWRITE_TEXT_RANGE{static_cast<UINT32>(caret),
                                static_cast<UINT32>(model.composition.size())});
  }

  float y = 0.0f;
  DWRITE_HIT_TEST_METRICS metrics{};
  out.layout->HitTestTextPosition(out.caretIndex, FALSE, &out.caretX, &y, &metrics);
  const float width = out.inner.right - out.inner.left;
  out.scroll = (std::max)(0.0f, out.caretX - width + kCaretWidthDip);
  return true;
}

}  // namespace

void DrawMonthGrid(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   ID2D1SolidColorBrush* brush, Month month, Date today, Date selected,
                   const float* hover, float offsetX) {
  const float radius = kDayCircleDip / 2.0f;

  for (int cell = 0; cell < kGridCells; ++cell) {
    const Date date = CellDate(month, cell);
    const D2D1_RECT_F base = CellRect(cell);
    const D2D1_RECT_F rect{base.left + offsetX, base.top, base.right + offsetX, base.bottom};
    const D2D1_POINT_2F center{(rect.left + rect.right) / 2.0f, rect.top + kDayCenterY};

    const bool inMonth = date.year() == month.year() && date.month() == month.month();
    const bool isToday = date == today;
    const bool isSelected = date == selected;

    const float fade = hover != nullptr ? hover[cell] : 0.0f;
    if (fade > 0.0f) {
      brush->SetColor(Fade(theme.hover, fade));
      FillCircle(target, center, radius, brush);
    }

    if (isToday) {
      brush->SetColor(theme.accent);
      FillCircle(target, center, radius, brush);
    } else if (isSelected) {
      // A ring rather than a fill, so the selected day never competes with today.
      brush->SetColor(theme.accent);
      target->DrawEllipse(D2D1_ELLIPSE{center, radius - 0.75f, radius - 0.75f}, brush, 1.5f);
    }

    brush->SetColor(isToday ? theme.onAccent : (inMonth ? theme.textPrimary : theme.textMuted));
    DrawTextIn(target, fonts.day.Get(), std::to_wstring(static_cast<unsigned>(date.day())),
               D2D1_RECT_F{rect.left, center.y - radius, rect.right, center.y + radius}, brush,
               Align::Center);

    if (const std::optional<std::uint32_t> color = SampleDayColor(date)) {
      brush->SetColor(Rgb(*color, inMonth ? 1.0f : 0.40f));
      FillCircle(target, D2D1_POINT_2F{center.x, rect.top + kDotCenterY}, kEventDotDip / 2.0f,
                 brush);
    }
  }
}

void DrawEventCard(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   ID2D1SolidColorBrush* brush, const D2D1_RECT_F& rect,
                   const SampleEvent& event, int more) {
  brush->SetColor(theme.surface);
  FillRound(target, rect, kRadiusCard, brush);

  // The colour bar inherits the rounded corners: clip to its three DIP and fill the whole
  // rounded rectangle again, this time in the calendar colour.
  target->PushAxisAlignedClip(
      D2D1_RECT_F{rect.left, rect.top, rect.left + kEventBarDip, rect.bottom},
      D2D1_ANTIALIAS_MODE_ALIASED);
  brush->SetColor(Rgb(event.color));
  FillRound(target, rect, kRadiusCard, brush);
  target->PopAxisAlignedClip();

  float right = rect.right - kCardRightPad;
  if (more > 0) {
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.event.Get(), std::format(L"+{}", more),
               D2D1_RECT_F{right - kCounterWidth, rect.top, right, rect.bottom}, brush,
               Align::Right);
    right -= kCounterWidth + kGapDip * 2.0f;
  }

  brush->SetColor(theme.textSecondary);
  DrawTextIn(target, fonts.event.Get(), FormatTime(event.startMin),
             D2D1_RECT_F{rect.left + kCardTextLeft, rect.top,
                         rect.left + kCardTextLeft + kCardTimeWidth, rect.bottom},
             brush);

  brush->SetColor(theme.textPrimary);
  DrawTextIn(target, fonts.event.Get(), event.title,
             D2D1_RECT_F{rect.left + kCardTitleLeft, rect.top, right, rect.bottom}, brush);
}

void DrawTextInput(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   ID2D1SolidColorBrush* brush, const PopupModel& model) {
  const D2D1_RECT_F rect = InputRect();

  brush->SetColor(theme.surface);
  FillRound(target, rect, kInputRadius, brush);
  brush->SetColor(theme.border);
  StrokeRound(target, rect, kInputRadius, brush, 1.0f);
  if (model.focus > 0.0f) {
    brush->SetColor(Fade(theme.accent, model.focus));
    StrokeRound(target, rect, kInputRadius, brush, 1.5f);
  }

  InputLayout input;
  if (!BuildInputLayout(fonts, model, input)) return;

  target->PushAxisAlignedClip(input.inner, D2D1_ANTIALIAS_MODE_ALIASED);

  if (input.layout) {
    const float originX = input.inner.left - input.scroll;

    if (model.input.hasSelection()) {
      const UINT32 start = static_cast<UINT32>(model.input.selectionStart());
      const UINT32 length = static_cast<UINT32>(model.input.selectionEnd() - start);
      DWRITE_HIT_TEST_METRICS boxes[4]{};
      UINT32 count = 0;
      input.layout->HitTestTextRange(start, length, originX, rect.top, boxes, 4, &count);
      brush->SetColor(theme.selection);
      for (UINT32 i = 0; i < (std::min)(count, 4u); ++i) {
        target->FillRectangle(
            D2D1_RECT_F{boxes[i].left, boxes[i].top, boxes[i].left + boxes[i].width,
                        boxes[i].top + boxes[i].height},
            brush);
      }
    }

    brush->SetColor(theme.textPrimary);
    target->DrawTextLayout(D2D1_POINT_2F{originX, rect.top}, input.layout.Get(), brush);
  } else {
    brush->SetColor(theme.textMuted);
    DrawTextIn(target, fonts.event.Get(), L"mañana 5pm dentista…", input.inner, brush);
  }

  if (model.caretOn) {
    const float x = input.inner.left + input.caretX - input.scroll;
    brush->SetColor(theme.textPrimary);
    target->FillRectangle(D2D1_RECT_F{x, rect.top + kCaretInsetDip, x + kCaretWidthDip,
                                      rect.bottom - kCaretInsetDip},
                          brush);
  }

  target->PopAxisAlignedClip();
}

size_t InputIndexAt(const Fonts& fonts, const PopupModel& model, float x) {
  InputLayout input;
  if (!BuildInputLayout(fonts, model, input) || !input.layout) return model.input.text().size();

  const D2D1_RECT_F rect = InputRect();
  BOOL trailing = FALSE;
  BOOL inside = FALSE;
  DWRITE_HIT_TEST_METRICS metrics{};
  if (FAILED(input.layout->HitTestPoint(x - input.inner.left + input.scroll,
                                        (rect.bottom - rect.top) / 2.0f, &trailing, &inside,
                                        &metrics))) {
    return model.input.text().size();
  }
  const size_t index = metrics.textPosition + (trailing ? metrics.length : 0);
  return (std::min)(index, model.input.text().size());
}

D2D1_POINT_2F InputCaretPoint(const Fonts& fonts, const PopupModel& model) {
  InputLayout input;
  BuildInputLayout(fonts, model, input);
  return D2D1_POINT_2F{input.inner.left + input.caretX - input.scroll, InputRect().bottom};
}

}  // namespace agenda
