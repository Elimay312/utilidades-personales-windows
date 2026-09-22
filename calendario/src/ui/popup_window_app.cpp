// The expanded half of PopupWindow: the spring that grows the popup into the app and back,
// and what the keyboard and the mouse mean once it is the app. Same class, another file, so
// neither half has to be read to understand the other.

#include "ui/popup_window.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>

#include "core/dates.h"
#include "core/log.h"
#include "sync/google.h"
#include "ui/layout.h"

namespace agenda {
namespace {

int DaysBetween(Date from, Date to) {
  return static_cast<int>((std::chrono::sys_days{to} - std::chrono::sys_days{from}).count());
}

LONG Width(const RECT& rect) { return rect.right - rect.left; }
LONG Height(const RECT& rect) { return rect.bottom - rect.top; }

// One notch of the wheel is an hour of timeline; a precision touchpad sends fractions of a
// notch and gets fractions of an hour, which is what makes it glide.
constexpr float kWheelMinutes = 60.0f;

}  // namespace

PanelLayout PopupWindow::ActiveLayout() const {
  if (!InApp()) return layout_;
  return MorphLayout(layout_, appLayout_, std::clamp(spring_.x, 0.0f, 1.0f));
}

D2D1_SIZE_F PopupWindow::SizeDip() const {
  const float toDip = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
  return D2D1_SIZE_F{static_cast<float>(size_.cx) * toDip, static_cast<float>(size_.cy) * toDip};
}

void PopupWindow::Relayout() {
  appLayout_ = MakeAppLayout(SizeDip(), layout_, app_.view, AllDayRows(app_));
}

void PopupWindow::Expand(Date day) {
  if (hwnd_ == nullptr || !visible_) return;
  const bool fromPopup = mode_ == Mode::Popup;
  if (fromPopup) {
    app_ = AppModel{};
    app_.view = AppView::Day;
  }
  mode_ = Mode::Morphing;
  goal_ = 1.0f;
  hoverDay_ = -1;
  FocusInput(false);
  SelectDay(day);  // rereads the day, and now that the mode says app, the app's days too

  if (fromPopup) {
    // Where the timeline starts is worked out at the size it is going to have, not at the
    // popup's: at 340 DIP wide there is no timeline to speak of.
    const float toDip = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
    const AppLayout final =
        MakeAppLayout(D2D1_SIZE_F{static_cast<float>(Width(appRect_)) * toDip,
                                  static_cast<float>(Height(appRect_)) * toDip},
                      layout_, app_.view, AllDayRows(app_));
    app_.scroll = InitialScroll(final, day == model_.today, NowMinuteLocal());
    // An app is a window among the others: it stops floating over everything.
    SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ScheduleNowTick();
    LogInfo(L"popup: expanding to {}x{} px", Width(appRect_), Height(appRect_));
  }

  if (!AnimationsEnabled()) {
    spring_.Snap(goal_);
    ApplyMorph();
    FinishMorph();
    return;
  }
  QueryPerformanceCounter(&lastFrame_);
  clock_.Run(hwnd_, kFrameMessage);
}

void PopupWindow::Contract() {
  if (!InApp() || goal_ == 0.0f) return;
  mode_ = Mode::Morphing;
  goal_ = 0.0f;
  hoverTab_ = -1;
  hoverCalendar_ = -1;
  hoverCollapse_ = false;
  hoverPeriodPrev_ = false;
  hoverPeriodNext_ = false;

  if (!AnimationsEnabled()) {
    spring_.Snap(goal_);
    ApplyMorph();
    FinishMorph();
    return;
  }
  QueryPerformanceCounter(&lastFrame_);
  clock_.Run(hwnd_, kFrameMessage);
}

void PopupWindow::StepMorph() {
  clock_.Taken();
  if (mode_ != Mode::Morphing) {
    clock_.Pause();
    return;
  }
  LARGE_INTEGER now{};
  LARGE_INTEGER frequency{};
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  const float seconds = static_cast<float>(now.QuadPart - lastFrame_.QuadPart) /
                        static_cast<float>(frequency.QuadPart);
  lastFrame_ = now;

  const bool moving = spring_.Step(seconds, goal_);
  ApplyMorph();
  if (!moving) FinishMorph();
}

void PopupWindow::ApplyMorph() {
  const RECT rect = LerpRect(popupRect_, appRect_, spring_.x);
  size_ = SIZE{Width(rect), Height(rect)};
  Relayout();
  // The content for the new size first, then the window around it, then one commit: the order
  // that gives DWM the least chance of showing one without the other. The swap chain is already
  // as big as the app, so none of this reallocates anything.
  Render();
  SetWindowPos(hwnd_, nullptr, rect.left, rect.top, size_.cx, size_.cy,
               SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOCOPYBITS |
                   SWP_NOREDRAW);
  composition_->Commit();
}

void PopupWindow::FinishMorph() {
  clock_.Pause();
  if (goal_ >= 1.0f) {
    mode_ = Mode::App;
    Invalidate();
    return;
  }

  // Back to the popup: floating again, the capsule waiting for text again.
  mode_ = Mode::Popup;
  KillTimer(hwnd_, kNowTimer);
  size_ = SIZE{Width(popupRect_), Height(popupRect_)};
  SetWindowPos(hwnd_, HWND_TOPMOST, popupRect_.left, popupRect_.top, size_.cx, size_.cy,
               SWP_NOACTIVATE);
  FocusInput(true);
  RestartCaret();
  Reload();
  Invalidate();
  // The popup goes when it loses the focus. If it was lost while this was still the app, the
  // popup never heard about it, so it goes now.
  if (GetForegroundWindow() != hwnd_) Hide();
}

void PopupWindow::ReloadApp() {
  app_.nowMinute = NowMinuteLocal();
  app_.first = FirstShown(app_.view, model_.selected);
  app_.days.assign(static_cast<size_t>(ShownDays(app_.view)), {});
  if (store_ != nullptr && store_->IsOpen()) {
    // One indexed lookup per day, 42 at most: well under the frame this runs in.
    for (size_t i = 0; i < app_.days.size(); ++i) {
      app_.days[i] = store_->ItemsForDay(AddDays(app_.first, static_cast<int>(i)), false);
    }
    app_.calendars = store_->AllCalendars();
    app_.undated = store_->UndatedTasks();
  }
  app_.calendarHover.resize(app_.calendars.size(), 0.0f);
  Relayout();
}

void PopupWindow::SetView(AppView view) {
  if (app_.view == view) return;
  app_.view = view;
  ReloadApp();
  app_.scroll = std::clamp(app_.scroll, 0.0f, MaxScroll(appLayout_));
  Invalidate();
}

void PopupWindow::MovePeriod(int direction) {
  switch (app_.view) {
    case AppView::Day:
      SelectDay(AddDays(model_.selected, direction));
      break;
    case AppView::Week:
      SelectDay(AddDays(model_.selected, 7 * direction));
      break;
    case AppView::Month:
      SelectDay(AddMonths(model_.selected, direction));
      break;
  }
}

void PopupWindow::AddToApp(const DayItem& item, std::optional<Date> day) {
  // Optimistic, like the popup's card: on screen before the worker has written it.
  if (!day) {
    app_.undated.insert(app_.undated.begin(), item);
  } else {
    const int index = DaysBetween(app_.first, *day);
    if (index < 0 || index >= static_cast<int>(app_.days.size())) return;
    std::vector<DayItem>& list = app_.days[static_cast<size_t>(index)];
    list.insert(std::lower_bound(list.begin(), list.end(), item, EarlierThan), item);
  }
  Relayout();
}

void PopupWindow::ScheduleNowTick() {
  app_.nowMinute = NowMinuteLocal();
  const Date today = TodayLocal();
  if (today != model_.today) {
    // Past midnight with the app open: today moves, and with it the circle and the red line.
    model_.today = today;
    Reload();
  }
  // Aimed at the turn of the next minute, so the line moves when the clock in the taskbar does
  // and not up to a minute later.
  SYSTEMTIME local{};
  GetLocalTime(&local);
  const UINT wait = (60u - local.wSecond) * 1000u - local.wMilliseconds + 20u;
  SetTimer(hwnd_, kNowTimer, wait, nullptr);
}

bool PopupWindow::OnAppKeyDown(WPARAM key) {
  if (inputFocused_) {
    // The first Esc lets go of the capsule and the second one folds the app away.
    if (key != VK_ESCAPE) return false;
    FocusInput(false);
    Invalidate();
    return true;
  }
  // With Ctrl down a letter is somebody else's shortcut (Ctrl+Z, Ctrl+V), not a view.
  if (GetKeyState(VK_CONTROL) < 0) return false;

  const bool timeline = app_.view != AppView::Month;
  switch (key) {
    case 'D':
      SetView(AppView::Day);
      return true;
    case 'S':
      SetView(AppView::Week);
      return true;
    case 'M':
      SetView(AppView::Month);
      return true;
    case 'T':
      SelectDay(model_.today);
      if (timeline) {
        app_.scroll = InitialScroll(appLayout_, true, NowMinuteLocal());
        Invalidate();
      }
      return true;
    case VK_LEFT:
      MovePeriod(-1);
      return true;
    case VK_RIGHT:
      MovePeriod(1);
      return true;
    case VK_UP:
    case VK_DOWN: {
      const int direction = key == VK_UP ? -1 : 1;
      if (!timeline) {
        SelectDay(AddDays(model_.selected, 7 * direction));
        return true;
      }
      app_.scroll = std::clamp(app_.scroll + 60.0f * static_cast<float>(direction), 0.0f,
                               MaxScroll(appLayout_));
      Invalidate();
      return true;
    }
    case VK_ESCAPE:
      Contract();
      return true;
    case VK_RETURN:
      return true;  // nothing to create with an empty capsule, and nothing else to mean
    default:
      return false;
  }
}

bool PopupWindow::OnAppLeftDown(float x, float y) {
  if (Inside(appLayout_.collapse, x, y)) {
    Contract();
    return true;
  }
  for (int i = 0; i < 3; ++i) {
    if (!Inside(appLayout_.tabs[i], x, y)) continue;
    FocusInput(false);
    SetView(static_cast<AppView>(i));
    return true;
  }
  if (Inside(appLayout_.prev, x, y) || Inside(appLayout_.next, x, y)) {
    FocusInput(false);
    MovePeriod(Inside(appLayout_.prev, x, y) ? -1 : 1);
    return true;
  }

  const int calendars = static_cast<int>(app_.calendars.size());
  for (int i = 0; i < calendars; ++i) {
    if (!Inside(appLayout_.calendarRowRect(i), x, y)) continue;
    CalendarInfo& calendar = app_.calendars[static_cast<size_t>(i)];
    calendar.hidden = !calendar.hidden;  // the switch flips now; the days follow the worker
    if (store_ != nullptr) store_->SetCalendarHidden(calendar.id, calendar.hidden);
    Invalidate();
    return true;
  }

  const int tasks = (std::min)(appLayout_.tasksThatFit(calendars),
                               static_cast<int>(app_.undated.size()));
  for (int i = 0; i < tasks; ++i) {
    const D2D1_RECT_F row = appLayout_.taskRow(calendars, i);
    if (!Inside(row, x, y)) continue;
    DayItem& item = app_.undated[static_cast<size_t>(i)];
    if (store_ != nullptr && Inside(CheckboxRect(layout_, row), x, y)) {
      item.done = !item.done;
      store_->SetDone(item.uid, item.done);
      if (sync_ != nullptr) sync_->Push();
      Invalidate();
    }
    return true;
  }

  if (app_.view == AppView::Month) {
    for (int i = 0; i < kGridCells; ++i) {
      if (!Inside(appLayout_.monthCell(i), x, y)) continue;
      FocusInput(false);
      SelectDay(AddDays(app_.first, i));
      return true;
    }
  } else if (app_.view == AppView::Week) {
    // A day's heading in the week opens that day.
    for (int i = 0; i < appLayout_.columns; ++i) {
      if (!Inside(appLayout_.dayHeader(i), x, y)) continue;
      FocusInput(false);
      SelectDay(AddDays(app_.first, i));
      SetView(AppView::Day);
      return true;
    }
  }
  return false;
}

bool PopupWindow::OnAppMouseMove(float x, float y) {
  int tab = -1;
  for (int i = 0; i < 3; ++i) {
    if (Inside(appLayout_.tabs[i], x, y)) tab = i;
  }
  int calendar = -1;
  for (int i = 0; i < static_cast<int>(app_.calendars.size()); ++i) {
    if (Inside(appLayout_.calendarRowRect(i), x, y)) calendar = i;
  }
  const bool collapse = Inside(appLayout_.collapse, x, y);
  const bool prev = Inside(appLayout_.prev, x, y);
  const bool next = Inside(appLayout_.next, x, y);

  const bool changed = tab != hoverTab_ || calendar != hoverCalendar_ ||
                       collapse != hoverCollapse_ || prev != hoverPeriodPrev_ ||
                       next != hoverPeriodNext_;
  hoverTab_ = tab;
  hoverCalendar_ = calendar;
  hoverCollapse_ = collapse;
  hoverPeriodPrev_ = prev;
  hoverPeriodNext_ = next;
  return changed;
}

void PopupWindow::OnWheel(int delta) {
  if (app_.view == AppView::Month) return;
  app_.scroll = std::clamp(
      app_.scroll - static_cast<float>(delta) / WHEEL_DELTA * kWheelMinutes, 0.0f,
      MaxScroll(appLayout_));
  Invalidate();
}

bool PopupWindow::TickApp(float step) {
  bool moving = false;
  for (int i = 0; i < 3; ++i) {
    if (Settle(app_.tabHover[i], hoverTab_ == i, step)) moving = true;
  }
  if (Settle(app_.collapseHover, hoverCollapse_, step)) moving = true;
  if (Settle(app_.prevHover, hoverPeriodPrev_, step)) moving = true;
  if (Settle(app_.nextHover, hoverPeriodNext_, step)) moving = true;
  for (size_t i = 0; i < app_.calendarHover.size(); ++i) {
    if (Settle(app_.calendarHover[i], hoverCalendar_ == static_cast<int>(i), step)) moving = true;
  }
  return moving;
}

}  // namespace agenda
