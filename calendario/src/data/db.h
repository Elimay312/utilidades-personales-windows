#pragma once

// SQLite wrapped in just enough: a handle that closes itself, a statement that finalises
// itself, and return codes that cannot be ignored by accident.
//
// Wrapping it is not decoration. It is three things that go wrong when SQLite is used raw and
// that cannot go wrong here: leaving a statement unfinalised -- which in WAL keeps the file
// from closing and leaves a .wal behind --, forgetting to check a return code, and handing a
// std::wstring to an API that expects UTF-8. The last one is what turns "Revision" into
// "RevisiA3n" inside the cache without anyone noticing until it shows up on screen.
//
// UTF-8 is spoken in here because that is what SQLite speaks; wstring is spoken everywhere
// else. The conversion lives in this file and nowhere else (CLAUDE.md: UTF-8 only at the edge).

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace agenda {

std::string ToUtf8(std::wstring_view text);
std::wstring ToWide(std::string_view utf8);

class Db;

// A prepared statement. Moves, never copies: two objects holding the same sqlite3_stmt would
// be two finalises on one pointer.
class Stmt {
 public:
  Stmt() = default;
  ~Stmt();

  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
  Stmt(Stmt&& other) noexcept;
  Stmt& operator=(Stmt&& other) noexcept;

  // Bind indices start at 1 and column indices start at 0. That is SQLite's, not ours, and it
  // is the easiest confusion in this API.
  void Bind(int index, std::int64_t value);
  void Bind(int index, int value);
  void Bind(int index, std::string_view utf8);
  // Spelled out instead of left to string_view: without it a literal decays to a pointer and
  // then to bool, which is a standard conversion and wins. Binding "hola" would store a 1.
  void Bind(int index, const char* utf8);
  void Bind(int index, const std::wstring& text);
  void Bind(int index, std::optional<int> value);
  void BindNull(int index);

  bool IsNull(int column) const;
  std::int64_t Int(int column) const;
  std::optional<int> OptInt(int column) const;
  std::string Text(int column) const;
  std::wstring Wide(int column) const;

  // true while there are rows. `ok` tells "the rows ran out" apart from "the database is
  // corrupt", which is exactly the difference that must not be lost when what is stored is
  // somebody's calendar.
  bool Step(bool* ok = nullptr);
  void Reset();

  bool valid() const { return stmt_ != nullptr; }

 private:
  friend class Db;
  Stmt(sqlite3_stmt* stmt, Db* owner) : stmt_(stmt), owner_(owner) {}

  sqlite3_stmt* stmt_ = nullptr;
  Db* owner_ = nullptr;
};

class Db {
 public:
  Db() = default;
  ~Db();

  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;

  // `path` may be ":memory:", which is how every test runs.
  bool Open(const std::filesystem::path& path);
  bool OpenMemory();
  void Close();
  bool IsOpen() const { return db_ != nullptr; }

  bool Exec(const char* sql);
  std::optional<Stmt> Prepare(std::string_view sql);
  bool RunOnce(std::string_view sql);

  std::optional<std::int64_t> UserVersion();
  bool SetUserVersion(int version);

  std::int64_t LastInsertId() const;
  // How many rows the last statement touched. The difference between "the update did nothing"
  // and "the update did something" is not in the return code, and it is the only way to say
  // whether anything was actually adopted.
  int Changes() const;
  sqlite3* handle() const { return db_; }

  // What went wrong last, ready to be shown. Logged as well, so nothing is lost when nobody
  // asks. By value and behind a lock, not by reference: the worker thread and the interface
  // thread share this connection, so both can be writing a message while the other reads it.
  std::wstring error() const;
  bool Fail(std::wstring_view what);

 private:
  bool OpenUtf8(const std::string& utf8Path);

  sqlite3* db_ = nullptr;
  mutable std::mutex errorMutex_;
  std::wstring error_;
};

// A transaction that rolls itself back when nobody commits it.
//
// It is what makes "a migration that fails does not leave the version bumped" true: with a
// BEGIN and a COMMIT written by hand, any return in the middle -- and there are several --
// leaves the transaction open and the database half converted.
class Transaction {
 public:
  explicit Transaction(Db& db) : db_(db) {}
  ~Transaction();

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  bool Begin();
  bool Commit();

 private:
  Db& db_;
  bool active_ = false;
};

}  // namespace agenda
