#pragma once

// The weather (phase 13): the day's sky and its highest temperature, next to the day. From
// Open-Meteo, which needs no key and no account; what leaves this PC is the latitude and the
// longitude of the city chosen in the settings, and nothing else -- not the agenda, not who.
//
// The reading of the answer lives here, inline, so the tests reach it without a network. The
// thread that fetches it is weather.cpp.

#include <windows.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/dates.h"
#include "data/model.h"

namespace agenda {

namespace sync {
class Http;
}

// The popup and the app hear about new weather with this, and ask Weather::Days() for it.
inline constexpr UINT kWeatherMessage = WM_APP + 8;

// What the icon draws: the WMO code of the day folded into the seven skies worth telling apart.
enum class Sky { Clear, PartlyCloudy, Cloudy, Fog, Rain, Snow, Storm };

inline Sky SkyOf(int wmo) {
  if (wmo <= 0) return Sky::Clear;
  if (wmo <= 2) return Sky::PartlyCloudy;
  if (wmo == 3) return Sky::Cloudy;
  if (wmo == 45 || wmo == 48) return Sky::Fog;
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return Sky::Snow;
  if (wmo >= 95) return Sky::Storm;
  return Sky::Rain;  // drizzle, rain, freezing rain and showers: 51 to 67 and 80 to 82
}

struct DayWeather {
  Date day{};
  Sky sky = Sky::Clear;
  int high = 0;  // °C, rounded
  int low = 0;
};

inline const DayWeather* WeatherOn(const std::vector<DayWeather>& days, Date day) {
  for (const DayWeather& weather : days) {
    if (weather.day == day) return &weather;
  }
  return nullptr;
}

// Open-Meteo's `daily` block: parallel arrays of days, codes and temperatures. Anything that
// does not read is left out rather than guessed.
inline std::vector<DayWeather> ReadWeather(std::string_view json) {
  std::vector<DayWeather> out;
  const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
  if (!root.is_object()) return out;
  const auto daily = root.find("daily");
  if (daily == root.end() || !daily->is_object()) return out;
  const auto array = [&](const char* key) -> const nlohmann::json* {
    const auto found = daily->find(key);
    return found != daily->end() && found->is_array() ? &*found : nullptr;
  };
  const nlohmann::json* days = array("time");
  const nlohmann::json* codes = array("weather_code");
  const nlohmann::json* highs = array("temperature_2m_max");
  const nlohmann::json* lows = array("temperature_2m_min");
  if (days == nullptr || codes == nullptr || highs == nullptr || lows == nullptr) return out;
  for (size_t i = 0; i < days->size(); ++i) {
    if (i >= codes->size() || i >= highs->size() || i >= lows->size()) break;
    const nlohmann::json& day = (*days)[i];
    if (!day.is_string() || !(*codes)[i].is_number() || !(*highs)[i].is_number() ||
        !(*lows)[i].is_number()) {
      continue;
    }
    const std::optional<Date> date = ParseDayKey(day.get<std::string>());
    if (!date) continue;
    out.push_back(DayWeather{*date, SkyOf((*codes)[i].get<int>()),
                             static_cast<int>(std::lround((*highs)[i].get<double>())),
                             static_cast<int>(std::lround((*lows)[i].get<double>()))});
  }
  return out;
}

// Where the weather is asked for. A city and not the zone's capital: Medellín is ten degrees
// warmer than Bogotá in the same zone. The first ones are the owner's country.
struct WeatherCity {
  std::wstring_view name;
  double latitude;
  double longitude;
  std::string_view zone;  // the IANA zone it is in: the default is the first city in this one
};

inline constexpr WeatherCity kWeatherCities[] = {
    {L"Bogotá", 4.711, -74.072, "America/Bogota"},
    {L"Medellín", 6.244, -75.581, "America/Bogota"},
    {L"Cali", 3.452, -76.532, "America/Bogota"},
    {L"Barranquilla", 10.964, -74.796, "America/Bogota"},
    {L"Cartagena", 10.391, -75.479, "America/Bogota"},
    {L"Bucaramanga", 7.119, -73.122, "America/Bogota"},
    {L"Pereira", 4.813, -75.696, "America/Bogota"},
    {L"Santa Marta", 11.240, -74.199, "America/Bogota"},
    {L"Ciudad de México", 19.433, -99.133, "America/Mexico_City"},
    {L"Lima", -12.046, -77.043, "America/Lima"},
    {L"Quito", -0.180, -78.468, "America/Guayaquil"},
    {L"Caracas", 10.481, -66.904, "America/Caracas"},
    {L"Panamá", 8.983, -79.517, "America/Panama"},
    {L"Santiago", -33.449, -70.669, "America/Santiago"},
    {L"Buenos Aires", -34.604, -58.382, "America/Argentina/Buenos_Aires"},
    {L"São Paulo", -23.551, -46.633, "America/Sao_Paulo"},
    {L"Miami", 25.762, -80.192, "America/New_York"},
    {L"Nueva York", 40.713, -74.006, "America/New_York"},
    {L"Los Ángeles", 34.052, -118.244, "America/Los_Angeles"},
    {L"Madrid", 40.417, -3.704, "Europe/Madrid"},
    {L"Barcelona", 41.385, 2.173, "Europe/Madrid"},
    {L"Londres", 51.507, -0.128, "Europe/London"},
    {L"París", 48.857, 2.352, "Europe/Paris"},
    {L"Berlín", 52.520, 13.405, "Europe/Berlin"},
    {L"Tokio", 35.676, 139.650, "Asia/Tokyo"},
};
inline constexpr int kWeatherCityCount = static_cast<int>(std::size(kWeatherCities));

// The city a name stands for, or else the first one in `zone`, or else Bogotá.
inline int WeatherCityIndex(std::wstring_view name, std::string_view zone) {
  for (int i = 0; i < kWeatherCityCount; ++i) {
    if (kWeatherCities[i].name == name) return i;
  }
  for (int i = 0; i < kWeatherCityCount; ++i) {
    if (kWeatherCities[i].zone == zone) return i;
  }
  return 0;
}

// The fetch: a thread that asks Open-Meteo on start (unless the copy on disk is fresh) and
// every three hours after, keeps the answer in %LOCALAPPDATA%\Agenda\weather.json so the next
// start has something to show at once, and posts kWeatherMessage to `notify`.
class Weather {
 public:
  Weather() = default;
  ~Weather() { Stop(); }
  Weather(const Weather&) = delete;
  Weather& operator=(const Weather&) = delete;

  void Start(HWND notify, const WeatherCity& city);
  void Stop();
  std::vector<DayWeather> Days() const;

 private:
  void Run(WeatherCity city);

  HWND notify_ = nullptr;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool stop_ = false;
  sync::Http* http_ = nullptr;  // the request in flight, so Stop can cut it short
  std::vector<DayWeather> days_;
};

}  // namespace agenda
