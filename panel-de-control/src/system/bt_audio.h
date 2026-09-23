#pragma once

// Connecting and disconnecting a paired Bluetooth audio device (SEGURIDAD.md 2.6, amended in
// phase 5b-3): what the "Connect" button in Windows' sound settings does. The Bluetooth audio
// driver has two one-shot properties for it, KSPROPERTY_ONESHOT_RECONNECT and _DISCONNECT in
// KSPROPSETID_BtAudio (ksmedia.h); they are sent to the device's audio filters, found among
// KSCATEGORY_AUDIO's by the device's address in their path. Blocking: the worker calls it.

#include <string>
#include <string_view>

namespace panel {

// "34:09:c9:ec:00:ac" as it appears inside a device path: "3409c9ec00ac".
inline std::wstring AddressKey(std::wstring_view address) {
  std::wstring key;
  for (const wchar_t c : address) {
    if (c == L':' || c == L'-') continue;
    key.push_back(static_cast<wchar_t>(towlower(c)));
  }
  return key;
}

// Whether an audio filter's interface path belongs to the Bluetooth device with `key`: it has
// to come from the Bluetooth enumerator, and carry the whole address.
inline bool AudioFilterOf(std::wstring_view path, std::wstring_view key) {
  if (key.size() != 12) return false;
  std::wstring lower(path);
  for (wchar_t& c : lower) c = static_cast<wchar_t>(towlower(c));
  return lower.find(L"bthenum") != std::wstring::npos && lower.find(key) != std::wstring::npos;
}

// True when at least one of the device's audio filters took the request. That the headphones
// then actually connect is theirs to do (they have to be on and in range); the device watcher
// says when they have.
bool SetBluetoothAudioConnected(std::wstring_view address, bool connect);

}  // namespace panel
