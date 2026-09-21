#pragma once

// Todo instante de Brújula es UTC y son segundos desde 1970. No hay husos horarios en
// ninguna parte, y es una decisión, no un descuido.
//
// GitHub entrega las fechas como "2026-09-17T22:15:45Z", siempre en UTC. La aplicación las
// usa para una sola cosa —cuántos días hace de aquello— y esa cuenta en horario local es
// una fuente de fallos que solo aparecen cerca de medianoche y solo en ciertas fechas: un
// repositorio empujado a las 23:30 UTC saldría empujado "mañana" en Madrid, y un repositorio
// justo en el borde de los 14 días cambiaría de grupo según la hora a la que se abra la
// aplicación. El usuario no vería un error; vería una tarjeta en el sitio equivocado.
//
// Lo que sí es local es cómo se escribe una fecha en pantalla, y eso es de la vista.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Model {

using Instant = std::chrono::sys_seconds;

// Acepta "2026-09-17T22:15:45Z" y también "2026-09-17" a secas, que es como PROYECTO.md
// escribe sus fechas. Devuelve vacío si no es ninguna de las dos o si la fecha no existe
// —un 31 de febrero entra como texto y tiene que salir rechazado—.
std::optional<Instant> ParseIso8601(std::string_view text);

// "2026-09-17T22:15:45Z". El formato en el que GitHub las manda y en el que se escriben.
std::string FormatIso8601(Instant when);
// "2026-09-17", para la lista de novedades y para PROYECTO.md.
std::string FormatDay(Instant when);

std::int64_t ToEpoch(Instant when);
Instant FromEpoch(std::int64_t seconds);

// Días completos de 'from' a 'to'. Negativo si 'to' es anterior, que es lo que pasa cuando
// el reloj del equipo va atrasado respecto al servidor: un push "del futuro" tiene que dar
// un número negativo y contarse como recentísimo, no desbordar hacia dormido.
std::int64_t DaysBetween(Instant from, Instant to);

}  // namespace Model
