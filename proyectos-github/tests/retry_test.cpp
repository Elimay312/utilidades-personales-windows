// Qué se reintenta, cuánto se espera, y que ningún fallo se quede sin frase.
//
// Las dos direcciones se equivocan en silencio: reintentar un 401 gasta cuota y retrasa el
// aviso hasta que el usuario ya ha dejado de mirar; no reintentar un 502 convierte un
// tropiezo del servidor en "Brújula no sincroniza". El 502 salió de verdad en las mediciones
// que decidieron el diseño de esta fase, así que no es un caso inventado.

#include <doctest/doctest.h>

#include "github/Retry.h"

#include <string>

using Github::BackoffMs;
using Github::Retryable;
using Github::RetryPolicy;
using Model::Fail;

TEST_CASE("un 401 no se reintenta") {
    CHECK_FALSE(Retryable(Fail::Auth, 401));
    CHECK_FALSE(Retryable(Fail::Auth, 403));
    CHECK_FALSE(Retryable(Fail::Http, 401));
    CHECK_FALSE(Retryable(Fail::Http, 404));
    CHECK_FALSE(Retryable(Fail::Http, 422));
}

TEST_CASE("el 502 que salió en las mediciones sí se reintenta") {
    CHECK(Retryable(Fail::Http, 502));
    CHECK(Retryable(Fail::Http, 503));
    CHECK(Retryable(Fail::Http, 504));
    // 429 también: "vas muy deprisa" se arregla esperando.
    CHECK(Retryable(Fail::Http, 429));
    // Y lo que ni llegó a hablar.
    CHECK(Retryable(Fail::Network, 0));
}

TEST_CASE("lo que no se arregla repitiéndolo no se repite") {
    // El límite no se reintenta: se espera hasta el resetAt y se avisa con la hora. Un
    // reintento a los cuatro segundos es un reintento que ya sabemos que va a fallar.
    CHECK_FALSE(Retryable(Fail::RateLimit, 403));
    // La respuesta llegó entera y no la entendemos; volver a pedirla dará lo mismo.
    CHECK_FALSE(Retryable(Fail::Protocol, 200));
    CHECK_FALSE(Retryable(Fail::Storage, 0));
}

TEST_CASE("cancelar no se reintenta") {
    // Reintentar aquí es justo lo que dejaría un hilo vivo después de que la ventana haya
    // muerto, que es el cuelgue más probable de toda la fase.
    CHECK_FALSE(Retryable(Fail::Cancelled, 0));
}

TEST_CASE("el respiro crece al doble") {
    const RetryPolicy policy;
    // Sin dispersión, para ver la progresión limpia. Por eso el jitter entra por parámetro
    // y no sale de un rand() de dentro.
    CHECK(BackoffMs(1, policy, 0) == 400);
    CHECK(BackoffMs(2, policy, 0) == 800);
    CHECK(BackoffMs(3, policy, 0) == 1600);
    CHECK(BackoffMs(4, policy, 0) == 3200);
}

TEST_CASE("el respiro tiene techo y no desborda") {
    const RetryPolicy policy;
    CHECK(BackoffMs(5, policy, 0) == policy.maxMs);
    CHECK(BackoffMs(20, policy, 0) == policy.maxMs);
    // Un intento absurdo no puede dar un número negativo: desplazar en vez de multiplicar
    // desbordaría, y una espera negativa es una espera de cero — la ráfaga de reintentos que
    // precisamente se quiere evitar.
    CHECK(BackoffMs(1000, policy, 0) == policy.maxMs);
    CHECK(BackoffMs(0, policy, 0) == 400);
    CHECK(BackoffMs(-5, policy, 0) > 0);
}

TEST_CASE("la dispersión suma pero no se desmadra") {
    const RetryPolicy policy;
    // Hasta un cuarto más, para que las seis peticiones del segundo pase no vuelvan todas
    // juntas después de fallar a la vez.
    for (std::uint32_t jitter : {0u, 1u, 99u, 4294967295u}) {
        const int delay = BackoffMs(2, policy, jitter);
        CHECK(delay >= 800);
        CHECK(delay <= 800 + 800 / 4);
    }
}

TEST_CASE("cada clase de fallo tiene una frase y ninguna está vacía") {
    // Una tabla con un hueco es un aviso en blanco, que es peor que no avisar. Y añadir un
    // Fail nuevo y olvidar su frase no se ve por ningún lado.
    const Fail kinds[] = {Fail::Network, Fail::Http,    Fail::Auth,     Fail::RateLimit,
                          Fail::Protocol, Fail::Storage, Fail::Cancelled};
    for (const Fail kind : kinds) {
        const std::wstring phrase = Model::NameOf(kind);
        CHECK_FALSE(phrase.empty());
        // Y son distintas entre sí: dos fallos con el mismo texto no ayudan a nadie.
        for (const Fail other : kinds) {
            if (other == kind) continue;
            CHECK(phrase != std::wstring(Model::NameOf(other)));
        }
    }
}
