// Comprueba lo unico del vigilante que no se ve en pantalla: que avisa, que al dejar de
// vigilar deja de avisar de verdad, y que ir y venir no deja handles colgando.
// Sin framework: se ejecuta con build\rayo_watch_check.exe y no imprime nada si todo va bien.
#undef NDEBUG  // los asserts son la comprobacion: tambien en Release

#include <Windows.h>

#include <cassert>
#include <mutex>
#include <string>
#include <vector>

#include "fs/DirectoryWatcher.h"

namespace {

std::mutex g_mutex;
std::vector<std::wstring> g_seen;
HANDLE g_signal = nullptr;

void OnChanged(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_seen.push_back(path);
    }
    SetEvent(g_signal);
}

size_t SeenCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_seen.size();
}

std::wstring MakeTempDir() {
    wchar_t temp[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    assert(length > 0 && length < MAX_PATH);
    std::wstring dir =
        std::wstring(temp, length) + L"rayo_watch_" + std::to_wstring(GetCurrentProcessId());
    assert(CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
    return dir;
}

void Touch(const std::wstring& dir, const wchar_t* name) {
    const std::wstring file = dir + L"\\" + name;
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(handle != INVALID_HANDLE_VALUE);
    CloseHandle(handle);
}

void Clean(const std::wstring& dir) {
    WIN32_FIND_DATAW found{};
    const HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                DeleteFileW((dir + L"\\" + found.cFileName).c_str());
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(dir.c_str());
}

DWORD HandleCount() {
    DWORD count = 0;
    const BOOL ok = GetProcessHandleCount(GetCurrentProcess(), &count);
    assert(ok);
    (void)ok;
    return count;
}

}  // namespace

int main() {
    const std::wstring dir = MakeTempDir();
    g_signal = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(g_signal);

    {
        DirectoryWatcher watcher;
        watcher.Start(&OnChanged);
        watcher.Watch({dir});

        // Crear un archivo avisa, y avisa con la ruta de la CARPETA, no la del archivo.
        // Se reintenta en vez de dormir a ojo: hasta que el hilo del vigilante no arranca y
        // procesa la APC no hay nada armado, y ese arranque puede tardar en una maquina
        // cargada. Un archivo distinto cada vez, que repetir el mismo no cambia nada.
        bool notified = false;
        for (int i = 0; i < 20 && !notified; ++i) {
            Touch(dir, (L"uno" + std::to_wstring(i) + L".txt").c_str());
            notified = WaitForSingleObject(g_signal, 250) == WAIT_OBJECT_0;
        }
        assert(notified);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            assert(!g_seen.empty());
            assert(g_seen.front() == dir);
        }

        // Dejar de vigilar tiene que cortar los avisos de verdad (CancelIoEx), no ignorarlos
        // con un flag. Con el hilo ya despierto, la APC se procesa enseguida.
        watcher.Watch({});
        Sleep(1000);
        ResetEvent(g_signal);
        const size_t before = SeenCount();
        Touch(dir, L"dos.txt");
        Sleep(500);
        assert(SeenCount() == before);

        // Navegar rapido es vigilar y desvigilar muy seguido. Si el handle de cada carpeta
        // no se cerrara al cancelar, esto lo delataria.
        watcher.Watch({dir});
        Sleep(500);
        const DWORD handlesBefore = HandleCount();
        for (int i = 0; i < 200; ++i) {
            watcher.Watch({});
            watcher.Watch({dir});
        }
        Sleep(1000);
        assert(HandleCount() <= handlesBefore + 8);
    }  // el destructor para el hilo: si dejara alguna E/S viva, esto se colgaria

    CloseHandle(g_signal);
    Clean(dir);
    return 0;
}
