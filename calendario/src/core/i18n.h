#pragma once

// The interface language. Two of them, and every string in both versions next to each other at
// the place it is drawn: T(L"Sin eventos", L"No events"). A table of ids in another file would
// make every call site a lookup to read, for two languages that are never going to be twenty.
//
// The parser does not care: it understands Spanish and English at once, whatever this says.

#include <atomic>
#include <string_view>

namespace agenda {

enum class Lang { Es, En };

// Set on the interface thread at start and from the settings window. Atomic because the store's
// worker words its failure messages on its own thread.
inline std::atomic<Lang>& CurrentLang() {
  static std::atomic<Lang> lang{Lang::Es};
  return lang;
}

inline bool English() { return CurrentLang().load(std::memory_order_relaxed) == Lang::En; }

inline std::wstring_view T(std::wstring_view es, std::wstring_view en) {
  return English() ? en : es;
}

}  // namespace agenda
