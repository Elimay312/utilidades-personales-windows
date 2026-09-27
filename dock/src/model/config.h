#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dock {

// Una entrada del dock. target puede ser un .exe, una carpeta, un documento, un elemento del
// shell (shell:AppsFolder\<AUMID>, shell:RecycleBinFolder) o una URL.
struct DockApp {
  std::wstring name;
  std::wstring target;
  std::wstring iconTarget;  // de dónde sale el icono si no es del target (un .ico de un .lnk)
  std::wstring arguments;
  bool separator = false;

  const std::wstring& IconSource() const { return iconTarget.empty() ? target : iconTarget; }
};

// Lo que el dock lee de dock.json. Las pantallas, los perfiles y dock.local.json llegan en F5.
struct DockConfig {
  int iconSize = 48;         // px lógicos (a 96 ppp) del icono en reposo
  int iconSpacing = 16;      // hueco entre iconos
  float magnification = 1.3f;
  bool autoHide = true;
  std::vector<DockApp> apps;
};

// "https", "steam"... pero no "C:" ni "shell:". Una URL nunca se normaliza.
bool IsUrl(const std::wstring& target);

DockConfig ParseConfig(std::string_view text);

// Un dock.json que falta o está roto no impide arrancar: se usan los valores por defecto y
// se dice en el log.
DockConfig LoadConfig(const std::filesystem::path& file);

}  // namespace dock
