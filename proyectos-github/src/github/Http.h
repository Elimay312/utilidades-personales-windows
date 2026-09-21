#pragma once

// WinHTTP: una sesión, una conexión, y una petición por hilo.
//
// Síncrono sobre N hilos y no asíncrono. El trabajo son seis peticiones una vez por
// sincronización, y ya hay un hilo director que las reparte: el asíncrono cambiaría una
// función en línea recta por una máquina de estados cuyas terminaciones aterrizan igualmente
// en el grupo de hilos de WinHTTP. La misma concurrencia con más formas de equivocarse con
// las vidas de los handles.
//
// La sesión y la conexión se comparten entre los seis hilos —WinHTTP lo admite— y eso es lo
// que da keep-alive y reuso de TLS. Con los tiempos medidos (~85 ms por repositorio), un
// apretón de manos por petición sería más caro que la petición.
//
// Y el ajuste que anularía el segundo pase en silencio: WinHTTP limita las conexiones por
// servidor. Seis peticiones "en paralelo" estranguladas a dos tardarían el triple, sin un
// solo error, solo lento. Se ponen las dos defensas —HTTP/2, que multiplexa las seis en una
// conexión, y el límite explícito por si no se negocia— y se comprueba con el cronómetro.

#include <Windows.h>

#include <winhttp.h>

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/Result.h"
#include "model/Time.h"

namespace Github {

struct Response {
    int status = 0;
    // UTF-8 crudo, sin convertir. No se registra en ningún sitio: son nombres y mensajes de
    // commit de repositorios privados de trabajo (SEGURIDAD.md, regla 3).
    std::string body;

    // -1 significa "la cabecera no venía", que no es lo mismo que cero.
    int rateLimit = -1;
    int rateRemaining = -1;
    std::optional<Model::Instant> rateReset;
    int retryAfterSeconds = 0;
    // Identifica el intercambio ante GitHub sin contener nada de dentro.
    std::wstring requestId;

    // Qué protocolo se negoció de verdad. No es curiosidad: con HTTP/1.1 las seis peticiones
    // del segundo pase compiten por las conexiones, y con HTTP/2 se multiplexan sobre una.
    // La diferencia no da ningún error, solo tiempo, así que hay que poder mirarla.
    bool http2 = false;
};

class Session {
public:
    Session() = default;
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Model::Outcome Open();
    void Close();

    // Cierra los handles de las peticiones que estén en vuelo. Es la única forma de sacar a
    // un hilo de un WinHttpReceiveResponse síncrono sin esperar los treinta segundos del
    // tiempo de espera, y es lo que hace que cerrar la ventana a mitad de una sincronización
    // no deje el proceso colgado medio minuto.
    void Cancel();
    bool Cancelled() const;

    // Una petición a api.github.com. Se llama desde varios hilos a la vez.
    //
    // 'authorization' es la cabecera Authorization ya montada; esta clase no sabe de dónde
    // sale ni la guarda.
    //
    // El verbo entra por parámetro desde la fase 5: hasta entonces todo era POST a /graphql,
    // y el modo repo escribe con PUT a la API de contenidos. La ruta pasa a ser una cadena
    // porque esa se calcula —lleva el dueño y el nombre del repositorio dentro— y quien la
    // calcula la valida antes (Github::ContentsPath).
    Model::Result<Response> Send(const wchar_t* verb, const std::wstring& path,
                                 const std::wstring& authorization, std::string_view body);

private:
    // Cada petición en vuelo se apunta con un número propio y creciente, no solo con su
    // handle. Si Cancel() cierra el handle y WinHTTP reutiliza ese mismo valor para una
    // petición nueva de otro hilo, el dueño del primero cerraría el de otro; con el número
    // no hay confusión posible.
    struct InFlight {
        std::uint64_t ticket = 0;
        HINTERNET request = nullptr;
    };

    std::uint64_t Register(HINTERNET request);
    // true si el handle sigue siendo nuestro y hay que cerrarlo; false si Cancel() ya lo hizo.
    bool Unregister(std::uint64_t ticket);

    HINTERNET m_session = nullptr;
    HINTERNET m_connect = nullptr;

    mutable std::mutex m_mutex;
    std::vector<InFlight> m_inflight;
    std::uint64_t m_nextTicket = 1;
    bool m_cancelled = false;
};

}  // namespace Github
