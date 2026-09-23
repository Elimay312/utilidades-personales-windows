#include "system/radios.h"

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>

#include <functional>
#include <optional>

#include "core/i18n.h"
#include "core/log.h"
#include "system/bt_audio.h"

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

// The properties each paired device is asked for. Only these: its name comes anyway.
constexpr const wchar_t* kIsConnected = L"System.Devices.Aep.IsConnected";
constexpr const wchar_t* kCodMajor = L"System.Devices.Aep.Bluetooth.Cod.Major";
constexpr const wchar_t* kCodMinor = L"System.Devices.Aep.Bluetooth.Cod.Minor";
constexpr const wchar_t* kLeCategory = L"System.Devices.Aep.Bluetooth.Le.Appearance.Category";
constexpr const wchar_t* kLeSubcategory = L"System.Devices.Aep.Bluetooth.Le.Appearance.Subcategory";

uint32_t UIntOf(const winrt::Windows::Foundation::IInspectable& value) {
  using winrt::Windows::Foundation::IPropertyValue;
  using winrt::Windows::Foundation::PropertyType;
  const IPropertyValue number = value.try_as<IPropertyValue>();
  if (!number) return 0;
  switch (number.Type()) {
    case PropertyType::UInt8: return number.GetUInt8();
    case PropertyType::UInt16: return number.GetUInt16();
    case PropertyType::UInt32: return number.GetUInt32();
    case PropertyType::Int16: return static_cast<uint32_t>(number.GetInt16());
    case PropertyType::Int32: return static_cast<uint32_t>(number.GetInt32());
    default: return 0;
  }
}

template <class Map>
uint32_t Property(const Map& properties, const wchar_t* key) {
  const auto value = properties.TryLookup(key);
  return value ? UIntOf(value) : 0;
}

template <class Map>
std::optional<bool> Connected(const Map& properties) {
  const auto value = properties.TryLookup(kIsConnected);
  if (!value) return std::nullopt;
  const auto flag = value.template try_as<winrt::Windows::Foundation::IPropertyValue>();
  if (!flag || flag.Type() != winrt::Windows::Foundation::PropertyType::Boolean) return std::nullopt;
  return flag.GetBoolean();
}

// What kind of thing a device is, for its glyph and for whether the panel can connect it. The
// classic Class of Device: major 4 is audio and video; major 5 is a peripheral, whose minor
// says keyboard (0x10) or pointer (0x20). LE says it with its appearance: category 15 is HID,
// subcategory 1 a keyboard, 2 a mouse.
template <class Map>
BluetoothDevice::Kind KindOf(const Map& properties) {
  const uint32_t major = Property(properties, kCodMajor);
  const uint32_t minor = Property(properties, kCodMinor);
  if (major == 4) return BluetoothDevice::Kind::Audio;
  if (major == 5) {
    if ((minor & 0x10) != 0) return BluetoothDevice::Kind::Keyboard;
    if ((minor & 0x20) != 0) return BluetoothDevice::Kind::Mouse;
  }
  if (Property(properties, kLeCategory) == 15) {
    const uint32_t sub = Property(properties, kLeSubcategory);
    if (sub == 1) return BluetoothDevice::Kind::Keyboard;
    if (sub == 2) return BluetoothDevice::Kind::Mouse;
  }
  return BluetoothDevice::Kind::Other;
}

template <class Paired>
enumeration::DeviceWatcher Watch(const winrt::hstring& selector, std::map<std::wstring, Paired>& devices,
                                  std::mutex& mutex, const std::function<void()>& changed) {
  winrt::Windows::Foundation::Collections::IVector<winrt::hstring> properties{
      winrt::single_threaded_vector<winrt::hstring>(
          {kIsConnected, kCodMajor, kCodMinor, kLeCategory, kLeSubcategory})};
  enumeration::DeviceWatcher watcher = enumeration::DeviceInformation::CreateWatcher(selector, properties);
  watcher.Added([&devices, &mutex, changed](const enumeration::DeviceWatcher&,
                                            const enumeration::DeviceInformation& info) {
    Paired paired;
    paired.name = info.Name();
    paired.kind = KindOf(info.Properties());
    paired.connected = Connected(info.Properties()).value_or(false);
    {
      std::lock_guard lock(mutex);
      devices[std::wstring(info.Id())] = std::move(paired);
    }
    changed();
  });
  // Connecting and disconnecting arrive here, as a new IsConnected.
  watcher.Updated([&devices, &mutex, changed](const enumeration::DeviceWatcher&,
                                              const enumeration::DeviceInformationUpdate& update) {
    const std::optional<bool> connected = Connected(update.Properties());
    if (!connected) return;
    {
      std::lock_guard lock(mutex);
      const auto found = devices.find(std::wstring(update.Id()));
      if (found == devices.end()) return;
      found->second.connected = *connected;
    }
    changed();
  });
  watcher.Removed([&devices, &mutex, changed](const enumeration::DeviceWatcher&,
                                              const enumeration::DeviceInformationUpdate& update) {
    {
      std::lock_guard lock(mutex);
      devices.erase(std::wstring(update.Id()));
    }
    changed();
  });
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

void Radios::SetAudioConnected(const std::wstring& address, bool connect) {
  if (worker_ == nullptr) return;
  {
    std::lock_guard lock(mutex_);
    busy_[address] = !connect;
  }
  // The row says "busy" before the slow part starts, not after it.
  worker_->Post({}, [this] { Publish(); });
  worker_->Post("bt-audio", [this, address, connect] {
    if (!SetBluetoothAudioConnected(address, connect)) {
      std::lock_guard lock(mutex_);
      busy_.erase(address);
      snapshot_.problem = connect ? T(L"No se pudo conectar. ¿Está encendido y cerca?",
                                      L"It could not connect. Is it on and nearby?")
                                  : T(L"No se pudo desconectar.", L"It could not disconnect.");
    }
    Publish();
  });
}

void Radios::ClearBusy() {
  if (worker_ == nullptr) return;
  worker_->Post({}, [this] {
    {
      std::lock_guard lock(mutex_);
      busy_.clear();
    }
    Publish();
  });
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
    state_->classic = Watch(bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true), classic_,
                            mutex_, changed);
    state_->le = Watch(bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true), le_, mutex_,
                       changed);
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
    // One row per address. The classic face is the one audio connects through, so its name and
    // kind win when it has one; connected if either face is.
    std::map<std::wstring, BluetoothDevice> byAddress;
    for (const auto* faces : {&classic_, &le_}) {
      for (const auto& [id, paired] : *faces) {
        const std::wstring address = BluetoothAddressFromId(id);
        auto [entry, added] = byAddress.try_emplace(address);
        BluetoothDevice& device = entry->second;
        if (added) {
          device.id = address;
          device.name = paired.name;
          device.kind = paired.kind;
        } else if (device.kind == BluetoothDevice::Kind::Other) {
          device.kind = paired.kind;
        }
        device.connected = device.connected || paired.connected;
      }
    }
    for (auto& [address, device] : byAddress) {
      const auto asked = busy_.find(address);
      if (asked != busy_.end()) {
        // Busy until it is no longer what it was when asked.
        if (device.connected != asked->second) busy_.erase(asked);
        else device.busy = true;
      }
      if (next.bluetooth.on && device.connected) ++next.bluetooth.connected;
      next.bluetooth.devices.push_back(std::move(device));
    }
    std::stable_sort(next.bluetooth.devices.begin(), next.bluetooth.devices.end(),
                     [](const BluetoothDevice& a, const BluetoothDevice& b) {
                       if (a.connected != b.connected) return a.connected;
                       return a.name < b.name;
                     });
    next.problem = snapshot_.problem;
    snapshot_ = next;
  }
  PostMessageW(window_, kRadiosMessage, 0, 0);
}

}  // namespace panel
