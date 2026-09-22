#include "model/Rules.h"

#include <algorithm>

namespace Model {

Activity Classify(std::optional<Instant> pushedAt, Instant now, const Thresholds& limits) {
    if (!pushedAt.has_value()) return Activity::Dormant;

    const std::int64_t days = DaysBetween(*pushedAt, now);

    // Negativo significa que el push es "del futuro", y pasa de verdad: el reloj del equipo
    // va unos segundos atrasado respecto al del servidor y un push de hace un minuto sale
    // con fecha posterior a la de ahora. Como la comparación es "menor que", eso cae solo
    // en activo; lo que no puede pasar es que alguien lo convierta a sin signo por el
    // camino y un push recentísimo salga dormido.
    if (days < limits.activeDays) return Activity::Active;
    if (days <= limits.dormantDays) return Activity::Paused;
    return Activity::Dormant;
}

FocusPlan PlanFocus(const std::vector<FocusEntry>& inFocus, const std::string& candidate,
                    Instant now, const Thresholds& limits) {
    FocusPlan plan;

    const bool alreadyIn =
        std::any_of(inFocus.begin(), inFocus.end(),
                    [&](const FocusEntry& entry) { return entry.id == candidate; });
    if (alreadyIn) {
        // No consume hueco. Sin esto, marcar Enfoque dos veces sobre el mismo repositorio
        // pediría bajar a alguien para hacerle sitio al que ya estaba dentro.
        plan.fits = true;
        return plan;
    }

    if (limits.focusLimit > 0 && static_cast<int>(inFocus.size()) < limits.focusLimit) {
        plan.fits = true;
        return plan;
    }

    // No cabe. Se devuelven todos los de dentro ordenados por el que lleva más tiempo sin un
    // push, para que la pregunta llegue con una respuesta ya sugerida. Elegir sigue siendo
    // del usuario: la lista se ofrece entera y no se baja a nadie por nuestra cuenta.
    plan.fits = false;

    std::vector<const FocusEntry*> sorted;
    sorted.reserve(inFocus.size());
    for (const FocusEntry& entry : inFocus) sorted.push_back(&entry);

    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const FocusEntry* a, const FocusEntry* b) {
                         // El que no tiene push va primero: es el que más tiempo lleva sin
                         // nada. Comparar optional directamente ya hace eso —vacío es menor
                         // que cualquier valor—, pero escrito a mano se lee sin tener que
                         // acordarse de esa regla.
                         if (a->pushedAt.has_value() != b->pushedAt.has_value()) {
                             return !a->pushedAt.has_value();
                         }
                         if (!a->pushedAt.has_value()) return false;
                         return *a->pushedAt < *b->pushedAt;
                     });

    plan.demote.reserve(sorted.size());
    for (const FocusEntry* entry : sorted) plan.demote.push_back(entry->id);

    // 'now' no se usa para ordenar —el orden por fecha de push ya es el mismo— pero se pide
    // en la firma para que la regla pueda pasar a mirar otra cosa sin cambiar a todos los
    // que la llaman.
    (void)now;
    return plan;
}

Mismatch Review(const Repo& repo, const Local& local, Instant now, const Thresholds& limits) {
    const Activity activity = Classify(repo.pushedAt, now, limits);

    // Archivado con pushes recientes. Se mira primero porque es el desajuste más raro y el
    // más informativo: alguien volvió a trabajar en algo que se había dado por cerrado.
    //
    // Cuenta como archivado tanto si lo está en GitHub como si el usuario lo bajó a
    // Archivado aquí: los dos significan "esto ya no se toca", y en los dos el push
    // contradice la decisión.
    const bool archived = repo.isArchived || local.priority == Priority::Archived;
    if (archived && activity == Activity::Active) return Mismatch::ArchivedButActive;

    // En Enfoque y parado. El umbral es suyo y no el de "activo": se puede querer que un
    // repositorio salga de Enfoque antes de que llegue siquiera a estar en pausa.
    if (local.priority == Priority::Focus) {
        if (!repo.pushedAt.has_value()) return Mismatch::FocusDormant;
        if (DaysBetween(*repo.pushedAt, now) > limits.focusDormantDays) {
            return Mismatch::FocusDormant;
        }
    }

    return Mismatch::None;
}

bool Snoozed(const Local& local, Mismatch asking, Instant today) {
    if (local.snoozeUntil == 0) return false;
    if (local.snoozeFor != asking) return false;
    return DayNumber(today) < local.snoozeUntil;
}

}  // namespace Model
