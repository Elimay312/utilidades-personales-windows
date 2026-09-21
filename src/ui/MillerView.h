#pragma once

#include <vector>

#include "fs/DirectoryReader.h"

namespace MillerView {

// Solo dibuja: recibe el estado por const& y devuelve cuantas filas caben en la columna.
// scrollToCursor se consume cuando el scroll ya ha seguido al cursor.
int DrawEntries(const std::vector<DirectoryEntry>& entries, int cursor, bool& scrollToCursor);

}  // namespace MillerView
