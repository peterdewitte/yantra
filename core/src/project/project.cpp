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
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

#include <unistd.h>

#include <nlohmann/json.hpp>

#include "schema.h"
#include "sqlite.h"
#include "yantra/core/build_info.h"
#include "yantra/core/building/edit.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/platform/file.h"
#include "yantra/core/util/sha256.h"
#include "yantra/core/platform/time.h"

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
    const std::tm tm = platform::utc_tm(t);
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
    "id, name, level, height_m, source, page, example_id, plan_width, plan_height";

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
    return f;
}

// ?2..?9 in the order of kFloorColumns after the id.
void bind_floor_fields(sql::Stmt& s, const building::Floor& f) {
    s.bind(2, f.name).bind(3, f.level).bind(4, f.height_m);
    if (f.source.empty()) s.bind_null(5);
    else s.bind(5, f.source);
    s.bind(6, f.page).bind(7, f.example_id).bind(8, f.plan_width).bind(9, f.plan_height);
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

// Text forms of the model's enums in the tables (docs/formats/PROJECT.md).
const char* scale_method_text(building::Scale::Method m) {
    switch (m) {
    case building::Scale::Method::TwoPoints: return "two_points";
    case building::Scale::Method::ScaleBar: return "scale_bar";
    case building::Scale::Method::Factor: return "factor";
    case building::Scale::Method::Shared: return "shared";
    case building::Scale::Method::None: break;
    }
    return "none";
}

building::Scale::Method scale_method_of(const std::string& t) {
    if (t == "two_points") return building::Scale::Method::TwoPoints;
    if (t == "scale_bar") return building::Scale::Method::ScaleBar;
    if (t == "factor") return building::Scale::Method::Factor;
    if (t == "shared") return building::Scale::Method::Shared;
    return building::Scale::Method::None;
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
    std::optional<MigrationNote> note;
};

bool has_table(sql::Db& db, const char* name) {
    sql::Stmt s(db, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = ?1");
    s.bind(1, name);
    s.step();
    return s.i64(0) > 0;
}

std::size_t count_rows(sql::Db& db, const char* table, const std::string& where = {}) {
    if (!has_table(db, table)) return 0;
    sql::Stmt s(db, std::string("SELECT count(*) FROM ") + table + (where.empty() ? "" : " WHERE " + where));
    s.step();
    return static_cast<std::size_t>(std::max<std::int64_t>(0, s.i64(0)));
}

// IND-46: what the step to schema 8 drops from a file of Yantra 0.3 or
// earlier (walls and wall classes from schema 3, ray-tracing maps from
// schema 6). nullopt when it drops nothing.
std::optional<MigrationNote> count_dropped(sql::Db& db, int version) {
    MigrationNote n;
    n.from_version = version;
    n.walls = count_rows(db, "walls");
    n.antennas = count_rows(db, "antennas");
    // The project's own classes; the default ones come with every file.
    n.wall_classes = count_rows(db, "wall_classes",
                                "id NOT IN ('concrete', 'brick', 'plasterboard', 'glass', 'wood', 'door', "
                                "'metal', 'coated_glass')");
    // Every stored map of schema 6 and 7 is a ray-tracing map.
    if (version < 8) n.ray_tracing_maps = count_rows(db, "interpolations");
    if (n.walls + n.antennas + n.wall_classes + n.ray_tracing_maps == 0) return std::nullopt;
    return n;
}

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
        if (version < 8) o.note = count_dropped(db, version);
        if (options.backup_before_migration) {
            // The older Yantra can still open the backup: a copy of the
            // file as it was (IND-46), after folding a log left by a crash
            // into it; VACUUM INTO when the file cannot be copied (e.g. a
            // byte range locked by SQLite on Windows).
            fs::path backup = p;
            backup += ".schema" + std::to_string(version) + ".bak";
            if (!fs::exists(backup, ec)) {
                const fs::path tmp = temp_name(backup);
                try {
                    db.exec("PRAGMA wal_checkpoint(TRUNCATE)", "backup");
                    std::error_code copy_ec;
                    fs::copy_file(p, tmp, fs::copy_options::overwrite_existing, copy_ec);
                    if (copy_ec) {
                        remove_quietly(tmp);
                        sql::Stmt s(db, "VACUUM INTO ?1");  // finalized before the move (Windows)
                        s.bind(1, tmp.string()).run();
                    }
                } catch (...) {
                    remove_quietly(tmp);
                    throw;
                }
                publish(tmp, backup, false);
            }
            o.backup = backup;
            if (o.note) o.note->backup = backup;
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

// IND-01: a copy of the project in another folder keeps naming the same
// files: its stored paths (reference sources, recordings) are written
// again for `to`, the folder of the copy `file`.
void rebase_paths(const fs::path& file, const fs::path& from, const fs::path& to) {
    if (from.lexically_normal() == to.lexically_normal()) return;
    sql::Db db;
    db.open(file.string(), SQLITE_OPEN_READWRITE);
    db.exec("BEGIN", "rebase paths");
    for (const char* table : {"sources", "recordings"}) {
        std::vector<std::pair<std::int64_t, std::string>> changed;  // rowid, new path
        {
            sql::Stmt q(db, std::string("SELECT rowid, reference FROM ") + table + " WHERE reference <> ''");
            while (q.step()) {
                const std::string old_path = q.text(1);
                const std::string new_path = stored_path(to, resolve_stored_path(from, old_path));
                if (new_path != old_path) changed.emplace_back(q.i64(0), new_path);
            }
        }
        for (const auto& [rowid, path] : changed) {
            sql::Stmt u(db, std::string("UPDATE ") + table + " SET reference = ?1 WHERE rowid = ?2");
            u.bind(1, path).bind(2, rowid).run();
        }
    }
    db.exec("COMMIT", "rebase paths");
}

}  // namespace

std::string stored_path(const fs::path& folder, const fs::path& file) {
    if (file.empty()) return {};
    std::error_code ec;
    const fs::path f = fs::absolute(file, ec).lexically_normal();
    const fs::path d = fs::absolute(folder, ec).lexically_normal();
    if (f.root_name() == d.root_name()) {
        const fs::path rel = f.lexically_relative(d);
        if (!rel.empty()) return rel.generic_string();
    }
    return f.string();
}

fs::path resolve_stored_path(const fs::path& folder, const std::string& stored) {
    if (stored.empty()) return {};
    const fs::path p(stored);
    if (p.is_absolute()) return p;
    return (folder / p).lexically_normal();
}

// ---- Impl ---------------------------------------------------------------

struct Project::Impl {
    fs::path path;
    sql::Db db;
    ProjectOptions options;
    int opened_version = 0;
    fs::path backup;
    std::optional<MigrationNote> migration_note;
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
        for (building::Floor& f : out) {
            load_trim(f);
            load_scale(f);
        }
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
        load_scale(*f);
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

    // The scale (IND-08) and reference point (IND-09) of a floor (schema 3,
    // floors row).
    void load_scale(building::Floor& f) const {
        sql::Stmt s(db, "SELECT scale_method, scale_ax, scale_ay, scale_bx, scale_by, scale_distance_m, "
                        "scale_metres_per_unit, scale_shared_floor, reference_x, reference_y FROM floors WHERE id = ?1");
        s.bind(1, f.id);
        if (!s.step()) return;
        building::Scale& sc = f.scale;
        sc = building::Scale{};
        sc.method = scale_method_of(s.text(0));
        sc.a = {s.f64(1), s.f64(2)};
        sc.b = {s.f64(3), s.f64(4)};
        sc.distance_m = s.f64(5);
        sc.metres_per_unit = s.f64(6);
        sc.shared_floor = s.text(7);
        f.reference_point.reset();
        if (!s.is_null(8) && !s.is_null(9)) f.reference_point = building::Point2{s.f64(8), s.f64(9)};
    }

    void store_scale(const building::Floor& f) {
        const building::Scale& sc = f.scale;
        sql::Stmt s(db, "UPDATE floors SET scale_method = ?2, scale_ax = ?3, scale_ay = ?4, scale_bx = ?5, "
                        "scale_by = ?6, scale_distance_m = ?7, scale_metres_per_unit = ?8, scale_shared_floor = ?9, "
                        "reference_x = ?10, reference_y = ?11 WHERE id = ?1");
        s.bind(1, f.id).bind(2, scale_method_text(sc.method));
        s.bind(3, sc.a.x).bind(4, sc.a.y).bind(5, sc.b.x).bind(6, sc.b.y);
        s.bind(7, sc.distance_m).bind(8, sc.metres_per_unit).bind(9, sc.shared_floor);
        if (f.reference_point) s.bind(10, f.reference_point->x).bind(11, f.reference_point->y);
        else s.bind_null(10).bind_null(11);
        s.run();
    }

    // The cells.
    std::vector<building::Cell> cells_locked() const {
        std::vector<building::Cell> out;
        sql::Stmt c(db, "SELECT id, label, technology_id, schema_id, carrier_hz FROM cells ORDER BY position, row_id");
        sql::Stmt ids(db, "SELECT name, value FROM cell_identity WHERE cell = ?1");
        while (c.step()) {
            building::Cell cell;
            cell.id = c.text(0);
            cell.label = c.text(1);
            cell.technology_id = c.text(2);
            cell.schema_id = c.text(3);
            cell.carrier_hz = static_cast<std::uint64_t>(std::max<std::int64_t>(0, c.i64(4)));
            ids.reset();
            ids.bind(1, cell.id);
            while (ids.step()) cell.identity[ids.text(0)] = ids.i64(1);
            out.push_back(std::move(cell));
        }
        return out;
    }

    // Writes the cells as the table: cells not in it are removed, the
    // others updated or added.
    void store_cells(const std::vector<building::Cell>& cells) {
        for (const building::Cell& have : cells_locked()) {
            const bool kept =
                std::any_of(cells.begin(), cells.end(), [&](const building::Cell& c) { return c.id == have.id; });
            if (kept) continue;
            sql::Stmt d(db, "DELETE FROM cells WHERE id = ?1");
            d.bind(1, have.id).run();
        }
        sql::Stmt up(db, "INSERT INTO cells (id, position, label, technology_id, schema_id, carrier_hz) VALUES "
                         "(?1, ?2, ?3, ?4, ?5, ?6) ON CONFLICT (id) DO UPDATE SET position = excluded.position, "
                         "label = excluded.label, technology_id = excluded.technology_id, "
                         "schema_id = excluded.schema_id, carrier_hz = excluded.carrier_hz");
        sql::Stmt di(db, "DELETE FROM cell_identity WHERE cell = ?1");
        sql::Stmt ii(db, "INSERT INTO cell_identity (cell, name, value) VALUES (?1, ?2, ?3)");
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const building::Cell& c = cells[i];
            if (c.carrier_hz > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                throw ProjectError(Code::Invalid, "cell " + c.id + ": carrier out of range");
            up.reset();
            up.bind(1, c.id).bind(2, static_cast<std::int64_t>(i)).bind(3, c.label).bind(4, c.technology_id);
            up.bind(5, c.schema_id).bind(6, static_cast<std::int64_t>(c.carrier_hz)).run();
            di.reset();
            di.bind(1, c.id).run();
            for (const auto& [name, value] : c.identity) {
                ii.reset();
                ii.bind(1, c.id).bind(2, name).bind(3, value).run();
            }
        }
    }

    // The cell database (DB-01, schema 5): the cells with their database
    // columns, parameters, neighbour lists and match fields.
    celldb::CellDatabase cell_database_locked() const {
        celldb::CellDatabase out;
        sql::Stmt c(db, "SELECT id, site_name, sector, azimuth_deg, colour, notes, origin, plugin_version FROM cells");
        std::map<std::string, celldb::CellEntry> extra;
        while (c.step()) {
            celldb::CellEntry e;
            e.site_name = c.text(1);
            e.sector = c.text(2);
            e.azimuth_deg = c.opt_f64(3);
            e.colour = c.text(4);
            e.notes = c.text(5);
            e.origin = celldb::origin_from_name(c.text(6)).value_or(celldb::Origin::Manual);
            e.plugin_version = c.text(7);
            extra.emplace(c.text(0), std::move(e));
        }
        auto value_of = [](const sql::Stmt& st, int col) -> celldb::FieldValue {
            switch (st.column_type(col)) {
            case SQLITE_INTEGER: return st.i64(col);
            case SQLITE_FLOAT: return st.f64(col);
            default: return st.text(col);
            }
        };
        sql::Stmt params(db, "SELECT cell, name, value FROM cell_parameters ORDER BY cell, name");
        while (params.step())
            if (auto it = extra.find(params.text(0)); it != extra.end())
                it->second.parameters[params.text(1)] = value_of(params, 2);
        sql::Stmt ns(db, "SELECT cell, idx, name, value, inherited FROM cell_neighbours ORDER BY cell, idx, name");
        std::map<std::string, std::map<std::int64_t, celldb::NeighbourRef>> lists;
        while (ns.step()) {
            celldb::NeighbourRef& r = lists[ns.text(0)][ns.i64(1)];
            r.fields[ns.text(2)] = value_of(ns, 3);
            if (ns.i64(4) != 0) r.inherited.insert(ns.text(2));
        }
        for (auto& [cell, list] : lists)
            if (auto it = extra.find(cell); it != extra.end())
                for (auto& [idx, ref] : list) it->second.neighbours.push_back(std::move(ref));
        std::vector<celldb::CellEntry> entries;
        for (building::Cell& cell : cells_locked()) {
            celldb::CellEntry e;
            if (auto it = extra.find(cell.id); it != extra.end()) e = std::move(it->second);
            e.cell = std::move(cell);
            entries.push_back(std::move(e));
        }
        try {
            out.merge(std::move(entries));
        } catch (const celldb::CellDbError& err) {
            throw ProjectError(Code::Damaged, std::string("cell database: ") + err.what());
        }
        sql::Stmt m(db, "SELECT key, value FROM meta WHERE key LIKE 'celldb_match:%' ORDER BY key");
        while (m.step()) {
            std::vector<std::string> fields;
            const std::string v = m.text(1);
            std::size_t start = 0;
            while (start <= v.size()) {
                const std::size_t comma = v.find(',', start);
                const std::string f = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!f.empty()) fields.push_back(f);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            try {
                out.set_match_fields(m.text(0).substr(std::string_view("celldb_match:").size()), fields);
            } catch (const celldb::CellDbError& err) {
                throw ProjectError(Code::Damaged, std::string("cell database: ") + err.what());
            }
        }
        return out;
    }

    // Writes the database columns of the cells (store_cells wrote the cells).
    void store_cell_database(const celldb::CellDatabase& cdb) {
        sql::Stmt up(db, "UPDATE cells SET site_name = ?2, sector = ?3, azimuth_deg = ?4, colour = ?5, notes = ?6, "
                         "origin = ?7, plugin_version = ?8 WHERE id = ?1");
        sql::Stmt dp(db, "DELETE FROM cell_parameters WHERE cell = ?1");
        sql::Stmt ip(db, "INSERT INTO cell_parameters (cell, name, value) VALUES (?1, ?2, ?3)");
        sql::Stmt dn(db, "DELETE FROM cell_neighbours WHERE cell = ?1");
        sql::Stmt in(db, "INSERT INTO cell_neighbours (cell, idx, name, value, inherited) VALUES (?1, ?2, ?3, ?4, ?5)");
        auto bind_value = [](sql::Stmt& st, int i, const celldb::FieldValue& v) {
            if (const auto* x = std::get_if<std::int64_t>(&v)) st.bind(i, *x);
            else if (const auto* d = std::get_if<double>(&v)) st.bind(i, *d);
            else st.bind(i, std::get<std::string>(v));
        };
        for (const celldb::CellEntry& e : cdb.entries()) {
            up.reset();
            up.bind(1, e.cell.id).bind(2, e.site_name).bind(3, e.sector).bind(4, e.azimuth_deg).bind(5, e.colour);
            up.bind(6, e.notes).bind(7, celldb::origin_name(e.origin)).bind(8, e.plugin_version).run();
            dp.reset();
            dp.bind(1, e.cell.id).run();
            for (const auto& [name, v] : e.parameters) {
                ip.reset();
                ip.bind(1, e.cell.id).bind(2, name);
                bind_value(ip, 3, v);
                ip.run();
            }
            dn.reset();
            dn.bind(1, e.cell.id).run();
            for (std::size_t k = 0; k < e.neighbours.size(); ++k)
                for (const auto& [name, v] : e.neighbours[k].fields) {
                    in.reset();
                    in.bind(1, e.cell.id).bind(2, static_cast<std::int64_t>(k)).bind(3, name);
                    bind_value(in, 4, v);
                    in.bind(5, e.neighbours[k].inherited.count(name) != 0 ? 1 : 0).run();
                }
        }
        db.exec("DELETE FROM meta WHERE key LIKE 'celldb_match:%'", "cell database");
        sql::Stmt mi(db, "INSERT INTO meta (key, value) VALUES (?1, ?2)");
        for (const auto& [tech, fields] : cdb.all_match_fields()) {
            std::string v;
            for (const std::string& f : fields) v += (v.empty() ? "" : ",") + f;
            mi.reset();
            mi.bind(1, "celldb_match:" + tech).bind(2, v).run();
        }
    }

    // Checks the whole changed model `b` and writes the parts named
    // (inside the caller's transaction).
    struct Parts {
        std::vector<std::string> floors;  // scale, reference point
        bool cells = false;
    };
    void write_parts(const building::Building& b, const Parts& parts) {
        require_valid(b);
        std::vector<const building::Floor*> floors;
        for (const std::string& id : parts.floors) {
            const building::Floor* f = b.find_floor(id);
            if (f == nullptr) throw ProjectError(Code::NotFound, "no floor " + id);
            floors.push_back(f);
        }
        if (parts.cells) store_cells(b.cells);
        for (const building::Floor* f : floors) store_scale(*f);
        touch();
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
        sql::Stmt s(db, "SELECT name FROM building WHERE id = 1");
        if (!s.step()) throw ProjectError(Code::Damaged, path.string() + ": no building row");
        b.name = s.text(0);
        b.cells = cells_locked();
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

    // ---- sessions and points (schema 4) ----------------------------------

    // The points' plan-frame metres follow each floor's current scale
    // (IND-20: stored in metres and plan units); NULL without a scale.
    void refresh_point_metres(const building::Building& b) {
        sql::Stmt with(db, "UPDATE points SET x_m = x * ?2, y_m = y * ?2 "
                           "WHERE session IN (SELECT id FROM sessions WHERE floor = ?1)");
        sql::Stmt without(db, "UPDATE points SET x_m = NULL, y_m = NULL "
                              "WHERE session IN (SELECT id FROM sessions WHERE floor = ?1)");
        for (const building::Floor& f : b.floors) {
            if (const auto mpu = b.metres_per_unit(f)) {
                with.reset();
                with.bind(1, f.id).bind(2, *mpu).run();
            } else {
                without.reset();
                without.bind(1, f.id).run();
            }
        }
    }

    // A trim edit keeps the measured points where they were on the sheet
    // (as plan::reanchor_floor does for the scale and reference points).
    void reanchor_points(const std::string& floor_id, const building::Trim& from, const building::Trim& to) {
        if (from == to) return;
        std::vector<std::tuple<std::int64_t, std::int64_t, building::Point2>> moved;
        {
            sql::Stmt q(db, "SELECT session, sequence, x, y FROM points "
                            "WHERE session IN (SELECT id FROM sessions WHERE floor = ?1)");
            q.bind(1, floor_id);
            while (q.step()) {
                const building::Point2 page = plan::trim_floor_to_page(from, {q.f64(2), q.f64(3)});
                moved.emplace_back(q.i64(0), q.i64(1), plan::trim_page_to_floor(to, page));
            }
        }
        sql::Stmt u(db, "UPDATE points SET x = ?3, y = ?4 WHERE session = ?1 AND sequence = ?2");
        for (const auto& [session, sequence, p] : moved) {
            u.reset();
            u.bind(1, session).bind(2, sequence).bind(3, p.x).bind(4, p.y).run();
        }
    }

    bool floor_has_sessions(const std::string& floor_id) const {
        sql::Stmt q(db, "SELECT 1 FROM sessions WHERE floor = ?1 LIMIT 1");
        q.bind(1, floor_id);
        return q.step();
    }

    std::optional<Session> session_locked(std::int64_t id) const;
    std::vector<Session> sessions_locked(const std::string& floor_id) const;
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
        {
            // The statements end before the close: an unfinished one keeps
            // the file open (sqlite3_close_v2), and Windows cannot move an
            // open file (HW-06).
            sql::Transaction tx(db);
            sql::Stmt s(db, "INSERT INTO project (id, name, site, operator, notes, created_utc, modified_utc, "
                            "created_with, modified_with) VALUES (1, ?1, ?2, ?3, ?4, ?5, ?5, ?6, ?6)");
            s.bind(1, info.name).bind(2, info.site).bind(3, info.operator_name).bind(4, info.notes);
            s.bind(5, now).bind(6, impl->tool()).run();
            sql::Stmt b(db, "UPDATE building SET name = ?1 WHERE id = 1");
            b.bind(1, info.site.empty() ? info.name : info.site).run();
            tx.commit();
        }
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
    impl->migration_note = std::move(o.note);
    impl->path = path;
    return std::unique_ptr<Project>(new Project(std::move(impl)));
}

const fs::path& Project::path() const { return impl_->path; }

std::string Project::stored_path(const fs::path& file) const {
    std::error_code ec;
    return project::stored_path(fs::absolute(impl_->path, ec).parent_path(), file);
}

fs::path Project::resolve_path(const std::string& stored) const {
    std::error_code ec;
    return resolve_stored_path(fs::absolute(impl_->path, ec).parent_path(), stored);
}
int Project::opened_version() const { return impl_->opened_version; }
const fs::path& Project::migration_backup() const { return impl_->backup; }
const std::optional<MigrationNote>& Project::migration_note() const { return impl_->migration_note; }

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
                                   ", created_utc) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)");
        s.bind(1, floor.id);
        bind_floor_fields(s, floor);
        s.bind(10, impl_->now()).run();
        impl_->store_trim(floor);
        impl_->store_scale(floor);
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::FloorAdded, floor.id});
    return floor.id;
}

void Project::update_floor(const building::Floor& floor) {
    require_floor_values(floor);
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
        const building::Trim old_trim = old->trim;
        *old = floor;
        require_valid(b);
        impl_->require_page(floor);
        sql::Stmt s(impl_->db, "UPDATE floors SET name = ?2, level = ?3, height_m = ?4, source = ?5, page = ?6, "
                               "example_id = ?7, plan_width = ?8, plan_height = ?9 "
                               "WHERE id = ?1");
        s.bind(1, floor.id);
        bind_floor_fields(s, floor);
        s.run();
        impl_->store_trim(floor);
        impl_->store_scale(floor);
        impl_->reanchor_points(floor.id, old_trim, floor.trim);
        impl_->refresh_point_metres(b);
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

void Project::remove_floor(const std::string& id) { remove_floor(id, false); }

void Project::remove_floor(const std::string& id, bool with_sessions) {
    bool sessions_removed = false;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        if (impl_->floor_has_sessions(id)) {
            if (!with_sessions)
                throw ProjectError(Code::InUse, "floor " + id + " has walk tests or measured points; remove them first");
            // Points go with their sessions (ON DELETE CASCADE); a later
            // session that continues one of them loses the link only.
            sql::Stmt del(impl_->db, "DELETE FROM sessions WHERE floor = ?1");
            del.bind(1, id).run();
            sessions_removed = true;
        }
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
    if (sessions_removed) notify({Change::Kind::Sessions, {}});
    notify({Change::Kind::FloorRemoved, id});
}

// ---- building model -------------------------------------------------------

building::Building Project::building() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->building_locked();
}

void Project::replace_building(const building::Building& b) {
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
        sql::Stmt bs(impl_->db, "UPDATE building SET name = ?1 WHERE id = 1");
        bs.bind(1, b.name).run();
        impl_->store_cells(b.cells);
        // Floors not in b go; the others are updated (keeping their
        // creation time) or added.
        for (const building::Floor& f : impl_->floors_locked()) {
            if (b.find_floor(f.id) == nullptr) {
                if (impl_->floor_has_sessions(f.id)) {
                    throw ProjectError(Code::InUse, "floor '" + f.name + "' has walk tests or measured points");
                }
                sql::Stmt d(impl_->db, "DELETE FROM floors WHERE id = ?1");
                d.bind(1, f.id).run();
            }
        }
        impl_->park_levels();
        sql::Stmt up(impl_->db, std::string("INSERT INTO floors (") + kFloorColumns +
                                    ", created_utc) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10) "
                                    "ON CONFLICT (id) DO UPDATE SET name = excluded.name, level = excluded.level, "
                                    "height_m = excluded.height_m, source = excluded.source, page = excluded.page, "
                                    "example_id = excluded.example_id, plan_width = excluded.plan_width, "
                                    "plan_height = excluded.plan_height");
        const std::string now = impl_->now();
        for (const building::Floor& f : b.floors) {
            up.reset();
            up.bind(1, f.id);
            bind_floor_fields(up, f);
            up.bind(10, now).run();
            impl_->require_page(f);
            impl_->store_trim(f);
            impl_->store_scale(f);
            impl_->bump_counter("next_floor_number", id_number(f.id, "floor-") + 1);
        }
        for (const building::Cell& c : b.cells) impl_->bump_counter("next_cell_number", id_number(c.id, "cell-") + 1);
        impl_->refresh_point_metres(b);
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Building, ""});
}

// ---- scale and alignment (IND-08, IND-09) -----------------------------------

namespace {

building::Floor& floor_in(building::Building& b, const std::string& id) {
    building::Floor* f = b.find_floor(id);
    if (f == nullptr) throw ProjectError(Code::NotFound, "no floor " + id);
    return *f;
}

// A Shared scale only between floors of one sheet (IND-08), never from a
// floor without its own scale (the model refuses chains).
void require_shareable(const building::Building& b, const building::Floor& f) {
    if (f.scale.method != building::Scale::Method::Shared) return;
    const building::Floor* from = b.find_floor(f.scale.shared_floor);
    if (from == nullptr) throw ProjectError(Code::NotFound, "no floor " + f.scale.shared_floor);
    if (!building::same_sheet(*from, f)) {
        throw ProjectError(Code::Invalid, "floor '" + f.name + "' is not cut from the sheet of floor '" + from->name +
                                              "': only floors of one sheet share a scale");
    }
}

}  // namespace

void Project::set_scale(const std::string& floor_id, const building::Scale& scale) {
    std::vector<std::string> followers;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        building::Floor& f = floor_in(b, floor_id);
        f.scale = scale;
        require_shareable(b, f);
        followers = building::floors_sharing_scale(b, floor_id);
        if (!followers.empty() && !building::own_metres_per_unit(scale)) {
            throw ProjectError(Code::Invalid, "floor '" + f.name + "' gives its scale to " +
                                                  std::to_string(followers.size()) +
                                                  " other floor(s): it needs a scale of its own");
        }
        impl_->write_parts(b, {{floor_id}, false});
        impl_->refresh_point_metres(b);
        tx.commit();
    }
    notify({Change::Kind::FloorChanged, floor_id});
    for (const std::string& id : followers) notify({Change::Kind::FloorChanged, id});
}

void Project::share_scale(const std::string& from_id, const std::vector<std::string>& floor_ids) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        floor_in(b, from_id);
        for (const std::string& id : floor_ids) {
            building::Floor& f = floor_in(b, id);
            if (id == from_id) throw ProjectError(Code::Invalid, "a floor cannot share its own scale");
            if (!building::floors_sharing_scale(b, id).empty()) {
                throw ProjectError(Code::Invalid, "floor '" + f.name + "' gives its scale to other floors");
            }
            f.scale = building::shared_scale(from_id);
            require_shareable(b, f);
        }
        impl_->write_parts(b, {floor_ids, false});
        impl_->refresh_point_metres(b);
        tx.commit();
    }
    for (const std::string& id : floor_ids) notify({Change::Kind::FloorChanged, id});
}

void Project::set_reference_point(const std::string& floor_id, std::optional<building::Point2> point) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        floor_in(b, floor_id).reference_point = point;
        impl_->write_parts(b, {{floor_id}, false});
        tx.commit();
    }
    notify({Change::Kind::FloorChanged, floor_id});
}

// ---- cells ----------------------------------------------------------------------

std::vector<building::Cell> Project::cells() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->cells_locked();
}

std::string Project::add_cell(building::Cell cell) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        if (cell.id.empty()) {
            cell.id = impl_->next_id("next_cell_number", "cell-", "cells");
        } else {
            if (b.find_cell(cell.id) != nullptr) throw ProjectError(Code::Invalid, "cell id " + cell.id + " is used");
            impl_->bump_counter("next_cell_number", id_number(cell.id, "cell-") + 1);
        }
        b.cells.push_back(cell);
        impl_->write_parts(b, {{}, true});
        tx.commit();
    }
    notify({Change::Kind::Cells, cell.id});
    return cell.id;
}

void Project::update_cell(const building::Cell& cell) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        auto it = std::find_if(b.cells.begin(), b.cells.end(), [&](const building::Cell& c) { return c.id == cell.id; });
        if (it == b.cells.end()) throw ProjectError(Code::NotFound, "no cell " + cell.id);
        *it = cell;
        impl_->write_parts(b, {{}, true});
        tx.commit();
    }
    notify({Change::Kind::Cells, cell.id});
}

void Project::remove_cell(const std::string& id) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        if (b.find_cell(id) == nullptr) throw ProjectError(Code::NotFound, "no cell " + id);
        b.cells.erase(std::remove_if(b.cells.begin(), b.cells.end(), [&](const building::Cell& c) { return c.id == id; }),
                      b.cells.end());
        impl_->write_parts(b, {{}, true});
        tx.commit();
    }
    notify({Change::Kind::Cells, id});
}

celldb::CellDatabase Project::cell_database() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->cell_database_locked();
}

void Project::set_cell_database(const celldb::CellDatabase& cdb) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        for (const celldb::CellEntry& e : cdb.entries())
            if (auto err = e.validate(); !err.empty()) throw ProjectError(Code::Invalid, "cell " + e.cell.id + ": " + err);
        sql::Transaction tx(impl_->db);
        building::Building b = impl_->building_locked();
        b.cells.clear();
        for (const celldb::CellEntry& e : cdb.entries()) b.cells.push_back(e.cell);
        impl_->write_parts(b, {{}, true});
        impl_->store_cell_database(cdb);
        for (const building::Cell& c : b.cells) impl_->bump_counter("next_cell_number", id_number(c.id, "cell-") + 1);
        tx.commit();
    }
    notify({Change::Kind::Cells, ""});
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
        s.bind(6, stored_path(cached_file)).bind(7, impl_->now()).run();
        r.added = true;
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::SourceAdded, r.id});
    return r;
}

AddSourceResult Project::add_source_reference(const SourceReference& described) {
    const auto hex = [](const std::string& h) {
        return h.size() == 64 &&
               std::all_of(h.begin(), h.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    };
    if (described.file_name.empty() || described.file_name.find_first_of("/\\") != std::string::npos)
        throw ProjectError(Code::Invalid, "a source reference needs a file name without a folder");
    if (described.size == 0 || described.size > kMaxSourceBytes)
        throw ProjectError(Code::Invalid, "source reference " + described.file_name + ": size out of range");
    if (!hex(described.sha256))
        throw ProjectError(Code::Invalid, "source reference " + described.file_name + ": SHA-256 is not 64 lower-case hex digits");
    AddSourceResult r;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        if (auto id = impl_->source_by_hash(described.sha256)) {
            return {std::move(*id), false};
        }
        sql::Transaction tx(impl_->db);
        r.id = impl_->next_id("next_source_number", "source-", "sources");
        sql::Stmt s(impl_->db, "INSERT INTO sources (id, file_name, media_type, size, sha256, storage, reference, "
                               "added_utc) VALUES (?1, ?2, ?3, ?4, ?5, 'reference', ?6, ?7)");
        s.bind(1, r.id).bind(2, described.file_name)
            .bind(3, described.media_type.empty() ? std::string("application/octet-stream") : described.media_type);
        s.bind(4, static_cast<std::int64_t>(described.size)).bind(5, described.sha256);
        // IND-01: an absolute path hint is stored relative when possible.
        const bool absolute = fs::path(described.reference).is_absolute();
        s.bind(6, absolute ? stored_path(described.reference) : described.reference).bind(7, impl_->now()).run();
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
    if (!src->plan.embedded) data = read_whole(resolve_path(src->reference), *src);
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

// ---- interpolation settings and results (schema 8) ----------------------------
// Copied from yantra-v2 68ea20a: core/src/project/project.cpp (interpolation
// results), adapted (IDW settings, 8-byte cells).

namespace {

void check_range(std::vector<std::string>& out, const char* name, double v, double lo, double hi) {
    if (!std::isfinite(v) || v < lo || v > hi) {
        char b[160];
        std::snprintf(b, sizeof b, "%s: %g is outside %g .. %g", name, v, lo, hi);
        out.emplace_back(b);
    }
}

bool is_hex64(const std::string& s) {
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool is_json_object(const std::string& text) {
    const auto j = nlohmann::json::parse(text, nullptr, false);
    return !j.is_discarded() && j.is_object();
}

constexpr const char* kInterpolationColumns =
    "id, floor_id, key, created_utc, created_with, building_sha256, points_sha256, settings_sha256, parameters, "
    "result, origin_x, origin_y, cell_m, cols, rows";

StoredInterpolation interpolation_row(const sql::Stmt& s, bool with_cells) {
    StoredInterpolation r;
    r.id = s.i64(0);
    r.floor_id = s.text(1);
    r.key = s.text(2);
    r.created_utc = s.text(3);
    r.created_with = s.text(4);
    r.building_sha256 = s.text(5);
    r.points_sha256 = s.text(6);
    r.settings_sha256 = s.text(7);
    r.parameters_json = s.text(8);
    r.result_json = s.text(9);
    r.origin_m = {s.f64(10), s.f64(11)};
    r.cell_m = s.f64(12);
    r.cols = static_cast<int>(s.i64(13));
    r.rows = static_cast<int>(s.i64(14));
    if (with_cells) r.cells = s.blob(15);
    return r;
}

}  // namespace

std::vector<std::string> InterpolationSettings::validate() const {
    std::vector<std::string> out;
    check_range(out, "grid_m", grid_m, 0.05, 10.0);
    check_range(out, "power", power, 0.5, 6.0);
    check_range(out, "radius_m", radius_m, 0.5, 1000.0);
    check_range(out, "full_confidence_m", full_confidence_m, 0.0, 1000.0);
    if (std::isfinite(full_confidence_m) && std::isfinite(radius_m) && full_confidence_m >= radius_m)
        out.emplace_back("full_confidence_m: must be below radius_m");
    return out;
}

InterpolationSettings Project::interpolation_settings() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    sql::Stmt s(impl_->db, "SELECT grid_m, power, radius_m, full_confidence_m FROM interpolation_settings WHERE id = 1");
    InterpolationSettings r;
    if (!s.step()) return r;
    r.grid_m = s.f64(0);
    r.power = s.f64(1);
    r.radius_m = s.f64(2);
    r.full_confidence_m = s.f64(3);
    return r;
}

void Project::set_interpolation_settings(const InterpolationSettings& v) {
    const auto problems = v.validate();
    if (!problems.empty()) throw ProjectError(Code::Invalid, "interpolation settings: " + problems.front());
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt s(impl_->db,
                    "INSERT INTO interpolation_settings (id, grid_m, power, radius_m, full_confidence_m) "
                    "VALUES (1, ?1, ?2, ?3, ?4) ON CONFLICT (id) DO UPDATE SET grid_m = excluded.grid_m, "
                    "power = excluded.power, radius_m = excluded.radius_m, "
                    "full_confidence_m = excluded.full_confidence_m");
        s.bind(1, v.grid_m).bind(2, v.power).bind(3, v.radius_m).bind(4, v.full_confidence_m).run();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::InterpolationSettings, ""});
}

// ---- acceptance profiles (schema 7) -------------------------------------------

std::vector<StoredAcceptanceProfile> Project::acceptance_profiles() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    sql::Stmt s(impl_->db, "SELECT name, definition FROM acceptance_profiles ORDER BY position");
    std::vector<StoredAcceptanceProfile> out;
    while (s.step()) out.push_back(StoredAcceptanceProfile{s.text(0), s.text(1)});
    return out;
}

void Project::set_acceptance_profiles(const std::vector<StoredAcceptanceProfile>& profiles) {
    std::set<std::string> names;
    for (const StoredAcceptanceProfile& p : profiles) {
        if (p.name.empty()) throw ProjectError(Code::Invalid, "acceptance profile: empty name");
        if (!names.insert(p.name).second)
            throw ProjectError(Code::Invalid, "acceptance profile '" + p.name + "' is there twice");
        if (!is_json_object(p.definition))
            throw ProjectError(Code::Invalid, "acceptance profile '" + p.name + "': the definition is not a JSON object");
    }
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        impl_->db.exec("DELETE FROM acceptance_profiles", "acceptance profiles");
        sql::Stmt s(impl_->db, "INSERT INTO acceptance_profiles (position, name, definition) VALUES (?1, ?2, ?3)");
        std::int64_t pos = 0;
        for (const StoredAcceptanceProfile& p : profiles) {
            s.reset();
            s.bind(1, pos++).bind(2, p.name).bind(3, p.definition).run();
        }
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Acceptance, ""});
}

std::int64_t Project::store_interpolation(StoredInterpolation r) {
    if (r.key.empty()) throw ProjectError(Code::Invalid, "interpolation: empty key");
    for (const std::string* h : {&r.building_sha256, &r.points_sha256, &r.settings_sha256})
        if (!is_hex64(*h)) throw ProjectError(Code::Invalid, "interpolation: a fingerprint is not 64 hex digits");
    if (!is_json_object(r.parameters_json) || !is_json_object(r.result_json))
        throw ProjectError(Code::Invalid, "interpolation: parameters and result must be JSON objects");
    if (!std::isfinite(r.origin_m.x) || !std::isfinite(r.origin_m.y) || !std::isfinite(r.cell_m) || r.cell_m <= 0.0 ||
        r.cols <= 0 || r.rows <= 0)
        throw ProjectError(Code::Invalid, "interpolation: the grid is not valid");
    if (r.cells.size() != static_cast<std::uint64_t>(r.cols) * static_cast<std::uint64_t>(r.rows) * 8u)
        throw ProjectError(Code::Invalid, "interpolation: the cells do not match the grid");
    if (r.cells.size() > kMaxSourceBytes) throw ProjectError(Code::Invalid, "interpolation: the grid is too large");
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        if (!impl_->db.text_value("SELECT id FROM floors WHERE id = " + sql::quote(r.floor_id), "floor"))
            throw ProjectError(Code::NotFound, "no floor " + r.floor_id);
        sql::Stmt del(impl_->db, "DELETE FROM interpolations WHERE floor_id = ?1 AND key = ?2");
        del.bind(1, r.floor_id).bind(2, r.key).run();
        sql::Stmt s(impl_->db,
                    "INSERT INTO interpolations (floor_id, key, created_utc, created_with, building_sha256, "
                    "points_sha256, settings_sha256, parameters, result, origin_x, origin_y, cell_m, cols, rows, "
                    "cells) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15)");
        s.bind(1, r.floor_id).bind(2, r.key).bind(3, impl_->now()).bind(4, impl_->tool());
        s.bind(5, r.building_sha256).bind(6, r.points_sha256).bind(7, r.settings_sha256);
        s.bind(8, r.parameters_json).bind(9, r.result_json).bind(10, r.origin_m.x).bind(11, r.origin_m.y);
        s.bind(12, r.cell_m).bind(13, r.cols).bind(14, r.rows).bind_blob(15, r.cells).run();
        r.id = impl_->db.last_rowid();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Interpolations, r.floor_id});
    return r.id;
}

std::vector<StoredInterpolation> Project::interpolations(const std::string& floor_id, bool with_cells) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    sql::Stmt s(impl_->db, std::string("SELECT ") + kInterpolationColumns + (with_cells ? ", cells" : "") +
                               " FROM interpolations WHERE floor_id = ?1 ORDER BY key");
    s.bind(1, floor_id);
    std::vector<StoredInterpolation> out;
    while (s.step()) out.push_back(interpolation_row(s, with_cells));
    return out;
}

std::optional<StoredInterpolation> Project::interpolation(std::int64_t id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    sql::Stmt s(impl_->db,
                std::string("SELECT ") + kInterpolationColumns + ", cells FROM interpolations WHERE id = ?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return interpolation_row(s, true);
}

void Project::remove_interpolation(std::int64_t id) {
    std::string floor_id;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt q(impl_->db, "SELECT floor_id FROM interpolations WHERE id = ?1");
        q.bind(1, id);
        if (!q.step()) throw ProjectError(Code::NotFound, "no interpolation " + std::to_string(id));
        floor_id = q.text(0);
        sql::Stmt s(impl_->db, "DELETE FROM interpolations WHERE id = ?1");
        s.bind(1, id).run();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Interpolations, floor_id});
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
            {
                sql::Stmt s(impl_->db, "VACUUM INTO ?1");  // finalized before the move (Windows)
                s.bind(1, tmp.string()).run();
            }
            rebase_paths(tmp, fs::absolute(impl_->path, ec).parent_path(), fs::absolute(path, ec).parent_path());
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

void Project::write_copy(const fs::path& path, bool replace) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    std::error_code ec;
    const bool exists = fs::exists(path, ec);
    if (exists && fs::equivalent(path, impl_->path, ec)) throw ProjectError(Code::Invalid, path.string() + " is the open project");
    if (exists && !replace) throw ProjectError(Code::Exists, path.string() + ": file exists");
    if (exists && locked_elsewhere(path)) throw ProjectError(Code::Locked, locked_message(path));
    const fs::path tmp = temp_name(path);
    try {
        {
            sql::Stmt s(impl_->db, "VACUUM INTO ?1");  // finalized before the move (Windows)
            s.bind(1, tmp.string()).run();
        }
        rebase_paths(tmp, fs::absolute(impl_->path, ec).parent_path(), fs::absolute(path, ec).parent_path());
        if (exists) remove_side_files(path);
        publish(tmp, path, replace);
    } catch (...) {
        remove_quietly(tmp);
        throw;
    }
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

// ---- recordings (REC-02, REC-03; schema 4) ----------------------------------

namespace {

constexpr const char* kRecordingColumns =
    "id, file_name, session_id, session_name, file_index, sha256, size, reference, header, added_utc";

RecordingFile recording_from(const sql::Stmt& s) {
    RecordingFile r;
    r.id = s.i64(0);
    r.file_name = s.text(1);
    r.session_id = s.text(2);
    r.session_name = s.text(3);
    r.file_index = static_cast<std::uint32_t>(s.i64(4));
    r.sha256 = s.text(5);
    r.size = static_cast<std::uint64_t>(std::max<std::int64_t>(0, s.i64(6)));
    r.reference = s.text(7);
    r.header = s.text(8);
    r.added_utc = s.text(9);
    return r;
}

constexpr const char* kSessionColumns =
    "id, floor, kind, name, started_utc, ended_utc, radio, calibration, notes, walk_mode, recording_session, "
    "plugins, continues";

Session session_from(const sql::Stmt& s) {
    Session x;
    x.id = s.i64(0);
    x.floor_id = s.text(1);
    x.kind = s.text(2);
    x.name = s.text(3);
    x.started_utc = s.text(4);
    x.ended_utc = s.text(5);
    x.radio = s.text(6);
    x.calibration = s.text(7);
    x.notes = s.text(8);
    x.walk_mode = s.text(9);
    x.recording_session = s.text(10);
    x.plugins = s.text(11);
    x.continues = s.opt_i64(12);
    return x;
}

void require_session_values(const Session& s) {
    if (s.kind != "walk" && s.kind != "manual" && s.kind != "simulated") {
        throw ProjectError(Code::Invalid, "session kind '" + s.kind + "' is not walk, manual or simulated");
    }
    if (s.walk_mode != "point_and_click" && s.walk_mode != "continuous") {
        throw ProjectError(Code::Invalid, "walk mode '" + s.walk_mode + "' is not point_and_click or continuous");
    }
}

constexpr const char* kPointColumns =
    "session, sequence, mono_ns, utc_ns, x, y, x_m, y_m, kind, grp, cycles, label, sample";

StoredPoint point_from(const sql::Stmt& s) {
    StoredPoint p;
    p.session_id = s.i64(0);
    p.sequence = s.i64(1);
    p.time_ns = s.i64(2);
    p.utc_ns = s.i64(3);
    p.plan = {s.f64(4), s.f64(5)};
    if (!s.is_null(6) && !s.is_null(7)) p.metres = building::Point2{s.f64(6), s.f64(7)};
    p.kind = s.text(8);
    p.group = static_cast<std::uint32_t>(s.i64(9));
    p.cycles = static_cast<std::uint32_t>(s.i64(10));
    p.label = s.text(11);
    p.sample = s.text(12);
    return p;
}

}  // namespace

std::int64_t Project::add_recording(RecordingFile file) {
    if (file.file_name.empty()) throw ProjectError(Code::Invalid, "a recording needs a file name");
    if (!is_hex64(file.sha256)) throw ProjectError(Code::Invalid, "recording SHA-256 is not 64 hex digits");
    std::int64_t id = 0;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        {
            sql::Stmt q(impl_->db, "SELECT 1 FROM recordings WHERE file_name = ?1");
            q.bind(1, file.file_name);
            if (q.step()) throw ProjectError(Code::Exists, "recording " + file.file_name + " is already listed");
        }
        sql::Stmt s(impl_->db, "INSERT INTO recordings (file_name, session_id, session_name, file_index, sha256, "
                               "size, reference, header, added_utc) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
        s.bind(1, file.file_name).bind(2, file.session_id).bind(3, file.session_name);
        s.bind(4, static_cast<std::int64_t>(file.file_index)).bind(5, file.sha256);
        // IND-01: an absolute path is stored relative when possible.
        if (fs::path(file.reference).is_absolute()) file.reference = stored_path(file.reference);
        s.bind(6, static_cast<std::int64_t>(file.size)).bind(7, file.reference).bind(8, file.header);
        s.bind(9, impl_->now()).run();
        id = impl_->db.last_rowid();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Recordings, std::to_string(id)});
    return id;
}

std::vector<RecordingFile> Project::recordings() const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    std::vector<RecordingFile> out;
    sql::Stmt s(impl_->db, std::string("SELECT ") + kRecordingColumns + " FROM recordings ORDER BY id");
    while (s.step()) out.push_back(recording_from(s));
    return out;
}

void Project::remove_recording(std::int64_t id) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt s(impl_->db, "DELETE FROM recordings WHERE id = ?1");
        s.bind(1, id).run();
        if (impl_->db.changes() == 0) throw ProjectError(Code::NotFound, "no recording " + std::to_string(id));
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Recordings, std::to_string(id)});
}

// ---- sessions and points (IND-20, IND-30; schema 4) ---------------------------

std::optional<Session> Project::Impl::session_locked(std::int64_t id) const {
    sql::Stmt s(db, std::string("SELECT ") + kSessionColumns + " FROM sessions WHERE id = ?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return session_from(s);
}

std::vector<Session> Project::Impl::sessions_locked(const std::string& floor_id) const {
    std::vector<Session> out;
    sql::Stmt s(db, std::string("SELECT ") + kSessionColumns + " FROM sessions WHERE ?1 = '' OR floor = ?1 ORDER BY id");
    s.bind(1, floor_id);
    while (s.step()) out.push_back(session_from(s));
    return out;
}

std::int64_t Project::add_session(Session session) {
    require_session_values(session);
    std::int64_t id = 0;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        if (!impl_->floor_locked(session.floor_id)) throw ProjectError(Code::NotFound, "no floor " + session.floor_id);
        if (session.continues && !impl_->session_locked(*session.continues)) {
            throw ProjectError(Code::NotFound, "no session " + std::to_string(*session.continues));
        }
        if (session.started_utc.empty()) session.started_utc = impl_->now();
        sql::Stmt s(impl_->db, "INSERT INTO sessions (floor, kind, name, started_utc, ended_utc, radio, calibration, "
                               "notes, walk_mode, recording_session, plugins, continues) "
                               "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)");
        s.bind(1, session.floor_id).bind(2, session.kind).bind(3, session.name).bind(4, session.started_utc);
        s.bind(5, session.ended_utc).bind(6, session.radio).bind(7, session.calibration).bind(8, session.notes);
        s.bind(9, session.walk_mode).bind(10, session.recording_session).bind(11, session.plugins);
        s.bind(12, session.continues).run();
        id = impl_->db.last_rowid();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Sessions, std::to_string(id)});
    return id;
}

void Project::update_session(const Session& session) {
    require_session_values(session);
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        const auto old = impl_->session_locked(session.id);
        if (!old) throw ProjectError(Code::NotFound, "no session " + std::to_string(session.id));
        if (old->floor_id != session.floor_id || old->kind != session.kind) {
            throw ProjectError(Code::Invalid, "a session keeps its floor and kind");
        }
        sql::Stmt s(impl_->db, "UPDATE sessions SET name = ?2, ended_utc = ?3, radio = ?4, calibration = ?5, "
                               "notes = ?6, walk_mode = ?7, recording_session = ?8, plugins = ?9 WHERE id = ?1");
        s.bind(1, session.id).bind(2, session.name).bind(3, session.ended_utc).bind(4, session.radio);
        s.bind(5, session.calibration).bind(6, session.notes).bind(7, session.walk_mode);
        s.bind(8, session.recording_session).bind(9, session.plugins).run();
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Sessions, std::to_string(session.id)});
}

std::optional<Session> Project::session(std::int64_t id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->session_locked(id);
}

std::vector<Session> Project::sessions(const std::string& floor_id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    return impl_->sessions_locked(floor_id);
}

void Project::remove_session(std::int64_t id) {
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        sql::Stmt s(impl_->db, "DELETE FROM sessions WHERE id = ?1");
        s.bind(1, id).run();
        if (impl_->db.changes() == 0) throw ProjectError(Code::NotFound, "no session " + std::to_string(id));
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Sessions, std::to_string(id)});
}

void Project::add_points(std::int64_t session_id, std::vector<StoredPoint> points) {
    if (points.empty()) return;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        const auto session = impl_->session_locked(session_id);
        if (!session) throw ProjectError(Code::NotFound, "no session " + std::to_string(session_id));
        const building::Building b = impl_->building_locked();
        const building::Floor* f = b.find_floor(session->floor_id);
        const std::optional<double> mpu = f ? b.metres_per_unit(*f) : std::nullopt;
        std::int64_t last = 0;
        {
            sql::Stmt q(impl_->db, "SELECT max(sequence) FROM points WHERE session = ?1");
            q.bind(1, session_id);
            if (q.step() && !q.is_null(0)) last = q.i64(0);
        }
        sql::Stmt s(impl_->db, std::string("INSERT INTO points (") + kPointColumns +
                                   ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)");
        for (StoredPoint& p : points) {
            if (!std::isfinite(p.plan.x) || !std::isfinite(p.plan.y)) {
                throw ProjectError(Code::Invalid, "a point needs a finite plan position");
            }
            if (p.sequence <= last) {
                throw ProjectError(Code::Invalid, "point sequence " + std::to_string(p.sequence) +
                                                      " is not above the session's last (" + std::to_string(last) +
                                                      ")");
            }
            if (p.kind != "walk" && p.kind != "manual" && p.kind != "simulated") {
                throw ProjectError(Code::Invalid, "point kind '" + p.kind + "' is not walk, manual or simulated");
            }
            if (p.cycles < 1) throw ProjectError(Code::Invalid, "a point is made of at least one cycle");
            last = p.sequence;
            s.reset();
            s.bind(1, session_id).bind(2, p.sequence).bind(3, p.time_ns).bind(4, p.utc_ns);
            s.bind(5, p.plan.x).bind(6, p.plan.y);
            if (mpu) s.bind(7, p.plan.x * *mpu).bind(8, p.plan.y * *mpu);
            else s.bind_null(7).bind_null(8);
            s.bind(9, p.kind).bind(10, static_cast<std::int64_t>(p.group)).bind(11, static_cast<std::int64_t>(p.cycles));
            s.bind(12, p.label).bind(13, p.sample).run();
        }
        impl_->touch();
        tx.commit();
    }
    notify({Change::Kind::Points, std::to_string(session_id)});
}

std::size_t Project::remove_points(std::int64_t session_id, std::uint32_t group) {
    std::size_t n = 0;
    {
        const std::lock_guard l(impl_->mutex);
        impl_->require_open();
        sql::Transaction tx(impl_->db);
        if (!impl_->session_locked(session_id)) throw ProjectError(Code::NotFound, "no session " + std::to_string(session_id));
        sql::Stmt s(impl_->db, "DELETE FROM points WHERE session = ?1 AND grp = ?2");
        s.bind(1, session_id).bind(2, static_cast<std::int64_t>(group)).run();
        n = static_cast<std::size_t>(impl_->db.changes());
        impl_->touch();
        tx.commit();
    }
    if (n > 0) notify({Change::Kind::Points, std::to_string(session_id)});
    return n;
}

std::vector<StoredPoint> Project::points(std::int64_t session_id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    std::vector<StoredPoint> out;
    sql::Stmt s(impl_->db, std::string("SELECT ") + kPointColumns + " FROM points WHERE session = ?1 ORDER BY sequence");
    s.bind(1, session_id);
    while (s.step()) out.push_back(point_from(s));
    return out;
}

std::optional<StoredPoint> Project::point(std::int64_t session_id, std::int64_t sequence) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    sql::Stmt s(impl_->db, std::string("SELECT ") + kPointColumns + " FROM points WHERE session = ?1 AND sequence = ?2");
    s.bind(1, session_id).bind(2, sequence);
    if (!s.step()) return std::nullopt;
    return point_from(s);
}

std::size_t Project::point_count(const std::string& floor_id) const {
    const std::lock_guard l(impl_->mutex);
    impl_->require_open();
    sql::Stmt s(impl_->db, "SELECT count(*) FROM points WHERE session IN (SELECT id FROM sessions WHERE floor = ?1)");
    s.bind(1, floor_id);
    return s.step() ? static_cast<std::size_t>(s.i64(0)) : 0;
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
