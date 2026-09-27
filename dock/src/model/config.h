#pragma once

#include <filesystem>
#include <string_view>

namespace dock {

// Lo que el dock lee de dock.json. De momento solo la geometría y el autoocultar; las apps,
// las pantallas y los perfiles llegan en F5.
struct DockConfig {
  int iconSize = 48;         // px lógicos (a 96 ppp) del icono en reposo
  int iconSpacing = 16;      // hueco entre iconos
  float magnification = 1.3f;
  bool autoHide = true;
};

DockConfig ParseConfig(std::string_view text);

// Un dock.json que falta o está roto no impide arrancar: se usan los valores por defecto y
// se dice en el log.
DockConfig LoadConfig(const std::filesystem::path& file);

}  // namespace dock
