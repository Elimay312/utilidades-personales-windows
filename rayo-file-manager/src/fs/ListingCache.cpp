#include "fs/ListingCache.h"

namespace {

constexpr size_t kMaxFolders = 32;
// Tope de memoria, no de carpetas: 32 carpetas de 12.000 entradas serian ~54 MB y el
// presupuesto entero son 50. 50.000 entradas son ~7 MB.
constexpr size_t kMaxRows = 50000;

}  // namespace

const std::vector<DirectoryEntry>& Rows(const EntryList& listing) {
    static const std::vector<DirectoryEntry> kEmpty;
    return listing ? *listing : kEmpty;
}

EntryList ListingCache::Get(const std::wstring& path) {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it->first != path) continue;
        m_items.splice(m_items.begin(), m_items, it);
        return m_items.front().second;
    }
    return {};
}

void ListingCache::Put(const std::wstring& path, EntryList listing) {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it->first != path) continue;
        m_rows -= it->second->size();
        m_rows += listing->size();
        it->second = std::move(listing);
        m_items.splice(m_items.begin(), m_items, it);
        Evict();
        return;
    }

    m_rows += listing->size();
    m_items.emplace_front(path, std::move(listing));
    Evict();
}

void ListingCache::Drop(const std::wstring& path) {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it->first != path) continue;
        m_rows -= it->second->size();
        m_items.erase(it);
        return;
    }
}

void ListingCache::Evict() {
    // El frente nunca se tira: es lo que se acaba de pedir.
    while (m_items.size() > kMaxFolders || (m_items.size() > 1 && m_rows > kMaxRows)) {
        m_rows -= m_items.back().second->size();
        m_items.pop_back();
    }
}
