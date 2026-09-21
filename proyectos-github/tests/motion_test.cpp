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
    CHECK(Motion::SpringFor(Kind::Snappy) == Motion::Spring{0.9f, 120.0f});
    CHECK(Motion::SpringFor(Kind::Standard) == Motion::Spring{0.85f, 220.0f});
    CHECK(Motion::SpringFor(Kind::Smooth) == Motion::Spring{0.8f, 300.0f});
    CHECK(Motion::SpringFor(Kind::Expressive) == Motion::Spring{0.75f, 350.0f});
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

TEST_CASE("el periodo no es la duración, y ninguno se va de tiempo") {
    // Period es el periodo NO amortiguado. Con 4/(damping * 2*pi/Period) los cuatro
    // asientan entre 85 y 300 ms, que es lo que se buscaba. La prueba está para que si
    // alguien sube un periodo a ojo y lo pone en 2 segundos, se entere aquí y no al
    // abrir la app.
    CHECK(Motion::SettleMs(Motion::kSnappy) == doctest::Approx(84.9f).epsilon(0.01));
    CHECK(Motion::SettleMs(Motion::kStandard) == doctest::Approx(164.8f).epsilon(0.01));
    CHECK(Motion::SettleMs(Motion::kSmooth) == doctest::Approx(238.7f).epsilon(0.01));
    CHECK(Motion::SettleMs(Motion::kExpressive) == doctest::Approx(297.1f).epsilon(0.01));

    for (const Kind kind : {Kind::Snappy, Kind::Standard, Kind::Smooth, Kind::Expressive}) {
        CHECK(Motion::SettleMs(Motion::SpringFor(kind)) < 350.0f);
    }
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
