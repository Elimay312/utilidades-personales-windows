#include "system/bt_audio.h"

#include <windows.h>

#include <winioctl.h>  // CTL_CODE, which ks.h needs and WIN32_LEAN_AND_MEAN leaves out

#include <cfgmgr32.h>
#include <ks.h>
#include <ksmedia.h>

#include <vector>

#include "core/log.h"

namespace panel {

bool SetBluetoothAudioConnected(std::wstring_view address, bool connect) {
  const std::wstring key = AddressKey(address);
  GUID category = KSCATEGORY_AUDIO;

  ULONG size = 0;
  if (CM_Get_Device_Interface_List_SizeW(&size, &category, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
          CR_SUCCESS ||
      size == 0) {
    return false;
  }
  std::vector<wchar_t> list(size);
  if (CM_Get_Device_Interface_ListW(&category, nullptr, list.data(), size, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
      CR_SUCCESS) {
    return false;
  }

  bool taken = false;
  int filters = 0;
  for (const wchar_t* path = list.data(); *path != L'\0'; path += wcslen(path) + 1) {
    if (!AudioFilterOf(path, key)) continue;
    ++filters;
    // SEGURIDAD.md 2.6: the one ioctl and the two one-shot properties, and nothing else.
    const HANDLE filter = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, 0, nullptr);
    if (filter == INVALID_HANDLE_VALUE) continue;
    KSPROPERTY property{};
    property.Set = KSPROPSETID_BtAudio;
    property.Id = connect ? KSPROPERTY_ONESHOT_RECONNECT : KSPROPERTY_ONESHOT_DISCONNECT;
    property.Flags = KSPROPERTY_TYPE_GET;
    DWORD returned = 0;
    if (DeviceIoControl(filter, IOCTL_KS_PROPERTY, &property, sizeof(property), nullptr, 0, &returned, nullptr)) {
      taken = true;
    } else {
      LogInfo(L"bluetooth: an audio filter did not take the request (error {})", GetLastError());
    }
    CloseHandle(filter);
  }
  LogInfo(L"bluetooth: {} {} through {} audio filter(s): {}", connect ? L"connect" : L"disconnect", key, filters,
          taken ? L"taken" : L"not taken");
  return taken;
}

}  // namespace panel
