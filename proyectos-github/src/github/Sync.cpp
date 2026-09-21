#include "github/Sync.h"

#include "github/Gh.h"
#include "github/Parse.h"
#include "github/Query.h"
#include "model/Utf.h"
#include "store/Schema.h"

namespace Github {
namespace {

constexpr const char* kSettingAccount = "cuenta";
constexpr const char* kSettingLastSync = "ultima_sync";
constexpr const char* kSettingScopes = "alcance";
constexpr const char* kSettingContents = "sin_contenidos";
constexpr const char* kSettingProtocol = "protocolo";

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
    if (m_running.load(std::memory_order_acquire)) return;
    if (Stopping()) return;

    // El hilo anterior ya terminó pero sigue siendo joinable: hay que unirlo antes de
    // asignar otro, o std::thread llama a terminate.
    if (m_thread.joinable()) m_thread.join();

    m_running.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
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

    m_thread = std::thread(&Sync::Run, this);
}

void Sync::UseCredential(Secret credential) {
    // Llega del hilo de UI y solo cuando no hay sincronización en marcha, que es cuando la
    // hoja de bienvenida está abierta.
    if (m_running.load(std::memory_order_acquire)) return;

    m_credential = std::move(credential);
    m_credentialIsOurs = true;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_progress.needsCredential = false;
        m_progress.account.clear();
    }
}

void Sync::SignOut() {
    if (m_running.load(std::memory_order_acquire)) return;

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
                Store::Repos repos(m_db);
                for (const Model::Repo& repo : batch.repos) repos.ApplyEnrichment(repo);
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
    if (pending.empty()) return Model::Ok();

    EnrichChunks(Chunk(pending, kDetailChunk));

    if (Stopping()) return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_progress.error.has_value()) return *m_progress.error;
    return Model::Ok();
}

// ---------------------------------------------------------------- El hilo director --

void Sync::Run() {
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

    m_running.store(false, std::memory_order_release);
    Notify();
}

}  // namespace Github
