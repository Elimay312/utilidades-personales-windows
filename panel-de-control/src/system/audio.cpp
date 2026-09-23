#include "system/audio.h"

#include <functiondiscoverykeys_devpkey.h>
#include <wrl/implements.h>

#include <cmath>

#include "core/hr.h"
#include "core/log.h"

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

namespace panel {
namespace {

// Both callbacks arrive on Core Audio's threads. They post a message and nothing else: no
// endpoint is released or registered from inside one (CLAUDE.md, architecture rule 2).

class LevelCallback : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAudioEndpointVolumeCallback> {
 public:
  explicit LevelCallback(HWND window) : window_(window) {}

  STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
    // The panel's own write coming back: the slider already shows it.
    if (data != nullptr && IsEqualGUID(data->guidEventContext, kPanelVolumeContext)) return S_OK;
    PostMessageW(window_, kAudioChangedMessage, 0, 0);
    return S_OK;
  }

 private:
  HWND window_;
};

class DeviceCallback : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMMNotificationClient> {
 public:
  DeviceCallback(HWND window, std::atomic<bool>* stale) : window_(window), stale_(stale) {}

  STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
    // Windows sends one of these per role, a few milliseconds apart. Only the one the panel
    // reads counts, or the endpoint would be dropped twice for one change (HUD, isla).
    if (flow != eRender || role != eMultimedia) return S_OK;
    stale_->store(true);
    PostMessageW(window_, kAudioDeviceMessage, 0, 0);
    return S_OK;
  }
  STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
  STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
  STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
  STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

 private:
  HWND window_;
  std::atomic<bool>* stale_;
};

}  // namespace

bool Audio::Start(HWND window) {
  window_ = window;
  if (Failed(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&enumerator_)),
             L"CoCreateInstance(MMDeviceEnumerator)")) {
    return false;
  }
  // On the enumerator and not on the endpoint: it has to outlive exactly what it announces.
  ComPtr<DeviceCallback> callback = Make<DeviceCallback>(window, &stale_);
  if (!callback || Failed(enumerator_->RegisterEndpointNotificationCallback(callback.Get()),
                          L"RegisterEndpointNotificationCallback")) {
    enumerator_.Reset();
    return false;
  }
  deviceCallback_ = callback;
  return true;
}

void Audio::Stop() {
  Release();
  if (enumerator_ && deviceCallback_) {
    enumerator_->UnregisterEndpointNotificationCallback(deviceCallback_.Get());
  }
  deviceCallback_.Reset();
  enumerator_.Reset();
}

void Audio::Release() {
  if (endpoint_ && levelCallback_) endpoint_->UnregisterControlChangeNotify(levelCallback_.Get());
  levelCallback_.Reset();
  endpoint_.Reset();
  name_.clear();
}

std::wstring Audio::DeviceName(IMMDevice* device) const {
  ComPtr<IPropertyStore> store;
  if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) return {};
  // The short name first ("Auriculares"), which is what fits the card's header; the long one
  // ("Auriculares (Realtek(R) Audio)") when a driver leaves the short one empty.
  for (const PROPERTYKEY& key : {PKEY_Device_DeviceDesc, PKEY_Device_FriendlyName}) {
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
      name = value.pwszVal;
    }
    PropVariantClear(&value);
    if (!name.empty()) return name;
  }
  return {};
}

IAudioEndpointVolume* Audio::Endpoint() {
  if (!enumerator_) return nullptr;
  // The fix: a change of device is acted on here, before the endpoint is used, and not when
  // the old endpoint fails -- because it never does.
  if (stale_.exchange(false)) {
    Release();
    noOutput_ = false;
  }
  if (endpoint_) return endpoint_.Get();

  ComPtr<IMMDevice> device;
  const HRESULT found = enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
  if (found == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
    // Nothing to play through. Not an error, and nothing to retry: the enumerator's callback
    // says so when a device appears.
    if (!noOutput_) LogInfo(L"audio: there is no output device");
    noOutput_ = true;
    return nullptr;
  }
  if (Failed(found, L"GetDefaultAudioEndpoint")) return nullptr;
  noOutput_ = false;

  ComPtr<IAudioEndpointVolume> endpoint;
  if (Failed(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
                              reinterpret_cast<void**>(endpoint.GetAddressOf())),
             L"IMMDevice::Activate(IAudioEndpointVolume)")) {
    return nullptr;
  }
  ComPtr<LevelCallback> callback = Make<LevelCallback>(window_);
  if (!callback ||
      Failed(endpoint->RegisterControlChangeNotify(callback.Get()), L"RegisterControlChangeNotify")) {
    return nullptr;
  }
  endpoint_ = endpoint;
  levelCallback_ = callback;
  name_ = DeviceName(device.Get());
  LogInfo(L"audio: output is {}", name_.empty() ? std::wstring(L"(no name)") : name_);
  return endpoint_.Get();
}

Audio::Result Audio::Read(AudioState& out) {
  IAudioEndpointVolume* endpoint = Endpoint();
  if (endpoint == nullptr) {
    out.available = false;
    out.device.clear();
    return noOutput_ ? Result::NoOutput : Result::Failed;
  }
  float level = 0.0f;
  BOOL muted = FALSE;
  if (Failed(endpoint->GetMasterVolumeLevelScalar(&level), L"GetMasterVolumeLevelScalar") ||
      Failed(endpoint->GetMute(&muted), L"GetMute")) {
    // A real failure -- the service restarting, a device pulled mid-call. Everything goes and
    // the next attempt opens it all again.
    Release();
    out.available = false;
    return Result::Failed;
  }
  out.available = true;
  // Whole percents, like everything the slider sets (PanelWindow's Quantize).
  out.level = std::round(level * 100.0f) / 100.0f;
  out.muted = muted != FALSE;
  out.device = name_;
  return Result::Ok;
}

bool Audio::SetLevel(float level) {
  IAudioEndpointVolume* endpoint = Endpoint();
  if (endpoint == nullptr) return false;
  if (Failed(endpoint->SetMasterVolumeLevelScalar(level, &kPanelVolumeContext),
             L"SetMasterVolumeLevelScalar")) {
    Release();
    return false;
  }
  return true;
}

bool Audio::SetMuted(bool muted) {
  IAudioEndpointVolume* endpoint = Endpoint();
  if (endpoint == nullptr) return false;
  if (Failed(endpoint->SetMute(muted ? TRUE : FALSE, &kPanelVolumeContext), L"SetMute")) {
    Release();
    return false;
  }
  return true;
}

}  // namespace panel
