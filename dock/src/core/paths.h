#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace dock {

// El dock.json de ejemplo que va dentro del exe (assets/dock.rc).
inline constexpr int kSeedConfigResource = 2;

// Sus bytes, tal cual están en el repo.
inline std::string_view SeedConfig() {
  const HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(kSeedConfigResource), RT_RCDATA);
  const HGLOBAL loaded = found ? LoadResource(nullptr, found) : nullptr;
  const char* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
  return bytes ? std::string_view(bytes, SizeofResource(nullptr, found)) : std::string_view();
}

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
