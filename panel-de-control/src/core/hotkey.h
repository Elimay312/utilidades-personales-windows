#pragma once

#include <windows.h>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace panel {

struct Hotkey {
  UINT mods = 0;
  UINT vk = 0;
};

inline constexpr std::string_view kDefaultHotkey = "Ctrl+Alt+A";

namespace detail {

inline UINT KeyFromName(std::string_view name) {
  if (name.size() == 1) {
    const char c = name.front();
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return static_cast<UINT>(c);
  }
  if (name.size() >= 2 && name.front() == 'F') {
    int number = 0;
    for (const char c : name.substr(1)) {
      if (c < '0' || c > '9') return 0;
      number = number * 10 + (c - '0');
    }
    if (number >= 1 && number <= 24) return VK_F1 + static_cast<UINT>(number - 1);
    return 0;
  }
  if (name == "SPACE") return VK_SPACE;
  if (name == "ENTER" || name == "RETURN") return VK_RETURN;
  if (name == "TAB") return VK_TAB;
  if (name == "ESC" || name == "ESCAPE") return VK_ESCAPE;
  return 0;
}

}  // namespace detail

// Parses "Alt+Shift+C", "Ctrl+Alt+Space" or "Win+F12". Case and spaces around the tokens do
// not matter. Returns nullopt when a token is unknown, when there is no key or when two keys
// are given. The text is ASCII, which is why it is read straight out of the JSON config
// without any widening.
inline std::optional<Hotkey> ParseHotkey(std::string_view text) {
  Hotkey hotkey{MOD_NOREPEAT, 0};

  for (size_t start = 0; start <= text.size();) {
    size_t end = text.find('+', start);
    if (end == std::string_view::npos) end = text.size();

    std::string token(text.substr(start, end - start));
    start = end + 1;
    while (!token.empty() && token.back() == ' ') token.pop_back();
    while (!token.empty() && token.front() == ' ') token.erase(token.begin());
    for (char& c : token) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (token == "CTRL" || token == "CONTROL") {
      hotkey.mods |= MOD_CONTROL;
    } else if (token == "ALT") {
      hotkey.mods |= MOD_ALT;
    } else if (token == "SHIFT") {
      hotkey.mods |= MOD_SHIFT;
    } else if (token == "WIN") {
      hotkey.mods |= MOD_WIN;
    } else if (hotkey.vk != 0) {
      return std::nullopt;  // two keys in one shortcut
    } else if (const UINT vk = detail::KeyFromName(token); vk != 0) {
      hotkey.vk = vk;
    } else {
      return std::nullopt;
    }
  }

  if (hotkey.vk == 0) return std::nullopt;
  return hotkey;
}

}  // namespace panel
