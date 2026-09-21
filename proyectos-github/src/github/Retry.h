#pragma once

// Qué se reintenta y cuánto se espera.
//
// Está en el núcleo y no dentro del cliente porque es una decisión, no una mecánica, y
// porque se equivoca en silencio en las dos direcciones: reintentar un 401 gasta cuota y
// retrasa el aviso hasta que el usuario ya ha dejado de mirar; no reintentar un 502 convierte
// un tropiezo del servidor en "Brújula no sincroniza". El 502 no es hipotético — salió una
// vez en las mediciones que decidieron el diseño de los dos pases.
//
// La dispersión entra por parámetro y no sale de un rand() de dentro. Es lo que permite que
// la prueba fije el número y compruebe la progresión exacta en vez de comprobar un rango.

#include <cstdint>

#include "model/Result.h"

namespace Github {

struct RetryPolicy {
    // Tres intentos: el primero y dos reintentos. Con seis peticiones en paralelo, más
    // intentos contra un servidor que va mal es empujar a un servidor que va mal.
    int attempts = 3;
    int baseMs = 400;
    int maxMs = 4000;
};

// httpStatus vale 0 cuando no llegó a haber respuesta.
bool Retryable(Model::Fail kind, int httpStatus);

// Espera antes del intento número 'attempt' (el primer reintento es 1). Crece al doble cada
// vez, con techo, y le suma hasta un cuarto de dispersión para que seis peticiones que
// fallaron a la vez no vuelvan todas juntas.
int BackoffMs(int attempt, const RetryPolicy& policy, std::uint32_t jitter);

}  // namespace Github
