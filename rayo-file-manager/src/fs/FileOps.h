#pragma once

#include <string>
#include <vector>

// Copiar, mover, borrar, renombrar y crear, todo con IFileOperation: asi salen gratis la
// Papelera, el deshacer del shell, los dialogos de conflicto y la barra de progreso nativa.
enum class FileOp {
    Copy,     // sources -> dest
    Move,     // sources -> dest
    Recycle,  // sources a la Papelera
    Delete,   // sources sin Papelera; la confirmacion la da quien llama
    Rename,   // sources[0] pasa a llamarse name
    Create,   // name dentro de dest; si acaba en barra invertida es una carpeta
};

// Bloqueante y con dialogos: se llama desde un hilo de trabajo con COM ya en STA, nunca
// desde el hilo de UI. Devuelve el mensaje para la barra de estado, ya en UTF-8.
//
// Las rutas van SIN el prefijo \\?\ : las APIs del shell no lo aceptan. Lo que cubre las
// rutas largas aqui es el longPathAware del manifiesto.
std::string RunFileOp(FileOp op, const std::vector<std::wstring>& sources,
                      const std::wstring& dest, const std::wstring& name);

// Longitud del nombre sin la extension, que es lo que se preselecciona al renombrar.
// "foto.jpg" -> 4. Un punto inicial no es extension (".gitignore" -> todo) y solo cuenta el
// ultimo ("a.tar.gz" -> "a.tar"). En UTF-8 el punto es ASCII y no puede aparecer dentro de
// una secuencia multibyte, asi que buscarlo a pelo es correcto.
size_t StemLength(const std::string& nameUtf8);

// Regla de la tecla 'a': terminar en barra significa carpeta. Quita las barras finales y
// devuelve si habia alguna.
bool SplitNewName(std::wstring& name);
