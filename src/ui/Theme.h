#pragma once

#include <imgui.h>

namespace Theme {

constexpr ImVec4 Rgb(unsigned int hex) {
    return ImVec4(static_cast<float>((hex >> 16) & 0xFFu) / 255.0f,
                  static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,
                  static_cast<float>(hex & 0xFFu) / 255.0f, 1.0f);
}

inline constexpr ImVec4 kBackground = Rgb(0x1e1e1e);
inline constexpr ImVec4 kPanel      = Rgb(0x252526);
inline constexpr ImVec4 kSelection  = Rgb(0x264f78);
inline constexpr ImVec4 kText       = Rgb(0xd4d4d4);
inline constexpr ImVec4 kTextDim    = Rgb(0x808080);
inline constexpr ImVec4 kAccent     = Rgb(0x4fc1ff);

// Colores y metricas del estilo, escaladas al DPI del monitor.
void Apply(float dpiScale);

// Segoe UI mas las fuentes de respaldo para CJK, hangul y simbolos. Se llama una sola
// vez: desde ImGui 1.92 el atlas es dinamico y los glifos se rasterizan bajo demanda,
// asi que un cambio de DPI no obliga a reconstruir nada.
void LoadFont();

}  // namespace Theme
