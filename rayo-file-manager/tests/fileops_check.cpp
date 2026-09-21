// Las dos reglas de nombres de la fase 6: ninguna se ve en pantalla hasta que ya ha decidido
// mal (preseleccionar de mas al renombrar, o crear un archivo donde querias una carpeta).
// Ejecutar: build\rayo_fileops_check.exe (no imprime nada si todo va bien).
#undef NDEBUG  // los asserts son la comprobacion: tambien en Release
#include <cassert>

#include "fs/FileOps.h"

int main() {
    // Lo que queda preseleccionado al renombrar: el nombre, nunca la extension.
    assert(StemLength("foto.jpg") == 4);
    assert(StemLength("sin_punto") == 9);
    assert(StemLength("") == 0);
    assert(StemLength("a.tar.gz") == 5);      // solo cuenta el ultimo punto
    assert(StemLength(".gitignore") == 10);   // un punto inicial no es extension
    assert(StemLength("acaba.") == 5);        // extension vacia: se deja fuera igual
    // UTF-8: el punto es ASCII y no aparece dentro de una secuencia multibyte, asi que la
    // longitud sale en bytes y parte donde debe.
    assert(StemLength("\xc3\xb1u.txt") == 3);

    std::wstring name = L"notas.txt";
    assert(!SplitNewName(name) && name == L"notas.txt");

    name = L"carpeta\\";
    assert(SplitNewName(name) && name == L"carpeta");

    name = L"carpeta\\\\";  // dos barras: se quitan las dos
    assert(SplitNewName(name) && name == L"carpeta");

    name = L"carpeta/";  // la barra normal tambien vale
    assert(SplitNewName(name) && name == L"carpeta");

    name = L"";
    assert(!SplitNewName(name) && name.empty());

    name = L"\\";  // todo barras: carpeta sin nombre, y RunFileOp devolvera el error
    assert(SplitNewName(name) && name.empty());
    return 0;
}
