#include "sync/map.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <format>

namespace agenda::sync {
namespace {

// Exactly `length` digits at `offset`, or false. Half a number read is worse than none: it
// would turn a malformed stamp into a confident wrong date instead of a refusal.
bool Digits(std::string_view text, size_t offset, size_t length, int& out) {
  if (offset + length > text.size()) return false;
  out = 0;
  for (size_t i = 0; i < length; ++i) {
    const char digit = text[offset + i];
    if (digit < '0' || digit > '9') return false;
    out = out * 10 + (digit - '0');
  }
  return true;
}

// nlohmann throws when the key is missing or holds another type, and this runs on a worker
// thread where an escaping exception is std::terminate, which is the window disappearing. Same
// guard main.cpp already puts around the config, for the same reason.
std::string Str(const nlohmann::json& parent, const char* key, std::string_view fallback = {}) {
  const auto found = parent.find(key);
  if (found == parent.end() || !found->is_string()) return std::string(fallback);
  return found->get<std::string>();
}

bool Flag(const nlohmann::json& parent, const char* key) {
  const auto found = parent.find(key);
  return found != parent.end() && found->is_boolean() && found->get<bool>();
}

constexpr std::string_view kUntitled = "(sin título)";

}  // namespace

// --- Days ---------------------------------------------------------------------------------

std::string NextDay(std::string_view day) {
  const std::optional<Date> parsed = ParseDayKey(day);
  if (!parsed) return std::string(day);
  return DayKey(AddDays(*parsed, 1));
}

std::string PrevDay(std::string_view day) {
  const std::optional<Date> parsed = ParseDayKey(day);
  if (!parsed) return std::string(day);
  return DayKey(AddDays(*parsed, -1));
}

// --- Time ---------------------------------------------------------------------------------

std::optional<std::int64_t> ParseInstant(std::string_view text) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (text.size() < 20) return std::nullopt;
  if (!Digits(text, 0, 4, year) || text[4] != '-' || !Digits(text, 5, 2, month) ||
      text[7] != '-' || !Digits(text, 8, 2, day)) {
    return std::nullopt;
  }
  if (text[10] != 'T' && text[10] != 't') return std::nullopt;
  if (!Digits(text, 11, 2, hour) || text[13] != ':' || !Digits(text, 14, 2, minute) ||
      text[16] != ':' || !Digits(text, 17, 2, second)) {
    return std::nullopt;
  }

  size_t at = 19;
  // Fractional seconds are read past and dropped. The cache keeps seconds, and the only thing
  // this number is ever asked is which of two edits came second.
  if (at < text.size() && text[at] == '.') {
    ++at;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
  }

  // A stamp with no zone is refused rather than guessed at. Guessing is how a meeting moves
  // five hours, and Google always sends one.
  if (at >= text.size()) return std::nullopt;
  int offsetMinutes = 0;
  if (text[at] == 'Z' || text[at] == 'z') {
    ++at;
  } else if (text[at] == '+' || text[at] == '-') {
    const int sign = text[at] == '-' ? -1 : 1;
    int offsetHour = 0;
    int offsetMinute = 0;
    if (!Digits(text, at + 1, 2, offsetHour)) return std::nullopt;
    at += 3;
    if (at < text.size() && text[at] == ':') ++at;
    if (!Digits(text, at, 2, offsetMinute)) return std::nullopt;
    at += 2;
    offsetMinutes = sign * (offsetHour * 60 + offsetMinute);
  } else {
    return std::nullopt;
  }
  if (at != text.size()) return std::nullopt;

  const Date date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                  std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok() || hour > 23 || minute > 59 || second > 60) return std::nullopt;

  const std::int64_t days = std::chrono::sys_days{date}.time_since_epoch().count();
  // A leap second is folded onto :59 rather than refused. It is a real stamp, and the second it
  // lands on changes nothing this number is used for.
  const std::int64_t secondsPerDay = 86400;
  return days * secondsPerDay + hour * 3600 + minute * 60 + std::min(second, 59) -
         offsetMinutes * 60;
}

std::string FormatInstant(std::int64_t epochSeconds) {
  const std::chrono::sys_seconds instant{std::chrono::seconds{epochSeconds}};
  const std::chrono::sys_days midnight = std::chrono::floor<std::chrono::days>(instant);
  const int secondOfDay = static_cast<int>((instant - midnight).count());
  return std::format("{}T{:02}:{:02}:{:02}Z", DayKey(Date{midnight}), secondOfDay / 3600,
                     (secondOfDay / 60) % 60, secondOfDay % 60);
}

std::optional<Wall> LocalFromInstant(std::int64_t epochSeconds) {
  const std::time_t when = static_cast<std::time_t>(epochSeconds);
  std::tm local{};
  if (localtime_s(&local, &when) != 0) return std::nullopt;
  Wall wall;
  wall.day =
      std::format("{:04}-{:02}-{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  wall.minute = local.tm_hour * 60 + local.tm_min;
  return wall;
}

std::optional<std::int64_t> InstantFromLocal(std::string_view day, int minuteOfDay) {
  const std::optional<Date> date = ParseDayKey(day);
  if (!date || minuteOfDay < 0 || minuteOfDay >= 24 * 60) return std::nullopt;
  std::tm local{};
  local.tm_year = static_cast<int>(date->year()) - 1900;
  local.tm_mon = static_cast<int>(static_cast<unsigned>(date->month())) - 1;
  local.tm_mday = static_cast<int>(static_cast<unsigned>(date->day()));
  local.tm_hour = minuteOfDay / 60;
  local.tm_min = minuteOfDay % 60;
  // -1 is what asks the system which side of a daylight saving change this falls on, instead of
  // us deciding. On the hour that happens twice it picks one; on the hour that never happens it
  // walks forward. Both are answers, and neither is ours to invent.
  local.tm_isdst = -1;
  const std::time_t when = std::mktime(&local);
  if (when == static_cast<std::time_t>(-1)) return std::nullopt;
  return static_cast<std::int64_t>(when);
}

std::string LocalZoneName() {
  // The one place this file can throw, and it is caught here rather than let out: the standard
  // library needs the system timezone database and says it is missing by throwing. Not having a
  // name is survivable -- every dateTime sent carries its own instant -- so an empty string is a
  // real answer and not a failure.
  try {
    return std::string(std::chrono::current_zone()->name());
  } catch (...) {
    return {};
  }
}

// --- Google to us -------------------------------------------------------------------------

std::optional<Wall> ReadStamp(const nlohmann::json& node) {
  if (!node.is_object()) return std::nullopt;

  if (const std::string date = Str(node, "date"); !date.empty()) {
    if (!ParseDayKey(date)) return std::nullopt;
    Wall wall;
    wall.day = date;  // no minute: all day, which is not the same claim as midnight
    return wall;
  }
  if (const std::string stamp = Str(node, "dateTime"); !stamp.empty()) {
    const std::optional<std::int64_t> instant = ParseInstant(stamp);
    if (!instant) return std::nullopt;
    return LocalFromInstant(*instant);
  }
  return std::nullopt;
}

std::optional<std::uint32_t> ReadColor(std::string_view hex) {
  if (hex.size() != 7 || hex[0] != '#') return std::nullopt;
  std::uint32_t value = 0;
  for (size_t i = 1; i < hex.size(); ++i) {
    const char digit = hex[i];
    int nibble = 0;
    if (digit >= '0' && digit <= '9') {
      nibble = digit - '0';
    } else if (digit >= 'a' && digit <= 'f') {
      nibble = digit - 'a' + 10;
    } else if (digit >= 'A' && digit <= 'F') {
      nibble = digit - 'A' + 10;
    } else {
      return std::nullopt;
    }
    value = (value << 4) | static_cast<std::uint32_t>(nibble);
  }
  return value;
}

std::string ReadReminders(const nlohmann::json& list) {
  std::string out;
  if (!list.is_array()) return out;
  for (const nlohmann::json& reminder : list) {
    if (!reminder.is_object()) continue;
    const std::string method = Str(reminder, "method");
    if (method != "popup" && method != "email") continue;
    const auto minutes = reminder.find("minutes");
    if (minutes == reminder.end() || !minutes->is_number_integer()) continue;
    if (!out.empty()) out += ',';
    // The e-mail ones are kept apart with an "m", so the notifications skip them and an edit
    // made here sends them back to Google as they were.
    if (method == "email") out += 'm';
    out += std::to_string(minutes->get<int>());
  }
  return out;
}

std::optional<EventRow> ReadEvent(const nlohmann::json& event) {
  if (!event.is_object()) return std::nullopt;

  EventRow row;
  row.remoteId = Str(event, "id");
  if (row.remoteId.empty()) return std::nullopt;
  row.etag = Str(event, "etag");
  row.updatedAt = ParseInstant(Str(event, "updated")).value_or(0);
  row.cancelled = Str(event, "status") == "cancelled";
  // An occurrence of a series that Google keeps apart. Read before the cancellation returns,
  // because a cancelled occurrence is exactly the one that needs to say which day it took away.
  row.seriesId = Str(event, "recurringEventId");
  if (const auto original = event.find("originalStartTime"); original != event.end()) {
    if (const std::optional<Wall> wall = ReadStamp(*original)) row.originalDay = wall->day;
  }
  // A cancellation is a tombstone: an id, a status and nothing else. Asking it for a start day
  // would refuse the one message that says the event is gone.
  if (row.cancelled) return row;

  row.title = Str(event, "summary", kUntitled);
  if (row.title.empty()) row.title = kUntitled;
  row.notes = Str(event, "description");
  row.location = Str(event, "location");

  const auto startNode = event.find("start");
  if (startNode == event.end()) return std::nullopt;
  const std::optional<Wall> start = ReadStamp(*startNode);
  if (!start) return std::nullopt;
  row.startDay = start->day;
  row.startMin = start->minute;

  const auto endNode = event.find("end");
  std::optional<Wall> end = endNode != event.end() ? ReadStamp(*endNode) : std::optional<Wall>{};
  // Google always sends an end. One missing is not worth throwing the event away over: an event
  // that lasts its own day is a better answer than one that never appears.
  if (!end) end = start;
  row.endMin = end->minute;
  // The exclusive end, undone. An all-day event of a single day arrives as 23 -> 24, and
  // without this line every one of them would be two days long in the month grid.
  row.endDay = start->minute.has_value() ? end->day : PrevDay(end->day);
  if (row.endDay < row.startDay) row.endDay = row.startDay;

  if (const auto found = event.find("reminders"); found != event.end() && found->is_object()) {
    if (!Flag(*found, "useDefault")) {
      const auto overrides = found->find("overrides");
      row.reminders = overrides != found->end() ? ReadReminders(*overrides) : std::string();
    }
  }

  if (const auto found = event.find("recurrence"); found != event.end() && found->is_array()) {
    // Every line, one per line: the RRULE and the EXDATEs that take days away from it.
    for (const nlohmann::json& line : *found) {
      if (!line.is_string()) continue;
      if (!row.recurrence.empty()) row.recurrence += '\n';
      row.recurrence += line.get<std::string>();
    }
  }
  return row;
}

std::optional<TaskRow> ReadTask(const nlohmann::json& task) {
  if (!task.is_object()) return std::nullopt;

  TaskRow row;
  row.remoteId = Str(task, "id");
  if (row.remoteId.empty()) return std::nullopt;
  row.etag = Str(task, "etag");
  row.updatedAt = ParseInstant(Str(task, "updated")).value_or(0);
  row.deleted = Flag(task, "deleted");
  if (row.deleted) return row;

  row.title = Str(task, "title", kUntitled);
  if (row.title.empty()) row.title = kUntitled;
  row.notes = Str(task, "notes");
  row.position = Str(task, "position");

  // `due` is a date wearing an instant's clothes: Google stores only the day, ignores any time
  // given to it, and always hands it back as midnight UTC. Taking the first ten characters is
  // not the lazy reading, it is the correct one -- converting this to local time would move the
  // day by one for everybody west of London.
  if (const std::string due = Str(task, "due"); due.size() >= 10) {
    const std::string_view day = std::string_view(due).substr(0, 10);
    if (ParseDayKey(day)) row.dueDay = std::string(day);
  }

  if (Str(task, "status") == "completed") {
    row.doneAt = ParseInstant(Str(task, "completed"));
    // Completed without a stamp is still completed. Leaving doneAt empty would untick it on the
    // next pass, and the task would come back from the dead.
    if (!row.doneAt) row.doneAt = row.updatedAt;
  }
  return row;
}

// --- Us to Google ---------------------------------------------------------------------------

std::string RruleLine(std::string_view rule) {
  if (rule.starts_with("RRULE:")) return std::string(rule);
  return "RRULE:" + std::string(rule);
}

std::string MovePath(std::string_view from, std::string_view remoteId, std::string_view to) {
  return "/calendar/v3/calendars/" + UrlEscape(from) + "/events/" + UrlEscape(remoteId) +
         "/move?destination=" + UrlEscape(to);
}

nlohmann::json WriteEvent(const EventRow& row, std::string_view id, unsigned edits) {
  nlohmann::json body = nlohmann::json::object();
  if (!id.empty()) body["id"] = std::string(id);
  body["summary"] = row.title;
  body["description"] = row.notes;

  const std::optional<std::int64_t> from =
      row.startMin ? InstantFromLocal(row.startDay, *row.startMin) : std::nullopt;
  if (from) {
    // The end is worked out as an instant and not as a minute of a day, so half past eleven
    // plus an hour is half past midnight tomorrow and not minute 1470 of today.
    const std::optional<std::int64_t> given =
        row.endMin ? InstantFromLocal(row.endDay, *row.endMin) : std::nullopt;
    const std::int64_t to = given && *given > *from ? *given : *from + 3600;

    nlohmann::json start = nlohmann::json::object();
    nlohmann::json end = nlohmann::json::object();
    start["dateTime"] = FormatInstant(*from);
    end["dateTime"] = FormatInstant(to);
    // The zone matters only to a repetition: it is what keeps a weekly seven o'clock at seven
    // when the clocks move. Left out when the standard library cannot name it, because the
    // instant above already places this one occurrence.
    if (const std::string zone = LocalZoneName(); !zone.empty()) {
      start["timeZone"] = zone;
      end["timeZone"] = zone;
    }
    body["start"] = std::move(start);
    body["end"] = std::move(end);
  } else {
    nlohmann::json start = nlohmann::json::object();
    nlohmann::json end = nlohmann::json::object();
    start["date"] = row.startDay;
    end["date"] = NextDay(row.endDay);  // back to Google's exclusive end
    body["start"] = std::move(start);
    body["end"] = std::move(end);
  }

  const bool creating = !id.empty();
  if (!row.location.empty() || (edits & kEditLocation)) body["location"] = row.location;

  // Only when they were chosen here: left out, Google keeps whatever it has.
  if (edits & kEditReminders) {
    nlohmann::json reminders = nlohmann::json::object();
    reminders["useDefault"] = !row.reminders.has_value();
    if (row.reminders) {
      nlohmann::json overrides = nlohmann::json::array();
      std::string_view rest = *row.reminders;
      while (!rest.empty()) {
        const size_t comma = rest.find(',');
        std::string_view item = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        const bool email = item.starts_with('m');
        if (email) item.remove_prefix(1);
        int minutes = 0;
        const auto [end, error] = std::from_chars(item.data(), item.data() + item.size(), minutes);
        if (error != std::errc{} || end != item.data() + item.size()) continue;
        overrides.push_back({{"method", email ? "email" : "popup"}, {"minutes", minutes}});
      }
      reminders["overrides"] = std::move(overrides);
    }
    body["reminders"] = std::move(reminders);
  }

  // This body is a PATCH, so a field left out is a field Google keeps. A modification says
  // nothing about the repetition unless the repetition is what was edited -- and then an empty
  // list is how "Nunca" stops a series.
  if (creating || (edits & kEditRecurrence)) {
    if (!row.recurrence.empty()) {
      nlohmann::json lines = nlohmann::json::array();
      std::string_view rest = row.recurrence;
      while (!rest.empty()) {
        const size_t newline = rest.find('\n');
        const std::string_view line = rest.substr(0, newline);
        rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
        if (line.empty()) continue;
        // The parser writes the rule bare; EXDATE and RDATE lines already say what they are.
        lines.push_back(line.starts_with("EXDATE") || line.starts_with("RDATE")
                            ? std::string(line)
                            : RruleLine(line));
      }
      body["recurrence"] = std::move(lines);
    } else if (!creating) {
      body["recurrence"] = nlohmann::json::array();
    }
  }
  return body;
}

nlohmann::json WriteTask(const TaskRow& row) {
  nlohmann::json body = nlohmann::json::object();
  body["title"] = row.title;
  body["notes"] = row.notes;
  body["status"] = row.doneAt ? "completed" : "needsAction";
  // Unticking has to clear the stamp as well. With only the status changed, Google keeps
  // `completed` and then disagrees with itself about whether the task is done.
  if (row.doneAt) {
    body["completed"] = FormatInstant(*row.doneAt);
  } else {
    body["completed"] = nullptr;
  }
  if (row.dueDay) {
    body["due"] = *row.dueDay + "T00:00:00.000Z";
  } else {
    body["due"] = nullptr;
  }
  return body;
}

std::string InstanceIdFor(std::string_view seriesId, std::string_view originalDay,
                          std::optional<int> startMin) {
  std::string stamp;
  if (startMin) {
    const std::optional<std::int64_t> start = InstantFromLocal(originalDay, *startMin);
    if (!start) return {};
    stamp = FormatInstant(*start);  // '2026-10-05T12:00:00Z'
  } else {
    stamp = std::string(originalDay);  // '2026-10-05'
  }
  std::erase_if(stamp, [](char c) { return c == '-' || c == ':'; });
  return std::string(seriesId) + '_' + stamp;
}

std::string EventIdFor(std::wstring_view uid) {
  std::string id;
  id.reserve(uid.size());
  for (const wchar_t letter : uid) {
    if (letter == L'-') continue;
    if (letter >= L'A' && letter <= L'Z') {
      id.push_back(static_cast<char>(letter - L'A' + 'a'));
    } else if ((letter >= L'0' && letter <= L'9') || (letter >= L'a' && letter <= L'z')) {
      id.push_back(static_cast<char>(letter));
    } else {
      return {};
    }
  }
  // Empty is a real answer, and the caller knows it: it means "let Google pick the id", which
  // costs the idempotent retry and nothing else.
  return IsUsableEventId(id) ? id : std::string();
}

bool IsUsableEventId(std::string_view id) {
  if (id.size() < 5 || id.size() > 1024) return false;
  for (const char letter : id) {
    const bool inAlphabet = (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'v');
    if (!inAlphabet) return false;
  }
  return true;
}

// --- The wire -----------------------------------------------------------------------------

std::string UrlEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char letter : text) {
    const bool unreserved = (letter >= 'A' && letter <= 'Z') ||
                            (letter >= 'a' && letter <= 'z') ||
                            (letter >= '0' && letter <= '9') || letter == '-' ||
                            letter == '_' || letter == '.' || letter == '~';
    if (unreserved) {
      out.push_back(letter);
    } else {
      out += std::format("%{:02X}", static_cast<unsigned>(static_cast<unsigned char>(letter)));
    }
  }
  return out;
}

std::string FormEncode(
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
  std::string out;
  for (const auto& [name, value] : fields) {
    // An empty field is left out rather than sent empty: the token endpoint answers a
    // `client_secret=` with invalid_client, which is a worse thing to read than nothing.
    if (value.empty()) continue;
    if (!out.empty()) out.push_back('&');
    out += UrlEscape(name);
    out.push_back('=');
    out += UrlEscape(value);
  }
  return out;
}

// --- Retrying -----------------------------------------------------------------------------

int BackoffMs(int attempt, int retryAfterSeconds) {
  // What the server asked for wins, up to the same ceiling. Being told to wait a minute and
  // waiting eight seconds is how one 429 becomes two; being told to wait an hour and doing it
  // would hang the pass, and the next one is five minutes away anyway.
  if (retryAfterSeconds > 0) {
    const std::int64_t asked = static_cast<std::int64_t>(retryAfterSeconds) * 1000;
    return static_cast<int>(std::min<std::int64_t>(asked, kMaxBackoffMs));
  }
  std::int64_t wait = kBaseBackoffMs;
  for (int step = 1; step < std::max(attempt, 1) && wait < kMaxBackoffMs; ++step) wait *= 2;
  return static_cast<int>(std::min<std::int64_t>(wait, kMaxBackoffMs));
}

}  // namespace agenda::sync
