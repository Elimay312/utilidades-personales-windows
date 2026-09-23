#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

#include "core/hotkey.h"
#include "core/i18n.h"

namespace panel {

// %LOCALAPPDATA%\Panel\panel.json, the one file the user edits by hand.
std::filesystem::path ConfigPath();

// Merges panel.json from the folder holding Panel.exe and then from %LOCALAPPDATA%\Panel, so the
// user's file always wins. Missing files are skipped and a malformed one is logged and ignored.
nlohmann::json LoadConfig();

// Writes a starting panel.json when there is none, so "Abrir panel.json" has something to open.
// Never touches a file that is already there.
void EnsureConfigFile();

struct Preferences {
  std::string hotkey = std::string(kDefaultHotkey);
  Lang lang = Lang::Es;
  std::wstring theme;  // "dark", "light", or empty to follow Windows
};

Preferences ReadPreferences(const nlohmann::json& config);

}  // namespace panel
