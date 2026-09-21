#include "github/Query.h"

#include <nlohmann/json.hpp>

#include "model/Base64.h"
#include "model/Utf.h"

namespace Github {
namespace {

// Los campos que pide CLAUDE.md, en un fragmento para que la consulta personal y la de
// organización pidan exactamente lo mismo. Duplicarlos sería la manera de que un día la
// lista de una organización saliera sin lenguaje y nadie supiera por qué.
constexpr const char* kMetaFragment =
    "fragment meta on Repository {\n"
    "  id\n"
    "  name\n"
    "  nameWithOwner\n"
    "  owner { login }\n"
    "  description\n"
    "  url\n"
    "  isPrivate\n"
    "  isArchived\n"
    "  isFork\n"
    "  stargazerCount\n"
    "  diskUsage\n"
    "  primaryLanguage { name color }\n"
    "  pushedAt\n"
    "  updatedAt\n"
    "  createdAt\n"
    "}\n";

constexpr const char* kRateFragment = "  rateLimit { limit cost remaining resetAt }\n";

// El detalle. 'proyecto' y 'raiz' son alias de objetos del árbol y se piden aparte porque
// son lo único de aquí que necesita permiso de Contents.
//
// Los cinco commits se piden en el PASE 2 y no cuando se abre el inspector, y es la regla 1
// de arquitectura: la interfaz no espera a la red. El primero de 'history' es el mismo que
// los tres campos sueltos de arriba; esos se quedan porque son las columnas con las que el
// pase 2 decide qué repetir, y quitarlos obligaría a leer una lista para saber una fecha.
constexpr const char* kDetailFields =
    "      id\n"
    "      pushedAt\n"
    "      defaultBranchRef {\n"
    "        name\n"
    "        target {\n"
    "          ... on Commit {\n"
    "            oid committedDate messageHeadline\n"
    "            history(first: 5) {\n"
    "              nodes { oid committedDate messageHeadline author { name user { login } } }\n"
    "            }\n"
    "          }\n"
    "        }\n"
    "      }\n"
    "      issues(states: OPEN) { totalCount }\n"
    "      pullRequests(states: OPEN) { totalCount }\n";

std::string Compose(const char* query, nlohmann::json variables) {
    nlohmann::json body;
    body["query"] = query;
    body["variables"] = std::move(variables);
    return body.dump();
}

}  // namespace

const char* MetadataQuery() {
    static const std::string query =
        std::string(
            "query($cursor: String) {\n") +
        kRateFragment +
        "  viewer {\n"
        "    repositories(first: 100, after: $cursor, ownerAffiliations: [OWNER],\n"
        "                 orderBy: {field: PUSHED_AT, direction: DESC}) {\n"
        "      totalCount\n"
        "      pageInfo { hasNextPage endCursor }\n"
        "      nodes { ...meta }\n"
        "    }\n"
        "  }\n"
        "}\n" +
        kMetaFragment;
    return query.c_str();
}

const char* OrgMetadataQuery() {
    // Por organización y no filtrando en el cliente: "qué organizaciones incluir es
    // configurable" (CLAUDE.md), y filtrar después sería descargar repositorios privados de
    // trabajo que el usuario pidió no ver.
    static const std::string query =
        std::string(
            "query($org: String!, $cursor: String) {\n") +
        kRateFragment +
        "  organization(login: $org) {\n"
        "    repositories(first: 100, after: $cursor,\n"
        "                 orderBy: {field: PUSHED_AT, direction: DESC}) {\n"
        "      totalCount\n"
        "      pageInfo { hasNextPage endCursor }\n"
        "      nodes { ...meta }\n"
        "    }\n"
        "  }\n"
        "}\n" +
        kMetaFragment;
    return query.c_str();
}

const char* DetailQuery() {
    static const std::string query =
        std::string(
            "query($ids: [ID!]!) {\n") +
        kRateFragment +
        "  nodes(ids: $ids) {\n"
        "    ... on Repository {\n" +
        kDetailFields +
        "      proyecto: object(expression: \"HEAD:PROYECTO.md\") {\n"
        "        ... on Blob { oid byteSize isTruncated text }\n"
        "      }\n"
        // La raíz entera, solo nombres y tipos. Es de donde salen los OTROS .md que el
        // inspector ofrece copiar a las novedades. 'entries' no es una conexión —no lleva
        // first: ni cursor— así que no suma al coste por nodos de la consulta.
        "      raiz: object(expression: \"HEAD:\") {\n"
        "        ... on Tree { entries { name type } }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";
    return query.c_str();
}

const char* DetailQueryNoContents() {
    // La misma sin el blob. Se usa cuando la credencial no tiene Contents: read, para que
    // una credencial estrecha sincronice sin PROYECTO.md en vez de no sincronizar.
    static const std::string query =
        std::string(
            "query($ids: [ID!]!) {\n") +
        kRateFragment +
        "  nodes(ids: $ids) {\n"
        "    ... on Repository {\n" +
        kDetailFields +
        "    }\n"
        "  }\n"
        "}\n";
    return query.c_str();
}

const char* ViewerQuery() {
    return "query { viewer { login name } }\n";
}

const char* FileQuery() {
    // Por identificador y no por dueño y nombre: el identificador es lo que la caché ya
    // tiene, y no hay que volver a partir un nombre completo para preguntar por un archivo.
    static const std::string query =
        "query($id: ID!, $expr: String!) {\n"
        "  node(id: $id) {\n"
        "    ... on Repository {\n"
        "      object(expression: $expr) {\n"
        "        ... on Blob { oid byteSize isTruncated text }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";
    return query.c_str();
}

std::string MetadataBody(const std::optional<std::string>& cursor) {
    nlohmann::json variables;
    // null explícito y no la clave ausente: GraphQL trata las dos igual, pero escribirlo
    // deja claro que la primera página es "sin cursor" y no "se me olvidó".
    variables["cursor"] = cursor.has_value() ? nlohmann::json(*cursor) : nlohmann::json(nullptr);
    return Compose(MetadataQuery(), std::move(variables));
}

std::string OrgMetadataBody(const std::string& org, const std::optional<std::string>& cursor) {
    nlohmann::json variables;
    variables["org"] = org;
    variables["cursor"] = cursor.has_value() ? nlohmann::json(*cursor) : nlohmann::json(nullptr);
    return Compose(OrgMetadataQuery(), std::move(variables));
}

std::string DetailBody(const std::vector<std::string>& ids, bool withContents) {
    nlohmann::json variables;
    variables["ids"] = ids;
    return Compose(withContents ? DetailQuery() : DetailQueryNoContents(), std::move(variables));
}

std::string ViewerBody() {
    return Compose(ViewerQuery(), nlohmann::json::object());
}

std::string FileBody(const std::string& repoId, const std::wstring& path) {
    nlohmann::json variables;
    variables["id"] = repoId;
    variables["expr"] = "HEAD:" + Model::ToUtf8(path);
    return Compose(FileQuery(), std::move(variables));
}

// --- La escritura ------------------------------------------------------------------

std::wstring ContentsPath(const std::wstring& nameWithOwner) {
    // Letras, dígitos, punto, guion, guion bajo y UNA barra. Es lo que GitHub admite en un
    // nombre de usuario y en uno de repositorio, así que cualquier otra cosa no es un nombre
    // raro: es algo que no debería estar ahí.
    int slashes = 0;
    for (const wchar_t c : nameWithOwner) {
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
            c == L'.' || c == L'-' || c == L'_') {
            continue;
        }
        if (c == L'/') {
            ++slashes;
            continue;
        }
        return std::wstring();
    }
    if (slashes != 1 || nameWithOwner.size() < 3) return std::wstring();
    if (nameWithOwner.front() == L'/' || nameWithOwner.back() == L'/') return std::wstring();
    // Y ni un punto suelto ni dos seguidos: «..» dentro de una ruta es lo único de esta lista
    // que además significa algo.
    if (nameWithOwner.find(L"..") != std::wstring::npos) return std::wstring();

    return L"/repos/" + nameWithOwner + L"/contents/" + kProyectoFile;
}

std::string ContentsBody(const std::wstring& text, const std::wstring& sha,
                         const std::wstring& branch) {
    nlohmann::json body;
    body["message"] = kCommitMessage;
    // A UTF-8 y de ahí a base64, que es como la API de contenidos recibe el archivo.
    body["content"] = Model::ToBase64(Model::ToUtf8(text));
    // El sha solo si el archivo ya estaba: mandarlo vacío al crear uno nuevo es un 422.
    if (!sha.empty()) body["sha"] = Model::ToUtf8(sha);
    if (!branch.empty()) body["branch"] = Model::ToUtf8(branch);
    return body.dump();
}

std::vector<std::vector<std::string>> Chunk(const std::vector<std::string>& ids,
                                            std::size_t size) {
    std::vector<std::vector<std::string>> out;
    if (size == 0 || ids.empty()) return out;

    out.reserve((ids.size() + size - 1) / size);
    for (std::size_t start = 0; start < ids.size(); start += size) {
        const std::size_t end = (start + size < ids.size()) ? start + size : ids.size();
        out.emplace_back(ids.begin() + static_cast<std::ptrdiff_t>(start),
                         ids.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return out;
}

}  // namespace Github
