#pragma once

// La caché: leer y escribir repositorios, lo del usuario y los ajustes.
//
// Toda la sincronización pasa por aquí, y aquí están las tres decisiones que protegen los
// datos de trabajo:
//
//   1. Escribir metadatos NO toca ni una columna del usuario ni las del segundo pase.
//   2. Un repositorio que deja de aparecer se marca, no se borra. Borrarlo se llevaría por
//      delante sus notas, y una sincronización a medias —un 502 en la segunda página— haría
//      justo eso.
//   3. La fila se reconoce por el node id y, si ese falla, por el nombre completo. GitHub
//      está migrando sus identificadores globales, y el día que cambien, reconocer solo por
//      id convertiría los 109 repositorios en 109 repositorios nuevos, con las notas viejas
//      colgando de identificadores que ya no existen y sin un solo error por ningún lado.

#include <optional>
#include <string>
#include <vector>

#include "model/Types.h"
#include "store/Db.h"

namespace Store {

struct Stats {
    int total = 0;
    int enriched = 0;
    int gone = 0;
    int withProyecto = 0;
};

class Repos {
public:
    explicit Repos(Db& db) : m_db(db) {}

    // Abre una pasada de sincronización y devuelve su número. Cada pasada tiene el suyo, y
    // es lo que luego distingue "apareció en esta" de "ya no aparece". Un sello de tiempo no
    // valdría: dos sincronizaciones en el mismo segundo tendrían el mismo.
    Model::Result<std::int64_t> BeginSync();

    // Primer pase. No toca 'local', ni 'novedades', ni las columnas del segundo pase.
    Model::Outcome UpsertMetadata(const std::vector<Model::Repo>& repos, std::int64_t seq,
                                  Model::Instant now);

    // Marca los que no aparecieron en la pasada 'seq'. Devuelve cuántos.
    Model::Result<int> MarkMissing(std::int64_t seq, Model::Instant now);

    // Los que hay que enriquecer: nuevos, o con un push posterior al detalle que tenemos.
    // Ordenados por push descendente, para que lo que el usuario mira primero llegue antes.
    Model::Result<std::vector<std::string>> NeedingEnrichment();

    // Segundo pase. 'enrichedPush' sale de la respuesta que trajo el detalle, no del primer
    // pase: es la fecha del dato que de verdad tenemos guardado.
    Model::Outcome ApplyEnrichment(const Model::Repo& repo);

    Model::Result<std::vector<Model::Repo>> All();
    Model::Result<Stats> Counts();

    // Lo del usuario. Si no hay fila, devuelve un Local con los valores por omisión: un
    // repositorio recién visto está sin clasificar y activo, y eso no hace falta escribirlo.
    Model::Result<Model::Local> LocalOf(const std::string& repoId);
    Model::Outcome SaveLocal(const Model::Local& local);

    Model::Outcome AddNovedad(const Model::Novedad& novedad);
    Model::Result<std::vector<Model::Novedad>> NovedadesOf(const std::string& repoId);

    // Ajustes: última sincronización, login de la cuenta, alcance de la credencial. Nunca
    // la credencial, que va al Administrador de credenciales y a ningún otro sitio.
    Model::Result<std::string> Setting(const std::string& key);
    Model::Outcome SetSetting(const std::string& key, const std::string& value);

private:
    Db& m_db;
};

}  // namespace Store
