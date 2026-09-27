#include "system/drop.h"

#include <shobjidl.h>  // antes que propkey.h: define PROPERTYKEY
#include <propkey.h>
#include <shlobj.h>

#include <filesystem>
#include <optional>

#include "core/log.h"

namespace dock {
namespace {

using Microsoft::WRL::ComPtr;

std::wstring Display(IShellItem* item, SIGDN kind) {
  PWSTR text = nullptr;
  if (FAILED(item->GetDisplayName(kind, &text))) return {};  // un objeto virtual no tiene ruta
  std::wstring result = text;
  CoTaskMemFree(text);
  return result;
}

std::wstring AumidOf(IShellItem* item) {
  ComPtr<IShellItem2> item2;
  PWSTR text = nullptr;
  if (FAILED(item->QueryInterface(IID_PPV_ARGS(&item2))) || FAILED(item2->GetString(PKEY_AppUserModel_ID, &text)))
    return {};  // lo normal en un fichero cualquiera
  std::wstring result = text;
  CoTaskMemFree(text);
  return result;
}

bool EndsWith(const std::wstring& text, const wchar_t* tail) {
  const size_t n = wcslen(tail);
  return text.size() >= n && _wcsicmp(text.c_str() + text.size() - n, tail) == 0;
}

// El icono solo vale si es un fichero entero: "shell32.dll,3" apunta a un índice dentro de un
// recurso y el extractor no sabe de índices; entonces se cae al del target.
std::wstring WholeIconFile(const std::wstring& raw, int index) {
  if (raw.empty() || index != 0) return {};
  wchar_t expanded[MAX_PATH]{};
  if (!ExpandEnvironmentStringsW(raw.c_str(), expanded, MAX_PATH)) return {};
  std::error_code error;
  return std::filesystem::is_regular_file(expanded, error) ? std::wstring(expanded) : std::wstring();
}

// Un .url es lo que Steam deja en Inicio por cada juego: un INI con URL=steam://rungameid/N e
// IconFile=...\x.ico. Se guarda por la URL y su icono, no por la ruta del .url, que Steam
// reescribe al actualizar la biblioteca.
std::optional<DockApp> ReadUrl(const std::wstring& path, const std::wstring& name) {
  wchar_t url[2048]{}, icon[MAX_PATH]{};
  if (!GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", url, 2048, path.c_str())) return std::nullopt;
  GetPrivateProfileStringW(L"InternetShortcut", L"IconFile", L"", icon, MAX_PATH, path.c_str());
  const int index = static_cast<int>(GetPrivateProfileIntW(L"InternetShortcut", L"IconIndex", 0, path.c_str()));
  return DockApp{name, url, WholeIconFile(icon, index), L"", false};
}

// Un acceso directo se guarda por su destino, no por el .lnk (el fichero puede moverse o
// borrarse), pero con SU icono y SUS argumentos: el de VALORANT apunta a RiotClientServices.exe
// con --launch-product=valorant, y quedándose solo con el destino el dock enseñaba y abría el
// cliente de Riot. Solo se lee: sin IShellLink::Resolve, que si no encuentra el destino busca
// por los discos y abre un diálogo modal.
std::optional<DockApp> ReadLink(const std::wstring& path, const std::wstring& name) {
  ComPtr<IShellLinkW> link;
  ComPtr<IPersistFile> file;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
      FAILED(link.As(&file)) || FAILED(file->Load(path.c_str(), STGM_READ)))
    return std::nullopt;
  wchar_t target[MAX_PATH]{}, icon[MAX_PATH]{}, arguments[INFOTIPSIZE]{};
  // S_FALSE y cadena vacía: el acceso directo de una app empaquetada no apunta a un fichero.
  if (link->GetPath(target, MAX_PATH, nullptr, 0) != S_OK || !*target) return std::nullopt;
  int index = 0;
  link->GetIconLocation(icon, MAX_PATH, &index);
  link->GetArguments(arguments, INFOTIPSIZE);
  return DockApp{name, target, WholeIconFile(icon, index), arguments, false};
}

// Manda la ruta de disco, y el AUMID solo cuando no hay ninguna. Chromium y Electron ponen un
// AUMID en sus accesos directos para agrupar ventanas (el de Brave es literalmente "Brave"),
// pero no es una entrada del shell: guardado como shell:AppsFolder\Brave, el dock nunca la
// reconocía abierta. Un objeto de verdad virtual (una app de la Store) no tiene otra identidad.
std::optional<Dropped> Resolve(IShellItem* item) {
  const std::wstring name = Display(item, SIGDN_NORMALDISPLAY);
  const std::wstring path = Display(item, SIGDN_FILESYSPATH);
  const auto asApp = [&]() -> std::optional<Dropped> {
    const std::wstring aumid = AumidOf(item);
    if (aumid.empty()) return std::nullopt;
    return Dropped{DockApp{name, L"shell:AppsFolder\\" + aumid, L"", L"", false}, L""};
  };
  if (path.empty()) return asApp();
  if (EndsWith(path, L".url"))
    if (auto app = ReadUrl(path, name)) return Dropped{*app, path};
  if (!EndsWith(path, L".lnk")) return Dropped{DockApp{name, path, L"", L"", false}, path};
  if (auto app = ReadLink(path, name)) return Dropped{*app, path};
  if (auto app = asApp()) return Dropped{app->app, path};
  return Dropped{DockApp{name, path, L"", L"", false}, path};
}

// Se acepta como copia, o como enlace si quien arrastra no ofrece copia (hay orígenes que solo
// ofrecen enlace). Nunca como mover: el origen borraría el fichero al acabar.
DWORD Effect(DWORD allowed) {
  if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
  if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
  return DROPEFFECT_NONE;
}

}  // namespace

std::vector<Dropped> ItemsOf(IDataObject* data) {
  std::vector<Dropped> result;
  ComPtr<IShellItemArray> items;
  if (FAILED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(&items)))) return result;
  DWORD count = 0;
  items->GetCount(&count);
  for (DWORD i = 0; i < count; i++) {
    ComPtr<IShellItem> item;
    if (FAILED(items->GetItemAt(i, &item))) continue;
    if (auto dropped = Resolve(item.Get())) result.push_back(std::move(*dropped));
  }
  return result;
}

DropTarget::DropTarget(HWND hwnd, std::function<bool(POINT)> over, std::function<void()> leave,
                       std::function<void(std::vector<Dropped>)> drop)
    : hwnd_(hwnd), over_(std::move(over)), leave_(std::move(leave)), drop_(std::move(drop)) {
  if (FAILED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper_))))
    LogError(L"[soltar] sin miniatura de arrastre");
}

// Donde soltar no haría nada (el separador, los márgenes), NONE: el cursor de prohibido lo
// dice antes de soltar.
STDMETHODIMP DropTarget::DragEnter(IDataObject* data, DWORD, POINTL at, DWORD* effect) {
  POINT point{at.x, at.y};
  *effect = over_(point) ? Effect(*effect) : DROPEFFECT_NONE;
  if (helper_) helper_->DragEnter(hwnd_, data, &point, *effect);
  return S_OK;
}

STDMETHODIMP DropTarget::DragOver(DWORD, POINTL at, DWORD* effect) {
  POINT point{at.x, at.y};
  *effect = over_(point) ? Effect(*effect) : DROPEFFECT_NONE;
  if (helper_) helper_->DragOver(&point, *effect);
  return S_OK;
}

STDMETHODIMP DropTarget::DragLeave() {
  leave_();
  if (helper_) helper_->DragLeave();
  return S_OK;
}

STDMETHODIMP DropTarget::Drop(IDataObject* data, DWORD, POINTL at, DWORD* effect) {
  POINT point{at.x, at.y};
  if (helper_) helper_->Drop(data, &point, Effect(*effect));
  // AQUÍ y no después: el IDataObject deja de valer en cuanto Drop vuelve. Y se contesta
  // NONE: el dock no se queda con nada, así que quien arrastra no tiene nada que hacer.
  auto items = ItemsOf(data);
  *effect = DROPEFFECT_NONE;
  drop_(std::move(items));
  return S_OK;
}

}  // namespace dock
