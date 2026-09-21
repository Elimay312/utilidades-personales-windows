#pragma once

// Los tipos de dominio. Lo que la aplicación sabe de un repositorio, partido en dos mitades
// que no se mezclan nunca:
//
//   Repo   es del servidor. La sincronización lo reescribe entero cada vez.
//   Local  es del usuario: prioridad, estado, siguiente paso. La sincronización NO lo toca.
//
// Que sean dos estructuras y dos tablas, y no una con campos de los dos sitios, es lo que
// hace imposible que una sincronización se lleve por delante el siguiente paso que alguien
// escribió a mano. Es la prioridad 1 de CLAUDE.md hecha estructura.
//
// Priority y Activity viven aquí y no en ui/Controls.h, que es donde los estrenó la fase 2:
// son del dominio, la píldora y el punto de color solo los enseñan. Controls.h los recibe
// con un 'using', así que ningún llamador de la fase 2 cambia.

#include <optional>
#include <string>
#include <string_view>

#include "model/Time.h"

namespace Model {

// El orden importa: es el de la barra lateral y el de las teclas 1-4.
enum class Priority { Focus, Secondary, Someday, Archived, Unsorted };

// El "estado" del frontmatter de PROYECTO.md.
enum class State { Active, Blocked, Waiting, Done };

// Deducida del último push, nunca escrita a mano.
enum class Activity { Active, Paused, Dormant };

// Los nombres con los que viajan a SQLite y a PROYECTO.md. Son ESTABLES: cambiar uno
// invalida las bases de datos que ya existen y los archivos que ya se escribieron, así que
// se tocan solo con una migración al lado. Van en ASCII y en minúsculas porque también son
// texto de un archivo que la gente edita a mano.
const char* SlugOf(Priority priority);
const char* SlugOf(State state);

// Vacío si el texto no es ninguno de los conocidos. Quien lea un PROYECTO.md escrito a mano
// tiene que poder encontrarse "prioridad: lo-que-sea" y no romperse: el parser es tolerante
// y quien llama decide con qué se queda.
std::optional<Priority> PriorityFromSlug(std::string_view slug);
std::optional<State> StateFromSlug(std::string_view slug);

// Lo que dice el servidor. Las cadenas en wstring porque su destino es la pantalla; el id
// en string porque su destino es una clave de SQLite y una variable de GraphQL, y nunca se
// enseña.
struct Repo {
    std::string id;  // node id de GraphQL
    std::wstring name;
    std::wstring owner;
    std::wstring nameWithOwner;
    std::wstring description;  // vacía en 76 de los 109: es lo normal, no la excepción
    std::wstring url;

    bool isPrivate = false;
    bool isArchived = false;
    bool isFork = false;

    std::wstring language;  // vacía en 7 de los 109
    std::wstring languageColor;
    int stars = 0;
    long long diskUsage = 0;

    Instant createdAt{};
    Instant updatedAt{};
    // Vacío en un repositorio sin un solo push.
    std::optional<Instant> pushedAt;

    // ---- Lo que rellena el segundo pase. Vacío hasta que ese repositorio se enriquece.
    bool enriched = false;
    // El pushedAt que venía en la respuesta que trajo el detalle, no el del primer pase.
    // Es la columna con la que el siguiente arranque decide si hay que volver a pedirlo.
    std::optional<Instant> enrichedPush;

    std::wstring defaultBranch;
    std::wstring commitOid;
    std::wstring commitTitle;
    std::optional<Instant> commitDate;
    int openIssues = 0;
    int openPrs = 0;

    // Crudo. Interpretarlo es de la fase 5; aquí solo se guarda, con su oid para saber si
    // cambió sin volver a leerlo entero.
    std::wstring proyectoOid;
    std::wstring proyectoText;

    Instant seenAt{};
    // Dejó de aparecer en la cuenta. La fila NO se borra: colgarían las notas del usuario.
    std::optional<Instant> goneAt;
};

// Lo del usuario. Nada de aquí viene de GitHub ni vuelve a GitHub salvo que se active el
// modo repo, que es la fase 5.
struct Local {
    std::string repoId;
    Priority priority = Priority::Unsorted;
    State state = State::Active;
    std::wstring nextStep;
    bool repoMode = false;  // escribir PROYECTO.md en ese repositorio; confirmado uno a uno
    std::wstring folder;    // carpeta local, para el botón de abrir
    Instant updatedAt{};
};

struct Novedad {
    long long id = 0;
    std::string repoId;
    std::string day;  // 'YYYY-MM-DD', como en PROYECTO.md
    std::wstring text;
    Instant createdAt{};
};

}  // namespace Model
