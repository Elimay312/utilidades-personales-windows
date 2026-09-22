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
#include <vector>

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

// Un commit de la rama principal. El inspector enseña los cinco últimos.
struct Commit {
    std::wstring oid;
    std::wstring title;
    std::wstring author;
    // Vacío no pasa con un commit de verdad, pero la respuesta puede venir sin el campo y
    // una fecha inventada se lee igual que una buena.
    std::optional<Instant> committedAt;
};

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

    // Crudo. Lo interpreta projectfile/Proyecto.h; aquí solo se guarda, con su oid para
    // saber si cambió sin volver a leerlo entero — y porque es el 'sha' que pide la API de
    // contenidos para escribir encima sin pisar a nadie.
    std::wstring proyectoOid;
    std::wstring proyectoText;

    // Los cinco últimos commits de la rama principal, el más nuevo primero. Vienen del
    // segundo pase y viven en la caché para que abrir el inspector no espere a la red.
    std::vector<Commit> commits;
    // Los otros .md de la raíz: los que se pueden copiar a las novedades. Sin README,
    // CHANGELOG, LICENSE ni el propio PROYECTO.md, que no son notas de nadie.
    std::vector<std::wstring> rootMarkdown;

    Instant seenAt{};
    // Dejó de aparecer en la cuenta. La fila NO se borra: colgarían las notas del usuario.
    std::optional<Instant> goneAt;
};

// Lo del usuario. Nada de aquí viene de GitHub ni vuelve a GitHub salvo que se active el
// modo repo.
struct Local {
    std::string repoId;
    Priority priority = Priority::Unsorted;
    State state = State::Active;
    std::wstring nextStep;
    bool repoMode = false;  // el interruptor: escribir PROYECTO.md en ese repositorio
    // El usuario ha confirmado alguna vez que se escriba en ESTE repositorio. Es una
    // columna aparte del interruptor y no un detalle: la regla 5 de SEGURIDAD.md dice que no
    // hay un ajuste global que active el modo repo en los 120 de golpe sin pasar por la
    // confirmación una vez por repositorio. Con un solo booleano, «modo repo por omisión»
    // sería exactamente ese ajuste. Escribir exige los dos.
    bool repoConfirmed = false;
    // Hay un guardado local que todavía no ha llegado a GitHub. Se pone al guardar y se
    // quita cuando el commit sale bien; los pendientes se reintentan al terminar la
    // siguiente sincronización. Sin esto, editar sin cobertura pierde el commit en silencio.
    bool pushPending = false;
    std::wstring folder;  // carpeta local, para el botón de abrir
    // El orden puesto a mano arrastrando. Cero es "nunca se ha tocado", y entonces manda la
    // fecha del último push, que es el orden de la fase 4. NO viaja a PROYECTO.md: el
    // formato de CLAUDE.md no tiene ese campo, y un commit por cada tarjeta que se arrastra
    // sería ciento nueve commits por una tarde ordenando.
    int order = 0;
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
