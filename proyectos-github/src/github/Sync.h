#pragma once

// La sincronización: un hilo director, dos pases, y un aviso al hilo de UI.
//
// El puente es el mismo que estrenó Shell::ThemeWatcher en la fase 1, y su cabecera ya decía
// que tendría este aspecto: el trabajador NO toca nada de la interfaz. Escribe en SQLite y
// publica un PostMessageW sin carga; el hilo de UI relee. Un WPARAM con un puntero dentro
// sería un puntero cruzando hilos, y el día que llegue tarde apuntará a algo muerto.
//
// Los dos pases y sus números están medidos contra la cuenta de verdad, y son el motivo de
// que esto no sea una sola consulta paginada:
//
//   Pase 1  metadatos por cursor, en serie       ~1,3 s los 109 repositorios
//   Pase 2  detalle por nodes(ids:), 6 en paralelo de 20   ~2,2 s
//
// El segundo pase solo pide los repositorios cuyo pushedAt cambió, así que la segunda
// sincronización del día no hace ninguna petición de detalle.

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "github/Auth.h"
#include "github/Client.h"
#include "model/Result.h"
#include "store/Repos.h"

namespace Github {

enum class Stage {
    Idle,
    Connecting,   // buscando y validando la credencial
    Metadata,     // pase 1
    Detail,       // pase 2
    Done,
    Failed,
};

struct Progress {
    Stage stage = Stage::Idle;
    bool running = false;
    // No hay credencial y hay que pedirla: es la señal de abrir la hoja de bienvenida.
    bool needsCredential = false;
    // La credencial no tiene Contents: read, así que se sincroniza sin PROYECTO.md.
    bool contentsForbidden = false;

    std::wstring account;
    int pages = 0;
    int reposSeen = 0;
    int toEnrich = 0;
    int enriched = 0;
    int gone = 0;

    std::optional<Model::Instant> lastSync;
    std::optional<Model::Error> error;
    std::vector<std::wstring> warnings;
    Limits limits;
};

class Sync {
public:
    Sync() = default;
    ~Sync();

    Sync(const Sync&) = delete;
    Sync& operator=(const Sync&) = delete;

    // 'message' se publica en 'hwnd' cada vez que hay algo nuevo que leer. Igual que
    // ThemeWatcher: el mensaje no lleva carga, solo dice "vuelve a mirar".
    bool Create(HWND hwnd, UINT message, const std::string& databasePathUtf8);

    // Cancela, espera a los hilos y cierra. App::Shutdown lo llama ANTES de cerrar la
    // ventana: un trabajador que sobreviva a la ventana es el cuelgue más probable de la fase.
    void Close();

    // --- Desde el hilo de UI ---
    void Start();
    void UseCredential(Secret credential);
    void SignOut();

    // El hilo de UI llama a esto al recibir el mensaje: primero reconoce —lo que permite que
    // el siguiente cambio vuelva a avisar— y luego lee.
    void Acknowledge();
    Progress Snapshot() const;

private:
    void Run();
    void Notify();

    Model::Outcome EnsureCredential();
    Model::Outcome PassOne(std::int64_t seq);
    Model::Outcome PassTwo();
    void EnrichChunks(const std::vector<std::vector<std::string>>& chunks);

    bool Stopping() const { return m_stop.load(std::memory_order_relaxed); }

    HWND m_hwnd = nullptr;
    UINT m_message = 0;
    std::string m_databasePath;

    Client m_client;
    Secret m_credential;
    // La credencial del usuario se guarda en la caja fuerte; la de GitHub CLI no, porque ya
    // tiene dueño (SEGURIDAD.md).
    bool m_credentialIsOurs = false;

    // El trabajador es el único que abre esta base. El hilo de UI tiene la suya para leer.
    Store::Db m_db;
    std::mutex m_dbMutex;

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    // Si una tanda dice que no hay permiso de Contents, se apaga y se repite sin el blob.
    std::atomic<bool> m_withContents{true};
    // Sin ATOMIC_FLAG_INIT: en C++20 está obsoleto y un atomic_flag nace ya limpio.
    std::atomic_flag m_notified;

    mutable std::mutex m_mutex;
    Progress m_progress;
};

}  // namespace Github
