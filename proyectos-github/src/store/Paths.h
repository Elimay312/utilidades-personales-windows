#pragma once

// Dónde vive la caché: %LOCALAPPDATA%\Brujula\brujula.db.
//
// Local y no Roaming a propósito. Es una caché de datos de trabajo —nombres y mensajes de
// commit de 108 repositorios privados—, y Roaming la copiaría al perfil del dominio, que es
// exactamente lo que la regla 4 de SEGURIDAD.md dice que no pasa: nada sale del equipo.

#include <string>

#include "model/Result.h"

namespace Store {

// Crea la carpeta si no existe. La ruta vuelve en UTF-8, que es lo que come sqlite3_open_v2.
Model::Result<std::string> DatabasePath();

}  // namespace Store
