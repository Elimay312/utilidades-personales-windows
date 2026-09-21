#pragma once

// La tabla de colores de CLAUDE.md, en sus dos columnas. Puro y con pruebas: los colores
// son datos, y tenerlos aquí en vez de repartidos por el dibujo es lo que permite que el
// cambio de tema sea "vuelve a pintar con estos otros" y no una cacería.
//
// Sin WinRT ni Win32: el acento del sistema lo trae ThemeWatcher y entra por parámetro.

#include <cstdint>

namespace Theme {

struct Color {
    std::uint8_t a = 255;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    constexpr bool operator==(const Color&) const = default;
};

// 0xRRGGBB, opaco.
constexpr Color Rgb(std::uint32_t hex) {
    return Color{255,
                 static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                 static_cast<std::uint8_t>((hex >> 8) & 0xFF),
                 static_cast<std::uint8_t>(hex & 0xFF)};
}

// El alfa en 0..1 como lo escribe la tabla de CLAUDE.md.
constexpr Color Rgba(std::uint32_t hex, float alpha) {
    Color color = Rgb(hex);
    color.a = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f);
    return color;
}

enum class Appearance { Light, Dark };

// El acento cuando el sistema no lo da. Es el azul de macOS, que es el que pide la
// estética del proyecto.
inline constexpr Color kAccentFallback = Rgb(0x0a84ff);

struct Tokens {
    Color textPrimary;
    Color textSecondary;
    Color separator;
    Color cardSurface;
    // No sale en la tabla de CLAUDE.md: es el velo de la barra lateral y el inspector,
    // el "material más translúcido". Va como color sobre la Mica y no como acrílico
    // porque CreateHostBackdropBrush se pinta negro en Win32 sin empaquetar — medido en
    // la isla, y los cuatro vecinos que creen tenerlo no lo tienen.
    Color sidebarVeil;
    Color accent;

    // Prioridades. Iguales en claro y oscuro: son etiquetas, y que cambien de tono al
    // cambiar el tema haría que dejaran de ser reconocibles de un vistazo.
    Color priorityFocus;
    Color prioritySecondary;
    Color prioritySomeday;
    Color priorityArchived;

    // Actividad.
    Color activityActive;
    Color activityPaused;
    Color activityDormant;

    // --- Controles ------------------------------------------------------------------
    // Velos sobre la Mica, no colores opacos: debajo hay material y taparlo con un gris
    // plano es exactamente lo que hace que una app no parezca de Mac.
    Color controlFill;
    Color controlHover;
    Color controlPressed;
    Color controlStroke;

    // --- Acento ---------------------------------------------------------------------
    // Los estados del botón primario se DERIVAN del acento en vez de ser dos hexadecimales
    // más, porque el acento lo elige Windows y puede ser cualquier cosa.
    Color accentHover;
    Color accentPressed;
    // Blanco no: el acento del sistema puede ser un amarillo, y entonces el texto
    // desaparece. Se decide por luminancia, igual que el tema.
    Color textOnAccent;
    Color textDisabled;

    // --- Foco y selección -------------------------------------------------------------
    Color focusRing;
    Color selectionText;  // el resalte de la selección dentro del campo de texto
    Color selectionRow;   // la fila seleccionada de una lista

    // --- Superficies flotantes ----------------------------------------------------------
    // Más opacas que cardSurface a propósito: no hay desenfoque detrás —el acrílico se
    // pinta negro en Win32 sin empaquetar, medido en la fase 1—, así que un menú
    // translúcido sobre contenido denso no se lee.
    Color menuSurface;
    Color toastSurface;
    Color fieldSurface;  // un pozo, no un relieve: más oscuro que el fondo en los dos temas
    Color scrim;
    Color shadow;
};

Tokens TokensFor(Appearance appearance, Color accent);

// Windows no dice "claro" u "oscuro". Lo que sí da es el color con el que escribe, y de
// ahí se deduce: si el texto del sistema es claro, el fondo es oscuro.
//
// Se hace por el texto y NO por el fondo, y es una corrección medida: en una aplicación
// de escritorio, UIColorType::Background devuelve negro SIEMPRE, con Windows en claro y
// en oscuro. Con él, el tema salía oscuro también en claro, y como la máquina de
// desarrollo estaba en oscuro, parecía que funcionaba.
Appearance AppearanceFromForeground(Color foreground);

// Texto legible sobre un fondo cualquiera. Misma luminancia que AppearanceFromForeground
// y por el mismo motivo: un canal solo se equivoca.
Color OnColor(Color background);

// Aclara (amount > 0) u oscurece (amount < 0) sin salirse de 0..255. De aquí salen los
// estados del botón primario, que no pueden estar en la tabla porque dependen del acento.
Color Shade(Color color, float amount);

}  // namespace Theme
