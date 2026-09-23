#pragma once

// The networks around, for the unfolded Wi-Fi card, and connecting to a saved one
// (SEGURIDAD.md 1.2 and 2.6, amended in phase 5b-2). This is the one file that looks for
// networks, and it only does while the card is open: since Windows 11 24H2 looking needs the
// location permission, and while it looks the panel is in the "location in use" icon. The
// user chose that knowing it.
//
// WlanAPI, on the worker: plain calls that answer at once, and a notification (ACM source only)
// when a scan finishes or a connection changes. The SSID of the network in use still comes
// from NetworkInformation (system/radios.cpp), which needs no permission.

#include <windows.h>

#include <wlanapi.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "model/state.h"
#include "system/worker.h"

namespace panel {

inline constexpr UINT kWifiMessage = WM_APP + 14;

// Windows' 0..100 link quality as the bars it draws.
inline int BarsFromQuality(unsigned long quality) {
  if (quality == 0) return 0;
  if (quality < 25) return 1;
  if (quality < 50) return 2;
  if (quality < 75) return 3;
  return 4;
}

// One entry of Windows' list, before merging.
struct FoundNetwork {
  std::wstring ssid;
  std::wstring profile;  // empty when Windows has none for it: joining needs a password first
  int bars = 0;
  bool secured = true;
  bool connected = false;
};

// Windows lists a network once per profile it has for it and once more without one, and lists
// hidden networks with no name. The card wants one row per name: the strongest signal, saved
// if any entry has a profile, connected if any entry is. The one in use goes first, then the
// saved ones, then by signal, then by name.
inline std::vector<FoundNetwork> MergeNetworks(const std::vector<FoundNetwork>& raw) {
  std::vector<FoundNetwork> merged;
  for (const FoundNetwork& found : raw) {
    if (found.ssid.empty()) continue;
    auto same = std::find_if(merged.begin(), merged.end(),
                             [&found](const FoundNetwork& n) { return n.ssid == found.ssid; });
    if (same == merged.end()) {
      merged.push_back(found);
      continue;
    }
    same->bars = std::max(same->bars, found.bars);
    same->connected = same->connected || found.connected;
    same->secured = same->secured && found.secured;
    if (same->profile.empty()) same->profile = found.profile;
  }
  std::stable_sort(merged.begin(), merged.end(), [](const FoundNetwork& a, const FoundNetwork& b) {
    if (a.connected != b.connected) return a.connected;
    if (a.profile.empty() != b.profile.empty()) return !a.profile.empty();
    if (a.bars != b.bars) return a.bars > b.bars;
    return a.ssid < b.ssid;
  });
  return merged;
}

class Wifi {
 public:
  struct Snapshot {
    std::vector<WifiNetwork> networks;
    bool scanning = false;
    bool denied = false;    // no location permission: Windows lists nothing
    std::wstring problem;   // said once in the notice line
  };

  Wifi() = default;
  ~Wifi() = default;
  Wifi(const Wifi&) = delete;
  Wifi& operator=(const Wifi&) = delete;

  void Start(HWND window, Worker& worker);
  // Queues closing the handle on the worker, before it stops.
  void Stop();
  // The card opened: read what Windows already knows, then ask it to look again.
  void Scan();
  // The card closed: nothing more is read until it opens again.
  void StopScanning();
  // Joins `ssid` with the profile Windows has for it. Nothing when there is none.
  void Connect(const std::wstring& ssid);

  Snapshot Current();

 private:
  static void WINAPI OnNotice(PWLAN_NOTIFICATION_DATA data, PVOID self);
  // On the worker only.
  bool Open();
  void Read();
  void Close();
  void Publish(std::vector<WifiNetwork> networks, bool denied);

  HWND window_ = nullptr;
  Worker* worker_ = nullptr;
  std::atomic<bool> wanted_{false};  // the card is open

  // The worker's.
  HANDLE handle_ = nullptr;
  GUID interface_{};
  std::map<std::wstring, std::pair<std::wstring, DOT11_BSS_TYPE>> profiles_;  // SSID -> profile

  std::mutex mutex_;
  Snapshot snapshot_;
};

}  // namespace panel
