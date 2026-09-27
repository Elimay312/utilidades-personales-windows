#include "model/config.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

#include "core/jsonc.h"
#include "core/log.h"

namespace dock {
namespace {

std::wstring Widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
  return out;
}

std::wstring Text(const nlohmann::json& object, std::string_view key) {
  const auto* v = Find(object, key);
  return v && v->is_string() ? Widen(v->get<std::string>()) : std::wstring{};
}

bool Same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

bool Contains(const std::vector<std::wstring>& keys, const std::wstring& key) {
  return std::any_of(keys.begin(), keys.end(), [&](const std::wstring& k) { return Same(k, key); });
}

// dock.json se escribe con barras normales para no escapar las invertidas, pero
// SHCreateItemFromParsingName rechaza "/" con E_INVALIDARG (File.Exists las aceptaba, así que
// en C# una entrada pasaba la validación y reventaba después).
std::wstring Backslashes(std::wstring path) {
  std::replace(path.begin(), path.end(), L'/', L'\\');
  return path;
}

bool IsShellItem(const std::wstring& target) { return _wcsnicmp(target.c_str(), L"shell:", 6) == 0; }

bool Exists(const std::wstring& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

std::vector<DockApp> ParseApps(const nlohmann::json* array) {
  std::vector<DockApp> apps;
  if (!array || !array->is_array()) return apps;
  for (const auto& entry : *array) {
    DockApp app;
    if (const auto* sep = Find(entry, "separator"); sep && sep->is_boolean()) app.separator = sep->get<bool>();
    app.name = Text(entry, "name");
    app.target = Text(entry, "target");
    app.iconTarget = Text(entry, "iconTarget");
    app.arguments = Text(entry, "arguments");
    apps.push_back(std::move(app));
  }
  return apps;
}

std::vector<std::wstring> ParseKeys(const nlohmann::json* array) {
  std::vector<std::wstring> keys;
  if (!array || !array->is_array()) return keys;
  for (const auto& key : *array)
    if (key.is_string()) keys.push_back(Widen(key.get<std::string>()));
  return keys;
}

ScreenList ParseScreen(const nlohmann::json& object);

std::map<std::wstring, ScreenList> ParseScreens(const nlohmann::json* object) {
  std::map<std::wstring, ScreenList> screens;
  if (!object || !object->is_object()) return screens;
  for (auto it = object->begin(); it != object->end(); ++it) screens[Widen(it.key())] = ParseScreen(it.value());
  return screens;
}

ScreenList ParseScreen(const nlohmann::json& object) {
  return {ParseApps(Find(object, "apps")), ParseScreens(Find(object, "pantallas"))};
}

LocalOverlay ParseLocalObject(const nlohmann::json& object) {
  LocalOverlay local;
  local.order = ParseKeys(Find(object, "orden"));
  local.added = ParseApps(Find(object, "anadidas"));
  local.removed = ParseKeys(Find(object, "quitadas"));
  local.profile = Text(object, "perfil");
  if (const auto* screens = Find(object, "pantallas"); screens && screens->is_object())
    for (auto it = screens->begin(); it != screens->end(); ++it)
      local.screens[Widen(it.key())] = ParseLocalObject(it.value());
  return local;
}

// Las claves guardadas también pasan por Repair: si una app se actualizó, Validate ya
// devuelve la ruta nueva y la de este fichero dejaría de casar; el icono perdería su sitio.
void RepairKeys(LocalOverlay& local) {
  for (auto& key : local.order) key = Repair(key);
  for (auto& key : local.removed) key = Repair(key);
  for (auto& [device, screen] : local.screens) RepairKeys(screen);
}

// La clave de cada entrada. Los separadores no tienen target: van por su orden de aparición.
// ponytail: quitar un separador desplaza el ordinal de los siguientes y esas entradas se van
// al final; con uno o dos no se nota. Si molesta, un id por separador en dock.json.
std::vector<std::wstring> KeysOf(const std::vector<DockApp>& apps) {
  std::vector<std::wstring> keys;
  int separators = 0;
  for (const DockApp& app : apps)
    keys.push_back(app.separator ? L"|separador|" + std::to_wstring(separators++) : app.target);
  return keys;
}

std::string ReadAll(const std::filesystem::path& file, bool& found) {
  std::ifstream in(file, std::ios::binary);
  found = static_cast<bool>(in);
  std::stringstream text;
  if (found) text << in.rdbuf();
  return text.str();
}

// Versión de 2 a 4 números separados por puntos, como System.Version del dock de C#.
std::optional<std::vector<int>> ParseVersion(const std::wstring& text) {
  std::vector<int> parts;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t dot = text.find(L'.', start);
    const std::wstring part = text.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start);
    if (part.empty() || part.size() > 9 ||
        !std::all_of(part.begin(), part.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
      return std::nullopt;
    parts.push_back(std::stoi(part));
    if (dot == std::wstring::npos) break;
    start = dot + 1;
  }
  if (parts.size() < 2 || parts.size() > 4) return std::nullopt;
  return parts;
}

}  // namespace

bool IsUrl(const std::wstring& target) {
  const size_t colon = target.find(L':');
  if (colon == std::wstring::npos || colon < 2) return false;
  for (size_t i = 0; i < colon; i++) {
    const wchar_t c = target[i];
    if (!std::iswalnum(c) && c != L'+' && c != L'.' && c != L'-') return false;
  }
  return !IsShellItem(target);
}

DockConfig ParseConfig(std::string_view text) {
  DockConfig config;
  const nlohmann::json root = ParseJsonc(text);
  if (!root.is_object()) {
    LogError(L"[config] dock.json no es JSON válido; valores por defecto");
    return config;
  }
  if (const auto* v = Find(root, "iconSize"); v && v->is_number()) config.iconSize = std::clamp(v->get<int>(), 16, 256);
  if (const auto* v = Find(root, "iconSpacing"); v && v->is_number()) config.iconSpacing = std::clamp(v->get<int>(), 0, 128);
  // [1, 2,5]: por encima, el bulto empuja tanto a los vecinos que el dock se sale de la
  // pantalla; 1 la apaga.
  if (const auto* v = Find(root, "magnification"); v && v->is_number())
    config.magnification = std::clamp(v->get<float>(), 1.0f, 2.5f);
  if (const auto* v = Find(root, "autoHide"); v && v->is_boolean()) config.autoHide = v->get<bool>();
  if (const auto* v = Find(root, "autoStart"); v && v->is_boolean()) config.autoStart = v->get<bool>();
  if (const auto* v = Find(root, "trash"); v && v->is_boolean()) config.trash = v->get<bool>();
  if (const auto* v = Find(root, "showRunning"); v && v->is_boolean()) config.showRunning = v->get<bool>();
  config.profileHotkey = Text(root, "atajoPerfil");
  config.apps = ParseApps(Find(root, "apps"));
  config.screens = ParseScreens(Find(root, "pantallas"));
  config.profiles = ParseScreens(Find(root, "perfiles"));
  return config;
}

DockConfig LoadConfig(const std::filesystem::path& file) {
  bool found = false;
  const std::string text = ReadAll(file, found);
  if (!found) {
    LogInfo(L"[config] no hay {}; valores por defecto", file.wstring());
    return {};
  }
  return ParseConfig(text);
}

LocalOverlay ParseLocal(std::string_view text) {
  const nlohmann::json root = ParseJsonc(text);
  if (!root.is_object()) {
    // Que este fichero esté roto no puede dejar sin dock: se sigue con dock.json.
    LogError(L"[config] dock.local.json ilegible; se ignora");
    return {};
  }
  LocalOverlay local = ParseLocalObject(root);
  RepairKeys(local);
  return local;
}

LocalOverlay LoadLocal(const std::filesystem::path& file) {
  bool found = false;
  const std::string text = ReadAll(file, found);
  return found ? ParseLocal(text) : LocalOverlay{};
}

std::vector<DockApp> Validate(const std::vector<DockApp>& apps) {
  std::vector<DockApp> valid;
  for (DockApp app : apps) {
    if (app.separator) {
      valid.push_back(app);
      continue;
    }
    if (app.target.empty()) {
      LogError(L"[config] omitida '{}': sin target", app.name);
      continue;
    }
    // A una URL no se le tocan las barras (dejaría de ser una URL); a su icono sí, que es
    // una ruta: el .ico de un acceso directo de Steam.
    if (IsUrl(app.target)) {
      app.iconTarget = Repair(Backslashes(app.iconTarget));
      valid.push_back(std::move(app));
      continue;
    }
    // Se normalizan todas, también las de shell:AppsFolder, para que en el JSON nunca haga
    // falta escapar una barra invertida. Repair antes de comprobar que existe: una app que se
    // ha actualizado sigue ahí, solo que una carpeta más allá.
    const std::wstring stored = Backslashes(app.target);
    app.target = Repair(stored);
    if (app.target != stored) LogInfo(L"[config] '{}' se actualizó: {}", app.name, app.target);
    if (!IsShellItem(app.target) && !Exists(app.target)) {
      LogError(L"[config] omitida '{}': no existe {}", app.name, app.target);
      continue;
    }
    app.iconTarget = Backslashes(app.iconTarget);
    valid.push_back(std::move(app));
  }
  return valid;
}

std::vector<DockApp> ApplyOverlay(const LocalOverlay& local, const std::vector<DockApp>& base) {
  const std::vector<std::wstring> baseKeys = KeysOf(base);
  std::vector<DockApp> result;
  for (size_t i = 0; i < base.size(); i++)
    if (!Contains(local.removed, baseKeys[i])) result.push_back(base[i]);

  // Las añadidas pasan el mismo filtro: si el usuario borró después el .exe que había
  // arrastrado, la entrada se cae sola.
  for (DockApp& app : Validate(local.added)) result.push_back(std::move(app));
  if (local.order.empty()) return result;

  // Orden estable: lo que no esté en "orden" va al final con el orden de dock.json, así que
  // añadir una app a mano en dock.json sigue funcionando aunque ya haya superposición.
  const std::vector<std::wstring> keys = KeysOf(result);
  std::vector<std::pair<size_t, size_t>> ranked;  // (rango, índice)
  for (size_t i = 0; i < result.size(); i++) {
    size_t rank = SIZE_MAX;
    for (size_t r = 0; r < local.order.size(); r++)
      if (Same(local.order[r], keys[i])) {
        rank = r;
        break;
      }
    ranked.emplace_back(rank, i);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<DockApp> ordered;
  for (const auto& [rank, index] : ranked) ordered.push_back(result[index]);
  return ordered;
}

ScreenApps ResolveFor(const DockConfig& config, const LocalOverlay& local, const std::wstring& device) {
  // El perfil activo manda sobre la lista de siempre, y dentro del perfil una pantalla puede
  // tener la suya. Un perfil guardado que ya no existe en dock.json se ignora.
  const auto profileIt = config.profiles.find(local.profile);
  const ScreenList* profile = profileIt == config.profiles.end() ? nullptr : &profileIt->second;
  const auto& screens = profile ? profile->screens : config.screens;
  const auto own = screens.find(device);
  const bool hasOwn = own != screens.end();

  ScreenApps result;
  result.base = Validate(hasOwn ? own->second.apps : profile ? profile->apps : config.apps);
  // La papelera entra en la lista BASE: se reordena y se quita arrastrando como cualquier
  // otra, y la superposición ya sabe recordar que se quitó.
  if (config.trash) result.base.push_back(DockApp{L"Papelera", kTrashTarget, L"", L"", false});

  // La clave lleva el perfil delante: reordenar en "juegos" no puede tocar "trabajo". Una
  // pantalla con lista propia, o un perfil, no heredan la superposición de por defecto: sería
  // meterles los iconos que el usuario arrastró a OTRA pantalla u otro perfil.
  const std::wstring key = local.profile.empty() ? device : local.profile + L"|" + device;
  static const LocalOverlay empty;
  const auto found = local.screens.find(key);
  const LocalOverlay& overlay = found != local.screens.end() ? found->second : (!hasOwn && !profile ? local : empty);
  result.apps = ApplyOverlay(overlay, result.base);
  return result;
}

std::optional<std::wstring> VersionPrefix(const std::wstring& folder) {
  // Lo que va antes del primer dígito, si detrás hay una versión: de "app-1.0.9258" sale
  // "app-". Tiene que haber prefijo: con "" la búsqueda se comería a todas las hermanas.
  const size_t digit = folder.find_first_of(L"0123456789");
  if (digit == std::wstring::npos || digit == 0 || !ParseVersion(folder.substr(digit))) return std::nullopt;
  return folder.substr(0, digit);
}

std::optional<std::wstring> Newest(const std::wstring& prefix, const std::optional<std::wstring>& a,
                                   const std::optional<std::wstring>& b) {
  if (!a) return b;
  if (!b) return a;
  // Por versión y no por texto: "1.0.10" va después de "1.0.9".
  const auto va = ParseVersion(a->substr(prefix.size()));
  const auto vb = ParseVersion(b->substr(prefix.size()));
  return va && vb && *vb > *va ? b : a;
}

std::wstring Repair(const std::wstring& target) {
  // Discord, Slack, Teams y lo empaquetado con Squirrel viven en ...\app-1.0.9258\x.exe: la
  // primera actualización deja la ruta anclada apuntando a una carpeta que ya no existe. Se
  // busca la hermana que empieza igual y tiene el mismo fichero, la de versión más alta.
  // ponytail: solo se mira la carpeta que contiene al fichero; un ...\app-1.2.3\bin\x.exe no
  // se seguiría.
  if (target.empty() || target[0] == L'|' || IsUrl(target) || IsShellItem(target)) return target;
  const std::filesystem::path path(target);
  const auto prefix = VersionPrefix(path.parent_path().filename().wstring());
  if (!prefix || Exists(target)) return target;

  const std::filesystem::path parent = path.parent_path().parent_path();
  const std::wstring file = path.filename().wstring();
  std::optional<std::wstring> best;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
    const std::wstring name = entry.path().filename().wstring();
    if (!entry.is_directory(ec) || _wcsnicmp(name.c_str(), prefix->c_str(), prefix->size()) != 0) continue;
    if (Exists((entry.path() / file).wstring())) best = Newest(*prefix, best, name);
  }
  return best ? (parent / *best / file).wstring() : target;
}

}  // namespace dock
