// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Indoor project file (IND-01, IND-03, IND-04): one *.yantra file, an
// SQLite database in the documented format of docs/formats/PROJECT.md
// (NFR-05, DOC-12). It holds one building (core/building/model.h): the
// project metadata, the floors ordered by level and the plan source files
// the floors are cut from (a copy of each, or for licence-restricted
// sources a reference by SHA-256).
//
// Schema version 1 (step 7.1) stores the building model's project-level
// and floor-level fields and the sources. Trims (IND-05..07), scales and
// alignment (IND-08/09), wall classes, walls, cells and antennas
// (IND-11/12), recordings, measured points and their provenance (IND-20,
// PLG-12) and interpolation results come with later schema versions (the
// hooks are listed in docs/formats/PROJECT.md). Until then, a call that
// would store such data is refused with Code::Unsupported: no part of a
// model is ever dropped silently.
//
// Storage rules (data safety):
// - Every change is written in its own transaction before the call
//   returns (SQLite write-ahead log, synchronous FULL): a crash or a power
//   loss loses at most the change in progress, never the file or an
//   earlier change. save() folds the log into the file, so a saved or
//   closed project is one file that can be copied to another computer.
// - One Yantra at a time: the file stays locked (SQLite exclusive locking
//   mode, an OS lock that goes with the process) while a Project has it
//   open; a second open, in this or another process, gets Code::Locked.
// - create() and save_as() write a complete file under a temporary name in
//   the target folder, flush it and rename it into place, so the target
//   name never holds half a file.
// - The schema has a version (PRAGMA user_version). open() migrates an
//   older file step by step, each step in one transaction, after writing a
//   backup copy "<file>.schema<N>.bak" that the older Yantra can still
//   open; a file of a newer schema is refused with Code::TooNew and is not
//   touched.
// - open() checks the file (application id, format mark, SQLite quick
//   check); a damaged or cut file is refused with a reason, never crashes.
//
// Threading: a Project may be used from any thread; calls are serialised
// by an internal lock. Change notifications run on the calling thread,
// after the change is committed and without the lock held.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/building/model.h"

namespace yantra::project {

/// The file name extension of a project (IND-01).
inline constexpr const char* kExtension = ".yantra";
/// SQLite application_id of a project file ("YNTR").
inline constexpr std::int32_t kApplicationId = 0x594E5452;
/// The value of the format mark in the meta table.
inline constexpr const char* kFormat = "yantra-project";
/// Schema version written by this build (docs/formats/PROJECT.md).
inline constexpr int kSchemaVersion = 1;

class ProjectError : public std::runtime_error {
public:
    enum class Code {
        NotAProject,  ///< not an SQLite file, or not a Yantra project
        TooNew,       ///< made by a newer Yantra (higher schema version)
        Locked,       ///< open in another Yantra (or another program)
        Exists,       ///< create/save_as: the target exists
        Damaged,      ///< the file is damaged (corrupt database, wrong hash)
        Io,           ///< reading or writing a file failed
        NotFound,     ///< no floor or source with that id
        InUse,        ///< a source still used by a floor
        Invalid,      ///< an argument is not valid (e.g. an empty name)
        Unsupported,  ///< data this schema version cannot store yet
        Closed,       ///< the project was closed
    };
    ProjectError(Code code, const std::string& message) : std::runtime_error(message), code_(code) {}
    Code code() const noexcept { return code_; }

private:
    Code code_;
};

/// What File > New indoor project asks for (IND-01).
struct ProjectInfo {
    std::string name;  ///< required
    std::string site;
    std::string operator_name;  ///< the network operator
    std::string notes;
    bool operator==(const ProjectInfo&) const = default;
};

/// Metadata kept by the file itself.
struct ProjectMeta {
    ProjectInfo info;
    std::string created_utc;    ///< ISO 8601, e.g. "2026-11-05T09:30:00Z"
    std::string modified_utc;   ///< last change
    std::string created_with;   ///< "Yantra <version>" that created the file
    std::string modified_with;  ///< "Yantra <version>" of the last change
    int schema_version = 0;
};

/// A plan source file the project keeps (IND-03).
struct SourceFile {
    /// id, file name, SHA-256 and embedded flag, as in the building model.
    building::PlanSource plan;
    std::string media_type;  ///< sniffed: application/pdf, image/png, ... or application/octet-stream
    std::uint64_t size = 0;  ///< bytes
    /// Reference sources: the cached file's path when it was added (a hint;
    /// the SHA-256 identifies the file).
    std::string reference;
    std::string added_utc;
    bool operator==(const SourceFile&) const = default;
};

struct AddSourceResult {
    std::string id;
    bool added = false;  ///< false: the same content was already in the project
};

/// A change, for the UI.
struct Change {
    enum class Kind {
        Info,             ///< project metadata
        Building,         ///< building name and defaults, or replace_building()
        FloorAdded,
        FloorChanged,
        FloorRemoved,
        FloorsReordered,  ///< the levels (and so the order) changed
        SourceAdded,
        SourceRemoved,
        Saved,            ///< save() or save_as(); path() may have changed
    };
    Kind kind = Kind::Info;
    std::string id;  ///< floor or source id
};

struct ProjectOptions {
    /// Current UTC time as ISO 8601 ("...Z"); the system clock when empty.
    std::function<std::string()> clock;
    /// "Yantra <version>"; the core's build version when empty.
    std::string tool;
    /// Write "<file>.schema<N>.bak" before migrating an older file.
    bool backup_before_migration = true;
    /// Tests only: called at the named points of create/save_as/migration
    /// ("create:written", "save_as:written", "migrate:step"); a throw
    /// there stands for a crash or a full disk at that point.
    std::function<void(std::string_view point)> fault;
    /// Tests only: extra schema steps after kSchemaVersion (SQL), as a
    /// later Yantra would add them, to exercise the migration path.
    std::vector<std::string> testing_extra_steps;
};

class Project {
public:
    /// Creates a new project file at `path` (IND-01) and opens it. The
    /// building gets the project's site name (or its name without one).
    /// Exists when the path exists; Invalid for an empty name.
    static std::unique_ptr<Project> create(const std::filesystem::path& path, const ProjectInfo& info,
                                           ProjectOptions options = {});
    /// Opens a project file, migrating an older schema version.
    static std::unique_ptr<Project> open(const std::filesystem::path& path, ProjectOptions options = {});
    ~Project();
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    const std::filesystem::path& path() const;
    /// The schema version the file had when it was opened (before a
    /// migration).
    int opened_version() const;
    /// The backup written before a migration, empty when none.
    const std::filesystem::path& migration_backup() const;

    // -- metadata -----------------------------------------------------
    ProjectMeta meta() const;
    void set_info(const ProjectInfo& info);
    /// The building's name ("<name>, <floor name>" is the plan name) and
    /// its default slab attenuation (IND-44).
    void set_building_name(const std::string& name);
    void set_floor_attenuation_db(double db);

    // -- floors (IND-04) -----------------------------------------------
    /// Floors ordered by level, lowest first. Every floor has its own
    /// level (the building model identifies floors by level).
    std::vector<building::Floor> floors() const;
    std::optional<building::Floor> floor(const std::string& id) const;
    /// Adds a floor and returns its id: the given one when not empty (it
    /// must be unused), else "floor-<n>" (never reused). Invalid for an
    /// empty name, a used level or a value the building model refuses;
    /// NotFound for an unknown source; Unsupported for a trim, scale,
    /// reference point, walls or antennas (later schema versions).
    std::string add_floor(building::Floor floor);
    /// Updates every field but the id (a new level must be free).
    void update_floor(const building::Floor& floor);
    void rename_floor(const std::string& id, const std::string& name);
    /// Sets the level number and, when given, the floor-to-floor height.
    void set_floor_level(const std::string& id, int level, std::optional<double> height_m = std::nullopt);
    void set_floor_height(const std::string& id, double height_m);
    /// Reorders: moves a floor to position `index` (clamped) of the list
    /// ordered by level; the floors keep the same set of levels, handed
    /// out in the new order.
    void move_floor(const std::string& id, std::size_t index);
    /// Removes a floor (the UI confirms first, IND-04).
    void remove_floor(const std::string& id);

    // -- building model ------------------------------------------------
    /// The building model of the project: name, slab default, sources and
    /// floors (ordered by level), the default wall-class table.
    building::Building building() const;
    /// Replaces the building in one transaction: name, slab default and
    /// floors (floors not in `b` are removed). Every source of `b` must be
    /// in the project with the same id (and SHA-256 when `b` gives one);
    /// sources are added with add_source*. Invalid when b.validate()
    /// fails; Unsupported for data of later schema versions.
    void replace_building(const building::Building& b);

    // -- source files (IND-03) -------------------------------------------
    /// Copies a file into the project unchanged; its id is
    /// "source-<n>". The same content added again returns the existing
    /// source. Io when it cannot be read or changes while it is copied.
    AddSourceResult add_source(const std::filesystem::path& file);
    /// Adds a reference to a cached licence-restricted file (IND-03,
    /// DEMO-12): its name, size and SHA-256 are stored, not its content.
    AddSourceResult add_source_reference(const std::filesystem::path& cached_file);
    std::vector<SourceFile> sources() const;
    std::optional<SourceFile> source(const std::string& id) const;
    /// The content; Damaged when it no longer matches its hash, Io when a
    /// referenced file is missing (use read_source_from for a moved cache).
    std::vector<std::uint8_t> read_source(const std::string& id) const;
    /// A reference source's content from `file` (e.g. the cache on another
    /// computer); Damaged when it is not the file the project uses.
    std::vector<std::uint8_t> read_source_from(const std::string& id, const std::filesystem::path& file) const;
    /// Writes the content to `file` (replaced atomically).
    void export_source(const std::string& id, const std::filesystem::path& file) const;
    /// InUse while a floor uses it.
    void remove_source(const std::string& id);

    // -- file ---------------------------------------------------------
    /// Folds the write-ahead log into the file (changes are already
    /// stored); afterwards the file alone holds the whole project.
    void save();
    /// Writes a compact copy to `path` and continues with that file; the
    /// old file stays as it was. Exists when `path` exists and `replace`
    /// is false (the UI asks first); Invalid for the open file itself.
    void save_as(const std::filesystem::path& path, bool replace = false);
    /// Saves and closes; later calls throw Closed. The destructor closes
    /// too (without throwing).
    void close();
    bool is_open() const;
    /// SQLite's integrity and foreign-key checks and the source hashes:
    /// problems, one line each; empty when the file is sound. Missing
    /// reference files are not problems of the file.
    std::vector<std::string> check() const;

    // -- notifications ---------------------------------------------------
    using Listener = std::function<void(const Change&)>;
    int subscribe(Listener listener);
    void unsubscribe(int token);

    struct Impl;

private:
    explicit Project(std::unique_ptr<Impl> impl);
    void notify(const Change& change) const;
    std::unique_ptr<Impl> impl_;
};

namespace testing {
/// Creates a project file with the schema of `version` (1..kSchemaVersion)
/// as an older Yantra would have written it: for migration tests and fuzz
/// seeds.
void create_at_version(const std::filesystem::path& path, int version, const ProjectInfo& info);
}  // namespace testing

}  // namespace yantra::project
