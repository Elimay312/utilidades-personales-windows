#pragma once

#include <Windows.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/TaskPool.h"
#include "fs/DirectoryReader.h"
#include "fs/ListingCache.h"
#include "platform/GraphicsDevice.h"
#include "platform/Window.h"
#include "ui/Keymap.h"

// Mensajes que los hilos de trabajo usan para despertar el bucle (PostMessageW).
constexpr UINT WM_APP_WAKE = WM_APP + 0;

class App {
public:
    ~App();

    // startPath opcional: sin el se abre la carpeta de usuario.
    bool Init(const wchar_t* startPath);
    int Run();

private:
    // Una columna Miller. La ruta vacia es la raiz virtual (lista de unidades).
    struct Pane {
        bool active = false;   // la raiz virtual no tiene padre: esa columna se apaga
        std::wstring path;
        std::wstring select;   // nombre que debe quedar bajo el cursor tras cada listado
        EntryList entries;     // compartido con la cache
        int cursor = 0;
        bool scrollToCursor = false;
    };

    void RequestFrames();
    void RenderFrame();
    void ProcessInput();
    void BuildUi();

    void Navigate(std::wstring path);
    void GoParent();
    void Open();
    void SetCursor(int cursor);
    const DirectoryEntry* Selected() const;

    void SetPane(Pane& pane, std::wstring path, std::wstring select);
    void ApplyListing(Pane& pane, const EntryList& entries);
    void Request(const std::wstring& path);
    void Report(std::string message, HWND hwnd);
    void DrainResults();
    void Execute(Command command);

    Window m_window;
    GraphicsDevice m_gfx;
    int m_pendingFrames = 0;
    bool m_running = true;

    // Estado: la UI solo lo lee, los comandos son los unicos que lo cambian.
    Pane m_current;
    Pane m_parent;
    std::string m_pathUtf8;
    int m_visibleRows = 1;
    std::string m_status;  // vacio = sin error
    Keymap::State m_keys;

    ListingCache m_cache;
    // Ruta -> nombre seleccionado. Solo durante la sesion: volver a una carpeta deja el
    // cursor donde estaba.
    std::unordered_map<std::wstring, std::wstring> m_cursorMemory;
    // Rutas pedidas y aun sin respuesta: entrar y salir a lo bruto no encola la misma
    // lectura veinte veces por delante de la carpeta a la que el usuario acaba de llegar.
    std::vector<std::wstring> m_inFlight;

    // Puente hilo de trabajo -> hilo de UI.
    std::mutex m_inboxMutex;
    std::vector<DirectoryListing> m_inbox;
    std::vector<std::string> m_messages;

    // El ultimo a proposito: al destruirse hace join antes de que mueran el mutex y el
    // inbox que sus tareas usan (los miembros se destruyen en orden inverso).
    TaskPool m_pool;
};
