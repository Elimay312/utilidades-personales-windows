#include "core/jsonc.h"

#include <cctype>

namespace dock {

std::string StripJsonc(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool inString = false;
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    if (inString) {
      out += c;
      if (c == '\\' && i + 1 < text.size()) out += text[++i];
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') {
      inString = true;
      out += c;
    } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n') i++;
      if (i < text.size()) out += '\n';
    } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      i += 2;
      while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) i++;
      i++;
    } else {
      out += c;
    }
  }

  // Segunda pasada, ya sin comentarios: una coma seguida solo de espacios y de } o ] sobra.
  std::string clean;
  clean.reserve(out.size());
  inString = false;
  for (size_t i = 0; i < out.size(); i++) {
    const char c = out[i];
    if (inString) {
      clean += c;
      if (c == '\\' && i + 1 < out.size()) clean += out[++i];
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') inString = true;
    if (c == ',') {
      size_t next = i + 1;
      while (next < out.size() && std::isspace(static_cast<unsigned char>(out[next]))) next++;
      if (next < out.size() && (out[next] == '}' || out[next] == ']')) continue;
    }
    clean += c;
  }
  return clean;
}

nlohmann::json ParseJsonc(std::string_view text) {
  return nlohmann::json::parse(StripJsonc(text), nullptr, /*allow_exceptions=*/false);
}

const nlohmann::json* Find(const nlohmann::json& object, std::string_view key) {
  if (!object.is_object()) return nullptr;
  for (auto it = object.begin(); it != object.end(); ++it) {
    const std::string& name = it.key();
    if (name.size() != key.size()) continue;
    bool same = true;
    for (size_t i = 0; i < name.size() && same; i++)
      same = std::tolower(static_cast<unsigned char>(name[i])) == std::tolower(static_cast<unsigned char>(key[i]));
    if (same) return &it.value();
  }
  return nullptr;
}

}  // namespace dock
