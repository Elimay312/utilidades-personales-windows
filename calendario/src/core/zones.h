#pragma once

// Time zones by the names people say them (phase 13): "Colombia", "Madrid", "EST". One table,
// read by the parser ("3pm hora de Madrid"), by the preview's label ("· Colombia"), by the
// settings' second zone and by the weather, which takes a city's latitude and longitude from
// the same row.
//
// The zone arithmetic is <chrono>'s tzdb, which on Windows reads the ICU that ships with the
// system (10 1903 and later). Every call is wrapped: without the database the answer is "I do
// not know", never an exception through a window procedure.

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dates.h"
#include "core/i18n.h"

namespace agenda {

struct ZoneInfo {
  // Folded (core/text.h) and separated by '|'. Several words are one alias: "nueva york".
  std::wstring_view aliases;
  std::string_view iana;
  std::wstring_view es;  // what the preview calls it: the country, or the city where a
  std::wstring_view en;  // country has more than one zone
  double latitude;
  double longitude;
};

// ponytail: a hand-kept table of the places this agenda's owner is likely to say, not every
// zone on Earth; a zone that is not here still shows, by the city in its IANA name.
inline constexpr ZoneInfo kZones[] = {
    {L"colombia|bogota|medellin|cali|barranquilla|cartagena", "America/Bogota", L"Colombia",
     L"Colombia", 4.711, -74.072},
    {L"mexico|ciudad de mexico|cdmx|guadalajara|monterrey", "America/Mexico_City", L"México",
     L"Mexico", 19.433, -99.133},
    {L"cancun", "America/Cancun", L"Cancún", L"Cancún", 21.161, -86.851},
    {L"peru|lima", "America/Lima", L"Perú", L"Peru", -12.046, -77.043},
    {L"ecuador|quito|guayaquil", "America/Guayaquil", L"Ecuador", L"Ecuador", -0.180, -78.468},
    {L"venezuela|caracas", "America/Caracas", L"Venezuela", L"Venezuela", 10.481, -66.904},
    {L"panama", "America/Panama", L"Panamá", L"Panama", 8.983, -79.517},
    {L"costa rica|san jose", "America/Costa_Rica", L"Costa Rica", L"Costa Rica", 9.928, -84.091},
    {L"guatemala", "America/Guatemala", L"Guatemala", L"Guatemala", 14.634, -90.507},
    {L"el salvador|san salvador", "America/El_Salvador", L"El Salvador", L"El Salvador", 13.693,
     -89.218},
    {L"honduras|tegucigalpa", "America/Tegucigalpa", L"Honduras", L"Honduras", 14.072, -87.192},
    {L"nicaragua|managua", "America/Managua", L"Nicaragua", L"Nicaragua", 12.114, -86.236},
    {L"cuba|la habana|habana|havana", "America/Havana", L"Cuba", L"Cuba", 23.113, -82.366},
    {L"republica dominicana|santo domingo|dominican republic", "America/Santo_Domingo",
     L"República Dominicana", L"Dominican Republic", 18.486, -69.931},
    {L"puerto rico|san juan", "America/Puerto_Rico", L"Puerto Rico", L"Puerto Rico", 18.466,
     -66.106},
    {L"bolivia|la paz", "America/La_Paz", L"Bolivia", L"Bolivia", -16.500, -68.150},
    {L"chile|santiago", "America/Santiago", L"Chile", L"Chile", -33.449, -70.669},
    {L"argentina|buenos aires", "America/Argentina/Buenos_Aires", L"Argentina", L"Argentina",
     -34.604, -58.382},
    {L"uruguay|montevideo", "America/Montevideo", L"Uruguay", L"Uruguay", -34.901, -56.165},
    {L"paraguay|asuncion", "America/Asuncion", L"Paraguay", L"Paraguay", -25.264, -57.576},
    {L"brasil|brazil|sao paulo|rio de janeiro", "America/Sao_Paulo", L"Brasil", L"Brazil",
     -23.551, -46.633},
    {L"nueva york|new york|nyc|miami|boston|washington|est|edt", "America/New_York",
     L"Nueva York", L"New York", 40.713, -74.006},
    {L"chicago|houston|dallas|cst|cdt", "America/Chicago", L"Chicago", L"Chicago", 41.878,
     -87.630},
    {L"denver|mst|mdt", "America/Denver", L"Denver", L"Denver", 39.739, -104.990},
    {L"los angeles|san francisco|seattle|california|pst|pdt", "America/Los_Angeles",
     L"Los Ángeles", L"Los Angeles", 34.052, -118.244},
    {L"toronto|montreal|canada", "America/Toronto", L"Toronto", L"Toronto", 43.653, -79.383},
    {L"vancouver", "America/Vancouver", L"Vancouver", L"Vancouver", 49.283, -123.121},
    {L"espana|spain|madrid|barcelona|valencia|sevilla", "Europe/Madrid", L"España", L"Spain",
     40.417, -3.704},
    {L"reino unido|united kingdom|inglaterra|england|uk|londres|london", "Europe/London",
     L"Reino Unido", L"United Kingdom", 51.507, -0.128},
    {L"portugal|lisboa|lisbon", "Europe/Lisbon", L"Portugal", L"Portugal", 38.722, -9.139},
    {L"francia|france|paris|cet|cest", "Europe/Paris", L"Francia", L"France", 48.857, 2.352},
    {L"alemania|germany|berlin|munich", "Europe/Berlin", L"Alemania", L"Germany", 52.520,
     13.405},
    {L"italia|italy|roma|rome|milan", "Europe/Rome", L"Italia", L"Italy", 41.903, 12.496},
    {L"paises bajos|holanda|netherlands|amsterdam", "Europe/Amsterdam", L"Países Bajos",
     L"Netherlands", 52.368, 4.904},
    {L"belgica|belgium|bruselas|brussels", "Europe/Brussels", L"Bélgica", L"Belgium", 50.850,
     4.352},
    {L"suiza|switzerland|zurich|ginebra|geneva", "Europe/Zurich", L"Suiza", L"Switzerland",
     47.377, 8.542},
    {L"irlanda|ireland|dublin", "Europe/Dublin", L"Irlanda", L"Ireland", 53.350, -6.260},
    {L"suecia|sweden|estocolmo|stockholm", "Europe/Stockholm", L"Suecia", L"Sweden", 59.329,
     18.069},
    {L"polonia|poland|varsovia|warsaw", "Europe/Warsaw", L"Polonia", L"Poland", 52.230, 21.012},
    {L"grecia|greece|atenas|athens", "Europe/Athens", L"Grecia", L"Greece", 37.984, 23.728},
    {L"turquia|turkey|estambul|istanbul", "Europe/Istanbul", L"Turquía", L"Turkey", 41.008,
     28.978},
    {L"rusia|russia|moscu|moscow", "Europe/Moscow", L"Rusia", L"Russia", 55.756, 37.617},
    {L"egipto|egypt|el cairo|cairo", "Africa/Cairo", L"Egipto", L"Egypt", 30.044, 31.236},
    {L"sudafrica|south africa|johannesburgo|johannesburg", "Africa/Johannesburg", L"Sudáfrica",
     L"South Africa", -26.204, 28.047},
    {L"nigeria|lagos", "Africa/Lagos", L"Nigeria", L"Nigeria", 6.524, 3.379},
    {L"emiratos|uae|dubai", "Asia/Dubai", L"Emiratos", L"UAE", 25.205, 55.271},
    {L"india|delhi|mumbai|bangalore|ist", "Asia/Kolkata", L"India", L"India", 28.614, 77.209},
    {L"china|pekin|beijing|shanghai", "Asia/Shanghai", L"China", L"China", 31.230, 121.474},
    {L"hong kong", "Asia/Hong_Kong", L"Hong Kong", L"Hong Kong", 22.320, 114.169},
    {L"singapur|singapore", "Asia/Singapore", L"Singapur", L"Singapore", 1.352, 103.820},
    {L"japon|japan|tokio|tokyo|jst", "Asia/Tokyo", L"Japón", L"Japan", 35.676, 139.650},
    {L"corea|korea|seul|seoul", "Asia/Seoul", L"Corea del Sur", L"South Korea", 37.567,
     126.978},
    {L"filipinas|philippines|manila", "Asia/Manila", L"Filipinas", L"Philippines", 14.600,
     120.984},
    {L"indonesia|yakarta|jakarta", "Asia/Jakarta", L"Indonesia", L"Indonesia", -6.208, 106.846},
    {L"tailandia|thailand|bangkok", "Asia/Bangkok", L"Tailandia", L"Thailand", 13.756, 100.502},
    {L"australia|sidney|sydney|melbourne", "Australia/Sydney", L"Sídney", L"Sydney", -33.869,
     151.209},
    {L"nueva zelanda|new zealand|auckland", "Pacific/Auckland", L"Nueva Zelanda", L"New Zealand",
     -36.849, 174.763},
    {L"hawai|hawaii|honolulu", "Pacific/Honolulu", L"Hawái", L"Hawaii", 21.307, -157.858},
    {L"utc|gmt|zulu", "Etc/UTC", L"UTC", L"UTC", 51.477, 0.0},
};

inline const ZoneInfo* ZoneByIana(std::string_view iana) {
  for (const ZoneInfo& zone : kZones) {
    if (zone.iana == iana) return &zone;
  }
  // The same place under its other names: Windows says America/Buenos_Aires or Asia/Calcutta.
  if (iana == "America/Buenos_Aires") return ZoneByIana("America/Argentina/Buenos_Aires");
  if (iana == "Asia/Calcutta") return ZoneByIana("Asia/Kolkata");
  if (iana == "UTC" || iana == "Etc/GMT" || iana == "GMT") return ZoneByIana("Etc/UTC");
  return nullptr;
}

// The zone whose alias starts at words[at], and how many words it took; the longest wins, so
// "nueva york" is not "nueva" and something else. `words` are folded.
inline const ZoneInfo* ZoneAt(const std::vector<std::wstring_view>& words, size_t at,
                              size_t& count) {
  const ZoneInfo* best = nullptr;
  count = 0;
  for (const ZoneInfo& zone : kZones) {
    std::wstring_view rest = zone.aliases;
    while (!rest.empty()) {
      const size_t bar = rest.find(L'|');
      std::wstring_view alias = rest.substr(0, bar);
      rest = bar == std::wstring_view::npos ? std::wstring_view{} : rest.substr(bar + 1);
      size_t used = 0;
      while (!alias.empty() && at + used < words.size()) {
        const size_t space = alias.find(L' ');
        const std::wstring_view word = alias.substr(0, space);
        if (words[at + used] != word) break;
        ++used;
        alias = space == std::wstring_view::npos ? std::wstring_view{} : alias.substr(space + 1);
      }
      if (alias.empty() && used > count) {
        best = &zone;
        count = used;
      }
    }
  }
  return best;
}

// This machine's zone, 'America/Bogota'; empty when the system cannot say.
inline std::string LocalZone() {
  try {
    return std::string(std::chrono::current_zone()->name());
  } catch (...) {
    return {};
  }
}

// What the preview and the detail panel call a zone: "Colombia", "Nueva York". A zone not in the
// table goes by the last part of its name, "America/Argentina/Cordoba" as "Cordoba".
inline std::wstring ZoneLabel(std::string_view iana) {
  if (const ZoneInfo* zone = ZoneByIana(iana)) return std::wstring(English() ? zone->en : zone->es);
  const size_t slash = iana.rfind('/');
  std::wstring out;
  for (const char c : iana.substr(slash == std::string_view::npos ? 0 : slash + 1)) {
    out.push_back(c == '_' ? L' ' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
  }
  return out;
}

// A wall clock in one zone read on the wall of another: 15:00 in Madrid is 08:00 in Bogota.
// Nullopt when either zone is unknown or there is no database.
inline std::optional<std::pair<Date, int>> ConvertWall(Date day, int minute, std::string_view from,
                                                       std::string_view to) {
  using namespace std::chrono;
  try {
    const time_zone* source = locate_zone(from);
    const time_zone* target = locate_zone(to);
    const local_time<minutes> written = local_days{day} + minutes{minute};
    // The hour that happens twice is the first one; the one that never happens walks forward.
    const sys_time<minutes> instant =
        floor<minutes>(source->to_sys(written, choose::earliest));
    const local_time<minutes> there = floor<minutes>(target->to_local(instant));
    const local_days date = floor<days>(there);
    return std::pair<Date, int>{Date{date}, static_cast<int>((there - date).count())};
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace agenda
