#pragma once

#include <string>
#include <utility>
#include <vector>

// %APPDATA%\Rayo\<nombre>, creando la carpeta si falta. Vacio si no hay APPDATA.
std::wstring AppFile(const wchar_t* name);

// config.ini y no config.toml: GetPrivateProfileStringW es el parser entero, y escribir uno
// de TOML o JSON serian cien lineas para leer veinte claves.
//
// El archivo se crea en UTF-16 con BOM a proposito: es lo que hace que las funciones W del
// perfil lean y escriban Unicode. Sin el, un marcador con tildes o CJK se perderia.
class Config {
public:
    using Pairs = std::vector<std::pair<std::wstring, std::wstring>>;

    // Crea el archivo con `defaults` si no existe. `path` vacio = %APPDATA%\Rayo\config.ini
    // (lo demas es para la prueba, que no puede tocar la configuracion de verdad).
    void Load(const std::wstring& defaults, std::wstring path = {});

    // Una seccion entera, ya partida en pares y en el orden del archivo.
    Pairs Section(const wchar_t* section) const;
    std::wstring Get(const wchar_t* section, const wchar_t* key,
                     const std::wstring& fallback = {}) const;
    int GetInt(const wchar_t* section, const wchar_t* key, int fallback) const;

    void Set(const wchar_t* section, const wchar_t* key, const std::wstring& value);
    void SetInt(const wchar_t* section, const wchar_t* key, int value);
    // Sustituye la seccion entera: es lo que hace falta para los marcadores, donde una
    // clave puede haber desaparecido.
    void SetSection(const wchar_t* section, const Pairs& entries);

private:
    std::wstring m_path;
};
