#include <catch2/catch_test_macros.hpp>

#include <format>
#include <optional>

#include "nlp/parser.h"

using namespace agenda;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

namespace {

Date Ymd(int y, unsigned m, unsigned d) { return Date{year{y}, month{m}, day{d}}; }

// Four clocks to write every case against. Nothing in the parser reads the real one.
const nlp::Now kTue{Ymd(2026, 9, 22), 10 * 60};             // a Tuesday, mid morning
const nlp::Now kDay30{Ymd(2026, 9, 30), 9 * 60};            // past the 25th of the month
const nlp::Now kYearEnd{Ymd(2026, 12, 28), 9 * 60};         // past the 25th of December
const nlp::Now kLate{Ymd(2026, 9, 22), 23 * 60 + 59};       // one minute before midnight

std::wstring When(const std::optional<nlp::DateTime>& moment) {
  if (!moment) return L"sin fecha";
  return std::format(L"{:04}-{:02}-{:02}", static_cast<int>(moment->date.year()),
                     static_cast<unsigned>(moment->date.month()),
                     static_cast<unsigned>(moment->date.day()));
}

std::wstring Clock(const std::optional<nlp::DateTime>& moment) {
  if (!moment || moment->minuteOfDay == nlp::kNoTime) return L"sin hora";
  return std::format(L"{:02}:{:02}", moment->minuteOfDay / 60, moment->minuteOfDay % 60);
}

}  // namespace

// --- the eight cases CLAUDE.md asks for by name ------------------------------------------

TEST_CASE("tomorrow at five in the afternoon is an hour long event") {
  const nlp::ParsedInput out = nlp::ParseInput(L"mañana 5pm dentista", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"17:00");
  CHECK(Clock(out.end) == L"18:00");
  CHECK(out.durationMin == 60);
  CHECK_FALSE(out.allDay);
  CHECK(out.title == L"dentista");
}

TEST_CASE("today at a twenty four hour time") {
  const nlp::ParsedInput out = nlp::ParseInput(L"hoy 17:00 dentista", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"17:00");
  CHECK(out.title == L"dentista");
}

TEST_CASE("a weekday, a spelled out hour and a length, all in one line") {
  const nlp::ParsedInput out =
      nlp::ParseInput(L"dentista el viernes a las 3 de la tarde por 2h", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(When(out.start) == L"2026-09-25");
  CHECK(Clock(out.start) == L"15:00");
  CHECK(Clock(out.end) == L"17:00");
  CHECK(out.durationMin == 120);
  CHECK(out.title == L"dentista");
}

TEST_CASE("the day after tomorrow with a bare hour") {
  const nlp::ParsedInput out = nlp::ParseInput(L"pasado mañana 9 reunión con Ana", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(When(out.start) == L"2026-09-24");
  CHECK(Clock(out.start) == L"09:00");
  CHECK(out.title == L"reunión con Ana");
}

TEST_CASE("a day of the month with no hour is a task") {
  const nlp::ParsedInput out = nlp::ParseInput(L"el 25 almuerzo", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(When(out.start) == L"2026-09-25");
  CHECK(out.allDay);
  CHECK(out.title == L"almuerzo");
}

TEST_CASE("nothing understood is a task with no date") {
  const nlp::ParsedInput out = nlp::ParseInput(L"comprar leche", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK_FALSE(out.start.has_value());
  CHECK(out.allDay);
  CHECK(out.title == L"comprar leche");
  CHECK(out.spans.empty());
}

TEST_CASE("the t prefix forces a task") {
  const nlp::ParsedInput out = nlp::ParseInput(L"t: pagar luz el lunes", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(When(out.start) == L"2026-09-28");
  CHECK(out.title == L"pagar luz");
}

TEST_CASE("every monday becomes a weekly rule starting on the next monday") {
  const nlp::ParsedInput out = nlp::ParseInput(L"gym cada lunes 7am", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(out.recurrence == L"FREQ=WEEKLY;BYDAY=MO");
  CHECK(When(out.start) == L"2026-09-28");
  CHECK(Clock(out.start) == L"07:00");
  CHECK(out.title == L"gym");
}

// --- capitals and accents ------------------------------------------------------------------

TEST_CASE("shouting changes nothing but the title") {
  const nlp::ParsedInput out = nlp::ParseInput(L"HOY 17:00 DENTISTA", kTue);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"17:00");
  CHECK(out.title == L"DENTISTA");
}

TEST_CASE("mixed case works the same as lower case") {
  const nlp::ParsedInput out = nlp::ParseInput(L"Mañana 5PM dentista", kTue);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"17:00");
}

TEST_CASE("a missing tilde still means tomorrow") {
  const nlp::ParsedInput out = nlp::ParseInput(L"manana 5pm dentista", kTue);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"17:00");
  CHECK(out.title == L"dentista");
}

TEST_CASE("a whole line without accents") {
  const nlp::ParsedInput out = nlp::ParseInput(L"pasado manana 9 reunion con Ana", kTue);
  CHECK(When(out.start) == L"2026-09-24");
  CHECK(Clock(out.start) == L"09:00");
  CHECK(out.title == L"reunion con Ana");
}

TEST_CASE("accented capitals fold too") {
  const nlp::ParsedInput out = nlp::ParseInput(L"PASADO MAÑANA 9 REUNIÓN CON ANA", kTue);
  CHECK(When(out.start) == L"2026-09-24");
  CHECK(out.title == L"REUNIÓN CON ANA");
}

// --- weekdays ------------------------------------------------------------------------------

TEST_CASE("next tuesday on a tuesday is a week away") {
  const nlp::ParsedInput out = nlp::ParseInput(L"próximo martes reunión", kTue);
  CHECK(When(out.start) == L"2026-09-29");
  CHECK(out.title == L"reunión");
}

TEST_CASE("a bare tuesday on a tuesday is today") {
  const nlp::ParsedInput out = nlp::ParseInput(L"martes reunión", kTue);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(out.kind == nlp::Kind::Task);
}

TEST_CASE("next tuesday in English") {
  const nlp::ParsedInput out = nlp::ParseInput(L"next tuesday meeting", kTue);
  CHECK(When(out.start) == L"2026-09-29");
  CHECK(out.title == L"meeting");
}

// --- days of the month, month and year rollover --------------------------------------------

TEST_CASE("a day of the month that already went by lands next month") {
  const nlp::ParsedInput out = nlp::ParseInput(L"el 25 almuerzo", kDay30);
  CHECK(When(out.start) == L"2026-10-25");
}

TEST_CASE("an English ordinal day of the month") {
  const nlp::ParsedInput out = nlp::ParseInput(L"on the 25th lunch", kDay30);
  CHECK(When(out.start) == L"2026-10-25");
  CHECK(out.title == L"lunch");
}

TEST_CASE("a day of the month in December rolls the year over") {
  const nlp::ParsedInput out = nlp::ParseInput(L"el 25 evento", kYearEnd);
  CHECK(When(out.start) == L"2027-01-25");
}

TEST_CASE("a thirty first skips the months that do not have one") {
  const nlp::ParsedInput out = nlp::ParseInput(L"el 31 pago", kTue);
  CHECK(When(out.start) == L"2026-10-31");
}

// --- the hour that already went by ----------------------------------------------------------

TEST_CASE("an hour gone by with no date moves to tomorrow") {
  const nlp::ParsedInput out = nlp::ParseInput(L"5pm reunion", kLate);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"17:00");
}

TEST_CASE("an explicit today is taken at its word even at one minute to midnight") {
  const nlp::ParsedInput out = nlp::ParseInput(L"hoy 17:00 reunion", kLate);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"17:00");
}

// --- hours that are not hours ---------------------------------------------------------------

TEST_CASE("a marked hour out of range is left in the title") {
  const nlp::ParsedInput out = nlp::ParseInput(L"a las 25 reunion", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK_FALSE(out.start.has_value());
  CHECK(out.title == L"a las 25 reunion");
  CHECK(out.spans.empty());
}

TEST_CASE("a twenty five hour clock is not a clock") {
  const nlp::ParsedInput out = nlp::ParseInput(L"25:00 reunion", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.title == L"25:00 reunion");
}

TEST_CASE("thirteen pm is not a time") {
  const nlp::ParsedInput out = nlp::ParseInput(L"13pm reunion", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.title == L"13pm reunion");
}

// --- noon and midnight ------------------------------------------------------------------------

TEST_CASE("noon in Spanish") {
  const nlp::ParsedInput out = nlp::ParseInput(L"mediodia almuerzo", kTue);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"12:00");
  CHECK(out.title == L"almuerzo");
}

TEST_CASE("midnight is minute zero, not the absence of a time") {
  const nlp::ParsedInput out = nlp::ParseInput(L"medianoche cierre", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK_FALSE(out.allDay);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"00:00");
}

TEST_CASE("noon in English") {
  const nlp::ParsedInput out = nlp::ParseInput(L"noon lunch", kTue);
  CHECK(Clock(out.start) == L"12:00");
  CHECK(out.title == L"lunch");
}

TEST_CASE("midnight in English") {
  const nlp::ParsedInput out = nlp::ParseInput(L"midnight backup", kTue);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"00:00");
}

// --- ranges and lengths -----------------------------------------------------------------------

TEST_CASE("from three to five sets the start and the length at once") {
  const nlp::ParsedInput out = nlp::ParseInput(L"de 3 a 5 reunion", kTue);
  CHECK(Clock(out.start) == L"15:00");
  CHECK(Clock(out.end) == L"17:00");
  CHECK(out.durationMin == 120);
  CHECK(out.title == L"reunion");
}

TEST_CASE("a range in English") {
  const nlp::ParsedInput out = nlp::ParseInput(L"from 3 to 5 meeting", kTue);
  CHECK(Clock(out.start) == L"15:00");
  CHECK(Clock(out.end) == L"17:00");
}

TEST_CASE("a range across noon keeps each end on its own side of the rule") {
  const nlp::ParsedInput out = nlp::ParseInput(L"de 11 a 1 turno", kTue);
  CHECK(Clock(out.start) == L"11:00");
  CHECK(Clock(out.end) == L"13:00");
  CHECK(out.durationMin == 120);
}

TEST_CASE("a length with no time to measure goes back into the title") {
  const nlp::ParsedInput out = nlp::ParseInput(L"30 min llamada", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.durationMin == 0);
  CHECK(out.title == L"30 min llamada");
}

TEST_CASE("two hours long, but of what") {
  const nlp::ParsedInput out = nlp::ParseInput(L"reunion por 2h", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.durationMin == 0);
  CHECK(out.title == L"reunion por 2h");
}

TEST_CASE("a date without an hour does not rescue a length either") {
  const nlp::ParsedInput out = nlp::ParseInput(L"reunion manana por 2h", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(out.durationMin == 0);
  CHECK(out.title == L"reunion por 2h");
}

// --- recurrence ---------------------------------------------------------------------------------

TEST_CASE("every monday in English") {
  const nlp::ParsedInput out = nlp::ParseInput(L"gym every monday 7am", kTue);
  CHECK(out.recurrence == L"FREQ=WEEKLY;BYDAY=MO");
  CHECK(When(out.start) == L"2026-09-28");
  CHECK(out.title == L"gym");
}

TEST_CASE("every day in Spanish is a daily rule with no weekday of its own") {
  const nlp::ParsedInput out = nlp::ParseInput(L"meditar todos los dias 6am", kTue);
  CHECK(out.recurrence == L"FREQ=DAILY");
  CHECK(When(out.start) == L"2026-09-23");  // six in the morning is already gone
  CHECK(out.title == L"meditar");
}

TEST_CASE("every day in English") {
  const nlp::ParsedInput out = nlp::ParseInput(L"meditar every day 6am", kTue);
  CHECK(out.recurrence == L"FREQ=DAILY");
  CHECK(When(out.start) == L"2026-09-23");
}

// --- prefixes -------------------------------------------------------------------------------------

TEST_CASE("the bang prefix keeps an hour but still makes a task") {
  const nlp::ParsedInput out = nlp::ParseInput(L"! reunion 5pm", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"17:00");
  CHECK_FALSE(out.allDay);
  CHECK(out.durationMin == 0);
  CHECK(out.title == L"reunion");
}

TEST_CASE("the e prefix makes an all day event out of a bare title") {
  const nlp::ParsedInput out = nlp::ParseInput(L"e: comprar pan", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(out.allDay);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(out.title == L"comprar pan");
}

TEST_CASE("a prefix in capitals is still a prefix") {
  const nlp::ParsedInput out = nlp::ParseInput(L"T: pagar luz el lunes", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(When(out.start) == L"2026-09-28");
  CHECK(out.title == L"pagar luz");
}

TEST_CASE("a title in title case is left alone") {
  const nlp::ParsedInput out = nlp::ParseInput(L"Comprar Leche", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.title == L"Comprar Leche");
}

// --- in N days ----------------------------------------------------------------------------------

TEST_CASE("in three days") {
  const nlp::ParsedInput out = nlp::ParseInput(L"en 3 dias pagar", kTue);
  CHECK(When(out.start) == L"2026-09-25");
  CHECK(out.title == L"pagar");
}

TEST_CASE("in three days in English") {
  const nlp::ParsedInput out = nlp::ParseInput(L"in 3 days pay rent", kTue);
  CHECK(When(out.start) == L"2026-09-25");
  CHECK(out.title == L"pay rent");
}

// --- odds and ends ---------------------------------------------------------------------------------

TEST_CASE("a line that is all date and hour leaves an empty title") {
  const nlp::ParsedInput out = nlp::ParseInput(L"el lunes a las 3", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(When(out.start) == L"2026-09-28");
  CHECK(Clock(out.start) == L"15:00");
  CHECK(out.title.empty());
}

TEST_CASE("seventeen h is a time and not seventeen hours long") {
  const nlp::ParsedInput out = nlp::ParseInput(L"17h reunion", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(Clock(out.start) == L"17:00");
  CHECK(out.durationMin == 60);
  CHECK(out.title == L"reunion");
}

TEST_CASE("an empty line understands nothing and says so") {
  const nlp::ParsedInput out = nlp::ParseInput(L"", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.title.empty());
  CHECK(out.spans.empty());
}

TEST_CASE("five in the morning is not tomorrow, and tomorrow is not five in the morning") {
  const nlp::ParsedInput out = nlp::ParseInput(L"mañana a las 5 de la mañana yoga", kTue);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"05:00");
  CHECK(out.title == L"yoga");
}

// --- spans, which is what the input paints ------------------------------------------------------

TEST_CASE("the spans point at the accented text the user actually typed") {
  const nlp::ParsedInput out = nlp::ParseInput(L"mañana 5pm dentista", kTue);
  REQUIRE(out.spans.size() == 2);
  CHECK(out.spans[0].offset == 0);
  CHECK(out.spans[0].length == 6);  // "mañana", the enye counting as one
  CHECK(out.spans[0].kind == nlp::SpanKind::Date);
  CHECK(out.spans[1].offset == 7);
  CHECK(out.spans[1].length == 3);  // "5pm"
  CHECK(out.spans[1].kind == nlp::SpanKind::Time);
}

TEST_CASE("the prefix is a span of its own so the input can dim it") {
  const nlp::ParsedInput out = nlp::ParseInput(L"t: pagar luz el lunes", kTue);
  REQUIRE(out.spans.size() == 2);
  CHECK(out.spans[0].offset == 0);
  CHECK(out.spans[0].length == 2);
  CHECK(out.spans[0].kind == nlp::SpanKind::Prefix);
  CHECK(out.spans[1].kind == nlp::SpanKind::Date);
}

TEST_CASE("the spans never overlap and never run past the text") {
  const std::wstring text = L"dentista el viernes a las 3 de la tarde por 2h";
  const nlp::ParsedInput out = nlp::ParseInput(text, kTue);
  size_t previous = 0;
  for (const nlp::Span& span : out.spans) {
    CHECK(span.offset >= previous);
    CHECK(span.offset + span.length <= text.size());
    previous = span.offset + span.length;
  }
}

// --- the preview card ----------------------------------------------------------------------------

TEST_CASE("the preview of an event reads as a date, a range and a title") {
  const nlp::ParsedInput out = nlp::ParseInput(L"mañana 5pm dentista", kTue);
  CHECK(nlp::PreviewText(out, kTue.date) == L"📅 Mañana · 17:00–18:00 · Dentista");
}

TEST_CASE("the preview of a task names the weekday") {
  const nlp::ParsedInput out = nlp::ParseInput(L"t: pagar luz el lunes", kTue);
  CHECK(nlp::PreviewText(out, kTue.date) == L"☑ Tarea · Lunes · Pagar luz");
}

TEST_CASE("the preview of something with no date says so") {
  const nlp::ParsedInput out = nlp::ParseInput(L"comprar leche", kTue);
  CHECK(nlp::PreviewText(out, kTue.date) == L"☑ Tarea sin fecha · Comprar leche");
}

TEST_CASE("the preview says a rule repeats without spelling out the RRULE") {
  const nlp::ParsedInput out = nlp::ParseInput(L"gym cada lunes 7am", kTue);
  CHECK(nlp::PreviewText(out, kTue.date) == L"📅 Lunes · 07:00–08:00 · Cada semana · Gym");
}

TEST_CASE("a far away date falls back to the day and the month") {
  const nlp::ParsedInput out = nlp::ParseInput(L"el 25 almuerzo", kDay30);
  CHECK(nlp::PreviewText(out, kDay30.date) == L"☑ Tarea · 25 Oct · Almuerzo");
}
