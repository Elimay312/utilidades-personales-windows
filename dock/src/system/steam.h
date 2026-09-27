#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dock {

// Un juego de Steam anclado no nombra ningún ejecutable, solo steam://rungameid/19680 (o un
// .url que lo lleva): para encender su puntito hay que saber en qué carpeta está instalado.
// Solo se lee lo de los juegos anclados, nunca la biblioteca entera.
std::optional<std::wstring> SteamFolderOf(const std::wstring& target);

// Lo puro, para --check.
std::optional<unsigned> SteamAppIdOf(const std::wstring& target);
std::vector<std::wstring> VdfValues(const std::string& vdf, const std::string& key);
// Caché: un acierto vale toda la sesión; un fallo caduca a los 30 s, o un juego recién
// instalado no se encendería hasta reiniciar el dock.
bool SteamMissExpired(bool found, unsigned long long when, unsigned long long now);

}  // namespace dock
