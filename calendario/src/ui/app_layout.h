#pragma once

// Every rectangle of the expanded app, worked out once from its size, the same way layout.h
// does it for the popup: the drawing and the hit testing read this one, so a click lands where
// the pixel is.
//
// The key decision is the sidebar. It is exactly as wide as the popup and starts at the same
// top-left corner, so the popup's header, weekday initials and month grid ARE the sidebar's mini
// month -- same rectangles, same pixels. While the window grows they stay pinned to its corner
// and everything else arrives around them. That is what makes the expansion one window
// reorganising itself instead of a cut to another one.

#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "core/config.h"
#include "data/model.h"
#include "ui/layout.h"
#include "ui/paint.h"

namespace agenda {

// The tabs, in the order they are drawn: a view's number is its tab. Quarter, Year and List came
// in phase 13, after the three that were already there.
enum class AppView { Day, Week, Month, Quarter, Year, List };
inline constexpr int kAppViews = 6;

// The two views with hours down the side, where blocks are dragged and the wheel scrolls time.
inline bool HasTimeline(AppView view) { return view == AppView::Day || view == AppView::Week; }
// The two that are a grid of day cells with their events written in: the month, and the
// quarter's fourteen weeks in a row.
inline bool IsDayGrid(AppView view) { return view == AppView::Month || view == AppView::Quarter; }

// A quarter is fourteen weeks, Monday to Sunday: the most its three months can touch.
inline constexpr int kQuarterRows = 14;
// The list reads a month ahead, as a phone's agenda does.
inline constexpr int kListDays = 30;

// The first day of the quarter `day` is in: 1 January, 1 April, 1 July or 1 October.
inline Date QuarterStart(Date day) {
  const unsigned month = (static_cast<unsigned>(day.month()) - 1u) / 3u * 3u + 1u;
  return Date{day.year(), std::chrono::month{month}, std::chrono::day{1}};
}

// The size the app is designed at: 80 % of a 1920x1032 work area. The snapshots render it, so
// a committed PNG never depends on the monitor that produced it.
inline constexpr float kAppBaseWidthDip = 1536.0f;
inline constexpr float kAppBaseHeightDip = 826.0f;

// Metrics at the size the design system is written at; scaled by the popup's `type`, so the
// letters in the app are the letters in the popup.
inline constexpr float kHourDip = 48.0f;          // one hour of timeline
inline constexpr float kGutterDip = 56.0f;        // the hour labels to the left of it
inline constexpr float kDayHeaderDip = 44.0f;
inline constexpr float kGutterTwoDip = 120.0f;  // two columns of hours, the second zone's first     // weekday and number above each column
inline constexpr float kAllDayRowDip = 22.0f;     // a chip in the all-day strip
inline constexpr int kAllDayMaxRows = 3;
inline constexpr float kMinBlockDip = 20.0f;      // an event of five minutes still gets a line
inline constexpr float kTabWidthDip = 72.0f;
inline constexpr float kCapsuleMaxDip = 460.0f;
inline constexpr float kCapsuleMinDip = 220.0f;
inline constexpr float kSectionLabelDip = 24.0f;  // "Calendarios", "Sin fecha"
inline constexpr float kCalendarRowDip = 28.0f;
inline constexpr float kSetsRowDip = 26.0f;       // the calendar sets' chips (phase 13)
inline constexpr float kSetChipMaxDip = 96.0f;
inline constexpr float kMonthLabelsDip = 24.0f;
inline constexpr float kMonthLineDip = 20.0f;     // one event in a month cell
inline constexpr float kDetailDip = 320.0f;      // the detail panel on the right
inline constexpr float kResizeGripDip = 6.0f;     // the bottom of a block that stretches it
inline constexpr int kSnapMinutes = 15;
inline constexpr int kMinutesPerDay = 24 * 60;

inline int ShownDays(AppView view) {
  switch (view) {
    case AppView::Day:
      return 1;
    case AppView::Week:
      return 7;
    case AppView::Month:
      return kGridCells;
    case AppView::Quarter:
      return kQuarterRows * kGridCols;
    case AppView::Year:
      return 0;  // twelve months of dots, which come from DotsForRange and not day by day
    case AppView::List:
      return kListDays;
  }
  return 1;
}

// The first day on screen: the day itself, the Monday of its week, or the first cell of its
// month's grid -- the same six-by-seven the popup draws.
inline Date FirstShown(AppView view, Date anchor) {
  switch (view) {
    case AppView::Day:
      return anchor;
    case AppView::Week:
      return AddDays(anchor, -MondayIndex(std::chrono::weekday{std::chrono::sys_days{anchor}}));
    case AppView::Month:
      return GridStart(Month{anchor.year(), anchor.month()});
    case AppView::Quarter: {
      const Date start = QuarterStart(anchor);
      return GridStart(Month{start.year(), start.month()});
    }
    case AppView::Year:
      return Date{anchor.year(), std::chrono::January, std::chrono::day{1}};
    case AppView::List:
      return anchor;
  }
  return anchor;
}

struct AppLayout {
  AppView view = AppView::Day;
  float width = kAppBaseWidthDip;
  float height = kAppBaseHeightDip;
  float type = 1.0f;
  float padding = 0.0f;
  float gap = 0.0f;
  float radius = 0.0f;

  // The sidebar: the popup's own width. Below the mini month come the calendars and the tray.
  float sidebarRight = 0.0f;
  float sideLeft = 0.0f;
  float sideRight = 0.0f;
  float setsTop = 0.0f;  // phase 13: the row of calendar sets, over "Calendarios"
  float setsHeight = 0.0f;
  float sectionTop = 0.0f;
  float sectionLabel = 0.0f;
  float calendarRow = 0.0f;
  float cardHeight = 0.0f;

  // The top row, level with the mini month's title: arrows and the period on the left, the
  // capsule in the middle, the view tabs and the collapse button on the right.
  float rowTop = 0.0f;
  float rowHeight = 0.0f;
  D2D1_RECT_F prev{};
  D2D1_RECT_F next{};
  D2D1_RECT_F title{};
  D2D1_RECT_F input{};
  D2D1_RECT_F tabs[kAppViews]{};
  D2D1_RECT_F collapse{};

  D2D1_RECT_F main{};
  // The detail panel. It slides in from the right edge and the main view gives way to it, so
  // while it is closed it sits just past the window, out of sight.
  float detailWidth = 0.0f;
  D2D1_RECT_F detail{};

  // Day and week.
  int columns = 1;
  float dayHeaderTop = 0.0f;
  float dayHeaderHeight = 0.0f;
  float allDayTop = 0.0f;
  float allDayRow = 0.0f;
  int allDayRows = 0;
  float allDayHeight = 0.0f;
  D2D1_RECT_F timeline{};  // the scrolling part, gutter included
  float gutter = 0.0f;
  float columnsLeft = 0.0f;
  float columnWidth = 0.0f;
  float hourHeight = 0.0f;
  float minBlock = 0.0f;

  // Month, and the quarter's weeks, which are the same cells with more rows.
  int monthRows = kGridRows;
  float monthLabelsTop = 0.0f;
  float monthLabelsHeight = 0.0f;
  float monthGridTop = 0.0f;
  float monthCellWidth = 0.0f;
  float monthCellHeight = 0.0f;
  float monthLine = 0.0f;

  D2D1_RECT_F column(int index) const {
    const float left = columnsLeft + static_cast<float>(index) * columnWidth;
    return D2D1_RECT_F{left, timeline.top, left + columnWidth, timeline.bottom};
  }

  D2D1_RECT_F dayHeader(int index) const {
    const float left = columnsLeft + static_cast<float>(index) * columnWidth;
    return D2D1_RECT_F{left, dayHeaderTop, left + columnWidth, dayHeaderTop + dayHeaderHeight};
  }

  D2D1_RECT_F allDayChip(int index, int row) const {
    const float left = columnsLeft + static_cast<float>(index) * columnWidth;
    const float top = allDayTop + gap + static_cast<float>(row) * (allDayRow + gap);
    return D2D1_RECT_F{left + gap / 2.0f, top, left + columnWidth - gap / 2.0f, top + allDayRow};
  }

  D2D1_RECT_F monthCell(int index) const {
    const float left = main.left + static_cast<float>(index % kGridCols) * monthCellWidth;
    const float top = monthGridTop + static_cast<float>(index / kGridCols) * monthCellHeight;
    return D2D1_RECT_F{left, top, left + monthCellWidth, top + monthCellHeight};
  }

  // The sets' chips: "Todos" and each set (`chips` of them in all), then the "+" that saves
  // the calendars as they are now. Equal widths, never wider than kSetChipMaxDip.
  float setChipWidth(int chips) const {
    const float room = sideRight - sideLeft - setsHeight - static_cast<float>(chips) * gap;
    return (std::min)(std::round(kSetChipMaxDip * type),
                      std::floor(room / static_cast<float>((std::max)(chips, 1))));
  }
  D2D1_RECT_F setChip(int index, int chips) const {
    const float left = sideLeft + static_cast<float>(index) * (setChipWidth(chips) + gap);
    return D2D1_RECT_F{left, setsTop, left + setChipWidth(chips), setsTop + setsHeight};
  }
  D2D1_RECT_F setAdd(int chips) const {
    const float left = setChip(chips, chips).left;
    return D2D1_RECT_F{left, setsTop, left + setsHeight, setsTop + setsHeight};
  }
  // The cross that takes a set away, at the right end of its chip while the pointer is on it.
  D2D1_RECT_F setRemove(int index, int chips) const {
    const D2D1_RECT_F chip = setChip(index, chips);
    return D2D1_RECT_F{chip.right - setsHeight, chip.top, chip.right, chip.bottom};
  }

  D2D1_RECT_F calendarsLabel() const {
    return D2D1_RECT_F{sideLeft, sectionTop, sideRight, sectionTop + sectionLabel};
  }

  D2D1_RECT_F calendarRowRect(int index) const {
    const float top = sectionTop + sectionLabel + static_cast<float>(index) * calendarRow;
    return D2D1_RECT_F{sideLeft, top, sideRight, top + calendarRow};
  }

  D2D1_RECT_F tasksLabel(int calendars) const {
    const float top = calendarRowRect(calendars).top + 3.0f * gap;
    return D2D1_RECT_F{sideLeft, top, sideRight, top + sectionLabel};
  }

  D2D1_RECT_F taskRow(int calendars, int index) const {
    const float top =
        tasksLabel(calendars).bottom + static_cast<float>(index) * (cardHeight + gap);
    return D2D1_RECT_F{sideLeft, top, sideRight, top + cardHeight};
  }

  // How many task cards fit before the bottom padding.
  int tasksThatFit(int calendars) const {
    const float room = height - padding - tasksLabel(calendars).bottom;
    return (std::max)(0, static_cast<int>((room + gap) / (cardHeight + gap)));
  }
};

// `popup` is the popup's layout on this same monitor: the sidebar is its width, the capsule is
// its capsule, and every length here is scaled by its `type`. `allDayRows` is how many chips
// the busiest day of the week has, which is the only part of the layout that depends on data.
// `detailShown` is how far the detail panel has slid in, nought to one.
// `secondClock` widens the gutter for the second zone's column of hours (phase 13).
inline AppLayout MakeAppLayout(D2D1_SIZE_F size, const PanelLayout& popup, AppView view,
                               int allDayRows, float detailShown = 0.0f,
                               bool secondClock = false) {
  AppLayout out;
  out.view = view;
  out.width = size.width;
  out.height = size.height;
  out.type = popup.type;
  const float type = out.type;
  const auto at = [type](float dip) { return std::round(dip * type); };

  out.padding = popup.padding;
  out.gap = popup.gap;
  out.radius = at(kRadiusApp);

  out.sidebarRight = popup.width;
  out.sideLeft = popup.contentLeft;
  out.sideRight = popup.contentRight;
  out.setsTop = popup.listTop;
  out.setsHeight = at(kSetsRowDip);
  out.sectionTop = out.setsTop + out.setsHeight + out.gap;
  out.sectionLabel = at(kSectionLabelDip);
  out.calendarRow = at(kCalendarRowDip);
  out.cardHeight = popup.cardHeight;

  // Centred on the month title's line, so the two headers read as one.
  out.rowHeight = popup.inputHeight;
  out.rowTop = std::round(popup.headerTop + (popup.headerHeight - out.rowHeight) / 2.0f);
  const float rowBottom = out.rowTop + out.rowHeight;
  const float left = out.sidebarRight + out.padding;
  const float right = (std::max)(left, out.width - out.padding);

  out.collapse = D2D1_RECT_F{right - out.rowHeight, out.rowTop, right, rowBottom};
  const float tabWidth = at(kTabWidthDip);
  const float tabsRight = out.collapse.left - 2.0f * out.gap;
  for (int i = 0; i < kAppViews; ++i) {
    const float tabLeft = tabsRight - static_cast<float>(kAppViews - i) * tabWidth;
    out.tabs[i] = D2D1_RECT_F{tabLeft, out.rowTop, tabLeft + tabWidth, rowBottom};
  }

  const float arrow = popup.arrowSize;
  const float arrowTop = std::round(out.rowTop + (out.rowHeight - arrow) / 2.0f);
  out.prev = D2D1_RECT_F{left, arrowTop, left + arrow, arrowTop + arrow};
  out.next = D2D1_RECT_F{out.prev.right + out.gap, arrowTop, out.prev.right + out.gap + arrow,
                         arrowTop + arrow};

  // The capsule takes what is left between the period and the tabs, within its limits; the
  // period's title gives way first because it ellipsises and the capsule cannot.
  const float inputRight = out.tabs[0].left - 3.0f * out.gap;
  const float titleMin = at(200.0f);
  const float room = inputRight - (out.next.right + 2.0f * out.gap + titleMin + 3.0f * out.gap);
  const float capsule = std::clamp(room, at(kCapsuleMinDip), at(kCapsuleMaxDip));
  out.input = D2D1_RECT_F{inputRight - capsule, out.rowTop, inputRight, rowBottom};
  out.title = D2D1_RECT_F{out.next.right + 2.0f * out.gap, out.rowTop,
                          (std::max)(out.next.right + 2.0f * out.gap,
                                     out.input.left - 3.0f * out.gap),
                          rowBottom};

  out.detailWidth = at(kDetailDip);
  const float taken = (out.detailWidth + 3.0f * out.gap) * std::clamp(detailShown, 0.0f, 1.0f);
  out.main = D2D1_RECT_F{left, rowBottom + 3.0f * out.gap, (std::max)(left, right - taken),
                         (std::max)(rowBottom + 3.0f * out.gap, out.height - out.padding)};
  out.detail = D2D1_RECT_F{right - taken + 3.0f * out.gap, out.main.top,
                           right - taken + 3.0f * out.gap + out.detailWidth, out.main.bottom};

  out.columns = view == AppView::Week ? 7 : 1;
  out.dayHeaderTop = out.main.top;
  out.dayHeaderHeight = at(kDayHeaderDip);
  out.allDayRow = at(kAllDayRowDip);
  // No chips at all is no strip at all, just the line under the headings.
  out.allDayRows = std::clamp(allDayRows, 0, kAllDayMaxRows);
  out.allDayTop = out.dayHeaderTop + out.dayHeaderHeight;
  out.allDayHeight = static_cast<float>(out.allDayRows) * (out.allDayRow + out.gap) + out.gap;
  out.gutter = at(secondClock ? kGutterTwoDip : kGutterDip);
  out.timeline = D2D1_RECT_F{out.main.left, out.allDayTop + out.allDayHeight, out.main.right,
                             (std::max)(out.allDayTop + out.allDayHeight, out.main.bottom)};
  out.columnsLeft = out.main.left + out.gutter;
  out.columnWidth = (std::max)(0.0f, (out.main.right - out.columnsLeft) /
                                         static_cast<float>(out.columns));
  out.hourHeight = at(kHourDip);
  out.minBlock = at(kMinBlockDip);

  out.monthLabelsTop = out.main.top;
  out.monthLabelsHeight = at(kMonthLabelsDip);
  out.monthGridTop = out.monthLabelsTop + out.monthLabelsHeight;
  out.monthCellWidth = (out.main.right - out.main.left) / kGridCols;
  out.monthRows = view == AppView::Quarter ? kQuarterRows : kGridRows;
  out.monthCellHeight = (std::max)(
      0.0f, (out.main.bottom - out.monthGridTop) / static_cast<float>(out.monthRows));
  out.monthLine = at(kMonthLineDip);
  return out;
}

// --- The year -------------------------------------------------------------------------------

// Twelve months, four across and three down. Each has its name, the weekday initials and the
// popup's own grid, scaled to fit: the mini month the sidebar is, twelve times.
struct YearMonth {
  D2D1_RECT_F box{};
  D2D1_RECT_F title{};
  D2D1_RECT_F weekdays{};
  PanelLayout grid;  // the popup's layout with its grid moved and resized into this box
};

inline YearMonth PlaceYearMonth(const AppLayout& app, const PanelLayout& popup, int month) {
  YearMonth out;
  const float width = (app.main.right - app.main.left) / 4.0f;
  const float height = (app.main.bottom - app.main.top) / 3.0f;
  const float left = app.main.left + static_cast<float>(month % 4) * width;
  const float top = app.main.top + static_cast<float>(month / 4) * height;
  const float pad = 2.0f * app.gap;
  out.box = D2D1_RECT_F{left + pad, top + pad / 2.0f, left + width - pad, top + height - pad};
  const float titleHeight = std::round(24.0f * app.type);
  const float weekdayHeight = std::round(18.0f * app.type);
  out.title = D2D1_RECT_F{out.box.left, out.box.top, out.box.right, out.box.top + titleHeight};
  out.weekdays = D2D1_RECT_F{out.box.left, out.title.bottom, out.box.right,
                             out.title.bottom + weekdayHeight};

  PanelLayout& grid = out.grid;
  grid = popup;
  grid.contentLeft = out.box.left;
  grid.contentRight = out.box.right;
  grid.contentWidth = out.box.right - out.box.left;
  grid.gridTop = out.weekdays.bottom;
  grid.cellWidth = grid.contentWidth / kGridCols;
  // Never taller than the popup's rows: a roomier year is more air, not bigger numbers.
  grid.cellHeight = (std::min)(popup.cellHeight, (out.box.bottom - grid.gridTop) / kGridRows);
  grid.gridHeight = grid.cellHeight * kGridRows;
  const float scale = grid.cellHeight / popup.cellHeight;
  grid.dayCircle = std::round((std::min)(popup.dayCircle * scale, grid.cellWidth - 4.0f));
  grid.dayCenterY = std::round(popup.dayCenterY * scale);
  grid.dotCenterY = grid.dayCenterY + grid.dayCircle / 2.0f + grid.eventDot / 2.0f;
  return out;
}

// The day under the pointer in the year, if any.
inline std::optional<Date> YearDayAt(const AppLayout& app, const PanelLayout& popup, Date anchor,
                                     float x, float y) {
  for (int m = 0; m < 12; ++m) {
    const YearMonth month = PlaceYearMonth(app, popup, m);
    if (x < month.box.left || x >= month.box.right || y < month.grid.gridTop ||
        y >= month.grid.gridTop + month.grid.gridHeight) {
      continue;
    }
    const int column = static_cast<int>((x - month.box.left) / month.grid.cellWidth);
    const int row = static_cast<int>((y - month.grid.gridTop) / month.grid.cellHeight);
    const Month shown{anchor.year(), std::chrono::month{static_cast<unsigned>(m + 1)}};
    const Date date = CellDate(shown, row * kGridCols + std::clamp(column, 0, kGridCols - 1));
    // A day of the month before or after is drawn muted, and belongs to its own month's box.
    if (date.month() != shown.month()) return std::nullopt;
    return date;
  }
  return std::nullopt;
}

// --- The list -------------------------------------------------------------------------------

// One row of the list view: a day's heading or one of its cards. `scroll` is how far the list
// has been wheeled, in pixels.
struct ListRow {
  bool heading = false;
  D2D1_RECT_F rect{};
  Date day{};
  int index = -1;  // the day's place in AppModel::days, and for a card its place in that day
  int item = -1;
};

// The days with something on them, one after another: a column for the date on the left and
// the cards to its right, as tall as the day needs. A day with nothing is left out.
inline std::vector<ListRow> PlaceList(const AppLayout& app, const PanelLayout& popup, Date first,
                                      const std::vector<std::vector<DayItem>>& days,
                                      float scroll) {
  std::vector<ListRow> out;
  const float dateColumn = std::round(120.0f * app.type);
  const float cardLeft = app.main.left + dateColumn;
  const float cardRight = (std::min)(app.main.right, cardLeft + std::round(640.0f * app.type));
  float y = app.main.top - scroll;
  for (int d = 0; d < static_cast<int>(days.size()); ++d) {
    const std::vector<DayItem>& items = days[static_cast<size_t>(d)];
    if (items.empty()) continue;
    const float cards = static_cast<float>(items.size()) * (popup.cardHeight + app.gap);
    const float height = (std::max)(cards, std::round(44.0f * app.type)) + 2.0f * app.gap;
    ListRow heading;
    heading.heading = true;
    heading.day = AddDays(first, d);
    heading.index = d;
    heading.rect = D2D1_RECT_F{app.main.left, y, app.main.right, y + height};
    out.push_back(heading);
    for (int k = 0; k < static_cast<int>(items.size()); ++k) {
      const float top = y + app.gap + static_cast<float>(k) * (popup.cardHeight + app.gap);
      ListRow card;
      card.day = heading.day;
      card.index = d;
      card.item = k;
      card.rect = D2D1_RECT_F{cardLeft, top, cardRight, top + popup.cardHeight};
      out.push_back(card);
    }
    y += height;
  }
  return out;
}

// How far the list can be wheeled: its length past the bottom of the view.
inline float ListMaxScroll(const AppLayout& app, const PanelLayout& popup, Date first,
                           const std::vector<std::vector<DayItem>>& days) {
  const std::vector<ListRow> rows = PlaceList(app, popup, first, days, 0.0f);
  if (rows.empty()) return 0.0f;
  float bottom = app.main.top;
  for (const ListRow& row : rows) bottom = (std::max)(bottom, row.rect.bottom);
  return (std::max)(0.0f, bottom - app.main.bottom + 2.0f * app.gap);
}

// --- Calendar sets (phase 13) ---------------------------------------------------------------

// Which chip the calendars' switches match: 0 when nothing is off ("Todos"), a set's place plus
// one when exactly its calendars are, and -1 for none of them.
// What a set saved from the calendars as they are is called: the ones that are on, by name,
// "Personal + Trabajo". Nobody has to type anything, and the settings file can rename it.
inline int ActiveSet(const std::vector<CalendarSet>& sets, const std::vector<CalendarInfo>& calendars) {
  std::vector<std::string> off;
  for (const CalendarInfo& calendar : calendars) {
    if (calendar.hidden) off.push_back(calendar.id);
  }
  if (off.empty()) return 0;
  std::sort(off.begin(), off.end());
  for (size_t i = 0; i < sets.size(); ++i) {
    // Only the calendars that exist count: one Google no longer lists does not break a match.
    std::vector<std::string> hidden;
    for (const std::string& id : sets[i].hidden) {
      const bool exists = std::any_of(calendars.begin(), calendars.end(),
                                      [&id](const CalendarInfo& c) { return c.id == id; });
      if (exists) hidden.push_back(id);
    }
    std::sort(hidden.begin(), hidden.end());
    if (hidden == off) return static_cast<int>(i) + 1;
  }
  return -1;
}

inline std::wstring NameForSet(const std::vector<CalendarInfo>& calendars) {
  std::wstring name;
  for (const CalendarInfo& calendar : calendars) {
    if (calendar.hidden || calendar.isTaskList) continue;
    if (!name.empty()) name += L" + ";
    name += calendar.title;
  }
  return name.empty() ? std::wstring(T(L"Solo tareas", L"Tasks only")) : name;
}

// --- The detail panel ---------------------------------------------------------------------

// The text fields, in the order Tab walks them.
enum DetailField { kFieldTitle, kFieldDate, kFieldStart, kFieldEnd, kFieldLocation, kFieldNotes };
inline constexpr int kDetailFields = 6;
inline constexpr int kRepeatChoices = 5;  // Nunca, Diaria, Semanal, Mensual, Anual
// Título, Fecha, Inicio, Fin, Calendario, Ubicación, Notas, Repetición, Aviso, Invitados.
inline constexpr int kDetailLabels = 10;
inline constexpr int kLabelGuests = 9;
// Sí, Quizá, No: Google's "accepted", "tentative" and "declined".
inline constexpr int kResponseChoices = 3;
inline constexpr const char* kResponseWords[kResponseChoices] = {"accepted", "tentative",
                                                                  "declined"};
// How many guests the panel names before it says "+N".
inline constexpr int kGuestsShown = 3;

// What the keyboard reaches in the panel that is not a text field. Each came after the one
// before it, so each is appended and the others keep their numbers.
enum DetailControl {
  kControlCalendar,
  kControlRepeat,
  kControlDelete,
  kControlReminder,
  kControlJoin,      // phase 13: only when there is a call
  kControlResponse,  // phase 13: only when this account is a guest
};

// The order Tab walks the panel in, top to bottom as it reads. A text field is its own index; a
// control is kDetailFields plus its own.
inline constexpr int kDetailStops[] = {kFieldTitle,
                                       kDetailFields + kControlJoin,
                                       kFieldDate,
                                       kFieldStart,
                                       kFieldEnd,
                                       kDetailFields + kControlCalendar,
                                       kFieldLocation,
                                       kFieldNotes,
                                       kDetailFields + kControlResponse,
                                       kDetailFields + kControlRepeat,
                                       kDetailFields + kControlReminder,
                                       kDetailFields + kControlDelete};

struct DetailLayout {
  D2D1_RECT_F panel{};
  D2D1_RECT_F close{};
  D2D1_RECT_F labels[kDetailLabels]{};
  D2D1_RECT_F fields[kDetailFields]{};
  D2D1_RECT_F calendar{};
  D2D1_RECT_F repeat[kRepeatChoices]{};
  D2D1_RECT_F reminder[kReminderChoices]{};
  D2D1_RECT_F remove{};
  // Phase 13. Empty rectangles when the event has no call, no guests, or no answer to give.
  D2D1_RECT_F join{};
  D2D1_RECT_F guests{};
  D2D1_RECT_F response[kResponseChoices]{};
  float pad = 0.0f;
  float fieldHeight = 0.0f;
  float radius = 0.0f;

  // The calendars listed under the chooser while it is open, over whatever is below it.
  D2D1_RECT_F calendarOption(int index) const {
    const float top = calendar.bottom + pad / 4.0f + static_cast<float>(index) * fieldHeight;
    return D2D1_RECT_F{calendar.left, top, calendar.right, top + fieldHeight};
  }

  // Whether a stop of kDetailStops is on the panel at all for this event.
  bool Shows(int stop) const {
    const auto has = [](const D2D1_RECT_F& rect) { return rect.right > rect.left; };
    if (stop == kDetailFields + kControlJoin) return has(join);
    if (stop == kDetailFields + kControlResponse) return has(response[0]);
    return true;
  }
};

// This account among the guests, when it is one and not the one who invited them.
inline const Attendee* SelfGuest(const EventDetail& event) {
  for (const Attendee& guest : event.attendees) {
    if (guest.self) return guest.organizer ? nullptr : &guest;
  }
  return nullptr;
}

inline DetailLayout MakeDetailLayout(const AppLayout& app, const EventDetail& event) {
  DetailLayout out;
  const float type = app.type;
  const auto at = [type](float dip) { return std::round(dip * type); };
  out.panel = app.detail;
  out.pad = at(16.0f);
  out.fieldHeight = at(32.0f);
  out.radius = at(kRadiusCard);
  const float label = at(18.0f);
  const float under = at(4.0f);
  const float row = at(12.0f);
  const float left = out.panel.left + out.pad;
  const float right = out.panel.right - out.pad;
  const float close = at(28.0f);
  float y = out.panel.top + out.pad;

  out.close = D2D1_RECT_F{right - close, y - at(4.0f), right, y - at(4.0f) + close};
  const auto labelled = [&](int index, float labelRight) {
    out.labels[index] = D2D1_RECT_F{left, y, labelRight, y + label};
    return y + label + under;
  };
  const auto field = [&](int index, float height) {
    const float top = labelled(index, index == 0 ? out.close.left - app.gap : right);
    y = top + height + row;
    return D2D1_RECT_F{left, top, right, top + height};
  };

  out.fields[kFieldTitle] = field(0, out.fieldHeight);
  out.fields[kFieldDate] = field(1, out.fieldHeight);

  // Start and end share a row, half and half.
  const float middle = std::round((left + right) / 2.0f);
  const float top = y + label + under;
  out.labels[2] = D2D1_RECT_F{left, y, middle - app.gap, y + label};
  out.labels[3] = D2D1_RECT_F{middle + app.gap, y, right, y + label};
  out.fields[kFieldStart] = D2D1_RECT_F{left, top, middle - app.gap, top + out.fieldHeight};
  out.fields[kFieldEnd] = D2D1_RECT_F{middle + app.gap, top, right, top + out.fieldHeight};
  y = top + out.fieldHeight + row;

  out.calendar = field(4, out.fieldHeight);
  out.fields[kFieldLocation] = field(5, out.fieldHeight);
  // Unirse sits on the title's line, left of the cross, where it is the first thing seen.
  if (!JoinUrl(event.conference, event.location, event.notes).empty()) {
    const float width = at(76.0f);
    out.join = D2D1_RECT_F{out.close.left - app.gap - width, out.close.top,
                           out.close.left - app.gap, out.close.bottom};
    out.labels[0].right = out.join.left - app.gap;
  }

  // The notes give up a line to the guests, so the panel still fits the window it did.
  const bool guests = !event.attendees.empty();
  out.fields[kFieldNotes] = field(6, at(guests ? 56.0f : 88.0f));

  if (guests) {
    const float line = labelled(kLabelGuests, right);
    out.guests = D2D1_RECT_F{left, line, right, line + at(20.0f)};
    y = out.guests.bottom + row;
    if (SelfGuest(event) != nullptr) {
      const float width = (right - left - static_cast<float>(kResponseChoices - 1) * app.gap) /
                          static_cast<float>(kResponseChoices);
      for (int i = 0; i < kResponseChoices; ++i) {
        const float pillLeft = left + static_cast<float>(i) * (width + app.gap);
        out.response[i] = D2D1_RECT_F{pillLeft, y, pillLeft + width, y + at(28.0f)};
      }
      y += at(28.0f) + row;
    }
  }

  const float pillTop = labelled(7, right);
  const float pill = (right - left - static_cast<float>(kRepeatChoices - 1) * app.gap) /
                     static_cast<float>(kRepeatChoices);
  for (int i = 0; i < kRepeatChoices; ++i) {
    const float pillLeft = left + static_cast<float>(i) * (pill + app.gap);
    out.repeat[i] = D2D1_RECT_F{pillLeft, pillTop, pillLeft + pill, pillTop + at(28.0f)};
  }
  y = pillTop + at(28.0f) + row;

  // The reminder: the same five pills, under the repetition.
  const float reminderTop = labelled(8, right);
  const float reminderPill =
      (right - left - static_cast<float>(kReminderChoices - 1) * app.gap) /
      static_cast<float>(kReminderChoices);
  for (int i = 0; i < kReminderChoices; ++i) {
    const float pillLeft = left + static_cast<float>(i) * (reminderPill + app.gap);
    out.reminder[i] =
        D2D1_RECT_F{pillLeft, reminderTop, pillLeft + reminderPill, reminderTop + at(28.0f)};
  }

  out.remove = D2D1_RECT_F{left, out.panel.bottom - out.pad - out.fieldHeight, right,
                           out.panel.bottom - out.pad};
  return out;
}

// --- The timeline -------------------------------------------------------------------------

// How many minutes the timeline shows at once, and how far it can scroll.
inline float VisibleMinutes(const AppLayout& layout) {
  if (layout.hourHeight <= 0.0f) return 0.0f;
  return (layout.timeline.bottom - layout.timeline.top) * 60.0f / layout.hourHeight;
}

inline float MaxScroll(const AppLayout& layout) {
  return (std::max)(0.0f, static_cast<float>(kMinutesPerDay) - VisibleMinutes(layout));
}

// `scroll` is the minute of the day at the top edge of the timeline.
inline float YForMinute(const AppLayout& layout, float scroll, float minute) {
  return layout.timeline.top + (minute - scroll) * layout.hourHeight / 60.0f;
}

// The minute under `y`, snapped to the quarter hour and kept inside the day. What a drag lands
// on in 6b, so it lives next to YForMinute and not inside the mouse handling.
inline int MinuteAtY(const AppLayout& layout, float scroll, float y) {
  if (layout.hourHeight <= 0.0f) return 0;
  const float raw = scroll + (y - layout.timeline.top) * 60.0f / layout.hourHeight;
  const int snapped = static_cast<int>(std::lround(raw / kSnapMinutes)) * kSnapMinutes;
  return std::clamp(snapped, 0, kMinutesPerDay);
}

// The day column under `x`, or -1 outside them.
inline int ColumnAt(const AppLayout& layout, float x) {
  if (layout.columnWidth <= 0.0f || x < layout.columnsLeft) return -1;
  const int column = static_cast<int>((x - layout.columnsLeft) / layout.columnWidth);
  return column < layout.columns ? column : -1;
}

// Where the timeline starts when a view opens: now a third of the way down on a view with
// today in it, and eight in the morning at the top otherwise.
inline float InitialScroll(const AppLayout& layout, bool showsToday, int nowMinute) {
  const float top = showsToday ? static_cast<float>(nowMinute) - VisibleMinutes(layout) / 3.0f
                               : 8.0f * 60.0f;
  return std::clamp(top, 0.0f, MaxScroll(layout));
}

// --- Overlapping events -----------------------------------------------------------------

// Side by side, the way every calendar does it: events that overlap, directly or through a
// chain, form a cluster, and each takes the first column free when it starts. `spans` are
// [start, end) minutes; the answer is, for each, its column and how many its cluster has.
struct Lane {
  int column = 0;
  int columns = 1;
};

inline std::vector<Lane> LayoutOverlaps(const std::vector<std::pair<int, int>>& spans) {
  std::vector<Lane> lanes(spans.size());
  std::vector<size_t> order(spans.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&spans](size_t a, size_t b) {
    if (spans[a].first != spans[b].first) return spans[a].first < spans[b].first;
    return spans[a].second > spans[b].second;
  });

  std::vector<int> columnEnds;      // when each column of the current cluster frees up
  std::vector<size_t> cluster;      // who is in it, to hand them the final count
  int clusterEnd = -1;
  const auto close = [&] {
    for (const size_t index : cluster) lanes[index].columns = static_cast<int>(columnEnds.size());
    cluster.clear();
    columnEnds.clear();
  };

  for (const size_t index : order) {
    const auto [start, end] = spans[index];
    if (start >= clusterEnd) close();
    size_t column = 0;
    while (column < columnEnds.size() && columnEnds[column] > start) ++column;
    if (column == columnEnds.size()) columnEnds.push_back(end);
    else columnEnds[column] = end;
    lanes[index].column = static_cast<int>(column);
    cluster.push_back(index);
    clusterEnd = (std::max)(clusterEnd, end);
  }
  close();
  return lanes;
}

// --- The expansion ----------------------------------------------------------------------

// Smoothstep of `x` between `from` and `to`: nought before, one after, an S in between.
inline float Ease(float from, float to, float x) {
  const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// The popup's layout with its capsule `progress` of the way to the app's. Everything else in
// the popup stays where it is -- that is the point of the sidebar being the popup.
//
// The capsule does not travel in a straight line: straight, it would cross the month grid on
// its way to the top. It goes right first, out from under the sidebar, and rises once it is
// clear of it; shrinking back runs the same path the other way, down and then in.
inline PanelLayout MorphLayout(const PanelLayout& popup, const AppLayout& app, float progress) {
  const float across = Ease(0.0f, 0.5f, progress);
  const float up = Ease(0.25f, 1.0f, progress);
  PanelLayout out = popup;
  out.inputTop = Lerp(popup.inputTop, app.input.top, up);
  out.inputLeft = Lerp(popup.inputLeft, app.input.left, across);
  out.inputRight = Lerp(popup.inputRight, app.input.right, across);
  out.previewBelow = up > 0.5f;
  return out;
}

// Lengths in physical pixels between the popup's rectangle and the app's.
inline RECT LerpRect(const RECT& from, const RECT& to, float t) {
  const auto mix = [t](LONG a, LONG b) {
    return static_cast<LONG>(std::lround(static_cast<float>(a) +
                                         (static_cast<float>(b) - static_cast<float>(a)) * t));
  };
  return RECT{mix(from.left, to.left), mix(from.top, to.top), mix(from.right, to.right),
              mix(from.bottom, to.bottom)};
}

}  // namespace agenda
