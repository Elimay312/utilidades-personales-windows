#pragma once

// The master volume of the default output, through Core Audio. The same surface the HUD and
// the island use (SEGURIDAD.md 2.1), and the same fix they paid for: the endpoint that is open
// when the default output changes does NOT fail -- it keeps answering for the old device, with
// the old device's numbers, forever (measured in hud/CHANGELOG.md: 47 samples, no error, 38 %
// reported against a real 100 %). So a change of device is heard on the enumerator, which
// survives it, and the endpoint is dropped and opened again on the interface thread.

#include <windows.h>

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <string>

#include "model/state.h"

namespace panel {

// Posted to the window from Core Audio's threads. No payload: the answer is always to read
// again on the interface thread, which is the only one that touches the endpoint.
inline constexpr UINT kAudioChangedMessage = WM_APP + 10;  // level or mute, from outside
inline constexpr UINT kAudioDeviceMessage = WM_APP + 11;   // the default output is another one

// Every write the panel makes carries this, so its own change coming back as a notification is
// recognised and dropped instead of redrawing a value the slider already shows. Other programs
// can read it too (phase 8: the HUD could stay quiet while the panel's slider moves).
// {5B0D7C34-8A41-4C2E-9F3A-612D7E94B01C}
inline constexpr GUID kPanelVolumeContext = {
    0x5b0d7c34, 0x8a41, 0x4c2e, {0x9f, 0x3a, 0x61, 0x2d, 0x7e, 0x94, 0xb0, 0x1c}};

class Audio {
 public:
  Audio() = default;
  ~Audio() { Stop(); }
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  // Listens for changes of device on the enumerator; the endpoint opens on the first Read.
  // Needs COM on the calling thread, and must be stopped on it before COM goes.
  bool Start(HWND window);
  void Stop();

  enum class Result { Ok, NoOutput, Failed };
  // Fills available, level, muted and device. NoOutput is a machine with nothing to play
  // through, which the enumerator announces when that changes; Failed is anything else, and
  // the caller tries again a little later.
  Result Read(AudioState& out);
  bool SetLevel(float level);
  bool SetMuted(bool muted);

 private:
  IAudioEndpointVolume* Endpoint();
  void Release();
  std::wstring DeviceName(IMMDevice* device) const;

  HWND window_ = nullptr;
  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
  Microsoft::WRL::ComPtr<IMMNotificationClient> deviceCallback_;
  Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpoint_;
  Microsoft::WRL::ComPtr<IAudioEndpointVolumeCallback> levelCallback_;
  std::wstring name_;
  bool noOutput_ = false;
  // Set from Core Audio's thread when the default output changes; the next Endpoint() on the
  // interface thread sees it and drops the old endpoint before using it.
  std::atomic<bool> stale_{false};
};

}  // namespace panel
