#pragma once

// Arrancar con Windows: el valor "Dock" bajo HKCU\...\Run, visible en la pestaña Inicio del
// Administrador de tareas. Copiado de Panel. Es el mismo valor que escribía el dock de C#, así
// que el instalador de C++ lo hereda sin más.

#include <windows.h>

#include <filesystem>
#include <string>

#include "core/log.h"
#include "core/paths.h"

namespace dock {

inline constexpr wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
inline constexpr wchar_t kRunValue[] = L"Dock";

// Solo lo toca la copia instalada (%LOCALAPPDATA%\Programs\Dock). Una build de desarrollo
// arrancada con el dock.json del usuario (autoStart: true) se registraría a sí misma y el
// siguiente inicio de sesión abriría esa build en vez del dock de verdad.
inline void SyncAutoStart(bool on) {
  wchar_t exe[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return;
  const std::filesystem::path installed = AppDataDir().parent_path() / L"Programs" / L"Dock";
  if (_wcsnicmp(exe, installed.c_str(), installed.native().size()) != 0) {
    LogInfo(L"[autoarranque] no es la copia instalada; no se toca");
    return;
  }

  const std::wstring command = L"\"" + std::wstring(exe, length) + L"\"";
  wchar_t current[MAX_PATH + 3]{};
  DWORD size = sizeof(current);
  const bool present =
      RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, current, &size) == ERROR_SUCCESS;
  if (on && (!present || command != current)) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, command.c_str(),
                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    LogInfo(L"[autoarranque] encendido: {}", command);
  } else if (!on && present) {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    LogInfo(L"[autoarranque] apagado");
  }
}

}  // namespace dock
