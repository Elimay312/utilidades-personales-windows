// The popup and the app without a mouse, and read aloud: where Tab goes, what the arrows mean
// once the keyboard is out of the capsule, and what UI Automation is told is on screen. Same
// class as popup_window.cpp and popup_window_app.cpp, a third file for a third concern.
//
// Tab walks the zones the way a web page walks its links. In the popup: the capsule, the month
// and the day's cards. In the app: the capsule, the events on screen, the detail panel when it
// is open, the calendars and the tray. Inside a zone the arrows move, Space ticks and Enter
// opens -- everything the mouse can do has a key.

#include "ui/popup_window.h"

#include <algorithm>
#include <chrono>
#include <format>

#include "core/dates.h"
#include "core/recurrence.h"
#include "sync/google.h"
#include "ui/layout.h"

namespace agenda {
namespace {

int DaysBetween(Date from, Date to) {
  return static_cast<int>((std::chrono::sys_days{to} - std::chrono::sys_days{from}).count());
}

std::wstring Clock(int minute) { return std::format(L"{:02}:{:02}", minute / 60, minute % 60); }

// Ids UIA sees. Fixed ones for the things there is one of, a base plus a position for the rest.
constexpr int kInputId = 1;
constexpr int kPrevMonthId = 2;
constexpr int kNextMonthId = 3;
constexpr int kGridId = 4;
constexpr int kListId = 5;
constexpr int kTitleId = 6;
constexpr int kTabIds = 7;  // 7, 8, 9
constexpr int kPrevPeriodId = 10;
constexpr int kNextPeriodId = 11;
constexpr int kCollapseId = 12;
constexpr int kEventsId = 13;
constexpr int kCalendarsId = 14;
constexpr int kTasksId = 15;
constexpr int kDetailId = 16;
constexpr int kCellIds = 100;
constexpr int kCardIds = 200;
constexpr int kEventIds = 1000;
constexpr int kCalendarIds = 3000;
constexpr int kTaskIds = 4000;
constexpr int kFieldIds = 5000;
constexpr int kChooserId = 5100;
constexpr int kDeleteId = 5101;
constexpr int kRepeatIds = 5200;
constexpr int kReminderIds = 5300;

bool Within(int id, int base, int count) { return id >= base && id < base + count; }

// What a card or a block is called out loud: its hours and its title, or what kind of task.
std::wstring ItemName(const DayItem& item) {
  if (item.isTask) {
    const std::wstring at = item.startMin ? Clock(*item.startMin) : std::wstring();
    std::wstring name = item.startMin ? std::vformat(T(L"Tarea a las {}: ", L"Task at {}: "),
                                                     std::make_wformat_args(at))
                                      : std::wstring(T(L"Tarea: ", L"Task: "));
    name += item.title;
    if (item.done) name += T(L", hecha", L", done");
    return name;
  }
  if (!item.startMin) return std::wstring(T(L"Todo el día, ", L"All day, ")) + item.title;
  std::wstring when = Clock(*item.startMin);
  if (item.endMin && *item.endMin != *item.startMin) when += L" – " + Clock(*item.endMin);
  return when + L", " + item.title;
}

}  // namespace

// --- Where Tab goes ----------------------------------------------------------------------------

bool PopupWindow::OnTab(bool back) {
  focusVisible_ = true;
  // Nullopt is the capsule, which is not a zone because inputFocused_ already says it.
  std::vector<std::optional<Zone>> stops{std::nullopt};
  if (!InApp()) {
    stops.push_back(Zone::Grid);
    if (!model_.day.empty()) stops.push_back(Zone::List);
  } else {
    stops.push_back(Zone::Events);
    if (app_.detail.open) stops.push_back(Zone::Detail);
    if (!app_.calendars.empty()) stops.push_back(Zone::Calendars);
    if (!app_.undated.empty()) stops.push_back(Zone::Tasks);
  }
  const std::optional<Zone> here =
      inputFocused_ ? std::nullopt : std::optional<Zone>(zone_);
  size_t at = 0;
  for (size_t i = 0; i < stops.size(); ++i) {
    if (stops[i] == here) at = i;
  }
  const size_t next = (at + (back ? stops.size() - 1 : 1)) % stops.size();
  if (!stops[next]) {
    FocusInput(true);  // commits whatever field of the panel had the keyboard
    app_.detail.control = -1;
    RestartCaret();
  } else {
    EnterZone(*stops[next], back);
  }
  Invalidate();
  return true;
}

void PopupWindow::EnterZone(Zone zone, bool back) {
  DetailModel& detail = app_.detail;
  if (zone != Zone::Detail) {
    if (detail.focus >= 0) CommitField(detail.focus);
    detail.focus = -1;
    detail.control = -1;
    detail.calendarOpen = false;
  }
  FocusInput(false);
  zone_ = zone;
  switch (zone) {
    case Zone::List:
      listFocus_ = back ? static_cast<int>(model_.day.size()) - 1 : 0;
      break;
    case Zone::Events:
      if (app_.selected.empty() && app_.view != AppView::Month) {
        const std::vector<const DayItem*> events = ShownEvents();
        if (!events.empty()) app_.selected = events.front()->uid;
        RevealSelected();
      }
      break;
    case Zone::Detail:
      FocusDetailStop(back ? kDetailStops[std::size(kDetailStops) - 1] : kDetailStops[0]);
      break;
    case Zone::Calendars:
      listFocus_ = back ? static_cast<int>(app_.calendars.size()) - 1 : 0;
      break;
    case Zone::Tasks: {
      const int shown = (std::min)(appLayout_.tasksThatFit(static_cast<int>(app_.calendars.size())),
                                   static_cast<int>(app_.undated.size()));
      listFocus_ = back ? shown - 1 : 0;
      break;
    }
    case Zone::Grid:
      break;
  }
  listFocus_ = (std::max)(0, listFocus_);
  RestartCaret();
}

// --- What the keys mean in each zone ----------------------------------------------------------

bool PopupWindow::OnZoneKey(WPARAM key) {
  const bool alt = GetKeyState(VK_MENU) < 0;
  const bool control = GetKeyState(VK_CONTROL) < 0 && !alt;

  if (!InApp()) {
    if (zone_ == Zone::Grid) {
      switch (key) {
        case VK_RETURN:
        case VK_SPACE:
          Expand(model_.selected);
          return true;
        case VK_PRIOR:
        case VK_NEXT:
          ChangeMonth(key == VK_PRIOR ? -1 : 1);
          return true;
        default:
          return false;
      }
    }
    if (zone_ != Zone::List) return false;
    const int count = static_cast<int>(model_.day.size());
    if (count == 0) {
      zone_ = Zone::Grid;
      return false;
    }
    switch (key) {
      case VK_UP:
      case VK_DOWN:
        listFocus_ = std::clamp(listFocus_ + (key == VK_UP ? -1 : 1), 0, count - 1);
        break;
      case VK_HOME:
      case VK_END:
        listFocus_ = key == VK_HOME ? 0 : count - 1;
        break;
      case VK_SPACE:
        ToggleFocusedCard();
        break;
      case VK_RETURN:
        OpenFocusedCard();
        break;
      default:
        return false;
    }
    Invalidate();
    return true;
  }

  switch (zone_) {
    case Zone::Calendars: {
      const int count = static_cast<int>(app_.calendars.size());
      if (count == 0) return false;
      if (key == VK_UP || key == VK_DOWN) {
        listFocus_ = std::clamp(listFocus_ + (key == VK_UP ? -1 : 1), 0, count - 1);
      } else if (key == VK_SPACE || key == VK_RETURN) {
        ToggleCalendar(listFocus_);
      } else {
        return false;
      }
      Invalidate();
      return true;
    }
    case Zone::Tasks: {
      const int count = (std::min)(appLayout_.tasksThatFit(static_cast<int>(app_.calendars.size())),
                                   static_cast<int>(app_.undated.size()));
      if (count == 0) return false;
      listFocus_ = std::clamp(listFocus_, 0, count - 1);
      if (key == VK_UP || key == VK_DOWN) {
        listFocus_ = std::clamp(listFocus_ + (key == VK_UP ? -1 : 1), 0, count - 1);
      } else if (key == VK_SPACE) {
        ToggleUndated(listFocus_);
      } else if (key == VK_RETURN) {
        // What dragging it onto an hour does, onto the next whole hour of the day on screen --
        // or nine in the morning on a day that is not today.
        const DayItem task = app_.undated[static_cast<size_t>(listFocus_)];
        const int start = model_.selected == model_.today
                              ? (std::min)((NowMinuteLocal() / 60 + 1) * 60, kMinutesPerDay - 60)
                              : 9 * 60;
        ConvertTask(task, DaysBetween(app_.first, model_.selected), start);
        zone_ = Zone::Events;
      } else {
        return false;
      }
      Invalidate();
      return true;
    }
    case Zone::Events: {
      if (app_.view == AppView::Month) {
        if (key != VK_RETURN) return false;
        SetView(AppView::Day);
        return true;
      }
      if (key == VK_PRIOR || key == VK_NEXT) {
        const float page = VisibleMinutes(appLayout_) * 0.8f;
        app_.scroll = std::clamp(app_.scroll + (key == VK_PRIOR ? -page : page), 0.0f,
                                 MaxScroll(appLayout_));
        Invalidate();
        return true;
      }
      if (app_.selected.empty()) return false;
      // The drag, by keyboard: Alt moves the block a quarter of an hour, or a day sideways, and
      // Ctrl moves only its end. Not Alt+Shift, which is Windows' own shortcut for switching
      // the keyboard layout and never arrives with its Shift.
      if (control && (key == VK_UP || key == VK_DOWN)) {
        NudgeSelected(0, 0, key == VK_UP ? -kSnapMinutes : kSnapMinutes);
        return true;
      }
      if (alt) {
        switch (key) {
          case VK_UP:
          case VK_DOWN:
            NudgeSelected(key == VK_UP ? -kSnapMinutes : kSnapMinutes, 0, 0);
            return true;
          case VK_LEFT:
          case VK_RIGHT:
            NudgeSelected(0, key == VK_LEFT ? -1 : 1, 0);
            return true;
          default:
            return false;
        }
      }
      switch (key) {
        case VK_UP:
        case VK_DOWN:
          SelectAdjacentEvent(key == VK_UP ? -1 : 1);
          Invalidate();
          return true;
        case VK_RETURN:
          OpenDetailFor(app_.selected);
          if (app_.detail.open) FocusDetailStop(kDetailStops[0]);
          Invalidate();
          return true;
        default:
          return false;
      }
    }
    default:
      return false;
  }
}

bool PopupWindow::OnDetailControlKey(WPARAM key) {
  DetailModel& detail = app_.detail;
  const bool shift = GetKeyState(VK_SHIFT) < 0;
  focusVisible_ = true;
  if (key == VK_TAB) {
    MoveDetailStop(shift);
    Invalidate();
    return true;
  }
  if (key == VK_ESCAPE) {
    if (detail.calendarOpen) {
      detail.calendarOpen = false;
    } else {
      CloseDetail();
    }
    Invalidate();
    return true;
  }

  switch (detail.control) {
    case kControlCalendar: {
      std::vector<const CalendarInfo*> calendars;
      int current = 0;
      for (const CalendarInfo& calendar : app_.calendars) {
        if (calendar.isTaskList) continue;
        if (calendar.id == detail.event.calendarId) current = static_cast<int>(calendars.size());
        calendars.push_back(&calendar);
      }
      const int count = static_cast<int>(calendars.size());
      if (count == 0) return true;
      const auto pick = [&](int index) {
        const CalendarInfo* chosen = calendars[static_cast<size_t>(index)];
        if (chosen->id == detail.event.calendarId) return;
        EventDetail event = detail.event;
        event.calendarId = chosen->id;
        SaveDetail(event, 0);
      };
      switch (key) {
        case VK_SPACE:
        case VK_RETURN:
        case VK_F4:
          if (detail.calendarOpen) {
            pick(std::clamp(detail.calendarPick, 0, count - 1));
            detail.calendarOpen = false;
          } else {
            detail.calendarOpen = true;
            detail.calendarPick = current;
          }
          break;
        case VK_UP:
        case VK_DOWN: {
          const int step = key == VK_UP ? -1 : 1;
          if (detail.calendarOpen) {
            detail.calendarPick = std::clamp(detail.calendarPick + step, 0, count - 1);
          } else {
            pick(std::clamp(current + step, 0, count - 1));
          }
          break;
        }
        default:
          return true;
      }
      break;
    }
    case kControlRepeat: {
      if (key != VK_LEFT && key != VK_RIGHT && key != VK_UP && key != VK_DOWN) return true;
      const Repeat now = RepeatOf(detail.event.recurrence);
      const int at = now == Repeat::Custom ? 0 : static_cast<int>(now);
      const int step = key == VK_LEFT || key == VK_UP ? -1 : 1;
      const auto repeat = static_cast<Repeat>(std::clamp(at + step, 0, kRepeatChoices - 1));
      if (repeat != now) {
        EventDetail event = detail.event;
        event.recurrence = RuleFor(repeat, event.startDay);
        SaveDetail(event, kEditRecurrence);
      }
      break;
    }
    case kControlReminder: {
      if (key != VK_LEFT && key != VK_RIGHT && key != VK_UP && key != VK_DOWN) return true;
      const ReminderChoice now = ReminderOf(detail.event.reminders);
      const int at = now == ReminderChoice::Custom ? 0 : static_cast<int>(now);
      const int step = key == VK_LEFT || key == VK_UP ? -1 : 1;
      ChooseReminder(static_cast<ReminderChoice>(std::clamp(at + step, 0, kReminderChoices - 1)));
      break;
    }
    case kControlDelete:
      if (key == VK_SPACE || key == VK_RETURN) AskDelete(detail.event.uid);
      break;
    default:
      break;
  }
  Invalidate();
  return true;
}

int PopupWindow::DetailStop() const {
  const DetailModel& detail = app_.detail;
  if (detail.focus >= 0) return detail.focus;
  if (detail.control >= 0) return kDetailFields + detail.control;
  return -1;
}

void PopupWindow::FocusDetailStop(int stop) {
  DetailModel& detail = app_.detail;
  zone_ = Zone::Detail;
  if (stop < kDetailFields) {
    detail.control = -1;
    FocusDetail(stop);
    detail.fields[stop].SelectAll();
  } else {
    if (detail.focus >= 0) CommitField(detail.focus);
    detail.focus = -1;
    if (inputFocused_) FocusInput(false);
    detail.control = stop - kDetailFields;
    detail.calendarOpen = false;
  }
  RestartCaret();
  Invalidate();
}

void PopupWindow::MoveDetailStop(bool back) {
  const int count = static_cast<int>(std::size(kDetailStops));
  const int stop = DetailStop();
  int at = back ? count : -1;
  for (int i = 0; i < count; ++i) {
    if (kDetailStops[i] == stop) at = i;
  }
  const int next = at + (back ? -1 : 1);
  if (next < 0) {
    EnterZone(Zone::Events, true);
  } else if (next >= count) {
    zone_ = Zone::Detail;
    OnTab(false);  // on to whatever follows the panel
  } else {
    FocusDetailStop(kDetailStops[next]);
  }
}

// --- The events on screen, by keyboard ------------------------------------------------------

std::vector<const DayItem*> PopupWindow::ShownEvents() const {
  std::vector<const DayItem*> out;
  for (const std::vector<DayItem>& day : app_.days) {
    for (const DayItem& item : day) {
      if (item.isTask) continue;
      // A repetition shown on three days of the week is one stop, not three.
      const bool seen = std::any_of(out.begin(), out.end(),
                                    [&item](const DayItem* other) { return other->uid == item.uid; });
      if (!seen) out.push_back(&item);
    }
  }
  return out;
}

void PopupWindow::SelectAdjacentEvent(int direction) {
  const std::vector<const DayItem*> events = ShownEvents();
  if (events.empty()) return;
  int at = -1;
  for (int i = 0; i < static_cast<int>(events.size()); ++i) {
    if (events[static_cast<size_t>(i)]->uid == app_.selected) at = i;
  }
  const int count = static_cast<int>(events.size());
  const int next = at < 0 ? (direction > 0 ? 0 : count - 1) : std::clamp(at + direction, 0, count - 1);
  app_.selected = events[static_cast<size_t>(next)]->uid;
  RevealSelected();
  // With the panel open it follows the selection, the way it follows a drag.
  if (app_.detail.open) {
    OpenDetailFor(app_.selected, events[static_cast<size_t>(next)]->occurrence);
  }
}

void PopupWindow::RevealSelected() {
  if (app_.view == AppView::Month) return;
  for (const std::vector<DayItem>& day : app_.days) {
    for (const DayItem& item : day) {
      if (item.uid != app_.selected || !item.startMin) continue;
      const float start = static_cast<float>(*item.startMin);
      const float end = static_cast<float>(item.endMin ? *item.endMin : *item.startMin + 60);
      const float visible = VisibleMinutes(appLayout_);
      if (start < app_.scroll) {
        app_.scroll = (std::max)(0.0f, start - 30.0f);
      } else if (end > app_.scroll + visible) {
        app_.scroll = (std::min)(MaxScroll(appLayout_), (std::max)(0.0f, end - visible + 30.0f));
      }
      return;
    }
  }
}

int PopupWindow::TodayColumnOf(const std::wstring& uid) const {
  for (int i = 0; i < static_cast<int>(app_.days.size()); ++i) {
    for (const DayItem& item : app_.days[static_cast<size_t>(i)]) {
      if (item.uid == uid) return i;
    }
  }
  return -1;
}

void PopupWindow::NudgeSelected(int minutes, int days, int endMinutes) {
  if (store_ == nullptr || app_.view == AppView::Month) return;
  const int column = TodayColumnOf(app_.selected);
  if (column < 0) return;
  const DayItem* found = nullptr;
  for (const DayItem& item : app_.days[static_cast<size_t>(column)]) {
    if (item.uid == app_.selected) found = &item;
  }
  // The same events a drag moves: one day long, with a start and an end.
  if (found == nullptr || found->isTask || !found->startMin || !found->endMin ||
      *found->endMin <= *found->startMin) {
    return;
  }
  const int start = *found->startMin;
  const int length = *found->endMin - start;
  Ghost ghost;
  ghost.hideUid = found->uid;
  ghost.color = found->color;
  ghost.title = found->title;
  ghost.column = column + days;
  if (endMinutes != 0) {
    ghost.start = start;
    ghost.end = std::clamp(start + length + endMinutes, start + kSnapMinutes, kMinutesPerDay);
  } else {
    ghost.start = std::clamp(start + minutes, 0, kMinutesPerDay - length);
    ghost.end = ghost.start + length;
  }
  CommitMove(ghost, AddDays(app_.first, column));
  // Moved off the day on screen: the view follows it, or it would vanish from under the keys.
  if (ghost.column < 0 || ghost.column >= appLayout_.columns) {
    SelectDay(AddDays(model_.selected, days));
  }
  app_.selected = ghost.hideUid;
  RevealSelected();
  a11y_.Announce(Clock(ghost.start) + L" – " + Clock(ghost.end));
  Invalidate();
}

// --- The popup's list, by keyboard ----------------------------------------------------------

void PopupWindow::OpenFocusedCard() {
  if (listFocus_ < 0 || listFocus_ >= static_cast<int>(model_.day.size())) return;
  const DayItem item = model_.day[static_cast<size_t>(listFocus_)];
  Expand(model_.selected);
  if (item.isTask) return;
  OpenDetailFor(item.uid, item.occurrence);
  if (app_.detail.open) FocusDetailStop(kDetailStops[0]);
}

void PopupWindow::ToggleFocusedCard() {
  if (listFocus_ < 0 || listFocus_ >= static_cast<int>(model_.day.size())) return;
  if (!model_.day[static_cast<size_t>(listFocus_)].isTask) return;
  ToggleDone(static_cast<size_t>(listFocus_));
}

// --- The ring -----------------------------------------------------------------------------------

void PopupWindow::UpdateRing() {
  model_.ringOn = false;
  model_.focusCard = -1;
  if (inputFocused_ || mode_ == Mode::Morphing) return;

  const float pad = std::round(3.0f * layout_.type);
  const auto set = [this](const D2D1_RECT_F& rect, float radius) {
    model_.ring = rect;
    model_.ringRadius = radius;
    model_.ringOn = focusVisible_;
  };

  if (!InApp()) {
    if (zone_ == Zone::List) {
      const int count = static_cast<int>(model_.day.size());
      if (count == 0) return;
      listFocus_ = std::clamp(listFocus_, 0, count - 1);
      model_.focusCard = listFocus_;
      const D2D1_RECT_F list = DayListRect(layout_, model_);
      const CardSlots slots = PlaceCards(layout_, list, count, KeptCard(model_));
      const int position = listFocus_ - slots.first;
      if (position >= 0 && position < slots.shown) {
        set(Inset(CardRect(layout_, list, slots, position), -pad), layout_.cardRadius + pad);
      }
    } else if (zone_ == Zone::Grid) {
      const int cell = DaysBetween(GridStart(model_.month), model_.selected);
      if (cell < 0 || cell >= kGridCells) return;
      const D2D1_RECT_F rect = layout_.cell(cell);
      const float cx = (rect.left + rect.right) / 2.0f;
      const float cy = rect.top + layout_.dayCenterY;
      const float r = layout_.dayCircle / 2.0f + pad;
      set(D2D1_RECT_F{cx - r, cy - r, cx + r, cy + r}, r);
    }
    return;
  }

  switch (zone_) {
    case Zone::Events: {
      if (app_.view == AppView::Month) {
        const int cell = DaysBetween(app_.first, model_.selected);
        if (cell >= 0 && cell < kGridCells) set(Inset(appLayout_.monthCell(cell), pad), pad * 2.0f);
        return;
      }
      const float radius = std::round(kRadiusCard * appLayout_.type) + pad;
      for (const PlacedBlock& block : PlaceBlocks(appLayout_, app_)) {
        if (block.item->uid == app_.selected) {
          set(Inset(block.rect, -pad), radius);
          return;
        }
      }
      for (int i = 0; i < appLayout_.columns && i < static_cast<int>(app_.days.size()); ++i) {
        int row = 0;
        for (const DayItem& item : app_.days[static_cast<size_t>(i)]) {
          if (item.startMin) continue;
          if (row < appLayout_.allDayRows && item.uid == app_.selected) {
            set(Inset(appLayout_.allDayChip(i, row), -pad), radius);
            return;
          }
          ++row;
        }
      }
      return;
    }
    case Zone::Detail: {
      const DetailModel& detail = app_.detail;
      if (detail.control < 0) return;
      const DetailLayout layout = MakeDetailLayout(appLayout_);
      const float radius = layout.radius + pad;
      if (detail.control == kControlCalendar) {
        set(Inset(detail.calendarOpen ? layout.calendarOption(detail.calendarPick) : layout.calendar,
                  detail.calendarOpen ? 2.0f : -pad),
            radius);
      } else if (detail.control == kControlRepeat) {
        const Repeat repeat = RepeatOf(detail.event.recurrence);
        if (repeat == Repeat::Custom) {
          const D2D1_RECT_F all{layout.repeat[0].left, layout.repeat[0].top,
                                layout.repeat[kRepeatChoices - 1].right,
                                layout.repeat[kRepeatChoices - 1].bottom};
          set(Inset(all, -pad), (all.bottom - all.top) / 2.0f + pad);
        } else {
          const D2D1_RECT_F& pill = layout.repeat[static_cast<int>(repeat)];
          set(Inset(pill, -pad), (pill.bottom - pill.top) / 2.0f + pad);
        }
      } else if (detail.control == kControlReminder) {
        const ReminderChoice reminder = ReminderOf(detail.event.reminders);
        const int last = kReminderChoices - 1;
        const D2D1_RECT_F pill =
            reminder == ReminderChoice::Custom
                ? D2D1_RECT_F{layout.reminder[0].left, layout.reminder[0].top,
                              layout.reminder[last].right, layout.reminder[last].bottom}
                : layout.reminder[static_cast<int>(reminder)];
        set(Inset(pill, -pad), (pill.bottom - pill.top) / 2.0f + pad);
      } else {
        set(Inset(layout.remove, -pad), radius);
      }
      return;
    }
    case Zone::Calendars: {
      const int count = static_cast<int>(app_.calendars.size());
      if (count == 0) return;
      listFocus_ = std::clamp(listFocus_, 0, count - 1);
      const D2D1_RECT_F row = appLayout_.calendarRowRect(listFocus_);
      set(D2D1_RECT_F{row.left - appLayout_.gap, row.top, row.right, row.bottom},
          std::round(6.0f * appLayout_.type));
      return;
    }
    case Zone::Tasks: {
      const int calendars = static_cast<int>(app_.calendars.size());
      const int count = (std::min)(appLayout_.tasksThatFit(calendars),
                                   static_cast<int>(app_.undated.size()));
      if (count == 0) return;
      listFocus_ = std::clamp(listFocus_, 0, count - 1);
      set(Inset(appLayout_.taskRow(calendars, listFocus_), -pad), layout_.cardRadius + pad);
      return;
    }
    default:
      return;
  }
}

// --- UI Automation --------------------------------------------------------------------------

std::vector<A11yNode> PopupWindow::A11yNodes() {
  std::vector<A11yNode> nodes;
  if (!visible_) return nodes;
  const bool app = mode_ == Mode::App;
  const bool keysHere = GetFocus() == hwnd_;

  A11yNode input;
  input.id = kInputId;
  input.type = UIA_EditControlTypeId;
  input.name = T(L"Escribe un evento o una tarea", L"Type an event or a task");
  input.value = model_.input.text();
  input.hasValue = true;
  input.readOnly = false;
  input.rect = ActiveLayout().input();
  input.focusable = true;
  input.focused = keysHere && inputFocused_ && app_.detail.focus < 0 && app_.detail.control < 0;
  nodes.push_back(input);

  const auto button = [&nodes](int id, std::wstring_view name, const D2D1_RECT_F& rect) {
    A11yNode node;
    node.id = id;
    node.type = UIA_ButtonControlTypeId;
    node.name = name;
    node.rect = rect;
    node.invokable = true;
    nodes.push_back(std::move(node));
  };
  button(kPrevMonthId, T(L"Mes anterior", L"Previous month"), layout_.prevArrow());
  button(kNextMonthId, T(L"Mes siguiente", L"Next month"), layout_.nextArrow());

  // The month: a calendar of six rows by seven, a day per cell.
  A11yNode grid;
  grid.id = kGridId;
  grid.type = UIA_CalendarControlTypeId;
  grid.name = std::format(L"{} {}", MonthName(model_.month.month()),
                          static_cast<int>(model_.month.year()));
  grid.rect = layout_.grid();
  grid.rows = kGridRows;
  grid.columns = kGridCols;
  nodes.push_back(grid);
  for (int cell = 0; cell < kGridCells; ++cell) {
    const Date date = CellDate(model_.month, cell);
    A11yNode day;
    day.id = kCellIds + cell;
    day.parent = kGridId;
    day.type = UIA_DataItemControlTypeId;
    day.name = PeriodTitle(AppView::Day, date);
    if (date == model_.today) day.name += T(L", hoy", L", today");
    if (std::any_of(model_.dots.begin(), model_.dots.end(),
                    [date](const DayDot& dot) { return dot.date == date; })) {
      day.name += T(L", con eventos", L", has events");
    }
    day.rect = layout_.cell(cell);
    day.row = cell / kGridCols;
    day.column = cell % kGridCols;
    day.selected = date == model_.selected ? 1 : 0;
    day.invokable = true;
    day.focusable = !InApp();
    day.focused = keysHere && !InApp() && !inputFocused_ && zone_ == Zone::Grid &&
                  date == model_.selected;
    nodes.push_back(std::move(day));
  }

  if (!InApp()) {
    const D2D1_RECT_F list = DayListRect(layout_, model_);
    A11yNode listNode;
    listNode.id = kListId;
    listNode.type = UIA_ListControlTypeId;
    listNode.name = PeriodTitle(AppView::Day, model_.selected);
    listNode.rect = list;
    nodes.push_back(listNode);
    const int count = static_cast<int>(model_.day.size());
    const CardSlots slots = PlaceCards(layout_, list, count, KeptCard(model_));
    for (int i = 0; i < count; ++i) {
      const DayItem& item = model_.day[static_cast<size_t>(i)];
      A11yNode card;
      card.id = kCardIds + i;
      card.parent = kListId;
      card.type = UIA_ListItemControlTypeId;
      card.name = ItemName(item);
      const int position = i - slots.first;
      if (position >= 0 && position < slots.shown) card.rect = CardRect(layout_, list, slots, position);
      card.focusable = true;
      card.focused = keysHere && !inputFocused_ && zone_ == Zone::List && listFocus_ == i;
      card.invokable = true;
      if (item.isTask) card.toggle = item.done ? 1 : 0;
      nodes.push_back(std::move(card));
    }
    return nodes;
  }
  if (!app) return nodes;  // mid-expansion: the popup's half is enough to be going on with

  A11yNode title;
  title.id = kTitleId;
  title.type = UIA_TextControlTypeId;
  title.name = PeriodTitle(app_.view, model_.selected);
  title.rect = appLayout_.title;
  nodes.push_back(title);
  const std::wstring_view views[3] = {T(L"Día", L"Day"), T(L"Semana", L"Week"), T(L"Mes", L"Month")};
  for (int i = 0; i < 3; ++i) {
    A11yNode tab;
    tab.id = kTabIds + i;
    tab.type = UIA_RadioButtonControlTypeId;
    tab.name = views[i];
    tab.rect = appLayout_.tabs[i];
    tab.selected = static_cast<int>(app_.view) == i ? 1 : 0;
    nodes.push_back(std::move(tab));
  }
  button(kPrevPeriodId, T(L"Periodo anterior", L"Previous period"), appLayout_.prev);
  button(kNextPeriodId, T(L"Periodo siguiente", L"Next period"), appLayout_.next);
  button(kCollapseId, T(L"Contraer", L"Collapse"), appLayout_.collapse);

  A11yNode events;
  events.id = kEventsId;
  events.type = UIA_ListControlTypeId;
  events.name = T(L"Eventos", L"Events");
  events.rect = appLayout_.main;
  nodes.push_back(events);
  const std::vector<PlacedBlock> blocks = PlaceBlocks(appLayout_, app_);
  const std::vector<const DayItem*> shown = ShownEvents();
  for (int k = 0; k < static_cast<int>(shown.size()); ++k) {
    const DayItem& item = *shown[static_cast<size_t>(k)];
    A11yNode event;
    event.id = kEventIds + k;
    event.parent = kEventsId;
    event.type = UIA_ListItemControlTypeId;
    const int column = TodayColumnOf(item.uid);
    event.name = ItemName(item);
    if (column >= 0 && app_.view != AppView::Day) {
      event.name += L", " + PeriodTitle(AppView::Day, AddDays(app_.first, column));
    }
    for (const PlacedBlock& block : blocks) {
      if (block.item->uid == item.uid) {
        event.rect = block.rect;
        break;
      }
    }
    event.selected = item.uid == app_.selected ? 1 : 0;
    event.focusable = true;
    event.focused = keysHere && !inputFocused_ && zone_ == Zone::Events && event.selected == 1;
    event.invokable = true;
    nodes.push_back(std::move(event));
  }

  A11yNode calendars;
  calendars.id = kCalendarsId;
  calendars.type = UIA_ListControlTypeId;
  calendars.name = T(L"Calendarios", L"Calendars");
  nodes.push_back(calendars);
  for (int i = 0; i < static_cast<int>(app_.calendars.size()); ++i) {
    const CalendarInfo& calendar = app_.calendars[static_cast<size_t>(i)];
    A11yNode row;
    row.id = kCalendarIds + i;
    row.parent = kCalendarsId;
    row.type = UIA_CheckBoxControlTypeId;
    row.name = calendar.title;
    row.rect = appLayout_.calendarRowRect(i);
    row.toggle = calendar.hidden ? 0 : 1;
    row.focusable = true;
    row.focused = keysHere && !inputFocused_ && zone_ == Zone::Calendars && listFocus_ == i;
    nodes.push_back(std::move(row));
  }

  A11yNode tasks;
  tasks.id = kTasksId;
  tasks.type = UIA_ListControlTypeId;
  tasks.name = T(L"Sin fecha", L"No date");
  nodes.push_back(tasks);
  const int calendarCount = static_cast<int>(app_.calendars.size());
  const int fits = (std::min)(appLayout_.tasksThatFit(calendarCount),
                              static_cast<int>(app_.undated.size()));
  for (int i = 0; i < fits; ++i) {
    const DayItem& item = app_.undated[static_cast<size_t>(i)];
    A11yNode task;
    task.id = kTaskIds + i;
    task.parent = kTasksId;
    task.type = UIA_ListItemControlTypeId;
    task.name = item.title;
    task.rect = appLayout_.taskRow(calendarCount, i);
    task.toggle = item.done ? 1 : 0;
    task.focusable = true;
    task.focused = keysHere && !inputFocused_ && zone_ == Zone::Tasks && listFocus_ == i;
    nodes.push_back(std::move(task));
  }

  const DetailModel& detail = app_.detail;
  if (!detail.open) return nodes;
  const DetailLayout layout = MakeDetailLayout(appLayout_);
  A11yNode pane;
  pane.id = kDetailId;
  pane.type = UIA_PaneControlTypeId;
  pane.name = T(L"Detalle del evento", L"Event details");
  pane.rect = layout.panel;
  nodes.push_back(pane);
  const std::wstring_view labels[kDetailFields] = {
      T(L"Título", L"Title"), T(L"Fecha", L"Date"), T(L"Inicio", L"Start"),
      T(L"Fin", L"End"), T(L"Ubicación", L"Location"), T(L"Notas", L"Notes")};
  const auto field = [&](int index) {
    A11yNode node;
    node.id = kFieldIds + index;
    node.parent = kDetailId;
    node.type = UIA_EditControlTypeId;
    node.name = labels[index];
    node.value = detail.fields[index].text();
    node.hasValue = true;
    node.readOnly = false;
    node.rect = layout.fields[index];
    node.focusable = true;
    node.focused = keysHere && detail.focus == index;
    nodes.push_back(std::move(node));
  };
  for (int i = kFieldTitle; i <= kFieldEnd; ++i) field(i);
  A11yNode chooser;
  chooser.id = kChooserId;
  chooser.parent = kDetailId;
  chooser.type = UIA_ComboBoxControlTypeId;
  chooser.name = std::wstring(T(L"Calendario", L"Calendar"));
  for (const CalendarInfo& calendar : app_.calendars) {
    if (calendar.id == detail.event.calendarId) chooser.name += L": " + calendar.title;
  }
  chooser.rect = layout.calendar;
  chooser.invokable = true;
  chooser.focusable = true;
  chooser.focused = keysHere && detail.control == kControlCalendar;
  nodes.push_back(std::move(chooser));
  field(kFieldLocation);
  field(kFieldNotes);
  const std::wstring_view repeats[kRepeatChoices] = {
      T(L"Nunca", L"Never"), T(L"Diaria", L"Daily"), T(L"Semanal", L"Weekly"),
      T(L"Mensual", L"Monthly"), T(L"Anual", L"Yearly")};
  const Repeat repeat = RepeatOf(detail.event.recurrence);
  for (int i = 0; i < kRepeatChoices; ++i) {
    A11yNode choice;
    choice.id = kRepeatIds + i;
    choice.parent = kDetailId;
    choice.type = UIA_RadioButtonControlTypeId;
    choice.name = std::wstring(T(L"Repetición: ", L"Repeat: ")) + std::wstring(repeats[i]);
    choice.rect = layout.repeat[i];
    choice.selected = static_cast<int>(repeat) == i ? 1 : 0;
    choice.focusable = true;
    choice.focused = keysHere && detail.control == kControlRepeat && choice.selected == 1;
    nodes.push_back(std::move(choice));
  }
  const std::wstring_view reminders[kReminderChoices] = {
      T(L"Los del calendario", L"The calendar's"), T(L"Ninguno", L"None"),
      T(L"10 minutos antes", L"10 minutes before"), T(L"1 hora antes", L"1 hour before"),
      T(L"1 día antes", L"1 day before")};
  const ReminderChoice reminder = ReminderOf(detail.event.reminders);
  for (int i = 0; i < kReminderChoices; ++i) {
    A11yNode choice;
    choice.id = kReminderIds + i;
    choice.parent = kDetailId;
    choice.type = UIA_RadioButtonControlTypeId;
    choice.name = std::wstring(T(L"Aviso: ", L"Reminder: ")) + std::wstring(reminders[i]);
    choice.rect = layout.reminder[i];
    choice.selected = static_cast<int>(reminder) == i ? 1 : 0;
    choice.focusable = true;
    choice.focused = keysHere && detail.control == kControlReminder && choice.selected == 1;
    nodes.push_back(std::move(choice));
  }
  A11yNode remove;
  remove.id = kDeleteId;
  remove.parent = kDetailId;
  remove.type = UIA_ButtonControlTypeId;
  remove.name = T(L"Borrar evento", L"Delete event");
  remove.rect = layout.remove;
  remove.invokable = true;
  remove.focusable = true;
  remove.focused = keysHere && detail.control == kControlDelete;
  nodes.push_back(std::move(remove));
  return nodes;
}

void PopupWindow::A11yInvoke(int id) {
  if (id == kPrevMonthId || id == kNextMonthId) {
    ChangeMonth(id == kPrevMonthId ? -1 : 1);
  } else if (Within(id, kCellIds, kGridCells)) {
    const Date date = CellDate(model_.month, id - kCellIds);
    if (InApp()) {
      SelectDay(date);
    } else {
      Expand(date);
    }
  } else if (Within(id, kCardIds, static_cast<int>(model_.day.size()))) {
    listFocus_ = id - kCardIds;
    OpenFocusedCard();
  } else if (Within(id, kTabIds, 3)) {
    SetView(static_cast<AppView>(id - kTabIds));
  } else if (id == kPrevPeriodId || id == kNextPeriodId) {
    MovePeriod(id == kPrevPeriodId ? -1 : 1);
  } else if (id == kCollapseId) {
    Contract();
  } else if (id >= kEventIds && id < kCalendarIds) {
    const std::vector<const DayItem*> events = ShownEvents();
    const size_t index = static_cast<size_t>(id - kEventIds);
    if (index < events.size()) {
      app_.selected = events[index]->uid;
      OpenDetailFor(app_.selected, events[index]->occurrence);
    }
  } else if (id == kChooserId) {
    app_.detail.calendarOpen = !app_.detail.calendarOpen;
  } else if (id == kDeleteId) {
    AskDelete(app_.detail.event.uid);
  } else {
    A11yToggle(id);
    return;
  }
  Invalidate();
}

void PopupWindow::A11yToggle(int id) {
  if (Within(id, kCardIds, static_cast<int>(model_.day.size()))) {
    listFocus_ = id - kCardIds;
    ToggleFocusedCard();
  } else if (Within(id, kCalendarIds, static_cast<int>(app_.calendars.size()))) {
    ToggleCalendar(id - kCalendarIds);
  } else if (Within(id, kTaskIds, static_cast<int>(app_.undated.size()))) {
    ToggleUndated(id - kTaskIds);
  }
  Invalidate();
}

void PopupWindow::A11ySelect(int id) {
  if (Within(id, kCellIds, kGridCells)) {
    SelectDay(CellDate(model_.month, id - kCellIds));
  } else if (Within(id, kTabIds, 3)) {
    SetView(static_cast<AppView>(id - kTabIds));
  } else if (id >= kEventIds && id < kCalendarIds) {
    const std::vector<const DayItem*> events = ShownEvents();
    const size_t index = static_cast<size_t>(id - kEventIds);
    if (index < events.size()) app_.selected = events[index]->uid;
  } else if (Within(id, kRepeatIds, kRepeatChoices) && app_.detail.open) {
    EventDetail event = app_.detail.event;
    event.recurrence = RuleFor(static_cast<Repeat>(id - kRepeatIds), event.startDay);
    SaveDetail(event, kEditRecurrence);
  } else if (Within(id, kReminderIds, kReminderChoices)) {
    ChooseReminder(static_cast<ReminderChoice>(id - kReminderIds));
  }
  Invalidate();
}

void PopupWindow::A11ySetValue(int id, const std::wstring& value) {
  if (id == kInputId) {
    FocusInput(true);
    model_.input.Clear();
    model_.input.Insert(value);
  } else if (Within(id, kFieldIds, kDetailFields) && app_.detail.open) {
    const int field = id - kFieldIds;
    app_.detail.fields[field].Clear();
    app_.detail.fields[field].Insert(value);
    CommitField(field);
  }
  RestartCaret();
  Invalidate();
}

void PopupWindow::A11yFocus(int id) {
  focusVisible_ = true;
  if (id == kInputId) {
    FocusInput(true);
  } else if (Within(id, kCellIds, kGridCells)) {
    EnterZone(Zone::Grid, false);
    SelectDay(CellDate(model_.month, id - kCellIds));
  } else if (Within(id, kCardIds, static_cast<int>(model_.day.size()))) {
    EnterZone(Zone::List, false);
    listFocus_ = id - kCardIds;
  } else if (id >= kEventIds && id < kCalendarIds) {
    const std::vector<const DayItem*> events = ShownEvents();
    const size_t index = static_cast<size_t>(id - kEventIds);
    if (index < events.size()) app_.selected = events[index]->uid;
    EnterZone(Zone::Events, false);
  } else if (Within(id, kCalendarIds, static_cast<int>(app_.calendars.size()))) {
    EnterZone(Zone::Calendars, false);
    listFocus_ = id - kCalendarIds;
  } else if (Within(id, kTaskIds, static_cast<int>(app_.undated.size()))) {
    EnterZone(Zone::Tasks, false);
    listFocus_ = id - kTaskIds;
  } else if (Within(id, kFieldIds, kDetailFields)) {
    FocusDetailStop(id - kFieldIds);
  } else if (id == kChooserId) {
    FocusDetailStop(kDetailFields + kControlCalendar);
  } else if (Within(id, kRepeatIds, kRepeatChoices)) {
    FocusDetailStop(kDetailFields + kControlRepeat);
  } else if (Within(id, kReminderIds, kReminderChoices)) {
    FocusDetailStop(kDetailFields + kControlReminder);
  } else if (id == kDeleteId) {
    FocusDetailStop(kDetailFields + kControlDelete);
  }
  Invalidate();
}

}  // namespace agenda
