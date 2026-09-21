// Humo de las dos dependencias que todavía no se usan: nlohmann y SQLite.
//
// No prueban lógica nuestra, prueban la cadena: que FetchContent las trae, que compilan
// con el CRT estático y que el texto en UTF-8 entra y sale igual. El riesgo real es ese,
// Unicode en el borde, y descubrirlo en la fase 3 con la sincronización a medias cuesta
// mucho más que descubrirlo ahora, que no hay nada montado encima.

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <string>

namespace {
// Lo que de verdad va a pasar por aquí: nombres de repositorio y notas escritas a mano.
// Sin prefijo u8: en C++20 ese literal seria const char8_t[] y no encaja con las APIs
// de SQLite ni de nlohmann. Con /utf-8 el compilador ya codifica los literales estrechos
// en UTF-8, que es justo lo que hace falta y de paso comprueba que la bandera esta puesta.
constexpr const char* kTexto = "Revisión año niño — 音楽";
}  // namespace

TEST_CASE("nlohmann conserva el texto y los tipos") {
    nlohmann::json origen;
    origen["nombre"] = kTexto;
    origen["privado"] = true;
    origen["issues"] = 7;
    origen["novedades"] = nlohmann::json::array({"uno", "dos"});

    const nlohmann::json vuelta = nlohmann::json::parse(origen.dump());

    CHECK(vuelta["nombre"].get<std::string>() == kTexto);
    CHECK(vuelta["privado"].get<bool>() == true);
    CHECK(vuelta["issues"].get<int>() == 7);
    CHECK(vuelta["novedades"].size() == 2);
}

TEST_CASE("SQLite guarda y devuelve el mismo UTF-8") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    REQUIRE(sqlite3_exec(db, "CREATE TABLE repos (nombre TEXT NOT NULL)", nullptr, nullptr,
                         nullptr) == SQLITE_OK);

    sqlite3_stmt* insertar = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO repos (nombre) VALUES (?)", -1, &insertar,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(insertar, 1, kTexto, -1, SQLITE_TRANSIENT) == SQLITE_OK);
    REQUIRE(sqlite3_step(insertar) == SQLITE_DONE);
    sqlite3_finalize(insertar);

    sqlite3_stmt* leer = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT nombre FROM repos", -1, &leer, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(leer) == SQLITE_ROW);
    const std::string vuelta = reinterpret_cast<const char*>(sqlite3_column_text(leer, 0));
    sqlite3_finalize(leer);

    CHECK(vuelta == kTexto);
    sqlite3_close(db);
}

TEST_CASE("SQLite viene con las opciones que pedimos") {
    // Que las comillas dobles NO sean un nombre de columna cuando no existe: con DQS a 0,
    // un error de tipeo en una consulta falla en vez de convertirse en una cadena
    // silenciosa. Es la clase de fallo que en la fase 3 devolvería listas vacías sin
    // decir por qué.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "CREATE TABLE t (a TEXT)", nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "SELECT \"no_existe\" FROM t", nullptr, nullptr, nullptr) != SQLITE_OK);
    sqlite3_close(db);

    CHECK(sqlite3_threadsafe() != 0);
    CHECK(std::string(sqlite3_libversion()).rfind("3.50.4", 0) == 0);
}
