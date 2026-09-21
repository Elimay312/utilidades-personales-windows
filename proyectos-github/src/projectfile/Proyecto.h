#pragma once

// PROYECTO.md: leerlo, escribirlo y fusionarlo.
//
// Es el formato de CLAUDE.md —frontmatter con prioridad, estado, siguiente paso y fecha, y
// una sección «## Novedades» con renglones fechados— y el archivo vive en la raíz de un
// repositorio de trabajo, editado también a mano y desde la web de GitHub. O sea que lo que
// hay que acertar aquí no es el caso bonito: es el otro.
//
// **Lo que no entendemos se conserva.** Una clave inventada en el frontmatter, un párrafo
// entre el frontmatter y las novedades, una sección entera debajo: todo eso vuelve a salir
// tal cual al escribir. Es la diferencia entre una herramienta que colabora con el archivo y
// una que se lo apropia — y el fallo, si se hace mal, es que Brújula le borra a alguien un
// párrafo de su propio repositorio sin dar ni un error.
//
// Puro: come y devuelve std::wstring, no sabe de red ni de SQLite, y está en brujula_core
// con pruebas. Entra aquí por la regla de siempre: nada de esto se ve mal en pantalla. Un
// campo mal leído sale como un valor por omisión plausible, y un párrafo perdido sale como
// un archivo más corto.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/Time.h"
#include "model/Types.h"

namespace Proyecto {

// Un renglón de «## Novedades».
struct Entry {
    // 'YYYY-MM-DD', o vacío si el renglón venía sin fecha. Sin fecha NO es un error: es un
    // archivo escrito a mano, y tirar el renglón por no traer fecha sería borrar una nota.
    std::string day;
    std::wstring text;
};

struct File {
    // --- Lo conocido del frontmatter. Vacío significa "no venía", que no es lo mismo que
    //     "venía vacío": quien escribe rellena lo que falte desde su propio estado.
    std::optional<Model::Priority> priority;
    std::optional<Model::State> state;
    std::wstring nextStep;
    std::optional<Model::Instant> updated;

    // El valor TAL CUAL de las tres claves que se interpretan, para poder devolverlo cuando
    // no lo reconocemos. Sin esto, leer y volver a escribir un archivo con
    // «prioridad: loquesea» le borraría esa línea a su dueño: el optional sale vacío y la
    // clave desaparece. No hace falta para siguiente_paso, que es texto libre y ya se guarda
    // entero. Al escribir manda el valor reconocido; esto es solo el respaldo.
    std::wstring priorityRaw;
    std::wstring stateRaw;
    std::wstring updatedRaw;

    // Las líneas del frontmatter que NO entendemos, en orden y EN CRUDO. En crudo y no como
    // pares clave/valor porque una línea puede no tener ni dos puntos, y reconstruirla desde
    // un par la cambiaría. Lo que se guarda es lo que se vuelve a escribir, letra por letra.
    std::vector<std::wstring> unknown;

    std::vector<Entry> novedades;

    // El cuerpo que no es la lista de novedades, en dos trozos: lo que iba entre el
    // frontmatter y el encabezado, y lo que iba después de la lista.
    std::wstring before;
    std::wstring after;

    bool hadFrontmatter = false;
    bool hadNovedades = false;
    // El salto de línea del original. Reescribir con LF un archivo que venía en CRLF es un
    // diff de TODAS las líneas, y un commit así no hay quien lo revise.
    bool crlf = false;
};

// No falla nunca y no devuelve Result. Un archivo que no entendemos no es un error: es un
// archivo del que solo sabemos que su contenido entero es 'before'.
File Parse(std::wstring_view text);

// Lo que se sube. Escribe primero las cuatro claves conocidas que tengan valor, después las
// desconocidas tal cual, y termina siempre con un solo salto de línea.
std::wstring Render(const File& file);

// Lo que hay en el repositorio ahora mismo + lo que el usuario tiene aquí = lo que se sube.
//
// Conserva de 'remote' todo lo que no es nuestro —el cuerpo, las claves desconocidas, el
// salto de línea— y pisa lo conocido con lo local. Las novedades se UNEN por fecha y texto:
// una escrita desde otro equipo no puede desaparecer porque este no la tuviera.
//
// Es la misma función para la primera escritura y para el reintento tras un conflicto, y eso
// es lo que hace cierto "si hay conflicto, recargar y reintentar sin perder la edición": lo
// único que cambia entre las dos llamadas es el 'remote'.
File Merge(const File& remote, const Model::Local& local,
           const std::vector<Model::Novedad>& novedades, Model::Instant now);

// El camino contrario: el botón «Importar» del inspector. Lo que el archivo no trae no se
// toca, así que importar nunca vacía un campo que ya estaba escrito aquí.
struct Import {
    Model::Local local;
    std::vector<Model::Novedad> novedades;
};
Import Adopt(const File& file, const Model::Local& current);

// Trocear OTRO .md de la raíz en novedades. Un renglón por viñeta; si no hay ninguna, una
// sola novedad con el texto entero. Se enseña antes de copiar nada, así que esto solo decide
// qué se va a proponer.
std::vector<std::wstring> AsNovedades(std::wstring_view markdown);

}  // namespace Proyecto
