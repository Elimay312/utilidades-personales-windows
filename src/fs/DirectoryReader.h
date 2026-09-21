#pragma once

#include <Windows.h>

#include <string>
#include <vector>

struct DirectoryEntry {
    std::wstring name;
    std::string nameUtf8;  // convertido en el hilo de trabajo: dibujar no convierte nada
    unsigned long long size = 0;
    FILETIME modified{};
    DWORD attributes = 0;

    bool IsDirectory() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
    bool IsHidden() const {
        return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
    }
};

// Resultado de una peticion. error != ERROR_SUCCESS -> entries vacio y mensaje en la barra.
struct DirectoryListing {
    std::wstring path;
    std::vector<DirectoryEntry> entries;
    DWORD error = ERROR_SUCCESS;
    unsigned long long generation = 0;
};

// Absoluta, con barras invertidas y sin . ni .. : el prefijo \\?\ exige exactamente eso.
std::wstring NormalizePath(const std::wstring& path);

// Bloqueante y sin estado: se llama desde un hilo de trabajo, nunca desde el de UI.
// Ordena carpetas primero y luego por orden natural (archivo2 antes que archivo10).
DirectoryListing ReadDirectory(std::wstring path, unsigned long long generation);

std::string ToUtf8(const std::wstring& text);
std::string FormatWin32Error(DWORD error);
