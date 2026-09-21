#include "github/Sync.h"

#include "github/Gh.h"
#include "github/Parse.h"
#include "github/Query.h"
#include "model/Utf.h"
#include "projectfile/Proyecto.h"
#include "store/Schema.h"

namespace Github {
namespace {

constexpr const char* kSettingAccount = "cuenta";
constexpr const char* kSettingLastSync = "ultima_sync";
constexpr const char* kSettingScopes = "alcance";
constexpr const char* kSettingContents = "sin_contenidos";
constexpr const char* kSettingProtocol = "protocolo";
// Cuánto tardó cada pase y sobre cuántos repositorios. La fase 3 decidió no tener log —la
// regla 3 de SEGURIDAD.md— y que ese papel lo hiciera la tabla de ajustes; esto es lo mismo
// para el tiempo. Hace falta porque la fase 5 le metió al pase 2 los cinco commits y el
// listado de la raíz, y «va parecido» no es una medida: si se dispara, hay que verlo.
constexpr const char* kSettingPassOneMs = "ms_pase1";
constexpr const char* kSettingPassTwoMs = "ms_pase2";
constexpr const char* kSettingDetailCount = "repos_detalle";

int MillisSince(std::chrono::steady_clock::time_point start) {
    const auto spent = std::chrono::steady_clock::now() - start;
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(spent).count());
}

// Setting() devuelve un Result, y llamar a Value() sobre uno fallido lanza. Al arrancar eso
// sería una excepción antes de que exista la ventana, así que aquí "no se pudo leer" y "no
// hay nada guardado" se tratan igual: cadena vacía.
std::string SettingOr(Store::Repos& repos, const char* key) {
    Model::Result<std::string> value = repos.Setting(key);
    return value.IsOk() ? value.Value() : std::string();
}

Model::Instant Now() {
    return std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::system_clock::now());
}

// La frase que se enseña de un error. Nunca un cuerpo de respuesta: 'detail' lo redactamos
// nosotros (SEGURIDAD.md, regla 3) y esto solo le pone delante de qué clase es.
std::wstring Phrase(const Model::Error& error) {
    return std::wstring(Model::NameOf(error.kind)) + L". " + error.detail;
}

}  // namespace

Sync::~Sync() {
    Close();
}

bool Sync::Create(HWND hwnd, UINT message, const std::string& databasePathUtf8) {
    m_hwnd = hwnd;
    m_message = message;
    m_databasePath = databasePathUtf8;

    if (Model::Outcome opened = m_db.Open(m_databasePath); !opened) {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.stage = Stage::Failed;
        m_progress.error = opened.Err();
        return false;
    }
    if (Model::Outcome migrated = Store::Migrate(m_db); !migrated) {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.stage = Stage::Failed;
        m_progress.error = migrated.Err();
        return false;
    }

    // Lo que se sabía al cerrar la última vez, para que el panel no salga en blanco mientras
    // no haya sincronizado nada.
    Store::Repos repos(m_db);
    std::lock_guard<std::mutex> guard(m_mutex);
    m_progress.account = Model::ToWide(SettingOr(repos, kSettingAccount));
    m_progress.contentsForbidden = !SettingOr(repos, kSettingContents).empty();
    if (const std::string stamp = SettingOr(repos, kSettingLastSync); !stamp.empty()) {
        m_progress.lastSync = Model::ParseIso8601(stamp);
    }
    return true;
}

void Sync::Close() {
    m_stop.store(true, std::memory_order_relaxed);
    m_client.Cancel();

    // Unir antes de tocar nada más. A partir de aquí no hay ningún hilo nuestro vivo, que es
    // lo que permite que la ventana se cierre sin que nadie le escriba encima.
    if (m_thread.joinable()) m_thread.join();

    m_client.Close();
    m_credential.Clear();
    m_db.Close();
    m_hwnd = nullptr;
}

void Sync::Start() {
    if (Stopping()) return;
    {
        std::lock_guard<std::mutex> gate(m_gate);
        // Ya pedida y todavía sin empezar: pulsar Ctrl+R dos veces es pedirlo una vez.
        if (m_wantSync) return;
        m_wantSync = true;
    }
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_progress.running) return;
        m_progress.running = true;
        m_progress.stage = Stage::Connecting;
        m_progress.error.reset();
        m_progress.warnings.clear();
        m_progress.needsCredential = false;
        m_progress.pages = 0;
        m_progress.reposSeen = 0;
        m_progress.toEnrich = 0;
        m_progress.enriched = 0;
        m_progress.gone = 0;
    }
    Notify();
    Wake();
}

void Sync::Enqueue(Job job) {
    if (Stopping()) return;
    {
        std::lock_guard<std::mutex> gate(m_gate);
        m_jobs.push_back(std::move(job));
    }
    Wake();
}

void Sync::Wake() {
    if (Stopping() || m_hwnd == nullptr) return;
    std::lock_guard<std::mutex> gate(m_gate);
    // Si hay trabajador vivo, ya lo recogerá: la comprobación de si queda algo la hace él
    // bajo este mismo candado justo antes de morirse.
    if (m_worker) return;
    // El anterior ya terminó pero sigue siendo joinable: hay que unirlo antes de asignar
    // otro, o std::thread llama a terminate.
    if (m_thread.joinable()) m_thread.join();
    m_worker = true;
    m_thread = std::thread(&Sync::Run, this);
}

void Sync::Run() {
    for (;;) {
        bool sync = false;
        Job job;
        bool hasJob = false;
        {
            std::lock_guard<std::mutex> gate(m_gate);
            if (Stopping()) {
                m_worker = false;
                return;
            }
            if (m_wantSync) {
                m_wantSync = false;
                sync = true;
            } else if (!m_jobs.empty()) {
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
                hasJob = true;
            } else {
                // Aquí, y solo aquí, se decide morirse: bajo el mismo candado que usa quien
                // encola. Fuera de él, un trabajo que llegara entre la comprobación y el
                // return se quedaría en la cola sin nadie que lo recogiera.
                m_worker = false;
                return;
            }
        }
        if (sync) {
            RunSync();
        } else if (hasJob) {
            RunJob(job);
        }
    }
}

void Sync::UseCredential(Secret credential) {
    // Llega del hilo de UI y solo cuando no hay sincronización en marcha, que es cuando la
    // hoja de bienvenida está abierta.
    if (Busy()) return;

    m_credential = std::move(credential);
    m_credentialIsOurs = true;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.needsCredential = false;
        m_progress.account.clear();
    }
}

void Sync::SignOut() {
    if (Busy()) return;

    Vault::Forget();
    m_credential.Clear();
    m_credentialIsOurs = false;

    {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        Store::Repos repos(m_db);
        repos.SetSetting(kSettingAccount, std::string());
        repos.SetSetting(kSettingScopes, std::string());
    }
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.account.clear();
        m_progress.needsCredential = true;
        m_progress.stage = Stage::Idle;
        m_progress.error.reset();
    }
    // Los repositorios y las notas NO se borran: cerrar sesión no es "borra mis notas".
    Notify();
}

void Sync::Acknowledge() {
    // Se limpia ANTES de que el hilo de UI lea. Si el trabajador cambia algo entre medias,
    // volverá a publicar y habrá otra vuelta; al revés se perdería el último cambio.
    m_notified.clear(std::memory_order_release);
}

Progress Sync::Snapshot() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_progress;
}

void Sync::Notify() {
    if (m_hwnd == nullptr) return;
    // Un solo mensaje por tanda: seis trozos terminando a la vez publicarían seis. Es el
    // mismo truco que Ui::Painter usa para juntar los repintados de un mismo evento.
    if (!m_notified.test_and_set(std::memory_order_acq_rel)) {
        PostMessageW(m_hwnd, m_message, 0, 0);
    }
}

// ------------------------------------------------------------------ La credencial --

Model::Outcome Sync::EnsureCredential() {
    if (m_credential.Empty()) {
        // 1. La que el usuario pegó alguna vez.
        Model::Result<Secret> stored = Vault::Load();
        if (stored.IsOk() && !stored.Value().Empty()) {
            m_credential = stored.Take();
            m_credentialIsOurs = true;
        }
    }
    if (m_credential.Empty()) {
        // 2. La de GitHub CLI. Cuesta ~570 ms y por eso se paga aquí, en el trabajador, y
        //    una sola vez por arranque.
        Model::Result<Secret> fromCli = Gh::AskForCredential();
        if (fromCli.IsOk() && !fromCli.Value().Empty()) {
            m_credential = fromCli.Take();
            // No se guarda: ya tiene dueño, y el mejor sitio para una credencial es el de
            // otro que ya la gestiona bien.
            m_credentialIsOurs = false;
        }
    }
    if (m_credential.Empty()) {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.needsCredential = true;
        return Model::Oops(Model::Fail::Auth, L"Hace falta conectar una cuenta de GitHub");
    }

    // Se valida solo si no sabemos ya de quién es. Validar en cada sincronización costaría
    // una petición de las ocho para no enterarse de nada nuevo.
    bool known = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        known = !m_progress.account.empty();
    }
    if (known) return Model::Ok();

    Model::Result<Response> answered = m_client.PostGraphQL(m_credential, ViewerBody());
    if (!answered) return answered.Err();

    Response response = answered.Take();
    Model::Result<std::wstring> login = ParseViewerLogin(response.body);
    if (!login) {
        if (login.Err().kind == Model::Fail::Auth) {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_progress.needsCredential = true;
        }
        return login.Err();
    }

    const std::wstring account = login.Take();
    // Solo aquí, con la credencial ya comprobada, se guarda la que pegó el usuario. Guardar
    // antes de validar dejaría una credencial rota en la caja fuerte.
    if (m_credentialIsOurs) {
        if (Model::Outcome saved = Vault::Save(m_credential); !saved) return saved;
    }

    {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        Store::Repos repos(m_db);
        repos.SetSetting(kSettingAccount, Model::ToUtf8(account));
    }
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.account = account;
    }
    Notify();
    return Model::Ok();
}

// ----------------------------------------------------------------------- Pase 1 --

Model::Outcome Sync::PassOne(std::int64_t seq) {
    const auto started = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.stage = Stage::Metadata;
    }
    Notify();

    std::optional<std::string> cursor;
    for (;;) {
        if (Stopping()) return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");

        Model::Result<Response> answered =
            m_client.PostGraphQL(m_credential, MetadataBody(cursor));
        if (!answered) return answered.Err();

        Response response = answered.Take();
        Model::Result<Page> parsed = ParseMetadata(response.body, false);
        if (!parsed) return parsed.Err();

        Page page = parsed.Take();
        {
            std::lock_guard<std::mutex> lock(m_dbMutex);
            Store::Repos repos(m_db);
            if (Model::Outcome written = repos.UpsertMetadata(page.repos, seq, Now()); !written) {
                return written;
            }
        }

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_progress.pages;
            m_progress.reposSeen += static_cast<int>(page.repos.size());
            for (std::wstring& warning : page.warnings) {
                m_progress.warnings.push_back(std::move(warning));
            }
            m_progress.limits = m_client.LastLimits();
        }
        // Cada página avisa: la lista de la fase 4 se irá llenando mientras llega la segunda
        // en vez de aparecer de golpe al final.
        Notify();

        if (!page.hasNextPage || page.endCursor.empty()) break;
        cursor = page.endCursor;
    }

    m_passOneMs = MillisSince(started);
    return Model::Ok();
}

// ----------------------------------------------------------------------- Pase 2 --

void Sync::EnrichChunks(const std::vector<std::vector<std::string>>& chunks) {
    std::atomic<std::size_t> next{0};

    auto worker = [this, &chunks, &next]() {
        for (;;) {
            const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= chunks.size()) return;
            if (Stopping()) return;

            const std::vector<std::string>& ids = chunks[index];

            bool withContents = m_withContents.load(std::memory_order_relaxed);
            Batch batch;

            // Dos vueltas como mucho: si la credencial no tiene Contents: read, la primera lo
            // dice y la segunda va sin el blob. Así una credencial estrecha sincroniza sin
            // PROYECTO.md en vez de no sincronizar.
            for (int round = 0; round < 2; ++round) {
                Model::Result<Response> answered =
                    m_client.PostGraphQL(m_credential, DetailBody(ids, withContents));
                if (!answered) {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    if (!m_progress.error.has_value()) m_progress.error = answered.Err();
                    return;
                }

                Response response = answered.Take();
                Model::Result<Batch> parsed = ParseDetail(response.body);
                if (!parsed) {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    if (!m_progress.error.has_value()) m_progress.error = parsed.Err();
                    return;
                }

                batch = parsed.Take();
                if (!batch.contentsForbidden || !withContents) break;

                m_withContents.store(false, std::memory_order_relaxed);
                withContents = false;
            }

            if (batch.contentsForbidden) {
                std::lock_guard<std::mutex> guard(m_mutex);
                m_progress.contentsForbidden = true;
            }

            {
                std::lock_guard<std::mutex> lock(m_dbMutex);
                // Una transacción por tanda. Desde la fase 5 cada repositorio son tres
                // escrituras —la fila, sus cinco commits y sus .md de la raíz—, así que veinte
                // repositorios sueltos serían sesenta confirmaciones a disco. Y de paso una
                // tanda entra entera o no entra: un repositorio con la fila puesta al día y
                // los commits del mes pasado no da ningún error, solo miente.
                Store::Transaction tx(m_db);
                if (tx.Begin().IsOk()) {
                    Store::Repos repos(m_db);
                    for (const Model::Repo& repo : batch.repos) repos.ApplyEnrichment(repo);
                    (void)tx.Commit();
                }
            }

            {
                std::lock_guard<std::mutex> guard(m_mutex);
                m_progress.enriched += static_cast<int>(batch.repos.size());
                for (std::wstring& warning : batch.warnings) {
                    m_progress.warnings.push_back(std::move(warning));
                }
                m_progress.limits = m_client.LastLimits();
            }
            Notify();
        }
    };

    // Seis hilos. Medido: 6 peticiones de 20 en paralelo son 2,22 s frente a los 11,02 s de
    // 3 de 50 en serie. Más no ayudaría —la latencia ya no es el cuello— y acercaría el
    // límite secundario de peticiones concurrentes de GitHub.
    const std::size_t count =
        chunks.size() < static_cast<std::size_t>(kParallelRequests)
            ? chunks.size()
            : static_cast<std::size_t>(kParallelRequests);

    std::vector<std::thread> crew;
    crew.reserve(count);
    for (std::size_t i = 0; i < count; ++i) crew.emplace_back(worker);
    for (std::thread& hand : crew) hand.join();
}

Model::Outcome Sync::PassTwo() {
    std::vector<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        Store::Repos repos(m_db);
        Model::Result<std::vector<std::string>> needing = repos.NeedingEnrichment();
        if (!needing) return needing.Err();
        pending = needing.Take();
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.stage = Stage::Detail;
        m_progress.toEnrich = static_cast<int>(pending.size());
    }
    Notify();

    // Y aquí es donde lo incremental se nota: con los datos de un día normal esta lista
    // viene vacía y el pase 2 no hace ni una petición.
    if (pending.empty()) {
        m_passTwoMs = 0;
        return Model::Ok();
    }

    const auto started = std::chrono::steady_clock::now();
    EnrichChunks(Chunk(pending, kDetailChunk));
    m_passTwoMs = MillisSince(started);

    if (Stopping()) return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_progress.error.has_value()) return *m_progress.error;
    return Model::Ok();
}

// ------------------------------------------------ Los trabajos de un solo repositorio --

bool Sync::Busy() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_progress.running;
}

std::vector<JobResult> Sync::TakeFinished() {
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<JobResult> out;
    out.swap(m_finished);
    return out;
}

void Sync::Finish(JobResult result) {
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_finished.push_back(std::move(result));
    }
    Notify();
}

void Sync::MarkPushed(const std::string& repoId, const std::wstring& sha,
                      const std::wstring& text) {
    std::lock_guard<std::mutex> lock(m_dbMutex);
    Store::Repos repos(m_db);
    // Lo que de verdad hay ahora en el repositorio. Sin esto, el siguiente guardado fusionaría
    // sobre el texto de la última sincronización y desharía lo que se acaba de subir.
    repos.SaveProyectoBlob(repoId, sha, text);
    repos.SetPushPending(repoId, false);
}

void Sync::RunJob(const Job& job) {
    JobResult result;
    result.kind = job.kind;
    result.repoId = job.repoId;
    result.path = job.path;

    if (Model::Outcome opened = m_client.Open(); !opened) {
        result.detail = Phrase(opened.Err());
        Finish(std::move(result));
        return;
    }
    if (Model::Outcome credential = EnsureCredential(); !credential) {
        result.detail = Phrase(credential.Err());
        Finish(std::move(result));
        return;
    }

    switch (job.kind) {
    case JobKind::PushProyecto:
        PushProyecto(job, result);
        break;
    case JobKind::ReadFile:
        ReadFile(job, result);
        break;
    }
    Finish(std::move(result));
}

void Sync::PushProyecto(const Job& job, JobResult& result) {
    Model::Repo repo;
    Model::Local local;
    std::vector<Model::Novedad> novedades;
    {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        Store::Repos repos(m_db);

        Model::Result<Model::Repo> got = repos.RepoOf(job.repoId);
        if (!got) {
            result.detail = Phrase(got.Err());
            return;
        }
        repo = got.Take();

        Model::Result<Model::Local> mine = repos.LocalOf(job.repoId);
        if (!mine) {
            result.detail = Phrase(mine.Err());
            return;
        }
        local = mine.Take();

        if (Model::Result<std::vector<Model::Novedad>> notes = repos.NovedadesOf(job.repoId);
            notes.IsOk()) {
            novedades = notes.Take();
        }
    }

    // Las dos, y las dos hacen falta: el interruptor Y la confirmación de ESE repositorio
    // (SEGURIDAD.md, regla 5). Se vuelve a comprobar aquí, en el trabajador, y no solo en la
    // interfaz: entre que se encoló y se atiende, el usuario ha podido apagarlo.
    if (!local.repoMode || !local.repoConfirmed) {
        result.detail = L"Ese repositorio no tiene el modo repo confirmado";
        return;
    }

    const std::wstring path = ContentsPath(repo.nameWithOwner);
    if (path.empty()) {
        result.detail = L"El nombre de ese repositorio no tiene la forma que espera GitHub";
        return;
    }

    std::wstring remoteRaw = repo.proyectoText;
    std::wstring sha = repo.proyectoOid;

    // Dos vueltas como mucho: la primera con lo que teníamos guardado, y si el archivo cambió
    // debajo, la segunda con lo que acaba de llegar. Más vueltas serían un bucle contra
    // alguien que está escribiendo a la vez, y de eso no se sale insistiendo.
    for (int round = 0; round < 2; ++round) {
        if (Stopping()) {
            result.detail = L"Cancelado";
            return;
        }

        const std::wstring text = Proyecto::Render(
            Proyecto::Merge(Proyecto::Parse(remoteRaw), local, novedades, Now()));

        // Un commit que no cambia nada no se hace. Tapa además el caso feo del reintento de
        // red: si la respuesta de un PUT se perdió, el commit ya existe, y al releer sale
        // exactamente esto en vez de un segundo commit idéntico.
        if (text == remoteRaw) {
            MarkPushed(job.repoId, sha, text);
            result.ok = true;
            result.unchanged = true;
            result.detail = L"PROYECTO.md ya decía lo mismo: no hizo falta ningún commit";
            return;
        }

        Model::Result<Response> answered = m_client.Rest(
            L"PUT", path, m_credential, ContentsBody(text, sha, repo.defaultBranch));

        if (answered.IsOk()) {
            const Response response = answered.Take();
            Model::Result<Written> written = ParseContentsWrite(response.body);
            if (!written) {
                result.detail = Phrase(written.Err());
                return;
            }
            MarkPushed(job.repoId, written.Value().blobSha, text);
            result.ok = true;
            result.commitUrl = written.Value().commitUrl;
            result.detail = L"PROYECTO.md actualizado en " + repo.nameWithOwner;
            return;
        }

        const Model::Error error = answered.Err();
        const bool conflict =
            error.kind == Model::Fail::Http && (error.code == 409 || error.code == 422);
        if (!conflict || round == 1) {
            // El pendiente se queda puesto: lo escrito está en SQLite desde antes de la
            // primera petición, así que no se pierde nada — solo tarda en llegar allí.
            result.detail = Phrase(error);
            return;
        }

        Model::Result<Response> reread =
            m_client.PostGraphQL(m_credential, FileBody(job.repoId, kProyectoFile));
        if (!reread) {
            result.detail = Phrase(reread.Err());
            return;
        }
        Model::Result<FileText> fresh = ParseFileText(reread.Value().body);
        if (!fresh) {
            result.detail = Phrase(fresh.Err());
            return;
        }
        remoteRaw = fresh.Value().text;
        sha = fresh.Value().oid;
    }
}

void Sync::ReadFile(const Job& job, JobResult& result) {
    Model::Result<Response> answered =
        m_client.PostGraphQL(m_credential, FileBody(job.repoId, job.path));
    if (!answered) {
        result.detail = Phrase(answered.Err());
        return;
    }

    Model::Result<FileText> file = ParseFileText(answered.Value().body);
    if (!file) {
        result.detail = Phrase(file.Err());
        return;
    }
    if (!file.Value().found) {
        result.detail = L"Ese archivo ya no está en el repositorio";
        return;
    }
    if (file.Value().truncated) {
        // Medio archivo copiado a las novedades es peor que ninguno: parece entero.
        result.detail = L"Ese archivo es demasiado grande para traerlo entero";
        return;
    }
    result.ok = true;
    result.text = file.Take().text;
}

void Sync::QueuePending() {
    std::vector<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        Store::Repos repos(m_db);
        if (Model::Result<std::vector<std::string>> got = repos.PendingPushes(); got.IsOk()) {
            pending = got.Take();
        }
    }
    if (pending.empty()) return;

    // Van a la cola y no se hacen aquí: el bucle del trabajador los recoge en cuanto vuelve,
    // y así un pendiente se cancela igual que cualquier otro trabajo al cerrar la ventana.
    std::lock_guard<std::mutex> gate(m_gate);
    for (std::string& id : pending) {
        Job job;
        job.kind = JobKind::PushProyecto;
        job.repoId = std::move(id);
        m_jobs.push_back(std::move(job));
    }
}

// ---------------------------------------------------------------- El hilo director --

void Sync::RunSync() {
    Model::Outcome result = Model::Ok();

    do {
        if (Model::Outcome opened = m_client.Open(); !opened) {
            result = opened;
            break;
        }
        if (Model::Outcome credential = EnsureCredential(); !credential) {
            result = credential;
            break;
        }

        // El cinturón de la cuota. Con ocho puntos por sincronización y cinco mil por hora
        // esto no salta nunca, y por eso mismo tiene que estar: si algún día salta, será
        // porque algo está pidiendo de más en un bucle.
        const Limits limits = m_client.LastLimits();
        if (limits.remaining >= 0 && limits.remaining < Client::kLowWater) {
            Model::Error tooMuch =
                Model::Oops(Model::Fail::RateLimit, L"Queda muy poca cuota de la API de GitHub");
            if (limits.reset.has_value()) tooMuch.retry = *limits.reset;
            result = tooMuch;
            break;
        }

        std::int64_t seq = 0;
        {
            std::lock_guard<std::mutex> lock(m_dbMutex);
            Store::Repos repos(m_db);
            Model::Result<std::int64_t> opened = repos.BeginSync();
            if (!opened) {
                result = opened.Err();
                break;
            }
            seq = opened.Value();
        }

        if (Model::Outcome one = PassOne(seq); !one) {
            result = one;
            break;
        }

        {
            std::lock_guard<std::mutex> lock(m_dbMutex);
            Store::Repos repos(m_db);
            Model::Result<int> missing = repos.MarkMissing(seq, Now());
            if (missing.IsOk()) {
                std::lock_guard<std::mutex> guard(m_mutex);
                m_progress.gone = missing.Value();
            }
        }

        result = PassTwo();
    } while (false);

    const Model::Instant finished = Now();
    if (result.IsOk()) {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        Store::Repos repos(m_db);
        repos.SetSetting(kSettingLastSync, Model::FormatIso8601(finished));
        repos.SetSetting(kSettingContents,
                         m_withContents.load(std::memory_order_relaxed) ? std::string() : "1");
        // Qué protocolo se acabó usando. Es la comprobación de que el ajuste de HTTP/2 hace
        // algo: sin él, las seis peticiones del segundo pase van por turnos y tardan el
        // triple sin dar ni un error.
        repos.SetSetting(kSettingProtocol, m_client.LastLimits().http2 ? "HTTP/2" : "HTTP/1.1");
        // Lo que tardó cada pase, y sobre cuántos repositorios tardó el segundo: un pase 2 de
        // cero segundos sobre cero repositorios no dice nada, y es el caso normal.
        repos.SetSetting(kSettingPassOneMs, std::to_string(m_passOneMs));
        repos.SetSetting(kSettingPassTwoMs, std::to_string(m_passTwoMs));
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            repos.SetSetting(kSettingDetailCount, std::to_string(m_progress.toEnrich));
        }
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.running = false;
        m_progress.limits = m_client.LastLimits();
        if (result.IsOk()) {
            m_progress.stage = Stage::Done;
            m_progress.lastSync = finished;
            m_progress.error.reset();
        } else {
            m_progress.stage = Stage::Failed;
            m_progress.error = result.Err();
        }
    }

    Notify();
    // Y los que se quedaron a medias otro día. Va al final y solo si todo fue bien: con un
    // error por delante, lo que falla es la red y reintentar diez commits es empujar a algo
    // que ya no puede.
    if (result.IsOk()) QueuePending();
}

}  // namespace Github
