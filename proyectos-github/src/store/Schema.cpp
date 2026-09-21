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

struct Migration {
    int version;
    const char* sql;
};

constexpr Migration kMigrations[] = {
    {1, kV1},
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
