#include "system/wifi.h"

#include "core/i18n.h"
#include "core/log.h"

namespace panel {
namespace {

std::wstring SsidText(const DOT11_SSID& ssid) {
  // SSIDs are bytes; nearly always UTF-8, which is what Windows' own list assumes too.
  const int length = static_cast<int>(std::min<ULONG>(ssid.uSSIDLength, DOT11_SSID_MAX_LENGTH));
  if (length == 0) return {};
  const auto* bytes = reinterpret_cast<const char*>(ssid.ucSSID);
  const int wide = MultiByteToWideChar(CP_UTF8, 0, bytes, length, nullptr, 0);
  std::wstring out(static_cast<size_t>(wide), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes, length, out.data(), wide);
  return out;
}

}  // namespace

void Wifi::Start(HWND window, Worker& worker) {
  window_ = window;
  worker_ = &worker;
}

void Wifi::Stop() {
  if (worker_ == nullptr) return;
  wanted_ = false;
  worker_->Post({}, [this] { Close(); });
  worker_ = nullptr;
}

void Wifi::Scan() {
  if (worker_ == nullptr) return;
  wanted_ = true;
  {
    std::lock_guard lock(mutex_);
    snapshot_.scanning = true;
  }
  worker_->Post("wifi-scan", [this] {
    if (!wanted_ || !Open()) return;
    Read();  // what Windows already knows, at once
    const DWORD scanned = WlanScan(handle_, &interface_, nullptr, nullptr, nullptr);
    if (scanned != ERROR_SUCCESS) {
      LogInfo(L"wifi: WlanScan answered {}", scanned);
      std::lock_guard lock(mutex_);
      snapshot_.scanning = false;
    }
    PostMessageW(window_, kWifiMessage, 0, 0);
  });
}

void Wifi::StopScanning() {
  wanted_ = false;
  std::lock_guard lock(mutex_);
  snapshot_.scanning = false;
}

void Wifi::Connect(const std::wstring& ssid) {
  if (worker_ == nullptr) return;
  worker_->Post("wifi-connect", [this, ssid] {
    if (!Open()) return;
    const auto found = profiles_.find(ssid);
    if (found == profiles_.end() || found->second.first.empty()) return;
    // SEGURIDAD.md 2.6: a saved profile, by name, and nothing else. No password passes here.
    WLAN_CONNECTION_PARAMETERS parameters{};
    parameters.wlanConnectionMode = wlan_connection_mode_profile;
    parameters.strProfile = found->second.first.c_str();
    parameters.dot11BssType = found->second.second;
    const DWORD result = WlanConnect(handle_, &interface_, &parameters, nullptr);
    if (result != ERROR_SUCCESS) {
      LogError(L"wifi: WlanConnect answered {}", result);
      std::lock_guard lock(mutex_);
      snapshot_.problem = T(L"Windows no pudo conectar con esa red.", L"Windows could not join that network.");
      PostMessageW(window_, kWifiMessage, 0, 0);
    }
  });
}

Wifi::Snapshot Wifi::Current() {
  std::lock_guard lock(mutex_);
  Snapshot out = snapshot_;
  snapshot_.problem.clear();
  return out;
}

void WINAPI Wifi::OnNotice(PWLAN_NOTIFICATION_DATA data, PVOID context) {
  // WlanAPI's own thread: hand it to the worker and nothing more.
  auto* self = static_cast<Wifi*>(context);
  if (data == nullptr || self == nullptr || data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM) return;
  switch (data->NotificationCode) {
    case wlan_notification_acm_scan_complete:
    case wlan_notification_acm_scan_fail:
    case wlan_notification_acm_connection_complete:
    case wlan_notification_acm_disconnected:
    case wlan_notification_acm_scan_list_refresh:
      break;
    default:
      return;
  }
  const bool scanDone = data->NotificationCode == wlan_notification_acm_scan_complete ||
                        data->NotificationCode == wlan_notification_acm_scan_fail;
  Worker* worker = self->worker_;
  if (worker == nullptr) return;
  worker->Post("wifi-read", [self, scanDone] {
    if (scanDone) {
      std::lock_guard lock(self->mutex_);
      self->snapshot_.scanning = false;
    }
    // Closed card: nothing is read (SEGURIDAD.md 1.2), however the notice came.
    if (self->wanted_) self->Read();
    PostMessageW(self->window_, kWifiMessage, 0, 0);
  });
}

bool Wifi::Open() {
  if (handle_ != nullptr) return true;
  DWORD version = 0;
  if (WlanOpenHandle(2, nullptr, &version, &handle_) != ERROR_SUCCESS) {
    handle_ = nullptr;
    return false;
  }
  PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
  if (WlanEnumInterfaces(handle_, nullptr, &interfaces) != ERROR_SUCCESS || interfaces == nullptr ||
      interfaces->dwNumberOfItems == 0) {
    if (interfaces != nullptr) WlanFreeMemory(interfaces);
    Close();
    return false;
  }
  interface_ = interfaces->InterfaceInfo[0].InterfaceGuid;
  WlanFreeMemory(interfaces);
  // ACM only: scans and connections. MSM would need the wiFiControl capability and says more.
  WlanRegisterNotification(handle_, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, &Wifi::OnNotice, this, nullptr, nullptr);
  return true;
}

void Wifi::Close() {
  if (handle_ == nullptr) return;
  WlanRegisterNotification(handle_, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr, nullptr);
  WlanCloseHandle(handle_, nullptr);
  handle_ = nullptr;
  profiles_.clear();
}

void Wifi::Read() {
  if (handle_ == nullptr) return;
  PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
  const DWORD result = WlanGetAvailableNetworkList(handle_, &interface_, 0, nullptr, &list);
  if (result == ERROR_ACCESS_DENIED) {
    // No location permission: Windows lists nothing to anybody without it.
    LogInfo(L"wifi: no location permission, the list stays empty");
    Publish({}, true);
    return;
  }
  if (result != ERROR_SUCCESS || list == nullptr) {
    LogError(L"wifi: WlanGetAvailableNetworkList answered {}", result);
    return;
  }
  std::vector<FoundNetwork> raw;
  std::map<std::wstring, std::pair<std::wstring, DOT11_BSS_TYPE>> profiles;
  for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
    const WLAN_AVAILABLE_NETWORK& network = list->Network[i];
    FoundNetwork found;
    found.ssid = SsidText(network.dot11Ssid);
    if ((network.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0) found.profile = network.strProfileName;
    found.bars = BarsFromQuality(network.wlanSignalQuality);
    found.secured = network.bSecurityEnabled != FALSE;
    found.connected = (network.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
    if (!found.profile.empty() && !found.ssid.empty()) {
      profiles.emplace(found.ssid, std::make_pair(found.profile, network.dot11BssType));
    }
    raw.push_back(std::move(found));
  }
  WlanFreeMemory(list);
  profiles_ = std::move(profiles);

  std::vector<WifiNetwork> networks;
  for (const FoundNetwork& found : MergeNetworks(raw)) {
    networks.push_back(WifiNetwork{found.ssid, found.bars, found.secured, !found.profile.empty(), found.connected});
  }
  Publish(std::move(networks), false);
}

void Wifi::Publish(std::vector<WifiNetwork> networks, bool denied) {
  std::lock_guard lock(mutex_);
  snapshot_.networks = std::move(networks);
  snapshot_.denied = denied;
}

}  // namespace panel
