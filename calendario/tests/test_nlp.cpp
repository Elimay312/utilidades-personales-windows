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

// --- morning or afternoon ---------------------------------------------------------------------

// What happened on the day the reminders were tried: "Prueba 4:05" typed at 16:00 became four
// in the morning, already gone, and nothing ever said a word.
const nlp::Now kFour{Ymd(2026, 9, 22), 16 * 60};

TEST_CASE("an hour and minutes with no am or pm goes by the eight to twenty rule") {
  const nlp::ParsedInput out = nlp::ParseInput(L"prueba 4:05", kFour);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"16:05");
  // The morning one had gone by, so there is nothing to ask.
  CHECK(out.otherMinute == nlp::kNoTime);
}

TEST_CASE("a leading zero or a twenty four hour clock says which half it means") {
  CHECK(Clock(nlp::ParseInput(L"hoy 04:05 prueba", kFour).start) == L"04:05");
  CHECK(nlp::ParseInput(L"hoy 04:05 prueba", kFour).otherMinute == nlp::kNoTime);
  CHECK(Clock(nlp::ParseInput(L"mañana 16:05 prueba", kFour).start) == L"16:05");
  CHECK(nlp::ParseInput(L"mañana 16:05 prueba", kFour).otherMinute == nlp::kNoTime);
  CHECK(Clock(nlp::ParseInput(L"mañana 4:05 pm prueba", kFour).start) == L"16:05");
}

TEST_CASE("today at nine said at ten means tonight, not this morning") {
  const nlp::ParsedInput out = nlp::ParseInput(L"hoy a las 9 cena", kTue);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"21:00");
  CHECK(out.otherMinute == nlp::kNoTime);
  // And without the "hoy" too: the next nine is tonight, not tomorrow morning.
  CHECK(Clock(nlp::ParseInput(L"a las 9 cena", kTue).start) == L"21:00");
  CHECK(When(nlp::ParseInput(L"a las 9 cena", kTue).start) == L"2026-09-22");
}

TEST_CASE("another day keeps the guess and offers the other half") {
  const nlp::ParsedInput out = nlp::ParseInput(L"el viernes a las 5 dentista", kTue);
  CHECK(Clock(out.start) == L"17:00");
  CHECK(out.otherMinute == 5 * 60);

  const nlp::ParsedInput flipped = nlp::Flipped(out);
  CHECK(Clock(flipped.start) == L"05:00");
  CHECK(Clock(flipped.end) == L"06:00");
  CHECK(flipped.otherMinute == 17 * 60);
  CHECK(When(flipped.start) == When(out.start));
}

TEST_CASE("both halves still ahead today is still a question") {
  const nlp::Now early{Ymd(2026, 9, 22), 3 * 60};
  const nlp::ParsedInput out = nlp::ParseInput(L"a las 5 gimnasio", early);
  CHECK(When(out.start) == L"2026-09-22");
  CHECK(Clock(out.start) == L"17:00");
  CHECK(out.otherMinute == 5 * 60);
}

TEST_CASE("an hour whose two halves are gone moves to tomorrow and asks there") {
  const nlp::ParsedInput out = nlp::ParseInput(L"a las 5 gimnasio", kLate);
  CHECK(When(out.start) == L"2026-09-23");
  CHECK(Clock(out.start) == L"17:00");
  CHECK(out.otherMinute == 5 * 60);
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

// --- phase 9: more ways of writing a range and a length ----------------------------------------

TEST_CASE("a range with a dash takes its half of the day from the end that says it") {
  nlp::ParsedInput out = nlp::ParseInput(L"reunion 3-5pm", kTue);
  CHECK(out.kind == nlp::Kind::Event);
  CHECK(Clock(out.start) == L"15:00");
  CHECK(Clock(out.end) == L"17:00");
  CHECK(out.title == L"reunion");
  // Unless that would start after it ends: eleven in the morning to one.
  out = nlp::ParseInput(L"11-1pm turno", kTue);
  CHECK(Clock(out.start) == L"11:00");
  CHECK(Clock(out.end) == L"13:00");
  out = nlp::ParseInput(L"10:00-11:30 standup", kTue);
  CHECK(Clock(out.start) == L"10:00");
  CHECK(Clock(out.end) == L"11:30");
  CHECK(out.title == L"standup");
}

TEST_CASE("a range said in words, with the half of the day at the end") {
  nlp::ParsedInput out = nlp::ParseInput(L"de 3 a 5 de la tarde clase", kTue);
  CHECK(Clock(out.start) == L"15:00");
  CHECK(Clock(out.end) == L"17:00");
  CHECK(out.title == L"clase");
  out = nlp::ParseInput(L"taller a las 3 hasta las 5", kTue);
  CHECK(Clock(out.start) == L"15:00");
  CHECK(Clock(out.end) == L"17:00");
  CHECK(out.title == L"taller");
  out = nlp::ParseInput(L"visita entre las 3 y las 5", kTue);
  CHECK(Clock(out.start) == L"15:00");
  CHECK(out.durationMin == 120);
  CHECK(out.title == L"visita");
  // Seven to nine is the morning: seven in the evening would end before it starts.
  out = nlp::ParseInput(L"de 7 a 9 desayuno", kTue);
  CHECK(Clock(out.start) == L"07:00");
  CHECK(Clock(out.end) == L"09:00");
}

TEST_CASE("two naked numbers with nothing in front are not a time") {
  nlp::ParsedInput out = nlp::ParseInput(L"comprar 3 a 5 manzanas", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.title == L"comprar 3 a 5 manzanas");
  out = nlp::ParseInput(L"precio 3-5", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(out.title == L"precio 3-5");
}

TEST_CASE("an hour and a half, written five ways") {
  for (const wchar_t* text :
       {L"a las 3 reunion por 1h30", L"a las 3 reunion por 1.5h", L"a las 3 reunion por 1,5 horas",
        L"a las 3 reunion por una hora y media", L"at 3pm reunion for an hour and a half"}) {
    const nlp::ParsedInput out = nlp::ParseInput(text, kTue);
    CHECK(out.durationMin == 90);
    CHECK(Clock(out.end) == L"16:30");
    CHECK(out.title == L"reunion");
  }
  const nlp::ParsedInput half = nlp::ParseInput(L"a las 3 llamada por media hora", kTue);
  CHECK(half.durationMin == 30);
  CHECK(half.title == L"llamada");
}

TEST_CASE("seventeen h thirty is half past five in the afternoon") {
  const nlp::ParsedInput out = nlp::ParseInput(L"cine 17h30", kTue);
  CHECK(Clock(out.start) == L"17:30");
  CHECK(out.otherMinute == nlp::kNoTime);
  CHECK(out.title == L"cine");
}

// --- phase 9: dates with their month, numeric, relative -----------------------------------------

TEST_CASE("a day with its month, in Spanish and in English") {
  nlp::ParsedInput out = nlp::ParseInput(L"almuerzo 25 de octubre", kTue);
  CHECK(out.kind == nlp::Kind::Task);
  CHECK(When(out.start) == L"2026-10-25");
  CHECK(out.title == L"almuerzo");
  out = nlp::ParseInput(L"cena el 25 de oct a la 1pm", kTue);
  CHECK(When(out.start) == L"2026-10-25");
  CHECK(Clock(out.start) == L"13:00");
  CHECK(out.title == L"cena");
  out = nlp::ParseInput(L"dinner October 25", kTue);
  CHECK(When(out.start) == L"2026-10-25");
  CHECK(out.title == L"dinner");
  out = nlp::ParseInput(L"party on Oct 25th at 7pm", kTue);
  CHECK(When(out.start) == L"2026-10-25");
  CHECK(Clock(out.start) == L"19:00");
  CHECK(out.title == L"party");
  out = nlp::ParseInput(L"the 3rd of November meeting", kTue);
  CHECK(When(out.start) == L"2026-11-03");
  CHECK(out.title == L"meeting");
}

TEST_CASE("a date that has gone by this year is next year's, unless the year is written") {
  nlp::ParsedInput out = nlp::ParseInput(L"viaje 5 de septiembre", kTue);
  CHECK(When(out.start) == L"2027-09-05");
  out = nlp::ParseInput(L"cumple 25 de octubre de 2027", kTue);
  CHECK(When(out.start) == L"2027-10-25");
  CHECK(out.title == L"cumple");
  out = nlp::ParseInput(L"boda 29 de febrero", kTue);
  CHECK(When(out.start) == L"2028-02-29");
}

TEST_CASE("a numeric date is day then month, as it is written here") {
  nlp::ParsedInput out = nlp::ParseInput(L"pagar 25/10", kTue);
  CHECK(When(out.start) == L"2026-10-25");
  CHECK(out.title == L"pagar");
  out = nlp::ParseInput(L"renovar 25/10/2027 a las 9", kTue);
  CHECK(When(out.start) == L"2027-10-25");
  CHECK(Clock(out.start) == L"09:00");
  out = nlp::ParseInput(L"renovar 1/2/27", kTue);
  CHECK(When(out.start) == L"2027-02-01");
  // A day that does not exist is not a date, and stays in the title.
  out = nlp::ParseInput(L"31/02 nada", kTue);
  CHECK(When(out.start) == L"sin fecha");
  CHECK(out.title == L"31/02 nada");
}

TEST_CASE("weeks and months from now, and within") {
  nlp::ParsedInput out = nlp::ParseInput(L"dentista en 2 semanas", kTue);
  CHECK(When(out.start) == L"2026-10-06");
  CHECK(out.title == L"dentista");
  out = nlp::ParseInput(L"llamar dentro de 3 días", kTue);
  CHECK(When(out.start) == L"2026-09-25");
  CHECK(out.title == L"llamar");
  out = nlp::ParseInput(L"call in a week", kTue);
  CHECK(When(out.start) == L"2026-09-29");
  out = nlp::ParseInput(L"revisión en un mes", kTue);
  CHECK(When(out.start) == L"2026-10-22");
  // A month after the 31st of January is the last day of February.
  out = nlp::ParseInput(L"x en 1 mes", nlp::Now{Ymd(2027, 1, 31), 9 * 60});
  CHECK(When(out.start) == L"2027-02-28");
}

TEST_CASE("the end of the month and the weekend") {
  nlp::ParsedInput out = nlp::ParseInput(L"informe fin de mes", kTue);
  CHECK(When(out.start) == L"2026-09-30");
  CHECK(out.title == L"informe");
  out = nlp::ParseInput(L"report end of the month", kTue);
  CHECK(When(out.start) == L"2026-09-30");
  out = nlp::ParseInput(L"paseo este fin de semana", kTue);
  CHECK(When(out.start) == L"2026-09-26");
  CHECK(out.title == L"paseo");
  out = nlp::ParseInput(L"hike this weekend", kTue);
  CHECK(When(out.start) == L"2026-09-26");
  CHECK(out.title == L"hike");
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
