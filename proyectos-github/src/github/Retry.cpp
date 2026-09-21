#include "github/Retry.h"

namespace Github {

bool Retryable(Model::Fail kind, int httpStatus) {
    switch (kind) {
        case Model::Fail::Network:
            // No llegó a haber conversación: un tiempo de espera, una conexión cortada, el
            // wifi volviendo. Es lo más reintentable que hay.
            return true;

        case Model::Fail::Http:
            // 502, 503 y 504 son "el de delante no pudo hablar con el de detrás": el mismo
            // envío otra vez es legítimo. 429 es "vas muy deprisa", que también se arregla
            // esperando. Cualquier otro 4xx dice que la petición está mal, y repetirla mal
            // no la arregla.
            return httpStatus == 429 || httpStatus == 502 || httpStatus == 503 ||
                   httpStatus == 504;

        case Model::Fail::Auth:
            // Repetir un 401 solo gasta cuota y retrasa la hoja de la credencial, que es lo
            // único que puede arreglarlo.
            return false;

        case Model::Fail::RateLimit:
            // Tampoco: esto no se reintenta, se espera hasta el resetAt y se avisa con la
            // hora. Un reintento a los cuatro segundos sería un reintento que ya sabemos que
            // va a fallar.
            return false;

        case Model::Fail::Protocol:
            // La respuesta llegó entera y no la entendemos. Volver a pedirla dará lo mismo.
            return false;

        case Model::Fail::Storage:
            return false;

        case Model::Fail::Cancelled:
            // Nos estamos cerrando. Reintentar aquí es justo lo que dejaría un hilo vivo
            // después de que la ventana haya muerto.
            return false;
    }
    return false;
}

int BackoffMs(int attempt, const RetryPolicy& policy, std::uint32_t jitter) {
    if (attempt < 1) attempt = 1;

    long long delay = policy.baseMs;
    // Multiplicar en un bucle y no desplazar: con un 'attempt' grande, baseMs << 30 desborda
    // y sale negativo, y una espera negativa es una espera de cero — el reintento en ráfaga
    // que precisamente se quiere evitar.
    for (int step = 1; step < attempt && delay < policy.maxMs; ++step) delay *= 2;
    if (delay > policy.maxMs) delay = policy.maxMs;
    if (delay < 0) delay = policy.maxMs;

    // Hasta un cuarto más. Sin dispersión, las seis peticiones del segundo pase fallan a la
    // vez, esperan lo mismo y vuelven juntas, que es la forma de convertir un tropiezo en
    // una tanda de tropiezos.
    const long long spread = delay / 4 + 1;
    return static_cast<int>(delay + static_cast<long long>(jitter % static_cast<std::uint32_t>(spread)));
}

}  // namespace Github
