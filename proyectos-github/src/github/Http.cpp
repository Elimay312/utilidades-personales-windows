#include "github/Http.h"

#include <algorithm>

#include "github/Auth.h"
#include "model/Utf.h"

namespace Github {
namespace {

// El único destino. SEGURIDAD.md, regla 4: un solo sitio, por HTTPS.
constexpr const wchar_t* kHost = L"api.github.com";
// GitHub rechaza las peticiones sin User-Agent. El nombre de la aplicación y nada más: ni
// versión de Windows, ni identificador de máquina, ni nada que diga quién es quien llama.
constexpr const wchar_t* kAgent = L"Brujula";

Model::Error WinError(std::wstring what) {
    const DWORD code = GetLastError();
    Model::Error error;
    // ERROR_WINHTTP_OPERATION_CANCELLED es lo que devuelve una petición a la que le han
    // cerrado el handle desde otro hilo: no es un fallo de red, es que nos estamos cerrando,
    // y confundirlos haría que el cierre enseñara un aviso de error.
    error.kind = (code == ERROR_WINHTTP_OPERATION_CANCELLED || code == ERROR_INVALID_HANDLE)
                     ? Model::Fail::Cancelled
                     : Model::Fail::Network;
    error.code = static_cast<int>(code);
    error.detail = std::move(what);
    return error;
}

std::wstring QueryHeader(HINTERNET request, const wchar_t* name) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, WINHTTP_NO_OUTPUT_BUFFER, &bytes,
                        WINHTTP_NO_HEADER_INDEX);
    if (bytes == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return {};

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value.data(), &bytes,
                            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return {};
    }
    // WinHTTP cuenta el terminador dentro de 'bytes'.
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

// -1 si la cabecera no venía o no es un número. Distinguirlo de cero importa: cero en
// 'remaining' significa cuota agotada y pararía la sincronización.
int HeaderNumber(HINTERNET request, const wchar_t* name) {
    const std::wstring value = QueryHeader(request, name);
    if (value.empty()) return -1;

    long long parsed = 0;
    for (const wchar_t c : value) {
        if (c < L'0' || c > L'9') return -1;
        parsed = parsed * 10 + (c - L'0');
        if (parsed > 2147483647LL) return -1;
    }
    return static_cast<int>(parsed);
}

void ReadRateHeaders(HINTERNET request, Response& response) {
    response.rateLimit = HeaderNumber(request, L"X-RateLimit-Limit");
    response.rateRemaining = HeaderNumber(request, L"X-RateLimit-Remaining");

    const int reset = HeaderNumber(request, L"X-RateLimit-Reset");
    if (reset > 0) response.rateReset = Model::FromEpoch(reset);

    const int retryAfter = HeaderNumber(request, L"Retry-After");
    if (retryAfter > 0) response.retryAfterSeconds = retryAfter;

    response.requestId = QueryHeader(request, L"X-GitHub-Request-Id");
}

// Un handle que se cierra solo salvo que se le diga que ya no es suyo.
class RequestHandle {
public:
    explicit RequestHandle(HINTERNET handle) : m_handle(handle) {}
    ~RequestHandle() {
        if (m_handle != nullptr) WinHttpCloseHandle(m_handle);
    }

    RequestHandle(const RequestHandle&) = delete;
    RequestHandle& operator=(const RequestHandle&) = delete;

    HINTERNET Get() const { return m_handle; }
    void Disown() { m_handle = nullptr; }

private:
    HINTERNET m_handle = nullptr;
};

}  // namespace

Session::~Session() {
    Close();
}

Model::Outcome Session::Open() {
    Close();

    m_session = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (m_session == nullptr) {
        return WinError(L"No se pudo preparar la conexión con GitHub");
    }

    // Treinta segundos para recibir. La consulta gorda midió entre ocho y nueve segundos, y
    // con diez un día lento se convertiría en un fallo permanente. Resolver y conectar sí
    // van cortos: si el DNS no contesta en ocho segundos, no es lentitud, es que no hay red.
    WinHttpSetTimeouts(m_session, 8000, 8000, 20000, 30000);

    // Las dos defensas del comentario de la cabecera.
    //
    // HTTP/2 es la buena: comprobado que api.github.com responde HTTP/2.0 cuando el cliente
    // lo ofrece, y entonces las seis peticiones del segundo pase se multiplexan sobre una
    // sola conexión y el límite por servidor deja de importar. No existe en Windows 10
    // anteriores a 1607, así que el fallo se ignora y se cae a HTTP/1.1.
    DWORD http2 = WINHTTP_PROTOCOL_FLAG_HTTP2;
    WinHttpSetOption(m_session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &http2, sizeof(http2));

    // Y por si no se negocia. Ocho y no seis: un hueco de margen para la petición del
    // primer pase si todavía anda por ahí.
    DWORD connections = 8;
    WinHttpSetOption(m_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &connections,
                     sizeof(connections));

    // La respuesta de cien repositorios con los blobs de PROYECTO.md dentro es grande. Esto
    // la trae comprimida sin escribir una línea de gzip.
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(m_session, WINHTTP_OPTION_DECOMPRESSION, &decompression,
                     sizeof(decompression));

    m_connect = WinHttpConnect(m_session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (m_connect == nullptr) {
        Model::Error error = WinError(L"No se pudo conectar con GitHub");
        Close();
        return error;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_cancelled = false;
    }
    return Model::Ok();
}

void Session::Close() {
    Cancel();

    if (m_connect != nullptr) {
        WinHttpCloseHandle(m_connect);
        m_connect = nullptr;
    }
    if (m_session != nullptr) {
        WinHttpCloseHandle(m_session);
        m_session = nullptr;
    }
}

void Session::Cancel() {
    std::vector<InFlight> victims;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_cancelled = true;
        // Se sacan de la lista DENTRO del candado. A partir de aquí, el hilo dueño de cada
        // una no la encontrará al desapuntarse y no volverá a cerrarla.
        victims.swap(m_inflight);
    }

    for (const InFlight& victim : victims) WinHttpCloseHandle(victim.request);
}

bool Session::Cancelled() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_cancelled;
}

std::uint64_t Session::Register(HINTERNET request) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_cancelled) return 0;

    const std::uint64_t ticket = m_nextTicket++;
    m_inflight.push_back(InFlight{ticket, request});
    return ticket;
}

bool Session::Unregister(std::uint64_t ticket) {
    std::lock_guard<std::mutex> guard(m_mutex);
    const auto found = std::find_if(m_inflight.begin(), m_inflight.end(),
                                    [&](const InFlight& entry) { return entry.ticket == ticket; });
    if (found == m_inflight.end()) return false;
    m_inflight.erase(found);
    return true;
}

Model::Result<Response> Session::Post(const wchar_t* path, const std::wstring& authorization,
                                      std::string_view body) {
    if (m_connect == nullptr) {
        return Model::Oops(Model::Fail::Network, L"La conexión con GitHub no está abierta");
    }
    if (Cancelled()) {
        return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");
    }

    RequestHandle request(WinHttpOpenRequest(m_connect, L"POST", path, nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE));
    if (request.Get() == nullptr) return WinError(L"No se pudo preparar la petición a GitHub");

    const std::uint64_t ticket = Register(request.Get());
    if (ticket == 0) {
        return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");
    }
    // Si Cancel() se lleva el handle mientras estamos dentro, este objeto ya no debe
    // cerrarlo: Unregister lo dice al salir.
    struct Unhook {
        Session& session;
        std::uint64_t ticket;
        RequestHandle& handle;
        ~Unhook() {
            if (!session.Unregister(ticket)) handle.Disown();
        }
    } unhook{*this, ticket, request};

    const std::wstring headers =
        authorization + L"\r\nContent-Type: application/json\r\nAccept: application/json\r\n";

    if (WinHttpSendRequest(request.Get(), headers.c_str(),
                           static_cast<DWORD>(headers.size()),
                           const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                           static_cast<DWORD>(body.size()), 0) == FALSE) {
        return WinError(L"No se pudo enviar la petición a GitHub");
    }

    if (WinHttpReceiveResponse(request.Get(), nullptr) == FALSE) {
        return WinError(L"GitHub no llegó a responder");
    }

    Response response;

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(request.Get(),
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return WinError(L"La respuesta de GitHub no trae código de estado");
    }
    response.status = static_cast<int>(status);
    ReadRateHeaders(request.Get(), response);

    DWORD protocol = 0;
    DWORD protocolSize = sizeof(protocol);
    if (WinHttpQueryOption(request.Get(), WINHTTP_OPTION_HTTP_PROTOCOL_USED, &protocol,
                           &protocolSize) != FALSE) {
        response.http2 = (protocol & WINHTTP_PROTOCOL_FLAG_HTTP2) != 0;
    }

    for (;;) {
        // Entre trozo y trozo: una respuesta grande y una cancelación no tienen por qué
        // esperarse la una a la otra.
        if (Cancelled()) return Model::Oops(Model::Fail::Cancelled, L"Sincronización cancelada");

        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request.Get(), &available) == FALSE) {
            return WinError(L"La respuesta de GitHub se cortó");
        }
        if (available == 0) break;

        const std::size_t offset = response.body.size();
        response.body.resize(offset + available);

        DWORD read = 0;
        if (WinHttpReadData(request.Get(), response.body.data() + offset, available, &read) ==
            FALSE) {
            return WinError(L"La respuesta de GitHub se cortó");
        }
        // Puede leer menos de lo que anunció, y entonces el hueco de más se queda con basura
        // dentro de la cadena. Recortar es lo que impide que el JSON acabe con relleno.
        response.body.resize(offset + read);
        if (read == 0) break;
    }

    return response;
}

}  // namespace Github
