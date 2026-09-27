#include "model/config.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

#include "core/jsonc.h"
#include "core/log.h"

namespace dock {
namespace {

std::wstring Widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
  return out;
}

std::wstring Text(const nlohmann::json& object, std::string_view key) {
  const auto* v = Find(object, key);
  return v && v->is_string() ? Widen(v->get<std::string>()) : std::wstring{};
}

// dock.json se escribe con barras normales para no escapar las invertidas, pero
// SHCreateItemFromParsingName rechaza "/" con E_INVALIDARG.
std::wstring Backslashes(std::wstring path) {
  std::replace(path.begin(), path.end(), L'/', L'\\');
  return path;
}

}  // namespace

bool IsUrl(const std::wstring& target) {
  const size_t colon = target.find(L':');
  if (colon == std::wstring::npos || colon < 2) return false;
  for (size_t i = 0; i < colon; i++) {
    const wchar_t c = target[i];
    if (!std::iswalnum(c) && c != L'+' && c != L'.' && c != L'-') return false;
  }
  return _wcsnicmp(target.c_str(), L"shell:", 6) != 0;
}

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

  if (const auto* apps = Find(root, "apps"); apps && apps->is_array()) {
    for (const auto& entry : *apps) {
      DockApp app;
      if (const auto* sep = Find(entry, "separator"); sep && sep->is_boolean()) app.separator = sep->get<bool>();
      if (!app.separator) {
        app.name = Text(entry, "name");
        app.target = Text(entry, "target");
        if (app.target.empty()) {
          LogError(L"[config] '{}' sin target; se ignora", app.name);
          continue;
        }
        if (!IsUrl(app.target)) app.target = Backslashes(app.target);
        app.iconTarget = Backslashes(Text(entry, "iconTarget"));
        app.arguments = Text(entry, "arguments");
      }
      config.apps.push_back(std::move(app));
    }
  }
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
