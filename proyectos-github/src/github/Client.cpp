#include "github/Client.h"

#include <Windows.h>

namespace Github {
namespace {

const std::wstring kGraphQlPath = L"/graphql";

// Una fuente de dispersión que no necesita <random> ni semilla: lo que se pide de ella es
// que seis hilos que fallan a la vez no vuelvan a la vez, no que sea impredecible.
std::uint32_t Spread() {
    return static_cast<std::uint32_t>(GetTickCount64()) * 2654435761u;
}

Model::Error FromStatus(const Response& response) {
    Model::Error error;
    error.code = response.status;
    error.requestId = response.requestId;

    switch (response.status) {
        case 401:
            error.kind = Model::Fail::Auth;
            error.detail = L"GitHub no acepta la credencial. Vuelve a conectar la cuenta.";
            break;

        case 403:
            // Un 403 es dos cosas distintas con el mismo número. Si no queda cuota, es el
            // límite y hay que esperar; si queda, es que la credencial no alcanza para lo
            // que se pidió. Reintentar el segundo no lo arregla y gasta lo que queda.
            if (response.rateRemaining == 0) {
                error.kind = Model::Fail::RateLimit;
                error.detail = L"Se agotó la cuota de la API de GitHub";
                if (response.rateReset.has_value()) error.retry = *response.rateReset;
            } else {
                error.kind = Model::Fail::Auth;
                error.detail = L"La credencial no tiene permiso para esto";
            }
            break;

        case 404:
            // En una escritura son las dos cosas a la vez: o el repositorio no está, o la
            // credencial no alcanza a verlo. GitHub contesta 404 y no 403 a propósito, para
            // no confirmar que algo privado existe.
            error.kind = Model::Fail::Auth;
            error.detail = L"GitHub no encuentra eso, o la credencial no llega a verlo";
            break;

        case 409:
        case 422:
            // El archivo cambió debajo. No es un error que se enseñe: quien escribe lo relee
            // y lo vuelve a fusionar, que es lo único que puede arreglarlo.
            error.kind = Model::Fail::Http;
            error.detail = L"El archivo cambió en GitHub mientras se escribía";
            break;

        case 429:
            error.kind = Model::Fail::RateLimit;
            error.detail = L"GitHub pide ir más despacio";
            if (response.rateReset.has_value()) error.retry = *response.rateReset;
            break;

        case 502:
        case 503:
        case 504:
            error.kind = Model::Fail::Http;
            error.detail = L"GitHub no está respondiendo bien ahora mismo";
            break;

        default:
            error.kind = Model::Fail::Http;
            error.detail = L"GitHub respondió con un error " + std::to_wstring(response.status);
            break;
    }
    return error;
}

}  // namespace

Model::Outcome Client::Open() {
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_cancelled = false;
    }
    return m_session.Open();
}

void Client::Close() {
    Cancel();
    m_session.Close();
}

void Client::Cancel() {
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_cancelled = true;
    }
    // Primero se despierta a quien esté esperando entre reintentos, y después se cortan las
    // peticiones en vuelo. En el otro orden, un hilo podría entrar a esperar justo después
    // de que se le cortara la petición.
    m_wake.notify_all();
    m_session.Cancel();
}

bool Client::Cancelled() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_cancelled;
}

bool Client::Pause(int ms) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_wake.wait_for(lock, std::chrono::milliseconds(ms), [this] { return m_cancelled; });
    return !m_cancelled;
}

void Client::Remember(const Response& response) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_limits.http2 = response.http2;
    if (response.rateRemaining < 0 && response.rateLimit < 0) return;

    if (response.rateLimit >= 0) m_limits.limit = response.rateLimit;
    // El menor de los vistos: con seis peticiones en paralelo, las respuestas llegan
    // desordenadas y quedarse con la última daría un número más alto que el de verdad.
    if (response.rateRemaining >= 0 &&
        (m_limits.remaining < 0 || response.rateRemaining < m_limits.remaining)) {
        m_limits.remaining = response.rateRemaining;
        m_limits.reset = response.rateReset;
    }
}

Limits Client::LastLimits() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_limits;
}

Model::Result<Response> Client::PostGraphQL(const Secret& credential, std::string_view body) {
    return Perform(L"POST", kGraphQlPath, credential, body);
}

Model::Result<Response> Client::Rest(const wchar_t* verb, const std::wstring& path,
                                    const Secret& credential, std::string_view body) {
    return Perform(verb, path, credential, body);
}

Model::Result<Response> Client::Perform(const wchar_t* verb, const std::wstring& path,
                                        const Secret& credential, std::string_view body) {
    std::wstring authorization = credential.AuthorizationHeader();
    Scrub wipe(authorization);

    Model::Error last = Model::Oops(Model::Fail::Network, L"No se pudo hablar con GitHub");

    for (int attempt = 1; attempt <= m_policy.attempts; ++attempt) {
        if (Cancelled()) return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");

        Model::Result<Response> sent = m_session.Send(verb, path, authorization, body);

        if (sent.IsOk()) {
            Response response = sent.Take();
            Remember(response);

            // 200 y 201: la API de contenidos devuelve 201 cuando el archivo no existía.
            if (response.status == 200 || response.status == 201) return response;
            last = FromStatus(response);

            // Retry-After manda sobre lo que calcularíamos: si el servidor dice cuánto,
            // sabe más que nosotros.
            if (Retryable(last.kind, response.status) && attempt < m_policy.attempts) {
                const int wait = response.retryAfterSeconds > 0
                                     ? response.retryAfterSeconds * 1000
                                     : BackoffMs(attempt, m_policy, Spread());
                if (!Pause(wait)) {
                    return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");
                }
                continue;
            }
            return last;
        }

        last = sent.Err();
        // Cancelado no se reintenta: es lo que dejaría un hilo vivo después de que la
        // ventana haya muerto.
        if (last.kind == Model::Fail::Cancelled) return last;
        if (!Retryable(last.kind, 0) || attempt >= m_policy.attempts) return last;

        if (!Pause(BackoffMs(attempt, m_policy, Spread()))) {
            return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");
        }
    }

    return last;
}

}  // namespace Github
