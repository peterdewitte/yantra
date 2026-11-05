// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Indoor project file (IND-01, IND-03, IND-04; docs/formats/PROJECT.md).
// Copied from yantra-v2 8c7a3e1: core/src/project/project.cpp, adapted for
// IND-01/03/04: the floors are the building model's floors (string ids,
// validated by building::Building::validate), sources have string ids,
// a building row, replace_building, migration backups, save_as over an
// existing file, fault points for the crash tests; sessions, samples and
// provenance are left to later steps.

#include "yantra/core/project/project.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>

#include <unistd.h>

#include "schema.h"
#include "sqlite.h"
#include "yantra/core/build_info.h"
#include "yantra/core/platform/file.h"
#include "yantra/core/util/sha256.h"

namespace yantra::project {

namespace fs = std::filesystem;
using Code = ProjectError::Code;

namespace {

constexpr std::size_t kChunk = 1u << 20;
// Below SQLite's default largest string or blob (1e9 bytes).
constexpr std::uint64_t kMaxSourceBytes = 1'000'000'000;
// Floor levels the project accepts (and the offset that moves them out
// of the way of the UNIQUE constraint while levels are handed out again).
constexpr int kMaxLevel = 1000;
constexpr int kLevelOffset = 1'000'000;

std::string system_now_utc() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string default_tool() { return std::string("Yantra ") + std::string(core::build_info().version_string); }

// A temporary name next to `target` (same folder, so a rename is atomic).
fs::path temp_name(const fs::path& target) {
    static std::atomic<unsigned> counter{0};
    fs::path p = target;
    p += ".tmp-" + std::to_string(::getpid()) + "-" + std::to_string(counter++);
    return p;
}

void remove_quietly(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
    for (const char* side : {"-wal", "-journal", "-shm"}) {
        fs::path s = p;
        s += side;
        fs::remove(s, ec);
    }
}

// Moves a finished temporary file into place, durably.
void publish(const fs::path& tmp, const fs::path& target, bool replace) {
    std::string error;
    if (!platform::sync_file(tmp, error)) {
        remove_quietly(tmp);
        throw ProjectError(Code::Io, error);
    }
    bool exists = false;
    const bool ok = replace ? platform::rename_replace(tmp, target, error)
                            : platform::rename_no_replace(tmp, target, exists, error);
    if (!ok) {
        remove_quietly(tmp);
        throw ProjectError(exists ? Code::Exists : Code::Io, error);
    }
    (void)platform::sync_folder(target.parent_path(), error);  // best effort
}

std::string sniff_media_type(const std::string& head, const fs::path& file) {
    auto starts = [&](std::string_view m) { return head.size() >= m.size() && head.compare(0, m.size(), m) == 0; };
    if (starts("%PDF-")) return "application/pdf";
    if (starts("\x89PNG\r\n\x1a\n")) return "image/png";
    if (starts("\xff\xd8\xff")) return "image/jpeg";
    if (starts("GIF87a") || starts("GIF89a")) return "image/gif";
    if (starts("BM")) return "image/bmp";
    if (starts("8BPS")) return "image/vnd.adobe.photoshop";
    if (starts("#?RADIANCE") || starts("#?RGBE")) return "image/vnd.radiance";
    if (head.size() >= 2 && head[0] == 'P' && head[1] >= '1' && head[1] <= '7') return "image/x-portable-anymap";
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".tga") return "image/x-tga";
    return "application/octet-stream";
}

struct FileDigest {
    std::uint64_t size = 0;
    std::string sha256;
    std::string head;  // first bytes
};

FileDigest digest_file(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        throw ProjectError(Code::Io, "cannot read " + file.string() + ": not a file");
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw ProjectError(Code::Io, "cannot read " + file.string() + ": " + std::strerror(errno));
    }
    FileDigest d;
    util::Sha256 h;
    std::vector<char> buf(kChunk);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = static_cast<std::size_t>(in.gcount());
        if (n == 0) break;
        if (d.head.size() < 16) d.head.append(buf.data(), std::min<std::size_t>(n, 16 - d.head.size()));
        h.update(buf.data(), n);
        d.size += n;
        if (d.size > kMaxSourceBytes) {
            throw ProjectError(Code::Invalid, file.string() + ": larger than 1 GB, too large for a project");
        }
    }
    if (in.bad()) {
        throw ProjectError(Code::Io, "cannot read " + file.string());
    }
    d.sha256 = h.finish_hex();
    return d;
}

constexpr const char* kFloorColumns =
    "id, name, level, height_m, source, page, example_id, plan_width, plan_height, slab_attenuation_db";

building::Floor floor_from(const sql::Stmt& s) {
    // Columns as kFloorColumns.
    building::Floor f;
    f.id = s.text(0);
    f.name = s.text(1);
    f.level = static_cast<int>(s.i64(2));
    f.height_m = s.f64(3);
    f.source = s.is_null(4) ? std::string() : s.text(4);
    f.page = static_cast<int>(s.i64(5));
    f.example_id = s.text(6);
    f.plan_width = s.f64(7);
    f.plan_height = s.f64(8);
    f.slab_attenuation_db = s.opt_f64(9);
    return f;
}

// ?2..?10 in the order of kFloorColumns after the id.
void bind_floor_fields(sql::Stmt& s, const building::Floor& f) {
    s.bind(2, f.name).bind(3, f.level).bind(4, f.height_m);
    if (f.source.empty()) s.bind_null(5);
    else s.bind(5, f.source);
    s.bind(6, f.page).bind(7, f.example_id).bind(8, f.plan_width).bind(9, f.plan_height);
    s.bind(10, f.slab_attenuation_db);
}

constexpr const char* kSourceColumns = "id, file_name, media_type, size, sha256, storage, reference, added_utc";

SourceFile source_from(const sql::Stmt& s) {
    SourceFile f;
    f.plan.id = s.text(0);
    f.plan.file_name = s.text(1);
    f.media_type = s.text(2);
    f.size = static_cast<std::uint64_t>(std::max<std::int64_t>(0, s.i64(3)));
    f.plan.sha256 = s.text(4);
    f.plan.embedded = s.text(5) == "embedded";
    f.reference = s.text(6);
    f.added_utc = s.text(7);
    return f;
}

// Data of later schema versions (docs/formats/PROJECT.md, hooks): refused
// rather than dropped.
void require_storable(const building::Floor& f) {
    auto refuse = [&](const char* what, const char* step) {
        throw ProjectError(Code::Unsupported, "floor '" + f.name + "': " + what +
                                                  " cannot be stored in project schema " +
                                                  std::to_string(kSchemaVersion) + " yet (" + step + ")");
    };
    if (f.scale != building::Scale{}) refuse("a scale", "IND-08");
    if (f.reference_point) refuse("a reference point", "IND-09");
    if (!f.walls.empty()) refuse("walls", "IND-11");
    if (!f.antennas.empty()) refuse("antennas", "IND-12");
}

void require_storable(const building::Building& b) {
    if (!b.cells.empty()) {
        throw ProjectError(Code::Unsupported, "cells cannot be stored in project schema " +
                                                  std::to_string(kSchemaVersion) + " yet (IND-12)");
    }
    if (b.wall_classes != building::default_wall_classes()) {
        throw ProjectError(Code::Unsupported, "an edited wall-class table cannot be stored in project schema " +
                                                  std::to_string(kSchemaVersion) + " yet (IND-11)");
    }
    for (const building::Floor& f : b.floors) require_storable(f);
}

void require_floor_values(const building::Floor& f) {
    if (f.name.empty()) throw ProjectError(Code::Invalid, "a floor needs a name");
    if (f.level < -kMaxLevel || f.level > kMaxLevel) {
        throw ProjectError(Code::Invalid, "floor level must be within -" + std::to_string(kMaxLevel) + ".." +
                                              std::to_string(kMaxLevel));
    }
}

// The model with the change must be valid; the first problem is the reason.
void require_valid(const building::Building& b) {
    const std::vector<std::string> problems = b.validate();
    if (!problems.empty()) {
        throw ProjectError(Code::Invalid, problems.front());
    }
}

// n of "<prefix><n>", 0 when `id` has another form.
std::int64_t id_number(const std::string& id, std::string_view prefix) {
    if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0 || id.size() - prefix.size() > 15) {
        return 0;
    }
    std::int64_t n = 0;
    for (std::size_t i = prefix.size(); i < id.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(id[i]))) return 0;
        n = n * 10 + (id[i] - '0');
    }
    return n;
}

std::string locked_message(const fs::path& p) {
    return p.string() + " is open in another Yantra window or program";
}

// The result of opening and checking a file.
struct Opened {
    sql::Db db;
    int version = 0;
    fs::path backup;
};

// Opens and locks an existing file; checks and migrates it.
Opened open_db(const fs::path& p, const ProjectOptions& options, const std::string& now) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
        throw ProjectError(Code::Io, p.string() + ": no such project file");
    }
    Opened o;
    sql::Db& db = o.db;
    db.open(p.string(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW);
    // One process at a time: the lock is taken at the first access and
    // kept until close (and no shared-memory index next to the file).
    db.exec("PRAGMA locking_mode = EXCLUSIVE", "lock");
    std::int64_t app = 0;
    int version = 0;
    try {
        app = db.int_value("PRAGMA application_id", "read").value_or(0);
        version = static_cast<int>(db.int_value("PRAGMA user_version", "read").value_or(0));
    } catch (const ProjectError& e) {
        if (e.code() == Code::Locked) throw ProjectError(Code::Locked, locked_message(p));
        if (e.code() == Code::NotAProject || e.code() == Code::Damaged) {
            throw ProjectError(Code::NotAProject, p.string() + " is not a Yantra project (" + e.what() + ")");
        }
        throw;
    }
    if (app != kApplicationId) {
        throw ProjectError(Code::NotAProject, p.string() + " is not a Yantra project");
    }
    const int target = kSchemaVersion + static_cast<int>(options.testing_extra_steps.size());
    if (version > target) {
        throw ProjectError(Code::TooNew, p.string() + " was saved by a newer Yantra (project schema " +
                                             std::to_string(version) + ", this version reads up to " +
                                             std::to_string(target) + "); it was not changed");
    }
    if (version < 1) {
        throw ProjectError(Code::NotAProject, p.string() + " is not a Yantra project (no schema)");
    }
    // A damaged or cut file is refused before anything is written to it.
    try {
        sql::Stmt check(db, "PRAGMA quick_check(5)");
        std::string problems;
        while (check.step()) {
            const std::string line = check.text(0);
            if (line != "ok") problems += (problems.empty() ? "" : "; ") + line;
        }
        if (!problems.empty()) {
            throw ProjectError(Code::Damaged, p.string() + " is damaged: " + problems);
        }
        const auto format = db.text_value("SELECT value FROM meta WHERE key = 'format'", "read");
        if (!format || *format != kFormat) {
            throw ProjectError(Code::NotAProject, p.string() + " is not a Yantra project (no format mark)");
        }
    } catch (const ProjectError& e) {
        if (e.code() == Code::Locked) throw ProjectError(Code::Locked, locked_message(p));
        if (e.code() == Code::Damaged && std::string_view(e.what()).find(" is damaged") == std::string_view::npos) {
            throw ProjectError(Code::Damaged, p.string() + " is damaged: " + e.what());
        }
        throw;
    }
    try {
        const auto mode = db.text_value("PRAGMA journal_mode = WAL", "journal");
        if (!mode || *mode != "wal") {
            throw ProjectError(Code::Io, p.string() + ": cannot use a write-ahead log");
        }
        // Takes the write lock now, so a second instance is refused at
        // once rather than at its first change.
        db.exec("BEGIN IMMEDIATE; COMMIT", "lock");
    } catch (const ProjectError& e) {
        if (e.code() == Code::Locked) throw ProjectError(Code::Locked, locked_message(p));
        throw;
    }
    db.exec("PRAGMA synchronous = FULL; PRAGMA foreign_keys = ON; PRAGMA cell_size_check = ON", "settings");
    o.version = version;
    if (version < target) {
        const std::string tool = options.tool.empty() ? default_tool() : options.tool;
        if (options.backup_before_migration) {
            // The older Yantra can still open the backup.
            fs::path backup = p;
            backup += ".schema" + std::to_string(version) + ".bak";
            if (!fs::exists(backup, ec)) {
                const fs::path tmp = temp_name(backup);
                try {
                    sql::Stmt s(db, "VACUUM INTO ?1");
                    s.bind(1, tmp.string()).run();
                } catch (...) {
                    remove_quietly(tmp);
                    throw;
                }
                publish(tmp, backup, false);
            }
            o.backup = backup;
        }
        schema::migrate(db, version, target, now, tool, options.testing_extra_steps, options.fault);
        db.exec("PRAGMA wal_checkpoint(TRUNCATE)", "migrate");
    }
    return o;
}

// True when `p` is a database another connection holds locked. The probe
// takes the write lock itself in exclusive mode, so it leaves no
// shared-memory or log file behind.
bool locked_elsewhere(const fs::path& p) {
    try {
        sql::Db probe;
        probe.open(p.string(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW);
        probe.exec("PRAGMA locking_mode = EXCLUSIVE", "probe");
        probe.exec("BEGIN IMMEDIATE", "probe");
        probe.exec("ROLLBACK", "probe");
    } catch (const ProjectError& e) {
        return e.code() == Code::Locked;
    }
    return false;
}

// Side files of `p` (write-ahead log, journal, shared memory).
void remove_side_files(const fs::path& p) {
    std::error_code ec;
    for (const char* side : {"-wal", "-journal", "-shm"}) {
        fs::path s = p;
        s += side;
        fs::remove(s, ec);
    }
}

}  // namespace

// ---- Impl ---------------------------------------------------------------

struct Project::Impl {
    fs::path path;
    sql::Db db;
    ProjectOptions options;
    int opened_version = 0;
    fs::path backup;
    mutable std::mutex mutex;
    mutable std::mutex listeners_mutex;
    std::map<int, Listener> listeners;
    int next_token = 1;

    std::string now() const { return options.clock ? options.clock() : system_now_utc(); }
    const std::string& tool() const { return options.tool; }
    void fault(std::string_view point) const {
        if (options.fault) options.fault(point);
    }

    void require_open() const {
        if (!db) throw ProjectError(Code::Closed, "the project is closed");
    }

    // Marks the project modified (inside the caller's transaction).
    void touch() {
        sql::Stmt s(db, "UPDATE project SET modified_utc = ?1, modified_with = ?2 WHERE id = 1");
        s.bind(1, now()).bind(2, tool()).run();
    }

    std::vector<building::Floor> floors_locked() const {
        std::vector<building::Floor> out;
        {
            sql::Stmt s(db, std::string("SELECT ") + kFloorColumns + " FROM floors ORDER BY level");
            while (s.step()) out.push_back(floor_from(s));
        }
        for (building::Floor& f : out) load_trim(f);
        return out;
    }

    std::optional<building::Floor> floor_locked(const std::string& id) const {
        std::optional<building::Floor> f;
        {
            sql::Stmt s(db, std::string("SELECT ") + kFloorColumns + " FROM floors WHERE id = ?1");
            s.bind(1, id);
            if (!s.step()) return std::nullopt;
            f = floor_from(s);
        }
        load_trim(*f);
        return f;
    }

    // The trim of a floor (schema 2, IND-07): shape and rotation in the
    // floors row, the outline in floor_outline.
    void load_trim(building::Floor& f) const {
        sql::Stmt s(db, "SELECT trim_shape, quarter_turns, deskew_deg FROM floors WHERE id = ?1");
        s.bind(1, f.id);
        if (!s.step()) return;
        const std::string shape = s.text(0);
        f.trim.shape = shape == "rectangle" ? building::Trim::Shape::Rectangle
                       : shape == "polygon" ? building::Trim::Shape::Polygon
                                            : building::Trim::Shape::None;
        f.trim.quarter_turns = static_cast<int>(s.i64(1));
        f.trim.deskew_deg = s.f64(2);
        f.trim.outline.clear();
        sql::Stmt o(db, "SELECT x, y FROM floor_outline WHERE floor = ?1 ORDER BY idx");
        o.bind(1, f.id);
        while (o.step()) f.trim.outline.push_back({o.f64(0), o.f64(1)});
    }

    void store_trim(const building::Floor& f) {
        const building::Trim& t = f.trim;
        const char* shape = t.shape == building::Trim::Shape::Rectangle ? "rectangle"
                            : t.shape == building::Trim::Shape::Polygon ? "polygon"
                                                                        : "none";
        sql::Stmt s(db, "UPDATE floors SET trim_shape = ?2, quarter_turns = ?3, deskew_deg = ?4 WHERE id = ?1");
        s.bind(1, f.id).bind(2, shape).bind(3, t.quarter_turns).bind(4, t.deskew_deg).run();
        sql::Stmt d(db, "DELETE FROM floor_outline WHERE floor = ?1");
        d.bind(1, f.id).run();
        sql::Stmt in(db, "INSERT INTO floor_outline (floor, idx, x, y) VALUES (?1, ?2, ?3, ?4)");
        for (std::size_t i = 0; i < t.outline.size(); ++i) {
            in.reset();
            in.bind(1, f.id).bind(2, static_cast<std::int64_t>(i)).bind(3, t.outline[i].x).bind(4, t.outline[i].y).run();
        }
    }

    // The page sizes recorded for a source (empty when not known yet).
    std::vector<SourcePage> pages_locked(const std::string& id) const {
        sql::Stmt s(db, "SELECT width, height, units_per_inch FROM source_pages WHERE source = ?1 ORDER BY page");
        s.bind(1, id);
        std::vector<SourcePage> out;
        while (s.step()) out.push_back({s.f64(0), s.f64(1), s.f64(2)});
        return out;
    }

    // A floor's page must exist when the source's pages are known.
    void require_page(const building::Floor& f) const {
        if (f.source.empty()) return;
        const std::vector<SourcePage> pages = pages_locked(f.source);
        if (!pages.empty() && f.page >= static_cast<int>(pages.size())) {
            throw ProjectError(Code::Invalid, "floor '" + f.name + "': source " + f.source + " has " +
                                                  std::to_string(pages.size()) + " page(s), no page " +
                                                  std::to_string(f.page + 1));
        }
    }

    std::vector<SourceFile> sources_locked() const {
        sql::Stmt s(db, std::string("SELECT ") + kSourceColumns + " FROM sources ORDER BY row_id");
        std::vector<SourceFile> out;
        while (s.step()) out.push_back(source_from(s));
        return out;
    }

    std::optional<SourceFile> source_locked(const std::string& id) const {
        sql::Stmt s(db, std::string("SELECT ") + kSourceColumns + " FROM sources WHERE id = ?1");
        s.bind(1, id);
        if (!s.step()) return std::nullopt;
        return source_from(s);
    }

    std::optional<std::string> source_by_hash(const std::string& sha) const {
        sql::Stmt s(db, "SELECT id FROM sources WHERE sha256 = ?1");
        s.bind(1, sha);
        if (!s.step()) return std::nullopt;
        return s.text(0);
    }

    std::int64_t source_rowid(const std::string& id) const {
        sql::Stmt s(db, "SELECT row_id FROM sources WHERE id = ?1");
        s.bind(1, id);
        if (!s.step()) throw ProjectError(Code::NotFound, "no source " + id);
        return s.i64(0);
    }

    building::Building building_locked() const {
        building::Building b;
        sql::Stmt s(db, "SELECT name, floor_attenuation_db FROM building WHERE id = 1");
        if (!s.step()) throw ProjectError(Code::Damaged, path.string() + ": no building row");
        b.name = s.text(0);
        b.floor_attenuation_db = s.f64(1);
        for (SourceFile& src : sources_locked()) b.sources.push_back(std::move(src.plan));
        b.floors = floors_locked();
        return b;
    }

    // Next "<prefix><n>" from the meta counter `key`, skipping used ids;
    // the counter moves on, so an id is never handed out twice.
    std::string next_id(const char* key, const char* prefix, const char* table) {
        const auto next = db.text_value(std::string("SELECT value FROM meta WHERE key = ") + sql::quote(key), "id");
        std::int64_t n = next ? std::max<std::int64_t>(1, id_number(std::string("n") + *next, "n")) : 1;
        sql::Stmt used(db, std::string("SELECT count(*) FROM ") + table + " WHERE id = ?1");
        for (;; ++n) {
            used.reset();
            used.bind(1, prefix + std::to_string(n));
            used.step();
            if (used.i64(0) == 0) break;
        }
        bump_counter(key, n + 1);
        return prefix + std::to_string(n);
    }

    // Raises the counter `key` to at least `n`.
    void bump_counter(const char* key, std::int64_t n) {
        const auto cur = db.text_value(std::string("SELECT value FROM meta WHERE key = ") + sql::quote(key), "id");
        const std::int64_t have = cur ? id_number(std::string("n") + *cur, "n") : 0;
        if (n <= have) return;
        sql::Stmt up(db, "INSERT OR REPLACE INTO meta (key, value) VALUES (?1, ?2)");
        up.bind(1, key).bind(2, std::to_string(n)).run();
    }

    // Moves every level out of the way of the UNIQUE constraint.
    void park_levels() {
        db.exec("UPDATE floors SET level = level - " + std::to_string(kLevelOffset) + " * 3", "levels");
    }

    std::uint64_t file_bytes() const {
        std::error_code ec;
        std::uint64_t n = fs::file_size(path, ec);
        fs::path wal = path;
        wal += "-wal";
        const auto w = fs::file_size(wal, ec);
        if (!ec) n += w;
        return n;
    }

    std::vector<std::uint8_t> read_embedded(const SourceFile& src) const {
        const std::int64_t rowid = source_rowid(src.plan.id);
        sqlite3_blob* blob = nullptr;
        int rc = sqlite3_blob_open(db.get(), "main", "sources", "content", rowid, 0, &blob);
        if (rc != SQLITE_OK) {
            sqlite3_blob_close(blob);
            sql::fail(rc, db.get(), "read source");
        }
        const auto n = static_cast<std::uint64_t>(std::max(0, sqlite3_blob_bytes(blob)));
        // A damaged file may claim more than it holds.
        if (n > file_bytes()) {
            sqlite3_blob_close(blob);
            throw ProjectError(Code::Damaged, "source " + src.plan.id + " is damaged (size)");
        }
        std::vector<std::uint8_t> data(n);
        for (std::uint64_t at = 0; at < n && rc == SQLITE_OK; at += kChunk) {
            const auto len = static_cast<int>(std::min<std::uint64_t>(kChunk, n - at));
            rc = sqlite3_blob_read(blob, data.data() + at, len, static_cast<int>(at));
        }
        sqlite3_blob_close(blob);
        if (rc != SQLITE_OK) sql::fail(rc, db.get(), "read source");
        return data;
    }
};

namespace {

std::vector<std::uint8_t> read_whole(const fs::path& file, const SourceFile& src) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw ProjectError(Code::Io, "the cached file of source '" + src.plan.file_name + "' is missing: " +
                                         file.string());
    }
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size != src.size) {
        throw ProjectError(Code::Damaged, file.string() + " is not the file source '" + src.plan.file_name +
                                              "' uses (size)");
    }
    std::vector<std::uint8_t> data(size);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(in.gcount()) != size) {
        throw ProjectError(Code::Io, "cannot read " + file.string());
    }
    return data;
}

void require_hash(const std::vector<std::uint8_t>& data, const SourceFile& src) {
    util::Sha256 h;
    h.update(data.data(), data.size());
    if (h.finish_hex() != src.plan.sha256) {
        throw ProjectError(Code::Damaged, "source '" + src.plan.file_name + "' does not match its SHA-256");
    }
}

}  // namespace

// ---- Project ------------------------------------------------------------

Project::Project(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Project::~Project() {
    try {
        close();
    } catch (...) {  // NOLINT(bugprone-empty-catch): destructors never throw
    }
}

std::unique_ptr<Project> Project::create(const fs::path& path, const ProjectInfo& info, ProjectOptions options) {
    if (info.name.empty()) {
        throw ProjectError(Code::Invalid, "a project needs a name");
    }
    std::error_code ec;
    if (fs::exists(path, ec) || fs::is_symlink(path, ec)) {
        throw ProjectError(Code::Exists, path.string() + ": file exists");
    }
    if (options.tool.empty()) options.tool = default_tool();
    auto impl = std::make_unique<Impl>();
    impl->options = std::move(options);
    const fs::path tmp = temp_name(path);
    try {
        sql::Db db;
        db.open(tmp.string(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOFOLLOW);
        db.exec("PRAGMA application_id = " + std::to_string(kApplicationId), "create");
        const std::string now = impl->now();
        schema::migrate(db, 0, kSchemaVersion, now, impl->tool());
        sql::Transaction tx(db);
        sql::Stmt s(db, "INSERT INTO project (id, name, site, operator, notes, created_utc, modified_utc, "
                        "created_with, modified_with) VALUES (1, ?1, ?2, ?3, ?4, ?5, ?5, ?6, ?6)");
        s.bind(1, info.name).bind(2, info.site).bind(3, info.operator_name).bind(4, info.notes);
        s.bind(5, now).bind(6, impl->tool()).run();
        sql::Stmt b(db, "UPDATE building SET name = ?1 WHERE id = 1");
        b.bind(1, info.site.empty() ? info.name : info.site).run();
        tx.commit();
        db.close();
        impl->fault("create:written");
        publish(tmp, path, false);
    } catch (...) {
        remove_quietly(tmp);
        throw;
    }
    Opened o = open_db(path, impl->options, impl->now());
    impl->db = std::move(o.db);
    impl->opened_version = o.version;
    impl->path = path;
    return std::unique_ptr<Project>(new Project(std::move(impl)));
}

std::unique_ptr<Project> Project::open(const fs::path& path, ProjectOptions options) {
    if (options.tool.empty()) options.tool = default_tool();
    auto impl = std::make_unique<Impl>();
    impl->options = std::move(options);
    Opened o = open_db(path, impl->options, impl->now());
    impl->db = std::move(o.db);
    impl->opened_version = o.version;
    impl->backup = std::move(o.backup);
    impl->path = path;
    return std::unique_ptr<Project>(new Project(std::move(impl)));
}

const fs::path& Project::path() const { return impl_->path; }
int Project::opened_version() const { return impl_->opened_version; }
const fs::path& Project::migration_backup() const { return impl_->backup; }

bool Project::is_open() const {
    const std::lock_guard l(impl_->mutex);
    return static_cast<bool>(impl_->db);
}

void Project::notify(const Change& change) const {
    std::vector<Listener> copy;
    {
        const std::lock_guard l(impl_->listeners_mutex);
        for (const auto& [token, fn] : impl_->listeners) copy.push_back(fn);
    }
    for (const Listener& fn : copy) fn(change);
}

int Project::subscribe(Listener listener) {
    const std::lock_guard l(impl_->listeners_mutex);
    const int token = impl_->next_token++;
    impl_->listeners[token] = std::move(listener);
    return token;
}

void Project::unsubscribe(int token) {
    const std::lock_guard l(impl_->listeners_mutex);
    impl_->listeners.erase(token);
}

// ---- metadata -------------------------------------------------------------

ProjectMeta Project::meta() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    sql::Stmt s(impl_->db, "SELECT name, site, operator, notes, created_utc, modified_utc, created_with, "
                           "modified_with FROM project WHERE id = 1");
    if (!s.step()) throw ProjectError(Code::Damaged, impl_->path.string() + ": no project row");
    ProjectMeta m;
    m.info.name = s.text(0);
    m.info.site = s.text(1);
    m.info.operator_name = s.text(2);
    m.info.notes = s.text(3);
    m.created_utc = s.text(4);
    m.modified_utc = s.text(5);
    m.created_with = s.text(6);
    m.modified_with = s.text(7);
    m.schema_version = static_cast<int>(impl_->db.int_value("PRAGMA user_version", "read").value_or(0));
    return m;
}

void Project::set_info(const ProjectInfo& info) {
    if (info.name.empty()) throw ProjectError(Code::Invalid, "a project needs a name");
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt s(impl_->db, "UPDATE project SET name = ?1, site = ?2, operator = ?3, notes = ?4 WHERE id = 1");
        s.bind(1, info.name).bind(2, info.site).bind(3, info.operator_name).bind(4, info.notes).run();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Info, ""});
}

void Project::set_building_name(const std::string& name) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt s(impl_->db, "UPDATE building SET name = ?1 WHERE id = 1");
        s.bind(1, name).run();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Building, ""});
}

void Project::set_floor_attenuation_db(double db) {
    if (!std::isfinite(db) || db < 0.0) throw ProjectError(Code::Invalid, "floor attenuation must be >= 0 dB");
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt s(impl_->db, "UPDATE building SET floor_attenuation_db = ?1 WHERE id = 1");
        s.bind(1, db).run();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Building, ""});
}

// ---- floors ---------------------------------------------------------------

std::vector<building::Floor> Project::floors() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->floors_locked();
}

std::optional<building::Floor> Project::floor(const std::string& id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->floor_locked(id);
}

std::string Project::add_floor(building::Floor floor) {
    require_floor_values(floor);
    require_storable(floor);
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        if (!floor.source.empty() && !impl_->source_locked(floor.source)) {
            throw ProjectError(Code::NotFound, "no source " + floor.source);
        }
        building::Building b = impl_->building_locked();
        if (floor.id.empty()) {
            floor.id = impl_->next_id("next_floor_number", "floor-", "floors");
        } else {
            if (b.find_floor(floor.id) != nullptr) throw ProjectError(Code::Invalid, "floor id " + floor.id + " is used");
            impl_->bump_counter("next_floor_number", id_number(floor.id, "floor-") + 1);
        }
        if (const building::Floor* other = b.floor_at_level(floor.level)) {
            throw ProjectError(Code::Invalid, "level " + std::to_string(floor.level) + " is already used by floor '" +
                                                  other->name + "'");
        }
        b.floors.push_back(floor);
        require_valid(b);
        impl_->require_page(floor);
        sql::Stmt s(impl_->db, std::string("INSERT INTO floors (") + kFloorColumns +
                                   ", created_utc) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)");
        s.bind(1, floor.id);
        bind_floor_fields(s, floor);
        s.bind(11, impl_->now()).run();
        impl_->store_trim(floor);
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::FloorAdded, floor.id});
    return floor.id;
}

void Project::update_floor(const building::Floor& floor) {
    require_floor_values(floor);
    require_storable(floor);
    bool moved = false;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        building::Floor* old = b.find_floor(floor.id);
        if (old == nullptr) throw ProjectError(Code::NotFound, "no floor " + floor.id);
        if (!floor.source.empty() && !impl_->source_locked(floor.source)) {
            throw ProjectError(Code::NotFound, "no source " + floor.source);
        }
        if (const building::Floor* other = b.floor_at_level(floor.level); other != nullptr && other->id != floor.id) {
            throw ProjectError(Code::Invalid, "level " + std::to_string(floor.level) + " is already used by floor '" +
                                                  other->name + "'");
        }
        moved = old->level != floor.level;
        *old = floor;
        require_valid(b);
        impl_->require_page(floor);
        sql::Stmt s(impl_->db, "UPDATE floors SET name = ?2, level = ?3, height_m = ?4, source = ?5, page = ?6, "
                               "example_id = ?7, plan_width = ?8, plan_height = ?9, slab_attenuation_db = ?10 "
                               "WHERE id = ?1");
        s.bind(1, floor.id);
        bind_floor_fields(s, floor);
        s.run();
        impl_->store_trim(floor);
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::FloorChanged, floor.id});
    if (moved) notify({Change::Kind::FloorsReordered, floor.id});
}

void Project::rename_floor(const std::string& id, const std::string& name) {
    auto f = floor(id);
    if (!f) throw ProjectError(Code::NotFound, "no floor " + id);
    f->name = name;
    update_floor(*f);
}

void Project::set_floor_level(const std::string& id, int level, std::optional<double> height_m) {
    auto f = floor(id);
    if (!f) throw ProjectError(Code::NotFound, "no floor " + id);
    f->level = level;
    if (height_m) f->height_m = *height_m;
    update_floor(*f);
}

void Project::set_floor_height(const std::string& id, double height_m) {
    auto f = floor(id);
    if (!f) throw ProjectError(Code::NotFound, "no floor " + id);
    f->height_m = height_m;
    update_floor(*f);
}

void Project::move_floor(const std::string& id, std::size_t index) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        const std::vector<building::Floor> all = impl_->floors_locked();
        std::vector<int> levels;
        std::vector<std::string> ids;
        bool found = false;
        for (const building::Floor& f : all) {
            levels.push_back(f.level);
            if (f.id == id) found = true;
            else ids.push_back(f.id);
        }
        if (!found) throw ProjectError(Code::NotFound, "no floor " + id);
        ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(std::min(index, ids.size())), id);
        // The floors keep the set of levels, handed out in the new order.
        impl_->park_levels();
        sql::Stmt s(impl_->db, "UPDATE floors SET level = ?1 WHERE id = ?2");
        for (std::size_t i = 0; i < ids.size(); ++i) {
            s.reset();
            s.bind(1, levels[i]).bind(2, ids[i]).run();
        }
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::FloorsReordered, id});
}

void Project::remove_floor(const std::string& id) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        // Another floor may share this floor's scale (a later schema); the
        // model must stay valid.
        b.floors.erase(std::remove_if(b.floors.begin(), b.floors.end(),
                                      [&](const building::Floor& f) { return f.id == id; }),
                       b.floors.end());
        require_valid(b);
        sql::Stmt s(impl_->db, "DELETE FROM floors WHERE id = ?1");
        s.bind(1, id).run();
        if (impl_->db.changes() == 0) throw ProjectError(Code::NotFound, "no floor " + id);
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::FloorRemoved, id});
}

// ---- building model -------------------------------------------------------

building::Building Project::building() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->building_locked();
}

void Project::replace_building(const building::Building& b) {
    require_storable(b);
    for (const building::Floor& f : b.floors) require_floor_values(f);
    require_valid(b);
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        for (const building::PlanSource& src : b.sources) {
            const auto have = impl_->source_locked(src.id);
            if (!have) throw ProjectError(Code::NotFound, "no source " + src.id + " in the project");
            if (!src.sha256.empty() && src.sha256 != have->plan.sha256) {
                throw ProjectError(Code::Invalid, "source " + src.id + " has another SHA-256 in the project");
            }
        }
        sql::Stmt bs(impl_->db, "UPDATE building SET name = ?1, floor_attenuation_db = ?2 WHERE id = 1");
        bs.bind(1, b.name).bind(2, b.floor_attenuation_db).run();
        // Floors not in b go; the others are updated (keeping their
        // creation time) or added.
        for (const building::Floor& f : impl_->floors_locked()) {
            if (b.find_floor(f.id) == nullptr) {
                sql::Stmt d(impl_->db, "DELETE FROM floors WHERE id = ?1");
                d.bind(1, f.id).run();
            }
        }
        impl_->park_levels();
        sql::Stmt up(impl_->db, std::string("INSERT INTO floors (") + kFloorColumns +
                                    ", created_utc) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11) "
                                    "ON CONFLICT (id) DO UPDATE SET name = excluded.name, level = excluded.level, "
                                    "height_m = excluded.height_m, source = excluded.source, page = excluded.page, "
                                    "example_id = excluded.example_id, plan_width = excluded.plan_width, "
                                    "plan_height = excluded.plan_height, "
                                    "slab_attenuation_db = excluded.slab_attenuation_db");
        const std::string now = impl_->now();
        for (const building::Floor& f : b.floors) {
            up.reset();
            up.bind(1, f.id);
            bind_floor_fields(up, f);
            up.bind(11, now).run();
            impl_->require_page(f);
            impl_->store_trim(f);
            impl_->bump_counter("next_floor_number", id_number(f.id, "floor-") + 1);
        }
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Building, ""});
}

// ---- sources ----------------------------------------------------------------

AddSourceResult Project::add_source(const fs::path& file) {
    const FileDigest d = digest_file(file);
    AddSourceResult r;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        if (auto id = impl_->source_by_hash(d.sha256)) {
            return {std::move(*id), false};
        }
        sql::Transaction tx(impl_->db);
        r.id = impl_->next_id("next_source_number", "source-", "sources");
        sql::Stmt s(impl_->db, "INSERT INTO sources (id, file_name, media_type, size, sha256, storage, content, "
                               "added_utc) VALUES (?1, ?2, ?3, ?4, ?5, 'embedded', ?6, ?7)");
        s.bind(1, r.id).bind(2, file.filename().string()).bind(3, sniff_media_type(d.head, file));
        s.bind(4, static_cast<std::int64_t>(d.size)).bind(5, d.sha256).bind_zeroblob(6, static_cast<std::int64_t>(d.size));
        s.bind(7, impl_->now()).run();
        const std::int64_t rowid = impl_->db.last_rowid();
        r.added = true;
        // Second pass: copy in chunks, hashing again; a file that changed
        // since the first pass is refused.
        sqlite3_blob* blob = nullptr;
        int rc = sqlite3_blob_open(impl_->db.get(), "main", "sources", "content", rowid, 1, &blob);
        if (rc != SQLITE_OK) {
            sqlite3_blob_close(blob);
            sql::fail(rc, impl_->db.get(), "copy source");
        }
        std::ifstream in(file, std::ios::binary);
        util::Sha256 h;
        std::vector<char> buf(kChunk);
        std::uint64_t at = 0;
        bool too_long = false;
        while (in && rc == SQLITE_OK) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const auto n = static_cast<std::size_t>(in.gcount());
            if (n == 0) break;
            if (at + n > d.size) {
                too_long = true;
                break;
            }
            rc = sqlite3_blob_write(blob, buf.data(), static_cast<int>(n), static_cast<int>(at));
            h.update(buf.data(), n);
            at += n;
        }
        sqlite3_blob_close(blob);
        if (rc != SQLITE_OK) sql::fail(rc, impl_->db.get(), "copy source");
        if (too_long || at != d.size || h.finish_hex() != d.sha256) {
            throw ProjectError(Code::Io, file.string() + " changed while it was copied into the project");
        }
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::SourceAdded, r.id});
    return r;
}

AddSourceResult Project::add_source_reference(const fs::path& cached_file) {
    const FileDigest d = digest_file(cached_file);
    AddSourceResult r;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        if (auto id = impl_->source_by_hash(d.sha256)) {
            return {std::move(*id), false};
        }
        sql::Transaction tx(impl_->db);
        r.id = impl_->next_id("next_source_number", "source-", "sources");
        sql::Stmt s(impl_->db, "INSERT INTO sources (id, file_name, media_type, size, sha256, storage, reference, "
                               "added_utc) VALUES (?1, ?2, ?3, ?4, ?5, 'reference', ?6, ?7)");
        s.bind(1, r.id).bind(2, cached_file.filename().string()).bind(3, sniff_media_type(d.head, cached_file));
        s.bind(4, static_cast<std::int64_t>(d.size)).bind(5, d.sha256);
        s.bind(6, fs::absolute(cached_file).lexically_normal().string()).bind(7, impl_->now()).run();
        r.added = true;
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::SourceAdded, r.id});
    return r;
}

std::vector<SourceFile> Project::sources() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->sources_locked();
}

std::optional<SourceFile> Project::source(const std::string& id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->source_locked(id);
}

std::vector<std::uint8_t> Project::read_source(const std::string& id) const {
    std::optional<SourceFile> src;
    std::vector<std::uint8_t> data;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        src = impl_->source_locked(id);
        if (!src) throw ProjectError(Code::NotFound, "no source " + id);
        if (src->plan.embedded) data = impl_->read_embedded(*src);
    }
    if (!src->plan.embedded) data = read_whole(src->reference, *src);
    require_hash(data, *src);
    return data;
}

std::vector<std::uint8_t> Project::read_source_from(const std::string& id, const fs::path& file) const {
    const auto src = source(id);
    if (!src) throw ProjectError(Code::NotFound, "no source " + id);
    std::vector<std::uint8_t> data = read_whole(file, *src);
    require_hash(data, *src);
    return data;
}

void Project::export_source(const std::string& id, const fs::path& file) const {
    const std::vector<std::uint8_t> data = read_source(id);
    const fs::path tmp = temp_name(file);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            remove_quietly(tmp);
            throw ProjectError(Code::Io, "cannot write " + tmp.string());
        }
    }
    publish(tmp, file, true);
}

std::vector<SourcePage> Project::source_pages(const std::string& id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    if (!impl_->source_locked(id)) throw ProjectError(Code::NotFound, "no source " + id);
    return impl_->pages_locked(id);
}

void Project::set_source_pages(const std::string& id, const std::vector<SourcePage>& pages) {
    if (pages.empty()) throw ProjectError(Code::Invalid, "a source has at least one page");
    for (const SourcePage& p : pages) {
        if (!(p.width > 0.0) || !(p.height > 0.0) || !std::isfinite(p.width) || !std::isfinite(p.height) ||
            !(p.units_per_inch >= 0.0) || !std::isfinite(p.units_per_inch)) {
            throw ProjectError(Code::Invalid, "page sizes must be finite and > 0");
        }
    }
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        if (!impl_->source_locked(id)) throw ProjectError(Code::NotFound, "no source " + id);
        sql::Stmt used(impl_->db, "SELECT name, page FROM floors WHERE source = ?1 AND page >= ?2 LIMIT 1");
        used.bind(1, id).bind(2, static_cast<std::int64_t>(pages.size()));
        if (used.step()) {
            throw ProjectError(Code::Invalid, "floor '" + used.text(0) + "' uses page " +
                                                  std::to_string(used.i64(1) + 1) + " of source " + id + ", which has " +
                                                  std::to_string(pages.size()) + " page(s)");
        }
        sql::Stmt d(impl_->db, "DELETE FROM source_pages WHERE source = ?1");
        d.bind(1, id).run();
        sql::Stmt in(impl_->db, "INSERT INTO source_pages (source, page, width, height, units_per_inch) "
                                "VALUES (?1, ?2, ?3, ?4, ?5)");
        for (std::size_t i = 0; i < pages.size(); ++i) {
            in.reset();
            in.bind(1, id).bind(2, static_cast<std::int64_t>(i)).bind(3, pages[i].width).bind(4, pages[i].height);
            in.bind(5, pages[i].units_per_inch).run();
        }
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::SourceChanged, id});
}

void Project::remove_source(const std::string& id) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt used(impl_->db, "SELECT name FROM floors WHERE source = ?1 ORDER BY level LIMIT 1");
        used.bind(1, id);
        if (used.step()) {
            throw ProjectError(Code::InUse, "source " + id + " is used by floor '" + used.text(0) + "'");
        }
        sql::Stmt s(impl_->db, "DELETE FROM sources WHERE id = ?1");
        s.bind(1, id).run();
        if (impl_->db.changes() == 0) throw ProjectError(Code::NotFound, "no source " + id);
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::SourceRemoved, id});
}

// ---- file -------------------------------------------------------------------

void Project::save() {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        impl_->db.exec("PRAGMA wal_checkpoint(TRUNCATE)", "save");
    }
    notify({Change::Kind::Saved, ""});
}

void Project::save_as(const fs::path& path, bool replace) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        std::error_code ec;
        const bool exists = fs::exists(path, ec);
        if (exists && fs::equivalent(path, impl_->path, ec)) {
            throw ProjectError(Code::Invalid, path.string() + " is the open project; use save()");
        }
        if (exists && !replace) throw ProjectError(Code::Exists, path.string() + ": file exists");
        if (exists && locked_elsewhere(path)) throw ProjectError(Code::Locked, locked_message(path));
        const fs::path tmp = temp_name(path);
        try {
            sql::Stmt s(impl_->db, "VACUUM INTO ?1");
            s.bind(1, tmp.string()).run();
            impl_->fault("save_as:written");
            // A log of the replaced file must never be applied to the new
            // one (SQLite would read it as the new file's log).
            if (exists) remove_side_files(path);
            publish(tmp, path, replace);
        } catch (...) {
            remove_quietly(tmp);
            throw;
        }
        // Continue with the copy; the old file is left as it is.
        Opened o = open_db(path, impl_->options, impl_->now());
        impl_->db.exec("PRAGMA wal_checkpoint(TRUNCATE)", "save");
        impl_->db = std::move(o.db);
        impl_->path = path;
        impl_->backup.clear();
    }
    notify({Change::Kind::Saved, ""});
}

void Project::close() {
    const std::lock_guard l(impl_->mutex);
    if (!impl_->db) return;
    try {
        impl_->db.exec("PRAGMA wal_checkpoint(TRUNCATE)", "close");
    } catch (...) {
        impl_->db.close();
        throw;
    }
    impl_->db.close();
}

std::vector<std::string> Project::check() const {
    std::vector<std::string> problems;
    std::vector<std::string> ids;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        try {
            sql::Stmt s(impl_->db, "PRAGMA integrity_check(100)");
            while (s.step()) {
                const std::string line = s.text(0);
                if (line != "ok") problems.push_back(line);
            }
            sql::Stmt fk(impl_->db, "PRAGMA foreign_key_check");
            while (fk.step()) problems.push_back("foreign key: " + fk.text(0) + " row " + fk.text(1));
            for (const std::string& p : impl_->building_locked().validate()) problems.push_back("building: " + p);
            sql::Stmt src(impl_->db, "SELECT id FROM sources ORDER BY row_id");
            while (src.step()) ids.push_back(src.text(0));
        } catch (const ProjectError& e) {
            problems.emplace_back(e.what());
            return problems;
        }
    }
    for (const std::string& id : ids) {
        try {
            (void)read_source(id);
        } catch (const ProjectError& e) {
            if (e.code() == Code::Damaged) problems.emplace_back(e.what());
        }
    }
    return problems;
}

// ---- testing ----------------------------------------------------------------

namespace testing {

void create_at_version(const fs::path& path, int version, const ProjectInfo& info) {
    if (version < 1 || version > kSchemaVersion) {
        throw ProjectError(Code::Invalid, "no schema version " + std::to_string(version));
    }
    sql::Db db;
    db.open(path.string(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("PRAGMA application_id = " + std::to_string(kApplicationId), "create");
    const std::string now = "2026-01-01T00:00:00Z";
    const std::string tool = "Yantra (schema " + std::to_string(version) + ")";
    schema::migrate(db, 0, version, now, tool);
    sql::Stmt s(db, "INSERT INTO project (id, name, site, operator, notes, created_utc, modified_utc, "
                    "created_with, modified_with) VALUES (1, ?1, ?2, ?3, ?4, ?5, ?5, ?6, ?6)");
    s.bind(1, info.name).bind(2, info.site).bind(3, info.operator_name).bind(4, info.notes);
    s.bind(5, now).bind(6, tool).run();
}

}  // namespace testing

}  // namespace yantra::project
