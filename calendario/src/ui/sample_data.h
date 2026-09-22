#pragma once

// The made-up day the offscreen snapshot paints from.
//
// It stays after phase 4 on purpose. `--render-snapshot` is how the design is judged, and a
// PNG that came from the user's own database would change every time somebody writes
// something down instead of when the design changes -- which is the whole reason the snapshot
// also pins the day and the hour. The live window reads SQLite; this fills the same model by
// hand so both go through exactly one DrawPopup.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/dates.h"
#include "data/model.h"
#include "ui/popup_view.h"

namespace agenda {
namespace detail {

// Keyed by day of the month rather than by a fixed date, so every month the user browses to
// looks lived in instead of empty.
struct SampleRow {
  unsigned day;
  bool isTask;
  int startMin;  // -1 for a task with no time
  int endMin;
  bool done;
  const wchar_t* title;
  std::uint32_t color;
};

inline constexpr std::uint32_t kSampleEventColor = 0x4A8BF5;
inline constexpr std::uint32_t kSampleTaskColor = 0xF5A623;

inline constexpr SampleRow kSampleRows[] = {
    {3, false, 9 * 60, 10 * 60, false, L"Revisión de diseño", kSampleEventColor},
    {8, false, 16 * 60 + 30, 17 * 60 + 30, false, L"Dentista", kSampleTaskColor},
    {12, false, 8 * 60, 8 * 60 + 30, false, L"Standup del equipo", kSampleEventColor},
    {12, true, -1, -1, false, L"Almuerzo con Ana", kSampleTaskColor},
    {15, false, 11 * 60, 12 * 60, false, L"Llamada con el banco", kSampleEventColor},
    // The day the snapshot opens on carries one of each, and one already ticked, because the
    // checkbox and the line through a finished task are things that have to be looked at.
    {22, false, 9 * 60 + 30, 10 * 60 + 30, false, L"Reunión de equipo", kSampleEventColor},
    {22, true, -1, -1, false, L"Llamar al fontanero", kSampleTaskColor},
    {22, true, -1, -1, true, L"Enviar el informe", kSampleTaskColor},
    {27, false, 7 * 60, 8 * 60, false, L"Gimnasio", kSampleTaskColor},
    {27, false, 10 * 60, 11 * 60, false, L"Entrega del informe", kSampleEventColor},
    {27, false, 18 * 60, 19 * 60, false, L"Cumpleaños de Sofía", kSampleTaskColor},
};

inline DayItem ToItem(const SampleRow& row, unsigned index) {
  DayItem item;
  item.uid = L"sample-" + std::to_wstring(index);
  item.isTask = row.isTask;
  if (row.startMin >= 0) item.startMin = row.startMin;
  if (row.endMin >= 0) item.endMin = row.endMin;
  item.title = row.title;
  item.color = row.color;
  item.done = row.done;
  return item;
}

}  // namespace detail

// Fills the day list and the month dots the snapshot draws, for whatever day and month the
// model is already sitting on.
inline void FillSampleData(PopupModel& model) {
  model.day.clear();
  model.dots.clear();

  unsigned index = 0;
  for (const detail::SampleRow& row : detail::kSampleRows) {
    if (row.day == static_cast<unsigned>(model.selected.day())) {
      model.day.push_back(detail::ToItem(row, index));
    }
    ++index;
  }
  // Sorted the same way the store sorts, so the PNG shows the order the app will show.
  std::sort(model.day.begin(), model.day.end(), EarlierThan);

  const Date first = GridStart(model.month);
  for (int cell = 0; cell < kGridCells; ++cell) {
    const Date date = AddDays(first, cell);
    for (const detail::SampleRow& row : detail::kSampleRows) {
      if (row.day != static_cast<unsigned>(date.day())) continue;
      model.dots.push_back(DayDot{date, row.color});
      break;  // one dot per day, and it belongs to whatever comes first
    }
  }
}

}  // namespace agenda
