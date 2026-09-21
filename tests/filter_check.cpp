// Lo unico de la fase 7 que no se ve en pantalla hasta que ya ha escondido el archivo que
// buscabas: a que se parece un nombre y donde acaba una ruta. El tiempo del filtro tambien
// se comprueba aqui, porque "instantaneo en carpetas de miles de archivos" es un criterio de
// aceptacion, no una impresion.
// Ejecutar: build\rayo_filter_check.exe (solo imprime el tiempo medido).
#undef NDEBUG  // los asserts son la comprobacion: tambien en Release
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "fs/DirectoryReader.h"

int main() {
    assert(NameContains(L"notas.txt", L""));     // sin filtro no se esconde nada
    assert(NameContains(L"notas.txt", L"ota"));  // en medio, no solo al principio
    assert(NameContains(L"notas.txt", L".txt"));
    assert(!NameContains(L"notas.txt", L"otra"));
    assert(!NameContains(L"a", L"abc"));  // la aguja no cabe en el pajar

    // Ni mayusculas ni tildes, que es lo que pide la fase.
    assert(NameContains(L"Informe FINAL.pdf", L"final"));
    assert(NameContains(L"Cancion de cuna.mp3", L"CANCION"));
    assert(NameContains(L"Canci\u00f3n.mp3", L"cancion"));
    assert(NameContains(L"cancion.mp3", L"Canci\u00f3n"));
    assert(NameContains(L"CAF\u00c9.txt", L"caf\u00e9"));

    // La barra final sobra en todas partes menos en la raiz de una unidad, donde quitarla
    // dejaria "C:", que para Win32 es el directorio actual de esa unidad y no su raiz.
    assert(NormalizePath(L"C:\\Windows\\") == L"C:\\Windows");
    assert(NormalizePath(L"C:/Windows/System32/") == L"C:\\Windows\\System32");
    assert(NormalizePath(L"C:\\") == L"C:\\");
    assert(NormalizePath(L"") == L"");  // la raiz virtual se devuelve tal cual

    // Tamanos y fechas: que digan algo en los extremos, el formato lo pone el usuario.
    assert(!FormatBytes(0).empty());
    assert(!FormatBytes(1536).empty());
    assert(!FormatBytes(1ull << 42).empty());
    FILETIME zero{};
    assert(!FormatTime(zero).empty());

    // Criterio de la fase: el filtro responde en carpetas de miles de archivos. Se mide
    // sobre 10.000 nombres, que es el tamano del presupuesto de listado.
    std::vector<std::wstring> names;
    names.reserve(10000);
    for (int i = 0; i < 10000; ++i)
        names.push_back(L"archivo de prueba " + std::to_wstring(i) + L".txt");

    const auto start = std::chrono::steady_clock::now();
    int hits = 0;
    for (const std::wstring& name : names)
        if (NameContains(name, L"prueba 99")) ++hits;
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();

    assert(hits == 111);  // los numeros que empiezan por 99: 99, 990..999 y 9900..9999
    std::printf("filtro: 10.000 nombres en %.1f ms\n", ms);
    assert(ms < 16.0);  // un frame: por encima de esto se notaria al teclear
    return 0;
}
