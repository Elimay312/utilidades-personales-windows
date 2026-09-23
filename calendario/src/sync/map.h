#pragma once

// What the synchronisation decides, with no socket and no window in sight: Google's RFC 3339
// turned into the wall clock this cache keeps, an event or a task of theirs turned into the
// columns of ours, and how long to wait after a 429.
//
// It lives apart from the rest of src/sync for the reason the parser lives apart from the
// interface: the tests reach it directly, without a network. And it earns that, because the
// clock is where this breaks. There are three rules living next to each other here and none of
// them is the other two:
//
//   * `end.date` on an all-day event is EXCLUSIVE. Google says a one-day event ends tomorrow.
//   * `dateTime` is an instant with an offset, and has to be read onto the wall clock of THIS
//     machine. Copying the digits out of the string is the bug that looks like it works.
//   * `due` on a task is a DATE that Google dresses up as an instant, always '...T00:00:00Z'.
//     Converting it would move the day for anybody west of London. It is not converted.
//
// Text is UTF-8 std::string all the way through, which is what Google sends and what SQLite
// stores. Nothing is widened on the way in and narrowed on the way out; only the log, which
// speaks wstring, converts.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/dates.h"
#include "data/model.h"

namespace agenda::sync {

// A reading of the clock on the wall: which day, and which minute of it. No minute means the
// thing lasts all day, which is a different claim from "midnight".
struct Wall {
  std::string day;  // 'YYYY-MM-DD'
  std::optional<int> minute;
};

// An event the way this cache keeps it. These are the columns of `events`, not Google's
// fields: the mapping has already happened by the time one of these exists. No colour in here
// on purpose -- the colour of an event is the colour of its calendar, which ItemsForDay
// already reaches with a join.
struct EventRow {
  std::string remoteId;
  std::string etag;
  std::string title;
  std::string notes;
  std::string location;
  std::string startDay;
  std::optional<int> startMin;  // empty means all day
  std::string endDay;           // INCLUSIVE: Google's exclusive end already had its day taken off
  std::optional<int> endMin;
  std::string recurrence;      // Google's list one per line: the RRULE and its EXDATEs
  // An occurrence Google keeps apart from its series -- moved, edited or cancelled on its own:
  // the series' id at Google (`recurringEventId`) and the day it had (`originalStartTime`).
  std::string seriesId;
  std::string originalDay;
  // Minutes before the start, "10,60", of the notification reminders; nullopt is Google's
  // useDefault -- the calendar's own list -- and an empty string is "no reminders at all".
  std::optional<std::string> reminders;
  std::int64_t updatedAt = 0;  // epoch seconds UTC, the one instant in the schema
  bool cancelled = false;      // how a deletion arrives in an incremental pass
};

struct TaskRow {
  std::string remoteId;
  std::string etag;
  std::string title;
  std::string notes;
  std::optional<std::string> dueDay;  // empty is a real answer: a task with no date at all
  std::optional<std::int64_t> doneAt;
  std::string position;
  std::int64_t updatedAt = 0;
  bool deleted = false;
};

// --- Days ---------------------------------------------------------------------------------

// The day before and the day after, on 'YYYY-MM-DD'. Named rather than written out at each of
// the four places it happens, because every one of those four is the exclusive-end trap and a
// name is what makes it visible when reading.
std::string NextDay(std::string_view day);
std::string PrevDay(std::string_view day);

// --- Time ---------------------------------------------------------------------------------

// An RFC 3339 stamp as the instant it names. Google writes them three ways -- 'Z', an offset,
// and either of those with fractional seconds -- and all three arrive here.
//
// Anything else gives nullopt and not 0: 0 is 1970, and "newest wins" against 1970 would hand
// every argument to the other side, quietly.
std::optional<std::int64_t> ParseInstant(std::string_view rfc3339);

// An instant written back as UTC, which is the only shape sent upwards: '2026-09-22T22:00:00Z'.
// No offset arithmetic to get wrong, and Google is happy with it.
std::string FormatInstant(std::int64_t epochSeconds);

// The two crossings between an instant and the clock on this wall. localtime_s and mktime are
// the same door core/dates.h already uses to ask what day it is, and they resolve daylight
// saving with the operating system's rules rather than ours. `tm_isdst = -1` is what asks it to.
std::optional<Wall> LocalFromInstant(std::int64_t epochSeconds);
std::optional<std::int64_t> InstantFromLocal(std::string_view day, int minuteOfDay);

// The IANA name of this machine's zone, 'America/Bogota'. Empty when the standard library
// cannot say, which is survivable: every dateTime sent carries its own instant, and the name
// only changes what happens to a REPEATING event when the clocks go forward.
std::string LocalZoneName();

// --- Google to us -------------------------------------------------------------------------

// One `start` or `end` of an event: either `date` (all day) or `dateTime` (an instant).
std::optional<Wall> ReadStamp(const nlohmann::json& node);

// A list of Google reminders -- an event's `overrides` or a calendar's `defaultReminders` --
// as the minutes of the notification ones, "10,60". E-mail reminders are Google's to send.
std::string ReadReminders(const nlohmann::json& list);

// '#039be5' as 0x039BE5. Nullopt for anything that is not six hex digits behind a hash.
std::optional<std::uint32_t> ReadColor(std::string_view hex);

// Nullopt means what arrived was not one at all -- no id -- which is worth refusing outright
// instead of writing a row nothing can ever update.
std::optional<EventRow> ReadEvent(const nlohmann::json& event);
std::optional<TaskRow> ReadTask(const nlohmann::json& task);

// --- Us to Google ---------------------------------------------------------------------------

// What goes up is a PATCH body, so a field left out is a field Google keeps. That is the whole
// reason attendees, location, reminders and the rest survive a round trip through Agenda
// untouched: they are never mentioned.
//
// `id` empty leaves the field out, which is what a modification wants; a creation passes
// EventIdFor(uid).
//
// `edits` is what the queued operation says changed beyond the times and the title
// (kEditLocation, kEditRecurrence in data/model.h). A modification only mentions the location
// when it has one or when it was emptied on purpose, and the repetition only when it was
// edited: an occurrence deleted on the web is a separate item and not an EXDATE, so sending the
// rule with every move could still bring it back. A creation sends whatever it has, and the
// recurrence always goes as every line it has, EXDATEs included.
nlohmann::json WriteEvent(const EventRow& row, std::string_view id, unsigned edits = 0);

// The rule as Google takes it: with its "RRULE:" in front. The parser writes it without one,
// and Google refuses the bare "FREQ=WEEKLY" with a 400.
std::string RruleLine(std::string_view rule);

// POST .../calendars/{from}/events/{id}/move?destination={to}: how Google moves an event to
// another calendar. A PATCH with another calendar in the path is a 404, not a move.
std::string MovePath(std::string_view from, std::string_view remoteId, std::string_view to);
nlohmann::json WriteTask(const TaskRow& row);

// Our uid as an identifier Google will accept: the hexadecimal without dashes, lowercase.
// Google wants base32hex, whose alphabet is 0-9 and a-v, and hexadecimal is 0-9 and a-f, so a
// GUID lands inside it whole. That is what makes a creation idempotent: a retry that repeats
// the POST gets a 409 back, which means "it is already there" and not a second event.
//
// Google Tasks has no equivalent -- it will not take an identifier from the client -- and that
// difference is the reason a task can be duplicated by a badly timed retry and an event cannot.
std::string EventIdFor(std::wstring_view uid);
bool IsUsableEventId(std::string_view id);

// The id Google gives one occurrence of a series: the series' id, an underscore and when the
// occurrence was due to start -- the day for an all-day series ('serie_20261005'), the instant
// in UTC otherwise ('serie_20261005T120000Z'). Knowing it is what lets "solo este" PATCH or
// DELETE one occurrence without asking Google for its instances first. `startMin` is the
// series' own start; empty means all day. Empty when the wall clock cannot be converted.
std::string InstanceIdFor(std::string_view seriesId, std::string_view originalDay,
                          std::optional<int> startMin);

// --- The wire -----------------------------------------------------------------------------

// Percent-encoding for anything that goes into a query string or a form. Unreserved characters
// through, everything else as %XX. It is here and not in http.cpp so the tests can reach it:
// the characters that matter are the '+' and '=' of a syncToken and the ':' of a calendar id,
// and those are exactly the ones that go wrong silently rather than loudly.
std::string UrlEscape(std::string_view text);
std::string FormEncode(
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields);

// --- Retrying -----------------------------------------------------------------------------

// How long to wait before trying again, with `attempt` counting from 1. Doubles from half a
// second, with a ceiling so an outage does not turn into a wait measured in minutes, and
// `Retry-After` wins over the arithmetic whenever the server bothered to say a number.
inline constexpr int kBaseBackoffMs = 500;
inline constexpr int kMaxBackoffMs = 8'000;
int BackoffMs(int attempt, int retryAfterSeconds);

// A 429 or a 5xx is worth repeating. Any other 4xx is an argument about the request itself, and
// sending it again word for word wins the same argument.
inline constexpr bool ShouldRetry(int status) {
  return status == 429 || (status >= 500 && status < 600);
}

// Newest wins (CLAUDE.md), and a tie goes to what is already here: the user is looking at the
// local one, and rewriting it with an identical remote one costs a write and a redraw to change
// nothing. Seconds against Google's milliseconds means a tie is possible, so this matters.
inline constexpr bool RemoteWins(std::int64_t remoteUpdated, std::int64_t localUpdated) {
  return remoteUpdated > localUpdated;
}

}  // namespace agenda::sync
