#include "sync/weather.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "core/log.h"
#include "core/paths.h"
#include "sync/http.h"

namespace agenda {
namespace {

constexpr auto kEvery = std::chrono::hours{3};

std::filesystem::path CacheFile() { return AppDataDir() / L"weather.json"; }

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// What was kept on disk, if it is for this same city: its days and when it was asked for.
bool LoadCache(const WeatherCity& city, std::vector<DayWeather>& days, std::int64_t& fetched) {
  std::ifstream file(CacheFile(), std::ios::binary);
  if (!file) return false;
  std::stringstream text;
  text << file.rdbuf();
  const nlohmann::json root = nlohmann::json::parse(text.str(), nullptr, false);
  if (!root.is_object()) return false;
  const auto number = [&](const char* key) {
    const auto found = root.find(key);
    return found != root.end() && found->is_number() ? found->get<double>() : 1e9;
  };
  // Another city's weather is no weather at all.
  if (std::abs(number("latitude_asked") - city.latitude) > 0.01 ||
      std::abs(number("longitude_asked") - city.longitude) > 0.01) {
    return false;
  }
  days = ReadWeather(text.str());
  fetched = static_cast<std::int64_t>(number("fetched_at"));
  return !days.empty();
}

}  // namespace

void Weather::Start(HWND notify, const WeatherCity& city) {
  Stop();
  notify_ = notify;
  stop_ = false;
  thread_ = std::thread([this, city] { Run(city); });
}

void Weather::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    // A request mid-retry would hold the join for as long as its back-off lasts.
    if (http_ != nullptr) http_->Cancel();
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  days_.clear();
}

std::vector<DayWeather> Weather::Days() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return days_;
}

void Weather::Run(WeatherCity city) {
  std::vector<DayWeather> cached;
  std::int64_t fetched = 0;
  if (LoadCache(city, cached, fetched)) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      days_ = std::move(cached);
    }
    PostMessageW(notify_, kWeatherMessage, 0, 0);
  }

  sync::Http http;
  if (!http.Open(/*decompress=*/false)) {
    LogError(L"clima: no se pudo abrir WinHTTP");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) return;
    http_ = &http;
  }
  // The first fetch waits for whatever is left of the three hours the copy on disk is good for.
  auto wait = std::chrono::seconds{(std::max)(std::int64_t{0}, fetched + 3 * 3600 - NowSeconds())};
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (wake_.wait_for(lock, wait, [this] { return stop_; })) break;
    }
    wait = kEvery;

    // The only thing sent: the latitude and the longitude of the city.
    const std::wstring path = std::format(
        L"/v1/forecast?latitude={:.3f}&longitude={:.3f}"
        L"&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=16",
        city.latitude, city.longitude);
    sync::HttpResponse response;
    // Uncompressed on purpose: see Http::Open.
    if (!http.Send(L"api.open-meteo.com", L"GET", path, L"Accept-Encoding: identity\r\n", {},
                   response) ||
        response.status != 200) {
      LogInfo(L"clima: Open-Meteo no respondió ({}), se reintenta en 3 h", response.status);
      continue;
    }
    std::vector<DayWeather> days = ReadWeather(response.body);
    if (days.empty()) continue;
    LogInfo(L"clima: {} días de Open-Meteo para {}", days.size(), city.name);

    nlohmann::json root = nlohmann::json::parse(response.body, nullptr, false);
    if (root.is_object()) {
      root["latitude_asked"] = city.latitude;
      root["longitude_asked"] = city.longitude;
      root["fetched_at"] = NowSeconds();
      std::ofstream file(CacheFile(), std::ios::binary | std::ios::trunc);
      file << root.dump();
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      days_ = std::move(days);
    }
    PostMessageW(notify_, kWeatherMessage, 0, 0);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    http_ = nullptr;
  }
  http.Close();
}

}  // namespace agenda
