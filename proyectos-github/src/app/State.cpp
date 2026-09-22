#include "app/State.h"

#include <algorithm>
#include <iterator>

namespace App {

namespace {

// 0x00C0 - 0x00FF, la mitad de Latin-1 que tiene letras. El hueco a cero significa "déjalo
// como está", que es lo que hace falta para los signos de multiplicar y dividir, que viven
// justo en medio de las letras (0xD7 y 0xF7) y no son letras de nada.
//
// La tabla llega hasta aquí y no más lejos a propósito. Lo que hay que plegar son nombres
// de repositorios y frases en español; una tabla Unicode completa sería un archivo de datos
// y un problema de mantenimiento para resolver un caso que no existe.
constexpr wchar_t kLatin1Fold[] =
    L"aaaaaaaceeeeiiiidnooooo\0ouuuuyps"
    L"aaaaaaaceeeeiiiidnooooo\0ouuuuypy";
static_assert(sizeof(kLatin1Fold) / sizeof(wchar_t) == 65, "la tabla cubre 0xC0..0xFF");

wchar_t FoldChar(wchar_t unit) {
    if (unit >= L'A' && unit <= L'Z') return static_cast<wchar_t>(unit - L'A' + L'a');
    if (unit >= 0x00C0 && unit <= 0x00FF) {
        const wchar_t folded = kLatin1Fold[unit - 0x00C0];
        return folded != 0 ? folded : unit;
    }
    return unit;
}

bool IsSpace(wchar_t unit) {
    return unit == L' ' || unit == L'\t' || unit == L'\n' || unit == L'\r';
}

// FNV-1a de 64 bits sobre el node id. Se usa como identidad de fila, no como clave de
// nada: dos repositorios con la misma clave harían que una animación de deslizamiento
// fuera a la fila equivocada durante trescientos milisegundos, y nada más. Con 109
// repositorios eso no va a pasar, y aunque pasara no se pierde un dato.
std::uint64_t Hash(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const char byte : text) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

void Append(std::wstring& out, std::wstring_view text) {
    if (text.empty()) return;
    if (!out.empty()) out.push_back(L' ');
    for (const wchar_t unit : text) out.push_back(FoldChar(unit));
}

}  // namespace

const wchar_t* NameOf(Lens lens) {
    switch (lens) {
    case Lens::Focus:         return L"Enfoque";
    case Lens::Secondary:     return L"Secundario";
    case Lens::Someday:       return L"Algún día";
    case Lens::Archived:      return L"Archivado";
    case Lens::Unsorted:      return L"Sin clasificar";
    case Lens::All:           return L"Todos";
    case Lens::NeedsDecision: return L"Necesita decisión";
    case Lens::Dormant:       return L"Dormidos";
    case Lens::ThisWeek:      return L"Actividad esta semana";
    }
    return L"";
}

const char* SlugOf(Lens lens) {
    switch (lens) {
    case Lens::Focus:         return "enfoque";
    case Lens::Secondary:     return "secundario";
    case Lens::Someday:       return "algun-dia";
    case Lens::Archived:      return "archivado";
    case Lens::Unsorted:      return "sin-clasificar";
    case Lens::All:           return "todos";
    case Lens::NeedsDecision: return "necesita-decision";
    case Lens::Dormant:       return "dormidos";
    case Lens::ThisWeek:      return "esta-semana";
    }
    return "todos";
}

std::optional<Model::Priority> PriorityOf(Lens lens) {
    for (std::size_t i = 0; i < std::size(kPriorityLenses); ++i) {
        if (kPriorityLenses[i] == lens) return static_cast<Model::Priority>(i);
    }
    return std::nullopt;
}

Lens LensOf(Model::Priority priority) {
    const std::size_t index = static_cast<std::size_t>(priority);
    return index < std::size(kPriorityLenses) ? kPriorityLenses[index] : Lens::All;
}

Lens LensFromSlug(std::string_view slug, Lens fallback) {
    for (const Lens lens : kPriorityLenses) {
        if (slug == SlugOf(lens)) return lens;
    }
    for (const Lens lens : kSmartLenses) {
        if (slug == SlugOf(lens)) return lens;
    }
    return fallback;
}

namespace {

// "hace 3 días" o "hace 1 día". El singular va aparte porque es el único que no lleva la
// ese, y escribirlo con un ternario en cada llamada es cómo aparecen los "hace 1 días".
std::wstring Count(std::int64_t value, const wchar_t* one, const wchar_t* many) {
    return L"hace " + std::to_wstring(value) + L" " + (value == 1 ? one : many);
}

}  // namespace

std::wstring AgoDays(std::int64_t days) {
    // Negativo es el reloj del equipo atrasado respecto al del servidor. No es un error y
    // no se enseña como tal: lo que acaba de pasar es de hoy.
    if (days <= 0) return L"hoy";
    if (days == 1) return L"ayer";
    if (days < 30) return Count(days, L"día", L"días");
    if (days < 365) return Count(days / 30, L"mes", L"meses");
    return Count(days / 365, L"año", L"años");
}

std::wstring AgoSeconds(std::int64_t seconds) {
    if (seconds < 60) return L"hace un momento";
    if (seconds < 3600) return Count(seconds / 60, L"minuto", L"minutos");
    if (seconds < 86400) return Count(seconds / 3600, L"hora", L"horas");
    return AgoDays(seconds / 86400);
}

std::wstring Fold(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t unit : text) out.push_back(FoldChar(unit));
    return out;
}

std::vector<std::wstring> Terms(std::wstring_view query) {
    std::vector<std::wstring> terms;
    std::wstring current;
    for (const wchar_t unit : query) {
        if (IsSpace(unit)) {
            if (!current.empty()) terms.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(FoldChar(unit));
    }
    if (!current.empty()) terms.push_back(std::move(current));
    return terms;
}

bool Matches(const Entry& entry, const std::vector<std::wstring>& terms) {
    for (const std::wstring& term : terms) {
        if (entry.haystack.find(term) == std::wstring::npos) return false;
    }
    return true;
}

bool InLens(const Entry& entry, Lens lens) {
    // Un repositorio que dejó de aparecer en la cuenta solo se ve en "Todos", y ahí sale
    // marcado. No se esconde del todo porque sus notas siguen existiendo y hay que poder
    // llegar a ellas; no sale en las demás porque una lista de decisiones no puede tener
    // dentro cosas sobre las que ya no se puede decidir.
    if (entry.repo.goneAt.has_value()) return lens == Lens::All;

    switch (lens) {
    case Lens::Focus:         return entry.local.priority == Model::Priority::Focus;
    case Lens::Secondary:     return entry.local.priority == Model::Priority::Secondary;
    case Lens::Someday:       return entry.local.priority == Model::Priority::Someday;
    case Lens::Archived:      return entry.local.priority == Model::Priority::Archived;
    case Lens::Unsorted:      return entry.local.priority == Model::Priority::Unsorted;
    case Lens::All:           return true;
    case Lens::NeedsDecision: return entry.mismatch != Model::Mismatch::None;
    case Lens::Dormant:       return entry.activity == Model::Activity::Dormant;
    case Lens::ThisWeek:      return entry.thisWeek;
    }
    return false;
}

void Derive(Entry& entry, Model::Instant now, const Model::Thresholds& limits) {
    entry.activity = Model::Classify(entry.repo.pushedAt, now, limits);
    entry.mismatch = Model::Review(entry.repo, entry.local, now, limits);
    entry.daysSincePush.reset();
    if (entry.repo.pushedAt.has_value()) {
        entry.daysSincePush = Model::DaysBetween(*entry.repo.pushedAt, now);
    }
    entry.thisWeek = entry.daysSincePush.has_value() && *entry.daysSincePush < kThisWeekDays;
    entry.key = Hash(entry.repo.id);

    entry.haystack.clear();
    Append(entry.haystack, entry.repo.name);
    // El dueño aparte del nombre completo: buscar "elimay" tiene que encontrar sus
    // repositorios aunque el nombre completo lleve la barra en medio.
    Append(entry.haystack, entry.repo.owner);
    Append(entry.haystack, entry.repo.description);
    Append(entry.haystack, entry.repo.language);
    Append(entry.haystack, entry.local.nextStep);
}

bool Earlier(const Entry& a, const Entry& b) {
    // Lo ordenado a mano manda, y manda en todas las vistas. Ver la cabecera: mirarlo solo
    // entre los de la misma prioridad no sería una relación de orden.
    if ((a.local.order != 0) != (b.local.order != 0)) return a.local.order != 0;
    if (a.local.order != 0 && a.local.order != b.local.order) {
        return a.local.order < b.local.order;
    }

    const bool hasA = a.repo.pushedAt.has_value();
    const bool hasB = b.repo.pushedAt.has_value();
    // El que no tiene push va al final: no es que sea viejísimo, es que no hay nada que
    // ordenar, y colarlo entre los de hace tres años sería inventarse una fecha.
    if (hasA != hasB) return hasA;
    if (hasA && *a.repo.pushedAt != *b.repo.pushedAt) {
        return *a.repo.pushedAt > *b.repo.pushedAt;
    }
    return a.repo.nameWithOwner < b.repo.nameWithOwner;
}

std::vector<std::string> Reordered(const std::vector<std::string>& ids, int from, int to) {
    const int count = static_cast<int>(ids.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) return {};

    std::vector<std::string> moved = ids;
    const std::string carried = moved[static_cast<std::size_t>(from)];
    moved.erase(moved.begin() + from);
    // Después de sacarlo, 'to' sigue siendo la posición de la PANTALLA, que es donde el
    // usuario lo soltó. Insertar ahí es lo correcto justo porque el hueco que se ve mientras
    // se arrastra se dibuja con el elemento ya fuera de la fila.
    moved.insert(moved.begin() + to, carried);
    return moved;
}

void State::Load(std::vector<Model::Repo> repos, const std::vector<Model::Local>& locals,
                 Model::Instant now) {
    m_entries.clear();
    m_entries.reserve(repos.size());

    for (Model::Repo& repo : repos) {
        Entry entry;
        // Lo del usuario, si lo hay. Sin fila, los valores por omisión —sin clasificar y
        // activo—, que es lo mismo que contesta Store::Repos::LocalOf y por lo mismo: 109
        // filas para decir que no hay nada escrito no son 109 filas de nada.
        const auto found = std::find_if(
            locals.begin(), locals.end(),
            [&repo](const Model::Local& local) { return local.repoId == repo.id; });
        if (found != locals.end()) entry.local = *found;
        entry.local.repoId = repo.id;
        entry.repo = std::move(repo);
        Derive(entry, now, m_limits);
        m_entries.push_back(std::move(entry));
    }

    std::stable_sort(m_entries.begin(), m_entries.end(), Earlier);
    Recount();
    Recompute();
}

void State::Recount() {
    for (int& count : m_counts) count = 0;
    for (const Entry& entry : m_entries) {
        for (const Lens lens : kPriorityLenses) {
            if (InLens(entry, lens)) ++m_counts[static_cast<int>(lens)];
        }
        for (const Lens lens : kSmartLenses) {
            if (InLens(entry, lens)) ++m_counts[static_cast<int>(lens)];
        }
    }
}

bool State::ApplyLocal(const Model::Local& local, Model::Instant now) {
    const auto found = std::find_if(m_entries.begin(), m_entries.end(),
                                    [&local](const Entry& entry) {
                                        return entry.repo.id == local.repoId;
                                    });
    if (found == m_entries.end()) return false;

    found->local = local;
    // El identificador es del repositorio, no de lo que llegue: una fila 'local' recién
    // creada puede venir con él vacío y entonces no volvería a encontrarse nunca.
    found->local.repoId = found->repo.id;
    Derive(*found, now, m_limits);
    Recount();
    Recompute();
    return true;
}

const Entry* State::EntryOf(const std::string& repoId) const {
    const auto found = std::find_if(
        m_entries.begin(), m_entries.end(),
        [&repoId](const Entry& entry) { return entry.repo.id == repoId; });
    return found == m_entries.end() ? nullptr : &*found;
}

void State::SetLens(Lens lens) {
    if (m_lens == lens) return;
    m_lens = lens;
    Recompute();
}

void State::SetQuery(std::wstring query) {
    if (m_query == query) return;
    m_query = std::move(query);
    m_terms = Terms(m_query);
    Recompute();
}

void State::Recompute() {
    m_visible.clear();
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const Entry& entry = m_entries[i];
        if (!InLens(entry, m_lens)) continue;
        if (!m_terms.empty() && !Matches(entry, m_terms)) continue;
        m_visible.push_back(static_cast<int>(i));
    }
}

const Entry* State::At(int slot) const {
    if (slot < 0 || slot >= static_cast<int>(m_visible.size())) return nullptr;
    return &m_entries[static_cast<std::size_t>(m_visible[static_cast<std::size_t>(slot)])];
}

std::vector<std::uint64_t> State::Keys() const {
    std::vector<std::uint64_t> keys;
    keys.reserve(m_visible.size());
    for (const int index : m_visible) {
        keys.push_back(m_entries[static_cast<std::size_t>(index)].key);
    }
    return keys;
}

int State::CountOf(Lens lens) const { return m_counts[static_cast<int>(lens)]; }

int State::SlotOfId(const std::string& id) const {
    for (std::size_t slot = 0; slot < m_visible.size(); ++slot) {
        if (m_entries[static_cast<std::size_t>(m_visible[slot])].repo.id == id) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

std::vector<std::string> ReviewQueue(const State& state) {
    std::vector<std::string> queue;
    // Dos pasadas sobre la misma lista y no una con dos cubos: Entries() ya viene ordenada
    // por Earlier, así que recorrerla dos veces sale ordenada dentro de cada grupo sin
    // volver a ordenar nada.
    for (const Entry& entry : state.Entries()) {
        if (InLens(entry, Lens::NeedsDecision)) queue.push_back(entry.repo.id);
    }
    for (const Entry& entry : state.Entries()) {
        // InLens ya deja fuera los que se fueron de la cuenta: una pila para decidir no
        // puede tener dentro cosas sobre las que ya no se puede decidir.
        if (InLens(entry, Lens::NeedsDecision)) continue;
        if (InLens(entry, Lens::Unsorted)) queue.push_back(entry.repo.id);
    }
    return queue;
}

}  // namespace App
