// La caché. Todo sobre :memory:, así que las pruebas no dejan archivos ni dependen de que
// exista %LOCALAPPDATA%.
//
// Lo que se vigila aquí no es que SQLite funcione: es que una sincronización no pueda tocar
// lo que escribió el usuario. Esa es la prioridad 1 de CLAUDE.md, y el fallo que tendría
// sería silencioso y definitivo — el siguiente paso de un proyecto dormido desaparece, y
// nadie se entera hasta que va a retomarlo, que es meses después y justo cuando hacía falta.

#include <doctest/doctest.h>

#include "store/Repos.h"
#include "store/Schema.h"

#include <string>
#include <vector>

namespace {

Model::Instant At(const char* iso) { return Model::ParseIso8601(iso).value(); }

const Model::Instant kNow = At("2026-09-21T12:00:00Z");

// Una base recién migrada, lista para usar.
struct Fresh {
    Store::Db db;
    Store::Repos repos{db};

    Fresh() {
        REQUIRE(db.Open(":memory:").IsOk());
        REQUIRE(Store::Migrate(db).IsOk());
    }
};

Model::Repo MakeRepo(const char* id, const wchar_t* nameWithOwner, const char* pushedAt) {
    Model::Repo repo;
    repo.id = id;
    repo.name = L"repo";
    repo.owner = L"Elimay312";
    repo.nameWithOwner = nameWithOwner;
    repo.url = L"https://github.com/Elimay312/repo";
    repo.createdAt = At("2025-01-01T00:00:00Z");
    repo.updatedAt = kNow;
    if (pushedAt != nullptr) repo.pushedAt = At(pushedAt);
    return repo;
}

}  // namespace

// ---------------------------------------------------------------------- Migración --

TEST_CASE("una base nueva llega a la versión del esquema") {
    Store::Db db;
    REQUIRE(db.Open(":memory:").IsOk());

    const auto before = db.UserVersion();
    REQUIRE(before.IsOk());
    CHECK(before.Value() == 0);

    REQUIRE(Store::Migrate(db).IsOk());

    const auto after = db.UserVersion();
    REQUIRE(after.IsOk());
    CHECK(after.Value() == Store::kSchemaVersion);
}

TEST_CASE("migrar dos veces no cambia nada") {
    // Es lo que pasa en cada arranque. Si la segunda vez volviera a ejecutar los CREATE
    // TABLE, fallaría; si los ejecutara con IF NOT EXISTS pero repitiera un INSERT de
    // datos iniciales, duplicaría filas sin dar error.
    Fresh fresh;
    CHECK(Store::Migrate(fresh.db).IsOk());
    CHECK(Store::Migrate(fresh.db).IsOk());

    const auto counts = fresh.repos.Counts();
    REQUIRE(counts.IsOk());
    CHECK(counts.Value().total == 0);
}

TEST_CASE("una base de una versión más nueva no se toca") {
    // Pasa al abrir una compilación vieja después de una nueva. Convertirla hacia atrás a
    // ciegas sería tirar columnas con datos dentro, así que lo único correcto es negarse.
    Store::Db db;
    REQUIRE(db.Open(":memory:").IsOk());
    REQUIRE(db.SetUserVersion(Store::kSchemaVersion + 5).IsOk());

    const Model::Outcome migrated = Store::Migrate(db);
    CHECK_FALSE(migrated.IsOk());
    CHECK(migrated.Err().kind == Model::Fail::Storage);
    CHECK_FALSE(migrated.Err().detail.empty());

    // Y la versión sigue donde estaba.
    const auto version = db.UserVersion();
    REQUIRE(version.IsOk());
    CHECK(version.Value() == Store::kSchemaVersion + 5);
}

// ------------------------------------------------------- Lo del servidor y lo mío --

TEST_CASE("la sincronización pisa lo del servidor y no toca lo del usuario") {
    // La prueba central de la fase.
    Fresh fresh;

    const auto seq1 = fresh.repos.BeginSync();
    REQUIRE(seq1.IsOk());
    Model::Repo repo = MakeRepo("R_1", L"Elimay312/brujula", "2026-09-01T00:00:00Z");
    repo.description = L"Priorizador de repositorios";
    REQUIRE(fresh.repos.UpsertMetadata({repo}, seq1.Value(), kNow).IsOk());

    // El usuario escribe lo suyo.
    Model::Local local;
    local.repoId = "R_1";
    local.priority = Model::Priority::Focus;
    local.state = Model::State::Blocked;
    local.nextStep = L"Conectar el lector de carpetas a la columna central";
    local.updatedAt = kNow;
    REQUIRE(fresh.repos.SaveLocal(local).IsOk());

    Model::Novedad novedad;
    novedad.repoId = "R_1";
    novedad.day = "2026-09-20";
    novedad.text = L"Terminada la fase 2, falta probar con rutas largas";
    novedad.createdAt = kNow;
    REQUIRE(fresh.repos.AddNovedad(novedad).IsOk());

    // Y llega otra sincronización, con el servidor diciendo otra cosa.
    const auto seq2 = fresh.repos.BeginSync();
    REQUIRE(seq2.IsOk());
    Model::Repo again = MakeRepo("R_1", L"Elimay312/brujula", "2026-09-21T00:00:00Z");
    again.description = L"Otra descripción";
    again.isArchived = true;
    REQUIRE(fresh.repos.UpsertMetadata({again}, seq2.Value(), kNow).IsOk());

    // Lo del servidor se actualizó.
    const auto all = fresh.repos.All();
    REQUIRE(all.IsOk());
    REQUIRE(all.Value().size() == 1);
    CHECK(all.Value()[0].description == L"Otra descripción");
    CHECK(all.Value()[0].isArchived);

    // Y lo del usuario sigue exactamente donde estaba.
    const auto mine = fresh.repos.LocalOf("R_1");
    REQUIRE(mine.IsOk());
    CHECK(mine.Value().priority == Model::Priority::Focus);
    CHECK(mine.Value().state == Model::State::Blocked);
    CHECK(mine.Value().nextStep == L"Conectar el lector de carpetas a la columna central");

    const auto novedades = fresh.repos.NovedadesOf("R_1");
    REQUIRE(novedades.IsOk());
    REQUIRE(novedades.Value().size() == 1);
    CHECK(novedades.Value()[0].text == L"Terminada la fase 2, falta probar con rutas largas");
}

TEST_CASE("un repositorio sin fila propia sale con los valores por omisión") {
    // 109 filas para decir "sin clasificar y activo" serían 109 filas para no decir nada.
    Fresh fresh;
    const auto mine = fresh.repos.LocalOf("R_nunca_visto");
    REQUIRE(mine.IsOk());
    CHECK(mine.Value().priority == Model::Priority::Unsorted);
    CHECK(mine.Value().state == Model::State::Active);
    CHECK(mine.Value().nextStep.empty());
}

// ------------------------------------------------------------- Aparecer y marchar --

TEST_CASE("un repositorio que desaparece se marca y no se borra") {
    Fresh fresh;

    const auto seq1 = fresh.repos.BeginSync();
    REQUIRE(seq1.IsOk());
    REQUIRE(fresh.repos
                .UpsertMetadata({MakeRepo("R_1", L"yo/uno", "2026-09-01T00:00:00Z"),
                                 MakeRepo("R_2", L"yo/dos", "2026-09-02T00:00:00Z")},
                                seq1.Value(), kNow)
                .IsOk());

    Model::Local local;
    local.repoId = "R_2";
    local.nextStep = L"No me borres";
    local.updatedAt = kNow;
    REQUIRE(fresh.repos.SaveLocal(local).IsOk());

    // Segunda pasada: R_2 ya no está en la cuenta.
    const auto seq2 = fresh.repos.BeginSync();
    REQUIRE(seq2.IsOk());
    REQUIRE(fresh.repos
                .UpsertMetadata({MakeRepo("R_1", L"yo/uno", "2026-09-01T00:00:00Z")},
                                seq2.Value(), kNow)
                .IsOk());
    const auto missing = fresh.repos.MarkMissing(seq2.Value(), kNow);
    REQUIRE(missing.IsOk());
    CHECK(missing.Value() == 1);

    // La fila sigue, marcada, y la nota con ella.
    const auto counts = fresh.repos.Counts();
    REQUIRE(counts.IsOk());
    CHECK(counts.Value().total == 2);
    CHECK(counts.Value().gone == 1);
    CHECK(fresh.repos.LocalOf("R_2").Value().nextStep == L"No me borres");
}

TEST_CASE("un repositorio que vuelve deja de estar marcado") {
    Fresh fresh;
    const auto seq1 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/uno", nullptr)}, seq1, kNow).IsOk());

    const auto seq2 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.MarkMissing(seq2, kNow).IsOk());
    CHECK(fresh.repos.Counts().Value().gone == 1);

    const auto seq3 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/uno", nullptr)}, seq3, kNow).IsOk());
    CHECK(fresh.repos.Counts().Value().gone == 0);
}

TEST_CASE("dos sincronizaciones en el mismo segundo se distinguen") {
    // Por esto la pasada se numera y no se sella con la hora: con un sello de tiempo, dos
    // sincronizaciones seguidas tendrían el mismo y la segunda daría por ausentes a los
    // repositorios que acababa de ver.
    Fresh fresh;
    const auto a = fresh.repos.BeginSync();
    const auto b = fresh.repos.BeginSync();
    REQUIRE(a.IsOk());
    REQUIRE(b.IsOk());
    CHECK(b.Value() > a.Value());
}

TEST_CASE("si GitHub cambia el identificador, la fila se reengancha por el nombre") {
    // GitHub avisa en extensions.warnings de que los identificadores globales viejos están
    // en retirada. El día que cambien, reconocer solo por id convertiría los 109 en 109
    // nuevos y dejaría las notas colgando — sin un solo error por ningún lado.
    Fresh fresh;

    const auto seq1 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos
                .UpsertMetadata({MakeRepo("MDEwOlJlcG9zaXRvcnkx", L"yo/uno", nullptr)}, seq1, kNow)
                .IsOk());

    Model::Local local;
    local.repoId = "MDEwOlJlcG9zaXRvcnkx";
    local.priority = Model::Priority::Focus;
    local.nextStep = L"Esto tiene que sobrevivir";
    local.updatedAt = kNow;
    REQUIRE(fresh.repos.SaveLocal(local).IsOk());

    // Misma cuenta, mismo repositorio, identificador nuevo.
    const auto seq2 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_kgAB", L"yo/uno", nullptr)}, seq2, kNow).IsOk());

    // Una sola fila, no dos.
    const auto counts = fresh.repos.Counts();
    REQUIRE(counts.IsOk());
    CHECK(counts.Value().total == 1);

    // Y la nota viajó con ella, que es lo que hace el ON UPDATE CASCADE.
    const auto mine = fresh.repos.LocalOf("R_kgAB");
    REQUIRE(mine.IsOk());
    CHECK(mine.Value().priority == Model::Priority::Focus);
    CHECK(mine.Value().nextStep == L"Esto tiene que sobrevivir");
}

TEST_CASE("renombrar un repositorio no crea otro") {
    Fresh fresh;
    const auto seq1 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/viejo", nullptr)}, seq1, kNow).IsOk());

    const auto seq2 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/nuevo", nullptr)}, seq2, kNow).IsOk());

    const auto all = fresh.repos.All();
    REQUIRE(all.IsOk());
    REQUIRE(all.Value().size() == 1);
    CHECK(all.Value()[0].nameWithOwner == L"yo/nuevo");
}

// ------------------------------------------------------------ El segundo pase --

TEST_CASE("el segundo pase solo pide lo que cambió") {
    Fresh fresh;

    const auto seq1 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos
                .UpsertMetadata({MakeRepo("R_1", L"yo/uno", "2026-09-01T00:00:00Z"),
                                 MakeRepo("R_2", L"yo/dos", "2026-09-02T00:00:00Z")},
                                seq1, kNow)
                .IsOk());

    // Recién sincronizados: los dos hacen falta.
    auto pending = fresh.repos.NeedingEnrichment();
    REQUIRE(pending.IsOk());
    CHECK(pending.Value().size() == 2);

    // Se enriquecen.
    for (const char* id : {"R_1", "R_2"}) {
        Model::Repo detail;
        detail.id = id;
        detail.enrichedPush = At(std::string(id) == "R_1" ? "2026-09-01T00:00:00Z"
                                                          : "2026-09-02T00:00:00Z");
        detail.defaultBranch = L"main";
        detail.commitTitle = L"Un commit";
        detail.openIssues = 3;
        REQUIRE(fresh.repos.ApplyEnrichment(detail).IsOk());
    }
    pending = fresh.repos.NeedingEnrichment();
    REQUIRE(pending.IsOk());
    CHECK(pending.Value().empty());

    // Otra sincronización sin novedades: sigue sin hacer falta nada. Esto es, literalmente,
    // el criterio de aceptación de lo incremental.
    const auto seq2 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos
                .UpsertMetadata({MakeRepo("R_1", L"yo/uno", "2026-09-01T00:00:00Z"),
                                 MakeRepo("R_2", L"yo/dos", "2026-09-02T00:00:00Z")},
                                seq2, kNow)
                .IsOk());
    pending = fresh.repos.NeedingEnrichment();
    REQUIRE(pending.IsOk());
    CHECK(pending.Value().empty());

    // Y ahora sí: a R_2 le entra un push.
    const auto seq3 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos
                .UpsertMetadata({MakeRepo("R_1", L"yo/uno", "2026-09-01T00:00:00Z"),
                                 MakeRepo("R_2", L"yo/dos", "2026-09-21T00:00:00Z")},
                                seq3, kNow)
                .IsOk());
    pending = fresh.repos.NeedingEnrichment();
    REQUIRE(pending.IsOk());
    REQUIRE(pending.Value().size() == 1);
    CHECK(pending.Value()[0] == "R_2");
}

TEST_CASE("un repositorio sin ningún push también se enriquece una vez") {
    // Sin la columna 'enriched', la comprobación sería solo "enriched_push distinto de
    // pushed_at", y con los dos a NULL eso es falso: un repositorio vacío no se pediría
    // nunca, y sus issues y sus PR abiertos no llegarían jamás.
    Fresh fresh;
    const auto seq = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/vacio", nullptr)}, seq, kNow).IsOk());

    auto pending = fresh.repos.NeedingEnrichment();
    REQUIRE(pending.IsOk());
    CHECK(pending.Value().size() == 1);

    Model::Repo detail;
    detail.id = "R_1";
    detail.openIssues = 1;
    REQUIRE(fresh.repos.ApplyEnrichment(detail).IsOk());

    pending = fresh.repos.NeedingEnrichment();
    REQUIRE(pending.IsOk());
    CHECK(pending.Value().empty());
}

TEST_CASE("el primer pase no borra el detalle del segundo") {
    // Si el upsert de metadatos tocara las columnas del segundo pase, cada sincronización
    // las dejaría en blanco y el segundo pase tendría que pedirlo TODO cada vez. No daría
    // error: solo tardaría diecisiete segundos en lugar de tres, para siempre.
    Fresh fresh;
    const auto seq1 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/uno", "2026-09-01T00:00:00Z")},
                                       seq1, kNow)
                .IsOk());

    Model::Repo detail;
    detail.id = "R_1";
    detail.enrichedPush = At("2026-09-01T00:00:00Z");
    detail.commitTitle = L"El commit de siempre";
    detail.proyectoText = L"---\nprioridad: enfoque\n---\n";
    detail.openPrs = 2;
    REQUIRE(fresh.repos.ApplyEnrichment(detail).IsOk());

    const auto seq2 = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/uno", "2026-09-01T00:00:00Z")},
                                       seq2, kNow)
                .IsOk());

    const auto all = fresh.repos.All();
    REQUIRE(all.IsOk());
    REQUIRE(all.Value().size() == 1);
    CHECK(all.Value()[0].commitTitle == L"El commit de siempre");
    CHECK(all.Value()[0].openPrs == 2);
    CHECK(all.Value()[0].enriched);
}

// ---------------------------------------------------------------------- El borde --

TEST_CASE("una nota con tildes vuelve igual") {
    // Ahora por nuestro código y no por el humo de deps_test.cpp: pasa por Stmt::Bind, por
    // SQLite y por Stmt::Wide, que es el camino de verdad.
    Fresh fresh;
    const auto seq = fresh.repos.BeginSync().Value();
    REQUIRE(fresh.repos.UpsertMetadata({MakeRepo("R_1", L"yo/uno", nullptr)}, seq, kNow).IsOk());

    Model::Novedad novedad;
    novedad.repoId = "R_1";
    novedad.day = "2026-09-20";
    novedad.text = L"Revisión del año: el niño de la eñe — 音楽 \U0001F600";
    novedad.createdAt = kNow;
    REQUIRE(fresh.repos.AddNovedad(novedad).IsOk());

    const auto back = fresh.repos.NovedadesOf("R_1");
    REQUIRE(back.IsOk());
    REQUIRE(back.Value().size() == 1);
    CHECK(back.Value()[0].text == novedad.text);
}

TEST_CASE("una descripción vacía se guarda como ausente, no como cadena vacía") {
    // 76 de los 109 no tienen descripción. Poder contar cuántas faltan es la diferencia
    // entre "no hay" y "hay, y está en blanco".
    Fresh fresh;
    const auto seq = fresh.repos.BeginSync().Value();

    Model::Repo withText = MakeRepo("R_1", L"yo/uno", nullptr);
    withText.description = L"Tiene descripción";
    const Model::Repo without = MakeRepo("R_2", L"yo/dos", nullptr);
    REQUIRE(fresh.repos.UpsertMetadata({withText, without}, seq, kNow).IsOk());

    auto prepared = fresh.db.Prepare("SELECT count(description) FROM repos");
    REQUIRE(prepared.IsOk());
    Store::Stmt stmt = prepared.Take();
    REQUIRE(stmt.Step().Value());
    CHECK(stmt.Int(0) == 1);
}

TEST_CASE("los ajustes van y vuelven, y lo que no existe sale vacío") {
    Fresh fresh;
    CHECK(fresh.repos.Setting("ultima_sync").Value().empty());
    REQUIRE(fresh.repos.SetSetting("ultima_sync", "2026-09-21T12:00:00Z").IsOk());
    CHECK(fresh.repos.Setting("ultima_sync").Value() == "2026-09-21T12:00:00Z");
    REQUIRE(fresh.repos.SetSetting("ultima_sync", "otra").IsOk());
    CHECK(fresh.repos.Setting("ultima_sync").Value() == "otra");
}
