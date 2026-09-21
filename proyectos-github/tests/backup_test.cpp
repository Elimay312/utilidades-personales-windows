// La copia de seguridad. Todo sobre :memory:, como store_test.cpp.
//
// Lo que se vigila aquí son tres cosas, y ninguna de las tres da un error cuando falla:
//
//   1. Que restaurar devuelva lo que había. Un campo que no se exporta no se echa de menos
//      hasta que hace falta, y entonces ya no está en ninguna parte.
//   2. Que importar NO borre ni duplique. Un archivo viejo que vacía lo escrito después es
//      la misma pérdida silenciosa que una sincronización pisando las notas.
//   3. Que un archivo no pueda encender el modo repo. Eso sería el interruptor global que la
//      regla 5 de SEGURIDAD.md dice que no existe, entrando por la puerta de atrás.

#include <doctest/doctest.h>

#include <string>

#include "store/Backup.h"
#include "store/Repos.h"
#include "store/Schema.h"

namespace {

Model::Instant At(const char* iso) { return Model::ParseIso8601(iso).value(); }

const Model::Instant kNow = At("2026-09-21T12:00:00Z");

struct Fresh {
    Store::Db db;
    Store::Repos repos{db};

    Fresh() {
        REQUIRE(db.Open(":memory:").IsOk());
        REQUIRE(Store::Migrate(db).IsOk());
    }

    void Add(const char* id, const wchar_t* nameWithOwner) {
        Model::Repo repo;
        repo.id = id;
        repo.name = L"repo";
        repo.owner = L"Elimay312";
        repo.nameWithOwner = nameWithOwner;
        repo.url = L"https://github.com/Elimay312/repo";
        repo.createdAt = kNow;
        repo.updatedAt = kNow;
        const auto seq = repos.BeginSync();
        REQUIRE(seq.IsOk());
        REQUIRE(repos.UpsertMetadata({repo}, seq.Value(), kNow).IsOk());
    }

    void Note(const char* id, const char* day, const wchar_t* text) {
        Model::Novedad novedad;
        novedad.repoId = id;
        novedad.day = day;
        novedad.text = text;
        novedad.createdAt = kNow;
        REQUIRE(repos.AddNovedad(novedad).IsOk());
    }
};

}  // namespace

TEST_CASE("exportar e importar devuelve lo que había") {
    Fresh origen;
    origen.Add("R_1", L"Elimay312/brujula");

    Model::Local local;
    local.repoId = "R_1";
    local.priority = Model::Priority::Focus;
    local.state = Model::State::Blocked;
    local.nextStep = L"Conectar el lector de carpetas — año, ñ, 音楽";
    local.folder = L"C:\\Proyectos\\brujula";
    local.updatedAt = kNow;
    REQUIRE(origen.repos.SaveLocal(local).IsOk());
    origen.Note("R_1", "2026-09-20", L"Terminada la fase 2");

    const auto archivo = Store::ExportJson(origen.db, kNow);
    REQUIRE(archivo.IsOk());

    // Otra caché, ya sincronizada: los repositorios vuelven de GitHub, las notas del archivo.
    Fresh destino;
    destino.Add("R_1", L"Elimay312/brujula");

    const auto informe = Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Merge);
    REQUIRE(informe.IsOk());
    CHECK(informe.Value().matched == 1);
    CHECK(informe.Value().skipped == 0);
    CHECK(informe.Value().novedades == 1);

    const auto back = destino.repos.LocalOf("R_1");
    REQUIRE(back.IsOk());
    CHECK(back.Value().priority == Model::Priority::Focus);
    CHECK(back.Value().state == Model::State::Blocked);
    CHECK(back.Value().nextStep == L"Conectar el lector de carpetas — año, ñ, 音楽");
    CHECK(back.Value().folder == L"C:\\Proyectos\\brujula");

    const auto notes = destino.repos.NovedadesOf("R_1");
    REQUIRE(notes.IsOk());
    REQUIRE(notes.Value().size() == 1);
    CHECK(notes.Value()[0].text == L"Terminada la fase 2");
    CHECK(notes.Value()[0].day == "2026-09-20");
}

TEST_CASE("el archivo no lleva los repositorios, solo lo del usuario") {
    // Son nombres y descripciones de repositorios privados de trabajo. Meterlos en un
    // archivo que acaba en una carpeta compartida es justo lo que no puede pasar.
    Fresh fresh;
    fresh.Add("R_1", L"Elimay312/brujula");

    Model::Local local;
    local.repoId = "R_1";
    local.nextStep = L"Algo";
    local.updatedAt = kNow;
    REQUIRE(fresh.repos.SaveLocal(local).IsOk());

    const auto archivo = Store::ExportJson(fresh.db, kNow);
    REQUIRE(archivo.IsOk());
    // El nombre completo sí va: es la clave con la que se vuelve a enganchar. La URL, la
    // descripción y los mensajes de commit, no.
    CHECK(archivo.Value().find("Elimay312/brujula") != std::string::npos);
    CHECK(archivo.Value().find("github.com") == std::string::npos);
    CHECK(archivo.Value().find("commit") == std::string::npos);
}

TEST_CASE("una copia no puede encender el modo repo") {
    Fresh origen;
    origen.Add("R_1", L"Elimay312/brujula");

    Model::Local local;
    local.repoId = "R_1";
    local.repoMode = true;
    local.repoConfirmed = true;
    local.nextStep = L"Con el modo repo puesto";
    local.updatedAt = kNow;
    REQUIRE(origen.repos.SaveLocal(local).IsOk());

    const auto archivo = Store::ExportJson(origen.db, kNow);
    REQUIRE(archivo.IsOk());
    // Se exporta, para que la copia diga la verdad de cómo estaba la cosa…
    CHECK(archivo.Value().find("modo_repo") != std::string::npos);

    Fresh destino;
    destino.Add("R_1", L"Elimay312/brujula");
    REQUIRE(Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Replace).IsOk());

    // …y al restaurar hay que volver a confirmar, repositorio por repositorio.
    const auto back = destino.repos.LocalOf("R_1");
    REQUIRE(back.IsOk());
    CHECK(back.Value().nextStep == L"Con el modo repo puesto");
    CHECK_FALSE(back.Value().repoMode);
    CHECK_FALSE(back.Value().repoConfirmed);
}

TEST_CASE("importar dos veces no duplica novedades") {
    Fresh origen;
    origen.Add("R_1", L"yo/uno");
    origen.Note("R_1", "2026-09-20", L"La misma nota");

    const auto archivo = Store::ExportJson(origen.db, kNow);
    REQUIRE(archivo.IsOk());

    Fresh destino;
    destino.Add("R_1", L"yo/uno");
    REQUIRE(Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Merge).IsOk());
    const auto segunda =
        Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Merge);
    REQUIRE(segunda.IsOk());
    CHECK(segunda.Value().novedades == 0);
    CHECK(destino.repos.NovedadesOf("R_1").Value().size() == 1);
}

TEST_CASE("fusionando gana lo más nuevo, y reemplazando manda el archivo") {
    Fresh origen;
    origen.Add("R_1", L"yo/uno");
    Model::Local viejo;
    viejo.repoId = "R_1";
    viejo.nextStep = L"Lo del archivo, que es de ayer";
    viejo.updatedAt = At("2026-09-20T00:00:00Z");
    REQUIRE(origen.repos.SaveLocal(viejo).IsOk());

    const auto archivo = Store::ExportJson(origen.db, kNow);
    REQUIRE(archivo.IsOk());

    Fresh destino;
    destino.Add("R_1", L"yo/uno");
    Model::Local nuevo;
    nuevo.repoId = "R_1";
    nuevo.nextStep = L"Lo de aquí, que es de hoy";
    nuevo.updatedAt = kNow;
    REQUIRE(destino.repos.SaveLocal(nuevo).IsOk());

    const auto fusion = Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Merge);
    REQUIRE(fusion.IsOk());
    CHECK(fusion.Value().kept == 1);
    CHECK(destino.repos.LocalOf("R_1").Value().nextStep == L"Lo de aquí, que es de hoy");

    REQUIRE(Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Replace).IsOk());
    CHECK(destino.repos.LocalOf("R_1").Value().nextStep == L"Lo del archivo, que es de ayer");
}

TEST_CASE("se empareja por nombre aunque el identificador haya cambiado") {
    // Una copia hecha antes de que GitHub renumerara sus identificadores globales tiene que
    // poder restaurarse después. El nombre completo es lo único que sobrevive a eso.
    Fresh origen;
    origen.Add("MDEw_viejo", L"yo/uno");
    Model::Local local;
    local.repoId = "MDEw_viejo";
    local.nextStep = L"Tiene que llegar";
    local.updatedAt = kNow;
    REQUIRE(origen.repos.SaveLocal(local).IsOk());

    const auto archivo = Store::ExportJson(origen.db, kNow);
    REQUIRE(archivo.IsOk());

    Fresh destino;
    destino.Add("R_kgAB", L"yo/uno");
    const auto informe = Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Merge);
    REQUIRE(informe.IsOk());
    CHECK(informe.Value().matched == 1);
    CHECK(destino.repos.LocalOf("R_kgAB").Value().nextStep == L"Tiene que llegar");
}

TEST_CASE("una fila sin repositorio en esta caché se cuenta y no se pierde el resto") {
    Fresh origen;
    origen.Add("R_1", L"yo/uno");
    origen.Add("R_2", L"yo/dos");
    for (const char* id : {"R_1", "R_2"}) {
        Model::Local local;
        local.repoId = id;
        local.nextStep = L"Algo";
        local.updatedAt = kNow;
        REQUIRE(origen.repos.SaveLocal(local).IsOk());
    }

    const auto archivo = Store::ExportJson(origen.db, kNow);
    REQUIRE(archivo.IsOk());

    Fresh destino;
    destino.Add("R_1", L"yo/uno");  // el segundo todavía no se ha sincronizado

    const auto informe = Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Merge);
    REQUIRE(informe.IsOk());
    CHECK(informe.Value().matched == 1);
    CHECK(informe.Value().skipped == 1);
    CHECK(destino.repos.LocalOf("R_1").Value().nextStep == L"Algo");
}

TEST_CASE("las novedades de un repositorio sin fila propia también se exportan") {
    // Se escribe una nota y no se toca la prioridad: no hay fila en 'local' y la nota
    // existe. Sin la segunda vuelta del exportador, esa nota no saldría en la copia.
    Fresh origen;
    origen.Add("R_1", L"yo/uno");
    origen.Note("R_1", "2026-09-20", L"Sin tocar la prioridad");

    const auto archivo = Store::ExportJson(origen.db, kNow);
    REQUIRE(archivo.IsOk());

    Fresh destino;
    destino.Add("R_1", L"yo/uno");
    const auto informe = Store::ImportJson(destino.db, archivo.Value(), Store::ImportMode::Merge);
    REQUIRE(informe.IsOk());
    CHECK(informe.Value().novedades == 1);
}

TEST_CASE("un archivo que no es una copia se rechaza sin tocar nada") {
    Fresh fresh;
    fresh.Add("R_1", L"yo/uno");
    Model::Local local;
    local.repoId = "R_1";
    local.nextStep = L"No me toques";
    local.updatedAt = kNow;
    REQUIRE(fresh.repos.SaveLocal(local).IsOk());

    for (const char* basura : {"", "{", "no soy json", "{\"otra\": 1}", "[]"}) {
        const auto informe = Store::ImportJson(fresh.db, basura, Store::ImportMode::Replace);
        CHECK_FALSE(informe.IsOk());
    }
    CHECK(fresh.repos.LocalOf("R_1").Value().nextStep == L"No me toques");
}

TEST_CASE("una copia de una versión más nueva no se lee a medias") {
    Fresh fresh;
    const auto informe = Store::ImportJson(
        fresh.db, "{\"brujula\": 99, \"repositorios\": []}", Store::ImportMode::Merge);
    CHECK_FALSE(informe.IsOk());
    CHECK_FALSE(informe.Err().detail.empty());
}
