#pragma once

#include <d2d1.h>

#include <string>
#include <vector>

#include "core/dates.h"
#include "data/model.h"
#include "ui/app_layout.h"
#include "ui/paint.h"
#include "ui/popup_view.h"
#include "ui/theme.h"

namespace agenda {

// What the expanded app paints from, on top of the PopupModel it shares with the popup: the
// selected day, the month of the mini grid, the capsule and its preview all live there, because
// they are the same things on screen before and after the expansion.
struct AppModel {
  AppView view = AppView::Day;
  Date first{};                             // the first day on screen
  std::vector<std::vector<DayItem>> days;   // one list per day on screen: 1, 7 or 42
  std::vector<CalendarInfo> calendars;      // the sidebar's switches
  std::vector<DayItem> undated;             // the tray: tasks with no date
  float scroll = 8.0f * 60.0f;              // the minute at the top of the timeline
  int nowMinute = 0;

  // Hover, walked like the popup's: the tabs, the collapse button, the period arrows and the
  // calendar rows.
  float tabHover[3] = {};
  float collapseHover = 0.0f;
  float prevHover = 0.0f;
  float nextHover = 0.0f;
  std::vector<float> calendarHover;
};

// The busiest day's count of chips, which is how tall the all-day strip is.
int AllDayRows(const AppModel& model);

// "Martes 22 de septiembre", "21 – 27 de septiembre", "Septiembre de 2026".
std::wstring PeriodTitle(AppView view, Date anchor);

// The whole window at `progress` of the way from the popup (0) to the app (1). With 0 it is
// DrawPopup; with 1 it is the app; in between the month stays pinned to the corner, the day
// list fades out, the capsule travels to the top and the rest of the app fades in around it.
// `size` is the window's size in DIP right now, which is what the app lays itself out in.
void DrawApp(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
             const PanelLayout& popup, const AppLayout& app, const PopupModel& model,
             const AppModel& appModel, float progress, bool acrylic);

// How far into the expansion the app's own parts are visible, and how far the day list still is.
float AppAlpha(float progress);
float ListAlpha(float progress);

}  // namespace agenda
