#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

#include "data/db.h"
#include "data/model.h"
#include "data/schema.h"
#include "data/store.h"

using namespace agenda;

namespace {

Date Day(int year, unsigned month, unsigned day) {
  return Date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
}

// Every test writes through the queue, so every test has to wait for it. Wrapped up here so a
// forgotten Drain() cannot turn into a test that passes by accident.
struct Open {
  Store store;
  Open() { REQUIRE(store.OpenMemory()); }
  Store* operator->() { return &store; }
  void settle() { store.Drain(); }
};

Draft EventAt(std::wstring title, Date day, int startMin, int endMin) {
  Draft draft;
  draft.isTask = false;
  draft.title = std::move(title);
  draft.day = day;
  draft.startMin = startMin;
  draft.endMin = endMin;
  return draft;
}

int CountRows(Db& db, const char* sql) {
  std::optional<Stmt> stmt = db.Prepare(sql);
  REQUIRE(stmt.has_value());
  REQUIRE(stmt->Step());
  return static_cast<int>(stmt->Int(0));
}

// A file of its own per run, so two runs cannot collide and a crashed one cannot poison the
// next. The user's own agenda lives in %LOCALAPPDATA% and is never opened by a test.
std::filesystem::path ScratchFile() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("agenda-test-" + std::to_string(stamp) + ".db");
}

void Erase(const std::filesystem::path& file) {
  std::error_code ignored;
  for (const char* suffix : {"", "-wal", "-shm"}) {
    std::filesystem::remove(file.wstring() + ToWide(suffix), ignored);
  }
}

}  // namespace

TEST_CASE("a day key survives the round trip") {
  CHECK(DayKey(Day(2026, 9, 23)) == "2026-09-23");
  CHECK(DayKey(Day(2026, 12, 1)) == "2026-12-01");
  CHECK(ParseDayKey("2026-09-23") == Day(2026, 9, 23));
  CHECK_FALSE(ParseDayKey("2026-9-23").has_value());
  CHECK_FALSE(ParseDayKey("2026-02-30").has_value());
  CHECK_FALSE(ParseDayKey("").has_value());
}

TEST_CASE("migrating stamps the version and is idempotent") {
  Db db;
  REQUIRE(db.OpenMemory());
  REQUIRE(db.UserVersion() == 0);

  REQUIRE(Migrate(db));
  CHECK(db.UserVersion() == kSchemaVersion);

  // Running it again on a database already up to date does nothing and is not an error.
  REQUIRE(Migrate(db));
  CHECK(db.UserVersion() == kSchemaVersion);

  // v1 seeds somewhere for an event to hang from before there is a Google account.
  CHECK(CountRows(db, "SELECT COUNT(*) FROM calendars") == 2);
  CHECK(CountRows(db, "SELECT COUNT(*) FROM sync_state") == 2);
}

TEST_CASE("a cache from a newer build is refused instead of converted") {
  Db db;
  REQUIRE(db.OpenMemory());
  REQUIRE(db.SetUserVersion(kSchemaVersion + 1));

  CHECK_FALSE(Migrate(db));
  // And it is left exactly as it was found: converting it backwards blind would lose whatever
  // the newer build wrote.
  CHECK(db.UserVersion() == kSchemaVersion + 1);
}

TEST_CASE("an event created shows up in its day and puts a dot on it") {
  Open store;
  const Date day = Day(2026, 9, 23);

  const DayItem shown = store->Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60));
  CHECK_FALSE(shown.uid.empty());
  CHECK(shown.title == L"Dentista");
  CHECK(shown.startMin == 17 * 60);
  CHECK(shown.color != 0);  // the card has to paint right on the first frame
  store.settle();

  const std::vector<DayItem> items = store->ItemsForDay(day, false);
  REQUIRE(items.size() == 1);
  CHECK(items[0].uid == shown.uid);
  CHECK(items[0].title == L"Dentista");
  CHECK(items[0].startMin == 17 * 60);
  CHECK(items[0].endMin == 18 * 60);
  CHECK_FALSE(items[0].isTask);

  CHECK(store->ItemsForDay(Day(2026, 9, 22), false).empty());

  const std::vector<DayDot> dots = store->DotsForRange(Day(2026, 9, 1), Day(2026, 9, 30));
  REQUIRE(dots.size() == 1);
  CHECK(dots[0].date == day);
  CHECK(dots[0].color == shown.color);
}

TEST_CASE("an event over two days appears on both, with a clock only on the first") {
  Open store;
  Draft draft = EventAt(L"Viaje", Day(2026, 9, 23), 9 * 60, 18 * 60);
  draft.endDay = Day(2026, 9, 25);
  store->Create(draft);
  store.settle();

  const std::vector<DayItem> first = store->ItemsForDay(Day(2026, 9, 23), false);
  REQUIRE(first.size() == 1);
  CHECK(first[0].startMin == 9 * 60);

  const std::vector<DayItem> middle = store->ItemsForDay(Day(2026, 9, 24), false);
  REQUIRE(middle.size() == 1);
  CHECK_FALSE(middle[0].startMin.has_value());

  REQUIRE(store->ItemsForDay(Day(2026, 9, 25), false).size() == 1);
  CHECK(store->ItemsForDay(Day(2026, 9, 26), false).empty());

  CHECK(store->DotsForRange(Day(2026, 9, 1), Day(2026, 9, 30)).size() == 3);
}

TEST_CASE("the day reads as a shape: all day, then the clock, then what has no hour") {
  Open store;
  const Date day = Day(2026, 9, 23);

  store->Create(EventAt(L"Tarde", day, 15 * 60, 16 * 60));
  store->Create(EventAt(L"Manana", day, 9 * 60, 10 * 60));
  Draft allDay = EventAt(L"Festivo", day, 0, 0);
  allDay.startMin.reset();
  allDay.endMin.reset();
  store->Create(allDay);
  Draft chore;
  chore.isTask = true;
  chore.title = L"Sacar la basura";
  chore.day = day;
  store->Create(chore);
  store.settle();

  const std::vector<DayItem> items = store->ItemsForDay(day, false);
  REQUIRE(items.size() == 4);
  CHECK(items[0].title == L"Festivo");
  CHECK(items[1].title == L"Manana");
  CHECK(items[2].title == L"Tarde");
  // Last, and that is the point: the popup shows two cards, so a to-do with no hour must not
  // be able to push the day's meetings off the panel.
  CHECK(items[3].title == L"Sacar la basura");
}

TEST_CASE("a task carries its check and keeps its place when it is ticked") {
  Open store;
  const Date day = Day(2026, 9, 23);

  Draft draft;
  draft.isTask = true;
  draft.title = L"Pagar luz";
  draft.day = day;
  const DayItem created = store->Create(draft);
  store.settle();

  std::vector<DayItem> items = store->ItemsForDay(day, false);
  REQUIRE(items.size() == 1);
  CHECK(items[0].isTask);
  CHECK_FALSE(items[0].done);
  CHECK_FALSE(items[0].startMin.has_value());

  store->SetDone(created.uid, true);
  store.settle();

  items = store->ItemsForDay(day, false);
  // Ticked, not gone: the text is struck through, which only works if the row is still there.
  REQUIRE(items.size() == 1);
  CHECK(items[0].done);

  store->SetDone(created.uid, false);
  store.settle();
  CHECK_FALSE(store->ItemsForDay(day, false)[0].done);
}

TEST_CASE("a task with no date at all lands on today instead of vanishing") {
  Open store;
  Draft draft;
  draft.isTask = true;
  draft.title = L"Comprar leche";
  store->Create(draft);
  store.settle();

  const Date today = Day(2026, 9, 23);
  CHECK(store->ItemsForDay(today, false).empty());
  REQUIRE(store->ItemsForDay(today, true).size() == 1);
  CHECK(store->ItemsForDay(today, true)[0].title == L"Comprar leche");

  // It has no date, so it earns no dot on any day.
  CHECK(store->DotsForRange(Day(2026, 9, 1), Day(2026, 9, 30)).empty());
}

TEST_CASE("creating queues one operation and undoing takes both away") {
  Open store;
  const DayItem created =
      store->Create(EventAt(L"Dentista", Day(2026, 9, 23), 17 * 60, 18 * 60));
  store.settle();

  CHECK(store->ItemsForDay(Day(2026, 9, 23), false).size() == 1);
  // The row and its operation were written together, which is the whole point of doing both
  // inside one transaction.
  CHECK(store->PendingOpCount() == 1);

  store->Remove(created.uid, /*isTask=*/false);
  store.settle();

  CHECK(store->ItemsForDay(Day(2026, 9, 23), false).empty());
  CHECK(store->DotsForRange(Day(2026, 9, 1), Day(2026, 9, 30)).empty());
  // Nothing has been sent yet, so undoing leaves no tombstone and no orphan operation.
  CHECK(store->PendingOpCount() == 0);
  CHECK(store->TakeFailures().empty());
}

TEST_CASE("a repeating event shows on every day its rule lands on, and puts dots there") {
  Open store;
  Draft draft = EventAt(L"Gym", Day(2026, 9, 28), 7 * 60, 8 * 60);
  draft.recurrence = L"RRULE:FREQ=WEEKLY;BYDAY=MO";
  store->Create(draft);
  store.settle();

  const std::vector<DayItem> items = store->ItemsForDay(Day(2026, 9, 28), false);
  REQUIRE(items.size() == 1);
  CHECK(items[0].repeats);

  // Phase 6 unfolds it: the following Monday has it, with its clock, and the Tuesday does not.
  const std::vector<DayItem> next = store->ItemsForDay(Day(2026, 10, 5), false);
  REQUIRE(next.size() == 1);
  CHECK(next[0].uid == items[0].uid);
  CHECK(next[0].startMin == 7 * 60);
  CHECK(next[0].endMin == 8 * 60);
  CHECK(store->ItemsForDay(Day(2026, 10, 6), false).empty());
  // And nothing before the series begins.
  CHECK(store->ItemsForDay(Day(2026, 9, 21), false).empty());

  const std::vector<DayDot> dots = store->DotsForRange(Day(2026, 9, 28), Day(2026, 10, 18));
  CHECK(dots.size() == 3);
}

// What schema v4 added, taken away again, to build a cache the way an older build left it.
constexpr const char* kUndoV4 =
    "DROP INDEX events_by_series; "
    "ALTER TABLE events DROP COLUMN series_id; "
    "ALTER TABLE events DROP COLUMN original_day;";

TEST_CASE("v1 caches migrate to v2 and keep what they had") {
  const std::filesystem::path file = ScratchFile();
  Erase(file);
  std::wstring uid;
  {
    Store store;
    REQUIRE(store.Open(file));
    uid = store.Create(EventAt(L"Dentista", Day(2026, 9, 23), 17 * 60, 18 * 60)).uid;
    store.Drain();
  }
  {
    // Put the file back the way a phase 5 build left it: the v2, v3 and v4 columns gone, the
    // stamp at 1.
    Db db;
    REQUIRE(db.Open(file));
    REQUIRE(db.Exec(kUndoV4));
    REQUIRE(db.Exec("ALTER TABLE events DROP COLUMN reminders; "
                    "ALTER TABLE calendars DROP COLUMN reminders; "
                    "ALTER TABLE events DROP COLUMN location; "
                    "ALTER TABLE events DROP COLUMN moved_from; "
                    "ALTER TABLE calendars DROP COLUMN hidden;"));
    REQUIRE(db.SetUserVersion(1));
  }
  {
    Store store;
    REQUIRE(store.Open(file));
    CHECK(store.db().UserVersion() == kSchemaVersion);
    const std::vector<DayItem> items = store.ItemsForDay(Day(2026, 9, 23), false);
    REQUIRE(items.size() == 1);
    CHECK(items[0].uid == uid);
    CHECK(CountRows(store.db(), "SELECT COUNT(*) FROM events WHERE location = ''") == 1);
    CHECK(CountRows(store.db(), "SELECT COUNT(*) FROM calendars WHERE hidden = 0") == 2);
  }
  Erase(file);
}

TEST_CASE("v2 caches migrate to v3, learn the reminders and download everything once more") {
  const std::filesystem::path file = ScratchFile();
  Erase(file);
  {
    Store store;
    REQUIRE(store.Open(file));
    store.Create(EventAt(L"Dentista", Day(2026, 9, 23), 17 * 60, 18 * 60));
    store.Drain();
  }
  {
    // A phase 6 cache: no reminder columns, and a sync token the next pass would carry on from.
    Db db;
    REQUIRE(db.Open(file));
    REQUIRE(db.Exec(kUndoV4));
    REQUIRE(db.Exec("ALTER TABLE events DROP COLUMN reminders; "
                    "ALTER TABLE calendars DROP COLUMN reminders; "
                    "UPDATE sync_state SET sync_token = 'CPDAlvWDx70CEPDAlvWDx70CGAU=';"));
    REQUIRE(db.SetUserVersion(2));
  }
  {
    Store store;
    REQUIRE(store.Open(file));
    CHECK(store.db().UserVersion() == kSchemaVersion);
    // The event keeps no list of its own: it goes by its calendar's, and the local one says ten.
    CHECK(CountRows(store.db(), "SELECT COUNT(*) FROM events WHERE reminders IS NULL") == 1);
    CHECK(CountRows(store.db(),
                    "SELECT COUNT(*) FROM calendars WHERE id = 'local' AND reminders = '10'") == 1);
    CHECK(CountRows(store.db(), "SELECT COUNT(*) FROM sync_state WHERE sync_token != ''") == 0);
  }
  Erase(file);
}

TEST_CASE("v3 caches migrate to v4 and download the exceptions once more") {
  const std::filesystem::path file = ScratchFile();
  Erase(file);
  {
    Store store;
    REQUIRE(store.Open(file));
    store.Create(EventAt(L"Dentista", Day(2026, 9, 23), 17 * 60, 18 * 60));
    store.Drain();
  }
  {
    // A 1.0.0 cache: no series columns, and a sync token that would skip what is already there.
    Db db;
    REQUIRE(db.Open(file));
    REQUIRE(db.Exec(kUndoV4));
    REQUIRE(db.Exec("UPDATE sync_state SET sync_token = 'CPDAlvWDx70CEPDAlvWDx70CGAU=';"));
    REQUIRE(db.SetUserVersion(3));
  }
  {
    Store store;
    REQUIRE(store.Open(file));
    CHECK(store.db().UserVersion() == kSchemaVersion);
    CHECK(CountRows(store.db(), "SELECT COUNT(*) FROM events WHERE series_id IS NULL") == 1);
    CHECK(CountRows(store.db(), "SELECT COUNT(*) FROM sync_state WHERE sync_token != ''") == 0);
    CHECK(store.ItemsForDay(Day(2026, 9, 23), false).size() == 1);
  }
  Erase(file);
}

TEST_CASE("an occurrence Google moved or cancelled leaves its day in the series") {
  Open store;
  // Every Monday from the 28th, gone up to Google as 'serie1'.
  Draft draft = EventAt(L"Gym", Day(2026, 9, 28), 7 * 60, 8 * 60);
  draft.recurrence = L"FREQ=WEEKLY;BYDAY=MO";
  const std::wstring uid = store->Create(draft).uid;
  store.settle();
  REQUIRE(store->db().Exec("UPDATE events SET remote_id = 'serie1'"));
  // Google keeps two occurrences apart: the 5th moved to Tuesday the 6th, the 12th cancelled.
  REQUIRE(store->db().Exec(
      "INSERT INTO events (uid, calendar_id, title, start_day, start_min, end_day, end_min, "
      "                    remote_id, updated_at, series_id, original_day) "
      "VALUES ('movida', 'local', 'Gym', '2026-10-06', 420, '2026-10-06', 480, "
      "        'serie1_20261005', 1, 'serie1', '2026-10-05');"
      "INSERT INTO events (uid, calendar_id, title, start_day, end_day, remote_id, updated_at, "
      "                    deleted_at, series_id, original_day) "
      "VALUES ('cancelada', 'local', '', '2026-10-12', '2026-10-12', 'serie1_20261012', 1, 1, "
      "        'serie1', '2026-10-12');"));

  CHECK(store->ItemsForDay(Day(2026, 10, 5), false).empty());
  const std::vector<DayItem> tuesday = store->ItemsForDay(Day(2026, 10, 6), false);
  REQUIRE(tuesday.size() == 1);
  CHECK(tuesday[0].uid == L"movida");
  CHECK(store->ItemsForDay(Day(2026, 10, 12), false).empty());
  CHECK(store->ItemsForDay(Day(2026, 10, 19), false).size() == 1);

  // Dots: the 28th, the 6th instead of the 5th, nothing on the 12th, the 19th.
  const std::vector<DayDot> dots = store->DotsForRange(Day(2026, 9, 28), Day(2026, 10, 19));
  REQUIRE(dots.size() == 3);
  const auto has = [&](Date date) {
    return std::any_of(dots.begin(), dots.end(),
                       [&](const DayDot& dot) { return dot.date == date; });
  };
  CHECK(has(Day(2026, 10, 6)));
  CHECK(has(Day(2026, 10, 19)));
  CHECK_FALSE(has(Day(2026, 10, 5)));

  // The reminder of the cancelled Monday does not ring; the local calendar reminds at 6:50.
  const long long due = WallMinute(Day(2026, 10, 12), 6 * 60 + 50);
  CHECK(store->DueReminders(due - 1, due).empty());

  // And deleting the series takes the occurrences Google kept apart with it.
  store->Remove(uid, false);
  store.settle();
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM events WHERE series_id = 'serie1'") == 0);
}

TEST_CASE("solo este: one occurrence moved or deleted here leaves the rest of the series") {
  Open store;
  Draft draft = EventAt(L"Gym", Day(2026, 9, 28), 7 * 60, 8 * 60);
  draft.recurrence = L"FREQ=WEEKLY;BYDAY=MO";
  const std::wstring series = store->Create(draft).uid;
  store.settle();
  // A card of the series knows which Monday it stands for.
  const std::vector<DayItem> monday = store->ItemsForDay(Day(2026, 10, 5), false);
  REQUIRE(monday.size() == 1);
  CHECK(monday[0].occurrence == Day(2026, 10, 5));

  // The 5th moves to Tuesday at nine, and gets a title of its own.
  EventDetail edit = *store->Event(series);
  edit.title = L"Gym (movido)";
  edit.startDay = edit.endDay = Day(2026, 10, 6);
  edit.startMin = 9 * 60;
  edit.endMin = 10 * 60;
  const std::wstring moved = store->DetachOccurrence(series, Day(2026, 10, 5), edit, 0);
  // The 12th goes.
  store->RemoveOccurrence(series, Day(2026, 10, 12));
  store.settle();

  CHECK(store->ItemsForDay(Day(2026, 10, 5), false).empty());
  const std::vector<DayItem> tuesday = store->ItemsForDay(Day(2026, 10, 6), false);
  REQUIRE(tuesday.size() == 1);
  CHECK(tuesday[0].uid == moved);
  CHECK(tuesday[0].title == L"Gym (movido)");
  CHECK_FALSE(tuesday[0].repeats);
  CHECK(store->ItemsForDay(Day(2026, 10, 12), false).empty());
  REQUIRE(store->ItemsForDay(Day(2026, 10, 19), false).size() == 1);
  CHECK(store->ItemsForDay(Day(2026, 10, 19), false)[0].uid == series);
  // The series itself is untouched.
  CHECK(store->Event(series)->title == L"Gym");
  CHECK(store->Event(series)->startDay == Day(2026, 9, 28));

  // What goes up: a PATCH for the moved one and a DELETE for the other, never a creation.
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM pending_ops WHERE op = 'update'") == 1);
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM pending_ops WHERE op = 'delete'") == 1);

  // Deleting the moved one keeps its day away from the series: it becomes a tombstone.
  store->Remove(moved, false);
  store.settle();
  CHECK(store->ItemsForDay(Day(2026, 10, 5), false).empty());
  CHECK(store->ItemsForDay(Day(2026, 10, 6), false).empty());
}

TEST_CASE("a reminder falls due once, at its minute, and not before or after") {
  Open store;
  const Date day = Day(2026, 9, 23);
  store->Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60));
  store.settle();

  // The local calendar reminds ten minutes before: 16:50.
  const long long due = WallMinute(day, 16 * 60 + 50);
  CHECK(store->DueReminders(due - 20, due - 1).empty());
  const std::vector<Reminder> found = store->DueReminders(due - 1, due);
  REQUIRE(found.size() == 1);
  CHECK(found[0].title == L"Dentista");
  CHECK(found[0].minutesBefore == 10);
  CHECK(found[0].at == due);
  CHECK(found[0].day == day);
  // The window is open at the start: a check that already covered the minute does not repeat it.
  CHECK(store->DueReminders(due, due + 20).empty());
}

TEST_CASE("an event written after all its reminders went by says so once, as it starts") {
  Open store;
  const Date day = Day(2026, 9, 23);
  store->Create(EventAt(L"Prueba", day, 16 * 60 + 5, 17 * 60));
  store.settle();
  // Written at 16:00 for 16:05, with the local calendar's ten minutes: 15:55 had gone already.
  const auto writtenAt = [&](const char* local) {
    store->Run([&] {
      const std::string sql = std::string("UPDATE events SET updated_at = "
                                          "CAST(strftime('%s', '2026-09-23 ") + local + "', 'utc') AS INTEGER)";
      store->db().Exec(sql.c_str());
    });
    store.settle();
  };
  writtenAt("16:00");
  // The app checks from the minute it last looked, which is after the event was written.
  std::vector<Reminder> found = store->DueReminders(WallMinute(day, 16 * 60), WallMinute(day, 17 * 60));
  REQUIRE(found.size() == 1);
  CHECK(found[0].minutesBefore == 0);
  CHECK(found[0].at == WallMinute(day, 16 * 60 + 5));

  // Written in time, the ten minutes are enough, and there is nothing extra at the start.
  writtenAt("15:00");
  found = store->DueReminders(WallMinute(day, 15 * 60), WallMinute(day, 17 * 60));
  REQUIRE(found.size() == 1);
  CHECK(found[0].minutesBefore == 10);
}

TEST_CASE("an event's own reminders win over its calendar's, and none means none") {
  Open store;
  const Date day = Day(2026, 9, 23);
  const DayItem first = store->Create(EventAt(L"Vuelo", day, 9 * 60, 11 * 60));
  const DayItem second = store->Create(EventAt(L"Almuerzo", day, 13 * 60, 14 * 60));
  store.settle();
  store->Run([&] {
    store->db().Exec("UPDATE events SET reminders = '60,1440' WHERE title = 'Vuelo'");
    store->db().Exec("UPDATE events SET reminders = '' WHERE title = 'Almuerzo'");
  });

  const std::vector<Reminder> found =
      store->DueReminders(WallMinute(AddDays(day, -2), 0), WallMinute(AddDays(day, 1), 0));
  REQUIRE(found.size() == 2);
  // In the order they fall due: the day before at nine, then an hour before.
  CHECK(found[0].uid == first.uid);
  CHECK(found[0].minutesBefore == 1440);
  CHECK(found[0].at == WallMinute(AddDays(day, -1), 9 * 60));
  CHECK(found[1].minutesBefore == 60);
  CHECK(found[1].at == WallMinute(day, 8 * 60));
  (void)second;
}

TEST_CASE("a repetition reminds on the day it lands on, not on the day it began") {
  Open store;
  Draft draft = EventAt(L"Gimnasio", Day(2026, 9, 21), 7 * 60, 8 * 60);
  draft.recurrence = L"FREQ=WEEKLY;BYDAY=MO";
  store->Create(draft);
  store.settle();

  const Date monday = Day(2026, 10, 5);
  const std::vector<Reminder> found =
      store->DueReminders(WallMinute(monday, 0), WallMinute(monday, 12 * 60));
  REQUIRE(found.size() == 1);
  CHECK(found[0].day == monday);
  CHECK(found[0].at == WallMinute(monday, 6 * 60 + 50));
  // And nothing on the Tuesday after it.
  CHECK(store->DueReminders(WallMinute(AddDays(monday, 1), 0),
                            WallMinute(AddDays(monday, 1), 23 * 60))
            .empty());
}

TEST_CASE("a calendar switched off in the sidebar does not remind either") {
  Open store;
  const Date day = Day(2026, 9, 23);
  store->Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60));
  store.settle();
  store->SetCalendarHidden(kLocalCalendarId, true);
  store.settle();
  CHECK(store->DueReminders(WallMinute(day, 0), WallMinute(day, 23 * 60)).empty());
}

TEST_CASE("switching a calendar off hides its days without touching what Google lists") {
  Open store;
  const Date day = Day(2026, 9, 23);
  const DayItem shown = store->Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60));
  store.settle();

  store->SetCalendarHidden(kLocalCalendarId, true);
  store.settle();
  CHECK(store->ItemsForDay(day, false).empty());
  CHECK(store->DotsForRange(Day(2026, 9, 1), Day(2026, 9, 30)).empty());

  // Still listed, with its switch off, so the sidebar can offer to turn it back on.
  bool found = false;
  for (const CalendarInfo& calendar : store->AllCalendars()) {
    if (calendar.id != kLocalCalendarId) continue;
    found = true;
    CHECK(calendar.hidden);
    CHECK_FALSE(calendar.isTaskList);
    CHECK(calendar.color == 0x4A8BF5);
  }
  CHECK(found);

  // What the pull writes on every pass is `visible`, and that is not the switch.
  REQUIRE(store->db().RunOnce("UPDATE calendars SET visible = 1"));
  CHECK(store->ItemsForDay(day, false).empty());

  store->SetCalendarHidden(kLocalCalendarId, false);
  store.settle();
  REQUIRE(store->ItemsForDay(day, false).size() == 1);
  CHECK(store->ItemsForDay(day, false)[0].uid == shown.uid);
}

TEST_CASE("with the default calendar switched off, new things go to one that can be seen") {
  Open store;
  REQUIRE(store->db().RunOnce(
      "INSERT INTO calendars (id, kind, title, color, is_primary) "
      "VALUES ('trabajo', 'calendar', 'Trabajo', 0x7CB342, 0)"));
  CHECK(store->DefaultCalendar(false) == kLocalCalendarId);
  store->SetCalendarHidden(kLocalCalendarId, true);
  store.settle();
  CHECK(store->DefaultCalendar(false) == "trabajo");
}

TEST_CASE("the tray of undated tasks holds only those, the ones to do first") {
  Open store;
  Draft chore;
  chore.isTask = true;
  chore.title = L"Comprar leche";
  const DayItem milk = store->Create(chore);
  chore.title = L"Arreglar la bici";
  store->Create(chore);
  chore.title = L"Con fecha";
  chore.day = Day(2026, 9, 23);
  store->Create(chore);
  store.settle();
  store->SetDone(milk.uid, true);
  store.settle();

  const std::vector<DayItem> tray = store->UndatedTasks();
  REQUIRE(tray.size() == 2);
  CHECK(tray[0].title == L"Arreglar la bici");
  CHECK_FALSE(tray[0].done);
  CHECK(tray[1].title == L"Comprar leche");
  CHECK(tray[1].done);
}

TEST_CASE("what was created is still there after the app is closed and opened again") {
  // The acceptance test of this phase, minus the window: write it, let go of the file the way
  // quitting does, open it again from scratch and look.
  const std::filesystem::path file = ScratchFile();
  Erase(file);
  const Date day = Day(2026, 9, 23);
  std::wstring uid;

  {
    Store store;
    REQUIRE(store.Open(file));
    uid = store.Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60)).uid;
    store.Drain();
  }

  {
    Store store;
    REQUIRE(store.Open(file));
    // Opening an existing cache migrates nothing and seeds nothing twice.
    CHECK(store.PendingOpCount() == 1);

    const std::vector<DayItem> items = store.ItemsForDay(day, false);
    REQUIRE(items.size() == 1);
    CHECK(items[0].uid == uid);
    CHECK(items[0].title == L"Dentista");
    CHECK(items[0].startMin == 17 * 60);

    const std::vector<DayDot> dots = store.DotsForRange(Day(2026, 9, 1), Day(2026, 9, 30));
    REQUIRE(dots.size() == 1);
    CHECK(dots[0].date == day);
  }

  Erase(file);
}

TEST_CASE("what is created lands in the calendar flagged as the default one") {
  Open store;
  const Date day = Day(2026, 9, 23);

  // A Google calendar arrives and takes the flag. The seeded local one keeps existing -- the
  // schema will not let a calendar with rows hanging off it be deleted -- it just stops being
  // where new things go.
  REQUIRE(store->db().RunOnce(
      "INSERT INTO calendars (id, kind, title, color, is_primary) "
      "VALUES ('eli@gmail.com', 'calendar', 'Personal', 0x7CB342, 1)"));
  REQUIRE(store->db().RunOnce("UPDATE calendars SET is_primary = 0 WHERE id = 'local'"));

  const DayItem shown = store->Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60));
  store.settle();

  // The colour on the first frame is already the new calendar's, because Create reads it before
  // it queues anything.
  CHECK(shown.color == 0x7CB342);

  std::optional<Stmt> stmt =
      store->db().Prepare("SELECT calendar_id FROM events WHERE uid = ?");
  REQUIRE(stmt.has_value());
  stmt->Bind(1, shown.uid);
  REQUIRE(stmt->Step());
  CHECK(stmt->Text(0) == "eli@gmail.com");
}

TEST_CASE("undoing something that was never sent takes it away for good") {
  Open store;
  const Date day = Day(2026, 9, 23);

  const DayItem shown = store->Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60));
  store.settle();
  store->Remove(shown.uid, /*isTask=*/false);
  store.settle();

  CHECK(store->ItemsForDay(day, false).empty());
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM events") == 0);
  // Nothing was ever up there, so there is nothing to tell Google about.
  CHECK(store->PendingOpCount() == 0);
}

TEST_CASE("undoing something already at Google leaves a tombstone and a deletion queued") {
  Open store;
  const Date day = Day(2026, 9, 23);

  const DayItem shown = store->Create(EventAt(L"Dentista", day, 17 * 60, 18 * 60));
  store.settle();

  // Stand in for a pass that already pushed it: the row now has an identity at Google.
  std::optional<Stmt> sent =
      store->db().Prepare("UPDATE events SET remote_id = 'abc', etag = '\"1\"' WHERE uid = ?");
  REQUIRE(sent.has_value());
  sent->Bind(1, shown.uid);
  bool ok = false;
  sent->Step(&ok);
  REQUIRE(ok);
  REQUIRE(store->db().RunOnce("DELETE FROM pending_ops"));

  store->Remove(shown.uid, /*isTask=*/false);
  store.settle();

  // Gone from the day, because that is what undo means to whoever pressed it.
  CHECK(store->ItemsForDay(day, false).empty());
  // But still in the table, as a tombstone. Deleting the row here would leave the event on the
  // phone for good and bring it back on the next pass.
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM events WHERE deleted_at IS NOT NULL") == 1);
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM pending_ops WHERE op = 'delete'") == 1);
}

TEST_CASE("a job handed to the writing thread has finished by the time Run returns") {
  Open store;
  // This is how the synchronisation touches SQLite: one connection, one writer, and no
  // second connection to disagree with the first about what a transaction is.
  int ran = 0;
  store->Run([&ran] { ++ran; });
  CHECK(ran == 1);

  store->Run([&store] {
    REQUIRE(store->db().RunOnce(
        "INSERT INTO calendars (id, kind, title, color) "
        "VALUES ('desde-el-sync', 'calendar', 'Trabajo', 0x4A8BF5)"));
  });
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM calendars WHERE id = 'desde-el-sync'") == 1);
}

TEST_CASE("an event is read back whole, location and rule included") {
  Open store;
  Draft draft = EventAt(L"Gym", Day(2026, 9, 28), 7 * 60, 8 * 60);
  draft.recurrence = L"FREQ=WEEKLY;BYDAY=MO";
  const DayItem shown = store->Create(draft);
  store.settle();

  const std::optional<EventDetail> event = store->Event(shown.uid);
  REQUIRE(event.has_value());
  CHECK(event->title == L"Gym");
  CHECK(event->calendarId == kLocalCalendarId);
  CHECK(event->startDay == Day(2026, 9, 28));
  CHECK(event->startMin == 7 * 60);
  CHECK(event->endMin == 8 * 60);
  CHECK(event->recurrence == L"FREQ=WEEKLY;BYDAY=MO");
  CHECK(event->location.empty());
  CHECK_FALSE(store->Event(L"no-existe").has_value());
}

TEST_CASE("dragging an event five times leaves one update carrying every field it touched") {
  Open store;
  const DayItem shown = store->Create(EventAt(L"Dentista", Day(2026, 9, 23), 17 * 60, 18 * 60));
  store.settle();

  EventDetail edit = *store->Event(shown.uid);
  for (int i = 1; i <= 5; ++i) {
    edit.startMin = (17 + i) * 60;
    edit.endMin = (18 + i) * 60;
    store->UpdateEvent(edit, i == 2 ? kEditLocation : 0u);
  }
  edit.location = L"Calle 10";
  store->UpdateEvent(edit, kEditRecurrence);
  store.settle();

  const std::optional<EventDetail> after = store->Event(shown.uid);
  REQUIRE(after.has_value());
  CHECK(after->startMin == 22 * 60);
  CHECK(after->location == L"Calle 10");
  // The creation and one update behind it, not six.
  CHECK(store->PendingOpCount() == 2);
  CHECK(CountRows(store->db(),
                  "SELECT COUNT(*) FROM pending_ops WHERE op = 'update+location+recurrence'") ==
        1);
  CHECK(store->ItemsForDay(Day(2026, 9, 23), false)[0].startMin == 22 * 60);
}

TEST_CASE("changing calendar writes down where Google has it, once, and forgets it going back") {
  Open store;
  REQUIRE(store->db().RunOnce(
      "INSERT INTO calendars (id, kind, title, color) VALUES "
      "('casa', 'calendar', 'Casa', 1), ('trabajo', 'calendar', 'Trabajo', 2)"));
  Draft draft = EventAt(L"Reunión", Day(2026, 9, 23), 9 * 60, 10 * 60);
  draft.calendar = "casa";
  const DayItem shown = store->Create(draft);
  store.settle();
  CHECK(shown.color == 1u);  // the chosen calendar's colour, on the first frame

  const auto movedFrom = [&store, &shown] {
    std::optional<Stmt> stmt = store->db().Prepare("SELECT moved_from FROM events WHERE uid = ?");
    REQUIRE(stmt.has_value());
    stmt->Bind(1, shown.uid);
    REQUIRE(stmt->Step());
    return stmt->IsNull(0) ? std::string("(null)") : stmt->Text(0);
  };

  // Never sent: the creation will simply go to the new calendar, there is nothing to move.
  EventDetail edit = *store->Event(shown.uid);
  edit.calendarId = "trabajo";
  store->UpdateEvent(edit, 0);
  store.settle();
  CHECK(movedFrom() == "(null)");

  // Already at Google, in 'trabajo': moving it to 'local' remembers 'trabajo'...
  REQUIRE(store->db().RunOnce("UPDATE events SET remote_id = 'r1'"));
  edit.calendarId = kLocalCalendarId;
  store->UpdateEvent(edit, 0);
  store.settle();
  CHECK(movedFrom() == "trabajo");
  // ...keeps remembering it through a second move...
  edit.calendarId = "casa";
  store->UpdateEvent(edit, 0);
  store.settle();
  CHECK(movedFrom() == "trabajo");
  // ...and forgets it when it goes back where Google has it.
  edit.calendarId = "trabajo";
  store->UpdateEvent(edit, 0);
  store.settle();
  CHECK(movedFrom() == "(null)");
}

TEST_CASE("a replaced operation never takes the number of one a pass may still be sending") {
  // pending_ops.id is a rowid: delete the highest and the next insert reuses it. A pass that
  // was sending the old operation would delete the new one by that number when it finished.
  Open store;
  const DayItem shown = store->Create(EventAt(L"Dentista", Day(2026, 9, 23), 17 * 60, 18 * 60));
  store.settle();
  REQUIRE(store->db().RunOnce("UPDATE events SET remote_id = 'r1'"));
  const auto highest = [&store] {
    return CountRows(store->db(), "SELECT COALESCE(MAX(id), 0) FROM pending_ops");
  };

  EventDetail edit = *store->Event(shown.uid);
  edit.startMin = 9 * 60;
  store->UpdateEvent(edit, 0);
  store.settle();
  // The creation went up meanwhile, and a pass is now holding the update.
  REQUIRE(store->db().RunOnce("DELETE FROM pending_ops WHERE op = 'create'"));
  const int held = highest();

  edit.recurrence = L"RRULE:FREQ=WEEKLY;BYDAY=WE";
  store->UpdateEvent(edit, kEditRecurrence);
  store.settle();
  CHECK(highest() > held);
  CHECK(store->PendingOpCount() == 1);
  // What the pass does when its request comes back: forget the one it sent, by its number.
  REQUIRE(store->db().RunOnce(("DELETE FROM pending_ops WHERE id = " + std::to_string(held))
                                  .c_str()));
  CHECK(CountRows(store->db(),
                  "SELECT COUNT(*) FROM pending_ops WHERE op = 'update+recurrence'") == 1);

  // The same holds for a deletion queued behind an update in flight.
  const int before = highest();
  store->Remove(shown.uid, /*isTask=*/false);
  store.settle();
  CHECK(highest() > before);
  CHECK(CountRows(store->db(), "SELECT COUNT(*) FROM pending_ops WHERE op = 'delete'") == 1);
  CHECK(store->PendingOpCount() == 1);
}
