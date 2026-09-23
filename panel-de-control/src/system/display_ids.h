#pragma once

// Which monitor is which, with nothing but strings (CLAUDE.md, architecture rule 4). WMI names
// the laptop's panel "DISPLAY\AUO7EAD\5&f094e1b&0&UID4355_0"; DisplayConfig names the same
// monitor "\\?\DISPLAY#AUO7EAD#5&f094e1b&0&UID4355#{e6f07b5f-...}". Both carry the device
// instance, and that is what is compared: Monitorian's method.

#include <cwctype>
#include <string>
#include <string_view>

namespace panel {

// "\\?\DISPLAY#hw#inst#{guid}" -> "DISPLAY\hw\inst". Anything else comes back empty.
inline std::wstring InstanceFromDevicePath(std::wstring_view path) {
  constexpr std::wstring_view prefix = L"\\\\?\\";
  if (path.starts_with(prefix)) path.remove_prefix(prefix.size());
  // The interface class GUID at the end is not part of the device.
  if (const size_t guid = path.rfind(L"#{"); guid != std::wstring_view::npos) path = path.substr(0, guid);
  if (path.empty() || path.find(L'#') == std::wstring_view::npos) return {};
  std::wstring out(path);
  for (wchar_t& c : out) {
    if (c == L'#') c = L'\\';
  }
  return out;
}

// WMI's InstanceName without its own "_0" suffix.
inline std::wstring InstanceFromWmi(std::wstring_view name) {
  if (name.size() > 2 && name.ends_with(L"_0")) name.remove_suffix(2);
  return std::wstring(name);
}

inline bool SameInstance(std::wstring_view a, std::wstring_view b) {
  if (a.empty() || a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::towupper(a[i]) != std::towupper(b[i])) return false;
  }
  return true;
}

// DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY: a panel built into the machine, whatever its wiring.
inline bool IsInternalOutput(long technology) {
  constexpr long kInternal = static_cast<long>(0x80000000);
  constexpr long kDisplayPortEmbedded = 11;
  constexpr long kUdiEmbedded = 13;
  return technology == kInternal || technology == kDisplayPortEmbedded || technology == kUdiEmbedded;
}

}  // namespace panel
