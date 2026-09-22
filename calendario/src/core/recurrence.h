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
// ponytail: no EXDATE and no moved instances; they arrive when one occurrence can be edited.

#include <string_view>

#include "core/dates.h"

namespace agenda {

// `rule` with or without its "RRULE:" prefix; both shapes are in the cache, the parser's and
// Google's. `start` is the day the series begins on.
bool OccursOn(std::string_view rule, Date start, Date day);

}  // namespace agenda
