#include "store/Repos.h"

#include "model/Utf.h"

namespace Store {
namespace {

// Vacío se guarda como NULL y no como cadena vacía. No es cosmética: 76 de los 109
// repositorios no tienen descripción y 7 no tienen lenguaje, así que "no hay" es el caso
// mayoritario y tiene que poder distinguirse de "hay, y está vacío" cuando la fase 4
// pregunte cuántos faltan por describir.
void BindTextOrNull(Stmt& stmt, int index, const std::wstring& text) {
    if (text.empty()) {
        stmt.BindNull(index);
    } else {
        stmt.Bind(index, text);
    }
}

// El orden de las columnas de All(). Se escribe una vez y se lee por nombre, porque leer
// por número una lista de veintitantas es la manera de desplazar todos los campos un sitio
// y que la aplicación siga arrancando con los datos cambiados de columna.
constexpr const char* kRepoColumns =
    "id, name, owner, name_with_owner, description, url, "
    "is_private, is_archived, is_fork, language, language_color, "
    "stars, disk_usage, created_at, updated_at, pushed_at, "
    "enriched, enriched_push, default_branch, commit_oid, commit_date, commit_title, "
    "open_issues, open_prs, proyecto_oid, proyecto_text, seen_at, gone_at";

enum Column {
    kId = 0, kName, kOwner, kNameWithOwner, kDescription, kUrl,
    kIsPrivate, kIsArchived, kIsFork, kLanguage, kLanguageColor,
    kStars, kDiskUsage, kCreatedAt, kUpdatedAt, kPushedAt,
    kEnriched, kEnrichedPush, kDefaultBranch, kCommitOid, kCommitDate, kCommitTitle,
    kOpenIssues, kOpenPrs, kProyectoOid, kProyectoText, kSeenAt, kGoneAt,
};

Model::Repo ReadRepo(const Stmt& stmt) {
    Model::Repo repo;
    repo.id = stmt.Text(kId);
    repo.name = stmt.Wide(kName);
    repo.owner = stmt.Wide(kOwner);
    repo.nameWithOwner = stmt.Wide(kNameWithOwner);
    repo.description = stmt.Wide(kDescription);
    repo.url = stmt.Wide(kUrl);
    repo.isPrivate = stmt.Int(kIsPrivate) != 0;
    repo.isArchived = stmt.Int(kIsArchived) != 0;
    repo.isFork = stmt.Int(kIsFork) != 0;
    repo.language = stmt.Wide(kLanguage);
    repo.languageColor = stmt.Wide(kLanguageColor);
    repo.stars = static_cast<int>(stmt.Int(kStars));
    repo.diskUsage = stmt.Int(kDiskUsage);
    repo.createdAt = Model::FromEpoch(stmt.Int(kCreatedAt));
    repo.updatedAt = Model::FromEpoch(stmt.Int(kUpdatedAt));
    repo.pushedAt = stmt.When(kPushedAt);
    repo.enriched = stmt.Int(kEnriched) != 0;
    repo.enrichedPush = stmt.When(kEnrichedPush);
    repo.defaultBranch = stmt.Wide(kDefaultBranch);
    repo.commitOid = stmt.Wide(kCommitOid);
    repo.commitDate = stmt.When(kCommitDate);
    repo.commitTitle = stmt.Wide(kCommitTitle);
    repo.openIssues = static_cast<int>(stmt.Int(kOpenIssues));
    repo.openPrs = static_cast<int>(stmt.Int(kOpenPrs));
    repo.proyectoOid = stmt.Wide(kProyectoOid);
    repo.proyectoText = stmt.Wide(kProyectoText);
    repo.seenAt = Model::FromEpoch(stmt.Int(kSeenAt));
    repo.goneAt = stmt.When(kGoneAt);
    return repo;
}

}  // namespace

Model::Result<std::int64_t> Repos::BeginSync() {
    Model::Result<std::string> stored = Setting("sync_seq");
    if (!stored) return stored.Err();

    std::int64_t seq = 0;
    if (!stored.Value().empty()) {
        // Si alguien editó el valor a mano y no es un número, se empieza de cero. Peor caso:
        // una pasada marca todo como ausente y la siguiente lo devuelve. Nada se borra.
        try {
            seq = std::stoll(stored.Value());
        } catch (...) {
            seq = 0;
        }
    }
    ++seq;

    if (Model::Outcome saved = SetSetting("sync_seq", std::to_string(seq)); !saved) {
        return saved.Err();
    }
    return seq;
}

Model::Outcome Repos::UpsertMetadata(const std::vector<Model::Repo>& repos, std::int64_t seq,
                                     Model::Instant now) {
    if (repos.empty()) return Model::Ok();

    // Una transacción para los 109. Sin ella son 109 confirmaciones a disco y la primera
    // sincronización tarda más en guardar que en descargar.
    Transaction tx(m_db);
    if (Model::Outcome started = tx.Begin(); !started) return started;

    // Paso 1: reenganchar por nombre. Solo hace algo si el id de un repositorio cambió, que
    // es lo que pasará el día que GitHub retire los identificadores viejos. ON UPDATE CASCADE
    // se lleva con él las notas y las novedades, que es el motivo entero de la maniobra.
    //
    // El NOT EXISTS evita el caso raro de dos repositorios intercambiándose el nombre entre
    // dos sincronizaciones: sin él, el UPDATE chocaría contra la clave primaria.
    Model::Result<Stmt> relinkPrepared = m_db.Prepare(
        "UPDATE repos SET id = ?1 "
        " WHERE name_with_owner = ?2 AND id <> ?1 "
        "   AND NOT EXISTS (SELECT 1 FROM repos WHERE id = ?1)");
    if (!relinkPrepared) return relinkPrepared.Err();
    Stmt relink = relinkPrepared.Take();

    // Paso 2: el upsert de siempre. Fíjate en lo que NO aparece en el DO UPDATE: enriched,
    // enriched_push, commit_*, open_*, proyecto_*. Son del segundo pase y sobrevivirlo es
    // justo lo que hace que la segunda sincronización no tenga que volver a pedirlas.
    Model::Result<Stmt> upsertPrepared = m_db.Prepare(
        "INSERT INTO repos (id, name, owner, name_with_owner, description, url, "
        "                   is_private, is_archived, is_fork, language, language_color, "
        "                   stars, disk_usage, created_at, updated_at, pushed_at, "
        "                   seen_seq, seen_at, gone_at) "
        "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,NULL) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  name = excluded.name, owner = excluded.owner, "
        "  name_with_owner = excluded.name_with_owner, description = excluded.description, "
        "  url = excluded.url, is_private = excluded.is_private, "
        "  is_archived = excluded.is_archived, is_fork = excluded.is_fork, "
        "  language = excluded.language, language_color = excluded.language_color, "
        "  stars = excluded.stars, disk_usage = excluded.disk_usage, "
        "  created_at = excluded.created_at, updated_at = excluded.updated_at, "
        "  pushed_at = excluded.pushed_at, seen_seq = excluded.seen_seq, "
        "  seen_at = excluded.seen_at, gone_at = NULL");
    if (!upsertPrepared) return upsertPrepared.Err();
    Stmt upsert = upsertPrepared.Take();

    const std::int64_t nowEpoch = Model::ToEpoch(now);

    for (const Model::Repo& repo : repos) {
        relink.Reset();
        relink.Bind(1, repo.id);
        relink.Bind(2, Model::ToUtf8(repo.nameWithOwner));
        if (Model::Result<bool> stepped = relink.Step(); !stepped) return stepped.Err();

        upsert.Reset();
        upsert.Bind(1, repo.id);
        upsert.Bind(2, repo.name);
        upsert.Bind(3, repo.owner);
        upsert.Bind(4, repo.nameWithOwner);
        BindTextOrNull(upsert, 5, repo.description);
        upsert.Bind(6, repo.url);
        upsert.Bind(7, repo.isPrivate);
        upsert.Bind(8, repo.isArchived);
        upsert.Bind(9, repo.isFork);
        BindTextOrNull(upsert, 10, repo.language);
        BindTextOrNull(upsert, 11, repo.languageColor);
        upsert.Bind(12, repo.stars);
        upsert.Bind(13, repo.diskUsage);
        upsert.Bind(14, Model::ToEpoch(repo.createdAt));
        upsert.Bind(15, Model::ToEpoch(repo.updatedAt));
        upsert.Bind(16, repo.pushedAt);
        upsert.Bind(17, seq);
        upsert.Bind(18, nowEpoch);
        if (Model::Result<bool> stepped = upsert.Step(); !stepped) return stepped.Err();
    }

    return tx.Commit();
}

Model::Result<int> Repos::MarkMissing(std::int64_t seq, Model::Instant now) {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "UPDATE repos SET gone_at = ?1 WHERE seen_seq <> ?2 AND gone_at IS NULL");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, Model::ToEpoch(now));
    stmt.Bind(2, seq);
    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();

    return sqlite3_changes(m_db.Handle());
}

Model::Result<std::vector<std::string>> Repos::NeedingEnrichment() {
    // 'enriched = 0' cubre el repositorio nuevo y el que se quedó a medias. La otra mitad,
    // 'enriched_push IS NOT pushed_at', usa el IS NOT de SQLite y no <> a propósito: con <>,
    // un NULL a cualquiera de los dos lados da NULL, que no es verdadero, y un repositorio
    // sin push nunca entraría por aquí.
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT id FROM repos "
        " WHERE gone_at IS NULL AND (enriched = 0 OR enriched_push IS NOT pushed_at) "
        " ORDER BY pushed_at DESC");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    std::vector<std::string> ids;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;
        ids.push_back(stmt.Text(0));
    }
    return ids;
}

Model::Outcome Repos::SaveCommits(const std::string& repoId,
                                  const std::vector<Model::Commit>& commits) {
    // Borrar y volver a escribir, no fusionar. Un rebase en la rama principal cambia los
    // cinco de golpe, y fusionar por oid dejaría en pantalla commits que ya no existen.
    Model::Result<Stmt> cleared = m_db.Prepare("DELETE FROM commits WHERE repo_id = ?1");
    if (!cleared) return cleared.Err();
    Stmt wipe = cleared.Take();
    wipe.Bind(1, repoId);
    if (Model::Result<bool> stepped = wipe.Step(); !stepped) return stepped.Err();

    if (commits.empty()) return Model::Ok();

    Model::Result<Stmt> prepared = m_db.Prepare(
        "INSERT INTO commits (repo_id, ord, oid, title, author, committed) "
        "VALUES (?1,?2,?3,?4,?5,?6)");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    for (std::size_t i = 0; i < commits.size(); ++i) {
        const Model::Commit& commit = commits[i];
        stmt.Reset();
        stmt.Bind(1, repoId);
        stmt.Bind(2, static_cast<std::int64_t>(i));
        stmt.Bind(3, commit.oid);
        stmt.Bind(4, commit.title);
        BindTextOrNull(stmt, 5, commit.author);
        stmt.Bind(6, commit.committedAt);
        if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    }
    return Model::Ok();
}

Model::Result<std::vector<Model::Commit>> Repos::CommitsOf(const std::string& repoId) {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT oid, title, author, committed FROM commits WHERE repo_id = ?1 ORDER BY ord");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, repoId);

    std::vector<Model::Commit> out;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;

        Model::Commit commit;
        commit.oid = stmt.Wide(0);
        commit.title = stmt.Wide(1);
        commit.author = stmt.Wide(2);
        commit.committedAt = stmt.When(3);
        out.push_back(std::move(commit));
    }
    return out;
}

Model::Outcome Repos::SaveRootMarkdown(const std::string& repoId,
                                       const std::vector<std::wstring>& names) {
    Model::Result<Stmt> cleared = m_db.Prepare("DELETE FROM raiz_md WHERE repo_id = ?1");
    if (!cleared) return cleared.Err();
    Stmt wipe = cleared.Take();
    wipe.Bind(1, repoId);
    if (Model::Result<bool> stepped = wipe.Step(); !stepped) return stepped.Err();

    if (names.empty()) return Model::Ok();

    // OR IGNORE: dos entradas con el mismo nombre no pueden pasar en un árbol de Git, pero
    // la clave primaria haría fallar la sincronización entera si alguna vez pasara.
    Model::Result<Stmt> prepared =
        m_db.Prepare("INSERT OR IGNORE INTO raiz_md (repo_id, name) VALUES (?1, ?2)");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    for (const std::wstring& name : names) {
        stmt.Reset();
        stmt.Bind(1, repoId);
        stmt.Bind(2, name);
        if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    }
    return Model::Ok();
}

Model::Result<std::vector<std::wstring>> Repos::RootMarkdownOf(const std::string& repoId) {
    Model::Result<Stmt> prepared =
        m_db.Prepare("SELECT name FROM raiz_md WHERE repo_id = ?1 ORDER BY name");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, repoId);

    std::vector<std::wstring> out;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;
        out.push_back(stmt.Wide(0));
    }
    return out;
}

Model::Outcome Repos::ApplyEnrichment(const Model::Repo& repo) {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "UPDATE repos SET enriched = 1, enriched_push = ?1, pushed_at = ?1, "
        "  default_branch = ?2, commit_oid = ?3, commit_date = ?4, commit_title = ?5, "
        "  open_issues = ?6, open_prs = ?7, proyecto_oid = ?8, proyecto_text = ?9 "
        " WHERE id = ?10");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    // El mismo valor en enriched_push y en pushed_at. Es lo que deja la fila coherente
    // cuando entra un push entre los dos pases: el detalle guardado es el de ESTA fecha, y
    // la siguiente sincronización compara contra ella.
    stmt.Bind(1, repo.enrichedPush);
    BindTextOrNull(stmt, 2, repo.defaultBranch);
    BindTextOrNull(stmt, 3, repo.commitOid);
    stmt.Bind(4, repo.commitDate);
    BindTextOrNull(stmt, 5, repo.commitTitle);
    stmt.Bind(6, repo.openIssues);
    stmt.Bind(7, repo.openPrs);
    BindTextOrNull(stmt, 8, repo.proyectoOid);
    BindTextOrNull(stmt, 9, repo.proyectoText);
    stmt.Bind(10, repo.id);

    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();

    // Las dos listas van DESPUÉS de la fila y dentro de la misma tanda: quien llama envuelve
    // el lote entero en una transacción, así que o entra todo o no entra nada.
    if (Model::Outcome saved = SaveCommits(repo.id, repo.commits); !saved) return saved;
    return SaveRootMarkdown(repo.id, repo.rootMarkdown);
}

Model::Result<std::vector<Model::Repo>> Repos::All() {
    const std::string sql =
        std::string("SELECT ") + kRepoColumns + " FROM repos ORDER BY pushed_at DESC";

    Model::Result<Stmt> prepared = m_db.Prepare(sql);
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    std::vector<Model::Repo> repos;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;
        repos.push_back(ReadRepo(stmt));
    }
    return repos;
}

Model::Result<Model::Repo> Repos::RepoOf(const std::string& repoId) {
    const std::string sql =
        std::string("SELECT ") + kRepoColumns + " FROM repos WHERE id = ?1";

    Model::Result<Stmt> prepared = m_db.Prepare(sql);
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, repoId);

    Model::Result<bool> row = stmt.Step();
    if (!row) return row.Err();
    if (!row.Value()) {
        // Vacío es un error aquí y no en LocalOf, y la diferencia importa: no tener notas es
        // normal, no tener repositorio significa que alguien pidió escribir en algo que esta
        // caché no conoce.
        return Model::Oops(Model::Fail::Storage, L"Ese repositorio no está en la caché");
    }
    return ReadRepo(stmt);
}

Model::Result<Stats> Repos::Counts() {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT count(*), sum(enriched), sum(gone_at IS NOT NULL), "
        "       sum(proyecto_text IS NOT NULL) FROM repos");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    Model::Result<bool> row = stmt.Step();
    if (!row) return row.Err();

    Stats stats;
    if (row.Value()) {
        stats.total = static_cast<int>(stmt.Int(0));
        stats.enriched = static_cast<int>(stmt.Int(1));
        stats.gone = static_cast<int>(stmt.Int(2));
        stats.withProyecto = static_cast<int>(stmt.Int(3));
    }
    return stats;
}

Model::Result<Model::Local> Repos::LocalOf(const std::string& repoId) {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT priority, state, next_step, repo_mode, folder, updated_at, "
        "       repo_confirmed, push_pending "
        "  FROM local WHERE repo_id = ?1");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, repoId);

    Model::Result<bool> row = stmt.Step();
    if (!row) return row.Err();

    Model::Local local;
    local.repoId = repoId;
    if (!row.Value()) {
        // Sin fila: los valores por omisión. Un repositorio recién sincronizado está sin
        // clasificar y activo, y escribir 109 filas para decir eso sería escribir 109 filas
        // para no decir nada.
        return local;
    }

    // Lo que no se reconoce se queda con el valor por omisión en vez de romper: la fase 5
    // escribirá aquí lo que lea de un PROYECTO.md editado a mano.
    if (const auto priority = Model::PriorityFromSlug(stmt.Text(0))) local.priority = *priority;
    if (const auto state = Model::StateFromSlug(stmt.Text(1))) local.state = *state;
    local.nextStep = stmt.Wide(2);
    local.repoMode = stmt.Int(3) != 0;
    local.folder = stmt.Wide(4);
    local.updatedAt = Model::FromEpoch(stmt.Int(5));
    local.repoConfirmed = stmt.Int(6) != 0;
    local.pushPending = stmt.Int(7) != 0;
    return local;
}

Model::Result<std::vector<Model::Local>> Repos::AllLocal() {
    // Una consulta y no 109. No es micro-optimización: el criterio de aceptación de la
    // fase 4 es que la ventana enseñe la lista en menos de 200 ms, y lo que se hace antes
    // de enseñarla hay que poder contarlo con los dedos.
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT repo_id, priority, state, next_step, repo_mode, folder, updated_at, "
        "       repo_confirmed, push_pending FROM local");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    std::vector<Model::Local> locals;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;

        Model::Local local;
        local.repoId = stmt.Text(0);
        if (const auto priority = Model::PriorityFromSlug(stmt.Text(1))) {
            local.priority = *priority;
        }
        if (const auto state = Model::StateFromSlug(stmt.Text(2))) local.state = *state;
        local.nextStep = stmt.Wide(3);
        local.repoMode = stmt.Int(4) != 0;
        local.folder = stmt.Wide(5);
        local.updatedAt = Model::FromEpoch(stmt.Int(6));
        local.repoConfirmed = stmt.Int(7) != 0;
        local.pushPending = stmt.Int(8) != 0;
        locals.push_back(std::move(local));
    }
    return locals;
}

Model::Outcome Repos::SaveLocal(const Model::Local& local) {
    // Fíjate en lo que NO aparece: push_pending. Lo escribe SetPushPending y nadie más,
    // porque el que lo apaga es el hilo de trabajo cuando el commit sale bien y el que lo
    // enciende es la interfaz al guardar. Listarlo aquí haría que un guardado hecho mientras
    // sube el anterior borrase la marca del que todavía está en vuelo.
    Model::Result<Stmt> prepared = m_db.Prepare(
        "INSERT INTO local (repo_id, priority, state, next_step, repo_mode, folder, "
        "                   updated_at, repo_confirmed) "
        "VALUES (?1,?2,?3,?4,?5,?6,?7,?8) "
        "ON CONFLICT(repo_id) DO UPDATE SET "
        "  priority = excluded.priority, state = excluded.state, "
        "  next_step = excluded.next_step, repo_mode = excluded.repo_mode, "
        "  folder = excluded.folder, updated_at = excluded.updated_at, "
        "  repo_confirmed = excluded.repo_confirmed");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, local.repoId);
    stmt.Bind(2, std::string_view(Model::SlugOf(local.priority)));
    stmt.Bind(3, std::string_view(Model::SlugOf(local.state)));
    stmt.Bind(4, local.nextStep);
    stmt.Bind(5, local.repoMode);
    BindTextOrNull(stmt, 6, local.folder);
    stmt.Bind(7, Model::ToEpoch(local.updatedAt));
    stmt.Bind(8, local.repoConfirmed);

    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    return Model::Ok();
}

Model::Outcome Repos::SetPushPending(const std::string& repoId, bool pending) {
    // INSERT y no UPDATE a secas: puede no haber fila todavía —un repositorio del que solo
    // se ha tocado el interruptor— y un UPDATE sobre cero filas no da error, no hace nada y
    // deja el pendiente sin apuntar.
    Model::Result<Stmt> prepared = m_db.Prepare(
        "INSERT INTO local (repo_id, push_pending) VALUES (?1, ?2) "
        "ON CONFLICT(repo_id) DO UPDATE SET push_pending = excluded.push_pending");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, repoId);
    stmt.Bind(2, pending);
    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    return Model::Ok();
}

Model::Result<std::vector<std::string>> Repos::PendingPushes() {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT repo_id FROM local "
        " WHERE push_pending = 1 AND repo_mode = 1 AND repo_confirmed = 1");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    std::vector<std::string> ids;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;
        ids.push_back(stmt.Text(0));
    }
    return ids;
}

Model::Outcome Repos::SaveProyectoBlob(const std::string& repoId, const std::wstring& oid,
                                       const std::wstring& text) {
    Model::Result<Stmt> prepared =
        m_db.Prepare("UPDATE repos SET proyecto_oid = ?1, proyecto_text = ?2 WHERE id = ?3");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    BindTextOrNull(stmt, 1, oid);
    BindTextOrNull(stmt, 2, text);
    stmt.Bind(3, repoId);
    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    return Model::Ok();
}

Model::Outcome Repos::AddNovedad(const Model::Novedad& novedad) {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "INSERT INTO novedades (repo_id, day, text, created_at) VALUES (?1,?2,?3,?4)");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, novedad.repoId);
    stmt.Bind(2, novedad.day);
    stmt.Bind(3, novedad.text);
    stmt.Bind(4, Model::ToEpoch(novedad.createdAt));

    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    return Model::Ok();
}

Model::Outcome Repos::DeleteNovedad(std::int64_t id) {
    Model::Result<Stmt> prepared = m_db.Prepare("DELETE FROM novedades WHERE id = ?1");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, id);
    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    return Model::Ok();
}

Model::Result<std::vector<Model::Novedad>> Repos::NovedadesOf(const std::string& repoId) {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT id, day, text, created_at FROM novedades "
        " WHERE repo_id = ?1 ORDER BY day DESC, id DESC");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, repoId);

    std::vector<Model::Novedad> out;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;

        Model::Novedad novedad;
        novedad.id = stmt.Int(0);
        novedad.repoId = repoId;
        novedad.day = stmt.Text(1);
        novedad.text = stmt.Wide(2);
        novedad.createdAt = Model::FromEpoch(stmt.Int(3));
        out.push_back(std::move(novedad));
    }
    return out;
}

Model::Result<std::vector<Model::Novedad>> Repos::AllNovedades() {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "SELECT id, repo_id, day, text, created_at FROM novedades "
        " ORDER BY repo_id, day DESC, id DESC");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    std::vector<Model::Novedad> out;
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;

        Model::Novedad novedad;
        novedad.id = stmt.Int(0);
        novedad.repoId = stmt.Text(1);
        novedad.day = stmt.Text(2);
        novedad.text = stmt.Wide(3);
        novedad.createdAt = Model::FromEpoch(stmt.Int(4));
        out.push_back(std::move(novedad));
    }
    return out;
}

Model::Result<std::string> Repos::Setting(const std::string& key) {
    Model::Result<Stmt> prepared = m_db.Prepare("SELECT valor FROM ajustes WHERE clave = ?1");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, key);

    Model::Result<bool> row = stmt.Step();
    if (!row) return row.Err();
    // Sin fila devuelve cadena vacía y no un error: "todavía no se ha sincronizado nunca"
    // es un estado normal, no un fallo.
    if (!row.Value()) return std::string();
    return stmt.Text(0);
}

Model::Outcome Repos::SetSetting(const std::string& key, const std::string& value) {
    Model::Result<Stmt> prepared = m_db.Prepare(
        "INSERT INTO ajustes (clave, valor) VALUES (?1, ?2) "
        "ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    stmt.Bind(1, key);
    stmt.Bind(2, value);

    if (Model::Result<bool> stepped = stmt.Step(); !stepped) return stepped.Err();
    return Model::Ok();
}

}  // namespace Store
