#include "store/Backup.h"

#include <nlohmann/json.hpp>

#include <map>
#include <set>
#include <vector>

#include "model/Utf.h"
#include "store/Repos.h"

namespace Store {
namespace {

using Json = nlohmann::json;

// Los mismos lectores que no se caen que estrenó github/Parse.cpp, y por el mismo motivo:
// este archivo lo puede haber tocado alguien con un editor de texto entre la exportación y
// la importación, así que "la clave no está" y "la clave vale null" tienen que dar lo mismo.
const Json* Field(const Json& object, const char* key) {
    if (!object.is_object()) return nullptr;
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) return nullptr;
    return &*found;
}

std::string Narrow(const Json& object, const char* key) {
    const Json* value = Field(object, key);
    if (value == nullptr || !value->is_string()) return {};
    return value->get<std::string>();
}

std::wstring Wide(const Json& object, const char* key) {
    return Model::ToWide(Narrow(object, key));
}

Model::Instant When(const Json& object, const char* key) {
    return Model::ParseIso8601(Narrow(object, key)).value_or(Model::Instant{});
}

Json Entry(const Model::Local& local, const std::wstring& nameWithOwner,
           const std::vector<Model::Novedad>& novedades) {
    Json out;
    out["id"] = local.repoId;
    out["nombre"] = Model::ToUtf8(nameWithOwner);
    out["prioridad"] = Model::SlugOf(local.priority);
    out["estado"] = Model::SlugOf(local.state);
    out["siguiente_paso"] = Model::ToUtf8(local.nextStep);
    out["carpeta"] = Model::ToUtf8(local.folder);
    out["actualizado"] = Model::FormatIso8601(local.updatedAt);
    // Se exportan para que la copia diga la verdad, y NO se importan. Ver la cabecera.
    out["modo_repo"] = local.repoMode;
    out["confirmado"] = local.repoConfirmed;

    Json lista = Json::array();
    for (const Model::Novedad& novedad : novedades) {
        Json one;
        one["fecha"] = novedad.day;
        one["texto"] = Model::ToUtf8(novedad.text);
        one["creado"] = Model::FormatIso8601(novedad.createdAt);
        lista.push_back(std::move(one));
    }
    out["novedades"] = std::move(lista);
    return out;
}

}  // namespace

Model::Result<std::string> ExportJson(Db& db, Model::Instant now) {
    Repos repos(db);

    Model::Result<std::vector<Model::Repo>> all = repos.All();
    if (!all) return all.Err();
    Model::Result<std::vector<Model::Local>> locals = repos.AllLocal();
    if (!locals) return locals.Err();
    Model::Result<std::vector<Model::Novedad>> novedades = repos.AllNovedades();
    if (!novedades) return novedades.Err();

    std::map<std::string, std::wstring> names;
    for (const Model::Repo& repo : all.Value()) names[repo.id] = repo.nameWithOwner;

    std::map<std::string, std::vector<Model::Novedad>> byRepo;
    for (Model::Novedad& novedad : novedades.Value()) {
        byRepo[novedad.repoId].push_back(std::move(novedad));
    }

    Json root;
    root["brujula"] = kBackupVersion;
    root["exportado"] = Model::FormatIso8601(now);

    Json lista = Json::array();
    std::set<std::string> written;
    for (const Model::Local& local : locals.Value()) {
        const auto name = names.find(local.repoId);
        lista.push_back(Entry(local, name != names.end() ? name->second : std::wstring(),
                              byRepo[local.repoId]));
        written.insert(local.repoId);
    }
    // Un repositorio puede tener novedades y ninguna fila en 'local' —se escribió una nota y
    // no se tocó la prioridad—. Sin esta vuelta, esas novedades no saldrían en la copia.
    for (auto& [repoId, lote] : byRepo) {
        if (written.count(repoId) != 0) continue;
        Model::Local empty;
        empty.repoId = repoId;
        const auto name = names.find(repoId);
        lista.push_back(
            Entry(empty, name != names.end() ? name->second : std::wstring(), lote));
    }
    root["repositorios"] = std::move(lista);

    // Con sangría: es un archivo que el usuario guarda, puede abrir y quizá quiera mirar.
    return root.dump(2);
}

Model::Result<ImportReport> ImportJson(Db& db, std::string_view json, ImportMode mode) {
    // allow_exceptions = false, igual que en github/Parse.cpp: un archivo truncado tiene que
    // dar un error que se enseñe, no una excepción.
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return Model::Oops(Model::Fail::Storage, L"Ese archivo no es una copia de Brújula");
    }

    const Json* version = Field(root, "brujula");
    if (version == nullptr || !version->is_number_integer()) {
        return Model::Oops(Model::Fail::Storage, L"Ese archivo no es una copia de Brújula");
    }
    if (version->get<int>() > kBackupVersion) {
        return Model::Oops(Model::Fail::Storage,
                           L"La copia la escribió una versión más nueva de Brújula");
    }

    const Json* lista = Field(root, "repositorios");
    if (lista == nullptr || !lista->is_array()) return ImportReport{};

    Repos repos(db);
    Model::Result<std::vector<Model::Repo>> all = repos.All();
    if (!all) return all.Err();

    std::map<std::wstring, std::string> byName;
    std::set<std::string> ids;
    for (const Model::Repo& repo : all.Value()) {
        byName[repo.nameWithOwner] = repo.id;
        ids.insert(repo.id);
    }

    // Todo dentro de una transacción. Una importación a medias sería la peor de las dos
    // mitades: ni la copia ni lo que había.
    Transaction tx(db);
    if (Model::Outcome started = tx.Begin(); !started) return started.Err();

    ImportReport report;
    for (const Json& node : *lista) {
        if (!node.is_object()) continue;

        // Primero por nombre. El identificador es el respaldo, no al revés: una copia hecha
        // antes de que GitHub renumerara sus identificadores tiene que poder restaurarse
        // después, y el nombre completo es lo único que sobrevive a eso.
        std::string target;
        if (const auto found = byName.find(Wide(node, "nombre")); found != byName.end()) {
            target = found->second;
        } else if (const std::string id = Narrow(node, "id"); ids.count(id) != 0) {
            target = id;
        }
        if (target.empty()) {
            ++report.skipped;
            continue;
        }
        ++report.matched;

        Model::Result<Model::Local> currentRead = repos.LocalOf(target);
        if (!currentRead) return currentRead.Err();
        Model::Local current = currentRead.Take();

        const Model::Instant stamp = When(node, "actualizado");
        const bool overwrite = mode == ImportMode::Replace || stamp > current.updatedAt;
        if (!overwrite) {
            ++report.kept;
        } else {
            if (const auto priority = Model::PriorityFromSlug(Narrow(node, "prioridad"))) {
                current.priority = *priority;
            }
            if (const auto state = Model::StateFromSlug(Narrow(node, "estado"))) {
                current.state = *state;
            }
            current.nextStep = Wide(node, "siguiente_paso");
            current.folder = Wide(node, "carpeta");
            current.updatedAt = stamp;
            // modo_repo y confirmado NO se copian, aunque vengan en el archivo. Ver la
            // cabecera: un archivo no puede autorizar commits en ciento nueve repositorios.
            if (Model::Outcome saved = repos.SaveLocal(current); !saved) return saved.Err();
        }

        const Json* entradas = Field(node, "novedades");
        if (entradas == nullptr || !entradas->is_array()) continue;

        Model::Result<std::vector<Model::Novedad>> haveRead = repos.NovedadesOf(target);
        if (!haveRead) return haveRead.Err();
        // No es const, y no es un descuido: lo que se va añadiendo se apunta aquí. Sin eso,
        // un archivo que traiga dos veces la misma nota —porque alguien juntó dos copias a
        // mano— la metería dos veces.
        std::vector<Model::Novedad> have = haveRead.Take();

        for (const Json& entrada : *entradas) {
            if (!entrada.is_object()) continue;
            Model::Novedad novedad;
            novedad.repoId = target;
            novedad.day = Narrow(entrada, "fecha");
            novedad.text = Wide(entrada, "texto");
            if (novedad.text.empty()) continue;
            novedad.createdAt = When(entrada, "creado");

            // Unión por fecha y texto, como en Proyecto::Merge. Importar dos veces la misma
            // copia no puede duplicar las notas.
            bool already = false;
            for (const Model::Novedad& mine : have) {
                if (mine.day == novedad.day && mine.text == novedad.text) {
                    already = true;
                    break;
                }
            }
            if (already) continue;
            if (Model::Outcome added = repos.AddNovedad(novedad); !added) return added.Err();
            have.push_back(novedad);
            ++report.novedades;
        }
    }

    if (Model::Outcome committed = tx.Commit(); !committed) return committed.Err();
    return report;
}

}  // namespace Store
