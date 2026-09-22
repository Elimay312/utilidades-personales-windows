#include "store/Schema.h"

#include <string>

namespace Store {
namespace {

// --- v1 ---------------------------------------------------------------------------
//
// Dos mitades que no se mezclan: 'repos' es del servidor y la sincronización la reescribe;
// 'local' y 'novedades' son del usuario y la sincronización no las toca nunca. Que sean
// tablas distintas es lo que hace imposible que una sincronización se lleve por delante un
// siguiente paso escrito a mano.
constexpr const char* kV1 = R"SQL(
CREATE TABLE repos (
  id               TEXT PRIMARY KEY,
  name             TEXT NOT NULL,
  owner            TEXT NOT NULL,
  name_with_owner  TEXT NOT NULL UNIQUE,
  description      TEXT,
  url              TEXT NOT NULL,
  is_private       INTEGER NOT NULL DEFAULT 0,
  is_archived      INTEGER NOT NULL DEFAULT 0,
  is_fork          INTEGER NOT NULL DEFAULT 0,
  language         TEXT,
  language_color   TEXT,
  stars            INTEGER NOT NULL DEFAULT 0,
  disk_usage       INTEGER,
  created_at       INTEGER NOT NULL,
  updated_at       INTEGER NOT NULL,
  pushed_at        INTEGER,

  enriched         INTEGER NOT NULL DEFAULT 0,
  enriched_push    INTEGER,
  default_branch   TEXT,
  commit_oid       TEXT,
  commit_date      INTEGER,
  commit_title     TEXT,
  open_issues      INTEGER,
  open_prs         INTEGER,
  proyecto_oid     TEXT,
  proyecto_text    TEXT,

  seen_seq         INTEGER NOT NULL DEFAULT 0,
  seen_at          INTEGER NOT NULL DEFAULT 0,
  gone_at          INTEGER
);

CREATE TABLE local (
  repo_id    TEXT PRIMARY KEY REFERENCES repos(id) ON UPDATE CASCADE,
  priority   TEXT NOT NULL DEFAULT 'sin-clasificar',
  state      TEXT NOT NULL DEFAULT 'activo',
  next_step  TEXT NOT NULL DEFAULT '',
  repo_mode  INTEGER NOT NULL DEFAULT 0,
  folder     TEXT,
  updated_at INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE novedades (
  id         INTEGER PRIMARY KEY,
  repo_id    TEXT NOT NULL REFERENCES repos(id) ON UPDATE CASCADE,
  day        TEXT NOT NULL,
  text       TEXT NOT NULL,
  created_at INTEGER NOT NULL
);

CREATE INDEX novedades_por_repo ON novedades(repo_id, day DESC);
CREATE INDEX repos_por_push ON repos(pushed_at DESC);

CREATE TABLE ajustes (
  clave TEXT PRIMARY KEY,
  valor TEXT NOT NULL
);
)SQL";

// --- v2 ---------------------------------------------------------------------------
//
// Lo que el inspector de la fase 5 necesita enseñar sin esperar a la red, y lo que hace falta
// para escribir en el repositorio.
//
// Las dos tablas nuevas van con las mismas claves ajenas que las de v1 y por el mismo motivo,
// que es el contrario del que uno escribe por inercia: SIN cascada al borrar —borrar un
// repositorio tiene que fallar, porque colgaría las notas— y CON cascada al renumerar, para
// que el día que GitHub cambie sus identificadores globales todo siga colgando de su sitio.
constexpr const char* kV2 = R"SQL(
CREATE TABLE commits (
  repo_id   TEXT NOT NULL REFERENCES repos(id) ON UPDATE CASCADE,
  ord       INTEGER NOT NULL,
  oid       TEXT NOT NULL,
  title     TEXT NOT NULL,
  author    TEXT,
  committed INTEGER,
  PRIMARY KEY (repo_id, ord)
);

-- Los .md de la raíz que no son README, CHANGELOG ni LICENSE. Solo el NOMBRE: el contenido
-- se pide cuando alguien quiere copiarlo a las novedades, y guardar ciento nueve archivos
-- para enseñar una lista de nombres sería llenar la caché de texto que nadie mira.
CREATE TABLE raiz_md (
  repo_id TEXT NOT NULL REFERENCES repos(id) ON UPDATE CASCADE,
  name    TEXT NOT NULL,
  PRIMARY KEY (repo_id, name)
);

ALTER TABLE local ADD COLUMN repo_confirmed INTEGER NOT NULL DEFAULT 0;
ALTER TABLE local ADD COLUMN push_pending   INTEGER NOT NULL DEFAULT 0;
)SQL";

// --- v3 ---------------------------------------------------------------------------
//
// El orden puesto a mano, que es lo que hace falta para poder arrastrar una tarjeta dentro
// de su grupo y que se quede donde se la dejó.
//
// Va en 'local' y no en 'repos' porque es del usuario: la sincronización no puede tocarlo,
// igual que no toca el siguiente paso. Y por omisión vale CERO, que significa "este nunca
// se ha ordenado a mano" — o sea que una caché de la fase 5 sigue enseñándose exactamente
// igual después de migrar, ordenada por el último push.
constexpr const char* kV3 = R"SQL(
ALTER TABLE local ADD COLUMN orden INTEGER NOT NULL DEFAULT 0;
)SQL";

// --- v4 ---------------------------------------------------------------------------
//
// Aplazar una pregunta de la revisión semanal. Dos columnas y no una: el DÍA en que vuelve
// a preguntarse y POR QUÉ se dejó de preguntar, que es lo que hace que aplazar silencie una
// pregunta y no el repositorio entero (ver el comentario de Local en model/Types.h).
//
// Van en 'local' porque son del usuario, como 'orden', y las dos nacen vacías: cero es
// "nunca se aplazó nada", así que una caché de la fase 7 se ve exactamente igual después de
// migrar. El día se guarda como número de días desde la época y no como segundos, que es lo
// que hace que el plazo venza al empezar el día y no a la hora a la que se pidió.
constexpr const char* kV4 = R"SQL(
ALTER TABLE local ADD COLUMN pospuesto_hasta INTEGER NOT NULL DEFAULT 0;
ALTER TABLE local ADD COLUMN pospuesto_por   TEXT    NOT NULL DEFAULT '';
)SQL";

struct Migration {
    int version;
    const char* sql;
};

constexpr Migration kMigrations[] = {
    {1, kV1},
    {2, kV2},
    {3, kV3},
    {4, kV4},
};

}  // namespace

Model::Outcome Migrate(Db& db) {
    Model::Result<std::int64_t> current = db.UserVersion();
    if (!current) return current.Err();

    const std::int64_t from = current.Value();
    if (from > kSchemaVersion) {
        return Model::Oops(
            Model::Fail::Storage,
            L"La caché local la escribió una versión más nueva de Brújula. "
            L"Actualiza la aplicación o borra la caché.",
            static_cast<int>(from));
    }
    if (from == kSchemaVersion) return Model::Ok();

    // Todo dentro de una transacción, incluido el user_version. Si un CREATE TABLE falla,
    // el destructor de Transaction deshace lo que hubiera y el número se queda donde estaba,
    // así que el siguiente arranque vuelve a intentarlo desde el mismo sitio.
    Transaction tx(db);
    if (Model::Outcome started = tx.Begin(); !started) return started;

    for (const Migration& step : kMigrations) {
        if (step.version <= from) continue;
        if (Model::Outcome applied = db.Exec(step.sql); !applied) return applied;
    }

    if (Model::Outcome stamped = db.SetUserVersion(kSchemaVersion); !stamped) return stamped;
    return tx.Commit();
}

}  // namespace Store
