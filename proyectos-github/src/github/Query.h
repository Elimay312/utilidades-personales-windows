#pragma once

// Las consultas de GraphQL y los cuerpos JSON que las llevan.
//
// Son dos, y el motivo está medido contra la cuenta de verdad: la consulta de un solo pase
// que pedía CLAUDE.md —los ~120 repositorios con todos los campos, 100 por página— tarda
// 8,3-9,1 segundos por página y una de las peticiones devolvió un 502. Dos páginas son
// diecisiete segundos, que no son "pocos segundos".
//
// La latencia va por repositorio (~85 ms) y no por petición, así que bajar el tamaño de
// página no ayuda: el cursor obliga a ir en serie. Lo que sí ayuda es partir en dos:
//
//   Pase 1  metadatos por cursor. Barato (~1,3 s los 109) y suficiente para pintar.
//   Pase 2  el detalle caro, pero por nodes(ids:), que NO lleva cursor y por eso se puede
//           pedir en paralelo. Medido: 6 peticiones de 20 a la vez, 2,2 s.
//
// Y de regalo, lo incremental sale solo: el pase 2 solo pide los repositorios cuyo pushedAt
// cambió, así que la segunda sincronización del día no hace ninguna petición de detalle.
//
// El texto va en literales adyacentes y NO en una cadena en crudo R"(...)". El limpiador de
// comentarios de auditar.ps1 reconoce cadenas con "(?:\\.|[^"\\])*" y no entiende las
// crudas; la consulta de detalle lleva comillas dentro, en expression: "HEAD:PROYECTO.md",
// y en crudo el auditor la trocearía de forma impredecible. Es la misma clase de falso
// positivo que la fase 2 decidió esquivar en el código en vez de relajando el auditor.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Github {

// 100 por página en el pase 1: es lo que pide CLAUDE.md y con los metadatos solos va bien.
inline constexpr std::size_t kPageSize = 100;
// 20 por petición en el pase 2, y seis a la vez. Medido: 6x20 en paralelo son 2,22 s frente
// a los 11,02 s de 3x50 en serie.
inline constexpr std::size_t kDetailChunk = 20;
inline constexpr int kParallelRequests = 6;

// --- El texto de las consultas -----------------------------------------------------

const char* MetadataQuery();       // repositorios personales
const char* OrgMetadataQuery();    // los de una organización
const char* DetailQuery();         // el pase 2, con PROYECTO.md
const char* DetailQueryNoContents();  // el pase 2 sin PROYECTO.md, para credenciales sin Contents
const char* ViewerQuery();         // validar una credencial
const char* FileQuery();           // el texto de UN archivo de la raíz, bajo demanda

// --- Los cuerpos JSON --------------------------------------------------------------
//
// Se construyen con nlohmann y no pegando cadenas: un nombre de organización con una
// comilla dentro rompería el JSON, y aunque hoy no pueda pasar, el día que se pueda no se
// vería como un error de escapado sino como "GitHub no contesta".

std::string MetadataBody(const std::optional<std::string>& cursor);
std::string OrgMetadataBody(const std::string& org, const std::optional<std::string>& cursor);
std::string DetailBody(const std::vector<std::string>& ids, bool withContents);
std::string ViewerBody();
std::string FileBody(const std::string& repoId, const std::wstring& path);

// --- La escritura, que es lo único del modo repo ------------------------------------
//
// El mensaje del commit, tal cual lo pide CLAUDE.md. Es una constante y no una cadena suelta
// porque el criterio de aceptación de la fase habla de él: «el commit aparece en GitHub con
// el formato correcto».
inline constexpr const char* kCommitMessage = "chore: actualizar PROYECTO.md";
// El ÚNICO archivo que Brújula escribe (SEGURIDAD.md, regla 5). Está aquí, en una constante
// y en un solo sitio, para que auditar.ps1 pueda comprobarlo.
inline constexpr const wchar_t* kProyectoFile = L"PROYECTO.md";

// /repos/{dueño}/{nombre}/contents/PROYECTO.md, o vacío si el nombre completo no tiene la
// forma que debe.
//
// Se VALIDA y no se pega y ya está: el nombre viene de la respuesta de GitHub, acaba dentro
// de una URL, y una ruta construida con algo que no se ha mirado es la clase de descuido que
// se arregla mucho más barato antes de existir. Con un nombre raro, la escritura falla con
// un aviso en vez de pedir una dirección inventada.
std::wstring ContentsPath(const std::wstring& nameWithOwner);

// El cuerpo del PUT. 'sha' vacío significa que el archivo no existía —la API lo pide solo
// para sobrescribir—, y 'branch' vacío deja que GitHub use la rama por omisión.
std::string ContentsBody(const std::wstring& text, const std::wstring& sha,
                         const std::wstring& branch);

// Parte la lista en trozos del tamaño pedido. Ni pierde ni repite ninguno, que es lo único
// que hay que acertar aquí y lo que la prueba comprueba con números que no son múltiplos.
std::vector<std::vector<std::string>> Chunk(const std::vector<std::string>& ids,
                                            std::size_t size);

}  // namespace Github
