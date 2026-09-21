#pragma once

#include <list>
#include <string>

#include "preview/Preview.h"

// LRU de previews con tope en bytes: recorrer una carpeta de mil fotos no puede crecer sin
// fin. Expulsar suelta el ComPtr de la textura, que es lo que la libera de la GPU.
class PreviewCache {
public:
    // Nulo si no esta o si el archivo cambio de fecha desde que se guardo.
    PreviewPtr Get(const std::wstring& path, const FILETIME& modified);
    void Put(PreviewPtr preview);
    // Al cerrar: las texturas son objetos de D3D y tienen que morir antes que el
    // dispositivo, no despues (la cache es miembro de App y se destruiria mas tarde).
    void Clear() {
        m_items.clear();
        m_bytes = 0;
    }

private:
    void Evict();

    // ponytail: busqueda lineal, como en ListingCache. Son 64 elementos y una navegacion por
    // pulsacion; un indice aparte seria mas codigo para ahorrar nanosegundos.
    std::list<PreviewPtr> m_items;  // el frente es lo mas reciente; la ruta va dentro
    size_t m_bytes = 0;
};
