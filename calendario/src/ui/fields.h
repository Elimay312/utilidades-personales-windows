#pragma once

// What the detail panel's date and time fields accept, as plain functions the tests can reach.
//
// The shapes a person types into a form first ("25/09", "17:30"), and then anything the natural
// language parser already understands ("mañana", "el viernes", "5pm") -- the same words that
// work in the capsule work here. Whatever is left over after that is a typo, and a typo is
// refused rather than guessed at: the field turns red and nothing is written.

#include <cwctype>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "core/dates.h"
#include "nlp/parser.h"

namespace agenda {

namespace detail {

inline std::wstring_view Trim(std::wstring_view text) {
  while (!text.empty() && std::iswspace(text.front())) text.remove_prefix(1);
  while (!text.empty() && std::iswspace(text.back())) text.remove_suffix(1);
  return text;
}

// Digits up to `max` of them, or nothing.
inline std::optional<int> Digits(std::wstring_view text, size_t max) {
  if (text.empty() || text.size() > max) return std::nullopt;
  int value = 0;
  for (const wchar_t c : text) {
    if (c < L'0' || c > L'9') return std::nullopt;
    value = value * 10 + (c - L'0');
  }
  return value;
}

}  // namespace detail

inline std::wstring DayFieldText(Date date) {
  return std::format(L"{:02}/{:02}/{:04}", static_cast<unsigned>(date.day()),
                     static_cast<unsigned>(date.month()), static_cast<int>(date.year()));
}

inline std::wstring TimeFieldText(int minute) {
  return std::format(L"{:02}:{:02}", minute / 60, minute % 60);
}

// "25/09/2026", "25-9-26", "25/09" (this year), or whatever the parser reads as a day.
inline std::optional<Date> ReadDayField(std::wstring_view text, Date today) {
  text = detail::Trim(text);
  const size_t first = text.find_first_of(L"/-");
  if (first != std::wstring_view::npos) {
    const std::wstring_view rest = text.substr(first + 1);
    const size_t second = rest.find_first_of(L"/-");
    const std::optional<int> day = detail::Digits(text.substr(0, first), 2);
    const std::optional<int> month = detail::Digits(rest.substr(0, second), 2);
    std::optional<int> year = static_cast<int>(today.year());
    if (second != std::wstring_view::npos) year = detail::Digits(rest.substr(second + 1), 4);
    if (!day || !month || !year) return std::nullopt;
    if (*year < 100) *year += 2000;
    const Date date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
                    std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) return std::nullopt;
    return date;
  }
  const nlp::ParsedInput parsed = nlp::ParseInput(text, nlp::Now{today, 0});
  if (!parsed.start || !parsed.title.empty()) return std::nullopt;
  return parsed.start->date;
}

// "17:30", "9:05", "17", or whatever the parser reads as an hour ("5pm"). 24:00 is allowed,
// because it is where an event that ends at midnight ends.
inline std::optional<int> ReadTimeField(std::wstring_view text) {
  text = detail::Trim(text);
  const size_t colon = text.find(L':');
  if (colon != std::wstring_view::npos || detail::Digits(text, 2)) {
    const std::optional<int> hour = detail::Digits(text.substr(0, colon), 2);
    const std::optional<int> minute =
        colon == std::wstring_view::npos ? std::optional<int>{0}
                                         : detail::Digits(text.substr(colon + 1), 2);
    if (!hour || !minute || *minute > 59 || *hour > 24) return std::nullopt;
    if (*hour == 24 && *minute != 0) return std::nullopt;
    return *hour * 60 + *minute;
  }
  // The date does not matter for an hour; an early one keeps "ya pasó" from moving anything.
  const Date anyDay{std::chrono::year{2026}, std::chrono::January, std::chrono::day{1}};
  const nlp::ParsedInput parsed = nlp::ParseInput(text, nlp::Now{anyDay, 0});
  if (!parsed.start || parsed.start->minuteOfDay == nlp::kNoTime || !parsed.title.empty()) {
    return std::nullopt;
  }
  return parsed.start->minuteOfDay;
}

}  // namespace agenda
