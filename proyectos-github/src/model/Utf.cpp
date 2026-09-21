#include "model/Utf.h"

#include <cstddef>
#include <cstdint>

namespace Model {
namespace {

constexpr char32_t kReplacement = 0xFFFD;

bool IsContinuation(std::string_view s, std::size_t at) {
    return at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80;
}

char32_t LowSix(std::string_view s, std::size_t at) {
    return static_cast<char32_t>(static_cast<unsigned char>(s[at]) & 0x3F);
}

// Devuelve cuántos bytes consumió. NUNCA cero: un byte inválido consume uno y sale como
// U+FFFD, y eso es lo que impide que un byte roto convierta el bucle en uno infinito.
std::size_t Decode(std::string_view s, std::size_t i, char32_t& out) {
    const auto b0 = static_cast<unsigned char>(s[i]);

    if (b0 < 0x80) {
        out = b0;
        return 1;
    }
    // 0xC0 y 0xC1 no aparecen aquí a propósito: solo pueden empezar una forma sobrelarga
    // de un carácter ASCII, que es la manera clásica de colar una barra o un punto por un
    // filtro que mira la cadena antes de convertirla.
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        if (IsContinuation(s, i + 1)) {
            out = (static_cast<char32_t>(b0 & 0x1F) << 6) | LowSix(s, i + 1);
            return 2;
        }
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        if (IsContinuation(s, i + 1) && IsContinuation(s, i + 2)) {
            const char32_t cp = (static_cast<char32_t>(b0 & 0x0F) << 12) |
                                (LowSix(s, i + 1) << 6) | LowSix(s, i + 2);
            // >= 0x800 descarta el sobrelargo. El hueco D800-DFFF son los suplentes, que en
            // UTF-8 no existen: codificarlos así es CESU-8, y aceptarlo dejaría entrar
            // pares rotos que luego no se pueden escribir de vuelta.
            if (cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF)) {
                out = cp;
                return 3;
            }
        }
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        if (IsContinuation(s, i + 1) && IsContinuation(s, i + 2) && IsContinuation(s, i + 3)) {
            const char32_t cp = (static_cast<char32_t>(b0 & 0x07) << 18) |
                                (LowSix(s, i + 1) << 12) | (LowSix(s, i + 2) << 6) |
                                LowSix(s, i + 3);
            if (cp >= 0x10000 && cp <= 0x10FFFF) {
                out = cp;
                return 4;
            }
        }
    }

    out = kReplacement;
    return 1;
}

void AppendUtf16(std::wstring& out, char32_t cp) {
    if (cp < 0x10000) {
        out.push_back(static_cast<wchar_t>(cp));
        return;
    }
    const char32_t rest = cp - 0x10000;
    out.push_back(static_cast<wchar_t>(0xD800 + (rest >> 10)));
    out.push_back(static_cast<wchar_t>(0xDC00 + (rest & 0x3FF)));
}

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

std::wstring ToWide(std::string_view utf8) {
    std::wstring out;
    // El texto de GitHub es casi todo ASCII, donde un byte es una unidad. Reservar por el
    // número de bytes se pasa un poco en el resto y no se queda corto nunca.
    out.reserve(utf8.size());

    std::size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = 0;
        i += Decode(utf8, i, cp);
        AppendUtf16(out, cp);
    }
    return out;
}

std::string ToUtf8(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());

    std::size_t i = 0;
    while (i < wide.size()) {
        const auto unit = static_cast<char32_t>(static_cast<std::uint16_t>(wide[i]));
        char32_t cp = unit;

        if (unit >= 0xD800 && unit <= 0xDBFF) {
            // Un suplente alto solo vale acompañado. Suelto —y llega suelto cuando alguien
            // corta una cadena por la mitad de un emoji— se sustituye, porque escribirlo
            // tal cual produciría UTF-8 que ningún otro programa acepta.
            const bool pairedUp =
                i + 1 < wide.size() &&
                static_cast<std::uint16_t>(wide[i + 1]) >= 0xDC00 &&
                static_cast<std::uint16_t>(wide[i + 1]) <= 0xDFFF;
            if (pairedUp) {
                const auto low = static_cast<char32_t>(static_cast<std::uint16_t>(wide[i + 1]));
                cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            } else {
                cp = kReplacement;
                i += 1;
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            cp = kReplacement;
            i += 1;
        } else {
            i += 1;
        }

        AppendUtf8(out, cp);
    }
    return out;
}

}  // namespace Model
