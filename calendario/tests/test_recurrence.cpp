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

TEST_CASE("the panel's five answers, and anything else is left alone as custom") {
  CHECK(RepeatOf(L"") == Repeat::None);
  CHECK(RepeatOf(L"FREQ=DAILY") == Repeat::Daily);
  CHECK(RepeatOf(L"RRULE:FREQ=WEEKLY;BYDAY=MO") == Repeat::Weekly);
  CHECK(RepeatOf(L"FREQ=WEEKLY") == Repeat::Weekly);
  CHECK(RepeatOf(L"RRULE:FREQ=MONTHLY") == Repeat::Monthly);
  CHECK(RepeatOf(L"RRULE:FREQ=YEARLY") == Repeat::Yearly);
  CHECK(RepeatOf(L"RRULE:FREQ=WEEKLY;BYDAY=MO,TH") == Repeat::Custom);
  CHECK(RepeatOf(L"RRULE:FREQ=DAILY;COUNT=5") == Repeat::Custom);

  const Date tuesday = Day(2026, 9, 22);
  CHECK(RuleFor(Repeat::Weekly, tuesday) == L"RRULE:FREQ=WEEKLY;BYDAY=TU");
  CHECK(RuleFor(Repeat::Daily, tuesday) == L"RRULE:FREQ=DAILY");
  CHECK(RuleFor(Repeat::None, tuesday).empty());
  CHECK(RuleFor(Repeat::Custom, tuesday).empty());
}

TEST_CASE("a weekly series dragged to another day repeats on the new day") {
  const Date thursday = Day(2026, 9, 24);
  CHECK(MoveRuleTo(L"FREQ=WEEKLY;BYDAY=MO", thursday) == L"FREQ=WEEKLY;BYDAY=TH");
  CHECK(MoveRuleTo(L"RRULE:FREQ=WEEKLY;BYDAY=MO", thursday) == L"RRULE:FREQ=WEEKLY;BYDAY=TH");
  // Two days, a count, or not weekly at all: nothing to follow, so nothing moves.
  CHECK(MoveRuleTo(L"RRULE:FREQ=WEEKLY;BYDAY=MO,WE", thursday) ==
        L"RRULE:FREQ=WEEKLY;BYDAY=MO,WE");
  CHECK(MoveRuleTo(L"RRULE:FREQ=WEEKLY;BYDAY=MO;COUNT=4", thursday) ==
        L"RRULE:FREQ=WEEKLY;BYDAY=MO;COUNT=4");
  CHECK(MoveRuleTo(L"RRULE:FREQ=DAILY", thursday) == L"RRULE:FREQ=DAILY");
}
