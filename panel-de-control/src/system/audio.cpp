#include "system/audio.h"

#include <functiondiscoverykeys_devpkey.h>
#include <wrl/implements.h>

#include <cmath>

#include "core/hr.h"
#include "core/log.h"
#include "system/policy_config.h"

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

namespace panel {
namespace {

// PKEY_AudioEndpoint_FormFactor, spelled out: the SDK only declares it, and defining it here is
// one line against pulling INITGUID into the whole file.
constexpr PROPERTYKEY kFormFactor = {
    {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};

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
  // Something was plugged in, unplugged, enabled or disabled: the list may have changed. No
  // flag, because the default did not; the window reads the list again. Microphones arrive
  // here too, and cost one read of a list that did not change.
  STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return Changed(); }
  STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return Changed(); }
  STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return Changed(); }
  STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

 private:
  HRESULT Changed() {
    PostMessageW(window_, kAudioDeviceMessage, 0, 0);
    return S_OK;
  }

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
  canSwitch_ = CanSetDefaultOutput();
  if (!canSwitch_) LogError(L"audio: this Windows cannot change the default output from here");
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

OutputNames Audio::Names(IMMDevice* device) const {
  OutputNames names;
  ComPtr<IPropertyStore> store;
  if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) return names;
  const auto text = [&store](const PROPERTYKEY& key) {
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring out;
    if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
      out = value.pwszVal;
    }
    PropVariantClear(&value);
    return out;
  };
  names.shortName = text(PKEY_Device_DeviceDesc);
  names.longName = text(PKEY_Device_FriendlyName);
  return names;
}

std::wstring Audio::DeviceName(IMMDevice* device) const {
  // The short name ("Auriculares"), which is what fits the card's header; the long one
  // ("Auriculares (Realtek(R) Audio)") when a driver leaves the short one empty.
  const OutputNames names = Names(device);
  return names.shortName.empty() ? names.longName : names.shortName;
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

void Audio::ReadOutputs(AudioState& out) {
  out.outputs.clear();
  out.canSwitch = canSwitch_;
  if (!enumerator_) return;
  ComPtr<IMMDeviceCollection> collection;
  if (Failed(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection),
             L"EnumAudioEndpoints")) {
    return;
  }
  std::wstring defaultId;
  ComPtr<IMMDevice> current;
  if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &current))) {
    LPWSTR id = nullptr;
    if (SUCCEEDED(current->GetId(&id))) defaultId = id;
    CoTaskMemFree(id);
  }

  UINT count = 0;
  collection->GetCount(&count);
  std::vector<OutputNames> names;
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    LPWSTR id = nullptr;
    if (FAILED(collection->Item(i, &device)) || FAILED(device->GetId(&id))) continue;
    AudioOutput output;
    output.id = id;
    CoTaskMemFree(id);
    output.isDefault = output.id == defaultId;

    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
      PROPVARIANT form;
      PropVariantInit(&form);
      if (SUCCEEDED(store->GetValue(kFormFactor, &form)) && form.vt == VT_UI4) {
        output.headphones = form.ulVal == Headphones || form.ulVal == Headset;
      }
      PropVariantClear(&form);
    }
    names.push_back(Names(device.Get()));
    out.outputs.push_back(std::move(output));
  }
  const std::vector<std::wstring> chosen = ChooseOutputNames(names);
  for (size_t i = 0; i < out.outputs.size(); ++i) {
    out.outputs[i].name = chosen[i];
    if (out.outputs[i].isDefault) out.device = chosen[i];
  }
}

bool Audio::SetDefault(const std::wstring& id) {
  if (!canSwitch_ || !enumerator_) return false;
  // Enumerated again now: the row may belong to headphones unplugged since the list was drawn.
  ComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) return false;
  UINT count = 0;
  collection->GetCount(&count);
  bool active = false;
  for (UINT i = 0; i < count && !active; ++i) {
    ComPtr<IMMDevice> device;
    LPWSTR current = nullptr;
    if (SUCCEEDED(collection->Item(i, &device)) && SUCCEEDED(device->GetId(&current))) {
      active = id == current;
    }
    CoTaskMemFree(current);
  }
  if (!active) {
    LogInfo(L"audio: that output is not active any more, not switching");
    return false;
  }
  if (!SetDefaultOutput(id.c_str())) {
    LogError(L"audio: Windows did not take the new default output");
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
