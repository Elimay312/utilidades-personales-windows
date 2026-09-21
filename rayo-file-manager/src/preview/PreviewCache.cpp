#include "preview/PreviewCache.h"

#include <utility>

namespace {

// Una preview a tamano de panel (~800x1000) son ~3 MB: 64 MB dan para unas veinte imagenes,
// de sobra para ir y volver con j/k sin redecodificar. El presupuesto de memoria base del
// proyecto son 50 MB, y las texturas viven sobre todo en la GPU.
constexpr size_t kMaxBytes = 64u * 1024u * 1024u;
// Tope aparte de entradas: mil previews de texto no llegarian nunca a kMaxBytes.
constexpr size_t kMaxItems = 64;

bool SameTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

}  // namespace

PreviewPtr PreviewCache::Get(const std::wstring& path, const FILETIME& modified) {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if ((*it)->path != path) continue;

        // El archivo se reescribio: lo guardado ya no es lo que hay en disco.
        if (!SameTime((*it)->modified, modified)) {
            m_bytes -= (*it)->Bytes();
            m_items.erase(it);
            return {};
        }

        m_items.splice(m_items.begin(), m_items, it);
        return m_items.front();
    }
    return {};
}

void PreviewCache::Put(PreviewPtr preview) {
    if (!preview) return;

    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if ((*it)->path != preview->path) continue;
        m_bytes -= (*it)->Bytes();
        m_items.erase(it);
        break;
    }

    m_bytes += preview->Bytes();
    m_items.push_front(std::move(preview));
    Evict();
}

void PreviewCache::Evict() {
    // El frente nunca se tira: es lo que se acaba de pedir.
    while (m_items.size() > kMaxItems || (m_items.size() > 1 && m_bytes > kMaxBytes)) {
        m_bytes -= m_items.back()->Bytes();
        m_items.pop_back();
    }
}
