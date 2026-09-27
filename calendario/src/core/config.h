#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

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

// A calendar set (phase 13): a name and which calendars it switches off. Applying it is setting
// every calendar's `hidden` to whether it is in the list, so it is nothing the cache has to know.
struct CalendarSet {
  std::wstring name;
  std::vector<std::string> hidden;  // calendar ids
};
inline constexpr int kMaxCalendarSets = 4;  // what fits in one row of the sidebar

// What the settings window changes and the rest of the app reads. The default calendar is not
// here -- it is the is_primary column, as it has been since phase 5 -- and neither is starting
// with Windows, whose only truth is the Run key.
struct Preferences {
  std::string hotkey = "Alt+Shift+C";
  int durationMin = 60;        // an event with a time and no length
  Lang lang = Lang::Es;
  std::wstring theme;          // "dark", "light", or empty to follow Windows
  // Phase 13: a second column of hours on the timeline, as an IANA zone; empty is none.
  std::string secondZone;
  std::vector<CalendarSet> calendarSets;
};

// The sets as config.local.json keeps them: [{"name": "Trabajo", "hidden": ["id", ...]}].
nlohmann::json WriteCalendarSets(const std::vector<CalendarSet>& sets);

// What the second-zone chooser steps through, after "Ninguna": the zones a person in Colombia
// most often has a meeting in. Any IANA name typed into the config by hand is honoured too.
inline constexpr const char* kSecondZoneChoices[] = {
    "Etc/UTC",          "America/New_York", "America/Chicago",    "America/Los_Angeles",
    "America/Mexico_City", "America/Bogota", "America/Lima",      "America/Santiago",
    "America/Argentina/Buenos_Aires", "America/Sao_Paulo", "Europe/London", "Europe/Madrid",
    "Europe/Paris",     "Europe/Berlin",    "Asia/Dubai",         "Asia/Kolkata",
    "Asia/Shanghai",    "Asia/Tokyo",       "Australia/Sydney"};

inline constexpr int kDurationChoices[] = {30, 45, 60, 90, 120};

Preferences ReadPreferences(const nlohmann::json& config);

}  // namespace agenda
