#pragma once

#include "fs/ListingCache.h"
#include "preview/Preview.h"

namespace PreviewPane {

// Solo dibuja. `entry` es lo que hay bajo el cursor y decide la fuente: si es carpeta se
// pinta `entries` (su listado), si es archivo se pinta `preview`. Cualquiera de los dos es
// nulo mientras el hilo de trabajo no ha contestado, y entonces no se pinta nada: el panel
// en blanco durante unos milisegundos se ve mejor que la preview de otro archivo.
void Draw(const DirectoryEntry* entry, const EntryList& entries, const PreviewPtr& preview);

}  // namespace PreviewPane
