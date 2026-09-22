// Phase 7: what the settings window writes and the rest of Agenda reads, the interface in two
// languages, and the words a reminder is said with.

#include <catch2/catch_test_macros.hpp>

#include "app/toast.h"
#include "core/config.h"
#include "core/dates.h"
#include "nlp/parser.h"

using namespace agenda;

namespace {

constexpr Date kTuesday{std::chrono::year{2026}, std::chrono::September, std::chrono::day{22}};

// Sets the language for one test and puts Spanish back however it ends.
struct InEnglish {
  InEnglish() { CurrentLang() = Lang::En; }
  ~InEnglish() { CurrentLang() = Lang::Es; }
};

Reminder DentistAt(Date day, int start, int end, int before) {
  Reminder reminder;
  reminder.title = L"Dentista";
  reminder.day = day;
  reminder.startMin = start;
  reminder.endMin = end;
  reminder.minutesBefore = before;
  reminder.at = WallMinute(day, start) - before;
  return reminder;
}

}  // namespace

TEST_CASE("the preferences come out of the config, and a bad value keeps the default") {
  const Preferences none = ReadPreferences(nlohmann::json::object());
  CHECK(none.hotkey == "Alt+Shift+C");
  CHECK(none.durationMin == 60);
  CHECK(none.lang == Lang::Es);
  CHECK(none.theme.empty());

  const Preferences set = ReadPreferences(nlohmann::json{{"hotkey", "Ctrl+Alt+Space"},
                                                          {"defaultDuration", 30},
                                                          {"language", "en"},
                                                          {"theme", "light"}});
  CHECK(set.hotkey == "Ctrl+Alt+Space");
  CHECK(set.durationMin == 30);
  CHECK(set.lang == Lang::En);
  CHECK(set.theme == L"light");

  // Hand-edited nonsense: a length the window does not offer, a theme that does not exist, the
  // wrong type. None of it throws and none of it sticks.
  const Preferences odd = ReadPreferences(nlohmann::json{
      {"hotkey", 5}, {"defaultDuration", 37}, {"language", "fr"}, {"theme", "sepia"}});
  CHECK(odd.hotkey == "Alt+Shift+C");
  CHECK(odd.durationMin == 60);
  CHECK(odd.lang == Lang::Es);
  CHECK(odd.theme.empty());
}

TEST_CASE("an event with no length lasts what the settings say") {
  const nlp::ParsedInput parsed =
      nlp::ParseInput(L"mañana 5pm dentista", nlp::Now{kTuesday, 10 * 60}, 30);
  CHECK(parsed.durationMin == 30);
  REQUIRE(parsed.end);
  CHECK(parsed.end->minuteOfDay == 17 * 60 + 30);
  // A length that was written still wins over the setting.
  const nlp::ParsedInput written =
      nlp::ParseInput(L"mañana 5pm dentista por 2h", nlp::Now{kTuesday, 10 * 60}, 30);
  CHECK(written.durationMin == 120);
}

TEST_CASE("the interface speaks English when asked, and the parser still understands both") {
  InEnglish english;
  CHECK(MonthName(std::chrono::September) == L"September");
  CHECK(WeekdayInitial(0) == L"M");
  CHECK(WeekdayName(6) == L"Sunday");
  const nlp::ParsedInput spanish =
      nlp::ParseInput(L"mañana 5pm dentista", nlp::Now{kTuesday, 10 * 60});
  CHECK(nlp::PreviewText(spanish, kTuesday) == L"\U0001F4C5 Tomorrow · 17:00–18:00 · Dentista");
  const nlp::ParsedInput task = nlp::ParseInput(L"buy milk", nlp::Now{kTuesday, 10 * 60});
  CHECK(nlp::PreviewText(task, kTuesday) == L"☑ Task, no date · Buy milk");
}

TEST_CASE("a reminder says when, where and how soon") {
  const long long now = WallMinute(kTuesday, 16 * 60 + 50);
  Reminder reminder = DentistAt(kTuesday, 17 * 60, 18 * 60, 10);
  CHECK(ReminderWhen(reminder, now) == L"17:00 – 18:00");
  CHECK(ReminderSoon(reminder, now) == L"En 10 min");

  reminder.location = L"Calle 10";
  CHECK(ReminderWhen(reminder, now) == L"17:00 – 18:00 · Calle 10");

  // The day before, at nine: it says which day, and how long is left in hours.
  const Reminder tomorrow = DentistAt(AddDays(kTuesday, 1), 9 * 60, 10 * 60, 60 * 12);
  const long long evening = WallMinute(kTuesday, 21 * 60);
  CHECK(ReminderWhen(tomorrow, evening) == L"09:00 – 10:00 · mañana");
  CHECK(ReminderSoon(tomorrow, evening) == L"En 12 h");

  Reminder late = reminder;
  CHECK(ReminderSoon(late, WallMinute(kTuesday, 17 * 60 + 2)) == L"Empieza ahora");

  InEnglish english;
  CHECK(ReminderSoon(DentistAt(kTuesday, 17 * 60, 18 * 60, 90),
                     WallMinute(kTuesday, 15 * 60 + 30)) == L"In 1 h 30 min");
}

TEST_CASE("a wall minute and its day go back and forth") {
  const long long wall = WallMinute(kTuesday, 23 * 60 + 59);
  CHECK(DayOfWall(wall) == kTuesday);
  CHECK(DayOfWall(wall + 1) == AddDays(kTuesday, 1));
  // Before 1970 still rounds towards the day it is in, not towards zero.
  const Date old{std::chrono::year{1969}, std::chrono::December, std::chrono::day{31}};
  CHECK(DayOfWall(WallMinute(old, 30)) == old);
}
