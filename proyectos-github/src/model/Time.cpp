#include "model/Time.h"

#include <cstdio>

namespace Model {
namespace {

// Lee 'count' dígitos exactos. Cualquier otra cosa —un espacio, un signo, una letra— es un
// fallo: std::from_chars aceptaría "+5" y atoi aceptaría basura por detrás, y las dos cosas
// convertirían una fecha mal escrita en una fecha plausible.
bool Digits(std::string_view text, std::size_t at, std::size_t count, int& out) {
    if (at + count > text.size()) return false;
    int value = 0;
    for (std::size_t k = 0; k < count; ++k) {
        const char c = text[at + k];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

}  // namespace

std::optional<Instant> ParseIso8601(std::string_view text) {
    // "2026-09-17" son 10 y "2026-09-17T22:15:45Z" son 20. Nada intermedio es válido.
    if (text.size() != 10 && text.size() != 20) return std::nullopt;

    int year = 0, month = 0, day = 0;
    if (!Digits(text, 0, 4, year) || text[4] != '-' || !Digits(text, 5, 2, month) ||
        text[7] != '-' || !Digits(text, 8, 2, day)) {
        return std::nullopt;
    }

    const std::chrono::year_month_day ymd{std::chrono::year{year},
                                          std::chrono::month{static_cast<unsigned>(month)},
                                          std::chrono::day{static_cast<unsigned>(day)}};
    // ok() es quien sabe que 2026 no es bisiesto y que el 29 de febrero de ese año no
    // existe. Comprobar 1..12 y 1..31 a mano dejaría pasar esa fecha.
    if (!ymd.ok()) return std::nullopt;

    const auto midnight = std::chrono::sys_days{ymd};
    if (text.size() == 10) return Instant{midnight};

    int hour = 0, minute = 0, second = 0;
    if (text[10] != 'T' || !Digits(text, 11, 2, hour) || text[13] != ':' ||
        !Digits(text, 14, 2, minute) || text[16] != ':' || !Digits(text, 17, 2, second) ||
        text[19] != 'Z') {
        return std::nullopt;
    }
    // 60 se rechaza: el segundo intercalar no existe en la escala de esta aplicación, y
    // GitHub no lo manda.
    if (hour > 23 || minute > 59 || second > 59) return std::nullopt;

    return Instant{midnight + std::chrono::hours{hour} + std::chrono::minutes{minute} +
                   std::chrono::seconds{second}};
}

std::string FormatIso8601(Instant when) {
    const auto days = std::chrono::floor<std::chrono::days>(when);
    const std::chrono::year_month_day ymd{days};
    const std::chrono::hh_mm_ss hms{when - days};

    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02lld:%02lld:%02lldZ",
                  static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()),
                  static_cast<long long>(hms.hours().count()),
                  static_cast<long long>(hms.minutes().count()),
                  static_cast<long long>(hms.seconds().count()));
    return buffer;
}

std::string FormatDay(Instant when) {
    const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(when)};

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
    return buffer;
}

std::int64_t ToEpoch(Instant when) {
    return when.time_since_epoch().count();
}

Instant FromEpoch(std::int64_t seconds) {
    return Instant{std::chrono::seconds{seconds}};
}

std::int64_t DaysBetween(Instant from, Instant to) {
    // floor y no una división: con duraciones negativas, dividir trunca hacia cero y
    // -1,5 días saldría -1 en vez de -2. Aquí importa poco porque lo negativo solo pasa con
    // el reloj desfasado, pero es la clase de redondeo que luego nadie vuelve a mirar.
    return std::chrono::floor<std::chrono::days>(to - from).count();
}

}  // namespace Model
