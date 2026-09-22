#pragma once

// La caché: leer y escribir repositorios, lo del usuario y los ajustes.
//
// Toda la sincronización pasa por aquí, y aquí están las tres decisiones que protegen los
// datos de trabajo:
//
//   1. Escribir metadatos NO toca ni una columna del usuario ni las del segundo pase.
//   2. Un repositorio que deja de aparecer se marca, no se borra. Borrarlo se llevaría por
//      delante sus notas, y una sincronización a medias —un 502 en la segunda página— haría
//      justo eso.
//   3. La fila se reconoce por el node id y, si ese falla, por el nombre completo. GitHub
//      está migrando sus identificadores globales, y el día que cambien, reconocer solo por
//      id convertiría los 109 repositorios en 109 repositorios nuevos, con las notas viejas
//      colgando de identificadores que ya no existen y sin un solo error por ningún lado.

#include <optional>
#include <string>
#include <vector>

#include "model/Types.h"
#include "store/Db.h"

namespace Store {

struct Stats {
    int total = 0;
    int enriched = 0;
    int gone = 0;
    int withProyecto = 0;
};

class Repos {
public:
    explicit Repos(Db& db) : m_db(db) {}

    // Abre una pasada de sincronización y devuelve su número. Cada pasada tiene el suyo, y
    // es lo que luego distingue "apareció en esta" de "ya no aparece". Un sello de tiempo no
    // valdría: dos sincronizaciones en el mismo segundo tendrían el mismo.
    Model::Result<std::int64_t> BeginSync();

    // Primer pase. No toca 'local', ni 'novedades', ni las columnas del segundo pase.
    Model::Outcome UpsertMetadata(const std::vector<Model::Repo>& repos, std::int64_t seq,
                                  Model::Instant now);

    // Marca los que no aparecieron en la pasada 'seq'. Devuelve cuántos.
    Model::Result<int> MarkMissing(std::int64_t seq, Model::Instant now);

    // Los que hay que enriquecer: nuevos, o con un push posterior al detalle que tenemos.
    // Ordenados por push descendente, para que lo que el usuario mira primero llegue antes.
    Model::Result<std::vector<std::string>> NeedingEnrichment();

    // Segundo pase. 'enrichedPush' sale de la respuesta que trajo el detalle, no del primer
    // pase: es la fecha del dato que de verdad tenemos guardado.
    //
    // Escribe también los cinco commits y los .md de la raíz, y los escribe borrando antes
    // los que había: un archivo que desaparece del repositorio tiene que desaparecer de
    // aquí, y un commit que se fue en un rebase también.
    Model::Outcome ApplyEnrichment(const Model::Repo& repo);

    // Todos, SIN commits ni .md de la raíz: son listas, y traerse quinientas filas más para
    // pintar una lista que no las enseña sería pagar el arranque por nada. El inspector
    // pide las suyas al abrirse, que es una consulta por índice.
    Model::Result<std::vector<Model::Repo>> All();
    // Uno solo, con todo lo de su fila. Lo pide quien va a escribir en el repositorio y
    // necesita el nombre completo, la rama y el PROYECTO.md que había.
    Model::Result<Model::Repo> RepoOf(const std::string& repoId);
    Model::Result<Stats> Counts();

    Model::Outcome SaveCommits(const std::string& repoId,
                               const std::vector<Model::Commit>& commits);
    Model::Result<std::vector<Model::Commit>> CommitsOf(const std::string& repoId);
    Model::Outcome SaveRootMarkdown(const std::string& repoId,
                                    const std::vector<std::wstring>& names);
    Model::Result<std::vector<std::wstring>> RootMarkdownOf(const std::string& repoId);

    // Lo del usuario. Si no hay fila, devuelve un Local con los valores por omisión: un
    // repositorio recién visto está sin clasificar y activo, y eso no hace falta escribirlo.
    Model::Result<Model::Local> LocalOf(const std::string& repoId);
    // Todas las filas que existen, de una sentada. Los repositorios sin nada escrito no
    // tienen fila y no salen aquí: quien cruza las dos listas pone los valores por omisión.
    Model::Result<std::vector<Model::Local>> AllLocal();
    Model::Outcome SaveLocal(const Model::Local& local);
    // El orden puesto a mano. Va aparte de SaveLocal —como push_pending y por un motivo
    // parecido— porque se escribe de una tacada para toda una vista al soltar una tarjeta, y
    // no lleva fecha ni encola ningún commit: ordenar no es editar el repositorio de nadie.
    Model::Outcome SetOrder(const std::string& repoId, int order);

    Model::Outcome AddNovedad(const Model::Novedad& novedad);
    Model::Outcome DeleteNovedad(std::int64_t id);
    Model::Result<std::vector<Model::Novedad>> NovedadesOf(const std::string& repoId);
    // Todas, de una consulta. Lo pide la copia de seguridad: ciento nueve consultas para
    // exportar un archivo es lento de una manera que se nota, y aquí no hay nada que
    // virtualizar.
    Model::Result<std::vector<Model::Novedad>> AllNovedades();

    // --- Modo repo -------------------------------------------------------------------
    // Hay un guardado que todavía no ha llegado a GitHub. Va aparte de SaveLocal porque lo
    // escribe el hilo de trabajo cuando el commit sale bien, y ese hilo no tiene —ni debe
    // tener— el resto del Local que el usuario pueda estar editando mientras tanto.
    Model::Outcome SetPushPending(const std::string& repoId, bool pending);
    // Los que hay que reintentar: pendientes, en modo repo y ya confirmados. Los tres
    // requisitos juntos, porque un pendiente de un repositorio sin confirmar sería un commit
    // que nadie autorizó.
    Model::Result<std::vector<std::string>> PendingPushes();
    // Después de un commit, lo que de verdad hay ahora en el repositorio. Sin esto, el
    // siguiente guardado fusionaría sobre el texto de la última sincronización y desharía lo
    // que acabamos de subir.
    Model::Outcome SaveProyectoBlob(const std::string& repoId, const std::wstring& oid,
                                    const std::wstring& text);

    // Ajustes: última sincronización, login de la cuenta, alcance de la credencial. Nunca
    // la credencial, que va al Administrador de credenciales y a ningún otro sitio.
    Model::Result<std::string> Setting(const std::string& key);
    Model::Outcome SetSetting(const std::string& key, const std::string& value);

private:
    Db& m_db;
};

}  // namespace Store
