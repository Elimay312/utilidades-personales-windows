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
    // El periodo NO amortiguado del muelle, que es también su DURACIÓN PERCEPTUAL: el
    // tiempo tras el cual el movimiento se lee como terminado aunque todavía le quede un
    // rabo de milímetros. Es el mando con el que se afina — ver la tabla de abajo—; lo que
    // tarda en quedarse completamente quieto es otra cosa y se llama SettleMs.
    float periodMs = 0.0f;

    constexpr bool operator==(const Spring&) const = default;
};

// LOS PERIODOS SON LA DURACIÓN PERCEPTUAL, y eso no es una manera de hablar: Composition
// define Period como «el tiempo que tarda el muelle en completar una oscilación» y Apple
// define Spring(duration:bounce:) con rigidez (2π/duration)² y masa 1. Las dos cosas fijan
// la misma ωn = 2π/T, así que `periodMs` ES el `duration` de Apple y `dampingRatio` es su
// `1 - bounce`. La tabla de Brújula ya hablaba ese idioma sin saberlo.
//
// Y de ahí sale la corrección de la fase 8. Las fases 1 y 6 afinaron contra SettleMs, que
// es lo que Apple llama *settling duration* y de lo que dice explícitamente que no se
// afina: «depends on many different factors and can be unpredictable». Lo que se afina es
// el periodo, porque es el número que se eligió para ser predecible. Afinando el otro, la
// fase 6 dejó los cuatro muelles entre 80 y 195 ms de duración perceptual — los tres
// presets de iOS están los tres en 500— y lo que se sintió al usarla no fue rapidez sino
// un corte. Que es lo que quiere decir «tosco».
//
// Los de ahora: ni los 120/220/300/350 de la fase 1, que se sintieron lentos para ir de un
// proyecto a otro, ni los 80/130/165/195 de la fase 6, que no llegan a moverse. Y la
// escala se ABRE —de 2,4× entre el primero y el último a 2,6×— porque lo que de verdad
// pide tiempo es la distancia recorrida: un anillo de foco que crece un 4 % y una hoja
// modal que cruza la pantalla no pueden estar a un factor de dos.
//
// Las amortiguaciones siguen sin tocarse desde la fase 1. En el idioma de Apple son
// rebotes de 0,10 / 0,15 / 0,20 / 0,25, y los tres presets de iOS van de 0 a 0,3: la tabla
// está entera dentro de ese rango y por encima de 0,3 el movimiento se lee como un dibujo
// animado.
inline constexpr Spring kSnappy{0.9f, 130.0f};
inline constexpr Spring kStandard{0.85f, 200.0f};
inline constexpr Spring kSmooth{0.8f, 250.0f};
inline constexpr Spring kExpressive{0.75f, 340.0f};

// Cuánto rebota cada muelle en el idioma de Apple, para poder compararlo con lo publicado
// sin hacer la cuenta a mano. Solo lo usan las pruebas y quien lea la tabla.
constexpr float BounceOf(Spring spring) { return 1.0f - spring.dampingRatio; }

// El fundido que sustituye al movimiento cuando "Mostrar animaciones en Windows" está
// apagado. Corto a propósito: quien apaga las animaciones no quiere media transición,
// quiere que el cambio ya esté hecho.
inline constexpr float kReducedFadeMs = 120.0f;

// El cruce de color de un ESTADO: hover, pulsado, el anillo de foco, la píldora que cambia
// de prioridad, el botón de ventana que se ilumina. No acompaña a ningún muelle, así que no
// sale de SettleMs — y salía. Con la tabla de la fase 6 eso dejaba el fundido del hover en
// 34 ms, DOS fotogramas a 60 Hz: un corte de color, que es exactamente lo que se siente
// como tosco. Y alargarlo no cuesta nada de lo que a esta aplicación le importa: un fundido
// de color no retrasa ni un clic, porque nadie espera a que termine para poder pulsar.
inline constexpr float kInkMs = 120.0f;

// El multiplicador del modo lento de depuración (F10 en Debug). Cinco: con ×2 un salto de
// un fotograma sigue siendo un fotograma.
inline constexpr float kSlowMotion = 5.0f;

// El temblor del límite de Enfoque: cuánto dura y cuánto se aparta. Corto y poco, las dos
// cosas: un rechazo de medio segundo se lee como que la aplicación se ha quedado pensando, y
// uno de veinte píxeles, como que la tarjeta se ha escapado.
inline constexpr float kShakeMs = 320.0f;
inline constexpr float kShakeDip = 7.0f;

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

// Cuánto tarda de verdad en quedarse quieto, dentro del 2 %: 4/(ζ·ωn) con ωn = 2π/T.
//
// NO es el mando de afinar —eso es periodMs, ver arriba—. Sirve para lo que de verdad
// necesita saber cuándo ha acabado del todo: el temporizador que esconde el inspector y el
// que tira la tarjeta de la revisión, que si disparan antes dejan el elemento a medio
// viaje. Y para que una prueba avise si alguien sube un periodo a ojo y se le va a dos
// segundos sin darse cuenta.
//
// **Y NO PREDICE LO QUE SE VE. Esto está medido y hay que saberlo antes de tocar la
// tabla.** Cronometrando el morfeo del inspector contra la pantalla —fotogramas cada 110 ms
// y contando píxeles que cambian— con el muelle estándar sale esto:
//
//     periodo 200 ms  (SettleMs dice 150)   ->  2115 ms de cambio en pantalla
//     periodo 100 ms  (SettleMs dice  75)   ->  1313 ms
//
// Tres repeticiones, ±10 ms. El periodo manda —el doble de periodo es 1,6 veces el tiempo—
// pero lo que se ve dura un orden de magnitud más que este número, porque un muelle se
// acerca a su destino asintóticamente y el último medio píxel tarda. Parte de esos
// milisegundos no los ve un ojo; cuáles, no lo sabe decir un contador de píxeles.
//
// Consecuencia práctica: **afinar la tabla mirando aquí lleva a equivocarse**, que es lo
// que pasó en las fases 1 y 6. Se afina con el periodo y se juzga con la aplicación
// delante.
float SettleMs(Spring spring);

}  // namespace Motion
