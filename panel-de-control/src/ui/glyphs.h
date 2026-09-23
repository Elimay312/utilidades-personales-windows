#pragma once

// Segoe Fluent Icons code points (the same ones exist in Segoe MDL2 Assets, the Windows 10
// fallback). Named here once so the drawing reads as words and not as hex.

namespace panel::glyph {

inline constexpr wchar_t kWifi = 0xE701;
inline constexpr wchar_t kBluetooth = 0xE702;
inline constexpr wchar_t kMoon = 0xE708;        // night light
inline constexpr wchar_t kSettings = 0xE713;
inline constexpr wchar_t kBrightness = 0xE706;
inline constexpr wchar_t kVolume = 0xE767;
inline constexpr wchar_t kMute = 0xE74F;
inline constexpr wchar_t kSpeakers = 0xE7F5;
inline constexpr wchar_t kHeadphones = 0xE7F6;
inline constexpr wchar_t kMonitor = 0xE7F4;
inline constexpr wchar_t kLaptop = 0xE7F8;
inline constexpr wchar_t kChevronRight = 0xE76C;
inline constexpr wchar_t kChevronDown = 0xE70D;
inline constexpr wchar_t kCheck = 0xE73E;
inline constexpr wchar_t kLock = 0xE72E;
inline constexpr wchar_t kOpenElsewhere = 0xE8A7;  // "this opens Windows' own"
inline constexpr wchar_t kKeyboard = 0xE765;
inline constexpr wchar_t kMouse = 0xE962;

// The signal of a Wi-Fi network, by bars: none, one, two, three, full.
inline constexpr wchar_t kWifiBars[] = {0xE871, 0xE872, 0xE873, 0xE874, 0xE701};

// The utilities row.
inline constexpr wchar_t kDock = 0xE8A9;       // a grid of apps
inline constexpr wchar_t kIsla = 0xE8D6;       // music note: what is playing
inline constexpr wchar_t kHud = 0xE995;        // volume with bars
inline constexpr wchar_t kQuickLook = 0xE890;  // eye
inline constexpr wchar_t kLauncher = 0xE721;   // search

}  // namespace panel::glyph
