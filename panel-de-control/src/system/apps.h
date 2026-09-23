#pragma once

// The row of utilities: which of this folder's apps are running, starting one, and asking one
// to close (SEGURIDAD.md 1.5, 1.6 and 2.7). Which apps are in the row comes from `utilidades` in
// panel.json, with the six that live in the background as the default, so a new utility is a
// line of JSON and not a rebuild.
//
// Only an exe named there is ever started, and only if its path is absolute, exists and ends in
// .exe; only a process whose full image path is that exe is ever asked to close, and asking is
// all it is: no TerminateProcess, no forcing.

#include <windows.h>

#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "model/state.h"
#include "system/worker.h"

namespace panel {

inline constexpr UINT kAppsMessage = WM_APP + 16;

struct Utility {
  std::wstring name;
  std::wstring exe;     // environment variables already expanded
  wchar_t glyph = 0;
  // The class of the window that closes the app when it gets WM_CLOSE (SEGURIDAD.md 1.5, amended
  // in phase 7). Empty: the panel can start it but not close it.
  std::wstring window;
};

// The row when panel.json says nothing: the utilities that live in the background, at the
// paths actualizar.ps1 installs them to. %LOCALAPPDATA% is expanded later.
std::vector<Utility> DefaultUtilities();

// `utilidades` from the config: [{"nombre", "exe", "icono": "E8A9", "ventana"}], in order. An entry without
// a name or an exe is skipped; a missing or malformed list gives the defaults. `expand` turns
// %VARIABLES% into their values (ExpandEnvironmentStrings in the app, a stub in the tests).
std::vector<Utility> ReadUtilities(const nlohmann::json& config,
                                   std::wstring (*expand)(std::wstring_view) = nullptr);

// %LOCALAPPDATA% and friends, as the user's environment has them.
std::wstring ExpandVariables(std::wstring_view text);

// SEGURIDAD.md 1.6: absolute (a drive and a backslash, or a UNC path) and ending in .exe,
// whatever the case. Existing on disk is checked where it is used.
bool LooksLikeExe(std::wstring_view path);

// The same file, as Windows compares paths: case does not matter, and / is \.
bool SamePath(std::wstring_view a, std::wstring_view b);

// "E8A9" into its code point; 0 when it is not 1 to 4 hex digits.
wchar_t GlyphFromHex(std::string_view hex);

class Apps {
 public:
  Apps() = default;
  Apps(const Apps&) = delete;
  Apps& operator=(const Apps&) = delete;

  void Start(HWND window, Worker& worker, std::vector<Utility> utilities);
  // Looks at what is running: one snapshot, when the panel opens. Never polled.
  void Refresh();
  void Launch(size_t index);
  void Close(size_t index);

  struct Snapshot {
    std::vector<AppEntry> apps;
    std::wstring problem;
  };
  Snapshot Current();

 private:
  void Look();  // on the worker

  HWND window_ = nullptr;
  Worker* worker_ = nullptr;
  std::vector<Utility> utilities_;

  std::mutex mutex_;
  Snapshot snapshot_;
};

}  // namespace panel
