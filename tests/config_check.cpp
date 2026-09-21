// Lo unico de la fase 8 que no se ve en pantalla hasta que ya ha decidido mal: que el
// config se relea igual que se escribio. El riesgo real es Unicode: las funciones del
// perfil solo escriben UTF-16 si el archivo ya lo es, y un marcador a una carpeta con
// tildes o CJK se perderia sin ruido.
//
// No imprime nada si todo va bien.

#include <Windows.h>

#include <assert.h>

#include <string>

#include "app/Config.h"

namespace {

std::wstring TempFile() {
    wchar_t folder[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, folder);
    assert(length > 0 && length < MAX_PATH);
    return std::wstring(folder, length) + L"rayo_config_check.ini";
}

}  // namespace

int main() {
    const std::wstring path = TempFile();
    DeleteFileW(path.c_str());

    const std::wstring defaults =
        L"[colors]\r\naccent=4fc1ff\r\n\r\n[keys]\r\nJ=MoveDown\r\nCtrl+D=HalfPageDown\r\n";
    {
        Config config;
        config.Load(defaults, path);

        // Se crea con los valores por defecto y se lee lo que se escribio.
        assert(config.Get(L"colors", L"accent") == L"4fc1ff");
        const Config::Pairs keys = config.Section(L"keys");
        assert(keys.size() == 2);
        assert(keys[0].first == L"J" && keys[0].second == L"MoveDown");
        assert(keys[1].first == L"Ctrl+D" && keys[1].second == L"HalfPageDown");

        // Una clave que no esta se queda en el valor de reserva.
        assert(config.Get(L"colors", L"nada", L"reserva") == L"reserva");
        assert(config.GetInt(L"window", L"x", -7) == -7);

        config.SetInt(L"window", L"x", -1234);  // negativo: un monitor a la izquierda
        config.SetInt(L"options", L"showHidden", 1);
        config.SetSection(L"bookmarks",
                          {{L"c", L"C:\\Users\\Canción\\音楽"}, {L"j", L"C:\\Users\\Peña"}});
    }
    {
        // Otra instancia, releyendo el mismo archivo: es lo que pasa entre dos sesiones.
        Config config;
        config.Load(L"esto no deberia escribirse", path);

        assert(config.GetInt(L"window", L"x", 0) == -1234);
        assert(config.GetInt(L"options", L"showHidden", 0) == 1);

        const Config::Pairs bookmarks = config.Section(L"bookmarks");
        assert(bookmarks.size() == 2);
        assert(bookmarks[0].first == L"c");
        assert(bookmarks[0].second == L"C:\\Users\\Canción\\音楽");
        assert(bookmarks[1].second == L"C:\\Users\\Peña");

        // Sustituir la seccion entera quita lo que ya no esta.
        config.SetSection(L"bookmarks", {{L"j", L"C:\\Temp"}});
        const Config::Pairs left = config.Section(L"bookmarks");
        assert(left.size() == 1 && left[0].first == L"j");

        // Y lo de fuera de la seccion no se ha movido.
        assert(config.Get(L"colors", L"accent") == L"4fc1ff");
    }

    DeleteFileW(path.c_str());
    return 0;
}
