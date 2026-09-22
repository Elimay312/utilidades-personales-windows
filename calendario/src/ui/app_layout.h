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

#include "ui/layout.h"
#include "ui/paint.h"

namespace agenda {

enum class AppView { Day, Week, Month };

// The size the app is designed at: 80 % of a 1920x1032 work area. The snapshots render it, so
// a committed PNG never depends on the monitor that produced it.
inline constexpr float kAppBaseWidthDip = 1536.0f;
inline constexpr float kAppBaseHeightDip = 826.0f;

// Metrics at the size the design system is written at; scaled by the popup's `type`, so the
// letters in the app are the letters in the popup.
inline constexpr float kHourDip = 48.0f;          // one hour of timeline
inline constexpr float kGutterDip = 56.0f;        // the hour labels to the left of it
inline constexpr float kDayHeaderDip = 44.0f;     // weekday and number above each column
inline constexpr float kAllDayRowDip = 22.0f;     // a chip in the all-day strip
inline constexpr int kAllDayMaxRows = 3;
inline constexpr float kMinBlockDip = 20.0f;      // an event of five minutes still gets a line
inline constexpr float kTabWidthDip = 72.0f;
inline constexpr float kCapsuleMaxDip = 460.0f;
inline constexpr float kCapsuleMinDip = 220.0f;
inline constexpr float kSectionLabelDip = 24.0f;  // "Calendarios", "Sin fecha"
inline constexpr float kCalendarRowDip = 28.0f;
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
  D2D1_RECT_F tabs[3]{};
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

  // Month.
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
inline AppLayout MakeAppLayout(D2D1_SIZE_F size, const PanelLayout& popup, AppView view,
                               int allDayRows, float detailShown = 0.0f) {
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
  out.sectionTop = popup.listTop;
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
  for (int i = 0; i < 3; ++i) {
    const float tabLeft = tabsRight - static_cast<float>(3 - i) * tabWidth;
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
  out.gutter = at(kGutterDip);
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
  out.monthCellHeight = (std::max)(0.0f, (out.main.bottom - out.monthGridTop) / kGridRows);
  out.monthLine = at(kMonthLineDip);
  return out;
}

// --- The detail panel ---------------------------------------------------------------------

// The text fields, in the order Tab walks them.
enum DetailField { kFieldTitle, kFieldDate, kFieldStart, kFieldEnd, kFieldLocation, kFieldNotes };
inline constexpr int kDetailFields = 6;
inline constexpr int kRepeatChoices = 5;  // Nunca, Diaria, Semanal, Mensual, Anual
// Título, Fecha, Inicio, Fin, Calendario, Ubicación, Notas, Repetición.
inline constexpr int kDetailLabels = 8;

// What the keyboard reaches in the panel that is not a text field.
enum DetailControl { kControlCalendar, kControlRepeat, kControlDelete };

// The order Tab walks the panel in, top to bottom as it reads. A text field is its own index; a
// control is kDetailFields plus its own.
inline constexpr int kDetailStops[] = {kFieldTitle,
                                       kFieldDate,
                                       kFieldStart,
                                       kFieldEnd,
                                       kDetailFields + kControlCalendar,
                                       kFieldLocation,
                                       kFieldNotes,
                                       kDetailFields + kControlRepeat,
                                       kDetailFields + kControlDelete};

struct DetailLayout {
  D2D1_RECT_F panel{};
  D2D1_RECT_F close{};
  D2D1_RECT_F labels[kDetailLabels]{};
  D2D1_RECT_F fields[kDetailFields]{};
  D2D1_RECT_F calendar{};
  D2D1_RECT_F repeat[kRepeatChoices]{};
  D2D1_RECT_F remove{};
  float pad = 0.0f;
  float fieldHeight = 0.0f;
  float radius = 0.0f;

  // The calendars listed under the chooser while it is open, over whatever is below it.
  D2D1_RECT_F calendarOption(int index) const {
    const float top = calendar.bottom + pad / 4.0f + static_cast<float>(index) * fieldHeight;
    return D2D1_RECT_F{calendar.left, top, calendar.right, top + fieldHeight};
  }
};

inline DetailLayout MakeDetailLayout(const AppLayout& app) {
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
  out.fields[kFieldNotes] = field(6, at(88.0f));

  const float pillTop = labelled(7, right);
  const float pill = (right - left - static_cast<float>(kRepeatChoices - 1) * app.gap) /
                     static_cast<float>(kRepeatChoices);
  for (int i = 0; i < kRepeatChoices; ++i) {
    const float pillLeft = left + static_cast<float>(i) * (pill + app.gap);
    out.repeat[i] = D2D1_RECT_F{pillLeft, pillTop, pillLeft + pill, pillTop + at(28.0f)};
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
