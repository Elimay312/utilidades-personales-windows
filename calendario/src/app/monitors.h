#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace agenda {

// Debug always targets \\.\DISPLAY3 (see the monitor rule in CLAUDE.md); Release uses whatever
// monitor Windows calls primary unless --monitor says otherwise.
#ifdef _DEBUG
inline constexpr int kDefaultMonitor = 3;
#else
inline constexpr int kDefaultMonitor = 0;  // 0 = primary
#endif

struct Options {
  int monitor = kDefaultMonitor;
  std::wstring error;  // non-empty means: log it and exit with code 2
};

// Reads --monitor=N, falling back to the AGENDA_DEV_MONITOR value passed in and then to
// kDefaultMonitor. argv[0] is skipped and unknown arguments are ignored.
Options ParseOptions(int argc, const wchar_t* const* argv, std::wstring_view envMonitor);

// Looks up \\.\DISPLAYn by device name, which is the number Windows shows in Settings. The
// enumeration order is not that number, so never use the index.
std::optional<HMONITOR> FindMonitorByDisplayNumber(int number);

}  // namespace agenda
