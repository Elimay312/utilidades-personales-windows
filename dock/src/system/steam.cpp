#include "system/steam.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "core/log.h"

namespace dock {
namespace {

constexpr wchar_t kPrefix[] = L"steam://rungameid/";
constexpr unsigned long long kMissMs = 30000;

std::string ReadFile(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream text;
  if (in) text << in.rdbuf();
  return text.str();
}

std::wstring Widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
  return out;
}

// El URL= de un .url (formato INI). Es lo que Steam pone en sus accesos del escritorio.
std::wstring UrlOfShortcut(const std::wstring& path) {
  std::istringstream lines(ReadFile(path));
  for (std::string line; std::getline(lines, line);) {
    if (line.rfind("URL=", 0) == 0) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      return Widen(line.substr(4));
    }
  }
  return {};
}

std::optional<std::wstring> Root() {
  wchar_t path[MAX_PATH]{};
  DWORD size = sizeof(path);
  if (RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Valve\Steam)", L"SteamPath", RRF_RT_REG_SZ, nullptr, path, &size) !=
      ERROR_SUCCESS)
    return std::nullopt;
  std::wstring root = path;
  std::replace(root.begin(), root.end(), L'/', L'\\');  // Steam la guarda con barras normales
  std::error_code ec;
  return std::filesystem::is_directory(root, ec) ? std::optional(root) : std::nullopt;
}

std::optional<std::wstring> Resolve(unsigned id) {
  const auto root = Root();
  if (!root) return std::nullopt;
  std::vector<std::wstring> libraries{*root};
  for (auto& library : VdfValues(ReadFile(std::filesystem::path(*root) / L"steamapps" / L"libraryfolders.vdf"), "path"))
    libraries.push_back(library);

  for (const auto& library : libraries) {
    const std::filesystem::path steamapps = std::filesystem::path(library) / L"steamapps";
    // Del manifiesto se saca SOLO la carpeta: el nombre, las fechas y el tiempo jugado
    // están al lado y no se miran.
    const auto dirs = VdfValues(ReadFile(steamapps / (L"appmanifest_" + std::to_wstring(id) + L".acf")), "installdir");
    if (dirs.empty()) continue;
    const std::filesystem::path folder = steamapps / L"common" / dirs.front();
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) continue;
    LogInfo(L"[steam] {} está instalado en {}", id, folder.wstring());
    return folder.wstring();
  }
  return std::nullopt;
}

}  // namespace

std::optional<unsigned> SteamAppIdOf(const std::wstring& target) {
  if (target.size() > 4 && _wcsicmp(target.c_str() + target.size() - 4, L".url") == 0)
    return SteamAppIdOf(UrlOfShortcut(target));
  if (_wcsnicmp(target.c_str(), kPrefix, wcslen(kPrefix)) != 0) return std::nullopt;
  const std::wstring digits = target.substr(wcslen(kPrefix));
  // Un rungameid de 64 bits es un atajo que el usuario metió en Steam a mano, no un juego
  // de la tienda: no tiene appmanifest y no hay nada que buscar.
  if (digits.empty() || digits.size() > 10 || !std::all_of(digits.begin(), digits.end(), iswdigit)) return std::nullopt;
  const unsigned long long id = std::stoull(digits);
  return id <= 0xFFFFFFFFull ? std::optional<unsigned>(static_cast<unsigned>(id)) : std::nullopt;
}

std::vector<std::wstring> VdfValues(const std::string& vdf, const std::string& key) {
  // Línea a línea: "clave"  "valor". VDF dobla las barras invertidas.
  std::vector<std::wstring> found;
  std::istringstream lines(vdf);
  for (std::string line; std::getline(lines, line);) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t quote; (quote = line.find('"', start)) != std::string::npos; start = quote + 1)
      parts.push_back(line.substr(start, quote - start));
    parts.push_back(line.substr(start));
    if (parts.size() < 4 || _stricmp(parts[1].c_str(), key.c_str()) != 0) continue;
    std::string value = parts[3];
    for (size_t at; (at = value.find("\\\\")) != std::string::npos;) value.replace(at, 2, "\\");
    found.push_back(Widen(value));
  }
  return found;
}

bool SteamMissExpired(bool found, unsigned long long when, unsigned long long now) {
  return !found && now - when >= kMissMs;
}

std::optional<std::wstring> SteamFolderOf(const std::wstring& target) {
  const auto id = SteamAppIdOf(target);
  if (!id) return std::nullopt;
  // Solo el hilo de UI llama aquí: sin cerrojo.
  static std::map<std::wstring, std::pair<std::optional<std::wstring>, unsigned long long>> cache;
  const unsigned long long now = GetTickCount64();
  if (auto hit = cache.find(target); hit != cache.end() && !SteamMissExpired(hit->second.first.has_value(), hit->second.second, now))
    return hit->second.first;
  auto folder = Resolve(*id);
  cache[target] = {folder, now};
  return folder;
}

}  // namespace dock
