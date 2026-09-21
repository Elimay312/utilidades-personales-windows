#include "fs/DirectoryReader.h"

#include <shlwapi.h>

#include <algorithm>
#include <utility>

namespace {

std::wstring MakeSearchPattern(const std::wstring& path) {
    std::wstring pattern = LongPath(path);
    if (!pattern.empty() && pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern.push_back(L'*');
    return pattern;
}

bool IsDriveRoot(const std::wstring& path) {
    return path.size() == 3 && path[1] == L':' && path[2] == L'\\';
}

// Quita las barras finales sin llegar a vaciar la cadena ("C:\" -> "C:").
std::wstring TrimTrailingSlashes(const std::wstring& path) {
    std::wstring out = path;
    while (out.size() > 1 && out.back() == L'\\') out.pop_back();
    return out;
}

// La raiz virtual: ninguna API enumera "Este equipo" como carpeta, asi que el listado
// de unidades se fabrica a mano y desde ahi todo lo demas funciona igual.
DirectoryListing ReadDrives() {
    DirectoryListing listing;

    wchar_t buffer[512];
    const DWORD length = GetLogicalDriveStringsW(ARRAYSIZE(buffer), buffer);
    if (length == 0 || length > ARRAYSIZE(buffer)) {
        listing.error = GetLastError();
        return listing;
    }

    // Sin esto, una unidad extraible sin disco saca el dialogo "Inserte un disco" desde
    // un hilo de trabajo, con la UI viva detras.
    DWORD previousErrorMode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previousErrorMode);
    for (const wchar_t* root = buffer; *root; root += wcslen(root) + 1) {
        DirectoryEntry entry;
        entry.name.assign(root, 2);  // "C:" : lo que unen JoinPath y la memoria de cursor
        entry.attributes = FILE_ATTRIBUTE_DIRECTORY;

        // La etiqueta solo se pinta; el nombre con el que se navega sigue siendo "C:".
        wchar_t label[MAX_PATH + 1] = {};
        std::wstring text = entry.name;
        if (GetVolumeInformationW(root, label, ARRAYSIZE(label), nullptr, nullptr, nullptr,
                                  nullptr, 0) &&
            label[0])
            text += L"  " + std::wstring(label);
        entry.nameUtf8 = ToUtf8(text);

        listing.entries.push_back(std::move(entry));
    }
    SetThreadErrorMode(previousErrorMode, nullptr);

    return listing;  // GetLogicalDriveStringsW ya las devuelve ordenadas
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

// El prefijo \\?\ quita el limite de MAX_PATH, pero a cambio desactiva toda la
// normalizacion (incluido convertir / en \): la ruta ya tiene que venir por NormalizePath.
std::wstring LongPath(const std::wstring& path) {
    if (path.compare(0, 4, L"\\\\?\\") == 0) return path;
    if (path.compare(0, 2, L"\\\\") == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}

std::optional<std::wstring> ParentPath(const std::wstring& path) {
    if (path.empty()) return std::nullopt;         // ya estamos en la lista de unidades
    if (IsDriveRoot(path)) return std::wstring();  // "C:\" -> lista de unidades

    const std::wstring trimmed = TrimTrailingSlashes(path);
    const size_t slash = trimmed.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash == 0) return std::nullopt;

    // La raiz de un recurso de red no tiene padre navegable: por encima solo esta el
    // servidor, que no se enumera con FindFirstFileExW.
    if (trimmed.compare(0, 2, L"\\\\") == 0 && trimmed.find(L'\\', 2) == slash)
        return std::nullopt;

    // "C:\Windows" -> "C:\" : la raiz conserva la barra, si no seria la unidad a secas.
    if (slash == 2 && trimmed[1] == L':') return trimmed.substr(0, 3);
    return trimmed.substr(0, slash);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name + L"\\";  // raiz virtual: "C:" -> "C:\"

    std::wstring out = dir;
    if (out.back() != L'\\') out.push_back(L'\\');
    out += name;
    return out;
}

std::wstring LastComponent(const std::wstring& path) {
    const std::wstring trimmed = TrimTrailingSlashes(path);
    const size_t slash = trimmed.find_last_of(L'\\');
    return slash == std::wstring::npos ? trimmed : trimmed.substr(slash + 1);
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

DirectoryListing ReadDirectory(std::wstring path) {
    if (path.empty()) return ReadDrives();

    DirectoryListing listing;
    listing.path = std::move(path);

    // Igual que en ReadDrives: sin esto, abrir una unidad extraible sin disco saca el
    // dialogo "Inserte un disco" desde un hilo de trabajo. Desde la fase 4 basta con pasar
    // el cursor por encima para llegar aqui, no hace falta ni entrar.
    DWORD previousErrorMode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previousErrorMode);

    WIN32_FIND_DATAW data;
    const HANDLE find =
        FindFirstFileExW(MakeSearchPattern(listing.path).c_str(), FindExInfoBasic, &data,
                         FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        SetThreadErrorMode(previousErrorMode, nullptr);
        // Carpeta vacia: no es un error, solo una lista sin nada.
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) listing.error = error;
        return listing;
    }
    SetThreadErrorMode(previousErrorMode, nullptr);

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
