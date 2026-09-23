#pragma once

// Everything the panel shows, and nothing about how it is drawn. Fixed in phase 1 with every
// field the later phases need (CLAUDE.md, architecture rule 1): each phase fills a part of it
// with real data instead of reshaping it, so the drawing never has to change for a new source.

#include <string>
#include <vector>

namespace panel {

// One network in the unfolded Wi-Fi card (phase 5b).
struct WifiNetwork {
  std::wstring ssid;
  int bars = 0;           // 0..4, the signal as Windows draws it
  bool secured = true;
  bool saved = false;     // Windows has its profile: connecting needs no password
  bool connected = false;
};

struct WifiState {
  bool available = true;  // there is a Wi-Fi radio at all
  bool on = false;
  std::wstring ssid;      // empty when on but not connected
  // Added in phase 5b, only filled while the card is unfolded.
  std::vector<WifiNetwork> networks;
  bool scanning = false;
  bool locationDenied = false;  // the user said no: the card says why it lists nothing
};

// One paired device in the unfolded Bluetooth card (phase 5b).
struct BluetoothDevice {
  enum class Kind { Audio, Keyboard, Mouse, Other };
  std::wstring id;
  std::wstring name;
  Kind kind = Kind::Other;
  bool connected = false;
  bool busy = false;  // asked to connect or disconnect, and not there yet
};

struct BluetoothState {
  bool available = true;
  bool on = false;
  int connected = 0;      // classic and LE together, each device counted once
  std::vector<BluetoothDevice> devices;  // phase 5b: the paired ones
};

struct NightLightState {
  bool supported = true;  // false when the registry blob is not one we understand
  bool on = false;
  bool scheduled = false;
  int fromMinute = 21 * 60;  // minutes after midnight, for the subtitle
  int toMinute = 7 * 60;
};

struct DisplayState {
  std::wstring name;       // what the row says: "Portátil", "DELL U2719D"...
  std::wstring id;         // DISPLAY\hw\inst, what WMI and DisplayConfig agree on
  float level = 0.0f;      // 0..1
  bool internal = false;   // WMI when true, DDC/CI when false
  bool reachable = true;   // false: no brightness control on this one
  bool here = false;       // the monitor the panel opened on
  std::wstring device;     // \\.\DISPLAYn, to tell which one that is (phase 4b)
};

struct AudioOutput {
  std::wstring id;         // the IMMDevice id, only ever passed back to Windows
  std::wstring name;
  bool headphones = false; // picks the glyph
  bool isDefault = false;
};

struct AudioState {
  bool available = true;   // there is a default output
  float level = 0.0f;      // 0..1
  bool muted = false;
  std::wstring device;     // the default output's name
  std::vector<AudioOutput> outputs;
  bool canSwitch = true;   // false when IPolicyConfig is missing (SEGURIDAD.md 2.2)
};

struct AppEntry {
  std::wstring name;
  wchar_t glyph = 0;       // Segoe Fluent Icons code point
  bool installed = true;
  bool running = false;
};

struct PanelState {
  WifiState wifi;
  BluetoothState bluetooth;
  NightLightState night;
  std::vector<DisplayState> displays;
  AudioState audio;
  std::vector<AppEntry> apps;
  // Something that went wrong and the user should know, said inside the panel (CLAUDE.md:
  // errors are values, never a MessageBox). Empty almost always.
  std::wstring notice;
};

// Made-up data: what phase 1 draws, and what every snapshot draws forever, so a committed PNG
// changes when the design does and not when a headset is plugged in.
PanelState SampleState();

// The display the brightness card talks about when it is closed: the one the panel opened on,
// or the first one when none says so. Null when there are none.
inline const DisplayState* HereDisplay(const PanelState& state) {
  for (const DisplayState& display : state.displays) {
    if (display.here) return &display;
  }
  return state.displays.empty() ? nullptr : &state.displays.front();
}

}  // namespace panel
