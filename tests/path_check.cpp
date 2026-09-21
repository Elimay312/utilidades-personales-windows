// Comprobacion minima de las utilidades de rutas: sin ellas, subir desde la raiz de una
// unidad o entrar desde la lista de unidades acaba en la carpeta equivocada.
// Ejecutar: build\rayo_path_check.exe (no imprime nada si todo va bien).
#undef NDEBUG  // los asserts son la comprobacion: tambien en Release
#include <cassert>

#include "fs/DirectoryReader.h"

int main() {
    assert(LastComponent(L"C:\\Windows\\System32") == L"System32");
    assert(LastComponent(L"C:\\Windows") == L"Windows");
    assert(LastComponent(L"C:\\") == L"C:");

    assert(JoinPath(L"C:\\Windows", L"System32") == L"C:\\Windows\\System32");
    assert(JoinPath(L"C:\\", L"Windows") == L"C:\\Windows");
    assert(JoinPath(L"", L"C:") == L"C:\\");  // desde la lista de unidades, a la raiz

    assert(ParentPath(L"C:\\Windows\\System32") == L"C:\\Windows");
    assert(ParentPath(L"C:\\Windows") == L"C:\\");
    assert(ParentPath(L"C:\\") == L"");  // raiz de unidad -> lista de unidades
    assert(!ParentPath(L""));            // ya no se sube mas

    // Recursos de red: el padre de una carpeta es la raiz del recurso, y ahi se para
    // (por encima solo esta el servidor, que FindFirstFileExW no enumera).
    assert(ParentPath(L"\\\\servidor\\recurso\\datos") == L"\\\\servidor\\recurso");
    assert(!ParentPath(L"\\\\servidor\\recurso"));

    // Ida y vuelta: bajar y volver a subir devuelve a la misma carpeta.
    const std::wstring start = L"C:\\Users\\Public";
    assert(ParentPath(JoinPath(start, L"Documents")) == start);
    return 0;
}
