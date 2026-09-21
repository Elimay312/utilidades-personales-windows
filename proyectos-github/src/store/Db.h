#pragma once

// SQLite envuelto en lo justo: un handle que se cierra solo, una sentencia que se libera
// sola, y todos los errores como valores.
//
// Envolver SQLite no es adornarlo. Son tres cosas concretas que se hacen mal cuando se usa
// a pelo y que aquí no se pueden hacer mal: dejar una sentencia sin finalizar —que en WAL
// impide que el archivo se cierre y deja un .wal detrás—, olvidar comprobar un código de
// retorno, y pasar un std::wstring a una API que espera UTF-8. Lo tercero es lo que
// convierte "Revisión" en "RevisiÃ³n" dentro de la caché sin que nadie se entere hasta que
// lo ve en pantalla tres fases después.
//
// Aquí dentro se habla UTF-8 porque es lo que habla SQLite; fuera se habla wstring. La
// conversión pasa por model/Utf.h y por ningún otro sitio.

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "model/Result.h"
#include "model/Time.h"

namespace Store {

class Db;

// Una sentencia preparada. Se mueve, no se copia: dos objetos con el mismo sqlite3_stmt
// serían dos finalize sobre el mismo puntero.
class Stmt {
public:
    Stmt() = default;
    ~Stmt();

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt(Stmt&& other) noexcept;
    Stmt& operator=(Stmt&& other) noexcept;

    // Los índices de enlace empiezan en 1 y los de lectura en 0. Es de SQLite, no nuestro,
    // y es la confusión más fácil de esta API.
    void Bind(int index, std::int64_t value);
    void Bind(int index, int value);
    void Bind(int index, bool value);
    void Bind(int index, std::string_view utf8);
    // Explícita y no dejada a string_view: sin ella, un literal se convierte a puntero y de
    // ahí a bool, que es una conversión estándar y gana a la de string_view. Enlazar "hola"
    // guardaría un 1. Compila, no avisa, y el dato entra mal.
    void Bind(int index, const char* utf8);
    void Bind(int index, const std::wstring& text);
    void Bind(int index, std::optional<Model::Instant> when);
    void BindNull(int index);

    bool IsNull(int column) const;
    std::int64_t Int(int column) const;
    std::string Text(int column) const;
    std::wstring Wide(int column) const;
    std::optional<Model::Instant> When(int column) const;

    // true si hay fila, false si se acabaron. El error viene como Error, no como false:
    // confundir "no hay más filas" con "la base está corrupta" es justo lo que no puede
    // pasar cuando lo que se lee son las notas de alguien.
    Model::Result<bool> Step();
    void Reset();

    sqlite3_stmt* Handle() const { return m_stmt; }

private:
    friend class Db;
    Stmt(sqlite3_stmt* stmt, sqlite3* db) : m_stmt(stmt), m_db(db) {}

    sqlite3_stmt* m_stmt = nullptr;
    sqlite3* m_db = nullptr;
};

class Db {
public:
    Db() = default;
    ~Db();

    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    // 'path' en UTF-8. ":memory:" vale, y es como corren todas las pruebas.
    Model::Outcome Open(const std::string& utf8Path);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    Model::Outcome Exec(const char* sql);
    Model::Result<Stmt> Prepare(std::string_view sql);

    // Ejecuta una sentencia sin filas de vuelta, de una sentada.
    Model::Outcome RunOnce(std::string_view sql);

    Model::Result<std::int64_t> UserVersion();
    Model::Outcome SetUserVersion(int version);

    std::int64_t LastInsertId() const;
    sqlite3* Handle() const { return m_db; }

    // Convierte el código y el mensaje de SQLite en un Error nuestro.
    Model::Error Fail(std::wstring what) const;

private:
    sqlite3* m_db = nullptr;
};

// Una transacción que se deshace sola si nadie la confirma.
//
// Es lo que hace cierta la frase "una migración que falla no deja la versión subida": con
// un BEGIN y un COMMIT escritos a mano, cualquier retorno por el medio —y hay varios— deja
// la transacción abierta y la base a medio convertir.
class Transaction {
public:
    explicit Transaction(Db& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    Model::Outcome Begin();
    Model::Outcome Commit();

private:
    Db& m_db;
    bool m_open = false;
};

}  // namespace Store
