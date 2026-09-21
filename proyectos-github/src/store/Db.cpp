#include "store/Db.h"

#include "model/Utf.h"

namespace Store {
namespace {

Model::Error FailFrom(sqlite3* db, int code, std::wstring what) {
    Model::Error error;
    error.kind = Model::Fail::Storage;
    error.code = code;
    error.detail = std::move(what);
    if (db != nullptr) {
        const char* message = sqlite3_errmsg(db);
        if (message != nullptr && *message != '\0') {
            error.detail += L": ";
            error.detail += Model::ToWide(message);
        }
    }
    return error;
}

}  // namespace

// ------------------------------------------------------------------------- Stmt --

Stmt::~Stmt() {
    // finalize sobre nullptr es legal y no hace nada, así que no hace falta comprobarlo.
    sqlite3_finalize(m_stmt);
}

Stmt::Stmt(Stmt&& other) noexcept : m_stmt(other.m_stmt), m_db(other.m_db) {
    other.m_stmt = nullptr;
    other.m_db = nullptr;
}

Stmt& Stmt::operator=(Stmt&& other) noexcept {
    if (this != &other) {
        sqlite3_finalize(m_stmt);
        m_stmt = other.m_stmt;
        m_db = other.m_db;
        other.m_stmt = nullptr;
        other.m_db = nullptr;
    }
    return *this;
}

void Stmt::Bind(int index, std::int64_t value) {
    sqlite3_bind_int64(m_stmt, index, value);
}

void Stmt::Bind(int index, int value) {
    sqlite3_bind_int(m_stmt, index, value);
}

void Stmt::Bind(int index, bool value) {
    sqlite3_bind_int(m_stmt, index, value ? 1 : 0);
}

void Stmt::Bind(int index, std::string_view utf8) {
    // SQLITE_TRANSIENT y no STATIC: SQLite copia el texto. Con STATIC se quedaría con el
    // puntero, y el caso normal aquí es enlazar un temporal que muere en la misma línea.
    sqlite3_bind_text(m_stmt, index, utf8.data(), static_cast<int>(utf8.size()),
                      SQLITE_TRANSIENT);
}

void Stmt::Bind(int index, const char* utf8) {
    Bind(index, std::string_view(utf8 != nullptr ? utf8 : ""));
}

void Stmt::Bind(int index, const std::wstring& text) {
    // El único sitio por donde una wstring entra en la base. Si esto usara reinterpret_cast
    // a char*, la eñe entraría partida y saldría partida.
    const std::string utf8 = Model::ToUtf8(text);
    sqlite3_bind_text(m_stmt, index, utf8.c_str(), static_cast<int>(utf8.size()),
                      SQLITE_TRANSIENT);
}

void Stmt::Bind(int index, std::optional<Model::Instant> when) {
    if (when.has_value()) {
        sqlite3_bind_int64(m_stmt, index, Model::ToEpoch(*when));
    } else {
        sqlite3_bind_null(m_stmt, index);
    }
}

void Stmt::BindNull(int index) {
    sqlite3_bind_null(m_stmt, index);
}

bool Stmt::IsNull(int column) const {
    return sqlite3_column_type(m_stmt, column) == SQLITE_NULL;
}

std::int64_t Stmt::Int(int column) const {
    return sqlite3_column_int64(m_stmt, column);
}

std::string Stmt::Text(int column) const {
    const auto* bytes = sqlite3_column_text(m_stmt, column);
    if (bytes == nullptr) return {};
    const int size = sqlite3_column_bytes(m_stmt, column);
    return std::string(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(size));
}

std::wstring Stmt::Wide(int column) const {
    return Model::ToWide(Text(column));
}

std::optional<Model::Instant> Stmt::When(int column) const {
    if (IsNull(column)) return std::nullopt;
    return Model::FromEpoch(Int(column));
}

Model::Result<bool> Stmt::Step() {
    const int code = sqlite3_step(m_stmt);
    if (code == SQLITE_ROW) return true;
    if (code == SQLITE_DONE) return false;
    return FailFrom(m_db, code, L"No se pudo leer de la caché local");
}

void Stmt::Reset() {
    sqlite3_reset(m_stmt);
    sqlite3_clear_bindings(m_stmt);
}

// --------------------------------------------------------------------------- Db --

Db::~Db() {
    Close();
}

Model::Outcome Db::Open(const std::string& utf8Path) {
    Close();

    const int code = sqlite3_open_v2(utf8Path.c_str(), &m_db,
                                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (code != SQLITE_OK) {
        // open_v2 deja el handle construido aunque falle, y hay que cerrarlo para poder
        // leerle el mensaje y no filtrarlo.
        Model::Error error = FailFrom(m_db, code, L"No se pudo abrir la caché local");
        sqlite3_close(m_db);
        m_db = nullptr;
        return error;
    }

    // WAL: es lo que permite que el hilo de sincronización escriba mientras el de UI lee.
    // Sin esto, el lector bloquea al escritor y la ventana se queda quieta justo mientras
    // llegan los datos, que es exactamente lo que la regla 1 de arquitectura prohíbe.
    //
    // En :memory: el journal_mode no se puede cambiar y SQLite devuelve "memory" sin fallar,
    // así que no se comprueba el resultado: no es un error, es que ahí no aplica.
    if (Model::Outcome pragma = RunOnce("PRAGMA journal_mode = WAL"); !pragma) {
        // Un WAL que no se puede activar tampoco es motivo para no arrancar: la aplicación
        // funciona igual, solo que con menos concurrencia.
    }
    // Cinco segundos antes de rendirse si la otra conexión tiene el archivo. Sin esto, dos
    // hilos que coinciden dan SQLITE_BUSY inmediato, que parece corrupción y no lo es.
    if (Model::Outcome busy = RunOnce("PRAGMA busy_timeout = 5000"); !busy) return busy;
    // Las claves ajenas ya vienen activadas por SQLITE_DEFAULT_FOREIGN_KEYS, pero decirlo
    // aquí también deja el comportamiento escrito donde se lee la base y no solo en CMake.
    if (Model::Outcome fk = RunOnce("PRAGMA foreign_keys = ON"); !fk) return fk;

    return Model::Ok();
}

void Db::Close() {
    if (m_db == nullptr) return;
    // close_v2 y no close: si quedara alguna sentencia viva, close devolvería BUSY y el
    // handle se quedaría abierto para siempre. close_v2 lo marca como zombi y lo cierra en
    // cuanto se libera la última.
    sqlite3_close_v2(m_db);
    m_db = nullptr;
}

Model::Outcome Db::Exec(const char* sql) {
    char* message = nullptr;
    const int code = sqlite3_exec(m_db, sql, nullptr, nullptr, &message);
    if (code != SQLITE_OK) {
        Model::Error error;
        error.kind = Model::Fail::Storage;
        error.code = code;
        error.detail = L"No se pudo preparar la caché local";
        if (message != nullptr) {
            error.detail += L": ";
            error.detail += Model::ToWide(message);
        }
        sqlite3_free(message);
        return error;
    }
    sqlite3_free(message);
    return Model::Ok();
}

Model::Result<Stmt> Db::Prepare(std::string_view sql) {
    sqlite3_stmt* raw = nullptr;
    const int code = sqlite3_prepare_v2(m_db, sql.data(), static_cast<int>(sql.size()), &raw,
                                        nullptr);
    if (code != SQLITE_OK) {
        sqlite3_finalize(raw);
        return FailFrom(m_db, code, L"Una consulta de la caché local no es válida");
    }
    return Stmt(raw, m_db);
}

Model::Outcome Db::RunOnce(std::string_view sql) {
    Model::Result<Stmt> prepared = Prepare(sql);
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    // Se agota del todo: un PRAGMA devuelve una fila y dejarla sin consumir mantiene viva
    // la sentencia.
    for (;;) {
        Model::Result<bool> row = stmt.Step();
        if (!row) return row.Err();
        if (!row.Value()) break;
    }
    return Model::Ok();
}

Model::Result<std::int64_t> Db::UserVersion() {
    Model::Result<Stmt> prepared = Prepare("PRAGMA user_version");
    if (!prepared) return prepared.Err();

    Stmt stmt = prepared.Take();
    Model::Result<bool> row = stmt.Step();
    if (!row) return row.Err();
    if (!row.Value()) return Model::Oops(Model::Fail::Storage, L"La caché local no dice su versión");
    return stmt.Int(0);
}

Model::Outcome Db::SetUserVersion(int version) {
    // PRAGMA no admite parámetros enlazados, así que el número se escribe dentro. Es un int
    // nuestro y no texto de nadie, que es lo único que hace esto aceptable.
    const std::string sql = "PRAGMA user_version = " + std::to_string(version);
    return Exec(sql.c_str());
}

std::int64_t Db::LastInsertId() const {
    return sqlite3_last_insert_rowid(m_db);
}

Model::Error Db::Fail(std::wstring what) const {
    return FailFrom(m_db, sqlite3_errcode(m_db), std::move(what));
}

// ------------------------------------------------------------------ Transaction --

Transaction::Transaction(Db& db) : m_db(db) {}

Transaction::~Transaction() {
    if (m_open) {
        // Nadie confirmó: se deshace. Este es el motivo entero de que la clase exista.
        m_db.Exec("ROLLBACK");
    }
}

Model::Outcome Transaction::Begin() {
    Model::Outcome started = m_db.Exec("BEGIN IMMEDIATE");
    if (started) m_open = true;
    return started;
}

Model::Outcome Transaction::Commit() {
    if (!m_open) return Model::Ok();
    Model::Outcome done = m_db.Exec("COMMIT");
    // Se marca cerrada pase lo que pase: si el COMMIT falla, SQLite ya ha deshecho la
    // transacción, y un ROLLBACK encima daría "no hay transacción activa".
    m_open = false;
    return done;
}

}  // namespace Store
