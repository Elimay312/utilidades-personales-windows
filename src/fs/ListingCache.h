#pragma once

#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fs/DirectoryReader.h"

// Listado inmutable compartido entre la cache y las columnas: cambiar de carpeta mueve un
// puntero, no copia 12.000 entradas. Nulo mientras una columna no tiene nada que mostrar.
using EntryList = std::shared_ptr<const std::vector<DirectoryEntry>>;

// Las filas de un listado que puede ser nulo, para no repetir el if en cada uso.
const std::vector<DirectoryEntry>& Rows(const EntryList& listing);

// LRU de listados recientes: ir y volver es inmediato. No caduca nada por tiempo; quien
// navega relanza siempre la lectura por detras (y en la fase 5 lo hara el vigilante).
class ListingCache {
public:
    EntryList Get(const std::wstring& path);  // nulo si no esta; lo encontrado pasa al frente
    void Put(const std::wstring& path, EntryList listing);
    // El vigilante de disco dice que ese listado ya no vale. Las columnas que lo esten
    // usando no se enteran: tienen su propio shared_ptr y siguen pintando hasta el refresco.
    void Drop(const std::wstring& path);

private:
    void Evict();

    // ponytail: busqueda lineal sobre 32 elementos. Un unordered_map de iteradores seria
    // mas codigo para ahorrar unos nanosegundos por navegacion.
    std::list<std::pair<std::wstring, EntryList>> m_items;
    size_t m_rows = 0;  // entradas totales: 32 carpetas grandes no caben en 50 MB
};
