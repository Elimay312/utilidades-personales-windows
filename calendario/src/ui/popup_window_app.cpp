// The expanded half of PopupWindow: the spring that grows the popup into the app and back,
// and what the keyboard and the mouse mean once it is the app. Same class, another file, so
// neither half has to be read to understand the other.

#include "ui/popup_window.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <format>

#include "core/dates.h"
#include "core/log.h"
#include "core/recurrence.h"
#include "sync/google.h"
#include "ui/fields.h"
#include "ui/layout.h"

namespace agenda {
namespace {

int DaysBetween(Date from, Date to) {
  return static_cast<int>((std::chrono::sys_days{to} - std::chrono::sys_days{from}).count());
}

LONG Width(const RECT& rect) { return rect.right - rect.left; }
LONG Height(const RECT& rect) { return rect.bottom - rect.top; }

// The minute under `y`, not yet snapped: a drag keeps the offset between the pointer and the
// block's start in these, so the block does not jump to put its top under the pointer.
float RawMinuteAt(const AppLayout& app, float scroll, float y) {
  return scroll + (y - app.timeline.top) * 60.0f / app.hourHeight;
}

int SnapMinute(float minute) {
  return static_cast<int>(std::lround(minute / kSnapMinutes)) * kSnapMinutes;
}

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
  appLayout_ = MakeAppLayout(SizeDip(), layout_, app_.view, AllDayRows(app_),
                             EaseOutCubic(app_.detail.t));
}

TextInput& PopupWindow::FocusedText() {
  return app_.detail.focus >= 0 ? app_.detail.fields[app_.detail.focus] : model_.input;
}

void PopupWindow::DropPending(std::vector<DayItem>& items) const {
  if (pendingDelete_.empty()) return;
  std::erase_if(items, [this](const DayItem& item) {
    for (const Pending& pending : pendingDelete_) {
      if (pending.uid == item.uid &&
          (!pending.occurrence || *pending.occurrence == item.occurrence)) {
        return true;
      }
    }
    return false;
  });
}

void PopupWindow::FlushDeletes() {
  if (pendingDelete_.empty() || store_ == nullptr) return;
  for (const Pending& pending : pendingDelete_) {
    if (pending.occurrence) {
      store_->RemoveOccurrence(pending.uid, *pending.occurrence);
    } else {
      store_->Remove(pending.uid, pending.isTask);
    }
  }
  pendingDelete_.clear();
  if (sync_ != nullptr) sync_->Push();
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
  zone_ = Zone::Events;
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
    // The buffer grows to the app now, once, while what it shows is still the popup.
    Resize(size_);
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
  zone_ = Zone::Grid;
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
    for (std::vector<DayItem>& day : app_.days) DropPending(day);
    DropPending(app_.undated);
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
    DayItem placed = item;
    if (placed.occurrence == Date{}) placed.occurrence = *day;
    list.insert(std::lower_bound(list.begin(), list.end(), placed, EarlierThan), placed);
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
      // In layers: the panel first, then the selection, the app after them.
      if (app_.detail.open) {
        CloseDetail();
      } else if (!app_.selected.empty()) {
        app_.selected.clear();
        Invalidate();
      } else {
        Contract();
      }
      return true;
    case VK_DELETE:
      if (!app_.selected.empty()) AskDelete(app_.selected);
      return true;
    case VK_RETURN:
      return true;  // nothing to create with an empty capsule, and nothing else to mean
    default:
      return false;
  }
}

bool PopupWindow::OnAppLeftDown(float x, float y) {
  // "Solo este / Toda la serie": one of the two, or anywhere else is "neither".
  if (!app_.scope.text.empty()) {
    const ScopeRects rects = PlaceScope(appLayout_);
    for (int i = 0; i < 2; ++i) {
      if (Inside(rects.options[i], x, y)) {
        AnswerScope(i);
        return true;
      }
    }
    if (!Inside(rects.bar, x, y)) CancelScope();
    return true;
  }
  // A click while "¿Borrar...?" waits is a no, whatever it lands on.
  if (!app_.confirm.empty()) {
    app_.confirm.clear();
    confirmUid_.clear();
    Invalidate();
    return true;
  }
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

  if (OnDetailLeftDown(x, y)) return true;

  const int calendars = static_cast<int>(app_.calendars.size());
  for (int i = 0; i < calendars; ++i) {
    if (!Inside(appLayout_.calendarRowRect(i), x, y)) continue;
    zone_ = Zone::Calendars;
    listFocus_ = i;
    ToggleCalendar(i);
    return true;
  }

  const int tasks = (std::min)(appLayout_.tasksThatFit(calendars),
                               static_cast<int>(app_.undated.size()));
  for (int i = 0; i < tasks; ++i) {
    const D2D1_RECT_F row = appLayout_.taskRow(calendars, i);
    if (!Inside(row, x, y)) continue;
    DayItem& item = app_.undated[static_cast<size_t>(i)];
    zone_ = Zone::Tasks;
    listFocus_ = i;
    if (Inside(CheckboxRect(layout_, row), x, y)) {
      ToggleUndated(i);
      return true;
    }
    // Anywhere else on the card picks the task up, to be dropped on an hour.
    drag_ = DragState{};
    drag_.kind = DragKind::Task;
    drag_.down = D2D1_POINT_2F{x, y};
    drag_.item = item;
    FocusInput(false);
    SetCapture(hwnd_);
    return true;
  }

  zone_ = Zone::Events;
  if (SelectAllDayAt(x, y)) return true;
  if (BeginDrag(x, y)) return true;

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

void PopupWindow::ToggleCalendar(int index) {
  if (index < 0 || index >= static_cast<int>(app_.calendars.size())) return;
  CalendarInfo& calendar = app_.calendars[static_cast<size_t>(index)];
  calendar.hidden = !calendar.hidden;  // the switch flips now; the days follow the worker
  if (store_ != nullptr) store_->SetCalendarHidden(calendar.id, calendar.hidden);
  Invalidate();
}

void PopupWindow::ToggleUndated(int index) {
  if (store_ == nullptr || index < 0 || index >= static_cast<int>(app_.undated.size())) return;
  DayItem& item = app_.undated[static_cast<size_t>(index)];
  item.done = !item.done;
  store_->SetDone(item.uid, item.done);
  if (sync_ != nullptr) sync_->Push();
  Invalidate();
}

bool PopupWindow::OnAppMouseMove(float x, float y) {
  // The answer under the pointer is the one Enter would give, as with the arrows.
  if (!app_.scope.text.empty()) {
    const ScopeRects rects = PlaceScope(appLayout_);
    for (int i = 0; i < 2; ++i) {
      if (Inside(rects.options[i], x, y) && app_.scope.pick != i) {
        app_.scope.pick = i;
        Invalidate();
      }
    }
  }
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
  // The panel slides in over the 160 ms everything that arrives takes, and the main view gives
  // way to it frame by frame.
  const float detailStep = step >= 1.0f ? 1.0f : step * kStateMs / kCardEnterMs;
  if (Settle(app_.detail.t, app_.detail.open, detailStep)) moving = true;
  Relayout();
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

// --- Dragging on the timeline ---------------------------------------------------------------

bool PopupWindow::BeginDrag(float x, float y) {
  if (app_.view == AppView::Month || !Inside(appLayout_.timeline, x, y) ||
      x < appLayout_.columnsLeft) {
    return false;
  }
  DragState drag;
  drag.down = D2D1_POINT_2F{x, y};
  const float raw = RawMinuteAt(appLayout_, app_.scroll, y);

  // The block on top wins: the last one drawn is the one the pointer sees.
  const std::vector<PlacedBlock> blocks = PlaceBlocks(appLayout_, app_);
  for (auto block = blocks.rbegin(); block != blocks.rend(); ++block) {
    if (!Inside(block->rect, x, y)) continue;
    const DayItem& item = *block->item;
    // Only an event with a start and an end on the same day moves here. A task is a moment,
    // not a span; one that runs past midnight would need two days' worth of timeline.
    const bool movable = !item.isTask && item.startMin && item.endMin &&
                         *item.endMin > *item.startMin;
    if (!movable) return true;
    const float grip = std::round(kResizeGripDip * appLayout_.type);
    drag.kind = y >= block->rect.bottom - grip ? DragKind::Resize : DragKind::Move;
    drag.item = item;
    drag.column = block->column;
    drag.origin = AddDays(app_.first, block->column);
    drag.grab = raw - static_cast<float>(*item.startMin);
    drag.length = *item.endMin - *item.startMin;
    break;
  }
  if (drag.kind == DragKind::None) {
    const int column = ColumnAt(appLayout_, x);
    if (column < 0) return false;
    drag.kind = DragKind::Create;
    drag.column = column;
    drag.anchor = SnapMinute(raw);
  }

  if (app_.detail.focus >= 0) {
    CommitField(app_.detail.focus);
    app_.detail.focus = -1;
  }
  // The field just written asked "solo este o toda la serie": that is answered first.
  if (!app_.scope.text.empty()) return true;
  FocusInput(false);
  drag_ = drag;
  SetCapture(hwnd_);
  return true;
}

void PopupWindow::UpdateDrag(float x, float y) {
  DragState& drag = drag_;
  if (!drag.moved) {
    // A click is allowed to wobble. Four DIP of it is still a click, not a drag.
    const float dx = x - drag.down.x;
    const float dy = y - drag.down.y;
    if (dx * dx + dy * dy < 16.0f * appLayout_.type * appLayout_.type) return;
    drag.moved = true;
  }

  Ghost& ghost = app_.ghost;
  ghost.on = true;
  ghost.free = false;
  const float raw = RawMinuteAt(appLayout_, app_.scroll, y);
  const int column = ColumnAt(appLayout_, x);
  switch (drag.kind) {
    case DragKind::Create: {
      const int here = SnapMinute(raw);
      ghost.column = drag.column;
      ghost.start = (std::min)(drag.anchor, here);
      ghost.end = (std::max)(drag.anchor, here);
      if (ghost.end - ghost.start < kSnapMinutes) ghost.end = ghost.start + kSnapMinutes;
      ghost.color = app_.calendars.empty() ? 0x4A8BF5 : app_.calendars.front().color;
      for (const CalendarInfo& calendar : app_.calendars) {
        if (calendar.isDefault && !calendar.isTaskList) ghost.color = calendar.color;
      }
      ghost.title = T(L"Nuevo evento", L"New event");
      break;
    }
    case DragKind::Move: {
      if (column >= 0) ghost.column = column;
      else if (ghost.hideUid.empty()) ghost.column = drag.column;
      ghost.start = std::clamp(SnapMinute(raw - drag.grab), 0, kMinutesPerDay - drag.length);
      ghost.end = ghost.start + drag.length;
      ghost.color = drag.item.color;
      ghost.title = drag.item.title;
      ghost.hideUid = drag.item.uid;
      ghost.hideDay = drag.item.occurrence;
      break;
    }
    case DragKind::Resize: {
      ghost.column = drag.column;
      ghost.start = *drag.item.startMin;
      ghost.end = std::clamp(SnapMinute(raw), ghost.start + kSnapMinutes, kMinutesPerDay);
      ghost.color = drag.item.color;
      ghost.title = drag.item.title;
      ghost.hideUid = drag.item.uid;
      ghost.hideDay = drag.item.occurrence;
      break;
    }
    case DragKind::Task: {
      ghost.color = drag.item.color;
      ghost.title = drag.item.title;
      ghost.at = D2D1_POINT_2F{x, y};
      // Over the timeline it becomes the hour it would take; anywhere else it is in the hand.
      const bool over = app_.view != AppView::Month && column >= 0 &&
                        Inside(appLayout_.timeline, x, y);
      ghost.free = !over;
      if (over) {
        ghost.column = column;
        ghost.start = std::clamp(SnapMinute(raw), 0, kMinutesPerDay - 60);
        ghost.end = ghost.start + 60;
      }
      break;
    }
    case DragKind::None:
      break;
  }
  Invalidate();
}

void PopupWindow::CancelDrag() {
  if (drag_.kind == DragKind::None && !app_.ghost.on) return;
  drag_ = DragState{};
  app_.ghost = Ghost{};
  if (GetCapture() == hwnd_) ReleaseCapture();
  Invalidate();
}

void PopupWindow::EndDrag() {
  const DragState drag = drag_;
  const Ghost ghost = app_.ghost;
  drag_ = DragState{};  // before letting go of the mouse, so WM_CAPTURECHANGED is not a cancel
  app_.ghost = Ghost{};
  if (GetCapture() == hwnd_) ReleaseCapture();

  if (!drag.moved) {
    // A click. On an event it opens it; on empty timeline it lets go of whatever was open.
    if (drag.kind == DragKind::Move || drag.kind == DragKind::Resize) {
      OpenDetailFor(drag.item.uid, drag.item.occurrence);
    } else if (drag.kind == DragKind::Create) {
      app_.selected.clear();
      CloseDetail();
    }
    Invalidate();
    return;
  }

  switch (drag.kind) {
    case DragKind::Create: {
      if (store_ == nullptr) break;
      Draft draft;
      draft.title = T(L"Nuevo evento", L"New event");
      draft.day = AddDays(app_.first, ghost.column);
      draft.startMin = ghost.start;
      draft.endMin = ghost.end;
      const DayItem created = store_->Create(draft);
      if (sync_ != nullptr) sync_->Push();
      AddToApp(created, draft.day);

      EventDetail event;
      event.uid = created.uid;
      event.calendarId = store_->DefaultCalendar(false);
      event.title = draft.title;
      event.startDay = *draft.day;
      event.endDay = *draft.day;
      event.startMin = draft.startMin;
      event.endMin = draft.endMin;
      // Straight into its title, selected, so typing replaces "Nuevo evento".
      OpenDetail(event, kFieldTitle);
      break;
    }
    case DragKind::Move:
    case DragKind::Resize:
      CommitMove(ghost, drag.origin);
      break;
    case DragKind::Task:
      if (ghost.on && !ghost.free) ConvertTask(drag.item, ghost.column, ghost.start);
      break;
    case DragKind::None:
      break;
  }
  Invalidate();
}

void PopupWindow::CommitMove(const Ghost& ghost, Date origin) {
  if (store_ == nullptr) return;
  std::optional<EventDetail> event = store_->Event(ghost.hideUid);
  // A row that spans days is not moved on a timeline that shows one day's worth of it.
  if (!event || event->startDay != event->endDay) return;
  const int shift = DaysBetween(origin, AddDays(app_.first, ghost.column));
  if (shift == 0 && event->startMin == ghost.start && event->endMin == ghost.end) return;

  // A repetition asks which: the occurrence that was dragged, or all of them.
  if (!event->recurrence.empty()) {
    scopeAsk_ = ScopeAsk{};
    scopeAsk_.action = ScopeAction::Move;
    scopeAsk_.uid = event->uid;
    scopeAsk_.occurrence = origin;
    scopeAsk_.ghost = ghost;
    AskScope(event->title, false);
    return;
  }
  CommitMoveSeries(ghost, origin);
}

void PopupWindow::CommitMoveSeries(const Ghost& ghost, Date origin) {
  std::optional<EventDetail> event = store_->Event(ghost.hideUid);
  if (!event) return;
  // `origin` is the day the block was picked up from, which for a repetition is not the day the
  // series starts: the whole series moves by the same number of days.
  const Date dropped = AddDays(app_.first, ghost.column);
  const int shift = DaysBetween(origin, dropped);

  event->startDay = AddDays(event->startDay, shift);
  event->endDay = event->startDay;
  event->startMin = ghost.start;
  event->endMin = ghost.end;
  unsigned edits = 0;
  if (shift != 0 && !event->recurrence.empty()) {
    const std::wstring moved = MoveRuleTo(event->recurrence, event->startDay);
    if (moved != event->recurrence) {
      event->recurrence = moved;
      edits |= kEditRecurrence;
    }
  }
  StoreEvent(*event, edits);

  // On screen now, not when the worker is done: the block lands where it was dropped.
  for (std::vector<DayItem>& day : app_.days) {
    std::erase_if(day, [&ghost](const DayItem& item) { return item.uid == ghost.hideUid; });
  }
  DayItem landed;
  landed.uid = event->uid;
  landed.title = event->title;
  landed.startMin = event->startMin;
  landed.endMin = event->endMin;
  landed.color = ghost.color;
  landed.repeats = !event->recurrence.empty();
  AddToApp(landed, dropped);
  app_.selected = event->uid;
  // The panel follows: on the occurrence that was dropped, if it is a repetition.
  if (app_.detail.open) OpenOccurrence(*event, dropped);
}

void PopupWindow::CommitMoveOccurrence(const Ghost& ghost, Date origin) {
  std::optional<EventDetail> series = store_->Event(ghost.hideUid);
  if (!series) return;
  const Date dropped = AddDays(app_.first, ghost.column);
  EventDetail moved = *series;
  moved.startDay = dropped;
  moved.endDay = dropped;
  moved.startMin = ghost.start;
  moved.endMin = ghost.end;
  moved.recurrence.clear();
  moved.uid = store_->DetachOccurrence(series->uid, origin, moved, 0);
  if (sync_ != nullptr) sync_->Push();

  // On screen now: that one occurrence leaves its day, and lands as an event of its own.
  for (std::vector<DayItem>& day : app_.days) {
    std::erase_if(day, [&](const DayItem& item) {
      return item.uid == series->uid && item.occurrence == origin;
    });
  }
  DayItem landed;
  landed.uid = moved.uid;
  landed.title = moved.title;
  landed.startMin = moved.startMin;
  landed.endMin = moved.endMin;
  landed.color = ghost.color;
  AddToApp(landed, dropped);
  app_.selected = moved.uid;
  if (app_.detail.open) OpenDetail(moved, -1);
}

void PopupWindow::ConvertTask(const DayItem& task, int column, int start) {
  if (store_ == nullptr) return;
  FlushDeletes();
  Draft draft;
  draft.title = task.title;
  draft.day = AddDays(app_.first, column);
  draft.startMin = start;
  draft.endMin = start + 60;
  const DayItem created = store_->Create(draft);
  AddToApp(created, draft.day);

  // The task goes when the notice does, so undo can still give it back as it was.
  pendingDelete_.push_back(Pending{task.uid, true});
  DropPending(app_.undated);
  undo_ = Undone{created.uid, false, {}, UndoKind::Converted, task.uid};
  ShowToast(std::wstring(T(L"Ahora es un evento · Deshacer", L"Now an event · Undo")));
  if (sync_ != nullptr) sync_->Push();
  app_.selected = created.uid;
  // With the panel open it follows the selection, or it would go on editing another event.
  if (app_.detail.open) {
    EventDetail event;
    event.uid = created.uid;
    event.calendarId = store_->DefaultCalendar(false);
    event.title = draft.title;
    event.startDay = *draft.day;
    event.endDay = *draft.day;
    event.startMin = draft.startMin;
    event.endMin = draft.endMin;
    OpenDetail(event, -1);
  }
  Relayout();
}

bool PopupWindow::SelectAllDayAt(float x, float y) {
  if (app_.view == AppView::Month) return false;
  for (int i = 0; i < appLayout_.columns && i < static_cast<int>(app_.days.size()); ++i) {
    std::vector<const DayItem*> chips;
    for (const DayItem& item : app_.days[static_cast<size_t>(i)]) {
      if (!item.startMin) chips.push_back(&item);
    }
    const int total = static_cast<int>(chips.size());
    for (int row = 0; row < appLayout_.allDayRows && row < total; ++row) {
      if (!Inside(appLayout_.allDayChip(i, row), x, y)) continue;
      // The last chip of a full strip is the "+N más", not an item.
      const bool counter = row == appLayout_.allDayRows - 1 && total > appLayout_.allDayRows;
      const DayItem& item = *chips[static_cast<size_t>(row)];
      if (!counter && !item.isTask) OpenDetailFor(item.uid, item.occurrence);
      return true;
    }
  }
  return false;
}

// --- The detail panel -----------------------------------------------------------------------

void PopupWindow::OpenDetailFor(const std::wstring& uid, std::optional<Date> occurrence) {
  if (store_ == nullptr) return;
  if (const std::optional<EventDetail> event = store_->Event(uid)) {
    OpenOccurrence(*event, occurrence ? occurrence : OccurrenceOf(uid));
  }
}

void PopupWindow::OpenOccurrence(EventDetail event, std::optional<Date> occurrence) {
  if (event.recurrence.empty() || !occurrence) {
    OpenDetail(event, -1);
    return;
  }
  // The dates of the occurrence, not the series' first ones: that is the one being looked at.
  event.endDay = AddDays(event.endDay, DaysBetween(event.startDay, *occurrence));
  event.startDay = *occurrence;
  OpenDetail(event, -1);
  app_.detail.occurrence = occurrence;
}

// The occurrence of a repetition on screen, for the callers that only know the uid.
// ponytail: the first one in the view; the keyboard's selection could remember its day instead.
std::optional<Date> PopupWindow::OccurrenceOf(const std::wstring& uid) const {
  for (const std::vector<DayItem>& day : app_.days) {
    for (const DayItem& item : day) {
      if (item.uid == uid && item.repeats) return item.occurrence;
    }
  }
  return std::nullopt;
}

void PopupWindow::OpenDetail(const EventDetail& event, int focus) {
  DetailModel& detail = app_.detail;
  if (detail.focus >= 0 && detail.event.uid != event.uid) CommitField(detail.focus);
  detail.open = true;
  detail.event = event;
  detail.invalid = 0;
  detail.calendarOpen = false;
  detail.focus = -1;
  detail.occurrence.reset();
  detail.wholeSeries = false;
  detail.fields[kFieldNotes].AllowNewlines(true);
  FillDetailFields();
  app_.selected = event.uid;
  if (focus >= 0) {
    FocusDetail(focus);
    detail.fields[focus].SelectAll();
  }
  RestartCaret();
  StartTicking();
  Invalidate();
}

void PopupWindow::CloseDetail() {
  DetailModel& detail = app_.detail;
  if (!detail.open) return;
  if (detail.focus >= 0) CommitField(detail.focus);
  detail.open = false;
  detail.focus = -1;
  detail.control = -1;
  detail.calendarOpen = false;
  if (zone_ == Zone::Detail) zone_ = Zone::Events;
  RestartCaret();
  StartTicking();
  Invalidate();
}

void PopupWindow::FillDetailFields() {
  DetailModel& detail = app_.detail;
  const EventDetail& event = detail.event;
  const std::wstring texts[kDetailFields] = {
      event.title,
      DayFieldText(event.startDay),
      event.startMin ? TimeFieldText(*event.startMin) : std::wstring(),
      event.endMin ? TimeFieldText(*event.endMin) : std::wstring(),
      event.location,
      event.notes};
  for (int i = 0; i < kDetailFields; ++i) {
    detail.fields[i].Clear();
    detail.fields[i].Insert(texts[i]);
  }
}

void PopupWindow::FocusDetail(int field) {
  DetailModel& detail = app_.detail;
  detail.control = -1;
  zone_ = Zone::Detail;
  if (detail.focus == field) return;
  if (detail.focus >= 0) CommitField(detail.focus);
  if (inputFocused_) FocusInput(false);
  detail.focus = field;
  detail.calendarOpen = false;
  RestartCaret();
  Invalidate();
}

bool PopupWindow::CommitField(int field) {
  DetailModel& detail = app_.detail;
  if (field < 0 || field >= kDetailFields) return true;
  EventDetail event = detail.event;
  const std::wstring& text = detail.fields[field].text();
  unsigned edits = 0;
  bool valid = true;

  switch (field) {
    case kFieldTitle: {
      const std::wstring_view title = detail::Trim(text);
      valid = !title.empty();
      if (valid) event.title = std::wstring(title);
      break;
    }
    case kFieldDate: {
      const std::optional<Date> day = ReadDayField(text, model_.today);
      valid = day.has_value();
      if (valid) {
        // The whole event moves; one that spans days keeps its length in days.
        event.endDay = AddDays(event.endDay, DaysBetween(event.startDay, *day));
        event.startDay = *day;
      }
      break;
    }
    case kFieldStart: {
      if (detail::Trim(text).empty() && !event.startMin) break;  // all day, and staying so
      const std::optional<int> minute = ReadTimeField(text);
      valid = minute && *minute < kMinutesPerDay;
      if (valid) {
        // The length is kept: moving the start moves the end with it.
        const int length = event.startMin && event.endMin && event.startDay == event.endDay &&
                                   *event.endMin > *event.startMin
                               ? *event.endMin - *event.startMin
                               : 60;
        if (!event.startMin) event.endDay = event.startDay;  // an all-day one takes an hour
        event.startMin = *minute;
        event.endMin = (std::min)(*minute + length, kMinutesPerDay);
      }
      break;
    }
    case kFieldEnd: {
      if (detail::Trim(text).empty() && !event.startMin) break;
      const std::optional<int> minute = ReadTimeField(text);
      valid = minute && event.startMin &&
              (event.endDay != event.startDay || *minute > *event.startMin);
      if (valid) event.endMin = *minute;
      break;
    }
    case kFieldLocation:
      if (text != event.location) edits |= kEditLocation;
      event.location = text;
      break;
    case kFieldNotes:
      event.notes = text;
      break;
    default:
      break;
  }

  if (!valid) {
    detail.invalid |= 1u << field;
    Invalidate();
    return false;
  }
  detail.invalid &= ~(1u << field);
  const bool changed = event.title != detail.event.title ||
                       event.startDay != detail.event.startDay ||
                       event.endDay != detail.event.endDay ||
                       event.startMin != detail.event.startMin ||
                       event.endMin != detail.event.endMin ||
                       event.location != detail.event.location ||
                       event.notes != detail.event.notes;
  // SaveDetail writes the fields back the way the panel writes them: "5pm" reads "17:00".
  if (changed) {
    SaveDetail(event, edits);
  } else {
    FillDetailFields();
  }
  return true;
}

void PopupWindow::SaveDetail(const EventDetail& event, unsigned edits) {
  if (store_ == nullptr) return;
  DetailModel& detail = app_.detail;
  if (detail.occurrence && detail.event.uid == event.uid) {
    // Which calendar it is in and how it repeats belong to the series: Google keeps neither
    // for one occurrence. Anything else asks, until the answer was the whole series.
    const bool seriesOnly = event.calendarId != detail.event.calendarId ||
                            event.recurrence != detail.event.recurrence;
    if (seriesOnly || detail.wholeSeries) {
      SaveAsSeries(event, edits);
      return;
    }
    scopeAsk_ = ScopeAsk{};
    scopeAsk_.action = ScopeAction::Edit;
    scopeAsk_.uid = event.uid;
    scopeAsk_.occurrence = *detail.occurrence;
    scopeAsk_.shown = event;
    scopeAsk_.edits = edits;
    AskScope(detail.event.title, false);
    return;
  }

  StoreEvent(event, edits);
  // The panel says what was just written, whoever wrote it: a drag on the timeline moves the
  // hours in the fields too. Only the field being typed in could hold anything unwritten, and
  // every caller has committed it by now.
  if (detail.event.uid == event.uid) {
    detail.event = event;
    FillDetailFields();
  }
  // The rest arrives with the worker's "something changed", a moment from now.
}

void PopupWindow::ChooseReminder(ReminderChoice choice) {
  const DetailModel& detail = app_.detail;
  if (!detail.open || ReminderOf(detail.event.reminders) == choice) return;
  EventDetail event = detail.event;
  // The e-mail reminders Google has for it ride along untouched.
  event.reminders = RemindersFor(choice, event.reminders);
  SaveDetail(event, kEditReminders);
}

void PopupWindow::StoreEvent(const EventDetail& event, unsigned edits) {
  store_->UpdateEvent(event, edits);
  if (sync_ != nullptr) sync_->Push();
}

// "Toda la serie" from the panel: what it shows is one occurrence, so a new date there moves the
// series by as many days, the way dragging the series does.
void PopupWindow::SaveAsSeries(const EventDetail& shown, unsigned edits) {
  DetailModel& detail = app_.detail;
  const std::optional<EventDetail> series = store_->Event(shown.uid);
  if (!series || !detail.occurrence) return;
  const int shift = DaysBetween(*detail.occurrence, shown.startDay);
  EventDetail out = shown;
  out.startDay = AddDays(series->startDay, shift);
  out.endDay = AddDays(out.startDay, DaysBetween(shown.startDay, shown.endDay));
  if (shift != 0 && !out.recurrence.empty() && out.recurrence == series->recurrence) {
    const std::wstring moved = MoveRuleTo(out.recurrence, out.startDay);
    if (moved != out.recurrence) {
      out.recurrence = moved;
      edits |= kEditRecurrence;
    }
  }
  StoreEvent(out, edits);
  detail.event = shown;
  detail.event.recurrence = out.recurrence;
  detail.occurrence = shown.startDay;
  FillDetailFields();
}

// "Solo este" from the panel: the occurrence becomes an event of its own, and the panel stays on
// it -- no longer a repetition, so nothing it does asks again.
void PopupWindow::DetachShown(const EventDetail& shown, unsigned edits) {
  DetailModel& detail = app_.detail;
  if (!detail.occurrence) return;
  EventDetail detached = shown;
  detached.recurrence.clear();
  detached.uid = store_->DetachOccurrence(shown.uid, *detail.occurrence, detached, edits);
  if (sync_ != nullptr) sync_->Push();
  detail.event = detached;
  detail.occurrence.reset();
  app_.selected = detached.uid;
  FillDetailFields();
}

bool PopupWindow::OnDetailKeyDown(WPARAM key) {
  DetailModel& detail = app_.detail;
  const bool shift = GetKeyState(VK_SHIFT) < 0;
  const bool control = GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0;
  TextInput& input = detail.fields[detail.focus];

  if (control) {
    switch (key) {
      case 0x41:  // Ctrl+A
        input.SelectAll();
        break;
      case 0x43:  // Ctrl+C
        CopySelection(/*cut=*/false);
        break;
      case 0x58:  // Ctrl+X
        CopySelection(/*cut=*/true);
        break;
      case 0x56:  // Ctrl+V
        Paste();
        break;
      default:
        return false;
    }
    RestartCaret();
    Invalidate();
    return true;
  }

  switch (key) {
    case VK_ESCAPE:
      // Esc closes the panel; what was typed is kept, the way leaving the field keeps it.
      CloseDetail();
      return true;
    case VK_RETURN:
      if (detail.focus == kFieldNotes && shift) {
        input.Insert(L"\n");
        break;
      }
      if (CommitField(detail.focus)) {
        detail.focus = -1;
        RestartCaret();
      }
      Invalidate();
      return true;
    case VK_TAB:
      focusVisible_ = true;
      MoveDetailStop(shift);
      Invalidate();
      return true;
    case VK_LEFT:
      input.MoveLeft(shift);
      break;
    case VK_RIGHT:
      input.MoveRight(shift);
      break;
    case VK_HOME:
      input.MoveHome(shift);
      break;
    case VK_END:
      input.MoveEnd(shift);
      break;
    case VK_BACK:
      input.Backspace();
      break;
    case VK_DELETE:
      input.DeleteForward();
      break;
    case VK_UP:
    case VK_DOWN:
      return true;  // one line, or notes with no line walking: nothing to go to
    default:
      return false;
  }
  RestartCaret();
  Invalidate();
  return true;
}

bool PopupWindow::OnDetailLeftDown(float x, float y) {
  DetailModel& detail = app_.detail;
  if (!detail.open) return false;
  const DetailLayout layout = MakeDetailLayout(appLayout_);

  // The open list sits over the fields below the chooser, so it answers first.
  if (detail.calendarOpen) {
    int row = 0;
    for (const CalendarInfo& calendar : app_.calendars) {
      if (calendar.isTaskList) continue;
      if (Inside(layout.calendarOption(row++), x, y)) {
        detail.calendarOpen = false;
        if (calendar.id != detail.event.calendarId) {
          EventDetail event = detail.event;
          event.calendarId = calendar.id;
          SaveDetail(event, 0);
        }
        Invalidate();
        return true;
      }
    }
    detail.calendarOpen = false;
    Invalidate();
  }

  if (!Inside(layout.panel, x, y)) {
    // A click anywhere else lets go of the field, and keeps what was typed in it.
    if (detail.focus >= 0) {
      CommitField(detail.focus);
      detail.focus = -1;
      RestartCaret();
      Invalidate();
    }
    return false;
  }

  if (Inside(layout.close, x, y)) {
    CloseDetail();
    return true;
  }
  for (int i = 0; i < kDetailFields; ++i) {
    if (!Inside(layout.fields[i], x, y)) continue;
    FocusDetail(i);
    detail.fields[i].MoveTo(FieldIndexAt(fonts_, layout.fields[i], detail.fields[i],
                                         i == kFieldNotes, appLayout_.type, x, y),
                            GetKeyState(VK_SHIFT) < 0);
    RestartCaret();
    Invalidate();
    return true;
  }
  if (detail.focus >= 0) {
    CommitField(detail.focus);
    detail.focus = -1;
    RestartCaret();
  }
  if (Inside(layout.calendar, x, y)) {
    detail.calendarOpen = true;
  } else if (Inside(layout.remove, x, y)) {
    AskDelete(detail.event.uid);
  } else {
    for (int i = 0; i < kRepeatChoices; ++i) {
      if (!Inside(layout.repeat[i], x, y)) continue;
      const auto repeat = static_cast<Repeat>(i);
      if (RepeatOf(detail.event.recurrence) != repeat) {
        EventDetail event = detail.event;
        event.recurrence = RuleFor(repeat, event.startDay);
        SaveDetail(event, kEditRecurrence);
      }
    }
    for (int i = 0; i < kReminderChoices; ++i) {
      if (Inside(layout.reminder[i], x, y)) ChooseReminder(static_cast<ReminderChoice>(i));
    }
  }
  Invalidate();
  return true;
}

bool PopupWindow::DetailCursor(float x, float y, LPCWSTR& cursor) {
  if (drag_.kind == DragKind::Resize) {
    cursor = IDC_SIZENS;
    return true;
  }
  if (app_.detail.open) {
    const DetailLayout layout = MakeDetailLayout(appLayout_);
    for (int i = 0; i < kDetailFields; ++i) {
      if (Inside(layout.fields[i], x, y)) {
        cursor = IDC_IBEAM;
        return true;
      }
    }
  }
  // The bottom edge of a block stretches it, and says so before anyone clicks.
  const float grip = std::round(kResizeGripDip * appLayout_.type);
  for (const PlacedBlock& block : PlaceBlocks(appLayout_, app_)) {
    if (!Inside(block.rect, x, y) || y < block.rect.bottom - grip) continue;
    if (block.item->isTask || !block.item->endMin) continue;
    cursor = IDC_SIZENS;
    return true;
  }
  return false;
}

// --- Deleting -------------------------------------------------------------------------------

void PopupWindow::AskDelete(const std::wstring& uid) {
  std::wstring title = app_.detail.event.uid == uid ? app_.detail.event.title : std::wstring();
  for (const std::vector<DayItem>& day : app_.days) {
    for (const DayItem& item : day) {
      if (item.uid == uid) title = item.title;
    }
  }
  if (title.empty()) return;
  if (app_.detail.focus >= 0) {
    CommitField(app_.detail.focus);
    app_.detail.focus = -1;
    RestartCaret();
  }
  if (!app_.scope.text.empty()) return;  // the field asked first

  // A repetition asks which, and that question is the confirmation as well.
  const std::optional<Date> occurrence =
      app_.detail.open && app_.detail.event.uid == uid ? app_.detail.occurrence
                                                       : OccurrenceOf(uid);
  if (occurrence) {
    scopeAsk_ = ScopeAsk{};
    scopeAsk_.action = ScopeAction::Delete;
    scopeAsk_.uid = uid;
    scopeAsk_.occurrence = *occurrence;
    AskScope(title, true);
    return;
  }
  confirmUid_ = uid;
  app_.confirm = ConfirmDeleteText(title);
  a11y_.Announce(app_.confirm);
  Invalidate();
}

void PopupWindow::ConfirmDelete() {
  const std::wstring uid = confirmUid_;
  app_.confirm.clear();
  confirmUid_.clear();
  if (uid.empty()) return;
  HideDeleted(Pending{uid, false, std::nullopt});
}

void PopupWindow::HideDeleted(const Pending& pending) {
  // Hidden now and deleted when the notice goes, so undo is only showing it again.
  FlushDeletes();
  pendingDelete_.push_back(pending);
  for (std::vector<DayItem>& day : app_.days) DropPending(day);
  DropPending(model_.day);
  if (app_.detail.event.uid == pending.uid) {
    app_.detail.focus = -1;
    CloseDetail();
  }
  app_.selected.clear();
  undo_ = Undone{pending.uid, false, {}, UndoKind::Deleted, {}};
  ShowToast(std::wstring(T(L"Borrado · Deshacer", L"Deleted · Undo")));
  Relayout();
  Invalidate();
}

// --- "Solo este / Toda la serie" ------------------------------------------------------------

void PopupWindow::AskScope(std::wstring_view title, bool deleting) {
  app_.scope.text = ScopeText(title, deleting);
  app_.scope.pick = 0;
  app_.scope.deleting = deleting;
  a11y_.Announce(std::format(L"{} {} · {}", app_.scope.text, ScopeOption(0), ScopeOption(1)));
  Invalidate();
}

void PopupWindow::AnswerScope(int pick) {
  const ScopeAsk ask = std::exchange(scopeAsk_, ScopeAsk{});
  app_.scope = ScopeQuestion{};
  if (store_ == nullptr) return;
  const bool thisOne = pick == 0;
  switch (ask.action) {
    case ScopeAction::Move:
      if (thisOne) {
        CommitMoveOccurrence(ask.ghost, ask.occurrence);
      } else {
        CommitMoveSeries(ask.ghost, ask.occurrence);
      }
      break;
    case ScopeAction::Edit:
      if (thisOne) {
        DetachShown(ask.shown, ask.edits);
      } else {
        app_.detail.wholeSeries = true;
        SaveAsSeries(ask.shown, ask.edits);
      }
      break;
    case ScopeAction::Delete:
      HideDeleted(Pending{ask.uid, false,
                          thisOne ? std::optional<Date>(ask.occurrence) : std::nullopt});
      break;
    case ScopeAction::None:
      break;
  }
  Invalidate();
}

void PopupWindow::CancelScope() {
  const ScopeAsk ask = std::exchange(scopeAsk_, ScopeAsk{});
  app_.scope = ScopeQuestion{};
  // An edit that was not answered is not written: the fields go back to what is stored.
  if (ask.action == ScopeAction::Edit && app_.detail.event.uid == ask.uid) FillDetailFields();
  Invalidate();
}

bool PopupWindow::OnScopeKey(WPARAM key) {
  switch (key) {
    case VK_LEFT:
    case VK_UP:
      app_.scope.pick = 0;
      break;
    case VK_RIGHT:
    case VK_DOWN:
      app_.scope.pick = 1;
      break;
    case VK_TAB:
      app_.scope.pick = 1 - app_.scope.pick;
      break;
    case VK_SPACE:
      eatSpace_ = true;
      [[fallthrough]];
    case VK_RETURN:
      AnswerScope(app_.scope.pick);
      return true;
    case VK_ESCAPE:
      CancelScope();
      return true;
    default:
      return true;  // nothing else happens while it waits
  }
  a11y_.Announce(std::wstring(ScopeOption(app_.scope.pick)));
  Invalidate();
  return true;
}

}  // namespace agenda
