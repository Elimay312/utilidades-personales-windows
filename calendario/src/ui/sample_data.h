#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/dates.h"

namespace agenda {

// A stand-in event, only as rich as a card needs. The real model arrives with SQLite in a
// later phase, so nothing here knows about storage or synchronisation.
struct SampleEvent {
  int startMin = 0;  // minutes since midnight
  int endMin = 0;
  std::wstring title;
  std::uint32_t color = 0;
};

namespace detail {

// Keyed by day of the month rather than by a fixed date, so every month the user browses to
// looks lived in instead of empty.
struct SampleRow {
  unsigned day;
  int startMin;
  int endMin;
  const wchar_t* title;
  std::uint32_t color;
};

inline constexpr SampleRow kSampleRows[] = {
    {3, 9 * 60, 10 * 60, L"Revisión de diseño", 0x4A8BF5},
    {8, 16 * 60 + 30, 17 * 60 + 30, L"Dentista", 0xF5A623},
    {12, 8 * 60, 8 * 60 + 30, L"Standup del equipo", 0x4A8BF5},
    {12, 13 * 60, 14 * 60, L"Almuerzo con Ana", 0xF5A623},
    {15, 11 * 60, 12 * 60, L"Llamada con el banco", 0x4A8BF5},
    {22, 9 * 60 + 30, 10 * 60 + 30, L"Reunión de equipo", 0x4A8BF5},
    {22, 14 * 60, 15 * 60, L"Almuerzo con Ana", 0xF5A623},
    {27, 7 * 60, 8 * 60, L"Gimnasio", 0xF5A623},
    {27, 10 * 60, 11 * 60, L"Entrega del informe", 0x4A8BF5},
    {27, 18 * 60, 19 * 60, L"Cumpleaños de Sofía", 0xF5A623},
};

}  // namespace detail

inline std::vector<SampleEvent> SampleEvents(Date date) {
  std::vector<SampleEvent> events;
  for (const detail::SampleRow& row : detail::kSampleRows) {
    if (row.day != static_cast<unsigned>(date.day())) continue;
    events.push_back(SampleEvent{row.startMin, row.endMin, row.title, row.color});
  }
  return events;
}

// The colour of the four DIP dot under a day, or nothing when the day is free. One lookup
// answers both questions, which keeps the grid from building a vector per cell.
inline std::optional<std::uint32_t> SampleDayColor(Date date) {
  for (const detail::SampleRow& row : detail::kSampleRows) {
    if (row.day == static_cast<unsigned>(date.day())) return row.color;
  }
  return std::nullopt;
}

}  // namespace agenda
