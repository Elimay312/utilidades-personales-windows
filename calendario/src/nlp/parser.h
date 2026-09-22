#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/dates.h"

// What the user typed, understood. Nothing in here knows about Direct2D, Win32 or the popup:
// the parser is a pure function from text plus a clock reading to a result, which is what lets
// the tests drive every rule of CLAUDE.md without opening a window.
namespace agenda::nlp {

enum class Kind { Task, Event };

// Which rule claimed a stretch of the text. The input paints them all in the accent colour
// today; keeping the categories apart leaves the door open to telling them apart later.
enum class SpanKind { Prefix, Date, Time, Duration, Recurrence };

// A recognised stretch of the ORIGINAL text, in UTF-16 code units, so the input can hand it
// straight to IDWriteTextLayout without translating anything.
struct Span {
  size_t offset = 0;
  size_t length = 0;
  SpanKind kind = SpanKind::Date;
};

// Midnight is minute 0, a perfectly good time, so "no time at all" cannot be 0.
inline constexpr int kNoTime = -1;

struct DateTime {
  Date date{};
  int minuteOfDay = kNoTime;  // 0..1439
};

struct ParsedInput {
  Kind kind = Kind::Task;
  std::wstring title;
  std::optional<DateTime> start;  // empty means a task with no date at all
  std::optional<DateTime> end;
  bool allDay = true;   // no time was recognised
  int durationMin = 0;  // 0 when there is no time to give length to
  // The RRULE string itself, not a struct: it is literally what the Google Calendar API wants
  // in phase 5, and "cada lunes" already fits in one line of text.
  std::optional<std::wstring> recurrence;
  std::vector<Span> spans;  // sorted by offset
  // The same hour in the other half of the day, when nothing said which one ("a las 5" for
  // Friday): the preview offers a.m. and p.m. and Flipped swaps them. kNoTime when it is clear.
  int otherMinute = kNoTime;
};

// `parsed` with the start in the other half of the day, and the end moved with it.
ParsedInput Flipped(ParsedInput parsed);

// The clock arrives as a parameter and is never read inside the parser, so a test can sit at
// 23:59 on the last day of the year without waiting for it.
struct Now {
  Date date{};
  int minuteOfDay = 0;
};

// `defaultMinutes` is how long an event with a time and no length lasts: the settings window
// moves it, and CLAUDE.md's hour is where it starts.
ParsedInput ParseInput(std::wstring_view text, Now now, int defaultMinutes = 60);

// First letter up. It lives here because the preview card uses it to say what will be
// created, and what gets created has to be what the preview promised -- the window titles its
// rows with this same call.
std::wstring Capitalised(std::wstring text);

// The line the preview card shows. It lives here, not in the UI, because it is plain logic and
// this way it is covered by the tests instead of only by looking at a PNG. It returns Spanish
// because it is interface text (CLAUDE.md: code in English, interface in Spanish).
std::wstring PreviewText(const ParsedInput& parsed, Date today);

}  // namespace agenda::nlp
