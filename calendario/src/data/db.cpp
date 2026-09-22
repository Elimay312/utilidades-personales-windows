#include "data/db.h"

#include <windows.h>

#include <string>
#include <utility>

#include "core/log.h"

namespace agenda {
namespace {

// A prepared statement holds a pointer back to its Db only to report errors, so a moved-from
// statement must not report anything.
std::wstring Describe(sqlite3* db, std::wstring_view what) {
  const std::wstring detail = db != nullptr ? ToWide(sqlite3_errmsg(db)) : L"sin conexion";
  return std::wstring(what) + L": " + detail;
}

}  // namespace

std::string ToUtf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size,
                      nullptr, nullptr);
  return out;
}

std::wstring ToWide(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
  return out;
}

// --- Stmt --------------------------------------------------------------------------------

Stmt::~Stmt() { sqlite3_finalize(stmt_); }

Stmt::Stmt(Stmt&& other) noexcept : stmt_(other.stmt_), owner_(other.owner_) {
  other.stmt_ = nullptr;
  other.owner_ = nullptr;
}

Stmt& Stmt::operator=(Stmt&& other) noexcept {
  if (this != &other) {
    sqlite3_finalize(stmt_);
    stmt_ = std::exchange(other.stmt_, nullptr);
    owner_ = std::exchange(other.owner_, nullptr);
  }
  return *this;
}

void Stmt::Bind(int index, std::int64_t value) { sqlite3_bind_int64(stmt_, index, value); }

void Stmt::Bind(int index, int value) { sqlite3_bind_int(stmt_, index, value); }

void Stmt::Bind(int index, std::string_view utf8) {
  // SQLITE_TRANSIENT: the view can be a temporary, and SQLite has to keep its own copy.
  sqlite3_bind_text(stmt_, index, utf8.data(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
}

void Stmt::Bind(int index, const char* utf8) { Bind(index, std::string_view(utf8)); }

void Stmt::Bind(int index, const std::wstring& text) { Bind(index, ToUtf8(text)); }

void Stmt::Bind(int index, std::optional<int> value) {
  if (value) {
    Bind(index, *value);
  } else {
    BindNull(index);
  }
}

void Stmt::BindNull(int index) { sqlite3_bind_null(stmt_, index); }

bool Stmt::IsNull(int column) const {
  return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

std::int64_t Stmt::Int(int column) const { return sqlite3_column_int64(stmt_, column); }

std::optional<int> Stmt::OptInt(int column) const {
  if (IsNull(column)) return std::nullopt;
  return sqlite3_column_int(stmt_, column);
}

std::string Stmt::Text(int column) const {
  const unsigned char* text = sqlite3_column_text(stmt_, column);
  if (text == nullptr) return {};
  const int size = sqlite3_column_bytes(stmt_, column);
  return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(size));
}

std::wstring Stmt::Wide(int column) const { return ToWide(Text(column)); }

bool Stmt::Step(bool* ok) {
  const int result = sqlite3_step(stmt_);
  if (ok != nullptr) *ok = result == SQLITE_ROW || result == SQLITE_DONE;
  if (result == SQLITE_ROW) return true;
  if (result != SQLITE_DONE && owner_ != nullptr) owner_->Fail(L"al recorrer una consulta");
  return false;
}

void Stmt::Reset() {
  sqlite3_reset(stmt_);
  sqlite3_clear_bindings(stmt_);
}

// --- Db ----------------------------------------------------------------------------------

Db::~Db() { Close(); }

std::wstring Db::error() const {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return error_;
}

bool Db::Fail(std::wstring_view what) {
  const std::wstring message = Describe(db_, what);
  {
    std::lock_guard<std::mutex> lock(errorMutex_);
    error_ = message;
  }
  LogError(L"db: {}", message);
  return false;
}

bool Db::Open(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  return OpenUtf8(ToUtf8(path.wstring()));
}

bool Db::OpenMemory() { return OpenUtf8(":memory:"); }

bool Db::OpenUtf8(const std::string& utf8Path) {
  Close();
  const int result = sqlite3_open_v2(utf8Path.c_str(), &db_,
                                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                         SQLITE_OPEN_FULLMUTEX,
                                     nullptr);
  if (result != SQLITE_OK) {
    Fail(L"al abrir la base de datos");
    Close();
    return false;
  }
  // WAL so a write on the worker thread does not block the read the popup is doing to paint
  // itself; busy_timeout so the one that does collide waits instead of failing.
  // ponytail: una sola conexion en modo serializado. Dos conexiones si una escritura llega a
  // notarse en una lectura.
  sqlite3_busy_timeout(db_, 3000);
  Exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;");
  return true;
}

void Db::Close() {
  if (db_ == nullptr) return;
  sqlite3_close(db_);
  db_ = nullptr;
}

bool Db::Exec(const char* sql) {
  char* message = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
    const std::wstring detail = L"al ejecutar SQL: " + ToWide(message != nullptr ? message : "");
    {
      std::lock_guard<std::mutex> lock(errorMutex_);
      error_ = detail;
    }
    LogError(L"db: {}", detail);
    sqlite3_free(message);
    return false;
  }
  sqlite3_free(message);
  return true;
}

std::optional<Stmt> Db::Prepare(std::string_view sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &raw, nullptr) !=
      SQLITE_OK) {
    Fail(L"al preparar una consulta");
    return std::nullopt;
  }
  return Stmt(raw, this);
}

bool Db::RunOnce(std::string_view sql) {
  std::optional<Stmt> stmt = Prepare(sql);
  if (!stmt) return false;
  bool ok = false;
  stmt->Step(&ok);
  return ok;
}

std::optional<std::int64_t> Db::UserVersion() {
  std::optional<Stmt> stmt = Prepare("PRAGMA user_version");
  if (!stmt) return std::nullopt;
  bool ok = false;
  if (!stmt->Step(&ok) || !ok) return std::nullopt;
  return stmt->Int(0);
}

bool Db::SetUserVersion(int version) {
  // PRAGMA does not take a bound parameter, so the number is pasted in. It is an int we chose
  // ourselves, never anything that came from outside.
  return Exec(("PRAGMA user_version=" + std::to_string(version)).c_str());
}

std::int64_t Db::LastInsertId() const { return sqlite3_last_insert_rowid(db_); }

// --- Transaction -------------------------------------------------------------------------

bool Transaction::Begin() {
  if (active_) return true;
  active_ = db_.Exec("BEGIN IMMEDIATE");
  return active_;
}

bool Transaction::Commit() {
  if (!active_) return false;
  active_ = false;
  return db_.Exec("COMMIT");
}

Transaction::~Transaction() {
  if (active_) db_.Exec("ROLLBACK");
}

}  // namespace agenda
