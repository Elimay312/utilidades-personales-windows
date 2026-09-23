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
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;BYDAY=TU;BYSETPOS=2", start, Day(2026, 9, 8)));
  // "The 20th Monday of the year": a yearly ordinal with no month to count inside.
  CHECK_FALSE(OccursOn("RRULE:FREQ=YEARLY;BYDAY=20MO", start, Day(2027, 5, 17)));
  // An ordinal in a weekly rule is not a rule at all.
  CHECK_FALSE(OccursOn("RRULE:FREQ=WEEKLY;BYDAY=1TU", start, Day(2026, 9, 8)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;BYDAY=6TU", start, Day(2026, 10, 6)));
  CHECK_FALSE(OccursOn("", start, Day(2026, 9, 2)));
  CHECK(OccursOn("RRULE:FREQ=MONTHLY;BYSETPOS=1", start, start));
}

TEST_CASE("the nth weekday of the month, from the start and from the end") {
  // The first Tuesday: 1 September, 6 October, 3 November.
  const char* first = "RRULE:FREQ=MONTHLY;BYDAY=1TU";
  const Date start = Day(2026, 9, 1);
  CHECK(OccursOn(first, start, Day(2026, 10, 6)));
  CHECK(OccursOn(first, start, Day(2026, 11, 3)));
  CHECK_FALSE(OccursOn(first, start, Day(2026, 10, 13)));
  CHECK_FALSE(OccursOn(first, start, Day(2026, 9, 8)));
  // The fourth Thursday, which is what Google writes for "monthly on the fourth Thursday".
  CHECK(OccursOn("RRULE:FREQ=MONTHLY;BYDAY=4TH", Day(2026, 9, 24), Day(2026, 10, 22)));
  // The last Friday: 25 September, 30 October, 27 November.
  const char* last = "RRULE:FREQ=MONTHLY;BYDAY=-1FR";
  const Date friday = Day(2026, 9, 25);
  CHECK(OccursOn(last, friday, Day(2026, 10, 30)));
  CHECK(OccursOn(last, friday, Day(2026, 11, 27)));
  CHECK_FALSE(OccursOn(last, friday, Day(2026, 10, 23)));
  // Every two months, and a COUNT that runs out.
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;INTERVAL=2;BYDAY=1TU", start, Day(2026, 10, 6)));
  CHECK(OccursOn("RRULE:FREQ=MONTHLY;INTERVAL=2;BYDAY=1TU", start, Day(2026, 11, 3)));
  CHECK(OccursOn("RRULE:FREQ=MONTHLY;COUNT=2;BYDAY=1TU", start, Day(2026, 10, 6)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;COUNT=2;BYDAY=1TU", start, Day(2026, 11, 3)));
}

TEST_CASE("a day of the month, the last one included, and where it meets a weekday") {
  const Date start = Day(2026, 9, 15);
  CHECK(OccursOn("RRULE:FREQ=MONTHLY;BYMONTHDAY=15", start, Day(2026, 10, 15)));
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;BYMONTHDAY=15", start, Day(2026, 10, 16)));
  // The last day, whatever its number: 30 September, 31 October, 28 February.
  const char* lastDay = "RRULE:FREQ=MONTHLY;BYMONTHDAY=-1";
  const Date the30th = Day(2026, 9, 30);
  CHECK(OccursOn(lastDay, the30th, Day(2026, 10, 31)));
  CHECK(OccursOn(lastDay, the30th, Day(2027, 2, 28)));
  CHECK_FALSE(OccursOn(lastDay, the30th, Day(2026, 10, 30)));
  // A 31st that some months lack is skipped there, not moved.
  CHECK_FALSE(OccursOn("RRULE:FREQ=MONTHLY;BYMONTHDAY=31", Day(2026, 8, 31), Day(2026, 9, 30)));
  // Both at once is where they meet: Friday the 13th, next in November 2026.
  const char* unlucky = "RRULE:FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13";
  CHECK(OccursOn(unlucky, Day(2026, 2, 13), Day(2026, 11, 13)));
  CHECK_FALSE(OccursOn(unlucky, Day(2026, 2, 13), Day(2026, 10, 13)));
}

TEST_CASE("a yearly rule inside its months") {
  // The fourth Thursday of November.
  const char* thanksgiving = "RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=4TH";
  const Date start = Day(2026, 11, 26);
  CHECK(OccursOn(thanksgiving, start, Day(2027, 11, 25)));
  CHECK_FALSE(OccursOn(thanksgiving, start, Day(2027, 11, 18)));
  CHECK_FALSE(OccursOn(thanksgiving, start, Day(2027, 10, 28)));
  // Two months of the year on the start's own date.
  const char* twice = "RRULE:FREQ=YEARLY;BYMONTH=3,9";
  CHECK(OccursOn(twice, Day(2026, 9, 22), Day(2027, 3, 22)));
  CHECK_FALSE(OccursOn(twice, Day(2026, 9, 22), Day(2027, 4, 22)));
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

TEST_CASE("an EXDATE takes its day away, in the shapes Google and imports write it") {
  const Date monday = Day(2026, 9, 21);
  const char* rule =
      "RRULE:FREQ=WEEKLY;BYDAY=MO\n"
      "EXDATE;TZID=America/Bogota:20260928T070000,20261012T070000";
  CHECK_FALSE(OccursOn(rule, monday, Day(2026, 9, 28)));
  CHECK(OccursOn(rule, monday, Day(2026, 10, 5)));
  CHECK_FALSE(OccursOn(rule, monday, Day(2026, 10, 12)));
  // An all-day series, with the lines the other way round, and the first day excluded.
  const char* allDay = "EXDATE;VALUE=DATE:20260921\r\nFREQ=DAILY";
  CHECK_FALSE(OccursOn(allDay, monday, monday));
  CHECK(OccursOn(allDay, monday, Day(2026, 9, 22)));
}

TEST_CASE("an EXDATE does not give its occurrence back to COUNT") {
  // Three Mondays counted, the 28th excluded: the 21st and 5 October happen, the 12th does not.
  const char* rule = "RRULE:FREQ=WEEKLY;COUNT=3\nEXDATE:20260928";
  const Date monday = Day(2026, 9, 21);
  CHECK(OccursOn(rule, monday, Day(2026, 10, 5)));
  CHECK_FALSE(OccursOn(rule, monday, Day(2026, 10, 12)));
}

TEST_CASE("the panel and a drag read the RRULE line and keep the EXDATEs") {
  const std::wstring rule = L"RRULE:FREQ=WEEKLY;BYDAY=MO\nEXDATE:20260928";
  CHECK(RepeatOf(rule) == Repeat::Weekly);
  CHECK(MoveRuleTo(rule, Day(2026, 9, 24)) == L"RRULE:FREQ=WEEKLY;BYDAY=TH\nEXDATE:20260928");
  CHECK(MoveRuleTo(L"EXDATE:20260928\nRRULE:FREQ=WEEKLY;BYDAY=MO", Day(2026, 9, 24)) ==
        L"EXDATE:20260928\nRRULE:FREQ=WEEKLY;BYDAY=TH");
}
