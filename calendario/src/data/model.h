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
  bool repeats = false;           // it carries an RRULE; phase 4 does not expand it
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

// A day that has something on it, and the colour its dot takes.
struct DayDot {
  Date date{};
  std::uint32_t color = 0;
};

}  // namespace agenda
