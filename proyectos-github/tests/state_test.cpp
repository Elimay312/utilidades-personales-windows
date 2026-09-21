// El estado de la vista principal: quién entra en cada vista, en qué orden salen, qué
// encuentra la búsqueda y cómo se escribe "hace X días".
//
// Las cuatro se equivocan en silencio. Un repositorio en la vista que no le toca parece un
// repositorio bien colocado; un orden mal hecho parece un orden; una búsqueda que no
// encuentra parece que no hay nada; y un "hace 1 días" lleva meses en pantalla antes de que
// alguien lo vea.

#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include "app/State.h"

namespace {

// Sin REQUIRE: esto corre en la inicialización estática, antes de que doctest tenga un
// contexto donde apuntar un fallo, y una aserción ahí no es un test rojo — es la prueba
// entera cayéndose sin escribir una línea.
Model::Instant At(const char* iso) {
    return Model::ParseIso8601(iso).value_or(Model::Instant{});
}

const Model::Instant kNow = At("2026-09-21T12:00:00Z");

Model::Repo MakeRepo(std::string id, std::wstring name, const char* pushed) {
    Model::Repo repo;
    repo.id = std::move(id);
    repo.name = name;
    repo.owner = L"elimay";
    repo.nameWithOwner = L"elimay/" + name;
    if (pushed != nullptr) repo.pushedAt = At(pushed);
    return repo;
}

App::Entry MakeEntry(Model::Repo repo, Model::Priority priority = Model::Priority::Unsorted) {
    App::Entry entry;
    entry.repo = std::move(repo);
    entry.local.repoId = entry.repo.id;
    entry.local.priority = priority;
    App::Derive(entry, kNow, Model::Thresholds{});
    return entry;
}

}  // namespace

TEST_CASE("las fechas de esta prueba se leen de verdad") {
    // At() no puede usar REQUIRE porque corre antes que doctest, así que lo que comprueba
    // que no está devolviendo la época en silencio es esto.
    CHECK(Model::ToEpoch(kNow) > 0);
    CHECK(Model::FormatIso8601(kNow) == "2026-09-21T12:00:00Z");
}

TEST_CASE("Fold quita tildes y baja a minúsculas") {
    CHECK(App::Fold(L"Revisión") == L"revision");
    CHECK(App::Fold(L"AÑO") == L"ano");
    CHECK(App::Fold(L"Niño Ángel Über") == L"nino angel uber");
    // Lo que no es una letra acentuada se queda como está. El signo de multiplicar vive en
    // medio de la tabla de Latin-1 y convertirlo en una letra sería un error mudo.
    CHECK(App::Fold(L"3 × 4") == L"3 × 4");
    CHECK(App::Fold(L"brujula-4") == L"brujula-4");
}

TEST_CASE("La búsqueda pide todas las palabras, en cualquier orden") {
    Model::Repo repo = MakeRepo("R1", L"Brújula", "2026-09-20T10:00:00Z");
    repo.description = L"Priorizador de repositorios";
    repo.language = L"C++";
    App::Entry entry = MakeEntry(std::move(repo));
    entry.local.nextStep = L"Terminar la fase cuatro";
    App::Derive(entry, kNow, Model::Thresholds{});

    CHECK(App::Matches(entry, App::Terms(L"brujula")));
    // Sin tildes por los dos lados: escrito con tilde encuentra lo que no la lleva y al revés.
    CHECK(App::Matches(entry, App::Terms(L"BRÚJULA")));
    CHECK(App::Matches(entry, App::Terms(L"fase cuatro")));
    // Palabras de campos distintos: el nombre y el siguiente paso.
    CHECK(App::Matches(entry, App::Terms(L"brujula fase")));
    CHECK(App::Matches(entry, App::Terms(L"elimay")));
    CHECK_FALSE(App::Matches(entry, App::Terms(L"brujula fase cinco")));
    // Una consulta vacía no filtra nada.
    CHECK(App::Terms(L"   ").empty());
}

TEST_CASE("Cada vista deja entrar a los suyos") {
    const App::Entry focus =
        MakeEntry(MakeRepo("R1", L"uno", "2026-09-20T10:00:00Z"), Model::Priority::Focus);
    const App::Entry dormant =
        MakeEntry(MakeRepo("R2", L"dos", "2025-01-01T10:00:00Z"), Model::Priority::Secondary);
    const App::Entry never = MakeEntry(MakeRepo("R3", L"tres", nullptr));

    CHECK(App::InLens(focus, App::Lens::Focus));
    CHECK_FALSE(App::InLens(focus, App::Lens::Secondary));
    CHECK(App::InLens(focus, App::Lens::ThisWeek));
    CHECK(App::InLens(focus, App::Lens::All));

    CHECK(App::InLens(dormant, App::Lens::Dormant));
    CHECK_FALSE(App::InLens(dormant, App::Lens::ThisWeek));

    // Sin un solo push sale dormido, que es lo que ya decía Model::Classify, y sin
    // clasificar, que es el valor por omisión de un repositorio recién visto.
    CHECK(App::InLens(never, App::Lens::Dormant));
    CHECK(App::InLens(never, App::Lens::Unsorted));

    // Enfoque parado más de dos semanas es un desajuste.
    const App::Entry stale =
        MakeEntry(MakeRepo("R4", L"cuatro", "2026-08-01T10:00:00Z"), Model::Priority::Focus);
    CHECK(App::InLens(stale, App::Lens::NeedsDecision));
    CHECK_FALSE(App::InLens(focus, App::Lens::NeedsDecision));
}

TEST_CASE("Un repositorio que ya no está en la cuenta solo sale en Todos") {
    Model::Repo repo = MakeRepo("R9", L"fantasma", "2026-09-20T10:00:00Z");
    repo.goneAt = kNow;
    const App::Entry gone = MakeEntry(std::move(repo), Model::Priority::Focus);

    CHECK(App::InLens(gone, App::Lens::All));
    // Ni en su grupo de prioridad ni en las vistas que existen para decidir: no se puede
    // decidir sobre algo que ya no está, y esconderlo del todo perdería sus notas de vista.
    CHECK_FALSE(App::InLens(gone, App::Lens::Focus));
    CHECK_FALSE(App::InLens(gone, App::Lens::ThisWeek));
    CHECK_FALSE(App::InLens(gone, App::Lens::NeedsDecision));
}

TEST_CASE("El orden es el último push primero y los que no tienen, al final") {
    std::vector<Model::Repo> repos;
    repos.push_back(MakeRepo("R1", L"viejo", "2025-03-01T10:00:00Z"));
    repos.push_back(MakeRepo("R2", L"sinpush", nullptr));
    repos.push_back(MakeRepo("R3", L"nuevo", "2026-09-20T10:00:00Z"));

    App::State state;
    state.Load(std::move(repos), {}, kNow);

    REQUIRE(state.VisibleCount() == 3);
    CHECK(state.At(0)->repo.id == "R3");
    CHECK(state.At(1)->repo.id == "R1");
    CHECK(state.At(2)->repo.id == "R2");
}

TEST_CASE("Los contadores cuentan la vista entera, no el filtro") {
    std::vector<Model::Repo> repos;
    repos.push_back(MakeRepo("R1", L"brujula", "2026-09-20T10:00:00Z"));
    repos.push_back(MakeRepo("R2", L"isla", "2026-09-19T10:00:00Z"));
    repos.push_back(MakeRepo("R3", L"rayo", "2024-01-01T10:00:00Z"));

    std::vector<Model::Local> locals;
    Model::Local local;
    local.repoId = "R1";
    local.priority = Model::Priority::Focus;
    local.nextStep = L"Terminar la fase cuatro";
    locals.push_back(local);

    App::State state;
    state.Load(std::move(repos), locals, kNow);

    CHECK(state.CountOf(App::Lens::All) == 3);
    CHECK(state.CountOf(App::Lens::Focus) == 1);
    CHECK(state.CountOf(App::Lens::Unsorted) == 2);
    CHECK(state.CountOf(App::Lens::ThisWeek) == 2);
    CHECK(state.CountOf(App::Lens::Dormant) == 1);

    state.SetQuery(L"isla");
    CHECK(state.VisibleCount() == 1);
    // El contador NO baja: es lo que permite saber que lo que buscas está en otro grupo.
    CHECK(state.CountOf(App::Lens::All) == 3);

    // Y la búsqueda sigue viva al cambiar de vista, pero se aplica dentro de ella.
    state.SetLens(App::Lens::Focus);
    CHECK(state.VisibleCount() == 0);
    state.SetQuery(L"");
    CHECK(state.VisibleCount() == 1);
    CHECK(state.At(0)->repo.id == "R1");
}

TEST_CASE("Las claves siguen al repositorio aunque cambie de sitio") {
    std::vector<Model::Repo> repos;
    repos.push_back(MakeRepo("R1", L"uno", "2026-09-10T10:00:00Z"));
    repos.push_back(MakeRepo("R2", L"dos", "2026-09-20T10:00:00Z"));

    App::State state;
    state.Load(std::move(repos), {}, kNow);
    const std::vector<std::uint64_t> before = state.Keys();
    REQUIRE(before.size() == 2);
    CHECK(state.At(0)->repo.id == "R2");

    // Llega un push a R1 y se pone el primero. La clave del que se mueve tiene que ser la
    // misma de antes, o la lista lo repintaría en su sitio nuevo en vez de deslizarlo.
    std::vector<Model::Repo> again;
    again.push_back(MakeRepo("R1", L"uno", "2026-09-21T09:00:00Z"));
    again.push_back(MakeRepo("R2", L"dos", "2026-09-20T10:00:00Z"));
    state.Load(std::move(again), {}, kNow);

    const std::vector<std::uint64_t> after = state.Keys();
    REQUIRE(after.size() == 2);
    CHECK(state.At(0)->repo.id == "R1");
    CHECK(after[0] == before[1]);
    CHECK(after[1] == before[0]);
}

TEST_CASE("Las lentes van y vuelven de su nombre guardado") {
    for (const App::Lens lens : App::kPriorityLenses) {
        CHECK(App::LensFromSlug(App::SlugOf(lens), App::Lens::All) == lens);
        CHECK(*App::NameOf(lens) != L'\0');
    }
    for (const App::Lens lens : App::kSmartLenses) {
        CHECK(App::LensFromSlug(App::SlugOf(lens), App::Lens::All) == lens);
        CHECK(*App::NameOf(lens) != L'\0');
    }
    // Un ajuste editado a mano, o de una versión que ya no existe, no rompe nada.
    CHECK(App::LensFromSlug("lo-que-sea", App::Lens::Focus) == App::Lens::Focus);
    CHECK(App::LensFromSlug("", App::Lens::Focus) == App::Lens::Focus);
}

TEST_CASE("Hace X días se escribe en singular cuando toca") {
    CHECK(App::AgoDays(0) == L"hoy");
    // Negativo es el reloj del equipo atrasado respecto al del servidor, y pasa de verdad.
    CHECK(App::AgoDays(-1) == L"hoy");
    CHECK(App::AgoDays(1) == L"ayer");
    CHECK(App::AgoDays(2) == L"hace 2 días");
    CHECK(App::AgoDays(29) == L"hace 29 días");
    CHECK(App::AgoDays(30) == L"hace 1 mes");
    CHECK(App::AgoDays(364) == L"hace 12 meses");
    CHECK(App::AgoDays(365) == L"hace 1 año");
    CHECK(App::AgoDays(800) == L"hace 2 años");

    CHECK(App::AgoSeconds(5) == L"hace un momento");
    CHECK(App::AgoSeconds(60) == L"hace 1 minuto");
    CHECK(App::AgoSeconds(3600) == L"hace 1 hora");
    CHECK(App::AgoSeconds(86400) == L"ayer");
}

TEST_CASE("filtrar es instantáneo con muchos más de los 120 de la cuenta") {
    // El criterio de aceptación de la fase 4 dice "instantáneo", y eso no es una impresión:
    // es que escribir una letra no pueda perder un fotograma. La cota de aquí abajo es
    // generosísima a propósito —lo medido son décimas de milisegundo— porque lo que tiene
    // que cazar no es una décima de más: es el día que alguien vuelva a plegar las 500
    // cadenas dentro del filtro y esto pase a ser cuadrático.
    std::vector<Model::Repo> repos;
    repos.reserve(500);
    for (int i = 0; i < 500; ++i) {
        Model::Repo repo =
            MakeRepo("R" + std::to_string(i), L"proyecto-" + std::to_wstring(i),
                     "2026-09-01T10:00:00Z");
        repo.description = L"Una descripción de ejemplo con unas cuantas palabras dentro";
        repo.language = L"C++";
        repos.push_back(std::move(repo));
    }

    App::State state;
    state.Load(std::move(repos), {}, kNow);
    REQUIRE(state.VisibleCount() == 500);

    // El ÚLTIMO, y no uno cualquiera: "proyecto-42" es prefijo de proyecto-420 a
    // proyecto-429 y encontraría once. El 499 no es prefijo de ninguno.
    const std::wstring typed = L"proyecto-499";
    const auto started = std::chrono::steady_clock::now();
    // Una pasada por cada letra que se escribe, y cien veces: lo que se mide es escribir
    // el nombre entero cien veces seguidas.
    for (int round = 0; round < 100; ++round) {
        for (std::size_t n = 1; n <= typed.size(); ++n) {
            state.SetQuery(typed.substr(0, n));
        }
        state.SetQuery(std::wstring());
    }
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();

    state.SetQuery(typed);
    CHECK(state.VisibleCount() == 1);

    const double perKeystroke = ms / (100.0 * (static_cast<double>(typed.size()) + 1.0));
    MESSAGE("filtrar 500: " << perKeystroke << " ms por pulsación");
    // Un fotograma a 60 Hz son 16,6 ms. Con 500 elementos —cuatro veces la cuenta real—
    // una pulsación tiene que caber muchas veces dentro de uno.
    CHECK(perKeystroke < 2.0);
}

// ---------------------------------------------- Editar uno sin releer los otros ciento ocho --

TEST_CASE("cambiar la prioridad de uno lo mueve de grupo y recuenta") {
    // Es lo que hace el inspector con cada pulsación. Releer los 109 de SQLite también
    // funcionaría; lo que no puede pasar es que el contador de la barra lateral y la lista
    // digan cosas distintas, porque ninguna de las dos da un error cuando se separan.
    App::State state;
    state.Load({MakeRepo("R_1", L"yo/uno", "2026-09-20T00:00:00Z"),
                MakeRepo("R_2", L"yo/dos", "2026-09-19T00:00:00Z")},
               {}, kNow);

    CHECK(state.CountOf(App::Lens::Unsorted) == 2);
    CHECK(state.CountOf(App::Lens::Focus) == 0);

    Model::Local local;
    local.repoId = "R_1";
    local.priority = Model::Priority::Focus;
    local.nextStep = L"Medir el pase 2";
    REQUIRE(state.ApplyLocal(local, kNow));

    CHECK(state.CountOf(App::Lens::Focus) == 1);
    CHECK(state.CountOf(App::Lens::Unsorted) == 1);

    state.SetLens(App::Lens::Focus);
    REQUIRE(state.VisibleCount() == 1);
    CHECK(state.At(0)->repo.id == "R_1");
    CHECK(state.At(0)->local.nextStep == L"Medir el pase 2");
}

TEST_CASE("el siguiente paso entra en lo que mira la búsqueda") {
    // El pajar se calcula al cargar, así que si ApplyLocal no lo rehiciera, buscar por lo que
    // uno acaba de escribir no encontraría nada — y una lista vacía se parece muchísimo a una
    // lista correcta.
    App::State state;
    state.Load({MakeRepo("R_1", L"yo/uno", "2026-09-20T00:00:00Z")}, {}, kNow);

    state.SetQuery(L"carpetas");
    CHECK(state.VisibleCount() == 0);

    Model::Local local;
    local.repoId = "R_1";
    local.nextStep = L"Conectar el lector de CARPETAS";
    REQUIRE(state.ApplyLocal(local, kNow));
    CHECK(state.VisibleCount() == 1);
}

TEST_CASE("editar uno que no está cargado no hace nada y lo dice") {
    App::State state;
    state.Load({MakeRepo("R_1", L"yo/uno", nullptr)}, {}, kNow);

    Model::Local local;
    local.repoId = "R_inventado";
    CHECK_FALSE(state.ApplyLocal(local, kNow));
    CHECK(state.Entries().size() == 1);
}

TEST_CASE("el identificador de la fila local es siempre el del repositorio") {
    // Una fila recién creada puede llegar con el identificador vacío. Si se copiara tal cual,
    // ese repositorio no volvería a encontrarse nunca — y sin dar ningún error.
    App::State state;
    state.Load({MakeRepo("R_1", L"yo/uno", nullptr)}, {}, kNow);

    Model::Local local;
    local.repoId = "R_1";
    local.state = Model::State::Blocked;
    REQUIRE(state.ApplyLocal(local, kNow));

    const App::Entry* entry = state.EntryOf("R_1");
    REQUIRE(entry != nullptr);
    CHECK(entry->local.repoId == "R_1");
    CHECK(entry->local.state == Model::State::Blocked);
}

TEST_CASE("un repositorio en Enfoque y parado sale en Necesita decisión al cambiarlo") {
    // El desajuste se deriva, no se guarda. Sin volver a derivarlo en ApplyLocal, subir a
    // Enfoque un repositorio dormido no lo metería en la vista que existe justo para eso.
    App::State state;
    state.Load({MakeRepo("R_1", L"yo/dormido", "2026-01-01T00:00:00Z")}, {}, kNow);
    CHECK(state.CountOf(App::Lens::NeedsDecision) == 0);

    Model::Local local;
    local.repoId = "R_1";
    local.priority = Model::Priority::Focus;
    REQUIRE(state.ApplyLocal(local, kNow));
    CHECK(state.CountOf(App::Lens::NeedsDecision) == 1);
}
