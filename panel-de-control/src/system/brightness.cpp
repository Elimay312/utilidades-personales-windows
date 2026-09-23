#include "system/brightness.h"

#include <oleauto.h>

#include <algorithm>
#include <cmath>

#include "core/hr.h"
#include "core/log.h"

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
  worker_->Post({}, [this] { Connect(); });
}

void Brightness::Stop() {
  if (worker_ == nullptr) return;
  worker_->Post({}, [this] {
    services_.Reset();
    methodsPath_.clear();
  });
  worker_ = nullptr;
}

void Brightness::Set(float level) {
  if (worker_ == nullptr) return;
  worker_->Post("brightness-internal", [this, level] { Write(level); });
}

Brightness::Internal Brightness::Current() const {
  std::lock_guard lock(mutex_);
  return current_;
}

void Brightness::Connect() {
  Internal found;
  found.known = true;

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
      VARIANT value;
      VariantInit(&value);
      if (SUCCEEDED(panel->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr)) &&
          value.vt == VT_UI1) {
        found.present = true;
        found.level = static_cast<float>(value.bVal) / 100.0f;
      }
      VariantClear(&value);
      found.instance = Text(panel.Get(), L"InstanceName");
      // "DISPLAY\AUO7EAD\5&f094e1b&0&UID4355_0": the _0 is WMI's own suffix, not the device's.
      if (found.instance.size() > 2 && found.instance.ends_with(L"_0")) {
        found.instance.resize(found.instance.size() - 2);
      }
      methodsPath_ = Text(methods.Get(), L"__PATH");
    }
  }
  if (found.present) {
    LogInfo(L"brightness: internal panel {} at {:.0f} %", found.instance, found.level * 100.0f);
  } else {
    LogInfo(L"brightness: no internal panel WMI can drive");
  }
  {
    std::lock_guard lock(mutex_);
    current_ = found;
  }
  PostMessageW(window_, kBrightnessMessage, 0, 0);
}

void Brightness::Write(float level) {
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
  brightness.bVal = static_cast<BYTE>(std::lround(std::clamp(level, 0.0f, 1.0f) * 100.0f));
  in->Put(L"Timeout", 0, &timeout, 0);
  in->Put(L"Brightness", 0, &brightness, 0);

  const HRESULT written = services_->ExecMethod(Bstr(methodsPath_.c_str()), Bstr(L"WmiSetBrightness"),
                                                0, nullptr, in.Get(), nullptr, nullptr);
  if (Failed(written, L"WmiSetBrightness")) {
    // The window takes back the last level that did get written, so the slider does not stay
    // somewhere the panel is not.
    PostMessageW(window_, kBrightnessMessage, 0, 0);
    return;
  }
  std::lock_guard lock(mutex_);
  current_.level = static_cast<float>(brightness.bVal) / 100.0f;
}

}  // namespace panel
