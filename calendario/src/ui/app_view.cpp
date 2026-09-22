#include "ui/app_view.h"

#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "ui/components.h"

using Microsoft::WRL::ComPtr;

namespace agenda {
namespace {

// Short enough for a week column, and in capitals because at 11 DIP a word in lower case reads
// as a sentence that got cut off.
constexpr std::wstring_view kWeekdayShort[7] = {L"LUN", L"MAR", L"MIÉ", L"JUE",
                                                L"VIE", L"SÁB", L"DOM"};

constexpr float kBlockTextPadDip = 6.0f;
constexpr float kBlockLineDip = 16.0f;
constexpr float kNowDotDip = 4.0f;

std::wstring Clock(int minute) {
  return std::format(L"{:02}:{:02}", minute / 60, minute % 60);
}

std::wstring Lower(std::wstring_view text) {
  std::wstring out(text);
  if (!out.empty()) out[0] = static_cast<wchar_t>(std::towlower(out[0]));
  return out;
}

bool SameMonth(Date a, Date b) { return a.year() == b.year() && a.month() == b.month(); }

// The tint an event block wears on the timeline: its calendar's colour washed into the card
// surface, strong enough to tell calendars apart and quiet enough to read black or white on.
D2D1_COLOR_F BlockFill(const Theme& theme, std::uint32_t color) {
  return Lerp(theme.surface, Rgb(color), theme.light ? 0.16f : 0.26f);
}

// A layer at `alpha`, or nothing when it is fully opaque. The expansion fades whole groups this
// way, which is what keeps overlapping shapes from showing through each other mid-fade.
class Faded {
 public:
  Faded(ID2D1RenderTarget* target, float alpha) : target_(target) {
    if (alpha >= 1.0f || FAILED(target->CreateLayer(nullptr, &layer_))) return;
    target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                            D2D1::IdentityMatrix(), alpha),
                      layer_.Get());
    pushed_ = true;
  }
  ~Faded() {
    if (pushed_) target_->PopLayer();
  }
  Faded(const Faded&) = delete;
  Faded& operator=(const Faded&) = delete;

 private:
  ID2D1RenderTarget* target_;
  ComPtr<ID2D1Layer> layer_;
  bool pushed_ = false;
};

// A block of colour with the calendar's bar down its left edge, the shape every event wears in
// this app: the popup's cards, the timeline's blocks and the all-day chips.
void DrawTinted(ID2D1RenderTarget* target, const Theme& theme, ID2D1SolidColorBrush* brush,
                const D2D1_RECT_F& rect, float radius, float bar, std::uint32_t color) {
  brush->SetColor(BlockFill(theme, color));
  FillRound(target, rect, radius, brush);
  target->PushAxisAlignedClip(D2D1_RECT_F{rect.left, rect.top, rect.left + bar, rect.bottom},
                              D2D1_ANTIALIAS_MODE_ALIASED);
  brush->SetColor(Rgb(color));
  FillRound(target, rect, radius, brush);
  target->PopAxisAlignedClip();
}

// --- Top row --------------------------------------------------------------------------------

void DrawCollapseIcon(ID2D1RenderTarget* target, const Theme& theme, const AppLayout& app,
                      ID2D1SolidColorBrush* brush, float hover, ID2D1StrokeStyle* style) {
  const D2D1_POINT_2F c = Center(app.collapse);
  const float type = app.type;
  if (hover > 0.0f) {
    brush->SetColor(Fade(theme.hover, hover));
    FillCircle(target, c, (app.collapse.bottom - app.collapse.top) / 2.0f, brush);
  }
  // Two corners pointing in: the window is about to fold back into its corner.
  const float tail = 6.0f * type;
  const float tipAt = 1.5f * type;
  const float head = 4.0f * type;
  const float stroke = 1.5f * type;
  brush->SetColor(Lerp(theme.textSecondary, theme.textPrimary, hover));
  for (const float s : {-1.0f, 1.0f}) {
    const D2D1_POINT_2F tip{c.x + s * tipAt, c.y + s * tipAt};
    target->DrawLine(D2D1_POINT_2F{c.x + s * tail, c.y + s * tail}, tip, brush, stroke, style);
    target->DrawLine(tip, D2D1_POINT_2F{tip.x + s * head, tip.y}, brush, stroke, style);
    target->DrawLine(tip, D2D1_POINT_2F{tip.x, tip.y + s * head}, brush, stroke, style);
  }
}

void DrawTopRow(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                const PanelLayout& popup, const AppLayout& app, const PopupModel& model,
                const AppModel& appModel, ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* style) {
  DrawChevron(target, theme, popup, brush, app.prev, true, appModel.prevHover, style);
  DrawChevron(target, theme, popup, brush, app.next, false, appModel.nextHover, style);
  brush->SetColor(theme.textPrimary);
  DrawTextIn(target, fonts.title.Get(), PeriodTitle(appModel.view, model.selected), app.title,
             brush);

  // The three views as one segmented capsule, the same height and the same round ends as the
  // input next to it.
  const float radius = (app.rowHeight) / 2.0f;
  const D2D1_RECT_F all{app.tabs[0].left, app.rowTop, app.tabs[2].right,
                        app.rowTop + app.rowHeight};
  brush->SetColor(theme.surface);
  FillRound(target, all, radius, brush);
  brush->SetColor(theme.border);
  StrokeRound(target, all, radius, brush, 1.0f);

  constexpr std::wstring_view kNames[3] = {L"Día", L"Semana", L"Mes"};
  const float inset = std::round(3.0f * app.type);
  for (int i = 0; i < 3; ++i) {
    const bool on = static_cast<int>(appModel.view) == i;
    const D2D1_RECT_F pill = Inset(app.tabs[i], inset);
    if (on) {
      brush->SetColor(Fade(theme.textPrimary, theme.light ? 0.08f : 0.12f));
      FillRound(target, pill, radius - inset, brush);
    } else if (appModel.tabHover[i] > 0.0f) {
      brush->SetColor(Fade(theme.hover, appModel.tabHover[i]));
      FillRound(target, pill, radius - inset, brush);
    }
    brush->SetColor(on ? theme.textPrimary
                       : Lerp(theme.textSecondary, theme.textPrimary, appModel.tabHover[i]));
    DrawTextIn(target, fonts.event.Get(), kNames[i], app.tabs[i], brush, Align::Center);
  }

  DrawCollapseIcon(target, theme, app, brush, appModel.collapseHover, style);
}

// --- Day and week -------------------------------------------------------------------------

void DrawDayHeaders(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                    const AppLayout& app, const PopupModel& model, const AppModel& appModel,
                    ID2D1SolidColorBrush* brush) {
  const float half = app.dayHeaderHeight / 2.0f;
  const float circle = std::round(28.0f * app.type);
  for (int i = 0; i < app.columns; ++i) {
    const Date date = AddDays(appModel.first, i);
    const D2D1_RECT_F cell = app.dayHeader(i);
    const int weekday = MondayIndex(std::chrono::weekday{std::chrono::sys_days{date}});
    const bool today = date == model.today;

    brush->SetColor(today ? theme.accent : theme.textSecondary);
    DrawTextIn(target, fonts.label.Get(), kWeekdayShort[weekday],
               D2D1_RECT_F{cell.left, cell.top, cell.right, cell.top + half - app.gap / 2.0f},
               brush, Align::Center);

    const D2D1_RECT_F number{cell.left, cell.top + half - app.gap / 2.0f, cell.right,
                             cell.bottom};
    if (today) {
      brush->SetColor(theme.accent);
      FillCircle(target, Center(number), circle / 2.0f, brush);
    }
    brush->SetColor(today ? theme.onAccent : theme.textPrimary);
    DrawTextIn(target, fonts.title.Get(), std::to_wstring(static_cast<unsigned>(date.day())),
               number, brush, Align::Center);
  }
}

void DrawAllDay(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                const AppLayout& app, const AppModel& appModel, ID2D1SolidColorBrush* brush) {
  const float radius = (std::min)(std::round(kRadiusCard * app.type), app.allDayRow / 2.0f);
  const float pad = std::round(kBlockTextPadDip * app.type) + app.gap;
  for (int i = 0; i < app.columns && i < static_cast<int>(appModel.days.size()); ++i) {
    std::vector<const DayItem*> chips;
    for (const DayItem& item : appModel.days[static_cast<size_t>(i)]) {
      if (!item.startMin) chips.push_back(&item);
    }
    const int total = static_cast<int>(chips.size());
    for (int row = 0; row < app.allDayRows && row < total; ++row) {
      const DayItem& item = *chips[static_cast<size_t>(row)];
      const D2D1_RECT_F chip = app.allDayChip(i, row);
      DrawTinted(target, theme, brush, chip, radius, app.gap / 1.5f, item.color);
      if (item.uid == appModel.selected) {
        brush->SetColor(theme.accent);
        StrokeRound(target, chip, radius, brush, (std::max)(1.0f, std::round(1.5f * app.type)));
      }
      const bool last = row == app.allDayRows - 1 && total > app.allDayRows;
      const std::wstring text = last ? std::format(L"+{} más", total - row) : item.title;
      brush->SetColor(item.done ? theme.textMuted : theme.textPrimary);
      DrawTextIn(target, fonts.label.Get(), text,
                 D2D1_RECT_F{chip.left + pad, chip.top, chip.right - app.gap, chip.bottom},
                 brush);
    }
  }
}

void DrawBlock(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const AppLayout& app, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& rect,
               const DayItem& item, int start, int end) {
  const float height = rect.bottom - rect.top;
  const float radius = (std::min)(std::round(kRadiusCard * app.type), height / 2.0f);
  DrawTinted(target, theme, brush, rect, radius, (std::max)(3.0f, std::round(3.0f * app.type)),
             item.color);

  const float pad = std::round(kBlockTextPadDip * app.type);
  const float line = std::round(kBlockLineDip * app.type);
  const float left = rect.left + (std::max)(3.0f, std::round(3.0f * app.type)) + pad;
  const float right = rect.right - pad / 2.0f;
  if (right <= left) return;

  const std::wstring when = item.isTask ? Clock(start) : Clock(start) + L" – " + Clock(end);
  const D2D1_COLOR_F titleColor = item.done ? theme.textMuted : theme.textPrimary;

  target->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
  if (height >= 2.0f * line + pad) {
    const float top = rect.top + pad / 2.0f;
    brush->SetColor(titleColor);
    DrawTextIn(target, fonts.event.Get(), item.title, D2D1_RECT_F{left, top, right, top + line},
               brush);
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.label.Get(), when,
               D2D1_RECT_F{left, top + line, right, top + 2.0f * line}, brush);
  } else {
    brush->SetColor(titleColor);
    DrawTextIn(target, fonts.event.Get(), item.title + L"  ·  " + Clock(start),
               D2D1_RECT_F{left, rect.top, right, rect.bottom}, brush);
  }
  target->PopAxisAlignedClip();
}

void DrawTimeline(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                  const AppLayout& app, const PopupModel& model, const AppModel& appModel,
                  ID2D1SolidColorBrush* brush) {
  const D2D1_RECT_F& area = app.timeline;
  const float scroll = appModel.scroll;
  const float labelHalf = std::round(8.0f * app.type);
  const float labelRight = app.columnsLeft - std::round(8.0f * app.type);

  // Where "now" is, if it is on screen: the hour label under it gives way to its own.
  int todayColumn = -1;
  for (int i = 0; i < app.columns; ++i) {
    if (AddDays(appModel.first, i) == model.today) todayColumn = i;
  }
  const float nowY = YForMinute(app, scroll, static_cast<float>(appModel.nowMinute));
  const bool nowShown = todayColumn >= 0 && nowY >= area.top && nowY <= area.bottom;

  // The hour labels get half a line of room above the timeline, in the gutter where nothing
  // else is, so the first hour on screen is read whole instead of cut in half by the edge. Only
  // hours whose line is on screen are labelled, so nothing scrolled away leaks up into it.
  target->PushAxisAlignedClip(
      D2D1_RECT_F{area.left, area.top - labelHalf, app.columnsLeft, area.bottom},
      D2D1_ANTIALIAS_MODE_ALIASED);
  for (int hour = 1; hour < 24; ++hour) {
    const float y = std::round(YForMinute(app, scroll, hour * 60.0f));
    if (y < area.top || y + labelHalf > area.bottom) continue;
    if (nowShown && std::fabs(y - nowY) < 2.0f * labelHalf) continue;
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.label.Get(), Clock(hour * 60),
               D2D1_RECT_F{area.left, y - labelHalf, labelRight, y + labelHalf}, brush,
               Align::Right);
  }
  target->PopAxisAlignedClip();

  target->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);

  for (int hour = 0; hour <= 24; ++hour) {
    const float y = std::round(YForMinute(app, scroll, hour * 60.0f));
    if (y < area.top || y > area.bottom) continue;
    brush->SetColor(theme.border);
    target->FillRectangle(D2D1_RECT_F{app.columnsLeft, y, area.right, y + 1.0f}, brush);
  }

  for (int i = 0; i <= app.columns; ++i) {
    const float x = std::round(app.columnsLeft + static_cast<float>(i) * app.columnWidth);
    brush->SetColor(theme.border);
    target->FillRectangle(D2D1_RECT_F{x, area.top, x + 1.0f, area.bottom}, brush);
  }

  const float outline = (std::max)(1.0f, std::round(1.5f * app.type));
  for (const PlacedBlock& block : PlaceBlocks(app, appModel)) {
    const DayItem& item = *block.item;
    const int end = item.isTask ? block.start : (item.endMin ? *item.endMin : block.end);
    DrawBlock(target, fonts, theme, app, brush, block.rect, item, block.start, end);
    if (item.uid == appModel.selected) {
      brush->SetColor(theme.accent);
      StrokeRound(target, block.rect,
                  (std::min)(std::round(kRadiusCard * app.type),
                             (block.rect.bottom - block.rect.top) / 2.0f),
                  brush, outline);
    }
  }

  // What a drag is proposing: the block where it would land, outlined like the selection.
  const Ghost& ghost = appModel.ghost;
  if (ghost.on && !ghost.free) {
    const D2D1_RECT_F rect = GhostRect(app, scroll, ghost.column, ghost.start, ghost.end);
    DayItem shadow;
    shadow.title = ghost.title;
    shadow.color = ghost.color;
    DrawBlock(target, fonts, theme, app, brush, rect, shadow, ghost.start, ghost.end);
    brush->SetColor(theme.accent);
    StrokeRound(target, rect,
                (std::min)(std::round(kRadiusCard * app.type), (rect.bottom - rect.top) / 2.0f),
                brush, outline);
  }

  target->PopAxisAlignedClip();

  if (nowShown) {
    // Its own clip, with the same half line above the timeline as the hour labels.
    target->PushAxisAlignedClip(
        D2D1_RECT_F{area.left, area.top - labelHalf, area.right, area.bottom},
        D2D1_ANTIALIAS_MODE_ALIASED);
    const float y = std::round(nowY);
    const float thick = (std::max)(1.0f, std::round(1.5f * app.type));
    brush->SetColor(Fade(theme.now, 0.35f));
    target->FillRectangle(D2D1_RECT_F{app.columnsLeft, y, area.right, y + 1.0f}, brush);
    const D2D1_RECT_F column = app.column(todayColumn);
    brush->SetColor(theme.now);
    target->FillRectangle(
        D2D1_RECT_F{column.left, y - thick / 2.0f, column.right, y + thick / 2.0f}, brush);
    FillCircle(target, D2D1_POINT_2F{column.left, y}, std::round(kNowDotDip * app.type), brush);
    DrawTextIn(target, fonts.label.Get(), Clock(appModel.nowMinute),
               D2D1_RECT_F{area.left, y - labelHalf, labelRight, y + labelHalf}, brush,
               Align::Right);
    target->PopAxisAlignedClip();
  }
}

void DrawDayOrWeek(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const AppLayout& app, const PopupModel& model, const AppModel& appModel,
                   ID2D1SolidColorBrush* brush) {
  DrawDayHeaders(target, fonts, theme, app, model, appModel, brush);
  DrawAllDay(target, fonts, theme, app, appModel, brush);
  brush->SetColor(theme.border);
  target->FillRectangle(D2D1_RECT_F{app.main.left, app.timeline.top - 1.0f, app.main.right,
                                    app.timeline.top},
                        brush);
  DrawTimeline(target, fonts, theme, app, model, appModel, brush);
}

// --- Month ----------------------------------------------------------------------------------

void DrawMonthView(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const AppLayout& app, const PopupModel& model, const AppModel& appModel,
                   ID2D1SolidColorBrush* brush) {
  for (int column = 0; column < kGridCols; ++column) {
    const float left = app.main.left + static_cast<float>(column) * app.monthCellWidth;
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.label.Get(), kWeekdayShort[column],
               D2D1_RECT_F{left, app.monthLabelsTop, left + app.monthCellWidth,
                           app.monthLabelsTop + app.monthLabelsHeight},
               brush, Align::Center);
  }

  const float pad = std::round(8.0f * app.type);
  const float circle = std::round(24.0f * app.type);
  const float dot = std::round(6.0f * app.type);
  const Month shown{model.selected.year(), model.selected.month()};
  const Date firstOfMonth{shown.year(), shown.month(), std::chrono::day{1}};

  for (int index = 0; index < kGridCells; ++index) {
    const D2D1_RECT_F cell = app.monthCell(index);
    const Date date = AddDays(appModel.first, index);
    const bool inMonth = SameMonth(date, firstOfMonth);

    if (date == model.selected) {
      brush->SetColor(Fade(theme.textPrimary, theme.light ? 0.04f : 0.05f));
      target->FillRectangle(cell, brush);
    }
    brush->SetColor(theme.border);
    target->FillRectangle(D2D1_RECT_F{cell.left, cell.top, cell.right, cell.top + 1.0f}, brush);
    if (index % kGridCols != 0) {
      target->FillRectangle(D2D1_RECT_F{cell.left, cell.top, cell.left + 1.0f, cell.bottom},
                            brush);
    }

    const D2D1_RECT_F number{cell.left + pad / 2.0f, cell.top + pad / 2.0f,
                             cell.left + pad / 2.0f + circle, cell.top + pad / 2.0f + circle};
    const bool today = date == model.today;
    if (today) {
      brush->SetColor(theme.accent);
      FillCircle(target, Center(number), circle / 2.0f, brush);
    }
    brush->SetColor(today ? theme.onAccent : (inMonth ? theme.textPrimary : theme.textMuted));
    DrawTextIn(target, fonts.day.Get(), std::to_wstring(static_cast<unsigned>(date.day())),
               number, brush, Align::Center);

    if (index >= static_cast<int>(appModel.days.size())) continue;
    const std::vector<DayItem>& items = appModel.days[static_cast<size_t>(index)];
    const float listTop = number.bottom + app.gap / 2.0f;
    const int fits =
        (std::max)(0, static_cast<int>((cell.bottom - pad / 2.0f - listTop) / app.monthLine));
    const int total = static_cast<int>(items.size());
    const int shownCount = total > fits ? (std::max)(0, fits - 1) : total;
    for (int k = 0; k < shownCount; ++k) {
      const DayItem& item = items[static_cast<size_t>(k)];
      const float top = listTop + static_cast<float>(k) * app.monthLine;
      const D2D1_RECT_F line{cell.left + pad, top, cell.right - pad / 2.0f, top + app.monthLine};
      brush->SetColor(Rgb(item.color, inMonth ? 1.0f : 0.45f));
      FillCircle(target, D2D1_POINT_2F{line.left + dot / 2.0f, (line.top + line.bottom) / 2.0f},
                 dot / 2.0f, brush);
      const std::wstring text =
          item.startMin ? Clock(*item.startMin) + L" " + item.title : item.title;
      brush->SetColor(item.done || !inMonth ? theme.textMuted : theme.textPrimary);
      DrawTextIn(target, fonts.label.Get(), text,
                 D2D1_RECT_F{line.left + dot + pad / 2.0f, line.top, line.right, line.bottom},
                 brush);
    }
    if (shownCount < total) {
      const float top = listTop + static_cast<float>(shownCount) * app.monthLine;
      brush->SetColor(theme.textSecondary);
      DrawTextIn(target, fonts.label.Get(), std::format(L"+{} más", total - shownCount),
                 D2D1_RECT_F{cell.left + pad, top, cell.right - pad / 2.0f, top + app.monthLine},
                 brush);
    }
  }
}

// --- Sidebar --------------------------------------------------------------------------------

void DrawSidebar(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                 const PanelLayout& popup, const AppLayout& app, const AppModel& appModel,
                 ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* style) {
  brush->SetColor(theme.border);
  target->FillRectangle(
      D2D1_RECT_F{app.sidebarRight, 0.0f, app.sidebarRight + 1.0f, app.height}, brush);

  brush->SetColor(theme.textSecondary);
  DrawTextIn(target, fonts.label.Get(), L"Calendarios", app.calendarsLabel(), brush);

  const float box = std::round(kCheckboxDip * app.type);
  const float boxRadius = std::round(4.0f * app.type);
  const float stroke = (std::max)(1.0f, std::round(1.5f * app.type));
  const int calendars = static_cast<int>(appModel.calendars.size());
  for (int i = 0; i < calendars; ++i) {
    const CalendarInfo& calendar = appModel.calendars[static_cast<size_t>(i)];
    const D2D1_RECT_F row = app.calendarRowRect(i);
    const float hover =
        i < static_cast<int>(appModel.calendarHover.size()) ? appModel.calendarHover[i] : 0.0f;
    if (hover > 0.0f) {
      brush->SetColor(Fade(theme.hover, hover));
      FillRound(target, D2D1_RECT_F{row.left - app.gap, row.top, row.right, row.bottom},
                std::round(6.0f * app.type), brush);
    }

    const float top = std::round((row.top + row.bottom - box) / 2.0f);
    const D2D1_RECT_F square{row.left, top, row.left + box, top + box};
    if (calendar.hidden) {
      brush->SetColor(Rgb(calendar.color));
      StrokeRound(target, square, boxRadius, brush, stroke);
    } else {
      brush->SetColor(Rgb(calendar.color));
      FillRound(target, square, boxRadius, brush);
      brush->SetColor(theme.onAccent);
      const float side = box;
      const D2D1_POINT_2F a{square.left + side * 0.26f, square.top + side * 0.52f};
      const D2D1_POINT_2F b{square.left + side * 0.44f, square.top + side * 0.70f};
      const D2D1_POINT_2F c{square.left + side * 0.75f, square.top + side * 0.32f};
      target->DrawLine(a, b, brush, stroke, style);
      target->DrawLine(b, c, brush, stroke, style);
    }
    brush->SetColor(calendar.hidden ? theme.textSecondary : theme.textPrimary);
    DrawTextIn(target, fonts.event.Get(), calendar.title,
               D2D1_RECT_F{square.right + 2.0f * app.gap, row.top, row.right, row.bottom}, brush);
  }

  brush->SetColor(theme.textSecondary);
  DrawTextIn(target, fonts.label.Get(), L"Sin fecha", app.tasksLabel(calendars), brush);
  if (appModel.undated.empty()) {
    brush->SetColor(theme.textMuted);
    DrawTextIn(target, fonts.event.Get(), L"Nada pendiente", app.taskRow(calendars, 0), brush);
    return;
  }
  const int fits = app.tasksThatFit(calendars);
  const int total = static_cast<int>(appModel.undated.size());
  const int shown = (std::min)(fits, total);
  for (int i = 0; i < shown; ++i) {
    const DayItem& item = appModel.undated[static_cast<size_t>(i)];
    const int more = i == shown - 1 ? total - shown : 0;
    DrawEventCard(target, fonts, theme, popup, brush, app.taskRow(calendars, i), item, more,
                  item.done ? 1.0f : 0.0f);
  }
}

// --- The detail panel -----------------------------------------------------------------------

constexpr std::wstring_view kDetailLabelNames[kDetailLabels] = {
    L"Título", L"Fecha", L"Inicio", L"Fin", L"Calendario", L"Ubicación", L"Notas", L"Repetición"};
constexpr std::wstring_view kRepeatNames[kRepeatChoices] = {L"Nunca", L"Diaria", L"Semanal",
                                                            L"Mensual", L"Anual"};

// A field's text, laid out the way it is drawn: one line that scrolls to keep the caret in
// sight, or -- for the notes -- wrapped inside the box. The drawing and the click both build
// one, so the caret goes where the pointer is.
struct FieldText {
  ComPtr<IDWriteTextLayout> layout;
  D2D1_RECT_F inner{};
  float scroll = 0.0f;
  float caretX = 0.0f;
  float caretY = 0.0f;
  float caretH = 0.0f;
};

bool BuildField(const Fonts& fonts, const D2D1_RECT_F& rect, const TextInput& input,
                bool multiline, float type, FieldText& out) {
  if (!fonts.ok()) return false;
  const float padX = std::round(10.0f * type);
  const float padY = std::round(6.0f * type);
  out.inner = multiline ? D2D1_RECT_F{rect.left + padX, rect.top + padY, rect.right - padX,
                                      rect.bottom - padY}
                        : D2D1_RECT_F{rect.left + padX, rect.top, rect.right - padX, rect.bottom};
  out.caretH = std::round(18.0f * type);
  out.caretY = multiline ? 0.0f : (rect.bottom - rect.top - out.caretH) / 2.0f;

  const std::wstring& text = input.text();
  if (text.empty()) return true;
  const float width = out.inner.right - out.inner.left;
  if (FAILED(fonts.factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                             fonts.event.Get(), multiline ? width : 4096.0f,
                                             out.inner.bottom - out.inner.top, &out.layout))) {
    return false;
  }
  out.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  const DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
  out.layout->SetTrimming(&none, nullptr);
  if (multiline) {
    out.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    out.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  }
  float y = 0.0f;
  DWRITE_HIT_TEST_METRICS metrics{};
  out.layout->HitTestTextPosition(static_cast<UINT32>(input.caret()), FALSE, &out.caretX, &y,
                                  &metrics);
  if (multiline) {
    out.caretY = y;
    out.caretH = metrics.height;
  } else {
    out.scroll = (std::max)(0.0f, out.caretX - width + (std::max)(1.0f, type));
  }
  return true;
}

void DrawField(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               ID2D1SolidColorBrush* brush, const D2D1_RECT_F& rect, float radius,
               const TextInput& input, bool focused, bool caretOn, bool invalid, bool multiline,
               std::wstring_view placeholder, float type) {
  brush->SetColor(theme.panelOpaque);
  FillRound(target, rect, radius, brush);
  brush->SetColor(invalid ? theme.now : (focused ? theme.accent : theme.border));
  StrokeRound(target, rect, radius, brush, invalid || focused ? 1.5f * type : 1.0f);

  FieldText field;
  if (!BuildField(fonts, rect, input, multiline, type, field)) return;
  target->PushAxisAlignedClip(field.inner, D2D1_ANTIALIAS_MODE_ALIASED);
  const float originX = field.inner.left - field.scroll;
  const float originY = multiline ? field.inner.top : rect.top;
  if (field.layout) {
    if (focused && input.hasSelection()) {
      const UINT32 start = static_cast<UINT32>(input.selectionStart());
      const UINT32 length = static_cast<UINT32>(input.selectionEnd() - start);
      DWRITE_HIT_TEST_METRICS boxes[8]{};
      UINT32 count = 0;
      field.layout->HitTestTextRange(start, length, originX, originY, boxes, 8, &count);
      brush->SetColor(theme.selection);
      for (UINT32 i = 0; i < (std::min)(count, 8u); ++i) {
        target->FillRectangle(D2D1_RECT_F{boxes[i].left, boxes[i].top,
                                          boxes[i].left + boxes[i].width,
                                          boxes[i].top + boxes[i].height},
                              brush);
      }
    }
    brush->SetColor(theme.textPrimary);
    target->DrawTextLayout(D2D1_POINT_2F{originX, originY}, field.layout.Get(), brush);
  } else if (!focused) {
    brush->SetColor(theme.textMuted);
    DrawTextIn(target, fonts.event.Get(), placeholder,
               multiline ? D2D1_RECT_F{field.inner.left, field.inner.top, field.inner.right,
                                       field.inner.top + field.caretH}
                         : field.inner,
               brush);
  }
  if (focused && caretOn) {
    const float x = originX + field.caretX;
    const float y = originY + field.caretY;
    brush->SetColor(theme.textPrimary);
    target->FillRectangle(D2D1_RECT_F{x, y, x + (std::max)(1.0f, type), y + field.caretH}, brush);
  }
  target->PopAxisAlignedClip();
}

void DrawCross(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, D2D1_POINT_2F c,
               float reach, float stroke, ID2D1StrokeStyle* style) {
  target->DrawLine(D2D1_POINT_2F{c.x - reach, c.y - reach},
                   D2D1_POINT_2F{c.x + reach, c.y + reach}, brush, stroke, style);
  target->DrawLine(D2D1_POINT_2F{c.x - reach, c.y + reach},
                   D2D1_POINT_2F{c.x + reach, c.y - reach}, brush, stroke, style);
}

const CalendarInfo* FindCalendar(const AppModel& model, const std::string& id) {
  for (const CalendarInfo& calendar : model.calendars) {
    if (calendar.id == id) return &calendar;
  }
  return nullptr;
}

void DrawDetail(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                const PanelLayout& popup, const AppLayout& app, const AppModel& appModel,
                ID2D1SolidColorBrush* brush, ID2D1StrokeStyle* style) {
  const DetailModel& detail = appModel.detail;
  if (detail.t <= 0.0f) return;
  const DetailLayout layout = MakeDetailLayout(app);
  const float type = app.type;
  Faded faded(target, EaseOutCubic(detail.t));

  brush->SetColor(theme.surface);
  FillRound(target, layout.panel, popup.panelRadius, brush);
  brush->SetColor(theme.border);
  StrokeRound(target, layout.panel, popup.panelRadius, brush, 1.0f);

  brush->SetColor(theme.textSecondary);
  DrawCross(target, brush, Center(layout.close), std::round(4.5f * type),
            (std::max)(1.0f, 1.5f * type), style);

  for (int i = 0; i < kDetailLabels; ++i) {
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.label.Get(), kDetailLabelNames[i], layout.labels[i], brush);
  }

  const bool allDay = !detail.event.startMin;
  const std::wstring_view placeholders[kDetailFields] = {
      L"Sin título",  L"dd/mm/aaaa",           allDay ? L"Todo el día" : L"--:--",
      L"--:--",       L"Añadir una ubicación", L"Añadir notas"};
  for (int i = 0; i < kDetailFields; ++i) {
    DrawField(target, fonts, theme, brush, layout.fields[i], layout.radius, detail.fields[i],
              detail.focus == i, detail.caretOn, ((detail.invalid >> i) & 1u) != 0,
              i == kFieldNotes,
              placeholders[i], type);
  }

  // The calendar: its colour and its name, and a chevron that says there is a list behind it.
  const float dot = std::round(10.0f * type);
  const auto calendarRow = [&](const D2D1_RECT_F& row, const CalendarInfo* calendar) {
    if (calendar == nullptr) return;
    const float padX = std::round(10.0f * type);
    brush->SetColor(Rgb(calendar->color));
    FillCircle(target, D2D1_POINT_2F{row.left + padX + dot / 2.0f, (row.top + row.bottom) / 2.0f},
               dot / 2.0f, brush);
    brush->SetColor(theme.textPrimary);
    DrawTextIn(target, fonts.event.Get(), calendar->title,
               D2D1_RECT_F{row.left + 2.0f * padX + dot, row.top, row.right - 3.0f * padX,
                           row.bottom},
               brush);
  };
  brush->SetColor(theme.panelOpaque);
  FillRound(target, layout.calendar, layout.radius, brush);
  brush->SetColor(detail.calendarOpen ? theme.accent : theme.border);
  StrokeRound(target, layout.calendar, layout.radius, brush,
              detail.calendarOpen ? 1.5f * type : 1.0f);
  calendarRow(layout.calendar, FindCalendar(appModel, detail.event.calendarId));
  {
    const D2D1_POINT_2F c{layout.calendar.right - std::round(16.0f * type),
                          (layout.calendar.top + layout.calendar.bottom) / 2.0f};
    const float reach = std::round(4.0f * type);
    brush->SetColor(theme.textSecondary);
    target->DrawLine(D2D1_POINT_2F{c.x - reach, c.y - reach / 2.0f},
                     D2D1_POINT_2F{c.x, c.y + reach / 2.0f}, brush, 1.5f * type, style);
    target->DrawLine(D2D1_POINT_2F{c.x, c.y + reach / 2.0f},
                     D2D1_POINT_2F{c.x + reach, c.y - reach / 2.0f}, brush, 1.5f * type, style);
  }

  // Repetition: five answers, and a rule that is none of them is said and kept, not replaced.
  const Repeat repeat = RepeatOf(detail.event.recurrence);
  for (int i = 0; i < kRepeatChoices; ++i) {
    const bool on = static_cast<int>(repeat) == i;
    const D2D1_RECT_F pill = layout.repeat[i];
    const float radius = (pill.bottom - pill.top) / 2.0f;
    brush->SetColor(on ? theme.accent : theme.panelOpaque);
    FillRound(target, pill, radius, brush);
    if (!on) {
      brush->SetColor(theme.border);
      StrokeRound(target, pill, radius, brush, 1.0f);
    }
    brush->SetColor(on ? theme.onAccent : theme.textSecondary);
    DrawTextIn(target, fonts.label.Get(), kRepeatNames[i], pill, brush, Align::Center);
  }
  if (repeat == Repeat::Custom) {
    brush->SetColor(theme.textSecondary);
    DrawTextIn(target, fonts.label.Get(), L"Personalizada: se conserva",
               D2D1_RECT_F{layout.labels[7].left + std::round(80.0f * type), layout.labels[7].top,
                           layout.labels[7].right, layout.labels[7].bottom},
               brush, Align::Right);
  }

  brush->SetColor(Fade(theme.now, theme.light ? 0.08f : 0.12f));
  FillRound(target, layout.remove, layout.radius, brush);
  brush->SetColor(theme.now);
  DrawTextIn(target, fonts.event.Get(), L"Borrar evento", layout.remove, brush, Align::Center);

  // The list, last, because it opens over the fields below the chooser.
  if (detail.calendarOpen) {
    int count = 0;
    for (const CalendarInfo& calendar : appModel.calendars) count += calendar.isTaskList ? 0 : 1;
    const D2D1_RECT_F first = layout.calendarOption(0);
    const D2D1_RECT_F all{first.left, first.top, first.right,
                          first.top + static_cast<float>(count) * layout.fieldHeight};
    brush->SetColor(theme.surface);
    FillRound(target, all, layout.radius, brush);
    brush->SetColor(theme.border);
    StrokeRound(target, all, layout.radius, brush, 1.0f);
    int row = 0;
    for (const CalendarInfo& calendar : appModel.calendars) {
      if (calendar.isTaskList) continue;
      const D2D1_RECT_F option = layout.calendarOption(row++);
      if (calendar.id == detail.event.calendarId) {
        brush->SetColor(theme.hover);
        FillRound(target, Inset(option, 2.0f), layout.radius - 2.0f, brush);
      }
      calendarRow(option, &calendar);
    }
  }
}

void DrawConfirm(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                 const AppLayout& app, const AppModel& appModel, ID2D1SolidColorBrush* brush) {
  if (appModel.confirm.empty()) return;
  const float width = (std::min)(std::round(520.0f * app.type), app.main.right - app.main.left);
  const float height = std::round(40.0f * app.type);
  const float middle = (app.main.left + app.main.right) / 2.0f;
  const float bottom = app.main.bottom - std::round(16.0f * app.type);
  const D2D1_RECT_F bar{middle - width / 2.0f, bottom - height, middle + width / 2.0f, bottom};
  brush->SetColor(theme.surface);
  FillRound(target, bar, height / 2.0f, brush);
  brush->SetColor(Fade(theme.now, 0.6f));
  StrokeRound(target, bar, height / 2.0f, brush, 1.5f * app.type);
  brush->SetColor(theme.textPrimary);
  DrawTextIn(target, fonts.event.Get(), appModel.confirm,
             Inset(bar, std::round(12.0f * app.type)), brush, Align::Center);
}

// A task from the tray on its way to the timeline, drawn under the pointer.
void DrawFreeGhost(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const AppLayout& app, const Ghost& ghost, ID2D1SolidColorBrush* brush) {
  const float width = std::round(220.0f * app.type);
  const float height = app.cardHeight;
  const D2D1_RECT_F card{ghost.at.x - std::round(24.0f * app.type), ghost.at.y - height / 2.0f,
                         ghost.at.x - std::round(24.0f * app.type) + width,
                         ghost.at.y + height / 2.0f};
  Faded faded(target, 0.9f);
  DrawTinted(target, theme, brush, card, std::round(kRadiusCard * app.type),
             (std::max)(3.0f, std::round(3.0f * app.type)), ghost.color);
  brush->SetColor(theme.textPrimary);
  DrawTextIn(target, fonts.event.Get(), ghost.title,
             D2D1_RECT_F{card.left + std::round(12.0f * app.type), card.top,
                         card.right - std::round(8.0f * app.type), card.bottom},
             brush);
}

}  // namespace

std::vector<PlacedBlock> PlaceBlocks(const AppLayout& app, const AppModel& model) {
  std::vector<PlacedBlock> out;
  if (model.view == AppView::Month) return out;
  const float inset = (std::max)(1.0f, std::round(app.gap / 2.0f));
  for (int i = 0; i < app.columns && i < static_cast<int>(model.days.size()); ++i) {
    const D2D1_RECT_F column = app.column(i);
    std::vector<const DayItem*> timed;
    std::vector<std::pair<int, int>> spans;
    for (const DayItem& item : model.days[static_cast<size_t>(i)]) {
      if (!item.startMin) continue;
      if (model.ghost.on && item.uid == model.ghost.hideUid) continue;
      const int start = *item.startMin;
      // A task has a moment and not a length; half an hour is what makes it big enough to read.
      int end = item.isTask ? start + 30 : (item.endMin ? *item.endMin : start + 60);
      if (end <= start) end = kMinutesPerDay;  // runs past midnight: it fills the rest of the day
      timed.push_back(&item);
      spans.emplace_back(start, (std::max)(end, start + kSnapMinutes));
    }
    const std::vector<Lane> lanes = LayoutOverlaps(spans);
    const float width = column.right - column.left - 2.0f * inset;
    for (size_t k = 0; k < timed.size(); ++k) {
      const float share = width / static_cast<float>(lanes[k].columns);
      const float left = column.left + inset + share * static_cast<float>(lanes[k].column);
      const float top =
          std::round(YForMinute(app, model.scroll, static_cast<float>(spans[k].first))) + inset;
      const float bottom =
          std::round(YForMinute(app, model.scroll, static_cast<float>(spans[k].second))) - inset;
      const D2D1_RECT_F block{left + (lanes[k].column > 0 ? inset : 0.0f), top, left + share,
                              (std::max)(bottom, top + app.minBlock)};
      out.push_back(PlacedBlock{i, timed[k], block, spans[k].first, spans[k].second});
    }
  }
  return out;
}

D2D1_RECT_F GhostRect(const AppLayout& app, float scroll, int column, int start, int end) {
  const float inset = (std::max)(1.0f, std::round(app.gap / 2.0f));
  const D2D1_RECT_F lane = app.column(column);
  const float top = std::round(YForMinute(app, scroll, static_cast<float>(start))) + inset;
  const float bottom = std::round(YForMinute(app, scroll, static_cast<float>(end))) - inset;
  return D2D1_RECT_F{lane.left + inset, top, lane.right - inset,
                     (std::max)(bottom, top + app.minBlock)};
}

size_t FieldIndexAt(const Fonts& fonts, const D2D1_RECT_F& rect, const TextInput& input,
                    bool multiline, float type, float x, float y) {
  FieldText field;
  if (!BuildField(fonts, rect, input, multiline, type, field) || !field.layout) {
    return input.text().size();
  }
  BOOL trailing = FALSE;
  BOOL inside = FALSE;
  DWRITE_HIT_TEST_METRICS metrics{};
  const float localY = multiline ? y - field.inner.top : (rect.bottom - rect.top) / 2.0f;
  if (FAILED(field.layout->HitTestPoint(x - field.inner.left + field.scroll, localY, &trailing,
                                        &inside, &metrics))) {
    return input.text().size();
  }
  return (std::min)(static_cast<size_t>(metrics.textPosition + (trailing ? metrics.length : 0)),
                    input.text().size());
}

// The day list is gone before the app starts arriving, so the two never sit on top of each
// other where the calendars take the list's place.
float AppAlpha(float progress) { return Ease(0.3f, 0.85f, progress); }
float ListAlpha(float progress) { return 1.0f - Ease(0.0f, 0.3f, progress); }

int AllDayRows(const AppModel& model) {
  if (model.view == AppView::Month) return 1;
  int most = 0;
  for (const std::vector<DayItem>& day : model.days) {
    int count = 0;
    for (const DayItem& item : day) count += item.startMin ? 0 : 1;
    most = (std::max)(most, count);
  }
  return most;
}

std::wstring PeriodTitle(AppView view, Date anchor) {
  switch (view) {
    case AppView::Day: {
      const int weekday = MondayIndex(std::chrono::weekday{std::chrono::sys_days{anchor}});
      return std::format(L"{} {} de {}", kWeekdayNames[weekday],
                         static_cast<unsigned>(anchor.day()), Lower(MonthName(anchor.month())));
    }
    case AppView::Week: {
      const Date first = FirstShown(AppView::Week, anchor);
      const Date last = AddDays(first, 6);
      if (SameMonth(first, last)) {
        return std::format(L"{} – {} de {}", static_cast<unsigned>(first.day()),
                           static_cast<unsigned>(last.day()), Lower(MonthName(last.month())));
      }
      return std::format(L"{} de {} – {} de {}", static_cast<unsigned>(first.day()),
                         Lower(MonthName(first.month())), static_cast<unsigned>(last.day()),
                         Lower(MonthName(last.month())));
    }
    case AppView::Month:
      return std::format(L"{} {}", MonthName(anchor.month()), static_cast<int>(anchor.year()));
  }
  return {};
}

void DrawApp(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
             const PanelLayout& popup, const AppLayout& app, const PopupModel& model,
             const AppModel& appModel, float progress, bool acrylic) {
  const float p = std::clamp(progress, 0.0f, 1.0f);
  DrawPanel(target, theme, D2D1_SIZE_F{app.width, app.height},
            Lerp(popup.panelRadius, app.radius, p), acrylic);
  if (!fonts.ok()) return;

  const PanelLayout morph = MorphLayout(popup, app, p);
  DrawPopupBody(target, fonts, theme, morph, model, ListAlpha(p));

  if (const float alpha = AppAlpha(p); alpha > 0.0f) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target->CreateSolidColorBrush(theme.textPrimary, &brush))) {
      ComPtr<ID2D1StrokeStyle> rounded = RoundedStroke(target);
      // The same eight DIP of rise the popup opens with: one vocabulary of movement.
      D2D1_MATRIX_3X2_F before{};
      target->GetTransform(&before);
      const float rise = kPopupSlideDip * app.type * (1.0f - alpha);
      target->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, rise) *
                           *D2D1::Matrix3x2F::ReinterpretBaseType(&before));
      {
        Faded faded(target, alpha);
        DrawSidebar(target, fonts, theme, popup, app, appModel, brush.Get(), rounded.Get());
        DrawTopRow(target, fonts, theme, popup, app, model, appModel, brush.Get(), rounded.Get());
        target->PushAxisAlignedClip(app.main, D2D1_ANTIALIAS_MODE_ALIASED);
        if (appModel.view == AppView::Month) {
          DrawMonthView(target, fonts, theme, app, model, appModel, brush.Get());
        } else {
          DrawDayOrWeek(target, fonts, theme, app, model, appModel, brush.Get());
        }
        target->PopAxisAlignedClip();
        DrawDetail(target, fonts, theme, popup, app, appModel, brush.Get(), rounded.Get());
        DrawConfirm(target, fonts, theme, app, appModel, brush.Get());
      }
      target->SetTransform(before);
    }
  }

  // Last, so the preview that hangs below the capsule in the app lands on top of the timeline.
  DrawPopupInput(target, fonts, theme, morph, model);

  // A task in flight goes over everything, capsule included: it is in the hand.
  if (appModel.ghost.on && appModel.ghost.free) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target->CreateSolidColorBrush(theme.textPrimary, &brush))) {
      DrawFreeGhost(target, fonts, theme, app, appModel.ghost, brush.Get());
    }
  }
}

}  // namespace agenda
