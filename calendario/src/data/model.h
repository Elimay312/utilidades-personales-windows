#pragma once

// The shapes the interface and the storage agree on. Nothing in here knows about Direct2D or
// about SQLite: it is the vocabulary both sides speak.

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "core/dates.h"

namespace agenda {

// 'YYYY-MM-DD', the format the day columns are stored in. It is Google's own wire format for
// an all-day date too, so nothing is converted at either edge. Text and not a day number
// because a cache you cannot read with sqlite3.exe is a cache you cannot debug.
inline std::string DayKey(Date date) {
  return std::format("{:04}-{:02}-{:02}", static_cast<int>(date.year()),
                     static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
}

inline std::optional<Date> ParseDayKey(std::string_view key) {
  if (key.size() != 10 || key[4] != '-' || key[7] != '-') return std::nullopt;
  const auto number = [key](size_t offset, size_t length, int& out) {
    out = 0;
    for (size_t i = 0; i < length; ++i) {
      const char digit = key[offset + i];
      if (digit < '0' || digit > '9') return false;
      out = out * 10 + (digit - '0');
    }
    return true;
  };
  int year = 0;
  int month = 0;
  int day = 0;
  if (!number(0, 4, year) || !number(5, 2, month) || !number(8, 2, day)) return std::nullopt;
  const Date date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                  std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok()) return std::nullopt;
  return date;
}

// What Enter asks the store to create. It is the parser's answer, already decided: which of
// the two kinds it is, and the wall clock it happens at.
struct Draft {
  bool isTask = false;
  std::string calendar;           // where it lands; empty means the default one
  std::wstring title;
  std::wstring recurrence;        // the RRULE as the parser produced it; phase 4 stores it
  std::optional<Date> day;        // empty only for a task with no date at all
  std::optional<int> startMin;    // empty means all day, or a task with no time
  std::optional<Date> endDay;     // events only; defaults to `day`
  std::optional<int> endMin;
};

// One row of the day list. Events and tasks arrive already mixed, because that is how the
// list reads them: whatever is happening that day, in order.
struct DayItem {
  std::wstring uid;               // ours, born before the row exists; the interface's handle
  bool isTask = false;
  std::optional<int> startMin;    // empty = all day (events) or no time (tasks)
  std::optional<int> endMin;
  std::wstring title;
  std::uint32_t color = 0;        // 0xRRGGBB, from the calendar or list it belongs to
  bool done = false;              // tasks only
  bool repeats = false;           // it carries an RRULE
  // The day this card stands for. For a repetition it is which occurrence of the series, the
  // one "solo este" edits; for anything else, the day it starts on.
  Date occurrence{};
};

// The order the day list reads in: all-day events first, then everything with a clock by the
// clock, and tasks with no hour at the end.
//
// The tail is the part that matters and it is not tidiness. The popup shows two cards. With
// undated tasks sorted first, three things on a to-do list would push today's meeting off the
// panel -- and what the day looks like is the reason anyone opened it. A task with no hour is
// not part of the shape of the day, so it waits at the bottom.
//
// It lives here because the store sorts what it reads and the window inserts into that same
// list before the write has happened: two ideas of the order would be one of them wrong.
inline bool EarlierThan(const DayItem& a, const DayItem& b) {
  const auto band = [](const DayItem& item) {
    if (item.startMin) return 1;
    return item.isTask ? 2 : 0;
  };
  if (band(a) != band(b)) return band(a) < band(b);
  if (a.startMin && *a.startMin != *b.startMin) return *a.startMin < *b.startMin;
  if (a.isTask != b.isTask) return !a.isTask;
  return a.title < b.title;
}

// An event whole, as the detail panel edits it and UpdateEvent writes it back. The day list's
// DayItem is what an event looks like on a card; this is what it is.
struct EventDetail {
  std::wstring uid;
  std::string calendarId;
  std::wstring title;
  std::wstring location;
  std::wstring notes;
  std::wstring recurrence;        // the RRULE, with or without its "RRULE:" prefix
  Date startDay{};
  std::optional<int> startMin;    // empty means all day
  Date endDay{};
  std::optional<int> endMin;
  // As events.reminders keeps them: nullopt is "the calendar's" (Google's useDefault), an empty
  // string is none, otherwise minutes before the start, "10,60", with Google's e-mail ones
  // written "m1440" so they go back up untouched when the list is edited here.
  std::optional<std::string> reminders;
};

// The five answers the detail panel offers for the reminder, and Custom for a list that is none
// of them -- "10,60" set on the web -- which the panel shows and keeps.
enum class ReminderChoice { Calendar, None, TenMinutes, OneHour, OneDay, Custom };
inline constexpr int kReminderChoices = 5;

inline ReminderChoice ReminderOf(const std::optional<std::string>& reminders) {
  if (!reminders) return ReminderChoice::Calendar;
  // Only the notification ones count; the e-mail ones are Google's business.
  std::string popups;
  for (size_t at = 0; at <= reminders->size();) {
    size_t comma = reminders->find(',', at);
    if (comma == std::string::npos) comma = reminders->size();
    const std::string_view item = std::string_view(*reminders).substr(at, comma - at);
    if (!item.empty() && item.front() != 'm') {
      if (!popups.empty()) popups += ',';
      popups += item;
    }
    at = comma + 1;
  }
  if (popups.empty()) return ReminderChoice::None;
  if (popups == "10") return ReminderChoice::TenMinutes;
  if (popups == "60") return ReminderChoice::OneHour;
  if (popups == "1440") return ReminderChoice::OneDay;
  return ReminderChoice::Custom;
}

// The list for an answer, keeping whatever e-mail reminders `current` had. "The calendar's" is
// Google's useDefault, which has no list of its own at all.
inline std::optional<std::string> RemindersFor(ReminderChoice choice,
                                               const std::optional<std::string>& current) {
  std::string out;
  switch (choice) {
    case ReminderChoice::Calendar:
    case ReminderChoice::Custom:
      return std::nullopt;
    case ReminderChoice::TenMinutes:
      out = "10";
      break;
    case ReminderChoice::OneHour:
      out = "60";
      break;
    case ReminderChoice::OneDay:
      out = "1440";
      break;
    case ReminderChoice::None:
      break;
  }
  if (current) {
    for (size_t at = 0; at <= current->size();) {
      size_t comma = current->find(',', at);
      if (comma == std::string::npos) comma = current->size();
      const std::string_view item = std::string_view(*current).substr(at, comma - at);
      if (!item.empty() && item.front() == 'm') {
        if (!out.empty()) out += ',';
        out += item;
      }
      at = comma + 1;
    }
  }
  return out;
}

// What an edit touched beyond the times and the title, which always go up. The queue has no
// payload (schema.cpp), so the operation itself says it: "update+location+recurrence".
//
// It matters for two fields in particular. A location Agenda never read is empty here and set
// on Google, so an empty one is only sent when somebody emptied it. And Agenda keeps only the
// RRULE of a repetition, not its EXDATEs, so the rule is only sent when it was edited: sending
// it with every move would bring back the occurrences somebody deleted on the web.
inline constexpr unsigned kEditLocation = 1u;
inline constexpr unsigned kEditRecurrence = 2u;
// The reminders were changed here (phase 11), so this once they go up instead of coming down.
inline constexpr unsigned kEditReminders = 4u;

inline std::string UpdateOp(unsigned edits) {
  std::string op = "update";
  if (edits & kEditLocation) op += "+location";
  if (edits & kEditRecurrence) op += "+recurrence";
  if (edits & kEditReminders) op += "+reminders";
  return op;
}

inline unsigned UpdateEdits(std::string_view op) {
  unsigned edits = 0;
  if (op.find("+location") != std::string_view::npos) edits |= kEditLocation;
  if (op.find("+recurrence") != std::string_view::npos) edits |= kEditRecurrence;
  if (op.find("+reminders") != std::string_view::npos) edits |= kEditReminders;
  return edits;
}

// A reminder that has fallen due: one occurrence of an event, and how long before it somebody
// asked to hear about it. `at` is the WallMinute it is due at.
struct Reminder {
  std::wstring uid;
  std::wstring title;
  std::wstring location;
  Date day{};
  std::optional<int> startMin;  // empty means all day
  std::optional<int> endMin;
  int minutesBefore = 0;
  long long at = 0;
  std::uint32_t color = 0;  // its calendar's, for the dot the island draws
};

// A day that has something on it, and the colour its dot takes.
struct DayDot {
  Date date{};
  std::uint32_t color = 0;
};

// A calendar, as the tray menu and the sidebar have to show it. Not the whole row: a name to
// draw, an id to send back, a tick next to the default, and -- for the sidebar -- its colour and
// whether the user switched it off.
struct CalendarInfo {
  std::string id;
  std::wstring title;
  bool isDefault = false;
  bool isTaskList = false;
  std::uint32_t color = 0;
  bool hidden = false;
  int accountId = 0;  // whose it is; 0 is the local placeholders
};

// A Google account (phase 12). `tokenFile` is the name of the file its refresh token is kept in,
// next to the cache: the first one is token.bin, the ones after it token-<id>.bin.
struct AccountInfo {
  int id = 0;
  std::string email;  // its primary calendar's id; empty until the first pass has read it
  std::string tokenFile;
};

}  // namespace agenda
