// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/project/sqlite.cpp, unchanged.

#include "sqlite.h"

namespace yantra::project::sql {

void fail(int rc, sqlite3* db, std::string_view what) {
    using Code = ProjectError::Code;
    std::string msg(what);
    msg += ": ";
    msg += db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    switch (rc & 0xff) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        throw ProjectError(Code::Locked, msg);
    case SQLITE_NOTADB:
        throw ProjectError(Code::NotAProject, msg);
    case SQLITE_CORRUPT:
    case SQLITE_FORMAT:
    case SQLITE_SCHEMA:
    case SQLITE_MISMATCH:
    case SQLITE_RANGE:
    case SQLITE_ERROR:
        throw ProjectError(Code::Damaged, msg);
    case SQLITE_CONSTRAINT:
    case SQLITE_TOOBIG:
        throw ProjectError(Code::Invalid, msg);
    default:
        throw ProjectError(Code::Io, msg);
    }
}

void Db::open(const std::string& path, int flags) {
    close();
    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &db, flags | SQLITE_OPEN_EXRESCODE, nullptr);
    if (rc != SQLITE_OK) {
        const std::string msg = db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
        sqlite3_close(db);
        throw ProjectError(ProjectError::Code::Io, "cannot open " + path + ": " + msg);
    }
    db_ = db;
    // Damaged files must give errors, never crashes or silent changes.
    sqlite3_db_config(db_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0, nullptr);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_ENABLE_VIEW, 0, nullptr);
    sqlite3_extended_result_codes(db_, 1);
    sqlite3_busy_timeout(db_, 0);
}

void Db::close() noexcept {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void Db::exec(const std::string& sql, std::string_view what) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    sqlite3_free(err);
    if (rc != SQLITE_OK) {
        fail(rc, db_, what);
    }
}

std::optional<std::int64_t> Db::int_value(const std::string& sql, std::string_view what) {
    Stmt s(*this, sql);
    (void)what;
    if (!s.step() || s.is_null(0)) {
        return std::nullopt;
    }
    return s.i64(0);
}

std::optional<std::string> Db::text_value(const std::string& sql, std::string_view what) {
    Stmt s(*this, sql);
    (void)what;
    if (!s.step() || s.is_null(0)) {
        return std::nullopt;
    }
    return s.text(0);
}

Stmt::Stmt(const Db& db, std::string_view sql) : db_(db.get()), sql_(sql) {
    if (db_ == nullptr) {
        throw ProjectError(ProjectError::Code::Closed, "the project is closed");
    }
    const int rc = sqlite3_prepare_v2(db_, sql_.c_str(), static_cast<int>(sql_.size()), &st_, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(st_);
        st_ = nullptr;
        fail(rc, db_, "prepare");
    }
}

Stmt::~Stmt() { sqlite3_finalize(st_); }

namespace {
void check_bind(int rc, sqlite3* db) {
    if (rc != SQLITE_OK) {
        fail(rc, db, "bind");
    }
}
}  // namespace

Stmt& Stmt::bind(int index, std::int64_t v) {
    check_bind(sqlite3_bind_int64(st_, index, v), db_);
    return *this;
}

Stmt& Stmt::bind(int index, double v) {
    check_bind(sqlite3_bind_double(st_, index, v), db_);
    return *this;
}

Stmt& Stmt::bind(int index, std::string_view v) {
    check_bind(sqlite3_bind_text64(st_, index, v.data(), v.size(), SQLITE_TRANSIENT, SQLITE_UTF8), db_);
    return *this;
}

Stmt& Stmt::bind_null(int index) {
    check_bind(sqlite3_bind_null(st_, index), db_);
    return *this;
}

Stmt& Stmt::bind_zeroblob(int index, std::int64_t size) {
    check_bind(sqlite3_bind_zeroblob64(st_, index, static_cast<sqlite3_uint64>(size)), db_);
    return *this;
}

Stmt& Stmt::bind_blob(int index, const std::vector<std::uint8_t>& v) {
    // An empty vector binds a zero-length blob, not NULL.
    static const std::uint8_t empty = 0;
    check_bind(sqlite3_bind_blob64(st_, index, v.empty() ? &empty : v.data(), static_cast<sqlite3_uint64>(v.size()),
                                   SQLITE_TRANSIENT),
               db_);
    return *this;
}

bool Stmt::step() {
    const int rc = sqlite3_step(st_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    fail(rc, db_, "query");
}

void Stmt::run() {
    while (step()) {
    }
}

void Stmt::reset() {
    sqlite3_reset(st_);
    sqlite3_clear_bindings(st_);
}

std::string Stmt::text(int col) const {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st_, col));
    const int n = sqlite3_column_bytes(st_, col);
    return p != nullptr ? std::string(p, static_cast<std::size_t>(n)) : std::string();
}

std::vector<std::uint8_t> Stmt::blob(int col) const {
    const auto* p = static_cast<const std::uint8_t*>(sqlite3_column_blob(st_, col));
    const int n = sqlite3_column_bytes(st_, col);
    return p != nullptr ? std::vector<std::uint8_t>(p, p + n) : std::vector<std::uint8_t>();
}

Transaction::Transaction(Db& db) : db_(db) { db_.exec("BEGIN IMMEDIATE", "begin"); }

Transaction::~Transaction() {
    if (!done_ && db_) {
        sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

void Transaction::commit() {
    db_.exec("COMMIT", "commit");
    done_ = true;
}

std::string quote(std::string_view s) {
    std::string out = "'";
    for (const char c : s) {
        out.push_back(c);
        if (c == '\'') {
            out.push_back('\'');
        }
    }
    out.push_back('\'');
    return out;
}

}  // namespace yantra::project::sql
