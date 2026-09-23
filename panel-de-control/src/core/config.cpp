#include "core/config.h"

#include <windows.h>

#include <fstream>

#include "core/log.h"
#include "core/paths.h"

namespace panel {

std::filesystem::path ConfigPath() { return AppDataDir() / L"panel.json"; }

nlohmann::json LoadConfig() {
  nlohmann::json config = nlohmann::json::object();

  for (const std::filesystem::path& file : {ExeDir() / L"panel.json", ConfigPath()}) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) continue;

    const nlohmann::json parsed = nlohmann::json::parse(stream, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
      LogError(L"config: {} is not valid JSON, ignored", file.wstring());
      continue;
    }
    config.merge_patch(parsed);
    LogInfo(L"config: loaded {}", file.wstring());
  }
  return config;
}

void EnsureConfigFile() {
  const std::filesystem::path file = ConfigPath();
  std::error_code ec;
  if (std::filesystem::exists(file, ec)) return;
  std::filesystem::create_directories(file.parent_path(), ec);

  // Same keys and defaults as panel.example.json. Written once; after that it is the user's.
  std::ofstream out(file, std::ios::binary);
  out << "{\n"
         "  \"hotkey\": \"" << kDefaultHotkey << "\",\n"
         "  \"language\": \"es\",\n"
         "  \"theme\": \"\"\n"
         "}\n";
  if (!out) LogError(L"config: could not write {}", file.wstring());
}

Preferences ReadPreferences(const nlohmann::json& config) {
  Preferences out;
  // nlohmann throws when value() meets the wrong type, and a hand-edited file is exactly where
  // that happens, so every read checks the type first and keeps the default otherwise.
  const auto text = [&config](const char* key) -> std::string {
    const auto found = config.find(key);
    return found != config.end() && found->is_string() ? found->get<std::string>() : std::string();
  };
  if (const std::string hotkey = text("hotkey"); !hotkey.empty()) out.hotkey = hotkey;
  out.lang = text("language") == "en" ? Lang::En : Lang::Es;
  if (const std::string theme = text("theme"); theme == "dark" || theme == "light") {
    out.theme = std::wstring(theme.begin(), theme.end());
  }
  return out;
}

}  // namespace panel
