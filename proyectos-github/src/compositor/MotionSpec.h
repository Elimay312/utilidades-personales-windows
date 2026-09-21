#pragma once

// Los cuatro muelles de CLAUDE.md, y qué hacer cuando el sistema pide no moverse.
//
// Puro y con pruebas: decidir la animación y crearla son dos cosas distintas, y solo la
// segunda necesita un Compositor. Motion.h hace la segunda.

namespace Motion {

enum class Kind {
    // Hover, pulsar, marcar.
    Snappy,
    // Paneles e inspector.
    Standard,
    // Reordenar y mover tarjetas entre grupos.
    Smooth,
    // Hojas modales y revisión semanal.
    Expressive,
};

struct Spring {
    float dampingRatio = 1.0f;
    // OJO: es el periodo NO amortiguado del muelle, no lo que dura la animación. Un
    // muelle de 350 ms de periodo no tarda 350 ms en llegar; ver SettleMs.
    float periodMs = 0.0f;

    constexpr bool operator==(const Spring&) const = default;
};

inline constexpr Spring kSnappy{0.9f, 120.0f};
inline constexpr Spring kStandard{0.85f, 220.0f};
inline constexpr Spring kSmooth{0.8f, 300.0f};
inline constexpr Spring kExpressive{0.75f, 350.0f};

// El fundido que sustituye al movimiento cuando "Mostrar animaciones en Windows" está
// apagado. Corto a propósito: quien apaga las animaciones no quiere media transición,
// quiere que el cambio ya esté hecho.
inline constexpr float kReducedFadeMs = 120.0f;

// El cruce del cambio de tema. Es duración fija y no muelle: no se mueve nada, solo se
// funde, y un muelle sobre una opacidad que va de 0 a 1 no aporta física ninguna.
inline constexpr float kThemeCrossfadeMs = 250.0f;

struct Resolved {
    // false: escribir el valor final directamente y limitarse a fundir.
    bool animate = true;
    Spring spring;
    float fadeMs = 0.0f;
};

Resolved Resolve(Kind kind, bool systemAnimationsEnabled);

constexpr Spring SpringFor(Kind kind) {
    switch (kind) {
    case Kind::Snappy:     return kSnappy;
    case Kind::Standard:   return kStandard;
    case Kind::Smooth:     return kSmooth;
    case Kind::Expressive: return kExpressive;
    }
    return kStandard;
}

// Cuánto tarda de verdad en asentarse, dentro del 2 %: 4/(ζ·ωn) con ωn = 2π/T.
// Sirve para elegir a ojo y para que las pruebas avisen si alguien toca la tabla y se le
// va a dos segundos sin darse cuenta.
float SettleMs(Spring spring);

}  // namespace Motion
