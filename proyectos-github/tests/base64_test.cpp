// Base64. Los vectores son los de la RFC 4648, que existen justo porque esto se escribe
// mal de una manera que no se nota: el relleno.
//
// Lo que se vigila aquí no es que la tabla esté bien copiada. Es lo otro: que un byte con el
// bit alto puesto —cualquier tilde en UTF-8— no se ensanche con signo, y que el alfabeto sea
// el estándar y no el de URL. Las dos cosas producen un base64 perfectamente válido que
// guarda otro archivo.

#include <doctest/doctest.h>

#include <string>

#include "model/Base64.h"
#include "model/Utf.h"

using Model::ToBase64;

TEST_CASE("los vectores de la RFC 4648") {
    CHECK(ToBase64("") == "");
    CHECK(ToBase64("f") == "Zg==");
    CHECK(ToBase64("fo") == "Zm8=");
    CHECK(ToBase64("foo") == "Zm9v");
    CHECK(ToBase64("foob") == "Zm9vYg==");
    CHECK(ToBase64("fooba") == "Zm9vYmE=");
    CHECK(ToBase64("foobar") == "Zm9vYmFy");
}

TEST_CASE("un byte con el bit alto no se ensancha con signo") {
    // La eñe en UTF-8 son 0xC3 0xB1. Como char CON signo los dos son negativos, y al
    // desplazarlos hacia la izquierda arrastran unos por delante: sale "/7E=" o cosas
    // parecidas, que son base64 válido de otro contenido.
    CHECK(ToBase64(Model::ToUtf8(L"ñ")) == "w7E=");
    CHECK(ToBase64(Model::ToUtf8(L"año")) == "YcOxbw==");
}

TEST_CASE("el alfabeto es el estándar, no el de URL") {
    // 0xFB 0xFF lleva a los dos caracteres que distinguen los dos alfabetos. Con el de URL
    // serían '-' y '_', y GitHub rechazaría el cuerpo sin explicar por qué.
    const std::string raw = std::string("\xFB\xFF", 2);
    const std::string encoded = ToBase64(raw);
    CHECK(encoded.find('+') != std::string::npos);
    CHECK(encoded.find('/') != std::string::npos);
    CHECK(encoded.find('-') == std::string::npos);
    CHECK(encoded.find('_') == std::string::npos);
}

TEST_CASE("la longitud siempre es múltiplo de cuatro") {
    // Es la comprobación que caza un relleno olvidado sin tener que saber qué tenía que
    // salir: un base64 que no es múltiplo de cuatro no lo acepta ningún descodificador.
    for (std::size_t size = 0; size < 64; ++size) {
        const std::string raw(size, 'x');
        CHECK(ToBase64(raw).size() % 4 == 0);
    }
}

TEST_CASE("un PROYECTO.md de verdad pasa entero") {
    const std::wstring file =
        L"---\nprioridad: enfoque\nsiguiente_paso: Conectar el lector de carpetas\n---\n"
        L"\n## Novedades\n\n- 2026-09-20 — Terminada la fase 2, faltan las rutas largas.\n";
    const std::string utf8 = Model::ToUtf8(file);
    const std::string encoded = ToBase64(utf8);

    CHECK(encoded.size() % 4 == 0);
    // Cuatro caracteres por cada tres bytes, redondeando hacia arriba. Si esto falla es que
    // se perdió o se repitió un trozo por el camino.
    CHECK(encoded.size() == ((utf8.size() + 2) / 3) * 4);
}
