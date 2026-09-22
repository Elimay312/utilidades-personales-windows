#include "core/config.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <optional>

#include "core/log.h"
#include "core/paths.h"

namespace agenda {
namespace {

// A missing file is an empty object; one that does not parse is nothing at all.
std::optional<nlohmann::json> ReadJsonFile(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) return nlohmann::json::object();
  nlohmann::json parsed = nlohmann::json::parse(stream, nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
  return parsed;
}

}  // namespace

nlohmann::json LoadConfig() {
  nlohmann::json config = nlohmann::json::object();

  for (const std::filesystem::path& dir : {ExeDir(), AppDataDir()}) {
    for (const wchar_t* name : {L"config.json", L"config.local.json"}) {
      const std::filesystem::path file = dir / name;
      std::ifstream stream(file, std::ios::binary);
      if (!stream) continue;

      const nlohmann::json parsed = nlohmann::json::parse(stream, nullptr, /*allow_exceptions=*/false);
      if (parsed.is_discarded()) {
        LogError(L"config: {} is not valid JSON, ignored", file.wstring());
        continue;
      }
      config.merge_patch(parsed);
      LogInfo(L"config: loaded {}", file.wstring());
    }
  }
  return config;
}

bool SaveSetting(const char* key, const nlohmann::json& value) {
  const std::filesystem::path dir = AppDataDir();
  const std::filesystem::path file = dir / L"config.local.json";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  std::optional<nlohmann::json> config = ReadJsonFile(file);
  if (!config) {
    // A hand-edited file that no longer parses holds the credentials too. Writing over it
    // would lose them for a theme change, so nothing is written and the log says why.
    LogError(L"config: {} is not valid JSON, the setting was not saved", file.wstring());
    return false;
  }
  (*config)[key] = value;

  // Written beside it and swapped in, so a crash halfway never leaves half a file where the
  // client secret used to be.
  const std::filesystem::path temp = dir / L"config.local.json.tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << config->dump(2);
    if (!out) return false;
  }
  if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    LogError(L"config: could not replace {} (error {})", file.wstring(), GetLastError());
    return false;
  }
  return true;
}

Preferences ReadPreferences(const nlohmann::json& config) {
  Preferences out;
  // nlohmann throws when value() meets the wrong type, and a hand-edited config is exactly
  // where that happens, so every read checks the type first and keeps the default otherwise.
  const auto text = [&config](const char* key) -> std::string {
    const auto found = config.find(key);
    return found != config.end() && found->is_string() ? found->get<std::string>() : std::string();
  };
  if (const std::string hotkey = text("hotkey"); !hotkey.empty()) out.hotkey = hotkey;
  if (const auto found = config.find("defaultDuration");
      found != config.end() && found->is_number_integer()) {
    const int minutes = found->get<int>();
    if (std::find(std::begin(kDurationChoices), std::end(kDurationChoices), minutes) !=
        std::end(kDurationChoices)) {
      out.durationMin = minutes;
    }
  }
  out.lang = text("language") == "en" ? Lang::En : Lang::Es;
  if (const std::string theme = text("theme"); theme == "dark" || theme == "light") {
    out.theme = std::wstring(theme.begin(), theme.end());
  }
  return out;
}

}  // namespace agenda
