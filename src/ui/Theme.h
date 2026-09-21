#pragma once

#include <imgui.h>

#include <span>

namespace Theme {

constexpr ImVec4 Rgb(unsigned int hex) {
    return ImVec4(static_cast<float>((hex >> 16) & 0xFFu) / 255.0f,
                  static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,
                  static_cast<float>(hex & 0xFFu) / 255.0f, 1.0f);
}

// Variables y no constantes: el config puede cambiarlas al arrancar. Quien pinta las lee
// en cada frame, asi que no hay nada mas que hacer para que el cambio se vea.
inline ImVec4 kBackground = Rgb(0x1e1e1e);
inline ImVec4 kPanel      = Rgb(0x252526);
inline ImVec4 kSelection  = Rgb(0x264f78);
inline ImVec4 kText       = Rgb(0xd4d4d4);
inline ImVec4 kTextDim    = Rgb(0x808080);
inline ImVec4 kAccent     = Rgb(0x4fc1ff);
// Fila marcada: el acento translucido, que se pinta encima de la seleccion. Lo recalcula
// Apply, porque el acento puede venir del config.
inline ImVec4 kMarked = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f);

// Los colores por su nombre en el config. Es la misma tabla que escribe los valores por
// defecto la primera vez, asi que no hay dos listas de hexadecimales que desincronizar.
struct NamedColor {
    const wchar_t* name;
    ImVec4* color;
};
std::span<const NamedColor> Colors();

unsigned ToHex(const ImVec4& color);

// Colores y metricas del estilo, escaladas al DPI del monitor.
void Apply(float dpiScale);

// Segoe UI mas las fuentes de respaldo para CJK, hangul y simbolos. Se llama una sola
// vez: desde ImGui 1.92 el atlas es dinamico y los glifos se rasterizan bajo demanda,
// asi que un cambio de DPI no obliga a reconstruir nada.
void LoadFont();

}  // namespace Theme
