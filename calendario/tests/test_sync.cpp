#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

#include "sync/map.h"

using namespace agenda;
using namespace agenda::sync;

namespace {

// Every test here has to pass on a laptop in Bogota and on a build machine in UTC, so nothing
// below asserts what the wall clock says. What it asserts is that a round trip comes back where
// it started and that two spellings of the same instant agree -- which is what would break if
// the digits of a string were being copied instead of the time being converted.
nlohmann::json Timed(const char* start, const char* end) {
  return nlohmann::json{{"id", "abc"},
                        {"etag", "\"1\""},
                        {"summary", "Dentista"},
                        {"updated", "2026-09-22T10:00:00.000Z"},
                        {"start", {{"dateTime", start}}},
                        {"end", {{"dateTime", end}}}};
}

nlohmann::json AllDay(const char* start, const char* end) {
  return nlohmann::json{{"id", "abc"},
                        {"summary", "Viaje"},
                        {"updated", "2026-09-22T10:00:00.000Z"},
                        {"start", {{"date", start}}},
                        {"end", {{"date", end}}}};
}

}  // namespace

TEST_CASE("an RFC 3339 stamp is read in every shape Google writes it") {
  const std::optional<std::int64_t> zulu = ParseInstant("2026-09-23T15:00:00Z");
  REQUIRE(zulu.has_value());

  // The same instant, spelled three other ways. If any of these disagreed, something would be
  // reading the digits instead of the time.
  REQUIRE(ParseInstant("2026-09-23T15:00:00.000Z") == zulu);
  REQUIRE(ParseInstant("2026-09-23T17:00:00+02:00") == zulu);
  REQUIRE(ParseInstant("2026-09-23T10:00:00-05:00") == zulu);
  REQUIRE(ParseInstant("2026-09-23T10:00:00-0500") == zulu);

  REQUIRE(FormatInstant(*zulu) == "2026-09-23T15:00:00Z");
}

TEST_CASE("a stamp that is not one gives no answer instead of 1970") {
  // Nullopt and not zero, on purpose: zero is 1970, and "newest wins" against 1970 would hand
  // every conflict to the other side without a word.
  REQUIRE_FALSE(ParseInstant("").has_value());
  REQUIRE_FALSE(ParseInstant("mañana a las cinco").has_value());
  REQUIRE_FALSE(ParseInstant("2026-09-23").has_value());
  REQUIRE_FALSE(ParseInstant("2026-13-01T00:00:00Z").has_value());  // no thirteenth month
  REQUIRE_FALSE(ParseInstant("2026-09-23T15:00:00").has_value());   // no zone at all
  REQUIRE_FALSE(ParseInstant("2026-09-23T15:00:00Zzz").has_value());
}

TEST_CASE("an instant survives the trip through this machine's wall clock") {
  // Midday on a day no country moves its clocks on. Whatever zone the machine is in, taking the
  // instant to the wall and back has to land on the same second.
  const std::optional<std::int64_t> noon = ParseInstant("2026-09-23T17:00:00Z");
  REQUIRE(noon.has_value());

  const std::optional<Wall> wall = LocalFromInstant(*noon);
  REQUIRE(wall.has_value());
  REQUIRE(wall->minute.has_value());

  REQUIRE(InstantFromLocal(wall->day, *wall->minute) == noon);
}

TEST_CASE("an all-day event of one day stays one day long") {
  // Google's end.date is exclusive: a single day arrives as 23 -> 24. Without the day taken off
  // again, every one-day event in the month grid would be two days long.
  const std::optional<EventRow> row = ReadEvent(AllDay("2026-09-23", "2026-09-24"));
  REQUIRE(row.has_value());
  REQUIRE(row->startDay == "2026-09-23");
  REQUIRE(row->endDay == "2026-09-23");
  REQUIRE_FALSE(row->startMin.has_value());
  REQUIRE_FALSE(row->endMin.has_value());
}

TEST_CASE("an all-day event of several days keeps all of them but the last") {
  const std::optional<EventRow> row = ReadEvent(AllDay("2026-09-23", "2026-09-26"));
  REQUIRE(row.has_value());
  REQUIRE(row->startDay == "2026-09-23");
  REQUIRE(row->endDay == "2026-09-25");
}

TEST_CASE("an all-day event goes back up with the exclusive end it came down with") {
  const std::optional<EventRow> row = ReadEvent(AllDay("2026-09-23", "2026-09-26"));
  REQUIRE(row.has_value());

  const nlohmann::json body = WriteEvent(*row, "");
  REQUIRE(body["start"]["date"] == "2026-09-23");
  REQUIRE(body["end"]["date"] == "2026-09-26");
  REQUIRE_FALSE(body.contains("id"));
}

TEST_CASE("a timed event lands on the wall and goes back to the same instant") {
  const nlohmann::json event = Timed("2026-09-23T17:00:00Z", "2026-09-23T18:00:00Z");
  const std::optional<EventRow> row = ReadEvent(event);
  REQUIRE(row.has_value());
  REQUIRE(row->startMin.has_value());
  REQUIRE(row->title == "Dentista");

  const nlohmann::json body = WriteEvent(*row, "");
  REQUIRE(ParseInstant(body["start"]["dateTime"].get<std::string>()) ==
          ParseInstant("2026-09-23T17:00:00Z"));
  REQUIRE(ParseInstant(body["end"]["dateTime"].get<std::string>()) ==
          ParseInstant("2026-09-23T18:00:00Z"));
}

TEST_CASE("the same instant written with an offset gives the same row") {
  // 17:00Z and 19:00+02:00 are the same moment. The row has to come out identical whichever
  // spelling Google happened to use.
  const std::optional<EventRow> zulu =
      ReadEvent(Timed("2026-09-23T17:00:00Z", "2026-09-23T18:00:00Z"));
  const std::optional<EventRow> offset =
      ReadEvent(Timed("2026-09-23T19:00:00+02:00", "2026-09-23T20:00:00+02:00"));
  REQUIRE(zulu.has_value());
  REQUIRE(offset.has_value());
  REQUIRE(zulu->startDay == offset->startDay);
  REQUIRE(zulu->startMin == offset->startMin);
  REQUIRE(zulu->endDay == offset->endDay);
  REQUIRE(zulu->endMin == offset->endMin);
}

TEST_CASE("an event with no end lasts an hour, across midnight if it has to") {
  EventRow row;
  row.remoteId = "abc";
  row.title = "Cena";
  row.startDay = "2026-09-23";
  row.startMin = 23 * 60 + 30;  // half past eleven at night

  const nlohmann::json body = WriteEvent(row, "");
  const std::optional<std::int64_t> from =
      ParseInstant(body["start"]["dateTime"].get<std::string>());
  const std::optional<std::int64_t> to = ParseInstant(body["end"]["dateTime"].get<std::string>());
  REQUIRE(from.has_value());
  REQUIRE(to.has_value());
  // An hour later, and worked out as an instant: minute 1470 of the same day does not exist.
  REQUIRE(*to - *from == 3600);
}

TEST_CASE("a cancelled event is a tombstone and is read as one") {
  const nlohmann::json cancelled{
      {"id", "abc"}, {"status", "cancelled"}, {"updated", "2026-09-22T10:00:00.000Z"}};
  const std::optional<EventRow> row = ReadEvent(cancelled);
  REQUIRE(row.has_value());
  REQUIRE(row->cancelled);
  // No start, no end, and it still came back: refusing it would throw away the only message
  // that says the event is gone.
}

TEST_CASE("an event with no id is refused outright") {
  REQUIRE_FALSE(ReadEvent(nlohmann::json{{"summary", "Sin identidad"}}).has_value());
  REQUIRE_FALSE(ReadEvent(nlohmann::json::array()).has_value());
}

TEST_CASE("a missing summary is a title and not an empty card") {
  nlohmann::json event = AllDay("2026-09-23", "2026-09-24");
  event.erase("summary");
  const std::optional<EventRow> row = ReadEvent(event);
  REQUIRE(row.has_value());
  REQUIRE(row->title == "(sin título)");
}

TEST_CASE("only the RRULE is kept out of the recurrence list") {
  nlohmann::json event = Timed("2026-09-23T17:00:00Z", "2026-09-23T18:00:00Z");
  event["recurrence"] = nlohmann::json::array(
      {"EXDATE;TZID=America/Bogota:20261001T120000", "RRULE:FREQ=WEEKLY;BYDAY=MO"});
  const std::optional<EventRow> row = ReadEvent(event);
  REQUIRE(row.has_value());
  REQUIRE(row->recurrence == "RRULE:FREQ=WEEKLY;BYDAY=MO");
}

TEST_CASE("a row with no recurrence says nothing about recurrence") {
  // The body is a PATCH: mentioning an empty recurrence would unrepeat an event somebody made
  // repeat on the web, and Agenda has no way of putting it back.
  const std::optional<EventRow> row = ReadEvent(AllDay("2026-09-23", "2026-09-24"));
  REQUIRE(row.has_value());
  REQUIRE_FALSE(WriteEvent(*row, "").contains("recurrence"));
}

TEST_CASE("an event's reminders are the notification ones, or its calendar's") {
  nlohmann::json event = AllDay("2026-09-23", "2026-09-24");
  // Nothing said is the calendar's list, which is also what useDefault says.
  CHECK_FALSE(ReadEvent(event)->reminders.has_value());
  event["reminders"] = {{"useDefault", true}};
  CHECK_FALSE(ReadEvent(event)->reminders.has_value());

  // Its own: the e-mail one is Google's to send, so only the notifications stay.
  event["reminders"] = {{"useDefault", false},
                        {"overrides",
                         {{{"method", "email"}, {"minutes", 1440}},
                          {{"method", "popup"}, {"minutes", 10}},
                          {{"method", "popup"}, {"minutes", 60}}}}};
  CHECK(ReadEvent(event)->reminders == std::optional<std::string>("10,60"));

  // Switched off on purpose is an empty list, which is not the same as the calendar's.
  event["reminders"] = {{"useDefault", false}};
  CHECK(ReadEvent(event)->reminders == std::optional<std::string>(""));
}

TEST_CASE("a calendar's default reminders are read the same way") {
  const nlohmann::json list = {{{"method", "popup"}, {"minutes", 30}},
                               {{"method", "email"}, {"minutes", 10}}};
  CHECK(ReadReminders(list) == "30");
  CHECK(ReadReminders(nlohmann::json::array()).empty());
  CHECK(ReadReminders(nlohmann::json::object()).empty());
}

TEST_CASE("the due date of a task is read as a date and never converted") {
  // Google keeps only the day and always hands it back as midnight UTC. Converting it would
  // move the day by one for everybody west of London, so this is the first ten characters and
  // nothing else -- which is also why this test says nothing about the machine's zone.
  const nlohmann::json task{{"id", "t1"},
                            {"title", "Pagar luz"},
                            {"updated", "2026-09-22T10:00:00.000Z"},
                            {"due", "2026-09-23T00:00:00.000Z"}};
  const std::optional<TaskRow> row = ReadTask(task);
  REQUIRE(row.has_value());
  REQUIRE(row->dueDay.has_value());
  REQUIRE(*row->dueDay == "2026-09-23");
}

TEST_CASE("a task with no date has no date, which is a real answer") {
  const nlohmann::json task{
      {"id", "t1"}, {"title", "Comprar leche"}, {"updated", "2026-09-22T10:00:00.000Z"}};
  const std::optional<TaskRow> row = ReadTask(task);
  REQUIRE(row.has_value());
  REQUIRE_FALSE(row->dueDay.has_value());
  REQUIRE_FALSE(row->doneAt.has_value());
}

TEST_CASE("a completed task keeps being completed even with no stamp") {
  nlohmann::json task{{"id", "t1"},
                      {"title", "Pagar luz"},
                      {"updated", "2026-09-22T10:00:00.000Z"},
                      {"status", "completed"}};
  const std::optional<TaskRow> row = ReadTask(task);
  REQUIRE(row.has_value());
  // Without this it would come back unticked on the next pass, and the task would rise from
  // the dead every five minutes.
  REQUIRE(row->doneAt.has_value());
}

TEST_CASE("unticking a task clears the stamp and not just the status") {
  TaskRow row;
  row.remoteId = "t1";
  row.title = "Pagar luz";

  const nlohmann::json body = WriteTask(row);
  REQUIRE(body["status"] == "needsAction");
  // Google keeps `completed` unless it is cleared, and then disagrees with its own status.
  REQUIRE(body["completed"].is_null());
  REQUIRE(body["due"].is_null());
}

TEST_CASE("a task that is done goes up done, on its day") {
  TaskRow row;
  row.remoteId = "t1";
  row.title = "Pagar luz";
  row.dueDay = "2026-09-23";
  row.doneAt = ParseInstant("2026-09-23T14:00:00Z");

  const nlohmann::json body = WriteTask(row);
  REQUIRE(body["status"] == "completed");
  REQUIRE(body["completed"] == "2026-09-23T14:00:00Z");
  REQUIRE(body["due"] == "2026-09-23T00:00:00.000Z");
}

TEST_CASE("a calendar colour is read out of its hexadecimal") {
  REQUIRE(ReadColor("#039be5") == 0x039BE5u);
  REQUIRE(ReadColor("#4A8BF5") == 0x4A8BF5u);
  REQUIRE_FALSE(ReadColor("039be5").has_value());
  REQUIRE_FALSE(ReadColor("#039be").has_value());
  REQUIRE_FALSE(ReadColor("rojo").has_value());
}

TEST_CASE("our uid is an identifier Google will take") {
  // Hexadecimal lands whole inside base32hex, which is what lets a creation be sent twice
  // without becoming two events.
  const std::string id = EventIdFor(L"3F2504E0-4F89-11D3-9A0C-0305E82C3301");
  REQUIRE(id == "3f2504e04f8911d39a0c0305e82c3301");
  REQUIRE(id.size() == 32);
  REQUIRE(IsUsableEventId(id));

  // The alphabet is base32hex and not hexadecimal, so a uid that is not a GUID can still be
  // usable -- and one that steps outside it is refused here instead of by Google. Empty means
  // "let Google pick one", which costs the idempotent retry and nothing else.
  REQUIRE(IsUsableEventId(EventIdFor(L"sample-nuevo")));
  REQUIRE(EventIdFor(L"muestra zeta").empty());          // a space is not in the alphabet
  REQUIRE(EventIdFor(L"3f2504e0-4f89-11d3-wxyz").empty());  // and neither are w to z
  REQUIRE_FALSE(IsUsableEventId("UPPERCASE"));
  REQUIRE_FALSE(IsUsableEventId("abc"));  // under five characters
}

TEST_CASE("what goes into a query string comes out escaped") {
  REQUIRE(UrlEscape("hola") == "hola");
  REQUIRE(UrlEscape("a-b_c.d~e") == "a-b_c.d~e");  // the unreserved four go through
  // A syncToken is base64 and carries these three. Left alone, '+' becomes a space at the far
  // end and the next incremental pass silently turns into a full one.
  REQUIRE(UrlEscape("CPjq/8rk+/YCEPjq") == "CPjq%2F8rk%2B%2FYCEPjq");
  REQUIRE(UrlEscape("eli@gmail.com") == "eli%40gmail.com");
  REQUIRE(UrlEscape("es-CO#1") == "es-CO%231");
}

TEST_CASE("a form leaves out what it has nothing to say about") {
  REQUIRE(FormEncode({{"grant_type", "refresh_token"}, {"refresh_token", "1//abc"}}) ==
          "grant_type=refresh_token&refresh_token=1%2F%2Fabc");
  // An empty secret is left out rather than sent empty: Google answers `client_secret=` with
  // invalid_client, which is a worse thing to read than nothing.
  REQUIRE(FormEncode({{"a", "1"}, {"b", ""}, {"c", "3"}}) == "a=1&c=3");
  REQUIRE(FormEncode({}).empty());
}

TEST_CASE("the wait after a 429 grows, has a ceiling, and listens to the server") {
  REQUIRE(BackoffMs(1, 0) == 500);
  REQUIRE(BackoffMs(2, 0) == 1000);
  REQUIRE(BackoffMs(3, 0) == 2000);
  REQUIRE(BackoffMs(4, 0) == 4000);
  REQUIRE(BackoffMs(5, 0) == 8000);
  // A long outage must not turn into a wait measured in minutes, and a silly attempt number
  // must not overflow into a negative one.
  REQUIRE(BackoffMs(40, 0) == kMaxBackoffMs);
  REQUIRE(BackoffMs(0, 0) == 500);

  // Retry-After wins over the arithmetic, up to the same ceiling.
  REQUIRE(BackoffMs(1, 3) == 3000);
  REQUIRE(BackoffMs(1, 3600) == kMaxBackoffMs);
}

TEST_CASE("only a 429 or a 5xx is worth sending again") {
  REQUIRE(ShouldRetry(429));
  REQUIRE(ShouldRetry(500));
  REQUIRE(ShouldRetry(503));
  REQUIRE_FALSE(ShouldRetry(200));
  REQUIRE_FALSE(ShouldRetry(400));
  REQUIRE_FALSE(ShouldRetry(403));
  REQUIRE_FALSE(ShouldRetry(410));  // this one means "start over", not "try again"
}

TEST_CASE("the newest wins and a tie stays where it is") {
  REQUIRE(RemoteWins(200, 100));
  REQUIRE_FALSE(RemoteWins(100, 200));
  // Our seconds against Google's milliseconds make ties possible, and the user is looking at
  // the local one.
  REQUIRE_FALSE(RemoteWins(100, 100));
}

TEST_CASE("the day before and the day after survive the ends of things") {
  REQUIRE(NextDay("2026-09-30") == "2026-10-01");
  REQUIRE(PrevDay("2026-10-01") == "2026-09-30");
  REQUIRE(NextDay("2026-12-31") == "2027-01-01");
  REQUIRE(PrevDay("2027-01-01") == "2026-12-31");
  REQUIRE(NextDay("2028-02-28") == "2028-02-29");  // a leap year
  REQUIRE(NextDay("2028-02-29") == "2028-03-01");
  REQUIRE(PrevDay("2027-03-01") == "2027-02-28");  // and one that is not
  // Something that is not a day comes back untouched rather than becoming a wrong one.
  REQUIRE(NextDay("mañana") == "mañana");
}

TEST_CASE("an edit names what it touched, and the queue reads it back") {
  CHECK(UpdateOp(0) == "update");
  CHECK(UpdateOp(kEditLocation | kEditRecurrence) == "update+location+recurrence");
  CHECK(UpdateEdits("update") == 0u);
  CHECK(UpdateEdits("update+recurrence") == kEditRecurrence);
  CHECK(UpdateEdits(UpdateOp(kEditLocation | kEditRecurrence)) ==
        (kEditLocation | kEditRecurrence));
}

TEST_CASE("a move says nothing about the repetition, an edit of it says all of it") {
  EventRow row;
  row.title = "Gym";
  row.startDay = "2026-09-28";
  row.startMin = 7 * 60;
  row.endDay = "2026-09-28";
  row.endMin = 8 * 60;
  row.recurrence = "RRULE:FREQ=WEEKLY;BYDAY=MO";

  // Dragged: Google keeps its rule and whatever EXDATEs Agenda never saw.
  CHECK_FALSE(WriteEvent(row, "").contains("recurrence"));
  // Edited: the rule goes up, and "Nunca" goes up as an empty list.
  CHECK(WriteEvent(row, "", kEditRecurrence)["recurrence"] ==
        nlohmann::json::array({"RRULE:FREQ=WEEKLY;BYDAY=MO"}));
  row.recurrence.clear();
  CHECK(WriteEvent(row, "", kEditRecurrence)["recurrence"] == nlohmann::json::array());
}

TEST_CASE("the parser's bare rule goes up with the prefix Google wants") {
  EventRow row;
  row.title = "Gym";
  row.startDay = "2026-09-28";
  row.startMin = 7 * 60;
  row.endDay = "2026-09-28";
  row.endMin = 8 * 60;
  row.recurrence = "FREQ=WEEKLY;BYDAY=MO";
  CHECK(WriteEvent(row, "abc")["recurrence"] ==
        nlohmann::json::array({"RRULE:FREQ=WEEKLY;BYDAY=MO"}));
  CHECK(RruleLine("RRULE:FREQ=DAILY") == "RRULE:FREQ=DAILY");
}

TEST_CASE("the location comes down, and goes up when there is one or it was emptied") {
  nlohmann::json event = Timed("2026-09-23T17:00:00Z", "2026-09-23T18:00:00Z");
  event["location"] = "Calle 10 # 5-20";
  const std::optional<EventRow> row = ReadEvent(event);
  REQUIRE(row.has_value());
  CHECK(row->location == "Calle 10 # 5-20");
  CHECK(WriteEvent(*row, "")["location"] == "Calle 10 # 5-20");

  EventRow empty = *row;
  empty.location.clear();
  // Empty because Agenda never read it: say nothing, Google keeps its own.
  CHECK_FALSE(WriteEvent(empty, "").contains("location"));
  // Empty because somebody emptied it: say so.
  CHECK(WriteEvent(empty, "", kEditLocation)["location"] == "");
}

TEST_CASE("moving to another calendar is a POST on the calendar it is still in") {
  CHECK(MovePath("casa@gmail.com", "abc123", "trabajo#1@group.calendar.google.com") ==
        "/calendar/v3/calendars/casa%40gmail.com/events/abc123/move?destination="
        "trabajo%231%40group.calendar.google.com");
}
