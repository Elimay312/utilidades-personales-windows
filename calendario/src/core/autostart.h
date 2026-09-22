#pragma once

// Starting with Windows: one value under HKCU's Run key, and nothing else. The value is the only
// truth -- there is no copy of the choice in the config to disagree with it -- so the settings
// switch reads it every time it is drawn, and the uninstaller removing it is all it takes.
//
// It is the one registry write in Agenda, and the user approved it for phase 7: it is written
// only when the switch is turned on and deleted when it is turned off.

#include <windows.h>

#include <string>

namespace agenda {

inline constexpr wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
inline constexpr wchar_t kRunValue[] = L"Agenda";

inline bool StartsWithWindows() {
  DWORD size = 0;
  return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr,
                      &size) == ERROR_SUCCESS;
}

// Points at whichever Agenda.exe is running, quoted, since %LOCALAPPDATA% can hold a space.
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

}  // namespace agenda
