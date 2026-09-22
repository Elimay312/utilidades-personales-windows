#include <catch2/catch_test_macros.hpp>

#include "core/dates.h"

using namespace agenda;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

namespace {

Date Ymd(int y, unsigned m, unsigned d) {
  return Date{year{y}, month{m}, day{d}};
}

}  // namespace

TEST_CASE("the week starts on Monday") {
  CHECK(MondayIndex(std::chrono::Monday) == 0);
  CHECK(MondayIndex(std::chrono::Thursday) == 3);
  CHECK(MondayIndex(std::chrono::Sunday) == 6);
  CHECK(kWeekdayInitials[0] == L"L");
  CHECK(kWeekdayInitials[6] == L"D");
}

TEST_CASE("the grid starts on the Monday of the week holding the first") {
  // 1 September 2026 is a Tuesday, so the grid opens on 31 August.
  CHECK(GridStart(Month{year{2026}, month{9}}) == Ymd(2026, 8, 31));
  // June 2026 starts on a Monday, so there is nothing from the month before.
  CHECK(GridStart(Month{year{2026}, month{6}}) == Ymd(2026, 6, 1));
}

TEST_CASE("the grid always covers six weeks") {
  const Month september{year{2026}, month{9}};
  CHECK(CellDate(september, 0) == Ymd(2026, 8, 31));
  CHECK(CellDate(september, 1) == Ymd(2026, 9, 1));
  CHECK(CellDate(september, kGridCells - 1) == Ymd(2026, 10, 11));
  CHECK(kGridCells == 42);
}

TEST_CASE("moving a month keeps the day when it exists") {
  CHECK(AddMonths(Ymd(2026, 9, 22), 1) == Ymd(2026, 10, 22));
  CHECK(AddMonths(Ymd(2026, 1, 15), -1) == Ymd(2025, 12, 15));
}

TEST_CASE("moving a month clamps to the last day that exists") {
  CHECK(AddMonths(Ymd(2026, 1, 31), 1) == Ymd(2026, 2, 28));
  CHECK(AddMonths(Ymd(2026, 3, 31), -1) == Ymd(2026, 2, 28));
  CHECK(AddMonths(Ymd(2024, 1, 31), 1) == Ymd(2024, 2, 29));
}

TEST_CASE("moving days crosses months and years") {
  CHECK(AddDays(Ymd(2026, 9, 22), 7) == Ymd(2026, 9, 29));
  CHECK(AddDays(Ymd(2026, 9, 29), 7) == Ymd(2026, 10, 6));
  CHECK(AddDays(Ymd(2026, 1, 1), -1) == Ymd(2025, 12, 31));
}

TEST_CASE("month names are Spanish regardless of the machine locale") {
  CHECK(MonthName(month{1}) == L"Enero");
  CHECK(MonthName(month{9}) == L"Septiembre");
  CHECK(MonthName(month{12}) == L"Diciembre");
}
