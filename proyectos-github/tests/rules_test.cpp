// Las tres reglas de dominio. Las tres se equivocan sin hacer ruido: un repositorio en el
// grupo que no toca parece un repositorio bien colocado, un límite de Enfoque que se puede
// saltar no avisa de que se saltó, y un desajuste que no se detecta es exactamente nada en
// pantalla. Por eso el borde de cada una está escrito aquí y no solo en CLAUDE.md.

#include <doctest/doctest.h>

#include "model/Rules.h"

#include <string>
#include <utility>
#include <vector>

using Model::Activity;
using Model::Classify;
using Model::FocusEntry;
using Model::Instant;
using Model::Local;
using Model::Mismatch;
using Model::PlanFocus;
using Model::Priority;
using Model::Repo;
using Model::Review;
using Model::State;
using Model::Thresholds;

namespace {

// value() y no REQUIRE: kNow se construye antes de que exista ningún caso de prueba, y un
// REQUIRE fuera de contexto no informa de nada. Si alguna de estas cadenas estuviera mal,
// value() lanza y el fallo sale con su nombre.
Instant At(const char* iso) { return Model::ParseIso8601(iso).value(); }

// El "ahora" de todas las pruebas. Fijo, porque una regla que dependa del reloj de verdad
// pasaría hoy y fallaría dentro de tres meses sin que nadie tocara nada.
const Instant kNow = At("2026-09-21T12:00:00Z");

Instant DaysAgo(int days) {
    return Model::FromEpoch(Model::ToEpoch(kNow) - static_cast<long long>(days) * 86400);
}

}  // namespace

// ------------------------------------------------------------------- Clasificación --

TEST_CASE("los bordes de catorce y noventa días están donde dice CLAUDE.md") {
    const Thresholds limits;

    // "activo si hubo push en los últimos 14 días": el día 14 ya NO está dentro.
    CHECK(Classify(DaysAgo(0), kNow, limits) == Activity::Active);
    CHECK(Classify(DaysAgo(13), kNow, limits) == Activity::Active);
    CHECK(Classify(DaysAgo(14), kNow, limits) == Activity::Paused);

    // "en pausa entre 14 y 90 días; dormido más de 90": el 90 todavía es pausa.
    CHECK(Classify(DaysAgo(89), kNow, limits) == Activity::Paused);
    CHECK(Classify(DaysAgo(90), kNow, limits) == Activity::Paused);
    CHECK(Classify(DaysAgo(91), kNow, limits) == Activity::Dormant);
}

TEST_CASE("un repositorio sin un solo push sale dormido") {
    // Y no activo: esconderlo en el grupo de los vivos sería esconderlo de la única vista
    // que existe para encontrarlo.
    CHECK(Classify(std::nullopt, kNow, Thresholds{}) == Activity::Dormant);
}

TEST_CASE("un push con fecha en el futuro no sale dormido") {
    // Pasa de verdad: el reloj del equipo va unos segundos por detrás del servidor y un push
    // de hace un minuto llega con fecha posterior a la de ahora. La resta da negativo, y si
    // alguien la pasara por un tipo sin signo por el camino, ese número enorme mandaría el
    // repositorio más activo de todos al grupo de los dormidos.
    const Instant future = Model::FromEpoch(Model::ToEpoch(kNow) + 3600);
    CHECK(Classify(future, kNow, Thresholds{}) == Activity::Active);
    CHECK(Classify(DaysAgo(-40), kNow, Thresholds{}) == Activity::Active);
}

TEST_CASE("los umbrales se pueden cambiar y la regla los obedece") {
    Thresholds tight;
    tight.activeDays = 3;
    tight.dormantDays = 10;

    CHECK(Classify(DaysAgo(2), kNow, tight) == Activity::Active);
    CHECK(Classify(DaysAgo(3), kNow, tight) == Activity::Paused);
    CHECK(Classify(DaysAgo(10), kNow, tight) == Activity::Paused);
    CHECK(Classify(DaysAgo(11), kNow, tight) == Activity::Dormant);
}

// ---------------------------------------------------------------- Límite de Enfoque --

namespace {

std::vector<FocusEntry> Focus(std::initializer_list<std::pair<const char*, int>> entries) {
    std::vector<FocusEntry> out;
    for (const auto& [id, daysAgo] : entries) {
        FocusEntry entry;
        entry.id = id;
        entry.pushedAt = DaysAgo(daysAgo);
        out.push_back(entry);
    }
    return out;
}

}  // namespace

TEST_CASE("hasta el quinto cabe y no hay que bajar a nadie") {
    const auto four = Focus({{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}});
    const auto plan = PlanFocus(four, "e", kNow, Thresholds{});
    CHECK(plan.fits);
    CHECK(plan.demote.empty());
}

TEST_CASE("el sexto no cabe y la regla dice a quién se puede bajar") {
    // CLAUDE.md: "no se puede saltar". La prueba de que no se puede es que la única salida
    // que da la función cuando está lleno es una lista de a quién bajar.
    const auto five = Focus({{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}, {"e", 5}});
    const auto plan = PlanFocus(five, "f", kNow, Thresholds{});
    CHECK_FALSE(plan.fits);
    CHECK(plan.demote.size() == 5);
}

TEST_CASE("el que más tiempo lleva parado se propone primero") {
    const auto five = Focus({{"ayer", 1}, {"viejo", 200}, {"medio", 30}, {"hoy", 0}, {"otro", 5}});
    const auto plan = PlanFocus(five, "nuevo", kNow, Thresholds{});
    REQUIRE_FALSE(plan.fits);
    REQUIRE(plan.demote.size() == 5);
    CHECK(plan.demote.front() == "viejo");
    CHECK(plan.demote.back() == "hoy");
}

TEST_CASE("el que no tiene ningún push se propone antes que el más viejo") {
    std::vector<FocusEntry> entries = Focus({{"viejo", 500}, {"reciente", 1}});
    FocusEntry never;
    never.id = "nunca";
    entries.push_back(never);

    Thresholds limits;
    limits.focusLimit = 3;
    const auto plan = PlanFocus(entries, "nuevo", kNow, limits);
    REQUIRE_FALSE(plan.fits);
    CHECK(plan.demote.front() == "nunca");
}

TEST_CASE("un repositorio que ya está en Enfoque no consume hueco") {
    // Sin esto, volver a pulsar "1" sobre algo que ya está en Enfoque abriría la pregunta de
    // a quién bajar para hacerle sitio al que ya estaba dentro.
    const auto five = Focus({{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}, {"e", 5}});
    const auto plan = PlanFocus(five, "c", kNow, Thresholds{});
    CHECK(plan.fits);
    CHECK(plan.demote.empty());
}

TEST_CASE("el límite es configurable, también hacia abajo") {
    Thresholds one;
    one.focusLimit = 1;

    CHECK(PlanFocus({}, "a", kNow, one).fits);
    CHECK_FALSE(PlanFocus(Focus({{"a", 1}}), "b", kNow, one).fits);

    // Con el límite en cero no cabe nadie, ni siquiera con Enfoque vacío. Es una
    // configuración absurda, pero tiene que dar "no cabe" y no dividir por cero ni colarse.
    Thresholds zero;
    zero.focusLimit = 0;
    CHECK_FALSE(PlanFocus({}, "a", kNow, zero).fits);
}

// -------------------------------------------------------------- Necesita decisión --

namespace {

Repo RepoPushed(int daysAgo) {
    Repo repo;
    repo.id = "R_1";
    repo.pushedAt = DaysAgo(daysAgo);
    return repo;
}

Local With(Priority priority) {
    Local local;
    local.repoId = "R_1";
    local.priority = priority;
    return local;
}

}  // namespace

TEST_CASE("un repositorio en Enfoque y parado necesita decisión") {
    const Thresholds limits;
    CHECK(Review(RepoPushed(10), With(Priority::Focus), kNow, limits) == Mismatch::None);
    // El día 14 justo todavía no: "más de 14 días".
    CHECK(Review(RepoPushed(14), With(Priority::Focus), kNow, limits) == Mismatch::None);
    CHECK(Review(RepoPushed(15), With(Priority::Focus), kNow, limits) == Mismatch::FocusDormant);

    // Y en Enfoque sin un solo push también, que si no se escaparía por el hueco del vacío.
    Repo never;
    never.id = "R_1";
    CHECK(Review(never, With(Priority::Focus), kNow, limits) == Mismatch::FocusDormant);
}

TEST_CASE("un repositorio archivado con pushes recientes necesita decisión") {
    Repo repo = RepoPushed(2);
    repo.isArchived = true;
    CHECK(Review(repo, With(Priority::Unsorted), kNow, Thresholds{}) ==
          Mismatch::ArchivedButActive);

    // Archivado aquí a mano cuenta igual que archivado en GitHub: los dos significan "esto
    // ya no se toca", y el push contradice a los dos.
    CHECK(Review(RepoPushed(2), With(Priority::Archived), kNow, Thresholds{}) ==
          Mismatch::ArchivedButActive);

    // Archivado y de verdad quieto no es desajuste: es lo que se esperaba.
    Repo quiet = RepoPushed(300);
    quiet.isArchived = true;
    CHECK(Review(quiet, With(Priority::Archived), kNow, Thresholds{}) == Mismatch::None);
}

TEST_CASE("sin clasificar no es un desajuste") {
    // "Sin clasificar" es una vista propia de la barra lateral, no algo que esté mal. Si
    // entrara aquí, los repositorios nuevos llenarían "Necesita decisión" el primer día y
    // la vista dejaría de servir para lo que sirve.
    CHECK(Review(RepoPushed(1), With(Priority::Unsorted), kNow, Thresholds{}) == Mismatch::None);
    CHECK(Review(RepoPushed(400), With(Priority::Unsorted), kNow, Thresholds{}) == Mismatch::None);
}

TEST_CASE("el umbral de Enfoque es suyo y no el de la actividad") {
    // Se puede querer que algo salga de Enfoque antes incluso de llegar a estar en pausa.
    Thresholds limits;
    limits.focusDormantDays = 5;
    CHECK(Review(RepoPushed(6), With(Priority::Focus), kNow, limits) == Mismatch::FocusDormant);
    CHECK(Classify(DaysAgo(6), kNow, limits) == Activity::Active);
}

// ------------------------------------------------------------------------- Nombres --

TEST_CASE("los nombres con los que se guardan van y vuelven") {
    // Estos textos están dentro de bases de datos ya escritas y dentro de archivos
    // PROYECTO.md que la gente edita a mano. Cambiar uno sin migración rompe las dos cosas.
    const Priority priorities[] = {Priority::Focus, Priority::Secondary, Priority::Someday,
                                   Priority::Archived, Priority::Unsorted};
    for (const Priority priority : priorities) {
        CHECK(Model::PriorityFromSlug(Model::SlugOf(priority)) == priority);
    }
    const State states[] = {State::Active, State::Blocked, State::Waiting, State::Done};
    for (const State state : states) {
        CHECK(Model::StateFromSlug(Model::SlugOf(state)) == state);
    }

    CHECK(std::string(Model::SlugOf(Priority::Focus)) == "enfoque");
    CHECK(std::string(Model::SlugOf(State::Waiting)) == "en-espera");

    // Lo que no se reconoce sale vacío y no se convierte en la primera opción: un
    // "prioridad: loquesea" escrito a mano en un PROYECTO.md no puede acabar en Enfoque.
    CHECK_FALSE(Model::PriorityFromSlug("loquesea").has_value());
    CHECK_FALSE(Model::PriorityFromSlug("").has_value());
    CHECK_FALSE(Model::StateFromSlug("Activo").has_value());
}

// ------------------------------------------------------------------------ Aplazar --

namespace {

// Un Local ya aplazado: la pregunta 'asking' apartada 'days' días a partir de kNow.
Local Parked(Mismatch asking, int days) {
    Local local;
    local.repoId = "R_1";
    local.snoozeFor = asking;
    local.snoozeUntil = Model::DayNumber(kNow) + days;
    return local;
}

Instant DaysAhead(int days) {
    return Model::FromEpoch(Model::ToEpoch(kNow) + static_cast<long long>(days) * 86400);
}

}  // namespace

TEST_CASE("un aplazamiento vence al empezar el día, no a la hora a la que se pidió") {
    // El borde entero: se aparta siete días a mediodía del 21. Sigue aparcado los días 1 a
    // 6, y el séptimo ya no — y el séptimo cuenta desde que empieza, no desde mediodía.
    const Local local = Parked(Mismatch::FocusDormant, 7);

    CHECK(Model::Snoozed(local, Mismatch::FocusDormant, kNow));
    CHECK(Model::Snoozed(local, Mismatch::FocusDormant, DaysAhead(6)));
    // Un minuto antes de la medianoche del sexto día todavía está aparcado…
    CHECK(Model::Snoozed(local, Mismatch::FocusDormant, At("2026-09-27T23:59:00Z")));
    // …y el séptimo, de madrugada, ya vuelve a preguntarse. Comparando por segundos, este
    // volvería a mediodía: a mitad de la mañana y en medio de otra cosa.
    CHECK_FALSE(Model::Snoozed(local, Mismatch::FocusDormant, At("2026-09-28T00:01:00Z")));
    CHECK_FALSE(Model::Snoozed(local, Mismatch::FocusDormant, DaysAhead(7)));
    CHECK_FALSE(Model::Snoozed(local, Mismatch::FocusDormant, DaysAhead(30)));
}

TEST_CASE("aplazar silencia UNA pregunta, no el repositorio") {
    // El caso que justifica guardar el porqué y no solo la fecha: alguien aparca "está en
    // Enfoque y parado" y, mientras dura el plazo, ese mismo repositorio empieza a recibir
    // pushes estando archivado. Es el desajuste más informativo de los tres y no puede
    // quedarse tapado por una respuesta que era a otra cosa.
    const Local local = Parked(Mismatch::FocusDormant, 30);

    CHECK(Model::Snoozed(local, Mismatch::FocusDormant, kNow));
    CHECK_FALSE(Model::Snoozed(local, Mismatch::ArchivedButActive, kNow));
    CHECK_FALSE(Model::Snoozed(local, Mismatch::None, kNow));
}

TEST_CASE("sin aplazamiento no hay nada aparcado, y el cero no es una fecha") {
    // Cero es "nunca se aplazó nada", como el orden. Sin esta comprobación, un repositorio
    // recién creado estaría aparcado hasta 1970 — o sea nunca, por casualidad y no por
    // diseño, hasta el día en que alguien invierta la comparación.
    Local nuevo;
    nuevo.repoId = "R_2";
    CHECK(nuevo.snoozeUntil == 0);
    CHECK_FALSE(Model::Snoozed(nuevo, Mismatch::None, kNow));
    CHECK_FALSE(Model::Snoozed(nuevo, Mismatch::FocusDormant, kNow));
}

TEST_CASE("los nombres del desajuste aplazado van y vuelven") {
    // Acaban escritos en la caché del usuario, como los de prioridad y estado.
    const Mismatch all[] = {Mismatch::None, Mismatch::FocusDormant,
                            Mismatch::ArchivedButActive};
    for (const Mismatch one : all) {
        CHECK(Model::MismatchFromSlug(Model::SlugOf(one)) == one);
    }
    CHECK_FALSE(Model::MismatchFromSlug("loquesea").has_value());
}
