#include "model/state.h"

#include "ui/glyphs.h"

namespace panel {

PanelState SampleState() {
  PanelState state;
  state.wifi = {true, true, L"Casa_5G"};
  state.wifi.networks = {
      {L"Casa_5G", 4, true, true, true},
      {L"Casa_2.4", 3, true, true, false},
      {L"Oficina", 2, true, false, false},
      {L"Cafe Libre", 1, false, false, false},
  };
  state.bluetooth = {true, true, 2};
  state.bluetooth.devices = {
      {L"a", L"Auriculares", BluetoothDevice::Kind::Audio, true},
      {L"b", L"Teclado", BluetoothDevice::Kind::Keyboard, true},
      {L"c", L"Altavoz de la sala", BluetoothDevice::Kind::Audio, false},
  };
  state.night = {true, false, true, 21 * 60, 7 * 60};

  state.displays = {
      {L"Portátil", L"DISPLAY\\AUO1234\\1", 0.62f, true, true, true},
      {L"LG ULTRAWIDE", L"DISPLAY\\GSM5B09\\2", 0.80f, false, true, false},
      {L"DELL P2419H", L"DISPLAY\\DEL41A1\\3", 0.45f, false, false, false},
  };

  state.audio.level = 0.45f;
  state.audio.device = L"Auriculares";
  state.audio.outputs = {
      {L"{0.0.0.00000000}.{a}", L"Auriculares", true, true},
      {L"{0.0.0.00000000}.{b}", L"Altavoces", false, false},
      {L"{0.0.0.00000000}.{c}", L"LG ULTRAWIDE (HDMI)", false, false},
  };

  state.apps = {
      {L"Dock", glyph::kDock, true, true},
      {L"Isla", glyph::kIsla, true, true},
      {L"HUD", glyph::kHud, true, false},
      {L"QuickLook", glyph::kQuickLook, true, true},
      {L"Lanzador", glyph::kLauncher, false, false},
  };
  return state;
}

}  // namespace panel
