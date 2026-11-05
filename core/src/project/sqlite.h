// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small RAII layer over the SQLite C API for the project file (internal to
// the core). Errors become ProjectError with the SQLite message.
// Copied from yantra-v2 68ea20a: core/src/project/sqlite.h, unchanged.
#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/project/project.h"

namespace yantra::project::sql {

// ProjectError for an SQLite result code, with `what` and the message.
[[noreturn]] void fail(int rc, sqlite3* db, std::string_view what);

class Db {
public:
    Db() = default;
    ~Db() { close(); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
    Db(Db&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }
    Db& operator=(Db&& o) noexcept {
        if (this != &o) {
            close();
            db_ = o.db_;
            o.db_ = nullptr;
        }
        return *this;
    }

    // Opens (flags SQLITE_OPEN_*); throws on failure.
    void open(const std::string& path, int flags);
    void close() noexcept;
    sqlite3* get() const { return db_; }
    explicit operator bool() const { return db_ != nullptr; }

    // Runs one or more statements without results.
    void exec(const std::string& sql, std::string_view what);
    // Runs a statement and returns the first column of its first row.
    std::optional<std::int64_t> int_value(const std::string& sql, std::string_view what);
    std::optional<std::string> text_value(const std::string& sql, std::string_view what);
    std::int64_t last_rowid() const { return sqlite3_last_insert_rowid(db_); }
    int changes() const { return sqlite3_changes(db_); }

private:
    sqlite3* db_ = nullptr;
};

class Stmt {
public:
    Stmt(const Db& db, std::string_view sql);
    ~Stmt();
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int index, std::int64_t v);
    Stmt& bind(int index, int v) { return bind(index, static_cast<std::int64_t>(v)); }
    Stmt& bind(int index, double v);
    Stmt& bind(int index, std::string_view v);
    Stmt& bind(int index, const std::string& v) { return bind(index, std::string_view(v)); }
    Stmt& bind(int index, const char* v) { return bind(index, std::string_view(v)); }
    Stmt& bind_null(int index);
    Stmt& bind(int index, const std::optional<double>& v) { return v ? bind(index, *v) : bind_null(index); }
    Stmt& bind(int index, const std::optional<std::int64_t>& v) { return v ? bind(index, *v) : bind_null(index); }
    Stmt& bind_zeroblob(int index, std::int64_t size);
    Stmt& bind_blob(int index, const std::vector<std::uint8_t>& v);

    // Steps once: true with a row, false when done; throws on errors.
    bool step();
    // Steps to the end (statements without rows).
    void run();
    void reset();

    bool is_null(int col) const { return sqlite3_column_type(st_, col) == SQLITE_NULL; }
    int column_type(int col) const { return sqlite3_column_type(st_, col); }
    std::int64_t i64(int col) const { return sqlite3_column_int64(st_, col); }
    double f64(int col) const { return sqlite3_column_double(st_, col); }
    std::string text(int col) const;
    std::vector<std::uint8_t> blob(int col) const;
    std::optional<double> opt_f64(int col) const {
        return is_null(col) ? std::nullopt : std::optional<double>(f64(col));
    }
    std::optional<std::int64_t> opt_i64(int col) const {
        return is_null(col) ? std::nullopt : std::optional<std::int64_t>(i64(col));
    }

private:
    sqlite3* db_;
    sqlite3_stmt* st_ = nullptr;
    std::string sql_;
};

// A transaction: BEGIN IMMEDIATE at construction, ROLLBACK unless commit()
// was called.
class Transaction {
public:
    explicit Transaction(Db& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Db& db_;
    bool done_ = false;
};

// Single-quoted SQL string literal.
std::string quote(std::string_view s);

}  // namespace yantra::project::sql
