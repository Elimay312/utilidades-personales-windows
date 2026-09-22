#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace agenda {

// %LOCALAPPDATA%\Agenda: logs, cache database and the user's config live here.
inline std::filesystem::path AppDataDir() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
  const std::filesystem::path root = (length > 0 && length < MAX_PATH) ? buffer : L".";
  return root / L"Agenda";
}

// Folder holding Agenda.exe.
inline std::filesystem::path ExeDir() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return L".";
  return std::filesystem::path(buffer).parent_path();
}

inline std::wstring EnvVar(const wchar_t* name) {
  wchar_t buffer[512]{};
  const DWORD length = GetEnvironmentVariableW(name, buffer, 512);
  if (length == 0 || length >= 512) return {};
  return std::wstring(buffer, length);
}

}  // namespace agenda
