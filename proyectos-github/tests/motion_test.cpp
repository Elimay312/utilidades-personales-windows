// Los cuatro muelles y el modo sin animaciones.
//
// El caso que justifica el archivo es el último: cuando Windows tiene desactivado
// "Mostrar animaciones", Brújula no puede limitarse a acortar las animaciones, tiene que
// dejar de mover cosas. Eso se decide aquí, en una función pura, y no repartido por cada
// sitio que anima algo, que es como se acaba olvidando en uno.

#include <doctest/doctest.h>

#include <initializer_list>

#include "compositor/MotionSpec.h"

using Motion::Kind;

TEST_CASE("la tabla de muelles es la de CLAUDE.md") {
    CHECK(Motion::SpringFor(Kind::Snappy) == Motion::Spring{0.9f, 130.0f});
    CHECK(Motion::SpringFor(Kind::Standard) == Motion::Spring{0.85f, 200.0f});
    CHECK(Motion::SpringFor(Kind::Smooth) == Motion::Spring{0.8f, 250.0f});
    CHECK(Motion::SpringFor(Kind::Expressive) == Motion::Spring{0.75f, 340.0f});
}

TEST_CASE("los muelles van de más seco a más suelto, en ese orden") {
    // Es lo que hace que la escala signifique algo: un hover no puede tardar más que una
    // hoja modal, y una hoja modal no puede rebotar menos que un hover.
    CHECK(Motion::kSnappy.dampingRatio > Motion::kStandard.dampingRatio);
    CHECK(Motion::kStandard.dampingRatio > Motion::kSmooth.dampingRatio);
    CHECK(Motion::kSmooth.dampingRatio > Motion::kExpressive.dampingRatio);

    CHECK(Motion::kSnappy.periodMs < Motion::kStandard.periodMs);
    CHECK(Motion::kStandard.periodMs < Motion::kSmooth.periodMs);
    CHECK(Motion::kSmooth.periodMs < Motion::kExpressive.periodMs);
}

TEST_CASE("el periodo ES la duración perceptual, y cae donde lo pone lo publicado") {
    // Este es el mando con el que se afina, y la prueba que lo dice. Las fases 1 y 6 lo
    // afinaron mirando SettleMs y se salieron por abajo: 80-195 ms de duración perceptual,
    // cuando los tres presets de iOS están los tres en 500.
    //
    // Las bandas son las publicadas: micro-interacción alrededor de 150 ms, cambio de
    // maquetación entre 200 y 350. El techo de 350 se queda: por encima empieza a estorbar
    // para ir de un proyecto a otro, que es lo que se midió usando la aplicación.
    CHECK(Motion::kSnappy.periodMs >= 100.0f);
    CHECK(Motion::kSnappy.periodMs <= 200.0f);
    for (const Kind kind : {Kind::Standard, Kind::Smooth, Kind::Expressive}) {
        CHECK(Motion::SpringFor(kind).periodMs >= 200.0f);
        CHECK(Motion::SpringFor(kind).periodMs <= 350.0f);
    }
}

TEST_CASE("los rebotes están dentro de lo que no parece un dibujo animado") {
    // En el idioma de Apple, bounce = 1 - amortiguación. Sus tres presets van de 0 a 0,3 y
    // por encima de 0,3 el movimiento se lee como decoración. Los cuatro de aquí caen
    // dentro, y esa es toda la comprobación: son el carácter de la tabla y no se tocan.
    for (const Kind kind : {Kind::Snappy, Kind::Standard, Kind::Smooth, Kind::Expressive}) {
        const float bounce = Motion::BounceOf(Motion::SpringFor(kind));
        CHECK(bounce > 0.0f);
        CHECK(bounce <= 0.3f);
    }
}

TEST_CASE("el asentado es una consecuencia, no un mando, y ninguno se va de tiempo") {
    // 4/(ζ · 2π/T). Está aquí para que si alguien sube un periodo a ojo y lo pone en dos
    // segundos se entere en esta línea y no abriendo la aplicación.
    CHECK(Motion::SettleMs(Motion::kSnappy) == doctest::Approx(91.96f).epsilon(0.01));
    CHECK(Motion::SettleMs(Motion::kStandard) == doctest::Approx(149.79f).epsilon(0.01));
    CHECK(Motion::SettleMs(Motion::kSmooth) == doctest::Approx(198.94f).epsilon(0.01));
    CHECK(Motion::SettleMs(Motion::kExpressive) == doctest::Approx(288.60f).epsilon(0.01));

    for (const Kind kind : {Kind::Snappy, Kind::Standard, Kind::Smooth, Kind::Expressive}) {
        CHECK(Motion::SettleMs(Motion::SpringFor(kind)) < 300.0f);
    }
}

TEST_CASE("el cruce de tinta no sale de ningún muelle") {
    // Hover, pulsado, foco y píldora. Salía de FadeMs(Snappy) y con la tabla de la fase 6
    // eso eran 34 ms: dos fotogramas a 60 Hz, o sea un corte. Tiene que ser bastante más
    // largo que eso y bastante más corto que un cambio de tema, que sí es una pantalla
    // entera cruzándose.
    CHECK(Motion::kInkMs >= 100.0f);
    CHECK(Motion::kInkMs < Motion::kThemeCrossfadeMs);
    // Y NO es el fundido del muelle rígido: si algún día coincidieran por accidente, esta
    // línea avisa de que se han vuelto a atar.
    CHECK(Motion::kInkMs != doctest::Approx(Motion::Resolve(Kind::Snappy, true).fadeMs));
}

TEST_CASE("con animaciones, el fundido termina antes que el muelle") {
    // Para que el contenido ya se lea mientras la forma todavía se está asentando.
    for (const Kind kind : {Kind::Snappy, Kind::Standard, Kind::Smooth, Kind::Expressive}) {
        const Motion::Resolved resuelto = Motion::Resolve(kind, true);
        CHECK(resuelto.animate);
        CHECK(resuelto.spring == Motion::SpringFor(kind));
        CHECK(resuelto.fadeMs < Motion::SettleMs(resuelto.spring));
        CHECK(resuelto.fadeMs > 0.0f);
    }
}

TEST_CASE("sin animaciones del sistema no se mueve nada, solo se funde") {
    for (const Kind kind : {Kind::Snappy, Kind::Standard, Kind::Smooth, Kind::Expressive}) {
        const Motion::Resolved resuelto = Motion::Resolve(kind, false);
        CHECK_FALSE(resuelto.animate);
        CHECK(resuelto.fadeMs == Motion::kReducedFadeMs);
    }
}

TEST_CASE("un muelle sin sentido no divide por cero") {
    CHECK(Motion::SettleMs(Motion::Spring{0.0f, 200.0f}) == 0.0f);
    CHECK(Motion::SettleMs(Motion::Spring{0.9f, 0.0f}) == 0.0f);
}
