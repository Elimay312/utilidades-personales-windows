#pragma once

#include <Windows.h>

#include <mutex>
#include <string>
#include <vector>

#include "core/TaskPool.h"
#include "fs/DirectoryReader.h"
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
    void RequestFrames();
    void RenderFrame();
    void ProcessInput();
    void BuildUi();

    void Navigate(std::wstring path);
    void DrainResults();
    void Execute(Command command);

    Window m_window;
    GraphicsDevice m_gfx;
    int m_pendingFrames = 0;
    bool m_running = true;

    // Estado: la UI solo lo lee, los comandos son los unicos que lo cambian.
    std::wstring m_path;
    std::string m_pathUtf8;
    std::vector<DirectoryEntry> m_entries;
    int m_cursor = 0;
    bool m_scrollToCursor = false;
    int m_visibleRows = 1;
    std::string m_status;  // vacio = sin error
    Keymap::State m_keys;

    // Puente hilo de trabajo -> hilo de UI.
    unsigned long long m_generation = 0;
    std::mutex m_inboxMutex;
    std::vector<DirectoryListing> m_inbox;

    // El ultimo a proposito: al destruirse hace join antes de que mueran el mutex y el
    // inbox que sus tareas usan (los miembros se destruyen en orden inverso).
    TaskPool m_pool;
};
