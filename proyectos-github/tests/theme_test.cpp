// La tabla de colores. Comprueba que las dos columnas de CLAUDE.md están donde dicen y
// que el alfa de la tabla sobrevive al paso a byte, que es donde se pierde sin ruido.

#include <doctest/doctest.h>

#include <initializer_list>

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

// --- Tokens de la fase 2 ------------------------------------------------------------

TEST_CASE("el texto sobre el acento se decide por luminancia, no es blanco siempre") {
    // Este es el fallo que se busca: dar por hecho que el acento es azul. El acento lo
    // elige Windows y hay quien lo tiene amarillo; con blanco encima, el botón primario
    // se queda sin texto.
    CHECK(Theme::OnColor(Theme::Rgb(0xffd60a)) == Theme::Rgb(0x1d1d1f));  // amarillo
    CHECK(Theme::OnColor(Theme::Rgb(0x0a84ff)) == Theme::Rgb(0xffffff));  // el azul de Apple
    CHECK(Theme::OnColor(Theme::Rgb(0x000000)) == Theme::Rgb(0xffffff));
    CHECK(Theme::OnColor(Theme::Rgb(0xffffff)) == Theme::Rgb(0x1d1d1f));

    // Y el token lo hereda, en los dos temas: el acento no cambia con el tema.
    const Theme::Tokens amarillo = Theme::TokensFor(Appearance::Dark, Theme::Rgb(0xffd60a));
    CHECK(amarillo.textOnAccent == Theme::Rgb(0x1d1d1f));
}

TEST_CASE("aclarar y oscurecer no se salen de los ocho bits") {
    // El caso que desborda: aclarar un blanco. Sin sujetarlo, 255 + algo da la vuelta a 0
    // y el botón pulsado sale negro.
    CHECK(Theme::Shade(Theme::Rgb(0xffffff), 0.5f) == Theme::Rgb(0xffffff));
    CHECK(Theme::Shade(Theme::Rgb(0x000000), -0.5f) == Theme::Rgb(0x000000));

    // Sin cambio no cambia.
    const Color azul = Theme::Rgb(0x0a84ff);
    CHECK(Theme::Shade(azul, 0.0f) == azul);

    // El alfa no se toca: aclarar un color es aclararlo, no volverlo opaco.
    const Color velo = Theme::Rgba(0x000000, 0.20f);
    CHECK(Theme::Shade(velo, 0.3f).a == velo.a);
}

TEST_CASE("los estados del acento se mueven en la dirección que toca") {
    for (const Color acento : {Theme::Rgb(0x0a84ff), Theme::Rgb(0xffd60a), Theme::Rgb(0x30d158)}) {
        const Theme::Tokens tokens = Theme::TokensFor(Appearance::Dark, acento);
        // Hover aclara, pulsado oscurece. Es la convención de macOS y la de Fluent.
        CHECK(tokens.accentHover.r >= acento.r);
        CHECK(tokens.accentHover.g >= acento.g);
        CHECK(tokens.accentPressed.r <= acento.r);
        CHECK(tokens.accentPressed.g <= acento.g);
        CHECK(tokens.focusRing == acento);
    }
}

TEST_CASE("los velos de control suben de intensidad con el estado") {
    for (const Appearance tema : {Appearance::Light, Appearance::Dark}) {
        const Theme::Tokens t = Theme::TokensFor(tema, Theme::kAccentFallback);
        // Reposo < hover < pulsado. Si se cruzan, pulsar un botón lo aclara y parece que
        // se apaga en vez de hundirse.
        CHECK(t.controlFill.a < t.controlHover.a);
        CHECK(t.controlHover.a < t.controlPressed.a);
        // Y ninguno es opaco: debajo hay Mica y taparla es lo que rompe la estética.
        CHECK(t.controlPressed.a < 255);
    }
}

TEST_CASE("el alfa de los tokens nuevos llega entero al byte") {
    const Theme::Tokens claro = Theme::TokensFor(Appearance::Light, Theme::kAccentFallback);
    const Theme::Tokens oscuro = Theme::TokensFor(Appearance::Dark, Theme::kAccentFallback);

    CHECK(claro.controlFill.a == 13);      // 0,05 * 255 = 12,75 -> 13
    CHECK(claro.controlPressed.a == 31);   // 0,12 * 255 = 30,6  -> 31
    CHECK(oscuro.controlFill.a == 15);     // 0,06 * 255 = 15,3  -> 15
    CHECK(claro.menuSurface.a == 235);     // 0,92 * 255 = 234,6 -> 235
    CHECK(oscuro.scrim.a == 102);          // 0,40 * 255 = 102
    CHECK(oscuro.shadow.a == 128);         // 0,50 * 255 = 127,5 -> 128
}

TEST_CASE("las superficies flotantes tapan más que una tarjeta") {
    for (const Appearance tema : {Appearance::Light, Appearance::Dark}) {
        const Theme::Tokens t = Theme::TokensFor(tema, Theme::kAccentFallback);
        // A propósito: detrás de un menú no hay desenfoque —el acrílico se pinta negro en
        // Win32 sin empaquetar, medido en la fase 1—, así que un menú tan translúcido
        // como una tarjeta no se lee sobre contenido denso.
        CHECK(t.menuSurface.a > t.cardSurface.a);
        CHECK(t.toastSurface.a > t.cardSurface.a);
    }
}

TEST_CASE("el fondo atenuado es más denso en oscuro") {
    const Theme::Tokens claro = Theme::TokensFor(Appearance::Light, Theme::kAccentFallback);
    const Theme::Tokens oscuro = Theme::TokensFor(Appearance::Dark, Theme::kAccentFallback);
    // Sobre un fondo ya oscuro, un velo negro al 20 % no separa la hoja de lo que hay
    // detrás. Lo mismo la sombra.
    CHECK(oscuro.scrim.a > claro.scrim.a);
    CHECK(oscuro.shadow.a > claro.shadow.a);
}
