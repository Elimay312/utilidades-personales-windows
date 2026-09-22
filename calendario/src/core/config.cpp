#include "core/config.h"

#include <fstream>

#include "core/log.h"
#include "core/paths.h"

namespace agenda {

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

}  // namespace agenda
