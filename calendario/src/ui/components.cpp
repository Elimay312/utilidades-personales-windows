#include "ui/components.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace agenda {
namespace {

// The card metrics live in layout.h now, so the checkbox is drawn and hit-tested from the same
// numbers. These two are the caret's and belong to the capsule.
constexpr float kCaretHeight = 18.0f;
constexpr float kCaretWidth = 1.0f;

std::wstring FormatTime(std::optional<int> minutes) {
  // Nothing at all for something that takes the whole day: the column is forty DIP wide and
  // the bar and the title already say what it is.
  if (!minutes) return {};
  return std::format(L"{:02}:{:02}", *minutes / 60, *minutes % 60);
}

// The colour of the dot under a day, or nothing when the day is free. A linear walk over at
// most two grids' worth of days, which is what the sample data did and costs nothing here.
const DayDot* FindDot(const std::vector<DayDot>& dots, Date date) {
  for (const DayDot& dot : dots) {
    if (dot.date == date) return &dot;
  }
  return nullptr;
}

// A tick box. Two strokes rather than a glyph, for the same reason the chevron is: at this
// size a drawn mark lands on the pixel grid the same way whatever font the machine has.
void DrawCheckbox(ID2D1RenderTarget* target, const Theme& theme, const PanelLayout& layout,
                  ID2D1SolidColorBrush* brush, const D2D1_RECT_F& box, float checked) {
  const float side = box.right - box.left;
  const float radius = std::round(4.0f * layout.type);
  const float stroke = (std::max)(1.0f, std::round(1.5f * layout.type));

  if (checked > 0.0f) {
    brush->SetColor(Fade(theme.accent, checked));
    FillRound(target, box, radius, brush);
  }
  brush->SetColor(Lerp(theme.textSecondary, theme.accent, checked));
  StrokeRound(target, box, radius, brush, stroke);

  if (checked <= 0.0f) return;
  brush->SetColor(Fade(theme.onAccent, checked));
  const D2D1_POINT_2F start{box.left + side * 0.26f, box.top + side * 0.52f};
  const D2D1_POINT_2F knee{box.left + side * 0.44f, box.top + side * 0.70f};
  const D2D1_POINT_2F end{box.left + side * 0.75f, box.top + side * 0.32f};
  target->DrawLine(start, knee, brush, stroke);
  target->DrawLine(knee, end, brush, stroke);
}

// The line across a finished task, drawn as a rectangle whose width grows rather than with
// IDWriteTextLayout::SetStrikethrough, which is all or nothing and so cannot be animated.
void StrikeThrough(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                   const D2D1_RECT_F& rect, const std::wstring& text, float progress) {
  if (progress <= 0.0f || text.empty() || !fonts.ok()) return;
  const float room = rect.right - rect.left;
  if (room <= 0.0f) return;

  Microsoft::WRL::ComPtr<IDWriteTextLayout> measured;
  if (FAILED(fonts.factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                             fonts.event.Get(), room, rect.bottom - rect.top,
                                             &measured))) {
    return;
  }
  // The shared format carries whatever the last DrawTextIn left on it, so the measurement says
  // what it wants: one line, from the left.
  measured->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  DWRITE_TEXT_METRICS metrics{};
  if (FAILED(measured->GetMetrics(&metrics))) return;

  const float width = (std::min)(metrics.width, room) * progress;
  const float middle = std::round((rect.top + rect.bottom) / 2.0f);
  const float thickness = (std::max)(1.0f, std::round(layout.type));
  brush->SetColor(theme.textMuted);
  target->FillRectangle(D2D1_RECT_F{rect.left, middle, rect.left + width, middle + thickness},
                        brush);
}

// What the input shows: the text with any in-flight IME composition spliced in at the caret.
// The drawing and the hit testing both build one of these, so a click lands on the glyph it
// looks like it lands on even when the text is scrolled.
struct InputLayout {
  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  std::wstring display;
  UINT32 caretIndex = 0;
  float caretX = 0.0f;
  float scroll = 0.0f;  // how far the text is pushed left to keep the caret in view
  D2D1_RECT_F inner{};
};

bool BuildInputLayout(const Fonts& fonts, const PanelLayout& panel, const PopupModel& model,
                      InputLayout& out) {
  const D2D1_RECT_F rect = panel.input();
  out.inner = D2D1_RECT_F{rect.left + panel.inputPad, rect.top, rect.right - panel.inputPad,
                          rect.bottom};
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
  out.scroll = (std::max)(0.0f, out.caretX - width + kCaretWidth * panel.type);
  return true;
}

// Light up what the parser understood. DrawTextLayout takes an ID2D1Brush set as a drawing
// effect straight off, so a span is one call and no renderer of our own. The in-flight IME
// text is spliced in at the caret, which pushes everything after it along.
void ApplySpans(const InputLayout& input, const PopupModel& model, ID2D1Brush* accent) {
  if (!input.layout) return;
  const size_t caret = model.input.caret();
  const size_t shift = model.composition.size();
  for (const nlp::Span& span : model.preview.spans) {
    const size_t offset = span.offset + (span.offset >= caret ? shift : 0);
    input.layout->SetDrawingEffect(accent,
                                   DWRITE_TEXT_RANGE{static_cast<UINT32>(offset),
                                                     static_cast<UINT32>(span.length)});
  }
}

// Chevron metrics at the size the design system is written at.
constexpr float kChevronHalfWidth = 3.5f;
constexpr float kChevronHalfHeight = 6.0f;
constexpr float kChevronStroke = 1.5f;

}  // namespace

Microsoft::WRL::ComPtr<ID2D1StrokeStyle> RoundedStroke(ID2D1RenderTarget* target) {
  Microsoft::WRL::ComPtr<ID2D1Factory> factory;
  target->GetFactory(&factory);
  Microsoft::WRL::ComPtr<ID2D1StrokeStyle> rounded;
  factory->CreateStrokeStyle(
      D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                  D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
      nullptr, 0, &rounded);
  return rounded;
}

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

void DrawMonthGrid(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush, Month month,
                   Date today, Date selected, const std::vector<DayDot>& dots,
                   const float* hover, float offsetX) {
  const float radius = layout.dayCircle / 2.0f;
  const float ring = 1.5f * layout.type;

  for (int index = 0; index < kGridCells; ++index) {
    const Date date = CellDate(month, index);
    const D2D1_RECT_F base = layout.cell(index);
    const D2D1_RECT_F rect{base.left + offsetX, base.top, base.right + offsetX, base.bottom};
    const D2D1_POINT_2F center{(rect.left + rect.right) / 2.0f, rect.top + layout.dayCenterY};

    const bool inMonth = date.year() == month.year() && date.month() == month.month();
    const bool isToday = date == today;
    const bool isSelected = date == selected;

    const float fade = hover != nullptr ? hover[index] : 0.0f;
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
      target->DrawEllipse(D2D1_ELLIPSE{center, radius - ring / 2.0f, radius - ring / 2.0f}, brush,
                          ring);
    }

    brush->SetColor(isToday ? theme.onAccent : (inMonth ? theme.textPrimary : theme.textMuted));
    DrawTextIn(target, fonts.day.Get(), std::to_wstring(static_cast<unsigned>(date.day())),
               D2D1_RECT_F{rect.left, center.y - radius, rect.right, center.y + radius}, brush,
               Align::Center);

    if (const DayDot* dot = FindDot(dots, date)) {
      brush->SetColor(Rgb(dot->color, inMonth ? 1.0f : 0.40f));
      FillCircle(target, D2D1_POINT_2F{center.x, rect.top + layout.dotCenterY},
                 layout.eventDot / 2.0f, brush);
    }
  }
}

void DrawEventCard(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                   const D2D1_RECT_F& rect, const DayItem& item, int more, float strike) {
  const float type = layout.type;

  brush->SetColor(theme.surface);
  FillRound(target, rect, layout.cardRadius, brush);

  // The colour bar inherits the rounded corners: clip to its width and fill the whole rounded
  // rectangle again, this time in the calendar colour.
  target->PushAxisAlignedClip(
      D2D1_RECT_F{rect.left, rect.top, rect.left + layout.barWidth, rect.bottom},
      D2D1_ANTIALIAS_MODE_ALIASED);
  brush->SetColor(Rgb(item.color));
  FillRound(target, rect, layout.cardRadius, brush);
  target->PopAxisAlignedClip();

  float right = rect.right - kCardRightPadDip * type;
  if (more > 0) {
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.event.Get(), std::format(L"+{}", more),
               D2D1_RECT_F{right - kCounterWidthDip * type, rect.top, right, rect.bottom}, brush,
               Align::Right);
    right -= (kCounterWidthDip + 2.0f * kGapDip) * type;
  }

  if (item.isTask) {
    DrawCheckbox(target, theme, layout, brush, CheckboxRect(layout, rect), strike);
  } else {
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.event.Get(), FormatTime(item.startMin),
               D2D1_RECT_F{rect.left + kCardTextLeftDip * type, rect.top,
                           rect.left + (kCardTextLeftDip + kCardTimeWidthDip) * type,
                           rect.bottom},
               brush);
  }

  // A task with an hour keeps it, in front of the title: the clock column is taken by the box,
  // and nothing the user typed is allowed to quietly disappear.
  const std::wstring title = (item.isTask && item.startMin)
                                 ? std::format(L"{} · {}", FormatTime(item.startMin), item.title)
                                 : item.title;

  const D2D1_RECT_F titleRect{rect.left + kCardTitleLeftDip * type, rect.top, right, rect.bottom};
  brush->SetColor(Lerp(theme.textPrimary, theme.textMuted, strike));
  DrawTextIn(target, fonts.event.Get(), title, titleRect, brush);
  StrikeThrough(target, fonts, theme, layout, brush, titleRect, title, strike);
}

void DrawToast(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PanelLayout& layout, ID2D1SolidColorBrush* brush, const PopupModel& model) {
  const D2D1_RECT_F rect = layout.preview();
  brush->SetColor(Fade(theme.surface, model.toastT));
  FillRound(target, rect, layout.cardRadius, brush);
  brush->SetColor(Fade(theme.textSecondary, model.toastT));
  DrawTextIn(target, fonts.event.Get(), model.toast,
             D2D1_RECT_F{rect.left + kCardTextLeftDip * layout.type, rect.top,
                         rect.right - kCardRightPadDip * layout.type, rect.bottom},
             brush, Align::Center);
}

void DrawPreviewCard(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                     const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                     const PopupModel& model) {
  const D2D1_RECT_F rect = layout.preview();
  const float type = layout.type;

  brush->SetColor(theme.surface);
  FillRound(target, rect, layout.cardRadius, brush);

  // The same bar an event card wears: the accent when this will be an event, muted when it
  // will be a task, so the shape of what Enter would create reads before the words do.
  target->PushAxisAlignedClip(
      D2D1_RECT_F{rect.left, rect.top, rect.left + layout.barWidth, rect.bottom},
      D2D1_ANTIALIAS_MODE_ALIASED);
  brush->SetColor(model.preview.kind == nlp::Kind::Event ? theme.accent : theme.textSecondary);
  FillRound(target, rect, layout.cardRadius, brush);
  target->PopAxisAlignedClip();

  brush->SetColor(theme.textPrimary);
  DrawTextIn(target, fonts.event.Get(), nlp::PreviewText(model.preview, model.today),
             D2D1_RECT_F{rect.left + kCardTextLeftDip * type, rect.top,
                         rect.right - kCardRightPadDip * type, rect.bottom},
             brush);
}

void DrawTextInput(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                   ID2D1SolidColorBrush* accent, const PopupModel& model) {
  const D2D1_RECT_F rect = layout.input();

  brush->SetColor(theme.surface);
  FillRound(target, rect, layout.inputRadius, brush);
  brush->SetColor(theme.border);
  StrokeRound(target, rect, layout.inputRadius, brush, 1.0f);
  if (model.focus > 0.0f) {
    brush->SetColor(Fade(theme.accent, model.focus));
    StrokeRound(target, rect, layout.inputRadius, brush, 1.5f * layout.type);
  }

  InputLayout input;
  if (!BuildInputLayout(fonts, layout, model, input)) return;
  ApplySpans(input, model, accent);

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
    const float inset = (layout.inputHeight - kCaretHeight * layout.type) / 2.0f;
    brush->SetColor(theme.textPrimary);
    target->FillRectangle(D2D1_RECT_F{x, rect.top + inset, x + kCaretWidth * layout.type,
                                      rect.bottom - inset},
                          brush);
  }

  target->PopAxisAlignedClip();
}

size_t InputIndexAt(const Fonts& fonts, const PanelLayout& layout, const PopupModel& model,
                    float x) {
  InputLayout input;
  if (!BuildInputLayout(fonts, layout, model, input) || !input.layout) {
    return model.input.text().size();
  }

  BOOL trailing = FALSE;
  BOOL inside = FALSE;
  DWRITE_HIT_TEST_METRICS metrics{};
  if (FAILED(input.layout->HitTestPoint(x - input.inner.left + input.scroll,
                                        layout.inputHeight / 2.0f, &trailing, &inside,
                                        &metrics))) {
    return model.input.text().size();
  }
  const size_t index = metrics.textPosition + (trailing ? metrics.length : 0);
  return (std::min)(index, model.input.text().size());
}

D2D1_POINT_2F InputCaretPoint(const Fonts& fonts, const PanelLayout& layout,
                              const PopupModel& model) {
  InputLayout input;
  BuildInputLayout(fonts, layout, model, input);
  return D2D1_POINT_2F{input.inner.left + input.caretX - input.scroll,
                       layout.inputTop + layout.inputHeight};
}

}  // namespace agenda
