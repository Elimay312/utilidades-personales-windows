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
#include <deque>
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

// Trabajo de UN repositorio, pedido desde la interfaz. Va por el mismo hilo que la
// sincronización y no por uno propio, y eso no es ahorro: comparten la credencial, el
// cliente y la conexión a SQLite, y dos dueños de una credencial son dos vidas que
// sincronizar. De paso queda serializado, que además es lo que se quiere — un PUT no puede
// correr a la vez que el segundo pase escribiendo la misma fila.
enum class JobKind {
    PushProyecto,  // escribir PROYECTO.md en el repositorio
    ReadFile,      // traer el texto de un .md de la raíz, para copiarlo a las novedades
};

struct Job {
    JobKind kind = JobKind::PushProyecto;
    std::string repoId;
    std::wstring path;  // solo ReadFile
};

// Lo que hay que contarle al usuario cuando un trabajo termina. NO viaja dentro de Progress:
// Snapshot() se llama en cada mensaje y copiaría el texto de un archivo entero cada vez.
struct JobResult {
    JobKind kind = JobKind::PushProyecto;
    std::string repoId;
    std::wstring path;
    bool ok = false;
    // Una frase redactada por nosotros. Nunca un cuerpo de respuesta (SEGURIDAD.md, regla 3).
    std::wstring detail;
    // ReadFile: el contenido pedido. Es del repositorio del usuario y se queda en memoria el
    // rato que tarda en verse en una hoja.
    std::wstring text;
    // PushProyecto: a dónde fue el commit, para poder abrirlo.
    std::wstring commitUrl;
    // PushProyecto: no hizo falta commitear porque el archivo ya decía lo mismo.
    bool unchanged = false;
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
    // Pide un trabajo de un repositorio. Vuelve enseguida: lo hace el trabajador.
    void Enqueue(Job job);
    void UseCredential(Secret credential);
    void SignOut();

    // El hilo de UI llama a esto al recibir el mensaje: primero reconoce —lo que permite que
    // el siguiente cambio vuelva a avisar— y luego lee.
    void Acknowledge();
    Progress Snapshot() const;
    // Saca los trabajos terminados desde la última vez. Se vacía al leer: son avisos, no
    // estado, y repetirlos en cada mensaje sería enseñar dos veces el mismo.
    std::vector<JobResult> TakeFinished();

private:
    // El trabajador: sincroniza si se lo han pedido, vacía la cola y se muere. La decisión
    // de morirse se toma BAJO EL MISMO CANDADO que usa quien encola, y esa es toda la
    // sincronización que hay aquí: sin eso, un trabajo encolado entre la última comprobación
    // y la salida del hilo se quedaría ahí para siempre y no daría ningún error — solo un
    // commit que nunca sube.
    void Run();
    void Wake();
    void RunSync();
    void RunJob(const Job& job);
    void PushProyecto(const Job& job, JobResult& result);
    void ReadFile(const Job& job, JobResult& result);
    void Finish(JobResult result);
    void MarkPushed(const std::string& repoId, const std::wstring& sha,
                    const std::wstring& text);
    // Hay una sincronización en marcha. Antes era un atomic aparte; ahora la verdad está en
    // Progress::running y solo hay un sitio donde mirarla.
    bool Busy() const;
    // Los que quedaron pendientes de una vez anterior. Se reintentan al terminar una
    // sincronización que fue bien.
    void QueuePending();

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

    // La puerta del trabajador. Protege las tres cosas que deciden si hay que arrancar uno o
    // si el que hay puede irse: si se ha pedido una sincronización, qué trabajos esperan, y
    // si hay alguien vivo atendiéndolos.
    std::mutex m_gate;
    std::deque<Job> m_jobs;
    bool m_wantSync = false;
    bool m_worker = false;
    // Si una tanda dice que no hay permiso de Contents, se apaga y se repite sin el blob.
    std::atomic<bool> m_withContents{true};
    // Sin ATOMIC_FLAG_INIT: en C++20 está obsoleto y un atomic_flag nace ya limpio.
    std::atomic_flag m_notified;

    // Lo que tardó cada pase. Los escribe y los lee SOLO el hilo director, entre el final de
    // un pase y el final de Run(), así que no hacen falta ni candado ni átomo.
    int m_passOneMs = 0;
    int m_passTwoMs = 0;

    mutable std::mutex m_mutex;
    Progress m_progress;
    std::vector<JobResult> m_finished;
};

}  // namespace Github
