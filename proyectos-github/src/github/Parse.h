#pragma once

// De JSON a Model::Repo. Sin red: come un std::string y devuelve valores, así que se prueba
// entero con respuestas enlatadas.
//
// Aquí está la mitad del riesgo de la fase, y no es el JSON: son los nulos. Medido contra la
// cuenta de verdad, 76 de los 109 repositorios no tienen descripción y 7 no tienen lenguaje
// principal. El nulo es el caso NORMAL. Un parser que trate un null como cadena en un sitio
// y lo desreferencie en otro no da un error: pinta 109 tarjetas con la palabra "null" donde
// debería ir la descripción, o se cae al llegar al primer repositorio vacío — que además son
// justo los olvidados, que son los que esta aplicación existe para encontrar.
//
// Y la otra mitad: nlohmann lanza excepciones por omisión. Una excepción en el hilo de
// sincronización no es un error que se enseñe, es un std::terminate y la ventana
// desapareciendo de la pantalla. Todo se lee con allow_exceptions = false.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/Result.h"
#include "model/Types.h"

namespace Github {

// Lo que GitHub dice de la cuota. -1 significa "no venía".
struct RateInfo {
    int limit = -1;
    int remaining = -1;
    int cost = 0;
    std::optional<Model::Instant> reset;
};

struct Page {
    std::vector<Model::Repo> repos;
    bool hasNextPage = false;
    std::string endCursor;
    int totalCount = 0;
    RateInfo rate;
    // Errores parciales de GraphQL: la respuesta traía datos Y errores. Los repositorios
    // legibles vuelven igual, y esto se enseña como aviso.
    std::vector<std::wstring> warnings;
};

struct Batch {
    std::vector<Model::Repo> repos;
    RateInfo rate;
    std::vector<std::wstring> warnings;
    // La credencial no tiene Contents: read. Quien llama repite la tanda sin ese campo.
    bool contentsForbidden = false;
};

// Pase 1. 'fromOrganization' cambia de dónde cuelga la lista dentro de la respuesta.
Model::Result<Page> ParseMetadata(std::string_view json, bool fromOrganization);

// Pase 2.
Model::Result<Batch> ParseDetail(std::string_view json);

// Validar una credencial: devuelve el login.
Model::Result<std::wstring> ParseViewerLogin(std::string_view json);

// --- Lo de la fase 5 ----------------------------------------------------------------

// El texto de un archivo pedido bajo demanda. 'found' distingue «el archivo no está» de
// «está y está vacío», que no es lo mismo cuando lo que se va a hacer es copiarlo.
struct FileText {
    std::wstring oid;
    std::wstring text;
    bool found = false;
    // GitHub corta los blobs grandes. Un archivo a medias no se copia: se dice.
    bool truncated = false;
};
Model::Result<FileText> ParseFileText(std::string_view json);

// La respuesta del PUT de la API de contenidos.
struct Written {
    // El sha del blob nuevo. Es el que hay que guardar para poder volver a escribir encima.
    std::wstring blobSha;
    std::wstring commitUrl;
};
Model::Result<Written> ParseContentsWrite(std::string_view json);

}  // namespace Github
