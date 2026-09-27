#include "system/jumplist.h"

#include <shobjidl.h>  // antes que propkey.h: define PROPERTYKEY
#include <propkey.h>
#include <shellapi.h>  // SHGetPropertyStoreForWindow
#include <wrl/client.h>

#include <map>

#include "core/log.h"
#include "system/inventory.h"

namespace dock {
namespace {

using Microsoft::WRL::ComPtr;

std::optional<std::wstring> OfWindow(HWND window) {
  if (!window) return std::nullopt;
  ComPtr<IPropertyStore> store;
  if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) return std::nullopt;
  PROPVARIANT value;
  PropVariantInit(&value);
  std::optional<std::wstring> id;
  if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR && value.pwszVal && *value.pwszVal)
    id = value.pwszVal;
  PropVariantClear(&value);
  return id;
}

std::vector<JumpItem> Read(const std::wstring& appId, APPDOCLISTTYPE type, size_t limit) {
  std::vector<JumpItem> items;
  ComPtr<IApplicationDocumentLists> lists;
  if (FAILED(CoCreateInstance(CLSID_ApplicationDocumentLists, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&lists))))
    return items;
  ComPtr<IObjectArray> array;
  // Una app sin lista, o un AppID que el shell no conoce, falla aquí: no es un error, es que
  // no hay recientes que enseñar.
  if (FAILED(lists->SetAppID(appId.c_str())) ||
      FAILED(lists->GetList(type, static_cast<UINT>(limit), IID_PPV_ARGS(&array))))
    return items;
  UINT count = 0;
  array->GetCount(&count);
  for (UINT i = 0; i < count && items.size() < limit; i++) {
    ComPtr<IShellItem> item;
    if (FAILED(array->GetAt(i, IID_PPV_ARGS(&item)))) continue;
    PWSTR path = nullptr, name = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
        SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && *name)
      items.push_back({name, path});
    CoTaskMemFree(path);
    CoTaskMemFree(name);
  }
  return items;
}

}  // namespace

std::optional<std::wstring> AppIdOf(const DockApp& app, HWND window) {
  if (!IsApp(app)) return std::nullopt;
  // Una app de la Store ya lleva su AUMID en el target.
  if (_wcsnicmp(app.target.c_str(), L"shell:", 6) == 0) {
    const size_t slash = app.target.find_last_of(L'\\');
    return slash != std::wstring::npos ? std::optional(app.target.substr(slash + 1)) : std::nullopt;
  }
  static std::map<std::wstring, std::wstring> known;
  if (auto cached = known.find(app.target); cached != known.end()) return cached->second;
  if (auto id = OfWindow(window)) {
    known[app.target] = *id;
    return id;
  }
  // El Explorador no declara AUMID en sus ventanas (medido: el almacén no trae la clave), pero
  // el sistema publica el suyo, y da la lista de carpetas frecuentes. El único caso a mano.
  const std::wstring tail = L"\\explorer.exe";
  if (app.target.size() >= tail.size() &&
      _wcsicmp(app.target.c_str() + app.target.size() - tail.size(), tail.c_str()) == 0)
    return L"Microsoft.Windows.Explorer";
  return std::nullopt;
}

std::vector<JumpItem> ReadJumpList(const std::wstring& appId, size_t limit) {
  // Los recientes primero; si la app no los registra (el Explorador lleva carpetas
  // frecuentes y no documentos), los frecuentes antes de rendirse.
  auto items = Read(appId, ADLT_RECENT, limit);
  if (items.empty()) items = Read(appId, ADLT_FREQUENT, limit);
  return items;
}

}  // namespace dock
