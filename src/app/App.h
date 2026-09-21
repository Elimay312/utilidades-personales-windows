#pragma once

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/TaskPool.h"
#include "fs/DirectoryReader.h"
#include "fs/ListingCache.h"
#include "platform/GraphicsDevice.h"
#include "platform/Window.h"
#include "preview/PreviewCache.h"
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

    int PreviewTargetPx() const;
    void UpdatePreview();
    void ArmPreview();
    void StartPreview();
    bool CreatePreviewTexture(Preview& preview);

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
    Pane m_preview;  // solo activa cuando el cursor esta sobre una carpeta
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

    // Vista previa del archivo bajo el cursor. Nada se lanza hasta que el cursor lleva
    // quieto kPreviewDelayMs: pasar de largo con j/k no decodifica nada.
    PreviewPtr m_previewFile;
    std::wstring m_previewTarget;         // ruta de lo que hay bajo el cursor
    unsigned long long m_previewDue = 0;  // GetTickCount64 del disparo; 0 = nada pendiente
    int m_previewTargetPx = 0;            // lado mayor del panel, en pixeles fisicos
    PreviewCache m_previewCache;
    // La lee un hilo de trabajo para no empezar un decode que ya nadie quiere.
    std::atomic<unsigned long long> m_previewGen{0};

    // Puente hilo de trabajo -> hilo de UI.
    std::mutex m_inboxMutex;
    std::vector<DirectoryListing> m_inbox;
    std::vector<Preview> m_previewInbox;
    std::vector<std::string> m_messages;

    // El ultimo a proposito: al destruirse hace join antes de que mueran el mutex y el
    // inbox que sus tareas usan (los miembros se destruyen en orden inverso).
    TaskPool m_pool;
};
