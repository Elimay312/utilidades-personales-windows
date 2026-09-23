#pragma once

// Every screen's brightness: the laptop's panel through WMI's ROOT\WMI (SEGURIDAD.md 2.3) and
// external monitors through DDC/CI (2.4). All of it happens on the worker thread, which owns the
// WMI connection and the physical monitor handles: the interface thread only ever reads a copy
// of the result, after a message says there is one.
//
// Following the Fn keys is not in here. Windows says the current brightness with
// GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS in a WM_POWERBROADCAST, which the window registers for
// itself (RegisterBrightnessNotification) -- no WMI event, no second process, no polling. An
// external monitor's own buttons say nothing to anyone, so those are read again on each opening.

#include <windows.h>

#include <wbemidl.h>
#include <wrl/client.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "system/worker.h"

namespace panel {

// Posted when the worker has (re)read the screens, or failed to write to one.
inline constexpr UINT kBrightnessMessage = WM_APP + 12;

// 8ffee2c6-2d01-46be-adb9-398addc5b4ff: winnt.h declares it, and spelling it out here is one
// line against INITGUID.
inline constexpr GUID kCurrentMonitorBrightness = {
    0x8ffee2c6, 0x2d01, 0x46be, {0xad, 0xb9, 0x39, 0x8a, 0xdd, 0xc5, 0xb4, 0xff}};

// Asks Windows to tell `window` the internal panel's brightness whenever it changes, with
// WM_POWERBROADCAST / PBT_POWERSETTINGCHANGE. It also sends the current value right away.
HPOWERNOTIFY RegisterBrightnessNotification(HWND window);
// The level in such a message, 0..1, or a negative number when the message is about
// something else.
float BrightnessFromPowerBroadcast(WPARAM wparam, LPARAM lparam);

class Brightness {
 public:
  struct Screen {
    std::wstring device;     // \\.\DISPLAYn: how the window knows which one it opened on
    std::wstring id;         // DISPLAY\hw\inst
    std::wstring name;       // DisplayConfig's friendly name; empty for most laptop panels
    bool internal = false;
    bool reachable = false;  // WMI or DDC/CI answered
    float level = 0.0f;      // 0..1
  };
  struct Snapshot {
    bool known = false;      // the worker has looked at least once
    std::vector<Screen> screens;  // left to right, as they sit on the desk
  };

  // Queues the WMI connection and the first look at the screens; the answer arrives as
  // kBrightnessMessage.
  void Start(HWND window, Worker& worker);
  // Queues letting go of the connection and the monitor handles. Before the worker stops, so
  // they are let go on its thread.
  void Stop();
  // Queues a new look at which screens there are: at start and on WM_DISPLAYCHANGE.
  void Enumerate();
  // Queues reading the external monitors' levels again: once per opening, never polled.
  void Reread();
  // Queues a write of `level`, 0..1, to the screen `id`. Only the latest pending one per screen
  // runs, and DDC/CI never more than once every kDdcInterval (SEGURIDAD.md 2.4).
  void Set(const std::wstring& id, float level);

  Snapshot Current() const;

 private:
  // A physical monitor handle, and the range its DDC/CI brightness takes.
  struct Ddc {
    // The handle is an index dxva2 hands out from 0 per process, so 0 is a real monitor: the
    // first one asked for. Measured in 4b: the LG got 0 and every write went nowhere.
    HANDLE handle = nullptr;
    bool open = false;
    DWORD min = 0;
    DWORD max = 100;
    std::chrono::steady_clock::time_point written{};
  };

  // On the worker thread only.
  void Connect();
  void Look();
  void ReadDdc();
  void Write(const std::wstring& id, float level);
  void WriteWmi(float level);
  void ReleaseMonitors();
  void Publish(const std::vector<Screen>& screens);

  HWND window_ = nullptr;
  Worker* worker_ = nullptr;

  // The worker's, and only the worker touches them.
  Microsoft::WRL::ComPtr<IWbemServices> services_;
  std::wstring methodsPath_;   // __PATH of the WmiMonitorBrightnessMethods instance
  std::wstring wmiInstance_;   // the laptop panel WMI drives, DISPLAY\hw\inst
  std::vector<Screen> screens_;
  std::vector<Ddc> ddc_;       // alongside screens_; not open for WMI's or an unreachable one

  mutable std::mutex mutex_;
  Snapshot current_;
};

}  // namespace panel
