#include "model/config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/jsonc.h"
#include "core/log.h"

namespace dock {

DockConfig ParseConfig(std::string_view text) {
  DockConfig config;
  const nlohmann::json root = ParseJsonc(text);
  if (!root.is_object()) {
    LogError(L"[config] dock.json no es JSON válido; valores por defecto");
    return config;
  }
  if (const auto* v = Find(root, "iconSize"); v && v->is_number()) config.iconSize = std::clamp(v->get<int>(), 16, 256);
  if (const auto* v = Find(root, "iconSpacing"); v && v->is_number()) config.iconSpacing = std::clamp(v->get<int>(), 0, 128);
  // [1, 2,5]: por encima, el bulto empuja tanto a los vecinos que el dock se sale de la
  // pantalla; 1 la apaga.
  if (const auto* v = Find(root, "magnification"); v && v->is_number())
    config.magnification = std::clamp(v->get<float>(), 1.0f, 2.5f);
  if (const auto* v = Find(root, "autoHide"); v && v->is_boolean()) config.autoHide = v->get<bool>();
  return config;
}

DockConfig LoadConfig(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    LogInfo(L"[config] no hay {}; valores por defecto", file.wstring());
    return {};
  }
  std::stringstream text;
  text << in.rdbuf();
  return ParseConfig(text.str());
}

}  // namespace dock
