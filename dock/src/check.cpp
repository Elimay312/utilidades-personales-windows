// --check: lo que se puede comprobar sin enseñar nada. Cada fallo se imprime y el código de
// salida es el número de fallos.
#include "check.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "core/jsonc.h"
#include "model/config.h"
#include "model/magnify.h"
#include "system/icons.h"
#include "system/inventory.h"
#include "system/steam.h"
#include "ui/visuals.h"

namespace dock {
namespace {

int g_failures = 0;

void Expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("[check] FALLO: %s\n", what);
    g_failures++;
  }
}

void CheckJsonc() {
  // Comentarios de los dos tipos y comas finales en objeto y en array.
  const auto a = ParseJsonc(R"({
    // línea
    "a": 1, /* bloque */
    "b": [1, 2,],
  })");
  Expect(a.is_object() && a["a"] == 1 && a["b"].size() == 2, "comentarios y comas finales");

  // Lo que parece comentario o coma final dentro de una cadena se queda como está.
  const auto b = ParseJsonc(R"({"url": "https://x.com/a", "t": ",}", "e": "\"//"})");
  Expect(b.is_object() && b["url"] == "https://x.com/a" && b["t"] == ",}" && b["e"] == "\"//",
         "cadenas intactas");

  Expect(ParseJsonc("{roto").is_discarded(), "JSON roto no lanza");

  const auto c = ParseJsonc(R"({"IconSize": 64})");
  Expect(Find(c, "iconsize") && *Find(c, "iconSize") == 64, "claves sin distinguir mayúsculas");
  Expect(Find(c, "nada") == nullptr, "clave ausente");
}

void CheckConfig() {
  const DockConfig d = ParseConfig("{}");
  Expect(d.iconSize == 48 && d.iconSpacing == 16 && d.magnification == 1.3f && d.autoHide,
         "valores por defecto");

  const DockConfig e = ParseConfig(R"({"magnification": 9, "autoHide": false, "iconSize": 64,})");
  Expect(e.magnification == 2.5f, "magnificación acotada a 2,5");
  Expect(!e.autoHide && e.iconSize == 64, "lee autoHide e iconSize");

  const DockConfig f = ParseConfig("no es json");
  Expect(f.iconSize == 48, "config rota da valores por defecto");

  const auto g = Validate(ParseConfig(R"({"apps": [
    {"name": "Explorador", "target": "C:/Windows/explorer.exe"},
    {"separator": true},
    {"name": "Web", "target": "https://example.com/a/b"},
    {"name": "Sin target"},
    {"name": "No existe", "target": "C:/no/existe.exe"}
  ]})").apps);
  Expect(g.size() == 3, "validar: fuera la entrada sin target y la que no existe");
  Expect(g.size() == 3 && g[0].target == L"C:\\Windows\\explorer.exe", "validar: / pasa a \\");
  Expect(g.size() == 3 && g[1].separator, "validar: separador");
  Expect(g.size() == 3 && g[2].target == L"https://example.com/a/b", "validar: una URL no se toca");
  Expect(IsUrl(L"steam://rungameid/1") && !IsUrl(L"C:\\x") && !IsUrl(L"shell:RecycleBinFolder"), "IsUrl");

  // Las apps que se actualizan (Squirrel): la familia es lo que va antes del número, y se
  // compara por versión y no por texto.
  Expect(VersionPrefix(L"app-1.0.9258") == L"app-", "versión: app-1.0.9258");
  Expect(!VersionPrefix(L"Discord"), "versión: sin número no es familia");
  Expect(!VersionPrefix(L"1.0.9258"), "versión: sin prefijo no es familia");
  Expect(!VersionPrefix(L"Steam 2"), "versión: '2' no es una versión");
  Expect(Newest(L"app-", L"app-1.0.9", L"app-1.0.10") == L"app-1.0.10", "versión: 1.0.10 > 1.0.9");
  Expect(Newest(L"app-", L"app-1.0.10", L"app-1.0.9") == L"app-1.0.10", "versión: en cualquier orden");
  Expect(Newest(L"app-", std::nullopt, L"app-1.0.1") == L"app-1.0.1", "versión: gana a nada");
}

// La superposición de dock.local.json y a qué pantalla le toca cuál. Con elementos del shell
// y URLs, que Validate no busca en disco.
void CheckOverlay() {
  const DockApp a{L"A", L"shell:AppsFolder\\A!App"}, b{L"B", L"shell:AppsFolder\\B!App"};
  const DockApp c{L"C", L"https://c.example"}, sep{L"", L"", L"", L"", true};
  LocalOverlay local;
  local.order = {L"https://c.example", L"shell:appsfolder\\a!app"};  // sin distinguir mayúsculas
  local.removed = {L"shell:AppsFolder\\B!App"};
  local.added = {DockApp{L"D", L"https://d.example"}};
  const auto applied = ApplyOverlay(local, {a, b, sep, c});
  // Sin B; C y A primero por "orden"; el resto en el orden de dock.json.
  Expect(applied.size() == 4 && applied[0].name == L"C" && applied[1].name == L"A" && applied[2].separator &&
             applied[3].name == L"D",
         "superposición: quitar, añadir y ordenar");

  // El fichero real lleva las claves en mayúscula ("Orden", "Anadidas"...).
  const LocalOverlay parsed = ParseLocal(R"({"Orden": ["x"], "Quitadas": ["y"], "Perfil": "juegos",
    "Pantallas": {"\\\\.\\DISPLAY2": {"Orden": ["z"]}}})");
  Expect(parsed.order.size() == 1 && parsed.removed.size() == 1 && parsed.profile == L"juegos" &&
             parsed.screens.contains(L"\\\\.\\DISPLAY2") && parsed.screens.at(L"\\\\.\\DISPLAY2").order[0] == L"z",
         "dock.local.json con claves en mayúscula");

  DockConfig config = ParseConfig(R"({
    "apps": [{"name": "A", "target": "shell:AppsFolder/A!App"}],
    "pantallas": {"\\\\.\\DISPLAY3": {"apps": [{"name": "B", "target": "shell:AppsFolder/B!App"}]}},
    "perfiles": {"juegos": {"apps": [{"name": "C", "target": "https://c.example"}],
                            "pantallas": {"\\\\.\\DISPLAY3": {"apps": [{"name": "D", "target": "https://d.example"}]}}}}
  })");
  LocalOverlay none;
  auto one = ResolveFor(config, none, L"\\\\.\\DISPLAY1");
  Expect(one.apps.size() == 2 && one.apps[0].name == L"A" && one.apps[1].target == kTrashTarget,
         "resolver: lista de siempre y la papelera al final");
  Expect(ResolveFor(config, none, L"\\\\.\\DISPLAY3").apps[0].name == L"B", "resolver: la pantalla con lista propia");
  LocalOverlay games;
  games.profile = L"juegos";
  Expect(ResolveFor(config, games, L"\\\\.\\DISPLAY1").apps[0].name == L"C", "resolver: el perfil sustituye");
  Expect(ResolveFor(config, games, L"\\\\.\\DISPLAY3").apps[0].name == L"D",
         "resolver: la pantalla dentro del perfil");
  // Una pantalla con lista propia no hereda la superposición de por defecto.
  LocalOverlay rootOnly;
  rootOnly.removed = {L"shell:AppsFolder\\B!App", L"shell:AppsFolder\\A!App"};
  Expect(ResolveFor(config, rootOnly, L"\\\\.\\DISPLAY3").apps[0].name == L"B",
         "resolver: la pantalla propia no hereda la superposición raíz");
  Expect(ResolveFor(config, rootOnly, L"\\\\.\\DISPLAY1").apps.size() == 1, "resolver: la raíz sí se aplica");
  config.trash = false;
  Expect(ResolveFor(config, none, L"\\\\.\\DISPLAY1").apps.size() == 1, "resolver: trash false sin papelera");
}

void CheckSteamAndApps() {
  Expect(SteamAppIdOf(L"steam://rungameid/19680") == 19680u, "steam: rungameid");
  Expect(!SteamAppIdOf(L"C:\\juego.exe") && !SteamAppIdOf(L"steam://open/games"), "steam: lo que no es juego");
  // Un atajo que el usuario metió a mano en Steam lleva un id de 64 bits: no hay manifiesto.
  Expect(!SteamAppIdOf(L"steam://rungameid/12345678901234567890"), "steam: id de 64 bits");
  const std::string vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
                          "\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n";
  const auto paths = VdfValues(vdf, "path");
  Expect(paths.size() == 2 && paths[0] == L"C:\\Program Files (x86)\\Steam" && paths[1] == L"D:\\SteamLibrary",
         "steam: rutas del VDF sin barras dobladas");
  Expect(VdfValues("\"AppState\"\n{\n\t\"installdir\"\t\t\"Half-Life\"\n}\n", "installdir")[0] == L"Half-Life",
         "steam: installdir del manifiesto");
  Expect(!SteamMissExpired(true, 0, 999999999) && !SteamMissExpired(false, 0, 29999) && SteamMissExpired(false, 0, 30000),
         "steam: un acierto no caduca; un fallo, a los 30 s");

  Expect(IsApp(DockApp{L"", L"C:\\x\\app.exe"}) && IsApp(DockApp{L"", L"shell:AppsFolder\\A!App"}), "IsApp: .exe y shell");
  Expect(!IsApp(DockApp{L"", L"C:\\notas.txt"}) && !IsApp(DockApp{L"", L"https://x.com"}) &&
             !IsApp(DockApp{L"", kTrashTarget}),
         "IsApp: documento, URL y papelera no");
}

// A propósito con ranuras de anchos DISTINTOS: el caso general desde que hay separadores.
void CheckMagnify() {
  const Slot icon{80, 60}, separator{24, 2};
  const Curve curve({icon, icon, separator, icon, icon, separator, icon}, 200, 2);
  const float width = curve.RestWidth() + curve.MaxGrowth();

  for (float c : {0.0f, 30.0f, 120.0f, curve.RestWidth() * 0.5f, curve.RestWidth() - 40, curve.RestWidth()}) {
    // Las ranuras particionan la fila: sin perder ni ganar píxeles, y sin solaparse.
    float sum = 0;
    for (int i = 0; i < curve.Count(); i++) {
      const float from = curve.Project(curve.SlotStart(i), width, c);
      const float to = curve.Project(curve.SlotStart(i) + curve.At(i).width, width, c);
      Expect(to > from, "curva: ranura de ancho <= 0");
      sum += to - from;
    }
    const float total = curve.Project(curve.RestWidth(), width, c) - curve.Project(0, width, c);
    Expect(std::abs(sum - total) < 0.01f, "curva: las ranuras no particionan la fila");
    for (int i = 0; i + 1 < curve.Count(); i++)
      Expect(curve.Project(curve.RestLeft(i + 1), width, c) >= curve.Project(curve.RestRight(i), width, c) - 0.001f,
             "curva: dos elementos se solapan");
    // Monótona: si no, los iconos se cruzarían.
    float previous = -1e9f;
    for (float u = 0; u <= curve.RestWidth(); u += curve.RestWidth() / 200) {
      const float now = curve.Project(u, width, c);
      Expect(now > previous, "curva: T no es monótona");
      previous = now;
    }
  }
  // Mientras el bulto cabe dentro, el ancho no "respira".
  for (float c = curve.Radius(); c <= curve.RestWidth() - curve.Radius(); c += 10) {
    const float w = curve.Transfer(curve.RestWidth(), c) - curve.Transfer(0, c);
    Expect(std::abs(w - (curve.RestWidth() + curve.MaxGrowth())) < 0.01f, "curva: el ancho respira");
  }
  // La inversión devuelve el punto (el de C# pedía < 0,5 px; 40 pasos dan mucho más).
  for (float u = 0; u <= curve.RestWidth(); u += 37) {
    const float back = curve.Invert(curve.Project(u, width, u), width);
    Expect(std::abs(back - u) < 0.01f, "curva: la inversión no devuelve el punto");
  }
  for (int i = 0; i < curve.Count(); i++)
    Expect(curve.SlotAt(curve.SlotStart(i) + curve.At(i).width * 0.5f) == i, "curva: el centro no cae en su ranura");
}

void CheckIcons() {
  std::vector<uint8_t> raw{200, 100, 50, 128};  // sin premultiplicar: canal > alfa
  PremultiplyIfNeeded(raw);
  Expect(raw[0] == 100 && raw[1] == 50 && raw[2] == 25, "premultiplica lo que no lo estaba");
  std::vector<uint8_t> already{50, 40, 30, 128};
  PremultiplyIfNeeded(already);
  Expect(already[0] == 50 && already[1] == 40, "no premultiplica dos veces");

  IconBitmap block{8, 8, std::vector<uint8_t>(8 * 8 * 4, 0)};
  for (int y = 2; y < 7; y++)
    for (int x = 1; x < 4; x++) block.bgra[(y * 8 + x) * 4 + 3] = 255;
  Expect(DrawnSide(block) == 5, "lado del dibujo 3x5 = 5");
  IconBitmap frame{8, 8, std::vector<uint8_t>(8 * 8 * 4, 0)};
  for (size_t i = 3; i < frame.bgra.size(); i += 4) frame.bgra[i] = 38;
  Expect(DrawnSide(frame) == 0, "el marco de miniatura (alfa 38) no cuenta");
}

// Las expresiones sobre un Compositor de verdad y con 40 iconos: el límite de longitud se
// alcanza antes de lo que parece, y esto es lo que avisa si una fórmula crece de más.
void CheckExpressions() {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DockCheck";
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, L"DockCheck", L"", WS_POPUP, 0, 0, 800,
                              300, nullptr, nullptr, wc.hInstance, nullptr);
  std::vector<Slot> slots;
  std::vector<DockItem> items;
  for (int i = 0; i < 40; i++) {
    const bool sep = i % 10 == 9;
    slots.push_back(sep ? Slot{19, 3} : Slot{64, 48});
    items.push_back({sep ? L"" : L"Una app con nombre largo", L"", sep});
  }
  try {
    Visuals visuals(hwnd);
    visuals.Build(Curve(slots, 112, 2.5f), items, {}, 2560, 287, 12, 48, 1.0f);
    visuals.SetCursor(300);
    visuals.SetHover(true);
    visuals.SetLabel(3);
  } catch (const winrt::hresult_error& e) {
    std::printf("[check] FALLO: expresiones con 40 iconos: %08X %ls\n", static_cast<unsigned>(e.code().value),
                e.message().c_str());
    g_failures++;
  }
  DestroyWindow(hwnd);
}

// La extracción de verdad de los iconos de la config, desde el worker STA como el dock.
//
// El fallo de extraer desde MTA no da error: devuelve el icono genérico (la hoja en blanco,
// 35789 píxeles opacos a 256). Y solo se nota con manejadores de apartamento, como los .url
// de Steam: con .exe, carpetas y elementos del shell MTA y STA dan lo mismo. Así que se
// compara cada icono con el genérico de verdad, el de una extensión que nadie tiene asociada.
void CheckExtraction(const std::filesystem::path& configPath) {
  // Lo que ve la pantalla principal, resuelto como lo resuelve el dock (dock.local.json incluido).
  MONITORINFOEXW primary{};
  primary.cbSize = sizeof(primary);
  GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary);
  const std::vector<DockApp> apps =
      ResolveFor(LoadConfig(configPath), LoadLocal(configPath.parent_path() / L"dock.local.json"), primary.szDevice).apps;
  std::printf("[check] %ls: %zu entradas\n", primary.szDevice, apps.size());
  const std::filesystem::path unknown = std::filesystem::temp_directory_path() / L"dock-check.sin-asociar";
  { FILE* f = nullptr; _wfopen_s(&f, unknown.c_str(), L"wb"); if (f) fclose(f); }
  // Por el mismo camino que el dock: el proceso hijo.
  std::vector<std::wstring> keys{unknown.wstring()};
  for (const DockApp& app : apps)
    if (!app.separator) keys.push_back(app.IconSource());
  const IconSet icons = ExtractIconsOutOfProcess(keys);
  {
    const auto found = icons.find(unknown.wstring());
    const IconBitmap* generic = found == icons.end() ? nullptr : &found->second;
    for (const DockApp& app : apps) {
      if (app.separator) continue;
      const auto it = icons.find(app.IconSource());
      const IconBitmap* icon = it == icons.end() ? nullptr : &it->second;
      if (!icon) {
        std::printf("[check] FALLO: sin icono para %ls\n", app.name.c_str());
        g_failures++;
        continue;
      }
      if (generic && icon->width == generic->width && icon->bgra == generic->bgra) {
        std::printf("[check] FALLO: %ls sale con el icono genérico (¿extraído fuera de STA?)\n", app.name.c_str());
        g_failures++;
      }
      int opaque = 0, transparent = 0;
      for (size_t i = 3; i < icon->bgra.size(); i += 4) {
        if (icon->bgra[i] == 255) opaque++;
        else if (icon->bgra[i] == 0) transparent++;
      }
      std::printf("[check] %-16ls %dx%d ocupa=%d%% opacos=%d transparentes=%d\n", app.name.c_str(), icon->width,
                  icon->height, 100 * DrawnSide(*icon) / icon->width, opaque, transparent);
    }
  }
  std::error_code ec;
  std::filesystem::remove(unknown, ec);
}

}  // namespace

int RunChecks(const std::filesystem::path& configPath) {
  CheckJsonc();
  CheckConfig();
  CheckOverlay();
  CheckSteamAndApps();
  CheckMagnify();
  CheckIcons();
  CheckExpressions();
  CheckExtraction(configPath);
  std::printf("[check] %s (%d fallos)\n", g_failures ? "MAL" : "OK", g_failures);
  return g_failures;
}

}  // namespace dock
