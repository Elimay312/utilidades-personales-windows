#pragma once

// Wi-Fi and Bluetooth: their radios on and off, the network the machine is on, and how many
// Bluetooth devices are connected (SEGURIDAD.md 1.2 and 2.6).
//
// - Windows.Devices.Radios turns the radios on and off and says when they change.
// - The SSID comes from NetworkInformation's connection profile, which gives the name and
//   nothing else. WlanQueryInterface would ask for the location since Windows 11 24H2 and put
//   the panel in the "location in use" icon; it is forbidden here.
// - Two DeviceWatchers, classic and LE, keep the set of connected devices; a device that talks
//   both is counted once, by its address.
//
// Everything that waits (.get() on an async call) runs on the worker: C++/WinRT refuses to
// block an STA like the interface thread. Events arrive on the thread pool, update a copy under
// a mutex, and post a message; the interface thread only reads the copy.

#include <windows.h>

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#include "model/state.h"
#include "system/worker.h"

namespace panel {

inline constexpr UINT kRadiosMessage = WM_APP + 13;

// The device address inside a Bluetooth device id, the same for its classic and its LE face:
// "Bluetooth#Bluetooth3c:58:c2:aa:bb:cc-a4:c1:38:11:22:33" and
// "BluetoothLE#BluetoothLE3c:58:c2:aa:bb:cc-a4:c1:38:11:22:33" both give "a4:c1:38:11:22:33".
// An id without that shape is its own address, so it still counts once.
inline std::wstring BluetoothAddressFromId(std::wstring_view id) {
  const size_t dash = id.rfind(L'-');
  std::wstring address(dash == std::wstring_view::npos ? id : id.substr(dash + 1));
  for (wchar_t& c : address) c = static_cast<wchar_t>(towlower(c));
  return address;
}

class Radios {
 public:
  struct Snapshot {
    bool known = false;  // the worker has looked
    WifiState wifi;
    BluetoothState bluetooth;
    std::wstring problem;  // said in the panel's notice line, then cleared
  };

  Radios();
  ~Radios();
  Radios(const Radios&) = delete;
  Radios& operator=(const Radios&) = delete;

  // Queues finding the radios and starting the watchers; answers come as kRadiosMessage.
  void Start(HWND window, Worker& worker);
  // Queues letting go of everything WinRT, on the worker, before it stops.
  void Stop();
  void SetWifi(bool on);
  void SetBluetooth(bool on);

  Snapshot Current();  // also clears `problem`, which is said once

 private:
  struct State;  // the WinRT objects, kept out of this header so only radios.cpp sees C++/WinRT
  void Connect();
  void Release();
  void Set(bool wifi, bool on);
  void Publish();

  HWND window_ = nullptr;
  Worker* worker_ = nullptr;
  std::unique_ptr<State> state_;

  std::mutex mutex_;
  // Held for the whole of Publish, reading and storing: it is called at once from the worker
  // and from pool threads, and without it a thread that read the radio a moment earlier could
  // store after one that read it later. Measured in phase 5: turning Wi-Fi off, a network
  // notice read "on", stored after the worker's "off", and the tile stayed on.
  std::mutex publish_;
  Snapshot snapshot_;
  std::set<std::wstring> classic_;  // ids the classic watcher says are connected
  std::set<std::wstring> le_;       // and the LE one
};

}  // namespace panel
