#include "fs/DirectoryReader.h"

#include <shlwapi.h>

#include <algorithm>
#include <utility>

namespace {

// El prefijo \\?\ quita el limite de MAX_PATH, pero a cambio desactiva toda la
// normalizacion: la ruta ya tiene que venir por NormalizePath.
std::wstring MakeSearchPattern(const std::wstring& path) {
    std::wstring pattern;
    if (path.compare(0, 4, L"\\\\?\\") == 0)
        pattern = path;
    else if (path.compare(0, 2, L"\\\\") == 0)
        pattern = L"\\\\?\\UNC\\" + path.substr(2);
    else
        pattern = L"\\\\?\\" + path;

    if (!pattern.empty() && pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern.push_back(L'*');
    return pattern;
}

bool CompareEntries(const DirectoryEntry& a, const DirectoryEntry& b) {
    if (a.IsDirectory() != b.IsDirectory()) return a.IsDirectory();

    const int natural = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
    if (natural != 0) return natural < 0;

    // ponytail: StrCmpLogicalW cuesta caro (12.000 entradas: 45 ms de sort frente a 5 ms de
    // enumerar) y no es transitivo en casos patologicos, con lo que std::sort podria caer en
    // UB; el desempate ordinal lo hace improbable, no imposible. A partir de unas 14.000
    // entradas se sale del presupuesto de 50 ms. Salida: comparador natural propio con clave
    // precalculada por entrada. No compensa mientras el sort viva en un hilo de trabajo.
    return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
}

}  // namespace

std::wstring NormalizePath(const std::wstring& path) {
    if (path.empty()) return path;

    wchar_t buffer[MAX_PATH];
    DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr);
    if (length == 0) return path;
    if (length < MAX_PATH) return std::wstring(buffer, length);

    // No cabia: el valor devuelto es el tamano que hace falta, con el nulo incluido.
    std::wstring large(length, L'\0');
    length = GetFullPathNameW(path.c_str(), length, large.data(), nullptr);
    if (length == 0 || length >= large.size()) return path;
    large.resize(length);
    return large;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size());
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};

    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, out.data(), size, nullptr, nullptr);
    return out;
}

std::string FormatWin32Error(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (length == 0 || !buffer) {
        if (buffer) LocalFree(buffer);
        return "Error " + std::to_string(error);
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        message.pop_back();
    return ToUtf8(message);
}

DirectoryListing ReadDirectory(std::wstring path, unsigned long long generation) {
    DirectoryListing listing;
    listing.path = std::move(path);
    listing.generation = generation;

    WIN32_FIND_DATAW data;
    const HANDLE find =
        FindFirstFileExW(MakeSearchPattern(listing.path).c_str(), FindExInfoBasic, &data,
                         FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        // Carpeta vacia: no es un error, solo una lista sin nada.
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) listing.error = error;
        return listing;
    }

    listing.entries.reserve(512);
    do {
        const wchar_t* name = data.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0')))
            continue;

        DirectoryEntry entry;
        entry.name = name;
        entry.nameUtf8 = ToUtf8(entry.name);
        entry.attributes = data.dwFileAttributes;
        entry.modified = data.ftLastWriteTime;
        if (!entry.IsDirectory())
            entry.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
                         static_cast<unsigned long long>(data.nFileSizeLow);
        listing.entries.push_back(std::move(entry));
    } while (FindNextFileW(find, &data));

    const DWORD error = GetLastError();
    FindClose(find);
    if (error != ERROR_NO_MORE_FILES) listing.error = error;

    std::sort(listing.entries.begin(), listing.entries.end(), CompareEntries);
    return listing;
}
