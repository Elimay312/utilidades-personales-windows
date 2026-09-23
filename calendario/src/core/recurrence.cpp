#include "core/recurrence.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

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
  // "1TU", "-1FR": the nth weekday of the month, counted from the end when negative. Kept as
  // {n, weekday}; the plain weekdays of the same BYDAY go to byDay.
  std::vector<std::pair<int, int>> byNthDay;
  std::vector<int> byMonthDay;   // 15, or -1 for the last day of the month
  std::array<bool, 12> byMonth{};
  bool hasByMonth = false;
  bool understood = true;        // false: something in it this file does not read
  std::vector<Date> except;      // EXDATE: days the series skips
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

// "-1" and "+2" as well as "15": BYDAY ordinals and BYMONTHDAY count from the end when negative.
std::optional<int> SignedNumber(std::string_view text) {
  const bool negative = text.starts_with('-');
  if (negative || text.starts_with('+')) text.remove_prefix(1);
  const std::optional<int> value = Number(text);
  if (!value) return std::nullopt;
  return negative ? -*value : *value;
}

// Each item of a comma-separated value.
template <typename Each>
void ForEachItem(std::string_view list, Each each) {
  while (!list.empty()) {
    const size_t comma = list.find(',');
    each(list.substr(0, comma));
    list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
  }
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

// One RRULE, without its "RRULE:".
void ParseRule(std::string_view text, Rule& rule) {
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
      ForEachItem(value, [&](std::string_view item) {
        const int index = item.size() >= 2 ? WeekdayIndex(item.substr(item.size() - 2)) : -1;
        if (index < 0) {
          rule.understood = false;
        } else if (item.size() == 2) {
          rule.hasByDay = true;
          rule.byDay[static_cast<size_t>(index)] = true;
        } else if (const std::optional<int> n = SignedNumber(item.substr(0, item.size() - 2));
                   n && *n != 0 && *n >= -5 && *n <= 5) {
          rule.byNthDay.emplace_back(*n, index);
        } else {
          rule.understood = false;
        }
      });
    } else if (key == "BYMONTHDAY") {
      ForEachItem(value, [&](std::string_view item) {
        const std::optional<int> n = SignedNumber(item);
        if (n && *n != 0 && *n >= -31 && *n <= 31) rule.byMonthDay.push_back(*n);
        else rule.understood = false;
      });
    } else if (key == "BYMONTH") {
      ForEachItem(value, [&](std::string_view item) {
        const std::optional<int> n = Number(item);
        if (n && *n >= 1 && *n <= 12) {
          rule.hasByMonth = true;
          rule.byMonth[static_cast<size_t>(*n - 1)] = true;
        } else {
          rule.understood = false;
        }
      });
    } else if (key == "WKST") {
      // Monday is where this app starts its weeks; any other start only matters to a weekly
      // rule with an interval, and none of those have ever been seen coming from Google.
    } else {
      rule.understood = false;
    }
  }
}

// The next line of a multi-line recurrence, the way the cache keeps Google's list: one per line.
std::string_view NextLine(std::string_view& text) {
  const size_t newline = text.find('\n');
  std::string_view line = text.substr(0, newline);
  text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
  if (line.ends_with('\r')) line.remove_suffix(1);
  return line;
}

// "EXDATE;TZID=America/Bogota:20261001T090000,20261008T090000" or "EXDATE:20261001". Only the
// day of each is kept, like UNTIL.
// ponytail: a UTC value ("...T020000Z") is read as its UTC day, which for an evening event west
// of London is the day after; Google writes EXDATE with a TZID, so this is only what imports do.
void ParseExdate(std::string_view line, Rule& rule) {
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos) return;
  std::string_view days = line.substr(colon + 1);
  while (!days.empty()) {
    const size_t comma = days.find(',');
    if (const std::optional<Date> day = ReadUntil(days.substr(0, comma))) {
      rule.except.push_back(*day);
    }
    days = comma == std::string_view::npos ? std::string_view{} : days.substr(comma + 1);
  }
}

// The whole recurrence: the RRULE, bare as the parser writes it or with its prefix as Google
// does, and the EXDATE lines next to it. RDATE adds days this file does not add, and is left out.
Rule Parse(std::string_view text) {
  Rule rule;
  bool sawRule = false;
  while (!text.empty()) {
    std::string_view line = NextLine(text);
    if (line.starts_with("EXDATE")) {
      ParseExdate(line, rule);
    } else if (line.starts_with("RRULE:") || line.starts_with("FREQ=")) {
      if (line.starts_with("RRULE:")) line.remove_prefix(6);
      if (!sawRule) ParseRule(line, rule);
      sawRule = true;
    }
  }
  if (rule.freq == Freq::None) rule.understood = false;
  // The nth weekday and the day of the month only mean something inside a month. RFC 5545 does
  // not allow them in a weekly rule, and a yearly one without BYMONTH counts them across the
  // whole year ("the 20th Monday"), which nobody writes and this file does not read.
  const bool dayOfMonth = !rule.byNthDay.empty() || !rule.byMonthDay.empty();
  if (dayOfMonth && (rule.freq == Freq::Daily || rule.freq == Freq::Weekly)) {
    rule.understood = false;
  }
  if ((dayOfMonth || rule.hasByDay) && rule.freq == Freq::Yearly && !rule.hasByMonth) {
    rule.understood = false;
  }
  return rule;
}

int DaysBetween(Date from, Date to) {
  return static_cast<int>(
      (std::chrono::sys_days{to} - std::chrono::sys_days{from}).count());
}

int Weekday(Date date) { return MondayIndex(std::chrono::weekday{std::chrono::sys_days{date}}); }

// Whether `day` is one of the days a monthly rule picks inside its month -- or a yearly one,
// inside the months it repeats in. BYMONTHDAY and BYDAY when the rule has them, and both at once
// is where they meet: "Friday the 13th". With neither, the start's own day of the month.
bool PicksDay(const Rule& rule, Date start, Date day) {
  const bool byWeekday = rule.hasByDay || !rule.byNthDay.empty();
  if (rule.byMonthDay.empty() && !byWeekday) return day.day() == start.day();

  const int dom = static_cast<int>(static_cast<unsigned>(day.day()));
  const int last = static_cast<int>(static_cast<unsigned>(
      std::chrono::year_month_day_last{day.year(), std::chrono::month_day_last{day.month()}}
          .day()));
  if (!rule.byMonthDay.empty() &&
      std::none_of(rule.byMonthDay.begin(), rule.byMonthDay.end(),
                   [&](int n) { return n > 0 ? dom == n : dom == last + n + 1; })) {
    return false;
  }
  if (!byWeekday) return true;

  const int weekday = Weekday(day);
  if (rule.hasByDay && rule.byDay[static_cast<size_t>(weekday)]) return true;
  // The first Tuesday is the Tuesday in days 1-7, the last one the Tuesday in the last seven.
  return std::any_of(rule.byNthDay.begin(), rule.byNthDay.end(), [&](const auto& nth) {
    const auto [n, on] = nth;
    return on == weekday && (n > 0 ? (dom - 1) / 7 + 1 == n : (last - dom) / 7 + 1 == -n);
  });
}

// Does the pattern land on this day, before COUNT and UNTIL have their say?
bool Lands(const Rule& rule, Date start, Date day) {
  const int days = DaysBetween(start, day);
  const auto month = static_cast<size_t>(static_cast<unsigned>(day.month()) - 1);
  if (rule.hasByMonth && !rule.byMonth[month]) return false;
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
      return months % rule.interval == 0 && PicksDay(rule, start, day);
    }
    case Freq::Yearly: {
      const int years = static_cast<int>(day.year()) - static_cast<int>(start.year());
      // BYMONTH, when there is one, was already asked above.
      return years % rule.interval == 0 && (rule.hasByMonth || day.month() == start.month()) &&
             PicksDay(rule, start, day);
    }
    case Freq::None:
      break;
  }
  return false;
}

constexpr std::wstring_view kDayCodes[7] = {L"MO", L"TU", L"WE", L"TH", L"FR", L"SA", L"SU"};

// Where the RRULE line sits inside a recurrence that may carry EXDATE lines as well: its offset
// and length, prefix included. The whole text when there is only the rule.
std::pair<size_t, size_t> RuleLine(std::wstring_view text) {
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find(L'\n', at);
    if (end == std::wstring_view::npos) end = text.size();
    const std::wstring_view line = text.substr(at, end - at);
    if (line.starts_with(L"RRULE:") || line.starts_with(L"FREQ=")) {
      size_t length = line.size();
      if (line.ends_with(L'\r')) --length;
      return {at, length};
    }
    at = end + 1;
  }
  return {0, 0};
}

std::wstring_view Bare(std::wstring_view text) {
  const auto [at, length] = RuleLine(text);
  std::wstring_view rule = text.substr(at, length);
  if (rule.starts_with(L"RRULE:")) rule.remove_prefix(6);
  return rule;
}

}  // namespace

Repeat RepeatOf(std::wstring_view text) {
  const std::wstring_view rule = Bare(text);
  if (rule.empty()) return Repeat::None;
  if (rule == L"FREQ=DAILY") return Repeat::Daily;
  if (rule == L"FREQ=MONTHLY") return Repeat::Monthly;
  if (rule == L"FREQ=YEARLY") return Repeat::Yearly;
  if (rule == L"FREQ=WEEKLY") return Repeat::Weekly;
  if (rule.starts_with(L"FREQ=WEEKLY;BYDAY=") && rule.size() == 20) return Repeat::Weekly;
  return Repeat::Custom;
}

std::wstring RuleFor(Repeat repeat, Date start) {
  switch (repeat) {
    case Repeat::Daily:
      return L"RRULE:FREQ=DAILY";
    case Repeat::Weekly:
      return L"RRULE:FREQ=WEEKLY;BYDAY=" +
             std::wstring(kDayCodes[Weekday(start)]);
    case Repeat::Monthly:
      return L"RRULE:FREQ=MONTHLY";
    case Repeat::Yearly:
      return L"RRULE:FREQ=YEARLY";
    case Repeat::None:
    case Repeat::Custom:
      break;
  }
  return {};
}

std::wstring MoveRuleTo(std::wstring_view rule, Date start) {
  const std::wstring_view bare = Bare(rule);
  constexpr std::wstring_view kWeeklyOn = L"FREQ=WEEKLY;BYDAY=";
  // Exactly one day after BYDAY and nothing behind it: "FREQ=WEEKLY;BYDAY=MO".
  if (!bare.starts_with(kWeeklyOn) || bare.size() != kWeeklyOn.size() + 2) {
    return std::wstring(rule);
  }
  // The day code is the last two characters of the RRULE line; any EXDATE line stays as it was.
  const auto [at, length] = RuleLine(rule);
  std::wstring out(rule);
  out.replace(at + length - 2, 2, kDayCodes[Weekday(start)]);
  return out;
}

bool OccursOn(std::string_view text, Date start, Date day) {
  if (day < start) return false;
  const Rule rule = Parse(text);
  // An EXDATE takes the day away whatever else is true, the first day included. It does not give
  // the occurrence back to COUNT: RFC 5545 counts before it excludes, and so does Google.
  if (std::find(rule.except.begin(), rule.except.end(), day) != rule.except.end()) return false;
  if (day == start) return true;
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
