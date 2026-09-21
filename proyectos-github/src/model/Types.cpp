#include "model/Types.h"

namespace Model {

const char* SlugOf(Priority priority) {
    switch (priority) {
        case Priority::Focus:     return "enfoque";
        case Priority::Secondary: return "secundario";
        case Priority::Someday:   return "algun-dia";
        case Priority::Archived:  return "archivado";
        case Priority::Unsorted:  return "sin-clasificar";
    }
    // Sin 'default' arriba: al añadir una prioridad, /W4 avisa de que falta su caso en vez
    // de dejarla caer aquí y guardarse en SQLite como si fuera otra cosa.
    return "sin-clasificar";
}

const char* SlugOf(State state) {
    switch (state) {
        case State::Active:  return "activo";
        case State::Blocked: return "bloqueado";
        case State::Waiting: return "en-espera";
        case State::Done:    return "terminado";
    }
    return "activo";
}

std::optional<Priority> PriorityFromSlug(std::string_view slug) {
    if (slug == "enfoque")        return Priority::Focus;
    if (slug == "secundario")     return Priority::Secondary;
    if (slug == "algun-dia")      return Priority::Someday;
    if (slug == "archivado")      return Priority::Archived;
    if (slug == "sin-clasificar") return Priority::Unsorted;
    return std::nullopt;
}

std::optional<State> StateFromSlug(std::string_view slug) {
    if (slug == "activo")    return State::Active;
    if (slug == "bloqueado") return State::Blocked;
    if (slug == "en-espera") return State::Waiting;
    if (slug == "terminado") return State::Done;
    return std::nullopt;
}

}  // namespace Model
