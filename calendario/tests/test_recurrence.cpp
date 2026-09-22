#include <catch2/catch_test_macros.hpp>

#include "core/recurrence.h"

using namespace agenda;

namespace {

Date Day(int year, unsigned month, unsigned day) {
  return Date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
}

}  // namespace

TEST_CASE("the first day is always an occurrence, and nothing comes before it") {
  CHECK(OccursOn("FREQ=DAILY", Day(2026, 9, 22), Day(2026, 9, 22)));
  CHECK_FALSE(OccursOn("FREQ=DAILY", Day(2026, 9, 22), Day(2026, 9, 21)));
}

TEST_CASE("daily, with and without an interval") {
  const Date start = Day(2026, 9, 22);
  CHECK(OccursOn("FREQ=DAILY", start, Day(2026, 9, 23)));
  CHECK(OccursOn("RRULE:FREQ=DAILY;INTERVAL=3", start, Day(2026, 9, 25)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=DAILY;INTERVAL=3", start, Day(2026, 9, 24)));
}

TEST_CASE("weekly on the parser's BYDAY and on the start's own weekday") {
  const Date monday = Day(2026, 9, 21);
  CHECK(OccursOn("FREQ=WEEKLY;BYDAY=MO", monday, Day(2026, 9, 28)));
  CHECK_FALSE(OccursOn("FREQ=WEEKLY;BYDAY=MO", monday, Day(2026, 9, 29)));
  CHECK(OccursOn("FREQ=WEEKLY", monday, Day(2026, 10, 5)));
  CHECK(OccursOn("RRULE:FREQ=WEEKLY;BYDAY=MO,TH", monday, Day(2026, 9, 24)));
  // Every two weeks: the week after is skipped, the one after that is not.
  CHECK_FALSE(OccursOn("RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO", monday, Day(2026, 9, 28)));
  CHECK(OccursOn("RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO", monday, Day(2026, 10, 5)));
}

TEST_CASE("COUNT stops the series, counting the first day") {
  const Date monday = Day(2026, 9, 21);
  // Three Mondays: the 21st, the 28th and 5 October. The 12th is the fourth.
  CHECK(OccursOn("RRULE:FREQ=WEEKLY;COUNT=3", monday, Day(2026, 10, 5)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=WEEKLY;COUNT=3", monday, Day(2026, 10, 12)));
}

TEST_CASE("UNTIL is inclusive, in both of the shapes Google writes it") {
  const Date start = Day(2026, 9, 22);
  CHECK(OccursOn("RRULE:FREQ=DAILY;UNTIL=20260925", start, Day(2026, 9, 25)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=DAILY;UNTIL=20260925", start, Day(2026, 9, 26)));
  CHECK(OccursOn("RRULE:FREQ=DAILY;UNTIL=20260925T045959Z", start, Day(2026, 9, 25)));
}

TEST_CASE("monthly skips the months without that day, yearly keeps its date") {
  const Date the31st = Day(2026, 1, 31);
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY", the31st, Day(2026, 2, 28)));
  CHECK(OccursOn("RRULE:FREQ=MONTHLY", the31st, Day(2026, 3, 31)));
  CHECK(OccursOn("RRULE:FREQ=YEARLY", Day(2026, 9, 22), Day(2027, 9, 22)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=YEARLY", Day(2026, 9, 22), Day(2027, 9, 23)));
}

TEST_CASE("what it cannot read stays on its first day instead of being guessed") {
  const Date start = Day(2026, 9, 1);
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;BYDAY=1TU", start, Day(2026, 10, 6)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;BYMONTHDAY=15", start, Day(2026, 9, 15)));
  CHECK_FALSE(OccursOn("", start, Day(2026, 9, 2)));
  CHECK(OccursOn("RRULE:FREQ=MONTHLY;BYDAY=1TU", start, start));
}
