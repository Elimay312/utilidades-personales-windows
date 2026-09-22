#pragma once

#include <nlohmann/json.hpp>

namespace agenda {

// Merges config.json and then config.local.json, first from the folder holding Agenda.exe and
// then from %LOCALAPPDATA%\Agenda, so the local file always wins. Missing files are skipped and
// a malformed one is logged and ignored.
nlohmann::json LoadConfig();

}  // namespace agenda
