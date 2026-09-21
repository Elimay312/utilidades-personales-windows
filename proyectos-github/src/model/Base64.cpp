#include "model/Base64.h"

namespace Model {
namespace {

// El alfabeto estándar de la RFC 4648, el que usa la API de contenidos. NO es el de URL:
// ese cambia '+' y '/' por '-' y '_', y GitHub rechazaría el cuerpo sin decir por qué.
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string ToBase64(std::string_view bytes) {
    std::string out;
    // Cuatro caracteres por cada tres bytes, redondeando hacia arriba. Reservar evita que
    // un PROYECTO.md de unos pocos kilobytes reasigne media docena de veces.
    out.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        // A unsigned char ANTES de ensanchar: un byte con el bit alto puesto —cualquier
        // tilde en UTF-8— es negativo como char con signo, y al desplazarlo se lleva unos
        // por delante. Sale un base64 que compila, viaja y guarda otro archivo.
        const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) |
                               (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                               static_cast<unsigned char>(bytes[i + 2]);
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        out.push_back(kAlphabet[(value >> 6) & 0x3F]);
        out.push_back(kAlphabet[value & 0x3F]);
    }

    const std::size_t left = bytes.size() - i;
    if (left == 1) {
        const unsigned value = static_cast<unsigned>(static_cast<unsigned char>(bytes[i])) << 16;
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (left == 2) {
        const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) |
                               (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        out.push_back(kAlphabet[(value >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

}  // namespace Model
