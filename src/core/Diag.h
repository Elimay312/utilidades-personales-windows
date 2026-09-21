#pragma once

#include <string>

// Diagnostico: cronometro de arranque y volcado de fugas. Los dos escriben al mismo archivo
// porque una app sin consola no tiene donde imprimir, y OutputDebugString solo se ve con un
// depurador enganchado.
namespace Diag {

// Instante con nombre. El primero fija el origen y anota ademas lo que costo llegar hasta el
// desde que el proceso existe: el cargador de Windows no se ve de ninguna otra forma.
void Mark(const char* stage);

// Abre el log en modo anadir. Hasta entonces Log() se traga las lineas, porque Mark() se
// llama antes de saber donde escribir. En Debug ademas manda aqui el informe de fugas del
// CRT, que se emite al final del proceso.
void Open(const std::wstring& path);

void Log(const std::string& text);

// Una linea con todas las etapas y lo que costo cada una. Se llama tras el primer Present.
void WriteStartup();

}  // namespace Diag
