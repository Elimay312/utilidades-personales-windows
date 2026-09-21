#pragma once

// La credencial de GitHub: cómo se guarda en memoria y cómo se guarda en el equipo.
//
// Secret no es un std::wstring con otro nombre. No tiene c_str(), no tiene operator<<, no
// tiene accesor al valor, y copiarlo es un error de compilación. La única salida es la
// cabecera Authorization ya montada. Eso convierte "no se copia más de lo necesario" y "no
// se registra nunca" —SEGURIDAD.md, reglas 1 y 2— en propiedades del tipo en vez de en cosas
// que hay que acordarse de hacer dentro de tres fases.
//
// Y lo que no promete: borrar de memoria una cadena que ya se ha copiado no es posible del
// todo. Lo que se hace es quitar las copias que sabemos que existen —el búfer de la tubería,
// la cabecera montada, el propio valor al morir— y reservar capacidad fija para que la
// cadena no se reubique dejando una copia en el montón liberado. Es una mitigación, no una
// garantía, y está escrito así a propósito.

#include <Windows.h>

#include <string>
#include <string_view>

#include "model/Result.h"

namespace Github {

// Borra una cadena al salir del ámbito.
class Scrub {
public:
    explicit Scrub(std::wstring& text) : m_text(text) {}
    ~Scrub() {
        if (!m_text.empty()) {
            SecureZeroMemory(m_text.data(), m_text.size() * sizeof(wchar_t));
        }
    }

    Scrub(const Scrub&) = delete;
    Scrub& operator=(const Scrub&) = delete;

private:
    std::wstring& m_text;
};

class Secret {
public:
    Secret() = default;
    ~Secret();

    // "No se copia" hecho error de compilación, que es la única manera honrada de decirlo.
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    Secret(Secret&& other) noexcept;
    Secret& operator=(Secret&& other) noexcept;

    bool Empty() const { return m_value.empty(); }
    // Se queda con el valor y borra el original de quien lo trajo.
    void Adopt(std::wstring value);
    void Clear();

    // La ÚNICA salida. Quien la recibe debe borrarla al terminar; hay un Scrub para eso.
    //
    // "Bearer" y no "token": los dos valen en la API de GitHub, y Bearer es el que acepta
    // tanto las credenciales clásicas como las de grano fino.
    static constexpr std::wstring_view kBearerPrefix = L"Authorization: Bearer ";
    std::wstring AuthorizationHeader() const;

private:
    std::wstring m_value;
};

// El Administrador de credenciales de Windows.
namespace Vault {

// Con prefijo de la aplicación: GitHub CLI y Git for Windows también guardan aquí, y sin él
// nos pisaríamos con ellos.
inline constexpr const wchar_t* kTarget = L"Brujula:api.github.com";

Model::Outcome Save(const Secret& credential);
// Devuelve un Secret vacío si no hay nada guardado. No es un error: es el primer arranque.
Model::Result<Secret> Load();
Model::Outcome Forget();

}  // namespace Vault

// ¿Tiene la forma de una credencial de GitHub? Se comprueba antes de intentar usarla, para
// que pegar media línea de otra cosa dé un aviso claro y no un 401 dentro de un rato.
bool LooksLikeCredential(const std::wstring& value);

}  // namespace Github
