#include "core/recurrence.h"

#include <array>
#include <chrono>
#include <optional>
#include <string_view>

namespace agenda {
namespace {

enum class Freq { None, Daily, Weekly, Monthly, Yearly };

struct Rule {
  Freq freq = Freq::None;
  int interval = 1;
  int count = 0;                 // 0 means no limit
  std::optional<Date> until;     // inclusive
  std::array<bool, 7> byDay{};   // Monday first, like the grid
  bool hasByDay = false;
  bool understood = true;        // false: something in it this file does not read
};

std::optional<int> Number(std::string_view text) {
  if (text.empty() || text.size() > 6) return std::nullopt;
  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
    value = value * 10 + (c - '0');
  }
  return value;
}

int WeekdayIndex(std::string_view code) {
  constexpr std::string_view kCodes[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
  for (int i = 0; i < 7; ++i) {
    if (code == kCodes[i]) return i;
  }
  return -1;
}

// UNTIL is 'YYYYMMDD' or 'YYYYMMDDTHHMMSSZ'. Only the day is kept: the cache speaks in days, and
// an UNTIL of the last evening in UTC is the same last day in every zone this app runs in.
std::optional<Date> ReadUntil(std::string_view text) {
  if (text.size() < 8) return std::nullopt;
  const std::optional<int> year = Number(text.substr(0, 4));
  const std::optional<int> month = Number(text.substr(4, 2));
  const std::optional<int> day = Number(text.substr(6, 2));
  if (!year || !month || !day) return std::nullopt;
  const Date date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
                  std::chrono::day{static_cast<unsigned>(*day)}};
  if (!date.ok()) return std::nullopt;
  return date;
}

Rule Parse(std::string_view text) {
  Rule rule;
  if (text.starts_with("RRULE:")) text.remove_prefix(6);

  while (!text.empty()) {
    const size_t semicolon = text.find(';');
    const std::string_view part = text.substr(0, semicolon);
    text = semicolon == std::string_view::npos ? std::string_view{} : text.substr(semicolon + 1);
    if (part.empty()) continue;

    const size_t equals = part.find('=');
    if (equals == std::string_view::npos) {
      rule.understood = false;
      continue;
    }
    const std::string_view key = part.substr(0, equals);
    const std::string_view value = part.substr(equals + 1);

    if (key == "FREQ") {
      if (value == "DAILY") rule.freq = Freq::Daily;
      else if (value == "WEEKLY") rule.freq = Freq::Weekly;
      else if (value == "MONTHLY") rule.freq = Freq::Monthly;
      else if (value == "YEARLY") rule.freq = Freq::Yearly;
      else rule.understood = false;
    } else if (key == "INTERVAL") {
      const std::optional<int> n = Number(value);
      if (!n || *n < 1) rule.understood = false;
      else rule.interval = *n;
    } else if (key == "COUNT") {
      const std::optional<int> n = Number(value);
      if (!n || *n < 1) rule.understood = false;
      else rule.count = *n;
    } else if (key == "UNTIL") {
      rule.until = ReadUntil(value);
      if (!rule.until) rule.understood = false;
    } else if (key == "BYDAY") {
      rule.hasByDay = true;
      std::string_view days = value;
      while (!days.empty()) {
        const size_t comma = days.find(',');
        const int index = WeekdayIndex(days.substr(0, comma));
        // "1MO", "-1FR": the nth weekday of a month. Not read, so not guessed at.
        if (index < 0) rule.understood = false;
        else rule.byDay[static_cast<size_t>(index)] = true;
        days = comma == std::string_view::npos ? std::string_view{} : days.substr(comma + 1);
      }
    } else if (key == "WKST") {
      // Monday is where this app starts its weeks; any other start only matters to a weekly
      // rule with an interval, and none of those have ever been seen coming from Google.
    } else {
      rule.understood = false;
    }
  }
  if (rule.freq == Freq::None) rule.understood = false;
  return rule;
}

int DaysBetween(Date from, Date to) {
  return static_cast<int>(
      (std::chrono::sys_days{to} - std::chrono::sys_days{from}).count());
}

int Weekday(Date date) { return MondayIndex(std::chrono::weekday{std::chrono::sys_days{date}}); }

// Does the pattern land on this day, before COUNT and UNTIL have their say?
bool Lands(const Rule& rule, Date start, Date day) {
  const int days = DaysBetween(start, day);
  switch (rule.freq) {
    case Freq::Daily:
      if (days % rule.interval != 0) return false;
      return !rule.hasByDay || rule.byDay[static_cast<size_t>(Weekday(day))];
    case Freq::Weekly: {
      // Weeks counted Monday to Monday, so "every two weeks on Monday and Thursday" keeps both
      // days in the same week even when the series started on the Thursday.
      const Date startMonday = AddDays(start, -Weekday(start));
      const int weeks = DaysBetween(startMonday, day) / 7;
      if (weeks % rule.interval != 0) return false;
      if (rule.hasByDay) return rule.byDay[static_cast<size_t>(Weekday(day))];
      return Weekday(day) == Weekday(start);
    }
    case Freq::Monthly: {
      const int months =
          (static_cast<int>(day.year()) - static_cast<int>(start.year())) * 12 +
          static_cast<int>(static_cast<unsigned>(day.month())) -
          static_cast<int>(static_cast<unsigned>(start.month()));
      // The 31st of a month that has no 31st is skipped, which is what RFC 5545 and Google do.
      return months % rule.interval == 0 && day.day() == start.day();
    }
    case Freq::Yearly: {
      const int years = static_cast<int>(day.year()) - static_cast<int>(start.year());
      return years % rule.interval == 0 && day.month() == start.month() &&
             day.day() == start.day();
    }
    case Freq::None:
      break;
  }
  return false;
}

}  // namespace

bool OccursOn(std::string_view text, Date start, Date day) {
  if (day == start) return true;
  if (day < start) return false;

  const Rule rule = Parse(text);
  if (!rule.understood) return false;
  if (rule.until && day > *rule.until) return false;
  if (!Lands(rule, start, day)) return false;
  if (rule.count == 0) return true;

  // COUNT has to be counted. Walked from the start and stopped as soon as it runs out, so a
  // rule of ten is ten steps' worth of matches, not the whole distance to `day`.
  // ponytail: a day at a time; a closed form per frequency if a long COUNT ever shows up slow.
  int seen = 0;
  for (Date walk = start; walk < day; walk = AddDays(walk, 1)) {
    if (walk == start || Lands(rule, start, walk)) {
      if (++seen >= rule.count) return false;
    }
  }
  return true;
}

}  // namespace agenda
