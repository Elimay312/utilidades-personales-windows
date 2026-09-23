#pragma once

// Whether a repeating event happens on a given day.
//
// Until phase 6 a rule was stored and not unfolded: the popup only ever showed one day, and the
// event sat on its first one with a card that said it repeats. The week and the month views are
// what CLAUDE.md was waiting for, so the rule is read here -- on a day, for a day, with no
// timezone and no instant in sight, because the cache keeps wall clocks.
//
// What it understands is what the parser writes and what Google sends for the everyday cases:
// FREQ DAILY/WEEKLY/MONTHLY/YEARLY with INTERVAL, BYDAY (plain weekdays), COUNT and UNTIL.
// Anything else -- BYDAY=1MO, BYMONTHDAY, BYSETPOS -- is not guessed at: the event stays on its
// first day, which is exactly what it did before this file existed.
//
// A recurrence is Google's list kept one line per line: the RRULE and any EXDATE next to it.
// An occurrence moved or cancelled at Google is not in here; it is a row of its own in the cache
// (events.series_id and original_day, schema v4), and the store skips its day.

#include <string>
#include <string_view>

#include "core/dates.h"

namespace agenda {

// `rule` with or without its "RRULE:" prefix; both shapes are in the cache, the parser's and
// Google's, and with its EXDATE lines, which take their days away. `start` is the day the series
// begins on.
bool OccursOn(std::string_view rule, Date start, Date day);

// The five answers the detail panel offers, and "Custom" for any rule that is not one of them
// -- which the panel shows and keeps, but does not pretend to edit.
enum class Repeat { None, Daily, Weekly, Monthly, Yearly, Custom };

Repeat RepeatOf(std::wstring_view rule);
// The rule for an answer, for a series that starts on `start`: a weekly one repeats on that
// weekday. Empty for None; Custom has no rule of its own and also gives empty.
std::wstring RuleFor(Repeat repeat, Date start);

// A weekly rule on one day, after its series was dragged to start on `start`: the day moves
// with it, or the series would start on a Tuesday and go on repeating on Mondays. Any other
// rule comes back as it was.
std::wstring MoveRuleTo(std::wstring_view rule, Date start);

}  // namespace agenda
