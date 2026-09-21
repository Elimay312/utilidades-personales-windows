#include "github/Query.h"

#include <nlohmann/json.hpp>

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

// El detalle. 'proyecto' es un alias del objeto del árbol, y se pide aparte porque es lo
// único de aquí que necesita permiso de Contents.
constexpr const char* kDetailFields =
    "      id\n"
    "      pushedAt\n"
    "      defaultBranchRef {\n"
    "        name\n"
    "        target { ... on Commit { oid committedDate messageHeadline } }\n"
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
