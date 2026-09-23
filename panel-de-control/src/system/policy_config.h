#pragma once

// Changing the default audio output. There is no documented way to do it, so this is the one
// undocumented COM interface in the project (SEGURIDAD.md 2.2), and this is the only file that
// knows its name, its CLSID and its IID -- auditar.ps1 fails if they show up anywhere else.
//
// Stable since Windows 10 RS1 and still what EarTrumpet and SoundSwitch use in 2026. Declared
// the way they declare it: every method in its vtable slot, though only SetDefaultEndpoint is
// ever called. A slot left out would shift the one we call onto another method.

#include <windows.h>

#include <mmdeviceapi.h>
#include <propidl.h>
#include <wrl/client.h>

namespace panel {
namespace detail {

MIDL_INTERFACE("F8679F50-850A-41CF-9C72-430F290290C8")
IPolicyConfig : public IUnknown {
 public:
  virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR device, void** format) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR device, INT defaultFormat, void** format) = 0;
  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR device) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR device, void* endpointFormat, void* mixFormat) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR device, INT defaultPeriod, PINT64 period, PINT64 minimum) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR device, PINT64 period) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR device, void* mode) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR device, void* mode) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR device, const PROPERTYKEY& key, PROPVARIANT* value) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR device, const PROPERTYKEY& key, PROPVARIANT* value) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR device, ERole role) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR device, INT visible) = 0;
};

class DECLSPEC_UUID("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9") PolicyConfigClient;

inline Microsoft::WRL::ComPtr<IPolicyConfig> OpenPolicyConfig() {
  Microsoft::WRL::ComPtr<IPolicyConfig> policy;
  if (FAILED(CoCreateInstance(__uuidof(PolicyConfigClient), nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&policy)))) {
    return nullptr;
  }
  return policy;
}

}  // namespace detail

// Whether this Windows has the interface at all. When it does not, the list of outputs is
// shown but cannot be changed, and the panel says why; no other IID is tried in its place.
inline bool CanSetDefaultOutput() { return detail::OpenPolicyConfig() != nullptr; }

// Makes `id` the default output for the three roles, which is what Windows' own quick settings
// does. The caller has just seen `id` in EnumAudioEndpoints (Audio::SetDefault).
inline bool SetDefaultOutput(const wchar_t* id) {
  const Microsoft::WRL::ComPtr<detail::IPolicyConfig> policy = detail::OpenPolicyConfig();
  if (!policy) return false;
  bool ok = true;
  for (const ERole role : {eConsole, eMultimedia, eCommunications}) {
    ok = SUCCEEDED(policy->SetDefaultEndpoint(id, role)) && ok;
  }
  return ok;
}

}  // namespace panel
