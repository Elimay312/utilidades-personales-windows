#include "model/Result.h"

namespace Model {

const wchar_t* NameOf(Fail kind) {
    // En español y en una línea, porque el destino es un Ui::Toast de una línea. Lo que
    // matiza cada caso —qué repositorio, a qué hora vuelve la cuota— lo pone quien
    // construye el Error en 'detail'; esto es solo el titular.
    switch (kind) {
        case Fail::Network:   return L"No se pudo conectar con GitHub";
        case Fail::Http:      return L"GitHub respondió con un error";
        case Fail::Auth:      return L"La credencial no vale";
        case Fail::RateLimit: return L"Se agotó la cuota de la API";
        case Fail::Protocol:  return L"La respuesta de GitHub no se entiende";
        case Fail::Storage:   return L"No se pudo guardar en la caché local";
        case Fail::Cancelled: return L"Sincronización cancelada";
    }
    // Sin 'default' arriba: así, al añadir un Fail nuevo, /W4 avisa de que falta su caso
    // en vez de dejarlo caer aquí en silencio.
    return L"Error desconocido";
}

}  // namespace Model
