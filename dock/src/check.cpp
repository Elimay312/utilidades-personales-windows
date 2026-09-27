// --check: lógica pura, sin abrir ventanas. Cada fallo se imprime y el código de salida es
// el número de fallos.
#include "check.h"

#include <cstdio>
#include <string>

#include "core/jsonc.h"
#include "model/config.h"

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
}

}  // namespace

int RunChecks() {
  CheckJsonc();
  CheckConfig();
  std::printf("[check] %s (%d fallos)\n", g_failures ? "MAL" : "OK", g_failures);
  return g_failures;
}

}  // namespace dock
