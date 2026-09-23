#pragma once

#include <d2d1.h>

#include <string>
#include <vector>

#include "core/dates.h"
#include "core/recurrence.h"
#include "data/model.h"
#include "ui/app_layout.h"
#include "ui/paint.h"
#include "ui/popup_view.h"
#include "ui/theme.h"

namespace agenda {

// The detail panel: one event, open for editing. `event` is what was last written, and what a
// field is compared against when it is committed; the fields are what is being typed.
struct DetailModel {
  bool open = false;
  float t = 0.0f;  // how far it has slid in, walked over kCardEnterMs
  EventDetail event;
  TextInput fields[kDetailFields];
  int focus = -1;  // the text field with the keyboard, or -1
  // The keyboard on one of the three controls that are not text -- kControlCalendar,
  // kControlRepeat, kControlDelete -- or -1. Never at the same time as `focus`.
  int control = -1;
  int calendarPick = 0;  // the entry of the open calendar list the arrows are on
  bool caretOn = false;
  unsigned invalid = 0;  // one bit per field that did not read, which turns it red
  bool calendarOpen = false;
  // Open on one occurrence of a repetition: which day of the series it is. `event` then shows
  // that day's dates, not the series' first ones. `wholeSeries` is the answer "toda la serie",
  // kept while the panel stays on it so every field does not ask again.
  std::optional<Date> occurrence;
  bool wholeSeries = false;
};

// "Solo este / Toda la serie": asked when one occurrence of a repetition is moved, edited or
// deleted, in the same capsule at the foot of the view as "¿Borrar...?", with the two answers
// in it. `text` empty means nothing is being asked; `pick` is the answer the arrows and the
// pointer are on, 0 this one and 1 the whole series, drawn filled like a default button.
struct ScopeQuestion {
  std::wstring text;
  int pick = 0;
  bool deleting = false;  // the outline goes red, as "¿Borrar...?" does
};

// What a drag is showing. On the timeline it is a block at `column`, from `start` to `end`;
// `free` is a task from the tray on its way there, drawn under the pointer at `at`. The block
// being moved (`hideUid`) is not drawn in its old place while the ghost stands in for it.
struct Ghost {
  bool on = false;
  bool free = false;
  int column = 0;
  int start = 0;
  int end = 0;
  std::uint32_t color = 0;
  std::wstring title;
  std::wstring hideUid;
  Date hideDay{};  // which occurrence of it, for a repetition; unset hides every one
  D2D1_POINT_2F at{};
};

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

  std::wstring selected;  // the event outlined, which Supr would delete
  DetailModel detail;
  Ghost ghost;
  std::wstring confirm;   // "¿Borrar «...»?" while it waits for an answer, empty otherwise
  ScopeQuestion scope;
};

// Where the question and its two answers are, shared by the drawing and the mouse.
struct ScopeRects {
  D2D1_RECT_F bar{};
  D2D1_RECT_F text{};
  D2D1_RECT_F options[2]{};
};
ScopeRects PlaceScope(const AppLayout& app);
// "Solo este" and "Toda la serie", in the interface language.
std::wstring_view ScopeOption(int index);
// The question itself: moving or editing ("«Gym» se repite. ¿Qué cambias?") or deleting.
std::wstring ScopeText(std::wstring_view title, bool deleting);

// Where each timed item of the day and week views lands, shared by the drawing and the mouse
// so a click always hits the block it looks like it hits. `end` is the end of the block as
// drawn, which a task (a moment, not a span) gets half an hour of.
struct PlacedBlock {
  int column = 0;
  const DayItem* item = nullptr;
  D2D1_RECT_F rect{};
  int start = 0;
  int end = 0;
};
std::vector<PlacedBlock> PlaceBlocks(const AppLayout& app, const AppModel& model);

// The block a ghost covers: the whole width of its column, from `start` to `end`.
D2D1_RECT_F GhostRect(const AppLayout& app, float scroll, int column, int start, int end);

// Where in a detail field a click lands, so the caret can go there.
size_t FieldIndexAt(const Fonts& fonts, const D2D1_RECT_F& rect, const TextInput& input,
                    bool multiline, float type, float x, float y);

// The busiest day's count of chips, which is how tall the all-day strip is.
int AllDayRows(const AppModel& model);

// "Martes 22 de septiembre", "21 – 27 de septiembre", "Septiembre de 2026".
std::wstring PeriodTitle(AppView view, Date anchor);

// The question Supr asks before anything is deleted, in the interface language.
std::wstring ConfirmDeleteText(std::wstring_view title);

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
