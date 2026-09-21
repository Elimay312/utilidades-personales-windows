// Las fechas. Todo es UTC y son segundos desde 1970, y la prueba que de verdad importa es
// la del epoch: si alguna conversión se fuera al horario local, la aplicación seguiría
// funcionando y solo se equivocaría cerca de medianoche y en los bordes de 14 y 90 días.
// Eso no se reproduce a mano, se sufre.

#include <doctest/doctest.h>

#include "model/Time.h"

using Model::DaysBetween;
using Model::FormatDay;
using Model::FormatIso8601;
using Model::FromEpoch;
using Model::ParseIso8601;
using Model::ToEpoch;

TEST_CASE("la fecha que manda GitHub se lee y se vuelve a escribir igual") {
    const auto when = ParseIso8601("2026-09-17T22:15:45Z");
    REQUIRE(when.has_value());
    CHECK(FormatIso8601(*when) == "2026-09-17T22:15:45Z");
    CHECK(FormatDay(*when) == "2026-09-17");
}

TEST_CASE("el instante es UTC y no la hora local del equipo") {
    // Este es el caso que vigila toda la decisión. En un equipo en Madrid, una conversión
    // por horario local daría dos horas menos y nadie lo notaría hasta que un repositorio
    // saliera en el grupo equivocado un día sí y otro no.
    const auto when = ParseIso8601("2026-09-17T22:15:45Z");
    REQUIRE(when.has_value());
    CHECK(ToEpoch(*when) == 1789683345);

    const auto epoch = ParseIso8601("1970-01-01T00:00:00Z");
    REQUIRE(epoch.has_value());
    CHECK(ToEpoch(*epoch) == 0);
}

TEST_CASE("una fecha sin hora es medianoche") {
    // Es como PROYECTO.md escribe las suyas, y como se guardan las novedades.
    const auto when = ParseIso8601("2026-09-17");
    REQUIRE(when.has_value());
    CHECK(ToEpoch(*when) == 1789603200);
}

TEST_CASE("el epoch va y vuelve") {
    const auto when = FromEpoch(1789683345);
    CHECK(ToEpoch(when) == 1789683345);
    CHECK(FormatIso8601(when) == "2026-09-17T22:15:45Z");
}

TEST_CASE("un 29 de febrero que no existe se rechaza") {
    // Comprobar 1..12 y 1..31 a mano dejaría pasar los dos primeros. Es un dato que llega
    // de fuera —del frontmatter de un PROYECTO.md escrito a mano— y tiene que poder ser
    // inválido sin que nadie lo convierta en una fecha plausible.
    CHECK_FALSE(ParseIso8601("2027-02-29").has_value());
    CHECK_FALSE(ParseIso8601("2026-02-30").has_value());
    CHECK_FALSE(ParseIso8601("2026-13-01").has_value());
    CHECK_FALSE(ParseIso8601("2026-00-10").has_value());

    // Y el bisiesto de verdad sí entra.
    const auto leap = ParseIso8601("2024-02-29T12:00:00Z");
    REQUIRE(leap.has_value());
    CHECK(ToEpoch(*leap) == 1709208000);
}

TEST_CASE("lo que no tiene la forma exacta se rechaza") {
    CHECK_FALSE(ParseIso8601("").has_value());
    CHECK_FALSE(ParseIso8601("ayer").has_value());
    // Sin la Z no sabríamos en qué huso está, y adivinarlo es justo lo que no se hace.
    CHECK_FALSE(ParseIso8601("2026-09-17T22:15:45").has_value());
    CHECK_FALSE(ParseIso8601("2026-09-17 22:15:45Z").has_value());
    CHECK_FALSE(ParseIso8601("2026-09-17T25:00:00Z").has_value());
    CHECK_FALSE(ParseIso8601("2026-09-17T22:60:00Z").has_value());
    // Un atoi aceptaría los dos siguientes. Aquí los dígitos son dígitos y nada más.
    CHECK_FALSE(ParseIso8601("+026-09-17").has_value());
    CHECK_FALSE(ParseIso8601("2026-9-17").has_value());
}

TEST_CASE("los días entre dos instantes se cuentan completos") {
    const auto a = ParseIso8601("2026-09-01T00:00:00Z");
    const auto b = ParseIso8601("2026-09-15T00:00:00Z");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(DaysBetween(*a, *b) == 14);

    // Un segundo menos de catorce días son trece días completos. Ese redondeo es
    // exactamente el que decide si un repositorio sale "activo" o "en pausa".
    const auto casi = ParseIso8601("2026-09-14T23:59:59Z");
    REQUIRE(casi.has_value());
    CHECK(DaysBetween(*a, *casi) == 13);
}

TEST_CASE("un instante en el futuro da días negativos") {
    // Pasa cuando el reloj del equipo va atrasado respecto al servidor. Tiene que salir
    // negativo y no un número enorme: un push "del futuro" es recentísimo, no dormido.
    const auto ahora = ParseIso8601("2026-09-01T00:00:00Z");
    const auto futuro = ParseIso8601("2026-09-05T00:00:00Z");
    REQUIRE(ahora.has_value());
    REQUIRE(futuro.has_value());
    CHECK(DaysBetween(*futuro, *ahora) == -4);

    // Y medio día hacia atrás baja a -1, no a 0: floor, no truncar hacia cero.
    const auto medio = ParseIso8601("2026-09-01T12:00:00Z");
    REQUIRE(medio.has_value());
    CHECK(DaysBetween(*medio, *ahora) == -1);
}
