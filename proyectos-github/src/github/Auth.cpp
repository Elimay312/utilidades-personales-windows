#include "github/Auth.h"

#include <wincred.h>

namespace Github {
namespace {

// Ni la más corta que hemos visto (40, la de GitHub CLI) ni la más larga que GitHub emite
// hoy. El rango es ancho a propósito: sirve para distinguir "esto es una credencial" de
// "esto es media línea pegada de cualquier sitio", no para validar formatos futuros.
constexpr std::size_t kMinLength = 20;
constexpr std::size_t kMaxLength = 255;

}  // namespace

// ------------------------------------------------------------------------ Secret --

Secret::~Secret() {
    Clear();
}

Secret::Secret(Secret&& other) noexcept : m_value(std::move(other.m_value)) {
    // El búfer se lo hemos quitado entero, así que en 'other' no queda copia que borrar.
    other.m_value.clear();
}

Secret& Secret::operator=(Secret&& other) noexcept {
    if (this != &other) {
        Clear();
        m_value = std::move(other.m_value);
        other.m_value.clear();
    }
    return *this;
}

void Secret::Adopt(std::wstring value) {
    Clear();
    // Capacidad fija antes de asignar: si la cadena creciera después, se reubicaría y
    // dejaría el valor viejo en el montón liberado, donde ya no se puede borrar.
    m_value.reserve(kMaxLength + 1);
    // assign y no move: con move nos quedaríamos con el búfer de 'value' y no con el
    // reservado, y además 'value' quedaría vacío sin haberse borrado.
    m_value.assign(value);
    if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
}

void Secret::Clear() {
    if (!m_value.empty()) {
        SecureZeroMemory(m_value.data(), m_value.size() * sizeof(wchar_t));
    }
    m_value.clear();
}

std::wstring Secret::AuthorizationHeader() const {
    std::wstring header;
    header.reserve(kMaxLength + kBearerPrefix.size() + 1);
    header.append(kBearerPrefix);
    header.append(m_value);
    return header;
}

bool LooksLikeCredential(const std::wstring& value) {
    if (value.size() < kMinLength || value.size() > kMaxLength) return false;
    for (const wchar_t c : value) {
        const bool allowed = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                             (c >= L'0' && c <= L'9') || c == L'_';
        if (!allowed) return false;
    }
    return true;
}

// ------------------------------------------------------------------------- Vault --

namespace Vault {
namespace {

Model::Error VaultError(std::wstring what) {
    const DWORD code = GetLastError();
    Model::Error error;
    error.kind = Model::Fail::Auth;
    error.code = static_cast<int>(code);
    error.detail = std::move(what);
    return error;
}

// CredFree en todos los caminos, también en el de fallo.
class Borrowed {
public:
    ~Borrowed() {
        if (m_entry != nullptr) CredFree(m_entry);
    }
    PCREDENTIALW* Receive() { return &m_entry; }
    const CREDENTIALW* Get() const { return m_entry; }

private:
    PCREDENTIALW m_entry = nullptr;
};

}  // namespace

Model::Outcome Save(const Secret& credential) {
    if (credential.Empty()) return Forget();

    // El valor se saca por la única puerta que hay y se le quita el prefijo de la cabecera.
    // Da un poco de rodeo, y es el precio de que el tipo no tenga un accesor al valor.
    std::wstring header = credential.AuthorizationHeader();
    Scrub wipeHeader(header);

    if (header.size() <= Secret::kBearerPrefix.size()) {
        return Model::Oops(Model::Fail::Auth, L"La credencial está vacía");
    }
    std::wstring value = header.substr(Secret::kBearerPrefix.size());
    Scrub wipeValue(value);

    CREDENTIALW entry = {};
    entry.Type = CRED_TYPE_GENERIC;
    entry.TargetName = const_cast<LPWSTR>(kTarget);
    entry.CredentialBlobSize = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    entry.CredentialBlob = reinterpret_cast<LPBYTE>(value.data());
    // LOCAL_MACHINE y no ENTERPRISE: ENTERPRISE la haría viajar al perfil del dominio, y la
    // regla 4 de SEGURIDAD.md dice que nada sale del equipo.
    entry.Persist = CRED_PERSIST_LOCAL_MACHINE;
    entry.UserName = const_cast<LPWSTR>(L"Brujula");

    if (CredWriteW(&entry, 0) == FALSE) {
        return VaultError(L"No se pudo guardar la credencial en el Administrador de credenciales");
    }
    return Model::Ok();
}

Model::Result<Secret> Load() {
    Borrowed entry;
    if (CredReadW(kTarget, CRED_TYPE_GENERIC, 0, entry.Receive()) == FALSE) {
        const DWORD why = GetLastError();
        // Que no haya nada guardado es el primer arranque, no un fallo.
        if (why == ERROR_NOT_FOUND) return Secret();
        return VaultError(L"No se pudo leer la credencial del Administrador de credenciales");
    }

    const CREDENTIALW* found = entry.Get();
    if (found == nullptr || found->CredentialBlob == nullptr || found->CredentialBlobSize == 0) {
        return Secret();
    }

    std::wstring value(reinterpret_cast<const wchar_t*>(found->CredentialBlob),
                       found->CredentialBlobSize / sizeof(wchar_t));
    Scrub wipe(value);

    Secret credential;
    credential.Adopt(value);
    return credential;
}

Model::Outcome Forget() {
    if (CredDeleteW(kTarget, CRED_TYPE_GENERIC, 0) == FALSE) {
        const DWORD why = GetLastError();
        // Borrar lo que no está es el resultado que se quería.
        if (why == ERROR_NOT_FOUND) return Model::Ok();
        return VaultError(L"No se pudo borrar la credencial del Administrador de credenciales");
    }
    return Model::Ok();
}

}  // namespace Vault
}  // namespace Github
