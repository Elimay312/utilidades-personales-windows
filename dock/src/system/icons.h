#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dock {

// Píxeles BGRA premultiplicados, filas de arriba abajo, como los quiere Composition.
struct IconBitmap {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> bgra;
};

// Los píxeles de cada icono, por clave (ruta, elemento del shell o URL).
using IconSet = std::map<std::wstring, IconBitmap>;

// Extrae los iconos en un proceso hijo (Dock.exe --extraer) y los devuelve. Medido con diez
// apps y tres pantallas: extraer dentro del dock cargaba la maquinaria del shell
// (AppsFolder, la papelera, los manejadores de .url) y dejaba el proceso en 18,2 MB privados
// y 12 hilos contra 2,5 MB y 6 sin ella, para siempre. En un hijo todo eso muere con él.
// Bloquea: se llama desde el worker, nunca desde el hilo de UI.
IconSet ExtractIconsOutOfProcess(const std::vector<std::wstring>& keys);

// El modo --extraer: lee las claves de stdin y escribe los iconos en stdout.
int RunExtractor();

// El icono del shell de una ruta, elemento del shell (shell:...) o URL (la cara de la app
// que la abre). Tiene que llamarse desde un hilo STA: desde MTA los manejadores de iconos
// de apartamento no se cargan y GetImage devuelve el genérico sin error (un .url de Steam
// daba una hoja en blanco).
std::optional<IconBitmap> ExtractIcon(const std::wstring& target);

// Premultiplica solo si algún canal supera al alfa, que es la señal de que el mapa de bits
// NO venía premultiplicado: premultiplicar dos veces oscurece los bordes.
void PremultiplyIfNeeded(std::vector<uint8_t>& bgra);

// Lado de la caja del dibujo (alfa ≥ 128) dentro del lienzo. El marco de miniatura del
// shell tiene alfa 38 y no cuenta.
int DrawnSide(const IconBitmap& icon);

}  // namespace dock
