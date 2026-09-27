#pragma once

#include <filesystem>
#include <map>
#include <optional>
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

inline constexpr wchar_t kTrashTarget[] = L"shell:RecycleBinFolder";

// Lo que una pantalla (o un perfil) quiere ver. Dentro de un perfil, una pantalla puede llevar
// su propia lista; en un bloque de "pantallas" normal, screens se queda vacío.
struct ScreenList {
  std::vector<DockApp> apps;
  std::map<std::wstring, ScreenList> screens;
};

// dock.json: del usuario, con sus comentarios. El dock nunca lo escribe.
struct DockConfig {
  int iconSize = 48;          // px lógicos (a 96 ppp) del icono en reposo
  int iconSpacing = 16;       // hueco entre iconos
  float magnification = 1.3f;
  bool autoHide = true;
  bool autoStart = false;
  bool trash = true;          // la papelera al final, como en macOS
  bool showRunning = true;    // las apps abiertas sin anclar, detrás de un separador
  std::wstring profileHotkey; // "atajoPerfil"
  std::vector<DockApp> apps;
  std::map<std::wstring, ScreenList> screens;   // "pantallas", por nombre de dispositivo
  std::map<std::wstring, ScreenList> profiles;  // "perfiles"
};

// dock.local.json: lo que el dock cambia por su cuenta (orden, añadidas, quitadas, perfil).
// Vive aparte de dock.json para no reescribir los comentarios del usuario, y borrarlo
// devuelve la configuración escrita a mano. Mismo formato que el del dock de C#.
struct LocalOverlay {
  std::vector<std::wstring> order;
  std::vector<DockApp> added;
  std::vector<std::wstring> removed;
  std::map<std::wstring, LocalOverlay> screens;  // clave "perfil|pantalla" o "pantalla"
  std::wstring profile;
};

// La lista de una pantalla: base es lo que dice dock.json (hace falta para deducir la
// superposición al guardar), apps es lo que se ve.
struct ScreenApps {
  std::vector<DockApp> base;
  std::vector<DockApp> apps;
};

// "https", "steam"... pero no "C:" ni "shell:". Una URL nunca se normaliza.
bool IsUrl(const std::wstring& target);

DockConfig ParseConfig(std::string_view text);
DockConfig LoadConfig(const std::filesystem::path& file);
LocalOverlay ParseLocal(std::string_view text);
LocalOverlay LoadLocal(const std::filesystem::path& file);

// Escribe la superposición de ESTA pantalla y ESTE perfil, deducida de la diferencia entre lo
// que dice dock.json (base) y lo que hay ahora (current, solo anclado): así no hay dos estados
// que mantener a la par. El resto del fichero se conserva.
bool SaveLocal(const std::filesystem::path& file, const std::wstring& device, const std::vector<DockApp>& base,
               const std::vector<DockApp>& current);
// Solo cambia el perfil activo; el resto del fichero se conserva.
bool SaveProfile(const std::filesystem::path& file, const std::wstring& profile);
// El que toca tras current. La vuelta incluye "sin perfil" (vacío): siempre se puede volver a
// la lista de siempre sin editar nada. Un perfil que ya no existe sigue por el principio.
std::wstring NextProfile(const DockConfig& config, const std::wstring& current);
// Con las claves en mayúscula del dock de C#, que lee el mismo fichero mientras convivan.
std::string LocalToJson(const LocalOverlay& local);

// Resuelve la pantalla: perfil activo, lista propia de la pantalla, papelera y superposición.
ScreenApps ResolveFor(const DockConfig& config, const LocalOverlay& local, const std::wstring& device);

// Las entradas utilizables, con las rutas normalizadas y reparadas. Una entrada mala no
// tumba el dock: se avisa en el log y se omite.
std::vector<DockApp> Validate(const std::vector<DockApp>& apps);
std::vector<DockApp> ApplyOverlay(const LocalOverlay& local, const std::vector<DockApp>& base);

// Sigue a una app que se ha mudado de carpeta al actualizarse (Squirrel: Discord, Slack...).
std::wstring Repair(const std::wstring& target);
std::optional<std::wstring> VersionPrefix(const std::wstring& folder);
std::optional<std::wstring> Newest(const std::wstring& prefix, const std::optional<std::wstring>& a,
                                   const std::optional<std::wstring>& b);

}  // namespace dock
