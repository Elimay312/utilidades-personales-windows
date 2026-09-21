#pragma once

// El estado de la vista principal: qué repositorios hay, en qué vista estamos, qué se
// está buscando y, de todo eso, qué se enseña y en qué orden.
//
// Es puro y vive en brujula_core, y no por simetría con model/. Es por lo de siempre: un
// repositorio que no aparece en la vista donde debería no da un error, da una lista más
// corta —y una lista más corta se parece muchísimo a una lista correcta—. Aquí dentro
// están las tres decisiones que se equivocan en silencio: quién entra en cada vista, en
// qué orden salen y qué significa que dos textos "coincidan".
//
// Y de paso es lo que permite medir el criterio de aceptación de la fase sin abrir la
// ventana: "filtrar entre 120 repositorios es instantáneo" es una prueba, no una
// impresión.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/Rules.h"
#include "model/Types.h"

namespace App {

// Las nueve entradas de la barra lateral, en dos grupos.
//
// Los cinco primeros son Model::Priority en su orden —que su cabecera ya declara como "el
// de la barra lateral y el de las teclas 1-4"— y los cuatro últimos son las vistas
// inteligentes de CLAUDE.md. Sin clasificar sale entre los grupos de prioridad y no entre
// las vistas: es un valor de Priority, y tenerlo en los dos sitios serían dos contadores
// del mismo número esperando a separarse.
//
// 'All' no está en CLAUDE.md y se añade por una razón concreta: sin ella, la búsqueda solo
// puede mirar dentro de la vista elegida, y buscar un repositorio que no sabes dónde
// clasificaste es exactamente para lo que se busca.
enum class Lens {
    Focus,
    Secondary,
    Someday,
    Archived,
    Unsorted,
    All,
    NeedsDecision,
    Dormant,
    ThisWeek,
};

inline constexpr Lens kPriorityLenses[] = {Lens::Focus, Lens::Secondary, Lens::Someday,
                                           Lens::Archived, Lens::Unsorted};
inline constexpr Lens kSmartLenses[] = {Lens::All, Lens::NeedsDecision, Lens::Dormant,
                                        Lens::ThisWeek};

const wchar_t* NameOf(Lens lens);
// El nombre con el que se guarda en 'ajustes'. ASCII y estable, como los slugs de
// model/Types.h y por el mismo motivo: acaba escrito en la caché del usuario.
const char* SlugOf(Lens lens);
// Vacío si el texto no es ninguna de las nueve.
Lens LensFromSlug(std::string_view slug, Lens fallback);

// Cuántos días cuenta "Actividad esta semana". Siete, y como constante porque aparece en
// la regla y en el texto del estado vacío, y dos sietes separados se despegan.
inline constexpr int kThisWeekDays = 7;

// Un repositorio con lo del usuario al lado y lo que ya se dedujo de los dos. Se construye
// una vez al cargar y las vistas solo preguntan: recalcular la actividad de 120
// repositorios en cada pulsación de una búsqueda es justo lo que haría que filtrar dejara
// de ser instantáneo.
struct Entry {
    Model::Repo repo;
    Model::Local local;
    Model::Activity activity = Model::Activity::Dormant;
    Model::Mismatch mismatch = Model::Mismatch::None;
    // Push en los últimos kThisWeekDays días. Se deriva aquí y no se mira al filtrar
    // porque "esta semana" depende de la hora a la que se pregunte, y una vista cuyo
    // contenido cambia entre el contador y la lista es un fallo que nadie ve.
    bool thisWeek = false;
    // Días completos desde el último push. Vacío si no hubo ninguno. Lo pinta la tarjeta, y
    // está precalculado por lo mismo que lo demás: pintar veinticinco tarjetas por
    // fotograma no puede consultar el reloj veinticinco veces.
    std::optional<std::int64_t> daysSincePush;

    // Nombre, dueño, descripción, lenguaje y siguiente paso, pegados y plegados. Es lo que
    // mira la búsqueda, y está precalculado por lo mismo que la actividad.
    std::wstring haystack;

    // Identidad estable del repositorio a través de sincronizaciones. La lista la usa para
    // saber que la fila 7 de antes es la 2 de ahora y deslizarla en vez de repintarla en su
    // sitio nuevo como si fuera otra.
    std::uint64_t key = 0;
};

// "hoy", "ayer", "hace 12 días", "hace 3 meses"… a partir de días completos. Está aquí y no
// en la vista porque el plural y los bordes se equivocan en silencio: un "hace 1 días" no se
// ve hasta que alguien lo ve, y entonces ya lleva meses en pantalla. Días negativos —el
// reloj del equipo atrasado respecto al servidor, que pasa de verdad— salen como "hoy".
std::wstring AgoDays(std::int64_t days);

// Lo mismo con segundos, que es lo que quiere el indicador de sincronización: por debajo de
// un día dice minutos y horas, y de ahí para arriba delega en AgoDays.
std::wstring AgoSeconds(std::int64_t seconds);

// Minúsculas y sin tildes. No usa la configuración regional a propósito: el resultado tiene
// que ser el mismo en cualquier equipo, y aquí lo único que se busca son nombres de
// repositorios y frases en español.
std::wstring Fold(std::wstring_view text);

// Parte la consulta en palabras, ya plegadas. Vacío si solo hay espacios.
std::vector<std::wstring> Terms(std::wstring_view query);

// Todas las palabras tienen que aparecer, en cualquier orden. Con una sola palabra es un
// "contiene" de toda la vida; con varias, "brujula fase" encuentra lo que lleva las dos
// aunque estén en campos distintos.
bool Matches(const Entry& entry, const std::vector<std::wstring>& terms);

bool InLens(const Entry& entry, Lens lens);

// Rellena activity, mismatch, haystack y key. Convierte dos filas de SQLite en algo que las
// vistas pueden preguntar sin volver a pensar.
void Derive(Entry& entry, Model::Instant now, const Model::Thresholds& limits);

// El orden de la lista: el último push primero, y el que no tiene push, al final. Empatados
// —dos repositorios sin push—, por nombre completo, para que el orden no dependa de en qué
// orden los devolvió SQLite.
bool Earlier(const Entry& a, const Entry& b);

class State {
public:
    void SetThresholds(const Model::Thresholds& limits) { m_limits = limits; }
    const Model::Thresholds& Thresholds() const { return m_limits; }

    // Cruza los repositorios con lo del usuario, deriva lo que haga falta y ordena. 'now'
    // entra por parámetro, como en model/Rules.h y por el mismo motivo: es lo único que
    // permite probar los bordes.
    void Load(std::vector<Model::Repo> repos, const std::vector<Model::Local>& locals,
              Model::Instant now);

    // Lo del usuario de UN repositorio, sin releer los otros ciento ocho. Vuelve a derivar
    // lo suyo —el desajuste y lo que mira la búsqueda— y rehace contadores y visibles.
    //
    // No reordena, y no hace falta: el orden de la lista es el del último push, que es del
    // servidor. Lo que sí cambia es en qué vistas entra, y de eso se encarga Recompute.
    // Devuelve false si ese repositorio no está cargado.
    bool ApplyLocal(const Model::Local& local, Model::Instant now);
    const Entry* EntryOf(const std::string& repoId) const;

    void SetLens(Lens lens);
    Lens CurrentLens() const { return m_lens; }

    void SetQuery(std::wstring query);
    const std::wstring& Query() const { return m_query; }
    bool Searching() const { return !m_terms.empty(); }

    const std::vector<Entry>& Entries() const { return m_entries; }
    // Índices de Entries(), en el orden en que se enseñan.
    const std::vector<int>& Visible() const { return m_visible; }
    int VisibleCount() const { return static_cast<int>(m_visible.size()); }

    // El de la posición 'slot' de la lista. Nunca nulo si slot está dentro de rango.
    const Entry* At(int slot) const;
    // Las claves de lo visible, en orden, para Ui::List::Update.
    std::vector<std::uint64_t> Keys() const;

    // Sin contar la búsqueda: el contador de la barra lateral dice cuántos hay en esa
    // vista, no cuántos quedan del filtro de ahora. Un contador que baja al escribir haría
    // imposible saber si el repositorio que buscas está en otro grupo.
    int CountOf(Lens lens) const;

    // Dónde cae un repositorio dentro de lo visible, o -1. Sirve para volver a seleccionar
    // el mismo después de recargar.
    int SlotOfId(const std::string& id) const;

private:
    void Recompute();
    void Recount();

    std::vector<Entry> m_entries;
    std::vector<int> m_visible;
    std::vector<std::wstring> m_terms;
    std::wstring m_query;
    Model::Thresholds m_limits;
    Lens m_lens = Lens::All;
    int m_counts[9] = {};
};

}  // namespace App
