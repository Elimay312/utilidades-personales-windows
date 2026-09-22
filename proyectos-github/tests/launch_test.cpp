// La frontera de confianza del arranque: una brujula:// la puede disparar cualquier página
// web que alguien abra sin mirar, así que lo que sale de aquí tiene que ser un nombre de
// repositorio o nada.

#include <doctest/doctest.h>

#include <vector>

#include "app/Launch.h"

namespace {

App::Launch Parse(std::vector<const wchar_t*> args) {
    // argv[0] es el ejecutable, como en wWinMain.
    args.insert(args.begin(), L"brujula.exe");
    return App::ParseArguments(static_cast<int>(args.size()), args.data());
}

}  // namespace

TEST_CASE("--repo NOMBRE") {
    CHECK(Parse({L"--repo", L"brujula"}).repo == L"brujula");
    CHECK(Parse({L"--repo", L"eli/brujula"}).repo == L"eli/brujula");
    // Sin valor detrás no es nada, y sobre todo no se lee fuera del array.
    CHECK(Parse({L"--repo"}).repo.empty());
    CHECK(Parse({}).repo.empty());
}

TEST_CASE("el esquema de URL llega como un argumento suelto") {
    CHECK(Parse({L"brujula://repo/brujula"}).repo == L"brujula");
    CHECK(Parse({L"brujula://repo/eli/brujula"}).repo == L"eli/brujula");
    // El shell suele añadir la barra final.
    CHECK(Parse({L"brujula://repo/brujula/"}).repo == L"brujula");
}

TEST_CASE("el esquema no distingue mayúsculas y los porcentajes se deshacen") {
    // Windows no promete cómo escribe el esquema al lanzarlo.
    CHECK(App::RepoFromUrl(L"BRUJULA://REPO/brujula") == L"brujula");
    CHECK(App::RepoFromUrl(L"brujula://repo/eli%2Fbrujula") == L"eli/brujula");
    CHECK(App::RepoFromUrl(L"brujula://repo/uno-dos_tres.md") == L"uno-dos_tres.md");
}

TEST_CASE("lo que no es nuestro no se toca") {
    CHECK(App::RepoFromUrl(L"https://github.com/eli/brujula").empty());
    CHECK(App::RepoFromUrl(L"brujula://otracosa/brujula").empty());
    CHECK(App::RepoFromUrl(L"brujula://repo/").empty());
    CHECK(App::RepoFromUrl(L"").empty());
}

TEST_CASE("lo que llega de fuera no pasa si no tiene forma de nombre") {
    // Esto es lo que justifica el archivo. Ninguna de estas cadenas llega hoy a una ruta ni
    // a una petición, pero la única manera de que eso siga siendo verdad dentro de tres
    // fases es que no salgan de aquí.
    CHECK_FALSE(App::LooksLikeRepoName(L"../../Windows/System32"));
    CHECK_FALSE(App::LooksLikeRepoName(L".."));
    CHECK_FALSE(App::LooksLikeRepoName(L"C:\\Windows"));
    CHECK_FALSE(App::LooksLikeRepoName(L"uno dos"));
    CHECK_FALSE(App::LooksLikeRepoName(L"uno\nDROP TABLE repos"));
    CHECK_FALSE(App::LooksLikeRepoName(L"repo?x=1"));
    CHECK_FALSE(App::LooksLikeRepoName(L"/brujula"));
    CHECK_FALSE(App::LooksLikeRepoName(L"brujula/"));
    CHECK_FALSE(App::LooksLikeRepoName(L"a//b"));
    CHECK_FALSE(App::LooksLikeRepoName(L"uno/dos/tres"));
    CHECK_FALSE(App::LooksLikeRepoName(L"brújula"));  // no ASCII
    CHECK_FALSE(App::LooksLikeRepoName(std::wstring(500, L'a')));
    CHECK_FALSE(App::LooksLikeRepoName(L""));

    CHECK(App::LooksLikeRepoName(L"brujula"));
    CHECK(App::LooksLikeRepoName(L"eli/brujula"));
    CHECK(App::LooksLikeRepoName(L"dock-mac-en-windows"));
}

TEST_CASE("una URL con un nombre imposible no arranca nada") {
    // El camino entero, que es lo que de verdad importa: que el filtro esté PUESTO y no
    // solo escrito.
    CHECK(App::RepoFromUrl(L"brujula://repo/..%2F..%2FWindows").empty());
    CHECK(App::RepoFromUrl(L"brujula://repo/uno%20dos").empty());
    CHECK(Parse({L"--repo", L"../../x"}).repo.empty());
}
