#pragma once

// Starting with Windows: one value under HKCU's Run key, and nothing else. The value is the only
// truth -- there is no copy of the choice in panel.json to disagree with it -- so the menu
// reads it every time it opens, and the uninstaller removing it is all it takes (Agenda's, as is).
//
// SEGURIDAD.md 1.7: written only when "Iniciar con Windows" is ticked, deleted when unticked.

#include <windows.h>

#include <string>

namespace panel {

inline constexpr wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
inline constexpr wchar_t kRunValue[] = L"Panel";

inline bool StartsWithWindows() {
  DWORD size = 0;
  return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr,
                      &size) == ERROR_SUCCESS;
}

// Points at whichever Panel.exe is running, quoted, since %LOCALAPPDATA% can hold a space.
inline bool SetStartWithWindows(bool on) {
  if (!on) {
    const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
  }
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return false;
  const std::wstring command = L"\"" + std::wstring(path, length) + L"\"";
  return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, command.c_str(),
                         static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) ==
         ERROR_SUCCESS;
}

}  // namespace panel
