#include "app/Config.h"

#include <Windows.h>

#include <cstdlib>

namespace {

// El buffer inicial cubre cualquier seccion nuestra; la API avisa de que no cabe
// devolviendo justo size-2.
constexpr DWORD kSectionBuffer = 4096;

}  // namespace

std::wstring AppFile(const wchar_t* name) {
    wchar_t roaming[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", roaming, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};

    std::wstring folder(roaming, length);
    folder += L"\\Rayo";
    CreateDirectoryW(folder.c_str(), nullptr);  // si ya existe, ERROR_ALREADY_EXISTS y ya
    return folder + L'\\' + name;
}

void Config::Load(const std::wstring& defaults, std::wstring path) {
    m_path = path.empty() ? AppFile(L"config.ini") : std::move(path);
    if (m_path.empty() || GetFileAttributesW(m_path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    const HANDLE file = CreateFileW(m_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(file, &bom, sizeof(bom), &written, nullptr);
    WriteFile(file, defaults.data(), static_cast<DWORD>(defaults.size() * sizeof(wchar_t)),
              &written, nullptr);
    CloseHandle(file);
}

Config::Pairs Config::Section(const wchar_t* section) const {
    Pairs entries;
    if (m_path.empty()) return entries;

    std::vector<wchar_t> buffer(kSectionBuffer);
    while (GetPrivateProfileSectionW(section, buffer.data(), static_cast<DWORD>(buffer.size()),
                                     m_path.c_str()) == buffer.size() - 2)
        buffer.resize(buffer.size() * 2);

    // "clave=valor\0clave=valor\0\0"
    for (const wchar_t* line = buffer.data(); *line; line += wcslen(line) + 1) {
        const wchar_t* equals = wcschr(line, L'=');
        if (!equals || equals == line) continue;
        entries.emplace_back(std::wstring(line, equals), std::wstring(equals + 1));
    }
    return entries;
}

std::wstring Config::Get(const wchar_t* section, const wchar_t* key,
                         const std::wstring& fallback) const {
    if (m_path.empty()) return fallback;
    wchar_t buffer[1024];
    const DWORD length = GetPrivateProfileStringW(section, key, fallback.c_str(), buffer,
                                                  ARRAYSIZE(buffer), m_path.c_str());
    return std::wstring(buffer, length);
}

// A mano y no GetPrivateProfileIntW: esa devuelve 0 con los negativos, y la posicion de la
// ventana en un monitor a la izquierda del principal lo es.
int Config::GetInt(const wchar_t* section, const wchar_t* key, int fallback) const {
    const std::wstring text = Get(section, key);
    if (text.empty()) return fallback;
    wchar_t* end = nullptr;
    const long value = wcstol(text.c_str(), &end, 10);
    return (end && *end == L'\0') ? static_cast<int>(value) : fallback;
}

void Config::Set(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    if (!m_path.empty()) WritePrivateProfileStringW(section, key, value.c_str(), m_path.c_str());
}

void Config::SetInt(const wchar_t* section, const wchar_t* key, int value) {
    Set(section, key, std::to_wstring(value));
}

void Config::SetSection(const wchar_t* section, const Pairs& entries) {
    if (m_path.empty()) return;

    std::wstring buffer;
    for (const auto& [key, value] : entries) {
        buffer += key;
        buffer += L'=';
        buffer += value;
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    WritePrivateProfileSectionW(section, buffer.c_str(), m_path.c_str());
}
