// El tipo de la credencial y la comprobación de forma.
//
// Aquí NO se toca el Administrador de credenciales. Escribir en él desde una prueba sería
// pisar la credencial de verdad del usuario, que vive en el mismo sitio y con el mismo
// nombre; las pruebas de esta casa no tienen permiso para eso. Lo que sí se comprueba es lo
// que decide en silencio: qué se acepta como credencial y qué garantiza el tipo.

#include <doctest/doctest.h>

#include "github/Auth.h"

#include <string>
#include <type_traits>

using Github::LooksLikeCredential;
using Github::Secret;

TEST_CASE("una credencial no se puede copiar, y eso lo dice el compilador") {
    // "No se copia más de lo necesario" (SEGURIDAD.md) no es una intención: es un error de
    // compilación. Si alguien quita el = delete, esta prueba deja de compilar, que es
    // exactamente el aviso que se quiere.
    static_assert(!std::is_copy_constructible_v<Secret>);
    static_assert(!std::is_copy_assignable_v<Secret>);
    // Moverse sí, porque hay que poder devolverlo desde la caja fuerte y desde GitHub CLI.
    static_assert(std::is_move_constructible_v<Secret>);
    static_assert(std::is_move_assignable_v<Secret>);
    CHECK(true);
}

TEST_CASE("la cabecera montada es la única salida que hay") {
    Secret credential;
    CHECK(credential.Empty());

    credential.Adopt(L"ghx_0123456789abcdefghij");
    CHECK_FALSE(credential.Empty());

    const std::wstring header = credential.AuthorizationHeader();
    CHECK(header.rfind(Secret::kBearerPrefix, 0) == 0);
    CHECK(header.substr(Secret::kBearerPrefix.size()) == L"ghx_0123456789abcdefghij");
}

TEST_CASE("al moverla, el origen se queda sin nada") {
    Secret first;
    first.Adopt(L"ghx_0123456789abcdefghij");

    Secret second = std::move(first);
    CHECK(second.AuthorizationHeader().size() > Secret::kBearerPrefix.size());
    // Y no queda una segunda copia viva por ahí con el mismo valor dentro.
    CHECK(first.Empty());
}

TEST_CASE("vaciarla la deja vacía de verdad") {
    Secret credential;
    credential.Adopt(L"ghx_0123456789abcdefghij");
    credential.Clear();
    CHECK(credential.Empty());
    CHECK(credential.AuthorizationHeader() == std::wstring(Secret::kBearerPrefix));
}

TEST_CASE("lo que tiene forma de credencial se acepta") {
    // Las que emite GitHub: letras, cifras y guiones bajos. La de GitHub CLI mide 40, y una
    // de grano fino es bastante más larga.
    //
    // Los ejemplos NO llevan los prefijos de verdad —ni "ghp_" ni "github_pat_"— y no es
    // remilgo: la regla 1 de auditar.ps1 busca justo eso y marcaría este archivo. Lo
    // comprobó ella sola en cuanto se añadió. Lo que importa aquí es la forma —letras,
    // cifras y guiones bajos, y la longitud—, no el prefijo.
    CHECK(LooksLikeCredential(L"ghx_0123456789abcdefghijklmnopqrstuvwxyz"));
    CHECK(LooksLikeCredential(std::wstring(93, L'a')));
    CHECK(LooksLikeCredential(L"prueba_pat_11ABCDEFG0abcdefghijklmnop"));
}

TEST_CASE("lo que no la tiene se rechaza antes de gastar una petición") {
    // Si esto dejara pasar cualquier cosa, el aviso no sería "eso no es un token" sino un
    // 401 dentro de un rato, que explica muchísimo peor lo que ha pasado.
    CHECK_FALSE(LooksLikeCredential(L""));
    CHECK_FALSE(LooksLikeCredential(L"corto"));

    // Pegar arrastra un salto de línea o un espacio con una facilidad enorme.
    CHECK_FALSE(LooksLikeCredential(L"ghx_0123456789abcdefghij\n"));
    CHECK_FALSE(LooksLikeCredential(L"ghx_0123456789abcdefghij "));
    CHECK_FALSE(LooksLikeCredential(L" ghx_0123456789abcdefghij"));

    // Media línea de otra cosa: una URL, una orden copiada de un README, un comentario.
    CHECK_FALSE(LooksLikeCredential(L"https://github.com/settings/tokens"));
    CHECK_FALSE(LooksLikeCredential(L"gh auth login --scopes repo"));

    // Y algo absurdamente largo tampoco: un archivo entero pegado por error.
    CHECK_FALSE(LooksLikeCredential(std::wstring(4000, L'a')));
}
