#include "system/brightness.h"

#include <highlevelmonitorconfigurationapi.h>
#include <oleauto.h>
#include <physicalmonitorenumerationapi.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <thread>

#include "core/hr.h"
#include "core/log.h"
#include "system/display_ids.h"

using Microsoft::WRL::ComPtr;

namespace panel {
namespace {

// BSTRs for WMI, freed when they go out of scope.
class Bstr {
 public:
  explicit Bstr(const wchar_t* text) : value_(SysAllocString(text)) {}
  ~Bstr() { SysFreeString(value_); }
  Bstr(const Bstr&) = delete;
  Bstr& operator=(const Bstr&) = delete;
  operator BSTR() const { return value_; }

 private:
  BSTR value_;
};

// The first active instance a WQL query returns, or null.
ComPtr<IWbemClassObject> First(IWbemServices* services, const wchar_t* query) {
  ComPtr<IEnumWbemClassObject> results;
  if (Failed(services->ExecQuery(Bstr(L"WQL"), Bstr(query),
                                 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                 &results),
             L"IWbemServices::ExecQuery")) {
    return nullptr;
  }
  ComPtr<IWbemClassObject> object;
  ULONG returned = 0;
  if (FAILED(results->Next(WBEM_INFINITE, 1, &object, &returned)) || returned == 0) return nullptr;
  return object;
}

std::wstring Text(IWbemClassObject* object, const wchar_t* name) {
  VARIANT value;
  VariantInit(&value);
  std::wstring out;
  if (SUCCEEDED(object->Get(name, 0, &value, nullptr, nullptr)) && value.vt == VT_BSTR &&
      value.bstrVal != nullptr) {
    out = value.bstrVal;
  }
  VariantClear(&value);
  return out;
}

// SEGURIDAD.md 2.4: fixed, not a panel.json key, so nobody can set it to 0.
constexpr auto kDdcInterval = std::chrono::milliseconds(100);

// What DisplayConfig says about each active monitor, and where GDI put it on the desktop.
struct Path {
  Brightness::Screen screen;
  long technology = 0;
  HMONITOR monitor = nullptr;
  LONG left = 0;
};

// The HMONITOR behind each GDI name, and its left edge for the order of the rows.
using MonitorsByName = std::map<std::wstring, std::pair<HMONITOR, LONG>>;

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info)) {
    (*reinterpret_cast<MonitorsByName*>(context))[info.szDevice] = {monitor, info.rcMonitor.left};
  }
  return TRUE;
}

std::vector<Path> ActivePaths() {
  std::vector<Path> found;
  UINT32 pathCount = 0;
  UINT32 modeCount = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
    return found;
  }
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(),
                         nullptr) != ERROR_SUCCESS) {
    LogError(L"brightness: QueryDisplayConfig failed");
    return found;
  }
  paths.resize(pathCount);
  for (const DISPLAYCONFIG_PATH_INFO& info : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = info.sourceInfo.adapterId;
    source.header.id = info.sourceInfo.id;
    DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
    target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size = sizeof(target);
    target.header.adapterId = info.targetInfo.adapterId;
    target.header.id = info.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
        DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
      continue;
    }
    Path path;
    path.screen.device = source.viewGdiDeviceName;
    path.screen.id = InstanceFromDevicePath(target.monitorDevicePath);
    path.screen.name = target.monitorFriendlyDeviceName;
    path.technology = static_cast<long>(info.targetInfo.outputTechnology);
    found.push_back(std::move(path));
  }

  MonitorsByName monitors;
  EnumDisplayMonitors(nullptr, nullptr, &CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
  for (Path& path : found) {
    if (const auto at = monitors.find(path.screen.device); at != monitors.end()) {
      path.monitor = at->second.first;
      path.left = at->second.second;
    }
  }
  std::stable_sort(found.begin(), found.end(), [](const Path& a, const Path& b) { return a.left < b.left; });
  return found;
}

// A DDC/CI brightness read: the test for "can this monitor be driven" (SEGURIDAD.md 2.4,
// amended in 4b). A monitor that does not answer fails it in about a millisecond.
bool ReadDdcLevel(HANDLE handle, DWORD* min, DWORD* max, float* level) {
  DWORD low = 0;
  DWORD current = 0;
  DWORD high = 0;
  if (!GetMonitorBrightness(handle, &low, &current, &high) || high <= low) return false;
  *min = low;
  *max = high;
  *level = static_cast<float>(std::clamp(current, low, high) - low) / static_cast<float>(high - low);
  return true;
}

}  // namespace

HPOWERNOTIFY RegisterBrightnessNotification(HWND window) {
  return RegisterPowerSettingNotification(window, &kCurrentMonitorBrightness,
                                          DEVICE_NOTIFY_WINDOW_HANDLE);
}

float BrightnessFromPowerBroadcast(WPARAM wparam, LPARAM lparam) {
  if (wparam != PBT_POWERSETTINGCHANGE || lparam == 0) return -1.0f;
  const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
  if (!IsEqualGUID(setting->PowerSetting, kCurrentMonitorBrightness) ||
      setting->DataLength < sizeof(DWORD)) {
    return -1.0f;
  }
  DWORD percent = 0;
  memcpy(&percent, setting->Data, sizeof(percent));
  return static_cast<float>(percent > 100 ? 100 : percent) / 100.0f;
}

void Brightness::Start(HWND window, Worker& worker) {
  window_ = window;
  worker_ = &worker;
  worker_->Post({}, [this] {
    Connect();
    Look();
  });
}

void Brightness::Stop() {
  if (worker_ == nullptr) return;
  worker_->Post({}, [this] {
    ReleaseMonitors();
    services_.Reset();
    methodsPath_.clear();
  });
  worker_ = nullptr;
}

void Brightness::Enumerate() {
  if (worker_ == nullptr) return;
  worker_->Post("brightness-look", [this] { Look(); });
}

void Brightness::Reread() {
  if (worker_ == nullptr) return;
  worker_->Post("brightness-reread", [this] { ReadDdc(); });
}

void Brightness::Set(const std::wstring& id, float level) {
  if (worker_ == nullptr) return;
  // One key per screen: dragging one slider never drops a pending write to another.
  std::string key = "brightness-";
  for (const wchar_t c : id) key.push_back(static_cast<char>(c));
  worker_->Post(std::move(key), [this, id, level] { Write(id, level); });
}

Brightness::Snapshot Brightness::Current() const {
  std::lock_guard lock(mutex_);
  return current_;
}

void Brightness::Publish(const std::vector<Screen>& screens) {
  {
    std::lock_guard lock(mutex_);
    current_.known = true;
    current_.screens = screens;
  }
  PostMessageW(window_, kBrightnessMessage, 0, 0);
}

void Brightness::ReleaseMonitors() {
  for (Ddc& ddc : ddc_) {
    if (!ddc.open) continue;
    PHYSICAL_MONITOR monitor{};
    monitor.hPhysicalMonitor = ddc.handle;
    DestroyPhysicalMonitors(1, &monitor);
  }
  ddc_.clear();
  screens_.clear();
}

void Brightness::Look() {
  ReleaseMonitors();
  // The laptop's level again: the Fn keys moved it while nobody was looking at WMI.
  float wmiLevel = -1.0f;
  if (services_ && !wmiInstance_.empty()) {
    const ComPtr<IWbemClassObject> panel = First(
        services_.Get(), L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active = TRUE");
    VARIANT value;
    VariantInit(&value);
    if (panel && SUCCEEDED(panel->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr)) &&
        value.vt == VT_UI1) {
      wmiLevel = static_cast<float>(value.bVal) / 100.0f;
    }
    VariantClear(&value);
  }

  for (Path& path : ActivePaths()) {
    Screen screen = std::move(path.screen);
    Ddc ddc;
    const bool wmi = SameInstance(screen.id, wmiInstance_);
    screen.internal = wmi || IsInternalOutput(path.technology);
    if (wmi) {
      screen.reachable = wmiLevel >= 0.0f;
      screen.level = std::max(wmiLevel, 0.0f);
    } else if (!screen.internal && path.monitor != nullptr) {
      // ponytail: the first physical monitor of each HMONITOR; a cloned pair shares one
      // HMONITOR and only its first gets a slider. Walk the array if that setup shows up.
      DWORD count = 0;
      if (GetNumberOfPhysicalMonitorsFromHMONITOR(path.monitor, &count) && count > 0) {
        std::vector<PHYSICAL_MONITOR> physical(count);
        if (GetPhysicalMonitorsFromHMONITOR(path.monitor, count, physical.data())) {
          if (count > 1) DestroyPhysicalMonitors(count - 1, physical.data() + 1);
          if (ReadDdcLevel(physical[0].hPhysicalMonitor, &ddc.min, &ddc.max, &screen.level)) {
            ddc.handle = physical[0].hPhysicalMonitor;
            ddc.open = true;
            screen.reachable = true;
          } else {
            DestroyPhysicalMonitors(1, physical.data());
          }
        }
      }
    }
    LogInfo(L"brightness: {} {} '{}' {}{}", screen.device, screen.id, screen.name,
            screen.internal ? L"internal" : L"external",
            screen.reachable ? std::format(L" at {:.0f} %", screen.level * 100.0f) : std::wstring(L", no control"));
    screens_.push_back(std::move(screen));
    ddc_.push_back(ddc);
  }
  Publish(screens_);
}

void Brightness::ReadDdc() {
  bool changed = false;
  for (size_t i = 0; i < screens_.size(); ++i) {
    if (!ddc_[i].open) continue;
    float level = 0.0f;
    if (ReadDdcLevel(ddc_[i].handle, &ddc_[i].min, &ddc_[i].max, &level) && level != screens_[i].level) {
      screens_[i].level = level;
      changed = true;
    }
  }
  if (changed) Publish(screens_);
}

void Brightness::Write(const std::wstring& id, float level) {
  const auto at = std::find_if(screens_.begin(), screens_.end(),
                               [&id](const Screen& screen) { return screen.id == id; });
  if (at == screens_.end() || !at->reachable) return;
  level = std::clamp(level, 0.0f, 1.0f);
  if (SameInstance(id, wmiInstance_)) {
    WriteWmi(level);
    return;
  }
  Ddc& ddc = ddc_[static_cast<size_t>(at - screens_.begin())];
  if (!ddc.open) return;
  // SEGURIDAD.md 2.4: at most once per interval. The worker keeps only the latest pending value
  // per screen, so waiting here drops the ones in between, and the last one is always written.
  const auto since = std::chrono::steady_clock::now() - ddc.written;
  if (since < kDdcInterval) std::this_thread::sleep_for(kDdcInterval - since);
  const DWORD value = ddc.min + static_cast<DWORD>(std::lround(level * static_cast<float>(ddc.max - ddc.min)));
  ddc.written = std::chrono::steady_clock::now();
  if (!SetMonitorBrightness(ddc.handle, value)) {
    LogError(L"brightness: SetMonitorBrightness({}, {}) failed with error {}", id, value, GetLastError());
    // The window takes back the last level that did get written.
    Publish(screens_);
    return;
  }
  at->level = level;
  std::lock_guard lock(mutex_);
  current_.screens = screens_;
}

void Brightness::Connect() {

  ComPtr<IWbemLocator> locator;
  if (!Failed(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&locator)),
              L"CoCreateInstance(WbemLocator)") &&
      !Failed(locator->ConnectServer(Bstr(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr,
                                     nullptr, &services_),
              L"IWbemLocator::ConnectServer(ROOT\\WMI)")) {
    CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    // Only the internal panel answers here; an external monitor has no WmiMonitorBrightness,
    // and a desktop has none at all -- which is not an error, just a machine without one.
    const ComPtr<IWbemClassObject> panel = First(
        services_.Get(),
        L"SELECT CurrentBrightness, InstanceName FROM WmiMonitorBrightness WHERE Active = TRUE");
    const ComPtr<IWbemClassObject> methods = First(
        services_.Get(), L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active = TRUE");
    if (panel && methods) {
      // "DISPLAY\AUO7EAD\5&f094e1b&0&UID4355_0": the _0 is WMI's own suffix, not the device's.
      wmiInstance_ = InstanceFromWmi(Text(panel.Get(), L"InstanceName"));
      methodsPath_ = Text(methods.Get(), L"__PATH");
    }
  }
  if (wmiInstance_.empty()) LogInfo(L"brightness: no internal panel WMI can drive");
}

void Brightness::WriteWmi(float level) {
  if (!services_ || methodsPath_.empty()) return;
  ComPtr<IWbemClassObject> methodsClass;
  ComPtr<IWbemClassObject> signature;
  ComPtr<IWbemClassObject> in;
  if (Failed(services_->GetObject(Bstr(L"WmiMonitorBrightnessMethods"), 0, nullptr, &methodsClass,
                                  nullptr),
             L"IWbemServices::GetObject(WmiMonitorBrightnessMethods)") ||
      Failed(methodsClass->GetMethod(L"WmiSetBrightness", 0, &signature, nullptr),
             L"GetMethod(WmiSetBrightness)") ||
      Failed(signature->SpawnInstance(0, &in), L"SpawnInstance")) {
    return;
  }
  // Timeout 0: set it now and keep it, which is what Windows' own slider does.
  VARIANT timeout;
  VariantInit(&timeout);
  timeout.vt = VT_I4;
  timeout.lVal = 0;
  VARIANT brightness;
  VariantInit(&brightness);
  brightness.vt = VT_UI1;
  brightness.bVal = static_cast<BYTE>(std::lround(level * 100.0f));
  in->Put(L"Timeout", 0, &timeout, 0);
  in->Put(L"Brightness", 0, &brightness, 0);

  const HRESULT written = services_->ExecMethod(Bstr(methodsPath_.c_str()), Bstr(L"WmiSetBrightness"),
                                                0, nullptr, in.Get(), nullptr, nullptr);
  if (Failed(written, L"WmiSetBrightness")) {
    // The window takes back the last level that did get written, so the slider does not stay
    // somewhere the panel is not.
    Publish(screens_);
    return;
  }
  for (Screen& screen : screens_) {
    if (SameInstance(screen.id, wmiInstance_)) screen.level = static_cast<float>(brightness.bVal) / 100.0f;
  }
  std::lock_guard lock(mutex_);
  current_.screens = screens_;
}

}  // namespace panel
