#pragma once

// El cliente: la sesión de WinHTTP más los reintentos y el respeto a la cuota.
//
// Es lo que separa "una petición" de "una petición que sale bien aunque el servidor tenga un
// mal momento". El 502 que absorbe no es hipotético: salió una vez en las mediciones de ocho
// peticiones que decidieron el diseño de esta fase.
//
// Se llama desde varios hilos a la vez y no guarda nada por petición; lo único compartido es
// lo último que GitHub dijo de la cuota, y va bajo candado.

#include <mutex>
#include <condition_variable>
#include <optional>
#include <string>
#include <string_view>

#include "github/Auth.h"
#include "github/Http.h"
#include "github/Retry.h"
#include "model/Result.h"
#include "model/Time.h"

namespace Github {

struct Limits {
    int limit = -1;
    int remaining = -1;
    std::optional<Model::Instant> reset;
    // Lo negoció la última respuesta. Se guarda para poder comprobar que el ajuste de HTTP/2
    // surte efecto, que es la única forma de saberlo: no falla, solo tarda el triple.
    bool http2 = false;
};

class Client {
public:
    Model::Outcome Open();
    void Close();

    // Corta todo lo que esté en vuelo y hace que las esperas entre reintentos vuelvan ya.
    void Cancel();
    bool Cancelled() const;

    // Una petición a /graphql con reintentos. Devuelve la respuesta solo si llegó con 200;
    // cualquier otra cosa sale como Error con su clase puesta.
    // Recibe la credencial y no la cabecera: así la cabecera se monta y se borra dentro de
    // una sola llamada, en vez de andar por ahí viva durante toda la sincronización.
    Model::Result<Response> PostGraphQL(const Secret& credential, std::string_view body);

    Limits LastLimits() const;

    // Por debajo de esto no se empieza una sincronización. Con ocho puntos por sincronización
    // y cinco mil por hora es un cinturón, no una restricción.
    static constexpr int kLowWater = 100;

private:
    // Espera 'ms', o vuelve antes si alguien cancela. Devuelve false si se canceló: sin
    // esto, cerrar la ventana durante el respiro de un reintento esperaría cuatro segundos
    // con la ventana ya cerrada.
    bool Pause(int ms);
    void Remember(const Response& response);

    Session m_session;
    RetryPolicy m_policy;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_cancelled = false;
    Limits m_limits;
};

}  // namespace Github
