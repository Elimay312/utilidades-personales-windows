#pragma once

#include <filesystem>

namespace dock {

int RunChecks(const std::filesystem::path& configPath);

}  // namespace dock
