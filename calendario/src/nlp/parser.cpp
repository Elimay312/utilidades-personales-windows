#include "nlp/parser.h"

#include <algorithm>
#include <format>
#include <initializer_list>

namespace agenda::nlp {
namespace {

using std::chrono::sys_days;

constexpr int kMinutesPerDay = 24 * 60;
constexpr int kDefaultEventMinutes = 60;

// Every character folds to exactly one character and none of them ever disappear, so an index
// into the folded text is the same index in the original. That is what lets a span point back
// at what the user actually typed, accents and capitals and all.
//
// Written as escapes and not as letters on purpose: this file is UTF-8 and compiles with
// /utf-8, and an escape survives any tool that decides to save it as something else.
wchar_t Fold(wchar_t c) {
  if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
  switch (c) {
    case L'Á': case L'á': return L'a';
    case L'É': case L'é': return L'e';
    case L'Í': case L'í': return L'i';
    case L'Ó': case L'ó': return L'o';
    case L'Ú': case L'ú':
    case L'Ü': case L'ü': return L'u';
    case L'Ñ': case L'ñ': return L'n';
    default: return c;
  }
}

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
    return Clock{Number(hours) * 60 + Number(minutes), true, false};
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
    minute = ((hour % 12) + (t[end].text == L"pm" ? 12 : 0)) * 60;
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

  if (!clock->exact) minute = Daylight(minute);

  // A naked number only counts as a time when something says so: a marker like "a las", or a
  // date right before it as in "pasado manana 9". Without this, "el 25" and "comprar 2 panes"
  // would both turn into times.
  if (bare && !marked && !(i > 0 && IsDateWord(t[i - 1].text))) return std::nullopt;

  return TimeHit{minute, i, end};
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

// "de 3 a 5" and "from 3 to 5": one pattern that produces a time and a length at once. It
// matches whole or not at all, so a half match never eats the "de" of "de la tarde".
std::optional<DurationHit> ReadRange(const std::vector<Token>& t, const std::vector<bool>& used,
                                     size_t i) {
  if (!AtAny(t, i, {L"de", L"from"}) || i + 3 >= t.size()) return std::nullopt;
  const std::optional<Clock> first = ReadClock(t[i + 1].text);
  if (!first || !AtAny(t, i + 2, {L"a", L"to"})) return std::nullopt;
  const std::optional<Clock> second = ReadClock(t[i + 3].text);
  if (!second) return std::nullopt;

  const int start = first->exact ? first->minute : Daylight(first->minute);
  const int end = second->exact ? second->minute : Daylight(second->minute);
  if (end <= start || !RangeFree(used, i, i + 4)) return std::nullopt;
  return DurationHit{end - start, start, i, i + 4};
}

// "por 2h", "for 30 min", "durante 90 minutos", and a bare "30 min". A bare "2h" is not a
// length: "17h reunion" is a time, and seventeen hours long would be absurd.
std::optional<DurationHit> ReadLength(const std::vector<Token>& t, const std::vector<bool>& used,
                                      size_t i) {
  size_t j = i;
  const bool keyword = AtAny(t, j, {L"por", L"for", L"durante"});
  if (keyword) ++j;
  if (j >= t.size()) return std::nullopt;

  const std::wstring_view word = t[j].text;
  int minutes = 0;
  size_t end = 0;
  if (keyword && word.size() > 1 && word.back() == L'h' &&
      AllDigits(word.substr(0, word.size() - 1))) {
    minutes = Number(word.substr(0, word.size() - 1)) * 60;
    end = j + 1;
  } else if (AllDigits(word) && AtAny(t, j + 1, {L"min", L"mins", L"minutos", L"minutes"})) {
    minutes = Number(word);
    end = j + 2;
  } else if (keyword && AllDigits(word) &&
             AtAny(t, j + 1, {L"h", L"hora", L"horas", L"hour", L"hours"})) {
    minutes = Number(word) * 60;
    end = j + 2;
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

std::optional<DateHit> ReadDateAt(const std::vector<Token>& t, const std::vector<bool>& used,
                                  Now now, size_t i) {
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
    if (j < t.size() && RangeFree(used, i, j + 1)) {
      const int weekday = WeekdayOf(t[j].text);
      if (weekday >= 0) return DateHit{NextWeekday(now.date, weekday, false), i, j + 1};
      const std::optional<int> day = ReadDayNumber(t[j].text);
      if (day) return DateHit{DayOfMonth(now.date, *day), i, j + 1};
    }
  }

  if (AtAny(t, i, {L"en", L"in"}) && i + 2 < t.size() && AllDigits(t[i + 1].text) &&
      AtAny(t, i + 2, {L"dias", L"dia", L"days", L"day"}) && RangeFree(used, i, i + 3)) {
    return DateHit{AddDays(now.date, Number(t[i + 1].text)), i, i + 3};
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
  if (delta == 0) return L"Hoy";
  if (delta == 1) return L"Mañana";
  if (delta == 2) return L"Pasado mañana";
  if (delta > 2 && delta < 7) {
    return std::wstring{kWeekdayNames[MondayIndex(std::chrono::weekday{sys_days{date}})]};
  }
  return std::format(L"{} {}", static_cast<unsigned>(date.day()),
                     MonthName(date.month()).substr(0, 3));
}

std::wstring HourMinute(int minute) {
  return std::format(L"{:02}:{:02}", minute / 60, minute % 60);
}

// The card says a rule repeats, not what the rule reads like. RRULE is for the Google API.
std::wstring RepeatLabel(const std::wstring& rule) {
  if (rule == L"FREQ=DAILY") return L"Cada día";
  if (rule.starts_with(L"FREQ=WEEKLY")) return L"Cada semana";
  return L"Se repite";
}

}  // namespace

ParsedInput ParseInput(std::wstring_view text, Now now) {
  ParsedInput out;

  std::wstring folded;
  folded.reserve(text.size());
  for (wchar_t c : text) folded.push_back(Fold(c));

  const PrefixHit prefix = ReadPrefix(folded);
  if (prefix.kind) out.spans.push_back(prefix.span);

  const std::vector<Token> tokens = Tokenize(folded);
  std::vector<bool> used(tokens.size(), false);

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
    if (!used[i]) duration = ReadRange(tokens, used, i);
  }
  for (size_t i = 0; i < tokens.size() && !duration; ++i) {
    if (!used[i]) duration = ReadLength(tokens, used, i);
  }
  const size_t durationSpan = out.spans.size();
  if (duration) {
    take(duration->from, duration->to,
         duration->startMinute ? SpanKind::Time : SpanKind::Duration);
  }

  std::optional<TimeHit> time;
  if (!duration || !duration->startMinute) time = FindTime(tokens, used);
  if (time) take(time->from, time->to, SpanKind::Time);

  std::optional<DateHit> date;
  for (size_t i = 0; i < tokens.size() && !date; ++i) {
    if (!used[i]) date = ReadDateAt(tokens, used, now, i);
  }
  if (date) take(date->from, date->to, SpanKind::Date);

  const int minute = duration && duration->startMinute ? *duration->startMinute
                                                       : (time ? time->minute : kNoTime);
  const bool hasTime = minute != kNoTime;

  // A length with nothing to measure is not a length: "30 min llamada" is just a task whose
  // title happens to say thirty minutes, so those words go back where they came from.
  if (duration && !duration->startMinute && !hasTime) {
    out.spans.erase(out.spans.begin() + static_cast<std::ptrdiff_t>(durationSpan));
    duration.reset();
  }

  out.kind = prefix.kind ? *prefix.kind : (hasTime ? Kind::Event : Kind::Task);

  std::optional<Date> when;
  if (date) {
    when = date->date;  // an explicit date is taken at its word, even if its hour is long gone
  } else if (recurrence && recurrence->weekday) {
    when = NextWeekday(now.date, *recurrence->weekday, false);
  } else if (hasTime) {
    when = minute <= now.minuteOfDay ? AddDays(now.date, 1) : now.date;
  } else if (out.kind == Kind::Event) {
    when = now.date;  // "e: comprar pan" is an all day event, and all day still needs a day
  }

  if (when) {
    out.allDay = !hasTime;
    out.start = DateTime{*when, hasTime ? minute : kNoTime};
    if (hasTime && out.kind == Kind::Event) {
      out.durationMin = duration ? duration->minutes : kDefaultEventMinutes;
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

std::wstring Capitalised(std::wstring text) {
  if (!text.empty() && text[0] >= L'a' && text[0] <= L'z') {
    text[0] = static_cast<wchar_t>(text[0] - L'a' + L'A');
  }
  return text;
}

std::wstring PreviewText(const ParsedInput& parsed, Date today) {
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
    out = L"☑ Tarea";
    add(DayLabel(parsed.start->date, today));
    if (!parsed.allDay) add(HourMinute(parsed.start->minuteOfDay));
  } else {
    out = L"☑ Tarea sin fecha";
  }

  if (parsed.recurrence) add(RepeatLabel(*parsed.recurrence));
  add(Capitalised(parsed.title));
  return out;
}

}  // namespace agenda::nlp
