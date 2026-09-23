#pragma once

// Text compared the way people type it: without capitals and without accents, so "reunion"
// finds "Reunión" and "MAÑANA" reads as "manana".
//
// Every character folds to exactly one character and none of them ever disappear, so an index
// into the folded text is the same index in the original. That is what lets the parser's spans
// point back at what the user actually typed, accents and capitals and all.

#include <string>
#include <string_view>

namespace agenda {

inline wchar_t FoldChar(wchar_t c) {
  if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
  // Written as escapes and not as letters: an escape survives any tool that decides to save
  // this file as something other than UTF-8.
  switch (c) {
    case L'Á': case L'á': return L'a';
    case L'É': case L'é': return L'e';
    case L'Í': case L'í': return L'i';
    case L'Ó': case L'ó': return L'o';
    case L'Ú': case L'ú':
    case L'Ü': case L'ü': return L'u';
    case L'Ñ': case L'ñ': return L'n';
    default: return c;
  }
}

inline std::wstring Folded(std::wstring_view text) {
  std::wstring out;
  out.reserve(text.size());
  for (const wchar_t c : text) out.push_back(FoldChar(c));
  return out;
}

}  // namespace agenda
