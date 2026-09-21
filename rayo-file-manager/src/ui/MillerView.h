#pragma once

#include <set>
#include <string>
#include <vector>

#include "fs/DirectoryReader.h"
#include "ui/EditField.h"

namespace MillerView {

// Solo dibuja: recibe el estado por const& y devuelve cuantas filas caben en la columna.
// scrollToCursor se consume cuando el scroll ya ha seguido al cursor.
//
// dir + marked: las marcas son rutas completas (valen entre carpetas), asi que hace falta
// saber en que carpeta esta esta columna. edit: campo de texto sobre edit->row, o nullptr.
// Las columnas que no marcan ni editan siguen llamando con tres argumentos.
int DrawEntries(const std::vector<DirectoryEntry>& entries, int cursor, bool& scrollToCursor,
                const std::wstring& dir = {}, const std::set<std::wstring>* marked = nullptr,
                EditField::State* edit = nullptr);

}  // namespace MillerView
