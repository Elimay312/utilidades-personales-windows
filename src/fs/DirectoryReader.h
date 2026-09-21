#pragma once

#include <Windows.h>

#include <optional>
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
// `path` identifica el resultado: si ninguna columna esta en esa ruta, se descarta.
struct DirectoryListing {
    std::wstring path;
    std::vector<DirectoryEntry> entries;
    DWORD error = ERROR_SUCCESS;
    // Espacio libre de la unidad, para la barra de estado. 0 = no se pudo saber (raiz
    // virtual). Viaja con el listado porque sale del mismo hilo y del mismo sitio: un
    // buzon aparte solo para esto seria mas fontaneria que dato.
    unsigned long long freeBytes = 0;
};

// Absoluta, con barras invertidas, sin . ni .. y sin barra final salvo en la raiz de una
// unidad: el prefijo \\?\ exige exactamente eso, y la misma carpeta escrita de dos formas
// serian dos claves distintas en la cache. La ruta vacia es la raiz virtual (lista de
// unidades) y se devuelve tal cual.
std::wstring NormalizePath(const std::wstring& path);

// Ruta lista para las APIs de disco: prefijo \\?\ (o \\?\UNC\) para saltarse MAX_PATH.
// Exige una ruta ya pasada por NormalizePath: el prefijo desactiva la normalizacion.
std::wstring LongPath(const std::wstring& path);

// nullopt = no hay donde subir. "" = raiz virtual con la lista de unidades.
std::optional<std::wstring> ParentPath(const std::wstring& path);

// JoinPath("", "C:") -> "C:\" : desde la lista de unidades se entra en la raiz, no en el
// directorio actual de esa unidad (que es lo que entenderia "C:").
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);

// Nombre de la carpeta dentro de su padre: "C:\Windows" -> "Windows", "C:\" -> "C:".
std::wstring LastComponent(const std::wstring& path);

// Bloqueante y sin estado: se llama desde un hilo de trabajo, nunca desde el de UI.
// Ordena carpetas primero y luego por orden natural (archivo2 antes que archivo10).
// Con la ruta vacia enumera las unidades logicas.
DirectoryListing ReadDirectory(std::wstring path);

// Subcadena ignorando mayusculas y tildes ("cafe" encuentra "Cafe.txt"). Aguja vacia =
// todo pasa. Es el filtro de la columna central.
bool NameContains(const std::wstring& name, const std::wstring& needle);

// En el formato del usuario: "1,21 MB" y "21/09/2026 14:03". Los usan la lista, la vista
// previa y la barra de estado, asi que viven aqui y no en uno de los tres.
std::wstring FormatBytes(unsigned long long bytes);
std::wstring FormatTime(const FILETIME& utc);

std::string ToUtf8(const std::wstring& text);
// La vuelta: lo que se escribe en un campo de ImGui llega en UTF-8 y el disco quiere wide.
std::wstring FromUtf8(const std::string& text);
std::string FormatWin32Error(DWORD error);
