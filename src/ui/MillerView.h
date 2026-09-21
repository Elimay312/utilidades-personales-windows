#pragma once

#include <vector>

#include "fs/DirectoryReader.h"

namespace MillerView {

// Solo dibuja: recibe el estado por const& y devuelve medidas del viewport.
// scrollToCursor se consume cuando el scroll ya ha seguido al cursor.
void DrawEntries(const std::vector<DirectoryEntry>& entries, int cursor, bool& scrollToCursor,
                 int& outVisibleRows);

}  // namespace MillerView
