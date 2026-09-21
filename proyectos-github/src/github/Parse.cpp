#include "github/Parse.h"

#include <nlohmann/json.hpp>

#include "model/Utf.h"

namespace Github {
namespace {

using Json = nlohmann::json;

// --- Lectores que no se caen ------------------------------------------------------
//
// Todos pasan por aquí, y todos tratan "no está la clave" y "la clave vale null" igual: no
// hay dato. Es la única forma de que 76 descripciones ausentes no sean 76 casos especiales.

const Json* Field(const Json& object, const char* key) {
    if (!object.is_object()) return nullptr;
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) return nullptr;
    return &*found;
}

std::wstring Wide(const Json& object, const char* key) {
    const Json* value = Field(object, key);
    // is_string() y no dump(): dump() sobre un nulo devuelve las cuatro letras "null", y esa
    // cadena acabaría impresa en la tarjeta como si fuera la descripción del repositorio.
    if (value == nullptr || !value->is_string()) return {};
    return Model::ToWide(value->get<std::string>());
}

std::string Narrow(const Json& object, const char* key) {
    const Json* value = Field(object, key);
    if (value == nullptr || !value->is_string()) return {};
    return value->get<std::string>();
}

bool Flag(const Json& object, const char* key) {
    const Json* value = Field(object, key);
    return value != nullptr && value->is_boolean() && value->get<bool>();
}

long long Number(const Json& object, const char* key) {
    const Json* value = Field(object, key);
    if (value == nullptr || !value->is_number_integer()) return 0;
    return value->get<long long>();
}

// totalCount vive siempre dentro de un objeto: issues { totalCount }.
int CountIn(const Json& object, const char* key) {
    const Json* container = Field(object, key);
    if (container == nullptr) return 0;
    return static_cast<int>(Number(*container, "totalCount"));
}

std::optional<Model::Instant> When(const Json& object, const char* key) {
    const std::string text = Narrow(object, key);
    if (text.empty()) return std::nullopt;
    return Model::ParseIso8601(text);
}

// --- Errores de GraphQL ------------------------------------------------------------

std::wstring MessageOf(const Json& error) {
    const std::wstring message = Wide(error, "message");
    // Nunca se devuelve el cuerpo entero: SEGURIDAD.md regla 3. El 'message' de GraphQL es
    // una frase sobre la consulta —"Field 'x' doesn't exist"—, no contenido del repositorio.
    return message.empty() ? std::wstring(L"GitHub devolvió un error sin explicación") : message;
}

// Un error de permisos sobre el campo del blob. Es lo que contesta una credencial con
// Metadata: read pero sin Contents: read, y no tiene por qué tirar la sincronización.
bool IsProyectoForbidden(const Json& error) {
    const std::string type = Narrow(error, "type");
    if (type != "FORBIDDEN") return false;

    const Json* path = Field(error, "path");
    if (path == nullptr || !path->is_array()) return false;
    for (const Json& step : *path) {
        if (step.is_string() && step.get<std::string>() == "proyecto") return true;
    }
    return false;
}

RateInfo ReadRate(const Json& data) {
    RateInfo rate;
    const Json* node = Field(data, "rateLimit");
    if (node == nullptr) return rate;
    // Solo se pisa el -1 si el campo viene de verdad. Con Number() a secas, un rateLimit al
    // que le falte 'remaining' daría cero — y cero en 'remaining' significa "cuota agotada",
    // así que la sincronización se pararía sola creyendo que no le queda nada. No daría
    // ningún error: diría que hay que esperar a una hora que tampoco vino.
    if (Field(*node, "limit") != nullptr) rate.limit = static_cast<int>(Number(*node, "limit"));
    if (Field(*node, "remaining") != nullptr) {
        rate.remaining = static_cast<int>(Number(*node, "remaining"));
    }
    if (Field(*node, "cost") != nullptr) rate.cost = static_cast<int>(Number(*node, "cost"));
    rate.reset = When(*node, "resetAt");
    return rate;
}

// --- La entrada común --------------------------------------------------------------

struct Body {
    Json root;
    std::vector<std::wstring> warnings;
    bool contentsForbidden = false;

    // Se vuelve a buscar la clave en vez de guardar un puntero a ella. Un puntero dentro de
    // un nlohmann sobrevive a un move —los nodos están en el montón y el move solo cambia de
    // dueño—, pero eso es un detalle de su implementación, y el Body se mueve al salir del
    // Result. Buscar una clave de un objeto cuesta nada y no hay que confiar en nadie.
    const Json* Data() const { return Field(root, "data"); }
};

Model::Result<Body> ReadBody(std::string_view text) {
    Body body;
    // allow_exceptions = false. Un JSON truncado —una conexión que se corta a mitad— tiene
    // que devolver un error, no lanzar desde un hilo de trabajo.
    body.root = Json::parse(text, nullptr, false);
    if (body.root.is_discarded()) {
        return Model::Oops(Model::Fail::Protocol,
                           L"La respuesta de GitHub llegó incompleta o no es JSON");
    }

    const Json* errors = Field(body.root, "errors");
    const Json* data = Field(body.root, "data");

    if (errors != nullptr && errors->is_array() && !errors->empty()) {
        for (const Json& error : *errors) {
            if (IsProyectoForbidden(error)) {
                body.contentsForbidden = true;
                continue;
            }
            body.warnings.push_back(MessageOf(error));
        }

        // Sin 'data' no hay nada que salvar. Con 'data', GitHub ha contestado lo que podía y
        // ha explicado lo que no: tirar la respuesta entera perdería los 108 repositorios
        // que sí venían por culpa del uno que no. Un permiso que falta sobre un campo llega
        // siempre de la segunda manera —el campo viene nulo y el error lo explica—, así que
        // contentsForbidden se detecta ahí y no aquí.
        if (data == nullptr) {
            Model::Error fail;
            fail.kind = Model::Fail::Protocol;
            fail.detail = body.warnings.empty() ? L"GitHub devolvió un error sin explicación"
                                                : body.warnings.front();
            // Los de permisos y autenticación se distinguen: llevan a la hoja de la
            // credencial en vez de a un reintento que no va a arreglar nada.
            for (const Json& error : *errors) {
                const std::string type = Narrow(error, "type");
                if (type == "FORBIDDEN" || type == "UNAUTHORIZED") fail.kind = Model::Fail::Auth;
                if (type == "RATE_LIMITED") fail.kind = Model::Fail::RateLimit;
            }
            return fail;
        }
    }

    if (data == nullptr) {
        return Model::Oops(Model::Fail::Protocol, L"La respuesta de GitHub no traía datos");
    }
    return body;
}

Model::Repo ReadMeta(const Json& node) {
    Model::Repo repo;
    repo.id = Narrow(node, "id");
    repo.name = Wide(node, "name");
    repo.nameWithOwner = Wide(node, "nameWithOwner");
    if (const Json* owner = Field(node, "owner")) repo.owner = Wide(*owner, "login");
    repo.description = Wide(node, "description");
    repo.url = Wide(node, "url");
    repo.isPrivate = Flag(node, "isPrivate");
    repo.isArchived = Flag(node, "isArchived");
    repo.isFork = Flag(node, "isFork");
    if (const Json* language = Field(node, "primaryLanguage")) {
        repo.language = Wide(*language, "name");
        repo.languageColor = Wide(*language, "color");
    }
    repo.stars = static_cast<int>(Number(node, "stargazerCount"));
    repo.diskUsage = Number(node, "diskUsage");
    repo.createdAt = When(node, "createdAt").value_or(Model::Instant{});
    repo.updatedAt = When(node, "updatedAt").value_or(Model::Instant{});
    repo.pushedAt = When(node, "pushedAt");
    return repo;
}

void ReadDetail(const Json& node, Model::Repo& repo) {
    repo.id = Narrow(node, "id");
    repo.enriched = true;
    // El pushedAt de ESTA respuesta, no el del primer pase: es la fecha del dato que de
    // verdad se va a guardar, y con la que la siguiente sincronización decide si repetir.
    repo.enrichedPush = When(node, "pushedAt");
    repo.pushedAt = repo.enrichedPush;

    // Un repositorio sin un solo commit no tiene defaultBranchRef, y son justo los
    // olvidados. Sin este if es una desreferencia nula en el peor sitio posible.
    if (const Json* branch = Field(node, "defaultBranchRef")) {
        repo.defaultBranch = Wide(*branch, "name");
        if (const Json* target = Field(*branch, "target")) {
            repo.commitOid = Wide(*target, "oid");
            repo.commitTitle = Wide(*target, "messageHeadline");
            repo.commitDate = When(*target, "committedDate");
        }
    }

    repo.openIssues = CountIn(node, "issues");
    repo.openPrs = CountIn(node, "pullRequests");

    if (const Json* blob = Field(node, "proyecto")) {
        repo.proyectoOid = Wide(*blob, "oid");
        // isTruncated: GitHub corta los blobs grandes. Guardar medio PROYECTO.md y luego
        // reescribirlo en el modo repo de la fase 5 sería borrarle al usuario la mitad de su
        // archivo, así que un blob cortado se trata como si no hubiera.
        if (!Flag(*blob, "isTruncated")) repo.proyectoText = Wide(*blob, "text");
    }
}

}  // namespace

Model::Result<Page> ParseMetadata(std::string_view json, bool fromOrganization) {
    Model::Result<Body> read = ReadBody(json);
    if (!read) return read.Err();

    Body body = read.Take();
    const Json* data = body.Data();
    if (data == nullptr) {
        return Model::Oops(Model::Fail::Protocol, L"La respuesta de GitHub no traía datos");
    }

    Page page;
    page.warnings = std::move(body.warnings);
    page.rate = ReadRate(*data);

    const Json* owner = Field(*data, fromOrganization ? "organization" : "viewer");
    if (owner == nullptr) {
        // Una organización que no existe, o a la que el usuario ya no pertenece, llega así.
        return Model::Oops(Model::Fail::Protocol,
                           fromOrganization
                               ? L"Esa organización no existe o la cuenta ya no pertenece a ella"
                               : L"GitHub no devolvió la cuenta");
    }

    const Json* list = Field(*owner, "repositories");
    if (list == nullptr) return page;

    page.totalCount = static_cast<int>(Number(*list, "totalCount"));
    if (const Json* info = Field(*list, "pageInfo")) {
        page.hasNextPage = Flag(*info, "hasNextPage");
        page.endCursor = Narrow(*info, "endCursor");
    }

    if (const Json* nodes = Field(*list, "nodes"); nodes != nullptr && nodes->is_array()) {
        page.repos.reserve(nodes->size());
        for (const Json& node : *nodes) {
            // Un nodo nulo dentro de la lista: pasa cuando algo desaparece entre que se pide
            // la página y se sirve.
            if (!node.is_object()) continue;
            Model::Repo repo = ReadMeta(node);
            if (repo.id.empty()) continue;
            page.repos.push_back(std::move(repo));
        }
    }
    return page;
}

Model::Result<Batch> ParseDetail(std::string_view json) {
    Model::Result<Body> read = ReadBody(json);
    if (!read) return read.Err();

    Body body = read.Take();
    const Json* data = body.Data();
    if (data == nullptr) {
        return Model::Oops(Model::Fail::Protocol, L"La respuesta de GitHub no traía datos");
    }

    Batch batch;
    batch.warnings = std::move(body.warnings);
    batch.contentsForbidden = body.contentsForbidden;
    batch.rate = ReadRate(*data);

    const Json* nodes = Field(*data, "nodes");
    if (nodes == nullptr || !nodes->is_array()) return batch;

    batch.repos.reserve(nodes->size());
    for (const Json& node : *nodes) {
        // Nulo: el repositorio se borró entre el primer pase y el segundo. Se salta, y el
        // siguiente primer pase lo marcará como ausente.
        if (!node.is_object()) continue;
        Model::Repo repo;
        ReadDetail(node, repo);
        if (repo.id.empty()) continue;
        batch.repos.push_back(std::move(repo));
    }
    return batch;
}

Model::Result<std::wstring> ParseViewerLogin(std::string_view json) {
    Model::Result<Body> read = ReadBody(json);
    if (!read) return read.Err();

    Body body = read.Take();
    const Json* data = body.Data();
    const Json* viewer = data != nullptr ? Field(*data, "viewer") : nullptr;
    if (viewer == nullptr) {
        return Model::Oops(Model::Fail::Auth, L"La credencial no identifica ninguna cuenta");
    }

    std::wstring login = Wide(*viewer, "login");
    if (login.empty()) {
        return Model::Oops(Model::Fail::Auth, L"La credencial no identifica ninguna cuenta");
    }
    return login;
}

}  // namespace Github
