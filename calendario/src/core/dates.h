#pragma once

#include <chrono>
#include <ctime>
#include <string_view>

#include "core/i18n.h"

namespace agenda {

using Date = std::chrono::year_month_day;
using Month = std::chrono::year_month;

// The grid is always six rows, even for a month that fits in five. Keeping the height fixed
// means the panel never jumps and the month slide never has to resize anything.
inline constexpr int kGridRows = 6;
inline constexpr int kGridCols = 7;
inline constexpr int kGridCells = kGridRows * kGridCols;

// Monday first in both languages: L M X J V S D, M T W T F S S.
inline constexpr std::wstring_view kWeekdayInitials[2][7] = {
    {L"L", L"M", L"X", L"J", L"V", L"S", L"D"}, {L"M", L"T", L"W", L"T", L"F", L"S", L"S"}};

// Spelled out for the preview card, which has room for the whole word.
inline constexpr std::wstring_view kWeekdayNames[2][7] = {
    {L"Lunes", L"Martes", L"Miércoles", L"Jueves", L"Viernes", L"Sábado", L"Domingo"},
    {L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday"}};

// Spelled out here instead of asked to GetLocaleInfoEx, so a snapshot taken on a machine set to
// English still says "Septiembre" when the interface is in Spanish.
inline constexpr std::wstring_view kMonthNames[2][12] = {
    {L"Enero", L"Febrero", L"Marzo", L"Abril", L"Mayo", L"Junio", L"Julio", L"Agosto",
     L"Septiembre", L"Octubre", L"Noviembre", L"Diciembre"},
    {L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August",
     L"September", L"October", L"November", L"December"}};

inline int MondayIndex(std::chrono::weekday day) {
  return static_cast<int>((day.c_encoding() + 6u) % 7u);
}

inline std::wstring_view WeekdayInitial(int mondayIndex) {
  return kWeekdayInitials[English() ? 1 : 0][mondayIndex];
}

inline std::wstring_view WeekdayName(int mondayIndex) {
  return kWeekdayNames[English() ? 1 : 0][mondayIndex];
}

inline std::wstring_view MonthName(std::chrono::month month) {
  return kMonthNames[English() ? 1 : 0][static_cast<unsigned>(month) - 1u];
}

inline Date AddDays(Date date, int days) {
  return Date{std::chrono::sys_days{date} + std::chrono::days{days}};
}

// Same day of the next or previous month, clamped to the last one that exists: a month after
// 31 January is 28 February, not 3 March.
inline Date AddMonths(Date date, int months) {
  const Month moved = Month{date.year(), date.month()} + std::chrono::months{months};
  const Date kept{moved.year(), moved.month(), date.day()};
  if (kept.ok()) return kept;
  const std::chrono::year_month_day_last last{moved.year(),
                                              std::chrono::month_day_last{moved.month()}};
  return Date{last.year(), last.month(), last.day()};
}

// First cell of the grid: the Monday of the week that holds the first of the month, which is
// usually a few days into the month before.
inline Date GridStart(Month month) {
  const std::chrono::sys_days first{Date{month.year(), month.month(), std::chrono::day{1}}};
  return Date{first - std::chrono::days{MondayIndex(std::chrono::weekday{first})}};
}

inline Date CellDate(Month month, int cell) {
  return AddDays(GridStart(month), cell);
}

// A day and a minute of it as one number: minutes of wall clock since 1 January 1970. Not an
// instant -- no time zone goes in or comes out -- which is exactly what comparing two readings
// of the same wall needs, the one a reminder is due at and the one the clock says now.
inline long long WallMinute(Date day, int minute) {
  return static_cast<long long>(std::chrono::sys_days{day}.time_since_epoch().count()) * 1440 +
         minute;
}

inline Date DayOfWall(long long wall) {
  const long long days = wall >= 0 ? wall / 1440 : (wall - 1439) / 1440;
  return Date{std::chrono::sys_days{std::chrono::days{days}}};
}

inline Date TodayLocal() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  return Date{std::chrono::year{local.tm_year + 1900},
              std::chrono::month{static_cast<unsigned>(local.tm_mon) + 1u},
              std::chrono::day{static_cast<unsigned>(local.tm_mday)}};
}

// Minutes since midnight. The parser needs it to know whether an hour has already gone by, and
// this is the only place in the project that reads the clock.
inline int NowMinuteLocal() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  return local.tm_hour * 60 + local.tm_min;
}

}  // namespace agenda
