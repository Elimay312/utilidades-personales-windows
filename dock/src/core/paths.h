#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace dock {

// %LOCALAPPDATA%\Dock: dock.json, dock.local.json y los logs. Es la misma carpeta que usaba
// el dock de C#, así que la configuración migra sola.
inline std::filesystem::path AppDataDir() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
  const std::filesystem::path root = (length > 0 && length < MAX_PATH) ? buffer : L".";
  return root / L"Dock";
}

inline std::wstring EnvVar(const wchar_t* name) {
  wchar_t buffer[512]{};
  const DWORD length = GetEnvironmentVariableW(name, buffer, 512);
  if (length == 0 || length >= 512) return {};
  return std::wstring(buffer, length);
}

}  // namespace dock
