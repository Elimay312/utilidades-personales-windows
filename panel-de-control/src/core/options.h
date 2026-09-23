#pragma once

#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace panel {

// Debug always targets \\.\DISPLAY3 (the monitor rule in CLAUDE.md); Release opens on the
// monitor the mouse is on unless --monitor pins one.
#ifdef _DEBUG
inline constexpr int kDefaultMonitor = 3;
#else
inline constexpr int kDefaultMonitor = 0;  // 0 = follow the mouse
#endif

// The views --render-snapshot knows. Named once, so the flag cannot accept a view the renderer
// does not draw.
inline constexpr std::wstring_view kSnapshotViews[] = {
    L"panel",          // as it opens
    L"panel-brillo",   // with the brightness card open: one slider per screen
    L"panel-volumen",  // with the volume card open: the list of outputs
};

inline bool KnowsSnapshotView(std::wstring_view view) {
  return std::find(std::begin(kSnapshotViews), std::end(kSnapshotViews), view) !=
         std::end(kSnapshotViews);
}

struct Options {
  int monitor = kDefaultMonitor;
  std::wstring snapshotView;  // --render-snapshot=<view>, empty when the app should just run
  std::wstring snapshotOut;   // --out=<file.png>, defaults to shot.png
  std::wstring theme;         // --theme=dark|light|contrast, empty follows the config
  std::wstring error;         // non-empty means: log it and exit with code 2
};

// Reads --monitor=N, --render-snapshot=<view>, --out=<file> and --theme, falling back to the
// PANEL_DEV_MONITOR value passed in and then to kDefaultMonitor. argv[0] is skipped and unknown
// arguments are ignored.
Options ParseOptions(int argc, const wchar_t* const* argv, std::wstring_view envMonitor);

// Looks up \\.\DISPLAYn by device name, which is the number Windows shows in Settings. The
// enumeration order is not that number, so never use the index.
std::optional<HMONITOR> FindMonitorByDisplayNumber(int number);

}  // namespace panel
