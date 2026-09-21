// El parser, con respuestas enlatadas y sin una línea de red.
//
// Los casos no son inventados: salen de mirar la cuenta de verdad. 76 de los 109
// repositorios no tienen descripción y 7 no tienen lenguaje, así que el nulo es el camino
// normal y no el raro. Lo que se vigila aquí es que ese camino no imprima la palabra "null"
// en una tarjeta, no desreferencie nada, y no tire los 108 repositorios buenos por culpa de
// uno malo.

#include <doctest/doctest.h>

#include "github/Parse.h"

#include <string>

namespace {

// El JSON de ejemplo se escribe con acentos graves y se convierten aquí en comillas.
//
// No es capricho: una cadena en crudo R"(...)" sería lo natural para pegar JSON, pero el
// limpiador de comentarios de auditar.ps1 no entiende los literales en crudo —reconoce
// cadenas con "(?:\\.|[^"\\])*"— y trocearía esto de forma impredecible. La fase 2 ya dejó
// escrito que el auditor no se toca para acallar un problema del código.
std::string Q(std::string text) {
    for (char& c : text) {
        if (c == '`') c = '"';
    }
    return text;
}

}  // namespace

// ------------------------------------------------------------------- El pase 1 --

TEST_CASE("una página de metadatos se lee entera") {
    const std::string body = Q(
        "{`data`: {"
        "  `rateLimit`: {`limit`: 5000, `cost`: 1, `remaining`: 4996,"
        "                `resetAt`: `2026-09-21T19:40:48Z`},"
        "  `viewer`: {`repositories`: {"
        "    `totalCount`: 109,"
        "    `pageInfo`: {`hasNextPage`: true, `endCursor`: `Y3Vyc29yOnYyOpK0`},"
        "    `nodes`: ["
        "      {`id`: `R_1`, `name`: `brujula`, `nameWithOwner`: `Elimay312/brujula`,"
        "       `owner`: {`login`: `Elimay312`}, `description`: `Priorizador`,"
        "       `url`: `https://github.com/Elimay312/brujula`,"
        "       `isPrivate`: true, `isArchived`: false, `isFork`: false,"
        "       `stargazerCount`: 3, `diskUsage`: 1024,"
        "       `primaryLanguage`: {`name`: `C++`, `color`: `#f34b7d`},"
        "       `pushedAt`: `2026-09-17T22:15:45Z`, `updatedAt`: `2026-09-18T00:00:00Z`,"
        "       `createdAt`: `2025-01-01T00:00:00Z`}"
        "    ]}}}}");

    const auto parsed = Github::ParseMetadata(body, false);
    REQUIRE(parsed.IsOk());

    const Github::Page& page = parsed.Value();
    CHECK(page.totalCount == 109);
    CHECK(page.hasNextPage);
    CHECK(page.endCursor == "Y3Vyc29yOnYyOpK0");
    CHECK(page.rate.limit == 5000);
    CHECK(page.rate.remaining == 4996);
    CHECK(page.rate.cost == 1);
    REQUIRE(page.rate.reset.has_value());

    REQUIRE(page.repos.size() == 1);
    const Model::Repo& repo = page.repos[0];
    CHECK(repo.id == "R_1");
    CHECK(repo.name == L"brujula");
    CHECK(repo.owner == L"Elimay312");
    CHECK(repo.description == L"Priorizador");
    CHECK(repo.isPrivate);
    CHECK_FALSE(repo.isArchived);
    CHECK(repo.stars == 3);
    CHECK(repo.language == L"C++");
    CHECK(repo.languageColor == L"#f34b7d");
    REQUIRE(repo.pushedAt.has_value());
    CHECK(Model::FormatIso8601(*repo.pushedAt) == "2026-09-17T22:15:45Z");
}

TEST_CASE("una descripción nula no se convierte en la cadena null") {
    // La trampa de nlohmann: dump() sobre un nulo devuelve cuatro letras, y esas cuatro
    // letras acabarían impresas en 76 de las 109 tarjetas como si fueran la descripción.
    const std::string body = Q(
        "{`data`: {`viewer`: {`repositories`: {`nodes`: ["
        "  {`id`: `R_1`, `name`: `uno`, `nameWithOwner`: `yo/uno`, `url`: `u`,"
        "   `description`: null, `primaryLanguage`: null, `diskUsage`: null,"
        "   `pushedAt`: null, `createdAt`: `2025-01-01T00:00:00Z`,"
        "   `updatedAt`: `2025-01-01T00:00:00Z`}"
        "]}}}}");

    const auto parsed = Github::ParseMetadata(body, false);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.Value().repos.size() == 1);

    const Model::Repo& repo = parsed.Value().repos[0];
    CHECK(repo.description.empty());
    CHECK(repo.description != L"null");
    CHECK(repo.language.empty());
    CHECK(repo.diskUsage == 0);
    // Sin push: es lo que hace que salga dormido, y tiene que llegar como "no hay" y no
    // como el año 1970.
    CHECK_FALSE(repo.pushedAt.has_value());
}

TEST_CASE("una clave que no viene se trata igual que una que viene nula") {
    const std::string body = Q(
        "{`data`: {`viewer`: {`repositories`: {`nodes`: ["
        "  {`id`: `R_1`, `name`: `uno`, `nameWithOwner`: `yo/uno`, `url`: `u`,"
        "   `createdAt`: `2025-01-01T00:00:00Z`, `updatedAt`: `2025-01-01T00:00:00Z`}"
        "]}}}}");

    const auto parsed = Github::ParseMetadata(body, false);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.Value().repos.size() == 1);
    CHECK(parsed.Value().repos[0].description.empty());
    CHECK_FALSE(parsed.Value().repos[0].isPrivate);
}

TEST_CASE("un nodo nulo dentro de la lista se salta") {
    // Pasa cuando algo desaparece entre que se pide la página y se sirve.
    const std::string body = Q(
        "{`data`: {`viewer`: {`repositories`: {`nodes`: ["
        "  null,"
        "  {`id`: `R_2`, `name`: `dos`, `nameWithOwner`: `yo/dos`, `url`: `u`,"
        "   `createdAt`: `2025-01-01T00:00:00Z`, `updatedAt`: `2025-01-01T00:00:00Z`}"
        "]}}}}");

    const auto parsed = Github::ParseMetadata(body, false);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.Value().repos.size() == 1);
    CHECK(parsed.Value().repos[0].id == "R_2");
}

TEST_CASE("una organización que ya no existe se distingue de una cuenta vacía") {
    const std::string body = Q("{`data`: {`organization`: null}}");
    const auto parsed = Github::ParseMetadata(body, true);
    CHECK_FALSE(parsed.IsOk());
    CHECK_FALSE(parsed.Err().detail.empty());
}

// ------------------------------------------------------------------- El pase 2 --

TEST_CASE("el detalle se lee y el pushedAt guardado es el de esta respuesta") {
    // Y no el del primer pase. Si entre los dos pases entra un push, el detalle que tenemos
    // ya es el del push nuevo; anotar la fecha vieja obligaría a volver a pedirlo la próxima
    // vez sin ninguna necesidad.
    const std::string body = Q(
        "{`data`: {"
        "  `rateLimit`: {`limit`: 5000, `cost`: 1, `remaining`: 4990},"
        "  `nodes`: ["
        "    {`id`: `R_1`, `pushedAt`: `2026-09-21T10:00:00Z`,"
        "     `defaultBranchRef`: {`name`: `main`, `target`: {"
        "        `oid`: `abc123`, `committedDate`: `2026-09-21T09:59:00Z`,"
        "        `messageHeadline`: `Arreglado el borde de los 14 días`}},"
        "     `issues`: {`totalCount`: 4}, `pullRequests`: {`totalCount`: 2},"
        "     `proyecto`: {`oid`: `blob1`, `byteSize`: 120, `isTruncated`: false,"
        "                  `text`: `---\\nprioridad: enfoque\\n---\\n`}}"
        "  ]}}");

    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.Value().repos.size() == 1);

    const Model::Repo& repo = parsed.Value().repos[0];
    CHECK(repo.enriched);
    CHECK(repo.defaultBranch == L"main");
    CHECK(repo.commitOid == L"abc123");
    CHECK(repo.commitTitle == L"Arreglado el borde de los 14 días");
    CHECK(repo.openIssues == 4);
    CHECK(repo.openPrs == 2);
    CHECK(repo.proyectoOid == L"blob1");
    CHECK(repo.proyectoText.find(L"prioridad: enfoque") != std::wstring::npos);

    REQUIRE(repo.enrichedPush.has_value());
    CHECK(Model::FormatIso8601(*repo.enrichedPush) == "2026-09-21T10:00:00Z");
    CHECK(repo.pushedAt == repo.enrichedPush);
    CHECK(parsed.Value().rate.remaining == 4990);
}

TEST_CASE("un repositorio vacío no tiene rama ni commit") {
    // defaultBranchRef nulo. Es una desreferencia nula justo en los repositorios sin un solo
    // commit, que son los olvidados — los que esta aplicación existe para encontrar.
    const std::string body = Q(
        "{`data`: {`nodes`: ["
        "  {`id`: `R_1`, `pushedAt`: null, `defaultBranchRef`: null,"
        "   `issues`: {`totalCount`: 0}, `pullRequests`: {`totalCount`: 0},"
        "   `proyecto`: null}"
        "]}}");

    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.Value().repos.size() == 1);

    const Model::Repo& repo = parsed.Value().repos[0];
    CHECK(repo.enriched);
    CHECK(repo.defaultBranch.empty());
    CHECK(repo.commitOid.empty());
    CHECK_FALSE(repo.commitDate.has_value());
    CHECK(repo.proyectoText.empty());
}

TEST_CASE("una rama sin target tampoco rompe") {
    const std::string body = Q(
        "{`data`: {`nodes`: ["
        "  {`id`: `R_1`, `defaultBranchRef`: {`name`: `main`, `target`: null}}"
        "]}}");

    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.Value().repos.size() == 1);
    CHECK(parsed.Value().repos[0].defaultBranch == L"main");
    CHECK(parsed.Value().repos[0].commitTitle.empty());
}

TEST_CASE("un blob cortado se trata como si no hubiera") {
    // GitHub trunca los blobs grandes. Guardar medio PROYECTO.md y reescribirlo luego en el
    // modo repo de la fase 5 sería borrarle al usuario la mitad de su archivo.
    const std::string body = Q(
        "{`data`: {`nodes`: ["
        "  {`id`: `R_1`, `proyecto`: {`oid`: `b`, `isTruncated`: true, `text`: `medio arch`}}"
        "]}}");

    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().repos[0].proyectoText.empty());
    // El oid sí se guarda: sirve para saber que hay archivo aunque no se haya podido leer.
    CHECK(parsed.Value().repos[0].proyectoOid == L"b");
}

TEST_CASE("una tanda puede traer un nodo nulo") {
    // El repositorio se borró entre el primer pase y el segundo.
    const std::string body = Q("{`data`: {`nodes`: [null, {`id`: `R_2`}]}}");

    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.Value().repos.size() == 1);
    CHECK(parsed.Value().repos[0].id == "R_2");
}

// ------------------------------------------------------------------- Errores --

TEST_CASE("los errores con datos parciales no tiran la sincronización") {
    // GitHub contesta lo que puede y explica lo que no. Tirar la respuesta entera perdería
    // el repositorio bueno por culpa del que no se pudo leer.
    const std::string body = Q(
        "{`data`: {`viewer`: {`repositories`: {`nodes`: ["
        "   {`id`: `R_1`, `name`: `uno`, `nameWithOwner`: `yo/uno`, `url`: `u`,"
        "    `createdAt`: `2025-01-01T00:00:00Z`, `updatedAt`: `2025-01-01T00:00:00Z`}"
        " ]}}},"
        " `errors`: [{`type`: `NOT_FOUND`, `message`: `Could not resolve to a Repository`}]}");

    const auto parsed = Github::ParseMetadata(body, false);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().repos.size() == 1);
    REQUIRE(parsed.Value().warnings.size() == 1);
    CHECK(parsed.Value().warnings[0] == L"Could not resolve to a Repository");
}

TEST_CASE("una respuesta con solo errores es un fallo, y con su tipo") {
    // Un error de validación llega con HTTP 200 y sin clave 'data'. Comprobado contra la
    // API de verdad: {`errors`:[{...`message`...}]} y nada más.
    const std::string validation = Q(
        "{`errors`: [{`message`: `Field noExiste doesn't exist on type User`}]}");
    const auto bad = Github::ParseMetadata(validation, false);
    CHECK_FALSE(bad.IsOk());
    CHECK(bad.Err().kind == Model::Fail::Protocol);

    // Y el de permisos se distingue, porque lleva a la hoja de la credencial y no a un
    // reintento que no va a arreglar nada.
    const std::string forbidden = Q(
        "{`errors`: [{`type`: `FORBIDDEN`, `message`: `Resource not accessible`}]}");
    const auto denied = Github::ParseMetadata(forbidden, false);
    CHECK_FALSE(denied.IsOk());
    CHECK(denied.Err().kind == Model::Fail::Auth);

    const std::string limited = Q("{`errors`: [{`type`: `RATE_LIMITED`, `message`: `slow down`}]}");
    const auto throttled = Github::ParseMetadata(limited, false);
    CHECK_FALSE(throttled.IsOk());
    CHECK(throttled.Err().kind == Model::Fail::RateLimit);
}

TEST_CASE("un JSON truncado devuelve un error y no lanza") {
    // nlohmann lanza por omisión, y una excepción en el hilo de sincronización no es un
    // error que se enseñe: es un std::terminate y la ventana desapareciendo. Por eso todo
    // pasa por parse(..., nullptr, false).
    const auto cut = Github::ParseMetadata("{\"data\": {\"viewer\": {\"reposi", false);
    CHECK_FALSE(cut.IsOk());
    CHECK(cut.Err().kind == Model::Fail::Protocol);

    CHECK_FALSE(Github::ParseMetadata("", false).IsOk());
    CHECK_FALSE(Github::ParseDetail("no soy json").IsOk());
    // Un JSON válido que no es un objeto tampoco puede colarse.
    CHECK_FALSE(Github::ParseDetail("[1,2,3]").IsOk());
}

TEST_CASE("la falta de permiso sobre PROYECTO.md se detecta aparte") {
    // Es lo que contesta una credencial con Metadata: read pero sin Contents: read. No puede
    // tirar la sincronización: los repositorios se sincronizan sin PROYECTO.md y ya está.
    const std::string body = Q(
        "{`data`: {`nodes`: [{`id`: `R_1`, `proyecto`: null}]},"
        " `errors`: [{`type`: `FORBIDDEN`, `path`: [`nodes`, 0, `proyecto`],"
        "             `message`: `Resource not accessible by personal access token`}]}");

    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().contentsForbidden);
    // Y no se cuenta como aviso suelto: ya tiene su propio camino.
    CHECK(parsed.Value().warnings.empty());
    CHECK(parsed.Value().repos.size() == 1);
}

TEST_CASE("extensions.warnings no es un error") {
    // GitHub avisa así de que los identificadores globales viejos están en retirada. Es un
    // aviso sobre el futuro, no un fallo de esta petición.
    const std::string body = Q(
        "{`data`: {`nodes`: [{`id`: `MDEwOlJlcG9zaXRvcnkx`}]},"
        " `extensions`: {`warnings`: [{`type`: `DEPRECATION`, `message`: `The id is deprecated`}]}}");

    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().warnings.empty());
    CHECK(parsed.Value().repos.size() == 1);
}

TEST_CASE("el límite se lee aunque falte alguna parte") {
    const std::string body = Q("{`data`: {`rateLimit`: {`remaining`: 17}, `nodes`: []}}");
    const auto parsed = Github::ParseDetail(body);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().rate.remaining == 17);
    // -1 y no 0: "no venía" y "queda cero" son cosas distintas, y confundirlas pararía la
    // sincronización creyendo que la cuota está agotada.
    CHECK(parsed.Value().rate.limit == -1);
    CHECK_FALSE(parsed.Value().rate.reset.has_value());
}

// ------------------------------------------------------------------- La cuenta --

TEST_CASE("el login de la cuenta se lee al validar la credencial") {
    const auto ok = Github::ParseViewerLogin(Q("{`data`: {`viewer`: {`login`: `Elimay312`}}}"));
    REQUIRE(ok.IsOk());
    CHECK(ok.Value() == L"Elimay312");

    const auto empty = Github::ParseViewerLogin(Q("{`data`: {`viewer`: null}}"));
    CHECK_FALSE(empty.IsOk());
    CHECK(empty.Err().kind == Model::Fail::Auth);
}
