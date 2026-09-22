#pragma once

#include <nlohmann/json.hpp>

#include <string>

#include "core/i18n.h"

namespace agenda {

// Merges config.json and then config.local.json, first from the folder holding Agenda.exe and
// then from %LOCALAPPDATA%\Agenda, so the local file always wins. Missing files are skipped and
// a malformed one is logged and ignored.
nlohmann::json LoadConfig();

// Writes one key into %LOCALAPPDATA%\Agenda\config.local.json, merged over what is already there
// so the Google credentials in the same file survive. That file is the last one LoadConfig
// reads, which is what makes a choice made in the settings window win over anything typed by
// hand elsewhere. False when it could not be written; the caller says so.
bool SaveSetting(const char* key, const nlohmann::json& value);

// What the settings window changes and the rest of the app reads. The default calendar is not
// here -- it is the is_primary column, as it has been since phase 5 -- and neither is starting
// with Windows, whose only truth is the Run key.
struct Preferences {
  std::string hotkey = "Alt+Shift+C";
  int durationMin = 60;        // an event with a time and no length
  Lang lang = Lang::Es;
  std::wstring theme;          // "dark", "light", or empty to follow Windows
};

inline constexpr int kDurationChoices[] = {30, 45, 60, 90, 120};

Preferences ReadPreferences(const nlohmann::json& config);

}  // namespace agenda
