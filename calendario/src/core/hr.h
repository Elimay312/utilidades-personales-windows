#pragma once

#include <windows.h>

#include "core/log.h"

namespace agenda {

// COM and Direct2D report everything through HRESULT, and CLAUDE.md forbids throwing across
// Win32 callbacks, so every call site reads: if (Failed(hr, L"What")) return false;
inline bool Failed(HRESULT hr, const wchar_t* what) {
  if (SUCCEEDED(hr)) return false;
  LogError(L"{} failed, hr {:#010x}", what, static_cast<unsigned long>(hr));
  return true;
}

}  // namespace agenda
