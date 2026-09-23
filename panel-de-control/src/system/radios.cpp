#include "system/radios.h"

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>

#include <functional>

#include "core/i18n.h"
#include "core/log.h"

namespace radios = winrt::Windows::Devices::Radios;
namespace bluetooth = winrt::Windows::Devices::Bluetooth;
namespace enumeration = winrt::Windows::Devices::Enumeration;
namespace connectivity = winrt::Windows::Networking::Connectivity;

namespace panel {

struct Radios::State {
  radios::Radio wifi{nullptr};
  radios::Radio bluetooth{nullptr};
  winrt::event_token wifiChanged{};
  winrt::event_token bluetoothChanged{};
  winrt::event_token networkChanged{};
  enumeration::DeviceWatcher classic{nullptr};
  enumeration::DeviceWatcher le{nullptr};
  bool accessAsked = false;
  bool accessAllowed = false;
};

namespace {

void LogWinrt(const wchar_t* what, const winrt::hresult_error& error) {
  LogError(L"radios: {} failed, hr {:#010x} ({})", what, static_cast<unsigned long>(error.code()),
           std::wstring_view(error.message()));
}

// The network the machine is on over Wi-Fi, or nothing. Every known profile is looked at, not
// only the one with internet: a Wi-Fi without internet is still the network you are on.
std::wstring ConnectedSsid() {
  try {
    for (const connectivity::ConnectionProfile& profile :
         connectivity::NetworkInformation::GetConnectionProfiles()) {
      if (!profile.IsWlanConnectionProfile()) continue;
      if (profile.GetNetworkConnectivityLevel() == connectivity::NetworkConnectivityLevel::None) continue;
      // The name and nothing else: this is what keeps the panel out of the location icon.
      return std::wstring(profile.WlanConnectionProfileDetails().GetConnectedSsid());
    }
  } catch (const winrt::hresult_error& error) {
    LogWinrt(L"reading the SSID", error);
  }
  return {};
}

// A radio that exists and can be switched. Disabled means firmware or a hardware switch has it
// off, which the panel cannot change and does not pretend to.
bool Usable(const radios::Radio& radio) {
  return radio != nullptr && radio.State() != radios::RadioState::Disabled;
}

bool IsOn(const radios::Radio& radio) {
  return radio != nullptr && radio.State() == radios::RadioState::On;
}

enumeration::DeviceWatcher Watch(const winrt::hstring& selector, std::set<std::wstring>& ids,
                                  std::mutex& mutex, const std::function<void()>& changed) {
  enumeration::DeviceWatcher watcher = enumeration::DeviceInformation::CreateWatcher(selector);
  // A device joins the set when it connects and leaves it when it disconnects: the selector
  // asks for connected ones, and the watcher raises Removed when one stops matching. Updated
  // has to have a handler for that to happen, even one that does nothing.
  watcher.Added([&ids, &mutex, changed](const enumeration::DeviceWatcher&,
                                        const enumeration::DeviceInformation& info) {
    {
      std::lock_guard lock(mutex);
      ids.insert(std::wstring(info.Id()));
    }
    changed();
  });
  watcher.Removed([&ids, &mutex, changed](const enumeration::DeviceWatcher&,
                                          const enumeration::DeviceInformationUpdate& update) {
    {
      std::lock_guard lock(mutex);
      ids.erase(std::wstring(update.Id()));
    }
    changed();
  });
  watcher.Updated([](const enumeration::DeviceWatcher&, const enumeration::DeviceInformationUpdate&) {});
  watcher.Start();
  return watcher;
}

}  // namespace

Radios::Radios() = default;
Radios::~Radios() = default;

void Radios::Start(HWND window, Worker& worker) {
  window_ = window;
  worker_ = &worker;
  worker_->Post({}, [this] { Connect(); });
}

void Radios::Stop() {
  if (worker_ == nullptr) return;
  worker_->Post({}, [this] { Release(); });
  worker_ = nullptr;
}

void Radios::SetWifi(bool on) {
  if (worker_ != nullptr) worker_->Post("radio-wifi", [this, on] { Set(true, on); });
}

void Radios::SetBluetooth(bool on) {
  if (worker_ != nullptr) worker_->Post("radio-bluetooth", [this, on] { Set(false, on); });
}

Radios::Snapshot Radios::Current() {
  std::lock_guard lock(mutex_);
  Snapshot out = snapshot_;
  snapshot_.problem.clear();
  return out;
}

void Radios::Connect() {
  state_ = std::make_unique<State>();
  const auto changed = [this] { Publish(); };
  try {
    for (const radios::Radio& radio : radios::Radio::GetRadiosAsync().get()) {
      if (radio.Kind() == radios::RadioKind::WiFi && state_->wifi == nullptr) state_->wifi = radio;
      if (radio.Kind() == radios::RadioKind::Bluetooth && state_->bluetooth == nullptr) {
        state_->bluetooth = radio;
      }
    }
    if (state_->wifi) {
      state_->wifiChanged = state_->wifi.StateChanged([changed](auto&&, auto&&) { changed(); });
    }
    if (state_->bluetooth) {
      state_->bluetoothChanged = state_->bluetooth.StateChanged([changed](auto&&, auto&&) { changed(); });
    }
  } catch (const winrt::hresult_error& error) {
    LogWinrt(L"finding the radios", error);
  }
  try {
    state_->networkChanged =
        connectivity::NetworkInformation::NetworkStatusChanged([changed](auto&&) { changed(); });
  } catch (const winrt::hresult_error& error) {
    LogWinrt(L"listening to the network", error);
  }
  try {
    state_->classic = Watch(bluetooth::BluetoothDevice::GetDeviceSelectorFromConnectionStatus(
                                bluetooth::BluetoothConnectionStatus::Connected),
                            classic_, mutex_, changed);
    state_->le = Watch(bluetooth::BluetoothLEDevice::GetDeviceSelectorFromConnectionStatus(
                           bluetooth::BluetoothConnectionStatus::Connected),
                       le_, mutex_, changed);
  } catch (const winrt::hresult_error& error) {
    LogWinrt(L"watching Bluetooth devices", error);
  }
  LogInfo(L"radios: Wi-Fi {}, Bluetooth {}", state_->wifi ? L"found" : L"none",
          state_->bluetooth ? L"found" : L"none");
  Publish();
}

void Radios::Release() {
  if (!state_) return;
  try {
    if (state_->wifi) state_->wifi.StateChanged(state_->wifiChanged);
    if (state_->bluetooth) state_->bluetooth.StateChanged(state_->bluetoothChanged);
    if (state_->networkChanged) connectivity::NetworkInformation::NetworkStatusChanged(state_->networkChanged);
    for (const enumeration::DeviceWatcher& watcher : {state_->classic, state_->le}) {
      if (watcher && watcher.Status() != enumeration::DeviceWatcherStatus::Stopped &&
          watcher.Status() != enumeration::DeviceWatcherStatus::Stopping) {
        watcher.Stop();
      }
    }
  } catch (const winrt::hresult_error& error) {
    LogWinrt(L"letting go", error);
  }
  // ponytail: a pool thread already inside a handler at exit can still call Publish on a State
  // that is going; it only ever happens while the process ends. Wait for the watchers' Stopped
  // events here if that ever shows up in a crash dump.
  state_.reset();
}

void Radios::Set(bool wifi, bool on) {
  if (!state_) return;
  std::wstring problem;
  try {
    if (!state_->accessAsked) {
      const radios::RadioAccessStatus access = radios::Radio::RequestAccessAsync().get();
      state_->accessAsked = true;
      state_->accessAllowed = access == radios::RadioAccessStatus::Allowed;
      LogInfo(L"radios: access {}", static_cast<int>(access));
    }
    const radios::Radio& radio = wifi ? state_->wifi : state_->bluetooth;
    if (!state_->accessAllowed) {
      problem = T(L"Windows no deja al panel cambiar las radios. Usa Win+A.",
                  L"Windows does not let the panel change the radios. Use Win+A.");
    } else if (radio) {
      const radios::RadioAccessStatus result =
          radio.SetStateAsync(on ? radios::RadioState::On : radios::RadioState::Off).get();
      if (result != radios::RadioAccessStatus::Allowed) {
        LogError(L"radios: SetStateAsync answered {}", static_cast<int>(result));
        problem = T(L"Windows no dejó cambiar la radio.", L"Windows did not let the radio change.");
      }
    }
  } catch (const winrt::hresult_error& error) {
    LogWinrt(L"switching a radio", error);
    problem = T(L"No se pudo cambiar la radio.", L"The radio could not be changed.");
  }
  if (!problem.empty()) {
    std::lock_guard lock(mutex_);
    snapshot_.problem = problem;
  }
  Publish();
}

void Radios::Publish() {
  std::lock_guard serial(publish_);
  if (!state_) return;
  Snapshot next;
  next.known = true;
  try {
    next.wifi.available = Usable(state_->wifi);
    next.wifi.on = IsOn(state_->wifi);
    if (next.wifi.on) next.wifi.ssid = ConnectedSsid();
    next.bluetooth.available = Usable(state_->bluetooth);
    next.bluetooth.on = IsOn(state_->bluetooth);
  } catch (const winrt::hresult_error& error) {
    LogWinrt(L"reading the radios", error);
  }
  {
    std::lock_guard lock(mutex_);
    if (next.bluetooth.on) {
      std::set<std::wstring> addresses;
      for (const std::set<std::wstring>* ids : {&classic_, &le_}) {
        for (const std::wstring& id : *ids) addresses.insert(BluetoothAddressFromId(id));
      }
      next.bluetooth.connected = static_cast<int>(addresses.size());
    }
    next.problem = snapshot_.problem;
    snapshot_ = next;
  }
  PostMessageW(window_, kRadiosMessage, 0, 0);
}

}  // namespace panel
