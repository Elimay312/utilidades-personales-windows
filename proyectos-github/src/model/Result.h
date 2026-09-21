#pragma once

// Los errores son valores. Es la regla 4 de arquitectura de CLAUDE.md, y hasta esta fase no
// hacía falta porque nada podía fallar a mitad: una ventana se crea o no se crea, y eso lo
// contaba un bool.
//
// Una sincronización no es así. Falla la red, falla el servidor, falla la credencial, falla
// el disco, y cada uno de los cuatro tiene que llegar hasta un aviso discreto en pantalla
// sin pasar por una excepción: el trabajo ocurre en un hilo que no es el de la UI, y una
// excepción que se escape de ahí es un std::terminate, no un mensaje de error.
//
// Y el detalle que no es de estilo: Error NO PUEDE llevar el cuerpo de una respuesta. La
// regla 3 de SEGURIDAD.md dice que no se registran las respuestas de la API —son nombres y
// mensajes de commit de repositorios privados de trabajo—, y la manera de cumplir eso
// dentro de tres fases no es acordarse, es que el tipo no tenga dónde meterlo. 'detail' es
// una frase que redactamos nosotros. Lo que sí identifica un intercambio sin contener nada
// es el X-GitHub-Request-Id, y por eso tiene su campo.

#include <chrono>
#include <string>
#include <utility>
#include <variant>

namespace Model {

enum class Fail {
    Network,    // WinHTTP no llegó a hablar con nadie
    Http,       // contestó, pero con 4xx o 5xx
    Auth,       // 401 o 403, o directamente no hay credencial
    RateLimit,  // cuota agotada; 'retry' dice a qué hora vuelve
    Protocol,   // llegó JSON, pero no el que esperábamos
    Storage,    // SQLite
    Cancelled,  // nos estamos cerrando
};

// Una frase por cada valor, y ninguna vacía: un aviso en blanco es peor que no avisar.
// Hay una prueba que lo comprueba, porque añadir un Fail y olvidar su frase no se ve.
const wchar_t* NameOf(Fail kind);

struct Error {
    Fail kind = Fail::Protocol;
    // Estado HTTP, código de SQLite o de Win32, según el 'kind'. Cero si no aplica.
    int code = 0;
    // El texto que se enseña. Nunca un cuerpo de respuesta, nunca una credencial.
    std::wstring detail;
    // X-GitHub-Request-Id: sirve para preguntar por un intercambio sin guardar su contenido.
    std::wstring requestId;
    // Solo con Fail::RateLimit.
    std::chrono::sys_seconds retry{};
};

// T o Error, sin excepciones y sin punteros. Se mueve, y por eso admite dentro cosas que no
// se copian —como la credencial de github/Auth.h, que a propósito no es copiable—.
template <class T>
class Result {
public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : m_data(std::in_place_index<1>, std::move(error)) {}

    bool IsOk() const { return m_data.index() == 0; }
    explicit operator bool() const { return IsOk(); }

    T& Value() { return std::get<0>(m_data); }
    const T& Value() const { return std::get<0>(m_data); }
    // Saca el valor. Después de esto el Result está movido y no se vuelve a mirar.
    T Take() { return std::move(std::get<0>(m_data)); }

    const Error& Err() const { return std::get<1>(m_data); }

private:
    std::variant<T, Error> m_data;
};

// Lo que devuelve algo que puede fallar pero no tiene nada que devolver.
//
// No se llama Status a propósito: "estado" en este proyecto es el del proyecto —activo,
// bloqueado, en espera, terminado— y vive en model/Types.h. Dos cosas con el mismo nombre
// en el mismo espacio es una colisión, y con 'using' de por medio sale un error de
// compilación que no se parece en nada a su causa.
struct Empty {};
using Outcome = Result<Empty>;

inline Outcome Ok() { return Outcome(Empty{}); }

// Atajos para no escribir el mismo agregado en cien sitios.
inline Error Oops(Fail kind, std::wstring detail, int code = 0) {
    Error e;
    e.kind = kind;
    e.code = code;
    e.detail = std::move(detail);
    return e;
}

}  // namespace Model
