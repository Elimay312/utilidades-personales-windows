// Las consultas y el troceado.
//
// Un campo que se cae de la consulta no da error: da una columna vacía en la fase 4 y una
// tarde buscando por qué. Y un troceado que pierde un identificador deja un repositorio sin
// enriquecer para siempre, porque el segundo pase solo vuelve a pedirlo si cambia su push —
// y si nunca cambia, nunca vuelve.

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "github/Query.h"
#include "model/Base64.h"
#include "model/Utf.h"

#include <string>
#include <vector>

namespace {

bool Has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("la consulta de metadatos pide lo que dice CLAUDE.md") {
    const std::string query = Github::MetadataQuery();

    for (const char* field : {"id", "nameWithOwner", "description", "isPrivate", "isArchived",
                              "primaryLanguage", "pushedAt", "updatedAt", "createdAt",
                              "stargazerCount", "diskUsage"}) {
        CHECK(Has(query, field));
    }
    // 100 por página, y ordenado por push para que lo reciente llegue en la primera.
    CHECK(Has(query, "first: 100"));
    CHECK(Has(query, "PUSHED_AT"));
    CHECK(Has(query, "ownerAffiliations: [OWNER]"));
    // El cursor y el límite, que son lo que hace que la paginación y la cuota funcionen.
    CHECK(Has(query, "after: $cursor"));
    CHECK(Has(query, "hasNextPage"));
    CHECK(Has(query, "rateLimit"));
}

TEST_CASE("la consulta de detalle pide lo caro, y por identificadores") {
    const std::string query = Github::DetailQuery();

    // nodes(ids:) y no un cursor: es lo único que permite pedir en paralelo, que es de donde
    // salen los 2,2 segundos del segundo pase en vez de once.
    CHECK(Has(query, "nodes(ids: $ids)"));
    CHECK(Has(query, "defaultBranchRef"));
    CHECK(Has(query, "messageHeadline"));
    CHECK(Has(query, "issues(states: OPEN)"));
    CHECK(Has(query, "pullRequests(states: OPEN)"));
    CHECK(Has(query, "HEAD:PROYECTO.md"));
    // Vuelve a pedir pushedAt: es la fecha que se guarda como enriched_push.
    CHECK(Has(query, "pushedAt"));

    // Fase 5. Los cinco commits y la raiz se piden AQUI y no al abrir el inspector, que es
    // la regla 1 de arquitectura: la interfaz no espera a la red.
    CHECK(Has(query, "history(first: 5)"));
    CHECK(Has(query, "HEAD:"));
    CHECK(Has(query, "entries { name type }"));
}

TEST_CASE("la consulta sin contenidos no pide PROYECTO.md y sí todo lo demás") {
    // Es la que se usa cuando la credencial no tiene Contents: read, para que una credencial
    // estrecha sincronice sin PROYECTO.md en vez de no sincronizar.
    const std::string query = Github::DetailQueryNoContents();
    CHECK_FALSE(Has(query, "PROYECTO.md"));
    CHECK(Has(query, "defaultBranchRef"));
    CHECK(Has(query, "issues(states: OPEN)"));
    // La raiz tambien necesita Contents: read, asi que se cae con PROYECTO.md. Los commits
    // no, y por eso siguen pidiendose: una credencial estrecha se queda sin los .md de la
    // raiz, no sin el historial.
    CHECK_FALSE(Has(query, "entries"));
    CHECK(Has(query, "history(first: 5)"));
}

TEST_CASE("la consulta de organización pide los mismos campos que la personal") {
    // Si las dos listas se separaran, un día los repositorios de una organización saldrían
    // sin lenguaje y no habría por dónde empezar a mirar.
    const std::string personal = Github::MetadataQuery();
    const std::string org = Github::OrgMetadataQuery();

    for (const char* field : {"primaryLanguage", "pushedAt", "stargazerCount", "isArchived"}) {
        CHECK(Has(personal, field));
        CHECK(Has(org, field));
    }
    CHECK(Has(org, "organization(login: $org)"));
}

TEST_CASE("el cuerpo es JSON válido y lleva la consulta y las variables") {
    // Construido con nlohmann y no pegando cadenas: un nombre de organización con una
    // comilla dentro rompería el JSON, y el fallo se vería como "GitHub no contesta".
    const nlohmann::json first =
        nlohmann::json::parse(Github::MetadataBody(std::nullopt), nullptr, false);
    REQUIRE_FALSE(first.is_discarded());
    CHECK(first.contains("query"));
    // La primera página va con cursor nulo explícito.
    CHECK(first["variables"]["cursor"].is_null());

    const nlohmann::json next =
        nlohmann::json::parse(Github::MetadataBody(std::string("CURSOR123")), nullptr, false);
    REQUIRE_FALSE(next.is_discarded());
    CHECK(next["variables"]["cursor"] == "CURSOR123");

    const nlohmann::json org = nlohmann::json::parse(
        Github::OrgMetadataBody("mi\"organizacion", std::nullopt), nullptr, false);
    REQUIRE_FALSE(org.is_discarded());
    CHECK(org["variables"]["org"] == "mi\"organizacion");
}

TEST_CASE("el cuerpo del detalle lleva los identificadores y elige la consulta") {
    const std::vector<std::string> ids = {"R_1", "R_2"};

    const nlohmann::json with = nlohmann::json::parse(Github::DetailBody(ids, true), nullptr, false);
    REQUIRE_FALSE(with.is_discarded());
    CHECK(with["variables"]["ids"].size() == 2);
    CHECK(with["variables"]["ids"][0] == "R_1");
    CHECK(with["query"].get<std::string>().find("PROYECTO.md") != std::string::npos);

    const nlohmann::json without =
        nlohmann::json::parse(Github::DetailBody(ids, false), nullptr, false);
    REQUIRE_FALSE(without.is_discarded());
    CHECK(without["query"].get<std::string>().find("PROYECTO.md") == std::string::npos);
}

TEST_CASE("el troceado no pierde ni repite ningún identificador") {
    std::vector<std::string> ids;
    // 109, que es el número de verdad, y con 20 por trozo no es múltiplo: el último trozo
    // tiene 9. Un troceado escrito con un bucle de más o de menos se nota justo ahí.
    for (int i = 0; i < 109; ++i) ids.push_back("R_" + std::to_string(i));

    const auto chunks = Github::Chunk(ids, Github::kDetailChunk);
    REQUIRE(chunks.size() == 6);
    CHECK(chunks.back().size() == 9);

    std::vector<std::string> flat;
    for (const auto& chunk : chunks) {
        CHECK(chunk.size() <= Github::kDetailChunk);
        for (const auto& id : chunk) flat.push_back(id);
    }
    CHECK(flat == ids);
}

TEST_CASE("trocear casos raros no se cuelga ni inventa trozos") {
    CHECK(Github::Chunk({}, 20).empty());
    // Tamaño cero: sin la guarda, el bucle avanza de cero en cero y no termina nunca.
    CHECK(Github::Chunk({"a", "b"}, 0).empty());

    const auto exact = Github::Chunk({"a", "b", "c", "d"}, 2);
    REQUIRE(exact.size() == 2);
    CHECK(exact[0].size() == 2);
    CHECK(exact[1].size() == 2);

    const auto bigger = Github::Chunk({"a"}, 20);
    REQUIRE(bigger.size() == 1);
    CHECK(bigger[0].size() == 1);
}

// ------------------------------------------------------ La escritura, que es de la fase 5 --

TEST_CASE("el mensaje del commit es el que dice CLAUDE.md, letra por letra") {
    // El criterio de aceptacion de la fase habla de el: «el commit aparece en GitHub con el
    // formato correcto». Si esto cambia, cambia el historial de ciento nueve repositorios.
    CHECK(std::string(Github::kCommitMessage) == "chore: actualizar PROYECTO.md");
}

TEST_CASE("la ruta de contenidos lleva el repositorio y termina en PROYECTO.md") {
    const std::wstring path = Github::ContentsPath(L"Elimay312/brujula");
    CHECK(path == L"/repos/Elimay312/brujula/contents/PROYECTO.md");
}

TEST_CASE("un nombre que no tiene forma de nombre no produce ruta") {
    // La ruta acaba dentro de una URL y el nombre viene de la respuesta de GitHub. Validar
    // antes es mucho mas barato que arrepentirse: con un nombre raro la escritura falla con
    // un aviso en vez de pedir una direccion inventada.
    for (const wchar_t* malo : {L"", L"sinbarra", L"dos/barras/aqui", L"/empieza",
                                L"termina/", L"con espacio/repo", L"repo/../otro",
                                L"con?query/repo", L"acentuó/repo"}) {
        CHECK(Github::ContentsPath(malo).empty());
    }
}

TEST_CASE("el cuerpo del PUT lleva el texto en base64 y el sha cuando lo hay") {
    const std::wstring texto = L"---\nprioridad: enfoque\n---\n\n## Novedades\n";
    const std::string body = Github::ContentsBody(texto, L"abc123", L"main");

    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    CHECK(parsed["message"] == "chore: actualizar PROYECTO.md");
    CHECK(parsed["sha"] == "abc123");
    CHECK(parsed["branch"] == "main");
    // Y el contenido va de ida y vuelta: lo que se codifica es el UTF-8 del archivo.
    CHECK(parsed["content"] == Model::ToBase64(Model::ToUtf8(texto)));
}

TEST_CASE("sin sha no se manda la clave, y sin rama tampoco") {
    // Mandar un sha vacio al crear un archivo nuevo es un 422, y el error que devuelve no
    // menciona la palabra "vacio" por ninguna parte.
    const std::string body = Github::ContentsBody(L"hola", std::wstring(), std::wstring());
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    CHECK(parsed.find("sha") == parsed.end());
    CHECK(parsed.find("branch") == parsed.end());
    CHECK(parsed["content"] == "aG9sYQ==");
}

TEST_CASE("la consulta de un archivo va por identificador y por expresion") {
    const std::string body = Github::FileBody("R_1", L"NOTAS.md");
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    CHECK(parsed["variables"]["id"] == "R_1");
    CHECK(parsed["variables"]["expr"] == "HEAD:NOTAS.md");
    CHECK(std::string(Github::FileQuery()).find("isTruncated") != std::string::npos);
}
