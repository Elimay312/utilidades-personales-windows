// La tabla de colores. Comprueba que las dos columnas de CLAUDE.md están donde dicen y
// que el alfa de la tabla sobrevive al paso a byte, que es donde se pierde sin ruido.

#include <doctest/doctest.h>

#include "shell/Theme.h"

using Theme::Appearance;
using Theme::Color;

TEST_CASE("los colores de la tabla son los de CLAUDE.md") {
    const Theme::Tokens claro = Theme::TokensFor(Appearance::Light, Theme::kAccentFallback);
    const Theme::Tokens oscuro = Theme::TokensFor(Appearance::Dark, Theme::kAccentFallback);

    CHECK(claro.textPrimary == Theme::Rgb(0x1d1d1f));
    CHECK(claro.textSecondary == Theme::Rgb(0x6e6e73));
    CHECK(oscuro.textPrimary == Theme::Rgb(0xf5f5f7));
    CHECK(oscuro.textSecondary == Theme::Rgb(0xa1a1a6));
}

TEST_CASE("el alfa de la tabla llega entero") {
    const Theme::Tokens claro = Theme::TokensFor(Appearance::Light, Theme::kAccentFallback);

    // 0,72 * 255 = 183,6 -> 184. Truncar daría 183 y la tarjeta saldría medio punto más
    // transparente en todas partes.
    CHECK(claro.cardSurface.a == 184);
    CHECK(claro.separator.a == 20);  // 0,08 * 255 = 20,4
}

TEST_CASE("prioridad y actividad no cambian con el tema") {
    const Theme::Tokens claro = Theme::TokensFor(Appearance::Light, Theme::kAccentFallback);
    const Theme::Tokens oscuro = Theme::TokensFor(Appearance::Dark, Theme::kAccentFallback);

    // Son etiquetas: si el naranja de Enfoque cambiara de tono al cambiar de tema,
    // dejaría de reconocerse de un vistazo, que es lo único que se le pide.
    CHECK(claro.priorityFocus == oscuro.priorityFocus);
    CHECK(claro.prioritySecondary == oscuro.prioritySecondary);
    CHECK(claro.prioritySomeday == oscuro.prioritySomeday);
    CHECK(claro.priorityArchived == oscuro.priorityArchived);
    CHECK(claro.activityActive == oscuro.activityActive);
    CHECK(claro.activityPaused == oscuro.activityPaused);
    CHECK(claro.activityDormant == oscuro.activityDormant);
}

TEST_CASE("el acento del sistema manda, y hay respaldo") {
    const Color verde = Theme::Rgb(0x30d158);
    CHECK(Theme::TokensFor(Appearance::Dark, verde).accent == verde);
    CHECK(Theme::TokensFor(Appearance::Dark, Theme::kAccentFallback).accent == Theme::Rgb(0x0a84ff));
}

TEST_CASE("el tema se deduce del color con el que escribe Windows") {
    // Texto blanco significa fondo oscuro, y al revés. Va por el TEXTO y no por el fondo
    // porque UIColorType::Background devuelve negro siempre en una aplicación de
    // escritorio: con él, el tema claro no existía.
    CHECK(Theme::AppearanceFromForeground(Theme::Rgb(0xffffff)) == Appearance::Dark);
    CHECK(Theme::AppearanceFromForeground(Theme::Rgb(0x000000)) == Appearance::Light);

    // Por luminancia y no por un canal: un azul saturado es oscuro aunque su canal azul
    // esté al máximo.
    CHECK(Theme::AppearanceFromForeground(Theme::Rgb(0x0000ff)) == Appearance::Light);
    CHECK(Theme::AppearanceFromForeground(Theme::Rgb(0xffff00)) == Appearance::Dark);
}
