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
#include "ui/app_view.h"
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

// --- The expanded app -------------------------------------------------------------------
//
// The popup's rows alone make a thin week: the app needs overlaps, all-day chips, a task with an
// hour, a repetition and a tray, because those are the things its design has to be judged on.
// They are dated, not keyed by day of the month, and only the app's snapshots read them, so the
// popup's PNGs stay exactly what they were.
namespace detail {

struct SampleWeekRow {
  unsigned day;  // of September 2026, the week of the 21st
  bool isTask;
  int startMin;  // -1: all day, or a task with no hour
  int endMin;
  const wchar_t* title;
  std::uint32_t color;
};

inline constexpr std::uint32_t kSampleWorkColor = 0x9B7BF0;

inline constexpr SampleWeekRow kSampleWeekRows[] = {
    {21, false, 9 * 60, 10 * 60, L"Planificación semanal", kSampleWorkColor},
    {21, false, 13 * 60, 14 * 60, L"Almuerzo con Diego", kSampleTaskColor},
    {22, false, 10 * 60, 11 * 60, L"Café con Ana", kSampleTaskColor},
    {22, false, 14 * 60, 15 * 60 + 30, L"Revisión de código", kSampleWorkColor},
    {22, false, 14 * 60 + 30, 15 * 60, L"Llamada con Marta", kSampleEventColor},
    {22, false, 17 * 60, 18 * 60, L"Clase de guitarra", kSampleEventColor},
    {23, false, 11 * 60, 12 * 60, L"Entrevista", kSampleWorkColor},
    {23, true, 18 * 60, -1, L"Pagar la luz", kSampleTaskColor},
    {24, false, -1, -1, L"Viaje a Medellín", kSampleEventColor},
    {24, false, 7 * 60 + 30, 9 * 60, L"Vuelo AV 9322", kSampleEventColor},
    {25, false, -1, -1, L"Viaje a Medellín", kSampleEventColor},
    {25, false, 16 * 60, 17 * 60, L"Demo del sprint", kSampleWorkColor},
    {26, false, 9 * 60, 10 * 60 + 30, L"Mercado", kSampleTaskColor},
};

inline std::vector<DayItem> SampleAppDay(Date date) {
  std::vector<DayItem> items;
  unsigned index = 0;
  for (const SampleRow& row : kSampleRows) {
    if (row.day == static_cast<unsigned>(date.day())) items.push_back(ToItem(row, index));
    ++index;
  }
  const bool september = date.year() == std::chrono::year{2026} &&
                         date.month() == std::chrono::September;
  // Numbered by their place in the table, so a snapshot can point at one: "week-2" is always
  // the coffee with Ana.
  for (size_t k = 0; k < std::size(kSampleWeekRows); ++k) {
    const SampleWeekRow& row = kSampleWeekRows[k];
    if (!september || row.day != static_cast<unsigned>(date.day())) continue;
    DayItem item;
    item.uid = L"week-" + std::to_wstring(k);
    item.isTask = row.isTask;
    if (row.startMin >= 0) item.startMin = row.startMin;
    if (row.endMin >= 0) item.endMin = row.endMin;
    item.title = row.title;
    item.color = row.color;
    items.push_back(item);
  }
  // Every Monday at seven, from the 7th of September: the repetition CLAUDE.md waited on the
  // week view for.
  if (MondayIndex(std::chrono::weekday{std::chrono::sys_days{date}}) == 0 &&
      date >= Date{std::chrono::year{2026}, std::chrono::September, std::chrono::day{7}}) {
    DayItem gym;
    gym.uid = L"gym";
    gym.startMin = 7 * 60;
    gym.endMin = 8 * 60;
    gym.title = L"Gimnasio";
    gym.color = kSampleTaskColor;
    gym.repeats = true;
    items.push_back(gym);
  }
  std::sort(items.begin(), items.end(), EarlierThan);
  return items;
}

}  // namespace detail

// The sidebar's calendars and the settings window's chooser, the same five everywhere.
inline std::vector<CalendarInfo> SampleCalendars() {
  return {
      CalendarInfo{"personal", L"Personal", true, false, detail::kSampleEventColor, false},
      CalendarInfo{"familia", L"Familia", false, false, detail::kSampleTaskColor, false},
      CalendarInfo{"trabajo", L"Trabajo", false, false, detail::kSampleWorkColor, false},
      CalendarInfo{"cumples", L"Cumpleaños", false, false, 0x34C38F, true},
      CalendarInfo{"tareas", L"Tareas", true, true, detail::kSampleTaskColor, false},
  };
}

// Fills both models for the app's snapshots: the mini month's dots, the days on screen, the
// sidebar's calendars (one of them switched off) and the tray.
inline void FillSampleApp(PopupModel& model, AppModel& app) {
  FillSampleData(model);
  model.dots.clear();
  const Date gridFirst = GridStart(model.month);
  for (int cell = 0; cell < kGridCells; ++cell) {
    const Date date = AddDays(gridFirst, cell);
    const std::vector<DayItem> items = detail::SampleAppDay(date);
    if (!items.empty()) model.dots.push_back(DayDot{date, items.front().color});
  }

  app.first = FirstShown(app.view, model.selected);
  app.days.clear();
  for (int i = 0; i < ShownDays(app.view); ++i) {
    app.days.push_back(detail::SampleAppDay(AddDays(app.first, i)));
  }

  app.calendars = SampleCalendars();
  app.calendarHover.assign(app.calendars.size(), 0.0f);

  const auto task = [](const wchar_t* uid, const wchar_t* title, bool done) {
    DayItem item;
    item.uid = uid;
    item.isTask = true;
    item.title = title;
    item.color = detail::kSampleTaskColor;
    item.done = done;
    return item;
  };
  app.undated = {task(L"u1", L"Comprar leche", false),
                 task(L"u2", L"Renovar el pasaporte", false),
                 task(L"u3", L"Devolver el libro a Sofía", false),
                 task(L"u4", L"Llamar a la abuela", true)};
}

}  // namespace agenda
