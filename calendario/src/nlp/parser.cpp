#include "nlp/parser.h"

#include <algorithm>
#include <format>
#include <initializer_list>

#include "core/text.h"
#include "core/zones.h"

namespace agenda::nlp {
namespace {

using std::chrono::sys_days;

constexpr int kMinutesPerDay = 24 * 60;

struct Token {
  size_t offset = 0;
  size_t length = 0;
  std::wstring_view text;  // into the folded copy
};

bool IsWordChar(wchar_t c) {
  // The colon rides along so "17:00" and "t:" each stay one token.
  return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L':';
}

std::vector<Token> Tokenize(const std::wstring& folded) {
  std::vector<Token> out;
  const std::wstring_view all{folded};
  size_t i = 0;
  while (i < all.size()) {
    if (!IsWordChar(all[i])) {
      ++i;
      continue;
    }
    const size_t start = i;
    while (i < all.size() && IsWordChar(all[i])) ++i;
    out.push_back(Token{start, i - start, all.substr(start, i - start)});
  }
  return out;
}

bool AllDigits(std::wstring_view s) {
  if (s.empty() || s.size() > 4) return false;
  for (wchar_t c : s) {
    if (c < L'0' || c > L'9') return false;
  }
  return true;
}

int Number(std::wstring_view s) {
  int value = 0;
  for (wchar_t c : s) value = value * 10 + (c - L'0');
  return value;
}

bool At(const std::vector<Token>& t, size_t i, std::wstring_view word) {
  return i < t.size() && t[i].text == word;
}

bool AtAny(const std::vector<Token>& t, size_t i, std::initializer_list<const wchar_t*> words) {
  if (i >= t.size()) return false;
  for (const wchar_t* word : words) {
    if (t[i].text == word) return true;
  }
  return false;
}

// Monday first, to match MondayIndex and the week the grid draws.
constexpr std::wstring_view kWeekdayEs[7] = {L"lunes",   L"martes", L"miercoles", L"jueves",
                                             L"viernes", L"sabado", L"domingo"};
constexpr std::wstring_view kWeekdayEn[7] = {L"monday", L"tuesday",  L"wednesday", L"thursday",
                                             L"friday", L"saturday", L"sunday"};
constexpr std::wstring_view kByDay[7] = {L"MO", L"TU", L"WE", L"TH", L"FR", L"SA", L"SU"};

int WeekdayOf(std::wstring_view word) {
  for (int i = 0; i < 7; ++i) {
    if (word == kWeekdayEs[i] || word == kWeekdayEn[i]) return i;
  }
  return -1;
}

constexpr std::wstring_view kMonthEs[12] = {L"enero",   L"febrero",    L"marzo",   L"abril",
                                             L"mayo",    L"junio",      L"julio",   L"agosto",
                                             L"septiembre", L"octubre", L"noviembre", L"diciembre"};
constexpr std::wstring_view kMonthEn[12] = {L"january", L"february", L"march",     L"april",
                                             L"may",     L"june",     L"july",      L"august",
                                             L"september", L"october", L"november", L"december"};

// 1 to 12, or 0. A month is its name in either language, "setiembre", or any beginning of the
// name at least three letters long: "oct", "sept", "dic". Only ever asked right next to a day
// number, so "mar" and "may" being words as well does not matter.
int MonthOf(std::wstring_view word) {
  if (word == L"setiembre") return 9;
  if (word.size() < 3) return 0;
  for (int i = 0; i < 12; ++i) {
    if (kMonthEs[i].starts_with(word) || kMonthEn[i].starts_with(word)) return i + 1;
  }
  return 0;
}

// What sits between two tokens in the text itself: the "/" of "25/10", the "-" of "3-5pm".
std::wstring_view Between(std::wstring_view folded, const Token& a, const Token& b) {
  std::wstring_view gap = folded.substr(a.offset + a.length, b.offset - (a.offset + a.length));
  while (!gap.empty() && gap.front() == L' ') gap.remove_prefix(1);
  while (!gap.empty() && gap.back() == L' ') gap.remove_suffix(1);
  return gap;
}

bool IsDateWord(std::wstring_view word) {
  return WeekdayOf(word) >= 0 || word == L"hoy" || word == L"today" || word == L"manana" ||
         word == L"tomorrow" || word == L"pasado";
}

bool Before(Date a, Date b) { return sys_days{a} < sys_days{b}; }

Date NextWeekday(Date from, int weekday, bool strictlyAfter) {
  const int current = MondayIndex(std::chrono::weekday{sys_days{from}});
  int delta = (weekday - current + 7) % 7;
  if (delta == 0 && strictlyAfter) delta = 7;
  return AddDays(from, delta);
}

// "el 25" means the next 25th there is: this month if it has not gone by, otherwise the first
// month ahead that actually has that day, so a 31st skips February without any special case.
Date DayOfMonth(Date today, int day) {
  Month month{today.year(), today.month()};
  for (int i = 0; i < 14; ++i) {
    const Date candidate{month.year(), month.month(),
                         std::chrono::day{static_cast<unsigned>(day)}};
    if (candidate.ok() && !Before(candidate, today)) return candidate;
    month += std::chrono::months{1};
  }
  return today;
}

// A day and a month, and the year if it was written. Without one it is the next time that date
// comes round, so "25 de octubre" said in November is next year's.
std::optional<Date> DayAndMonth(Date today, int day, int month, std::optional<int> year) {
  const auto make = [&](int y) {
    return Date{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(month)},
                std::chrono::day{static_cast<unsigned>(day)}};
  };
  if (year) {
    const Date date = make(*year);
    return date.ok() ? std::optional<Date>(date) : std::nullopt;
  }
  // A 29 February waits for the next year that has one.
  for (int y = static_cast<int>(today.year()); y < static_cast<int>(today.year()) + 8; ++y) {
    const Date date = make(y);
    if (date.ok() && !Before(date, today)) return date;
  }
  return std::nullopt;
}

// A month later lands on the same day, or on the last one when that month is shorter.
Date AddMonths(Date date, int months) {
  const std::chrono::year_month month =
      std::chrono::year_month{date.year(), date.month()} + std::chrono::months{months};
  const Date same{month.year(), month.month(), date.day()};
  if (same.ok()) return same;
  return Date{std::chrono::year_month_day_last{month.year(),
                                               std::chrono::month_day_last{month.month()}}};
}

struct Clock {
  int minute = 0;
  bool exact = false;  // am/pm, HH:MM or 17h: the eight-to-twenty rule does not apply
  bool bare = false;   // a naked number, which needs a marker or a date beside it
};

std::optional<Clock> ReadClock(std::wstring_view word) {
  if (word == L"mediodia" || word == L"noon") return Clock{12 * 60, true, false};
  if (word == L"medianoche" || word == L"midnight") return Clock{0, true, false};

  const size_t colon = word.find(L':');
  if (colon != std::wstring_view::npos) {
    const std::wstring_view hours = word.substr(0, colon);
    const std::wstring_view minutes = word.substr(colon + 1);
    if (!AllDigits(hours) || !AllDigits(minutes) || minutes.size() != 2) return std::nullopt;
    if (Number(hours) > 23 || Number(minutes) > 59) return std::nullopt;
    // "4:05" is how the time is written here in the afternoon too, so it goes by the same
    // eight-to-twenty rule as "a las 4". "04:05", "16:05" and "12:30" say which one they mean.
    const bool twelve = hours.size() == 1 || (hours[0] != L'0' && Number(hours) < 12);
    return Clock{Number(hours) * 60 + Number(minutes), !twelve || Number(hours) == 0, false};
  }

  if (word.size() > 2 && (word.ends_with(L"am") || word.ends_with(L"pm"))) {
    const std::wstring_view hours = word.substr(0, word.size() - 2);
    if (!AllDigits(hours)) return std::nullopt;
    const int hour = Number(hours);
    if (hour < 1 || hour > 12) return std::nullopt;
    return Clock{((hour % 12) + (word.ends_with(L"pm") ? 12 : 0)) * 60, true, false};
  }

  if (word.size() > 1 && word.back() == L'h') {
    const std::wstring_view hours = word.substr(0, word.size() - 1);
    if (!AllDigits(hours) || Number(hours) > 23) return std::nullopt;
    return Clock{Number(hours) * 60, true, false};
  }

  // "17h30", the way it is written on a poster.
  if (const size_t h = word.find(L'h'); h != std::wstring_view::npos && h > 0) {
    const std::wstring_view hours = word.substr(0, h);
    const std::wstring_view minutes = word.substr(h + 1);
    if (!AllDigits(hours) || !AllDigits(minutes) || minutes.size() != 2) return std::nullopt;
    if (Number(hours) > 23 || Number(minutes) > 59) return std::nullopt;
    return Clock{Number(hours) * 60 + Number(minutes), true, false};
  }

  if (AllDigits(word)) {
    const int hour = Number(word);
    if (hour > 23) return std::nullopt;
    return Clock{hour * 60, false, true};
  }
  return std::nullopt;
}

// CLAUDE.md: "a las 3" with no am or pm lands between 8:00 and 20:00.
int Daylight(int minute) { return minute < 8 * 60 ? minute + 12 * 60 : minute; }

struct TimeHit {
  int minute = kNoTime;
  size_t from = 0;  // token indices, half open
  size_t to = 0;
  bool twelve = false;  // the other half of the day would read just as well
};

bool RangeFree(const std::vector<bool>& used, size_t from, size_t to) {
  for (size_t k = from; k < to; ++k) {
    if (k >= used.size() || used[k]) return false;
  }
  return true;
}

std::optional<TimeHit> ReadTimeAt(const std::vector<Token>& t, const std::vector<bool>& used,
                                  size_t i) {
  size_t j = i;
  bool marked = false;
  if (At(t, j, L"a") && (At(t, j + 1, L"las") || At(t, j + 1, L"la"))) {
    marked = true;
    j += 2;
  } else if (At(t, j, L"at")) {
    marked = true;
    j += 1;
  }
  if (j >= t.size() || used[j]) return std::nullopt;

  std::optional<Clock> clock = ReadClock(t[j].text);
  if (!clock) return std::nullopt;

  size_t end = j + 1;
  int minute = clock->minute;
  bool bare = clock->bare;

  if (!clock->exact && AtAny(t, end, {L"am", L"pm"}) && RangeFree(used, end, end + 1)) {
    const int hour = minute / 60;
    if (hour < 1 || hour > 12) return std::nullopt;
    minute = ((hour % 12) + (t[end].text == L"pm" ? 12 : 0)) * 60 + minute % 60;
    clock->exact = true;
    bare = false;
    ++end;
  } else if (!clock->exact && At(t, end, L"de") && At(t, end + 1, L"la") &&
             AtAny(t, end + 2, {L"tarde", L"noche", L"manana", L"madrugada"}) &&
             RangeFree(used, end, end + 3)) {
    const int hour = minute / 60;
    if (hour > 12) return std::nullopt;
    const bool afternoon = t[end + 2].text == L"tarde" || t[end + 2].text == L"noche";
    minute = ((hour % 12) + (afternoon ? 12 : 0)) * 60 + minute % 60;
    clock->exact = true;
    bare = false;
    end += 3;
  }

  // Nothing said morning or afternoon: 1 to 11 could be either, and ParseInput picks.
  const bool twelve = !clock->exact && minute / 60 >= 1 && minute / 60 <= 11;
  if (!clock->exact) minute = Daylight(minute);

  // A naked number only counts as a time when something says so: a marker like "a las", or a
  // date right before it as in "pasado manana 9". Without this, "el 25" and "comprar 2 panes"
  // would both turn into times.
  if (bare && !marked && !(i > 0 && IsDateWord(t[i - 1].text))) return std::nullopt;

  return TimeHit{minute, i, end, twelve};
}

std::optional<TimeHit> FindTime(const std::vector<Token>& t, const std::vector<bool>& used) {
  for (size_t i = 0; i < t.size(); ++i) {
    if (used[i]) continue;
    const std::optional<TimeHit> hit = ReadTimeAt(t, used, i);
    if (hit && RangeFree(used, hit->from, hit->to)) return hit;
  }
  return std::nullopt;
}

struct DurationHit {
  int minutes = 0;
  std::optional<int> startMinute;  // "de 3 a 5" pins the start as well as the length
  size_t from = 0;
  size_t to = 0;
};

// "pm", "am" or "de la tarde" right after a clock: which half of the day it said, and how many
// tokens that took. Nothing said is nullopt.
std::optional<bool> ReadHalf(const std::vector<Token>& t, size_t& at) {
  if (AtAny(t, at, {L"am", L"pm"})) {
    return t[at++].text == L"pm";
  }
  if (At(t, at, L"de") && At(t, at + 1, L"la") &&
      AtAny(t, at + 2, {L"tarde", L"noche", L"manana", L"madrugada"})) {
    const bool afternoon = t[at + 2].text == L"tarde" || t[at + 2].text == L"noche";
    at += 3;
    return afternoon;
  }
  return std::nullopt;
}

// A clock in the half of the day that was said. Nullopt when that makes no sense ("15 pm").
std::optional<int> InHalf(int minute, bool pm) {
  const int hour = minute / 60;
  if (hour < 1 || hour > 12) return std::nullopt;
  return ((hour % 12) + (pm ? 12 : 0)) * 60 + minute % 60;
}

// A time with its end: "de 3 a 5", "from 3 to 5", "3-5pm", "a las 3 hasta las 5", "entre las 3
// y las 5", "10:00-11:30", "de 3 a 5 de la tarde". One pattern that produces a start and a length
// at once, and matches whole or not at all, so a half match never eats the "de" of "de la
// tarde".
//
// A half of the day said at one end goes for the other as well -- "3-5pm" is three to five in
// the afternoon -- unless that would put the start after the end: "11-1pm" starts at eleven in
// the morning. Two naked numbers with nothing in front ("comprar 3 a 5 manzanas") are not a
// time at all.
std::optional<DurationHit> ReadRange(const std::vector<Token>& t, const std::vector<bool>& used,
                                     std::wstring_view folded, size_t i) {
  size_t j = i;
  bool marked = false;
  if (AtAny(t, j, {L"de", L"desde", L"from", L"entre", L"between"})) {
    marked = true;
    ++j;
    if (AtAny(t, j, {L"las", L"la"})) ++j;
  } else if (At(t, j, L"a") && AtAny(t, j + 1, {L"las", L"la"})) {
    marked = true;
    j += 2;
  } else if (At(t, j, L"at")) {
    marked = true;
    ++j;
  }
  if (j >= t.size()) return std::nullopt;
  const std::optional<Clock> first = ReadClock(t[j].text);
  if (!first) return std::nullopt;
  size_t k = j + 1;
  const std::optional<bool> firstHalf = first->exact ? std::nullopt : ReadHalf(t, k);

  if (AtAny(t, k, {L"a", L"to", L"hasta", L"until", L"till", L"y", L"and"})) {
    ++k;
    if (AtAny(t, k, {L"las", L"la"})) ++k;
  } else if (k >= t.size() || (Between(folded, t[k - 1], t[k]) != L"-" &&
                               Between(folded, t[k - 1], t[k]) != L"–")) {
    return std::nullopt;
  }
  if (k >= t.size()) return std::nullopt;
  const std::optional<Clock> second = ReadClock(t[k].text);
  if (!second) return std::nullopt;
  ++k;
  const std::optional<bool> secondHalf = second->exact ? std::nullopt : ReadHalf(t, k);

  const bool saidNothing = first->bare && second->bare && !firstHalf && !secondHalf;
  if (!marked && saidNothing) return std::nullopt;

  const std::optional<bool> firstSays = firstHalf ? firstHalf : secondHalf;
  const std::optional<bool> secondSays = secondHalf ? secondHalf : firstHalf;
  int start = first->minute;
  bool startGuessed = false;
  if (!first->exact) {
    if (firstSays) {
      const std::optional<int> in = InHalf(first->minute, *firstSays);
      if (!in) return std::nullopt;
      start = *in;
    } else {
      start = Daylight(first->minute);
      startGuessed = true;
    }
  }
  int end = second->minute;
  if (!second->exact) {
    if (secondSays) {
      const std::optional<int> in = InHalf(second->minute, *secondSays);
      if (!in) return std::nullopt;
      end = *in;
    } else {
      end = Daylight(second->minute);
    }
  }
  // "11-1pm": the half that was borrowed does not fit the start, the other one does.
  if (end <= start && !firstHalf && secondHalf && start >= 12 * 60) start -= 12 * 60;
  // "de 7 a 9": the guess moved seven to the evening past nine; the morning reads it whole.
  if (end <= start && startGuessed && first->minute < end) start = first->minute;
  if (end <= start || !RangeFree(used, i, k)) return std::nullopt;
  return DurationHit{end - start, start, i, k};
}

// "un", "una", "a" and "an" count as one: "en una semana", "in a week", "por una hora".
std::optional<int> CountOf(std::wstring_view word) {
  if (AllDigits(word)) return Number(word);
  if (word == L"un" || word == L"una" || word == L"a" || word == L"an") return 1;
  return std::nullopt;
}

bool IsHourWord(const std::vector<Token>& t, size_t i) {
  return AtAny(t, i, {L"h", L"hora", L"horas", L"hour", L"hours"});
}

// "por 2h", "for 30 min", "durante 90 minutos", "por 1h30", "por 1.5h", "por media hora",
// "por una hora y media", and a bare "30 min". A bare "2h" is not a length: "17h reunion" is a
// time, and seventeen hours long would be absurd.
std::optional<DurationHit> ReadLength(const std::vector<Token>& t, const std::vector<bool>& used,
                                      std::wstring_view folded, size_t i) {
  size_t j = i;
  const bool keyword = AtAny(t, j, {L"por", L"for", L"durante"});
  if (keyword) ++j;
  if (j >= t.size()) return std::nullopt;

  const std::wstring_view word = t[j].text;
  const size_t h = word.find(L'h');
  int minutes = 0;
  size_t end = 0;
  if (keyword && word.size() > 1 && word.back() == L'h' &&
      AllDigits(word.substr(0, word.size() - 1))) {
    minutes = Number(word.substr(0, word.size() - 1)) * 60;
    end = j + 1;
  } else if (keyword && h != std::wstring_view::npos && h > 0 &&
             AllDigits(word.substr(0, h)) && AllDigits(word.substr(h + 1))) {
    // "1h30": an hour and thirty minutes.
    minutes = Number(word.substr(0, h)) * 60 + Number(word.substr(h + 1));
    end = j + 1;
  } else if (keyword && AllDigits(word) && j + 1 < t.size() &&
             (Between(folded, t[j], t[j + 1]) == L"." || Between(folded, t[j], t[j + 1]) == L",")) {
    // "1.5h" and "1,5 horas": the decimal point splits the token.
    const std::wstring_view next = t[j + 1].text;
    const bool glued = next.size() > 1 && next.back() == L'h';
    const std::wstring_view fraction = glued ? next.substr(0, next.size() - 1) : next;
    if (!AllDigits(fraction) || fraction.size() > 2 || (!glued && !IsHourWord(t, j + 2))) {
      return std::nullopt;
    }
    const int scale = fraction.size() == 1 ? 10 : 100;
    minutes = Number(word) * 60 + Number(fraction) * 60 / scale;
    end = glued ? j + 2 : j + 3;
  } else if (keyword && At(t, j, L"media") && AtAny(t, j + 1, {L"hora"})) {
    minutes = 30;
    end = j + 2;
  } else if (AllDigits(word) && AtAny(t, j + 1, {L"min", L"mins", L"minutos", L"minutes"})) {
    minutes = Number(word);
    end = j + 2;
  } else if (const std::optional<int> count = CountOf(word);
             keyword && count && IsHourWord(t, j + 1)) {
    minutes = *count * 60;
    end = j + 2;
    // "una hora y media", "an hour and a half".
    if (At(t, end, L"y") && At(t, end + 1, L"media")) {
      minutes += 30;
      end += 2;
    } else if (At(t, end, L"and") && At(t, end + 1, L"a") && At(t, end + 2, L"half")) {
      minutes += 30;
      end += 3;
    }
  } else {
    return std::nullopt;
  }

  if (minutes <= 0 || minutes > kMinutesPerDay || !RangeFree(used, i, end)) return std::nullopt;
  return DurationHit{minutes, std::nullopt, i, end};
}

struct RecurrenceHit {
  std::wstring rule;
  std::optional<int> weekday;
  size_t from = 0;
  size_t to = 0;
};

std::optional<RecurrenceHit> FindRecurrence(const std::vector<Token>& t) {
  for (size_t i = 0; i < t.size(); ++i) {
    if (AtAny(t, i, {L"cada", L"every"})) {
      const int weekday = i + 1 < t.size() ? WeekdayOf(t[i + 1].text) : -1;
      if (weekday >= 0) {
        return RecurrenceHit{std::format(L"FREQ=WEEKLY;BYDAY={}", kByDay[weekday]), weekday, i,
                             i + 2};
      }
      if (AtAny(t, i + 1, {L"dia", L"dias", L"day", L"days"})) {
        return RecurrenceHit{L"FREQ=DAILY", std::nullopt, i, i + 2};
      }
      if (AtAny(t, i + 1, {L"semana", L"week"})) {
        return RecurrenceHit{L"FREQ=WEEKLY", std::nullopt, i, i + 2};
      }
    }
    if (AtAny(t, i, {L"todos", L"todas"}) && AtAny(t, i + 1, {L"los", L"las"}) &&
        AtAny(t, i + 2, {L"dias", L"dia"})) {
      return RecurrenceHit{L"FREQ=DAILY", std::nullopt, i, i + 3};
    }
    if (AtAny(t, i, {L"diariamente", L"daily"})) {
      return RecurrenceHit{L"FREQ=DAILY", std::nullopt, i, i + 1};
    }
  }
  return std::nullopt;
}

struct DateHit {
  Date date{};
  size_t from = 0;
  size_t to = 0;
};

std::optional<int> ReadDayNumber(std::wstring_view word) {
  std::wstring_view digits = word;
  for (const wchar_t* suffix : {L"st", L"nd", L"rd", L"th"}) {
    if (digits.size() > 2 && digits.ends_with(suffix)) {
      digits = digits.substr(0, digits.size() - 2);
      break;
    }
  }
  if (!AllDigits(digits)) return std::nullopt;
  const int day = Number(digits);
  if (day < 1 || day > 31) return std::nullopt;
  return day;
}

std::optional<int> ReadYear(std::wstring_view word, bool shortToo) {
  if (!AllDigits(word)) return std::nullopt;
  if (word.size() == 4 && Number(word) >= 2000 && Number(word) < 2100) return Number(word);
  if (shortToo && word.size() == 2) return 2000 + Number(word);
  return std::nullopt;
}

// A date that starts with its day: "25 de octubre", "25 octubre de 2027", "25th of October",
// "25/10", "25/10/2027". Day first, as it is written in Colombia. Returns where it ends.
std::optional<DateHit> ReadDayFirst(const std::vector<Token>& t, std::wstring_view folded,
                                    Now now, size_t from, size_t j) {
  if (j >= t.size()) return std::nullopt;
  const std::optional<int> day = ReadDayNumber(t[j].text);
  if (!day) return std::nullopt;

  int month = 0;
  std::optional<int> year;
  size_t k = j + 1;
  if (k < t.size() && Between(folded, t[j], t[k]) == L"/" && AllDigits(t[k].text)) {
    month = Number(t[k].text);
    ++k;
    if (k < t.size() && Between(folded, t[k - 1], t[k]) == L"/") {
      year = ReadYear(t[k].text, /*shortToo=*/true);
      if (!year) return std::nullopt;
      ++k;
    }
  } else {
    if (AtAny(t, k, {L"de", L"of"})) ++k;
    month = k < t.size() ? MonthOf(t[k].text) : 0;
    if (month == 0) return std::nullopt;
    ++k;
    if (At(t, k, L"de") && k + 1 < t.size() && ReadYear(t[k + 1].text, false)) {
      year = ReadYear(t[k + 1].text, false);
      k += 2;
    } else if (k < t.size() && ReadYear(t[k].text, false)) {
      year = ReadYear(t[k].text, false);
      ++k;
    }
  }
  if (month < 1 || month > 12) return std::nullopt;
  const std::optional<Date> date = DayAndMonth(now.date, *day, month, year);
  if (!date) return std::nullopt;
  return DateHit{*date, from, k};
}

// And the one that starts with its month: "October 25", "Oct 25th, 2027", "octubre 25".
std::optional<DateHit> ReadMonthFirst(const std::vector<Token>& t, Now now, size_t from,
                                      size_t j) {
  if (j + 1 >= t.size()) return std::nullopt;
  const int month = MonthOf(t[j].text);
  const std::optional<int> day = ReadDayNumber(t[j + 1].text);
  if (month == 0 || !day) return std::nullopt;
  size_t k = j + 2;
  std::optional<int> year;
  if (k < t.size() && ReadYear(t[k].text, false)) year = ReadYear(t[k++].text, false);
  const std::optional<Date> date = DayAndMonth(now.date, *day, month, year);
  if (!date) return std::nullopt;
  return DateHit{*date, from, k};
}

// "fin de semana", "weekend", "the weekend": the Saturday coming, or today if it is one.
size_t WeekendEnd(const std::vector<Token>& t, size_t j) {
  if (AtAny(t, j, {L"fin", L"finde"}) && At(t, j + 1, L"de") && At(t, j + 2, L"semana")) {
    return j + 3;
  }
  if (At(t, j, L"finde") || At(t, j, L"weekend")) return j + 1;
  return 0;
}

std::optional<DateHit> ReadDateAt(const std::vector<Token>& t, const std::vector<bool>& used,
                                  std::wstring_view folded, Now now, size_t i) {
  const auto free = [&](const std::optional<DateHit>& hit) {
    return hit && RangeFree(used, hit->from, hit->to) ? hit : std::nullopt;
  };
  if (AtAny(t, i, {L"hoy", L"today"})) return DateHit{now.date, i, i + 1};
  if (AtAny(t, i, {L"manana", L"tomorrow"})) return DateHit{AddDays(now.date, 1), i, i + 1};
  if (At(t, i, L"pasado") && At(t, i + 1, L"manana") && RangeFree(used, i, i + 2)) {
    return DateHit{AddDays(now.date, 2), i, i + 2};
  }
  if (At(t, i, L"day") && At(t, i + 1, L"after") && At(t, i + 2, L"tomorrow") &&
      RangeFree(used, i, i + 3)) {
    return DateHit{AddDays(now.date, 2), i, i + 3};
  }

  // "proximo lunes" looks strictly forward; a bare "lunes" can still be today.
  if (AtAny(t, i, {L"proximo", L"proxima", L"siguiente", L"next"}) && i + 1 < t.size()) {
    const int weekday = WeekdayOf(t[i + 1].text);
    if (weekday >= 0 && RangeFree(used, i, i + 2)) {
      return DateHit{NextWeekday(now.date, weekday, true), i, i + 2};
    }
  }

  if (AtAny(t, i, {L"el", L"on", L"the"})) {
    size_t j = i + 1;
    if (At(t, i, L"on") && At(t, j, L"the")) ++j;
    // "el 25 de octubre" before "el 25": the month, when there is one, is part of the date.
    if (const auto hit = free(ReadDayFirst(t, folded, now, i, j))) return hit;
    if (const auto hit = free(ReadMonthFirst(t, now, i, j))) return hit;
    if (const size_t end = WeekendEnd(t, j); end != 0 && RangeFree(used, i, end)) {
      return DateHit{NextWeekday(now.date, 5, false), i, end};
    }
    if (j < t.size() && RangeFree(used, i, j + 1)) {
      const int weekday = WeekdayOf(t[j].text);
      if (weekday >= 0) return DateHit{NextWeekday(now.date, weekday, false), i, j + 1};
      const std::optional<int> day = ReadDayNumber(t[j].text);
      if (day) return DateHit{DayOfMonth(now.date, *day), i, j + 1};
    }
  }
  if (const auto hit = free(ReadDayFirst(t, folded, now, i, i))) return hit;
  if (const auto hit = free(ReadMonthFirst(t, now, i, i))) return hit;

  // "en 3 días", "en 2 semanas", "dentro de un mes", "in a week".
  const bool within = At(t, i, L"dentro") && At(t, i + 1, L"de");
  if (within || AtAny(t, i, {L"en", L"in"})) {
    const size_t j = within ? i + 2 : i + 1;
    const std::optional<int> count = j < t.size() ? CountOf(t[j].text) : std::nullopt;
    if (count && RangeFree(used, i, j + 2)) {
      if (AtAny(t, j + 1, {L"dias", L"dia", L"days", L"day"})) {
        return DateHit{AddDays(now.date, *count), i, j + 2};
      }
      if (AtAny(t, j + 1, {L"semana", L"semanas", L"week", L"weeks"})) {
        return DateHit{AddDays(now.date, 7 * *count), i, j + 2};
      }
      if (AtAny(t, j + 1, {L"mes", L"meses", L"month", L"months"})) {
        return DateHit{AddMonths(now.date, *count), i, j + 2};
      }
    }
  }

  // "fin de mes", "a fin de mes", "end of the month": the last day of this one.
  {
    size_t j = i;
    if (At(t, j, L"a") && AtAny(t, j + 1, {L"fin", L"final", L"finales"})) ++j;
    size_t end = 0;
    if (AtAny(t, j, {L"fin", L"final", L"finales"}) && AtAny(t, j + 1, {L"de", L"del"}) &&
        At(t, j + 2, L"mes")) {
      end = j + 3;
    } else if (At(t, j, L"end") && At(t, j + 1, L"of")) {
      const size_t m = At(t, j + 2, L"the") ? j + 3 : j + 2;
      if (At(t, m, L"month")) end = m + 1;
    }
    if (end != 0 && RangeFree(used, i, end)) {
      const Date last{std::chrono::year_month_day_last{
          now.date.year(), std::chrono::month_day_last{now.date.month()}}};
      return DateHit{last, i, end};
    }
  }

  // "este fin de semana", "this weekend", "fin de semana".
  {
    const size_t j = AtAny(t, i, {L"este", L"this"}) ? i + 1 : i;
    if (const size_t end = WeekendEnd(t, j); end != 0 && RangeFree(used, i, end)) {
      return DateHit{NextWeekday(now.date, 5, false), i, end};
    }
  }

  const int weekday = WeekdayOf(t[i].text);
  if (weekday >= 0) return DateHit{NextWeekday(now.date, weekday, false), i, i + 1};
  return std::nullopt;
}

struct PrefixHit {
  std::optional<Kind> kind;
  Span span{};
};

// Read off the raw text before tokenising, so "t: pagar" and "t:pagar" behave the same. The
// characters are then blanked out of the folded copy, which keeps every later index lined up
// with the original.
PrefixHit ReadPrefix(std::wstring& folded) {
  size_t i = 0;
  while (i < folded.size() && folded[i] == L' ') ++i;
  if (i >= folded.size()) return {};

  size_t length = 0;
  std::optional<Kind> kind;
  if (folded[i] == L'!') {
    length = 1;
    kind = Kind::Task;
  } else if (folded.compare(i, 2, L"t:") == 0) {
    length = 2;
    kind = Kind::Task;
  } else if (folded.compare(i, 2, L"e:") == 0) {
    length = 2;
    kind = Kind::Event;
  }
  if (!kind) return {};

  for (size_t k = i; k < i + length; ++k) folded[k] = L' ';
  return PrefixHit{kind, Span{i, length, SpanKind::Prefix}};
}

// --- Phase 13: guests, places and zones -----------------------------------------------------

bool EndsAddress(wchar_t c) {
  return c == L' ' || c == L'\t' || c == L',' || c == L';' || c == L'(' || c == L')' ||
         c == L'<' || c == L'>';
}

// Every e-mail address in the text, found before tokenising because the tokenizer splits at the
// '@'. Each is blanked out of the folded copy, like the prefix, so no rule reads it again.
std::vector<Span> ReadAddresses(std::wstring_view text, std::wstring& folded,
                                std::vector<Person>& out) {
  std::vector<Span> spans;
  for (size_t at = text.find(L'@'); at != std::wstring_view::npos; at = text.find(L'@', at + 1)) {
    if (at == 0 || EndsAddress(text[at - 1])) continue;  // "@ Oficina" is a place, not this
    size_t from = at;
    while (from > 0 && !EndsAddress(text[from - 1])) --from;
    size_t to = at + 1;
    while (to < text.size() && !EndsAddress(text[to])) ++to;
    while (to > at + 1 && (text[to - 1] == L'.' || text[to - 1] == L':')) --to;
    const std::wstring_view address = text.substr(from, to - from);
    const std::wstring_view domain = address.substr(at - from + 1);
    if (domain.find(L'.') == std::wstring_view::npos || domain.find(L'@') != std::wstring_view::npos ||
        domain.front() == L'.' || domain.back() == L'.') {
      continue;
    }
    out.push_back(Person{L"", std::wstring(address)});
    for (size_t k = from; k < to; ++k) folded[k] = L' ';
    spans.push_back(Span{from, to - from, SpanKind::Person});
    at = to;
    if (at >= text.size()) break;
  }
  return spans;
}

// Whatever no rule claimed, with the runs of whitespace squeezed back together. Because every
// recogniser swallows its own connecting words, dropping the spans is all it takes.
std::wstring BuildTitle(std::wstring_view original, const std::vector<Span>& spans) {
  std::wstring kept;
  size_t cursor = 0;
  for (const Span& span : spans) {
    if (span.offset > cursor) kept.append(original.substr(cursor, span.offset - cursor));
    cursor = (std::max)(cursor, span.offset + span.length);
  }
  if (cursor < original.size()) kept.append(original.substr(cursor));

  std::wstring out;
  bool pending = false;
  for (wchar_t c : kept) {
    if (c == L' ' || c == L'\t') {
      pending = !out.empty();
      continue;
    }
    if (pending) {
      out.push_back(L' ');
      pending = false;
    }
    out.push_back(c);
  }
  return out;
}

std::wstring DayLabel(Date date, Date today) {
  const int delta = static_cast<int>((sys_days{date} - sys_days{today}).count());
  if (delta == 0) return std::wstring(T(L"Hoy", L"Today"));
  if (delta == 1) return std::wstring(T(L"Mañana", L"Tomorrow"));
  if (delta == 2) return std::wstring(T(L"Pasado mañana", L"Day after tomorrow"));
  if (delta > 2 && delta < 7) {
    return std::wstring{WeekdayName(MondayIndex(std::chrono::weekday{sys_days{date}}))};
  }
  return std::format(L"{} {}", static_cast<unsigned>(date.day()),
                     MonthName(date.month()).substr(0, 3));
}

std::wstring HourMinute(int minute) {
  return std::format(L"{:02}:{:02}", minute / 60, minute % 60);
}

// The card says a rule repeats, not what the rule reads like. RRULE is for the Google API.
std::wstring RepeatLabel(const std::wstring& rule) {
  if (rule == L"FREQ=DAILY") return std::wstring(T(L"Cada día", L"Every day"));
  if (rule.starts_with(L"FREQ=WEEKLY")) return std::wstring(T(L"Cada semana", L"Every week"));
  return std::wstring(T(L"Se repite", L"Repeats"));
}

}  // namespace

ParsedInput ParseInput(std::wstring_view text, Now now, int defaultMinutes,
                       const std::vector<Person>& people) {
  ParsedInput out;

  std::wstring folded = Folded(text);

  const PrefixHit prefix = ReadPrefix(folded);
  if (prefix.kind) out.spans.push_back(prefix.span);
  const size_t addressSpans = out.spans.size();
  for (const Span& span : ReadAddresses(text, folded, out.attendees)) out.spans.push_back(span);

  const std::vector<Token> tokens = Tokenize(folded);
  std::vector<bool> used(tokens.size(), false);
  std::vector<std::wstring_view> words;
  for (const Token& token : tokens) words.push_back(token.text);

  // "con ana@x.com", "y luis@x.com": the word that joins an address goes with it.
  for (size_t s = addressSpans; s < out.spans.size(); ++s) {
    Span& span = out.spans[s];
    for (size_t k = tokens.size(); k-- > 0;) {
      if (tokens[k].offset + tokens[k].length > span.offset) continue;
      if (!used[k] && AtAny(tokens, k, {L"con", L"with", L"y", L"and"})) {
        used[k] = true;
        span.length += span.offset - tokens[k].offset;
        span.offset = tokens[k].offset;
      }
      break;
    }
  }

  const auto take = [&](size_t from, size_t to, SpanKind kind) {
    for (size_t k = from; k < to; ++k) used[k] = true;
    const size_t offset = tokens[from].offset;
    const size_t end = tokens[to - 1].offset + tokens[to - 1].length;
    out.spans.push_back(Span{offset, end - offset, kind});
  };

  const std::optional<RecurrenceHit> recurrence = FindRecurrence(tokens);
  if (recurrence) {
    out.recurrence = recurrence->rule;
    take(recurrence->from, recurrence->to, SpanKind::Recurrence);
  }

  // Length before time, so "de 3 a 5" gets to set both and "por 2h" is never read as a clock.
  std::optional<DurationHit> duration;
  for (size_t i = 0; i < tokens.size() && !duration; ++i) {
    if (!used[i]) duration = ReadRange(tokens, used, folded, i);
  }
  for (size_t i = 0; i < tokens.size() && !duration; ++i) {
    if (!used[i]) duration = ReadLength(tokens, used, folded, i);
  }
  const size_t durationSpan = out.spans.size();
  if (duration) {
    take(duration->from, duration->to,
         duration->startMinute ? SpanKind::Time : SpanKind::Duration);
  }

  std::optional<TimeHit> time;
  if (!duration || !duration->startMinute) time = FindTime(tokens, used);
  if (time) take(time->from, time->to, SpanKind::Time);

  // The zone the hour was said in: "hora de Madrid", "Madrid time", or a place right behind
  // the hour, "3pm Madrid", "9am EST". A place anywhere else is where it happens, not when.
  {
    const auto free = [&](size_t from, size_t count) {
      return from + count <= tokens.size() && RangeFree(used, from, from + count);
    };
    size_t count = 0;
    for (size_t i = 0; i + 2 < tokens.size() && out.timeZone.empty(); ++i) {
      if (!AtAny(tokens, i, {L"hora", L"horario"}) || !AtAny(tokens, i + 1, {L"de", L"en"})) continue;
      const ZoneInfo* zone = ZoneAt(words, i + 2, count);
      if (zone != nullptr && free(i, 2 + count)) {
        out.timeZone = zone->iana;
        take(i, i + 2 + count, SpanKind::Zone);
      }
    }
    for (size_t i = 0; i < tokens.size() && out.timeZone.empty(); ++i) {
      const ZoneInfo* zone = ZoneAt(words, i, count);
      if (zone != nullptr && At(tokens, i + count, L"time") && free(i, count + 1)) {
        out.timeZone = zone->iana;
        take(i, i + count + 1, SpanKind::Zone);
      }
    }
    const size_t after = duration && duration->startMinute ? duration->to : (time ? time->to : 0);
    if (out.timeZone.empty() && after > 0) {
      const ZoneInfo* zone = ZoneAt(words, after, count);
      if (zone != nullptr && free(after, count)) {
        out.timeZone = zone->iana;
        take(after, after + count, SpanKind::Zone);
      }
    }
  }

  std::optional<DateHit> date;
  for (size_t i = 0; i < tokens.size() && !date; ++i) {
    if (!used[i]) date = ReadDateAt(tokens, used, folded, now, i);
  }
  if (date) take(date->from, date->to, SpanKind::Date);

  // "con Ana y Luis": names the agenda has seen on a guest list before. A name it has never
  // seen stays in the title, because inviting somebody means knowing where to write to them.
  const auto personAt = [&](size_t at, size_t& count) -> const Person* {
    const Person* best = nullptr;
    count = 0;
    for (const Person& person : people) {
      if (person.name.empty() || person.email.empty()) continue;
      const std::wstring name = Folded(person.name);
      // The whole name, or only its first word.
      for (const bool whole : {true, false}) {
        size_t matched = 0;
        std::wstring_view rest = name;
        while (!rest.empty() && at + matched < tokens.size()) {
          const size_t space = rest.find(L' ');
          if (tokens[at + matched].text != rest.substr(0, space)) break;
          ++matched;
          rest = space == std::wstring_view::npos || !whole ? std::wstring_view{}
                                                             : rest.substr(space + 1);
        }
        if (rest.empty() && matched > count && RangeFree(used, at, at + matched)) {
          best = &person;
          count = matched;
        }
      }
    }
    return best;
  };
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (used[i] || !AtAny(tokens, i, {L"con", L"with"})) continue;
    size_t count = 0;
    const Person* first = personAt(i + 1, count);
    if (first == nullptr) continue;
    out.attendees.push_back(*first);
    size_t end = i + 1 + count;
    while (end < tokens.size() && !used[end] && AtAny(tokens, end, {L"y", L"and"})) {
      const Person* next = personAt(end + 1, count);
      if (next == nullptr) break;
      out.attendees.push_back(*next);
      end += 1 + count;
    }
    take(i, end, SpanKind::Person);
    i = end - 1;
  }

  int minute = duration && duration->startMinute ? *duration->startMinute
                                                 : (time ? time->minute : kNoTime);
  const bool hasTime = minute != kNoTime;
  // The other half of the day, while nothing has ruled it out. "de 3 a 5" is left alone.
  std::optional<int> other;
  if (time && time->twelve) other = (minute + 12 * 60) % kMinutesPerDay;

  // A length with nothing to measure is not a length: "30 min llamada" is just a task whose
  // title happens to say thirty minutes, so those words go back where they came from.
  if (duration && !duration->startMinute && !hasTime) {
    out.spans.erase(out.spans.begin() + static_cast<std::ptrdiff_t>(durationSpan));
    duration.reset();
  }

  out.kind = prefix.kind ? *prefix.kind : (hasTime ? Kind::Event : Kind::Task);

  // Guests, places and zones belong to an event with an hour: a task has nowhere to keep them,
  // and a zone means nothing to a day that lasts all day. Their words go back to the title.
  if (out.kind == Kind::Task || !hasTime) {
    const bool keepPeople = out.kind == Kind::Event;
    std::erase_if(out.spans, [keepPeople](const Span& span) {
      return span.kind == SpanKind::Zone || (!keepPeople && span.kind == SpanKind::Person);
    });
    out.timeZone.clear();
    if (!keepPeople) out.attendees.clear();
  }

  // The place: "en Crepes", "at Starbucks" -- a capital after the word, so "en equipo" stays in
  // the title -- or anything after an '@' that is not an address. It runs to the next thing
  // recognised, or to "con".
  if (out.kind == Kind::Event) {
    const auto capital = [](wchar_t c) {
      return (c >= L'A' && c <= L'Z') || c == L'Á' || c == L'É' || c == L'Í' || c == L'Ó' ||
             c == L'Ú' || c == L'Ñ';
    };
    const auto placeEnd = [&](size_t from) {
      size_t end = from;
      while (end < tokens.size() && !used[end] && !AtAny(tokens, end, {L"con", L"with"})) ++end;
      return end;
    };
    for (size_t i = 0; i + 1 < tokens.size() && out.location.empty(); ++i) {
      if (used[i] || used[i + 1] || !AtAny(tokens, i, {L"en", L"at"})) continue;
      if (!capital(text[tokens[i + 1].offset])) continue;
      const size_t end = placeEnd(i + 1);
      const size_t from = tokens[i + 1].offset;
      const size_t to = tokens[end - 1].offset + tokens[end - 1].length;
      out.location = std::wstring(text.substr(from, to - from));
      take(i, end, SpanKind::Place);
    }
    for (size_t at = folded.find(L'@'); at != std::wstring::npos && out.location.empty();
         at = folded.find(L'@', at + 1)) {
      size_t first = 0;
      while (first < tokens.size() && tokens[first].offset < at) ++first;
      if (first >= tokens.size() || used[first]) continue;
      const size_t end = placeEnd(first);
      const size_t to = tokens[end - 1].offset + tokens[end - 1].length;
      out.location = std::wstring(text.substr(tokens[first].offset, to - tokens[first].offset));
      for (size_t k = first; k < end; ++k) used[k] = true;
      out.spans.push_back(Span{at, to - at, SpanKind::Place});
    }
  }

  // Somebody typed twice, once by name and once by address, is invited once.
  std::vector<Person> guests;
  for (const Person& person : out.attendees) {
    const bool seen = std::any_of(guests.begin(), guests.end(), [&](const Person& other) {
      return Folded(other.email) == Folded(person.email);
    });
    if (!seen) guests.push_back(person);
  }
  out.attendees = std::move(guests);

  std::optional<Date> when;
  if (date) {
    when = date->date;  // an explicit date is taken at its word, even if its hour is long gone
  } else if (recurrence && recurrence->weekday) {
    when = NextWeekday(now.date, *recurrence->weekday, false);
  } else if (hasTime) {
    // With both halves still possible, today wins while either of them is still ahead.
    const int latest = other ? (std::max)(minute, *other) : minute;
    when = latest <= now.minuteOfDay ? AddDays(now.date, 1) : now.date;
  } else if (out.kind == Kind::Event) {
    when = now.date;  // "e: comprar pan" is an all day event, and all day still needs a day
  }

  // "Hoy a las 5" said at ten in the morning is not five in the morning: of the two halves of
  // today, the one that has not gone by. Only when both are still ahead, or on any other day,
  // is it really in doubt, and then the preview asks (CLAUDE.md's eight-to-twenty is the guess).
  if (other && when && *when == now.date) {
    const bool mineGone = minute <= now.minuteOfDay;
    const bool otherGone = *other <= now.minuteOfDay;
    if (mineGone != otherGone) {
      if (mineGone) minute = *other;
      other.reset();
    }
  }
  if (other && when) out.otherMinute = *other;

  if (when) {
    out.allDay = !hasTime;
    out.start = DateTime{*when, hasTime ? minute : kNoTime};
    if (hasTime && out.kind == Kind::Event) {
      out.durationMin = duration ? duration->minutes : defaultMinutes;
      out.end = DateTime{*when, (minute + out.durationMin) % kMinutesPerDay};
    } else {
      out.end = out.start;
    }
  }

  std::sort(out.spans.begin(), out.spans.end(),
            [](const Span& a, const Span& b) { return a.offset < b.offset; });
  out.title = BuildTitle(text, out.spans);
  return out;
}

ParsedInput ParseWithTemplates(std::wstring_view text, Now now, int defaultMinutes,
                               const std::vector<Person>& people,
                               const std::vector<Template>& templates) {
  size_t slash = 0;
  while (slash < text.size() && text[slash] == L' ') ++slash;
  if (slash >= text.size() || text[slash] != L'/') {
    return ParseInput(text, now, defaultMinutes, people);
  }
  // The longest name that the text starts with, whole: "/1:1 mañana" and not "/1".
  const std::wstring folded = Folded(text);
  const Template* found = nullptr;
  size_t end = 0;
  for (const Template& candidate : templates) {
    const std::wstring name = Folded(candidate.name);
    if (name.empty() || folded.compare(slash + 1, name.size(), name) != 0) continue;
    const size_t after = slash + 1 + name.size();
    if (after < text.size() && text[after] != L' ') continue;
    if (found == nullptr || after > end) {
      found = &candidate;
      end = after;
    }
  }
  if (found == nullptr) {
    ParsedInput out;
    out.title.clear();
    std::wstring hint = std::wstring(T(L"Plantillas:", L"Templates:"));
    if (templates.empty()) {
      hint = T(L"Sin plantillas: guárdalas desde el detalle de un evento",
               L"No templates yet: save one from an event's details");
    }
    for (const Template& candidate : templates) {
      hint += (hint.back() == L':' ? L" /" : L" · /") + candidate.name;
    }
    out.templateHint = hint;
    out.spans.push_back(Span{slash, 1, SpanKind::Prefix});
    return out;
  }

  // The template, then whatever was typed after its name.
  const std::wstring expanded = found->text + std::wstring(text.substr(end));
  ParsedInput out = ParseInput(expanded, now, defaultMinutes, people);
  const size_t body = found->text.size();
  std::vector<Span> spans{Span{slash, end - slash, SpanKind::Prefix}};
  for (const Span& span : out.spans) {
    if (span.offset < body) continue;  // inside the template: nothing typed to light up
    spans.push_back(Span{span.offset - body + end, span.length, span.kind});
  }
  out.spans = std::move(spans);
  return out;
}

ParsedInput Flipped(ParsedInput parsed) {
  if (parsed.otherMinute == kNoTime || !parsed.start) return parsed;
  std::swap(parsed.start->minuteOfDay, parsed.otherMinute);
  if (parsed.end && parsed.end->minuteOfDay != kNoTime) {
    parsed.end->minuteOfDay = (parsed.start->minuteOfDay + parsed.durationMin) % kMinutesPerDay;
  }
  return parsed;
}

std::wstring Capitalised(std::wstring text) {
  if (!text.empty() && text[0] >= L'a' && text[0] <= L'z') {
    text[0] = static_cast<wchar_t>(text[0] - L'a' + L'A');
  }
  return text;
}

std::wstring PreviewText(const ParsedInput& parsed, Date today) {
  if (!parsed.templateHint.empty()) return parsed.templateHint;
  std::wstring out;
  const auto add = [&out](const std::wstring& piece) {
    if (piece.empty()) return;
    if (!out.empty()) out += L" · ";
    out += piece;
  };

  if (parsed.kind == Kind::Event && parsed.start) {
    out = L"\U0001F4C5 " + DayLabel(parsed.start->date, today);
    if (!parsed.allDay && parsed.end) {
      add(HourMinute(parsed.start->minuteOfDay) + L"–" + HourMinute(parsed.end->minuteOfDay));
    }
  } else if (parsed.start) {
    out = L"☑ " + std::wstring(T(L"Tarea", L"Task"));
    add(DayLabel(parsed.start->date, today));
    if (!parsed.allDay) add(HourMinute(parsed.start->minuteOfDay));
  } else {
    out = L"☑ " + std::wstring(T(L"Tarea sin fecha", L"Task, no date"));
  }

  if (parsed.recurrence) add(RepeatLabel(*parsed.recurrence));
  add(Capitalised(parsed.title));
  if (!parsed.location.empty()) add(std::wstring(T(L"en ", L"at ")) + parsed.location);
  if (!parsed.attendees.empty()) {
    std::wstring names;
    for (const Person& person : parsed.attendees) {
      if (!names.empty()) names += L", ";
      names += person.name.empty() ? person.email : person.name;
    }
    add(std::wstring(T(L"con ", L"with ")) + names);
  }
  return out;
}

}  // namespace agenda::nlp
