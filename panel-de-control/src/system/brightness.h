#pragma once

// The laptop's own panel, through WMI's ROOT\WMI and its two brightness classes (SEGURIDAD.md
// 2.3). Everything WMI happens on the worker thread, which owns the one connection: the
// interface thread only ever reads a copy of the result, after a message says there is one.
//
// Following the Fn keys is not in here. Windows says the current brightness with
// GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS in a WM_POWERBROADCAST, which the window registers for
// itself (RegisterBrightnessNotification) -- no WMI event, no second process, no polling.

#include <windows.h>

#include <wbemidl.h>
#include <wrl/client.h>

#include <mutex>
#include <string>

#include "system/worker.h"

namespace panel {

// Posted when the worker has read the panel (at start) or failed to write to it.
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
  struct Internal {
    bool known = false;     // the worker has answered
    bool present = false;   // there is an internal panel WMI can drive
    float level = 0.0f;     // 0..1
    std::wstring instance;  // WMI's InstanceName without the trailing _0: DISPLAY\hw\inst
  };

  // Queues the connection and the first read on `worker`; the answer arrives as
  // kBrightnessMessage.
  void Start(HWND window, Worker& worker);
  // Queues letting go of the connection. Before the worker stops, so it is let go on its thread.
  void Stop();
  // Queues a write of `level`, 0..1. Only the latest pending one runs.
  void Set(float level);

  Internal Current() const;

 private:
  // On the worker thread only.
  void Connect();
  void Write(float level);

  HWND window_ = nullptr;
  Worker* worker_ = nullptr;

  // The worker's, and only the worker touches them.
  Microsoft::WRL::ComPtr<IWbemServices> services_;
  std::wstring methodsPath_;  // __PATH of the WmiMonitorBrightnessMethods instance

  mutable std::mutex mutex_;
  Internal current_;
};

}  // namespace panel
