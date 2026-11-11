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
// and floor-level fields and the sources; version 2 (step 7.2) adds the
// floors' non-destructive trims (IND-05..07) and the page sizes of each
// source (IND-02); version 3 (step 7.3) the rest of the building model:
// floor scales and alignment points (IND-08/09), the project's wall-class
// table, walls, cells and antennas (IND-11/12); version 4 (step 7.5) the
// project's recordings (REC-02/03), its walk tests and manual-point sets
// (sessions, IND-20, GEO-02, GEO-05) and their measured points, the one
// point list of IND-30 (core/survey/points.h reads it); version 5 (step
// 7.6) the cell database in the cells (DB-01: site, sector, azimuth,
// colour, notes, origin, parameters, neighbour lists); version 6 (step
// 8.2) the ray-tracing settings (IND-40) and the stored interpolation
// results (IND-41..45; core/survey/interpolation.h makes and reads them).
// No part of a model is ever dropped silently.
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
//
// Copied from yantra-v2 8c7a3e1: core/include/yantra/core/project/project.h,
// adapted for IND-01/03/04 (building-model floors and sources).
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
#include "yantra/core/celldb/database.h"

namespace yantra::project {

/// The file name extension of a project (IND-01).
inline constexpr const char* kExtension = ".yantra";
/// SQLite application_id of a project file ("YNTR").
inline constexpr std::int32_t kApplicationId = 0x594E5452;
/// The value of the format mark in the meta table.
inline constexpr const char* kFormat = "yantra-project";
/// Schema version written by this build (docs/formats/PROJECT.md).
inline constexpr int kSchemaVersion = 7;

class ProjectError : public std::runtime_error {
public:
    enum class Code {
        NotAProject,  ///< not an SQLite file, or not a Yantra project
        TooNew,       ///< made by a newer Yantra (higher schema version)
        Locked,       ///< open in another Yantra (or another program)
        Exists,       ///< create/save_as: the target exists
        Damaged,      ///< the file is damaged (corrupt database, wrong hash)
        Io,           ///< reading or writing a file failed
        NotFound,     ///< no floor, source, session or recording with that id
        InUse,        ///< a source, wall class, cell or floor still in use
        Invalid,      ///< an argument is not valid (e.g. an empty name)
        Unsupported,  ///< data this schema version cannot store
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

/// The size of one page of a plan source in page units (PDF points or
/// image pixels; IND-02), recorded by the plan import (plan/import.h) so
/// the page list is known without reading the file.
struct SourcePage {
    double width = 0.0;
    double height = 0.0;
    double units_per_inch = 0.0;  ///< 72 for PDF; an image's stored resolution, else 0
    bool operator==(const SourcePage&) const = default;
};

/// A licence-restricted source file known only by its description
/// (add_source_reference without the file).
struct SourceReference {
    std::string file_name;   ///< no folder
    std::string media_type;  ///< e.g. image/jpeg
    std::uint64_t size = 0;  ///< bytes
    std::string sha256;      ///< 64 lower-case hex digits
    std::string reference;   ///< the path hint ("" none)
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
        FloorChanged,     ///< also its scale, reference point, walls or antennas
        FloorRemoved,
        FloorsReordered,  ///< the levels (and so the order) changed
        SourceAdded,
        SourceRemoved,
        SourceChanged,    ///< set_source_pages()
        WallClasses,      ///< the wall-class table changed (IND-11)
        Cells,            ///< a cell was added, changed or removed (IND-12)
        Recordings,       ///< a recording was added or removed (id: its id)
        Sessions,         ///< a session was added, changed or removed (id: its id; "" for those removed with their floor)
        Points,           ///< points of a session were added or removed (id: the session id)
        Saved,            ///< save() or save_as(); path() may have changed
        Propagation,      ///< the ray-tracing settings (IND-40)
        Interpolations,   ///< an interpolation result was stored or removed (id: its floor)
        Acceptance,       ///< the stored acceptance profiles (EXP-06)
    };
    Kind kind = Kind::Info;
    std::string id;  ///< floor or source id
};

/// A recording file of the project's walks (REC-02, REC-03; schema 4).
/// The recording itself is a file of its own (docs/formats/RECORDING.md)
/// in the project's recordings folder; the project keeps its name, size,
/// SHA-256 and header, so a moved or changed file is noticed.
struct RecordingFile {
    std::int64_t id = 0;            ///< given by add_recording
    std::string file_name;          ///< e.g. "walk_0000.yrec.zst"; unique in the project
    std::string session_id;         ///< the recording session id (16 hex digits)
    std::string session_name;
    std::uint32_t file_index = 0;
    std::string sha256;             ///< 64 lower-case hex digits
    std::uint64_t size = 0;         ///< bytes
    std::string reference;          ///< the path when it was added (a hint)
    std::string header;             ///< the header line (JSON)
    std::string added_utc;          ///< set by add_recording
    bool operator==(const RecordingFile&) const = default;
};

/// A walk test or a set of manual points on one floor (IND-20, GEO-02,
/// GEO-05; schema 4 `sessions`). Several per floor. A walk that changes
/// floor goes on in a new session on the other floor (`continues`).
struct Session {
    std::int64_t id = 0;            ///< given by add_session
    std::string floor_id;
    std::string kind = "walk";      ///< "walk" (GEO-02), "manual" (GEO-05) or "simulated" (sim_position, RAD-19)
    std::string name;
    std::string started_utc;        ///< ISO 8601; set by add_session when empty
    std::string ended_utc;          ///< "" while it runs
    std::string radio;              ///< the radio (plugin, device model and serial; no personal data)
    std::string calibration;        ///< the calibration in use (JSON text)
    std::string notes;
    std::string walk_mode = "point_and_click";  ///< or "continuous"
    std::string recording_session;  ///< the recording session id ("" without one)
    std::string plugins;            ///< provenance (PLG-12): JSON object plugin id -> version
    std::optional<std::int64_t> continues;  ///< the session on the floor before (floor change)
    bool operator==(const Session&) const = default;
};

/// A measured point of a session (schema 4 `points`): a sample with a
/// plan position. The values are the sample's JSON (core/survey/point.h
/// writes and reads it; the project keeps it as text).
struct StoredPoint {
    std::int64_t session_id = 0;
    std::int64_t sequence = 0;            ///< 1, 2, ... in the session; ascending
    std::int64_t time_ns = 0;             ///< the sample's time on the pipeline clock (monotonic)
    std::int64_t utc_ns = 0;
    building::Point2 plan;                ///< plan units of the floor (pixels for display)
    std::optional<building::Point2> metres;  ///< plan-frame metres with the floor's current scale; nullopt without one
    std::string kind = "walk";            ///< walk, manual, simulated
    std::uint32_t group = 0;              ///< walk segment number / manual point number
    std::uint32_t cycles = 1;             ///< measurement cycles it is made of (manual: averaged)
    std::string label;                    ///< manual: the point's label (e.g. a room)
    std::string sample;                   ///< the values (JSON)
    bool operator==(const StoredPoint&) const = default;
};

/// Settings of the ray-tracing model (IND-40), stored in the project
/// (schema 6). Ranges as raytrace::ModelParams (docs/PROPAGATION.md); the
/// dB per floor of IND-44 is the building's floor_attenuation_db.
struct PropagationSettings {
    double grid_m = 0.25;
    int max_reflections = 2;
    double path_loss_exponent = 2.0;
    double reference_distance_m = 1.0;
    double reflection_loss_db = 6.0;
    double rx_height_m = 1.5;
    bool other_floors = true;
    double default_eirp_dbm = 10.0;
    bool operator==(const PropagationSettings&) const = default;
    /// Problems, one message each; empty when usable.
    std::vector<std::string> validate() const;
};

/// A stored interpolation result (IND-41..43, IND-45; schema 6): the
/// project keeps it as data, survey/interpolation.h makes and reads it.
/// One per floor and key; storing another with the same key replaces it.
struct StoredInterpolation {
    std::int64_t id = 0;           ///< set by store_interpolation
    std::string floor_id;
    std::string key;               ///< what it is for (metric and source group), not empty
    std::string created_utc;       ///< set by store_interpolation
    std::string created_with;      ///< "Yantra <version>", set by store_interpolation
    /// Fingerprints (SHA-256, 64 lower-case hex digits) of the inputs: the
    /// building model, the measured points, the settings and parameters.
    std::string building_sha256;
    std::string points_sha256;
    std::string settings_sha256;
    std::string parameters_json;   ///< JSON object: everything needed to compute it again
    std::string result_json;       ///< JSON object: fit, sources, points
    building::Point2 origin_m;     ///< the grid (raytrace::Grid), plan-frame metres
    double cell_m = 0.0;
    int cols = 0;
    int rows = 0;
    /// Per cell (grid index order) value, model and confidence as
    /// little-endian float32: 12 bytes per cell.
    std::vector<std::uint8_t> cells;
    bool operator==(const StoredInterpolation&) const = default;
};

/// A stored acceptance profile (EXP-06; schema 7): the project keeps it as
/// data, survey/acceptance.h makes and reads the definition. The project's
/// profiles are an ordered list with unique names.
struct StoredAcceptanceProfile {
    std::string name;        ///< not empty, unique in the project
    std::string definition;  ///< JSON object (survey/acceptance.h)
    bool operator==(const StoredAcceptanceProfile&) const = default;
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

/// IND-01: a path as a project file stores it (plan-source references,
/// recordings): relative to `folder` (the project file's folder) with "/"
/// separators when both are on the same volume, so the project folder can
/// be moved or shared; absolute otherwise. "" stays "".
std::string stored_path(const std::filesystem::path& folder, const std::filesystem::path& file);
/// IND-01: the file a stored path names: an absolute path as it is (files
/// of earlier versions), a relative one in `folder`. "" gives "".
std::filesystem::path resolve_stored_path(const std::filesystem::path& folder, const std::string& stored);

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
    /// IND-01: `file` as this project stores a path (stored_path() in the
    /// project file's folder).
    std::string stored_path(const std::filesystem::path& file) const;
    /// IND-01: the file a stored path names (resolve_stored_path() in the
    /// project file's folder).
    std::filesystem::path resolve_path(const std::string& stored) const;
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
    /// NotFound for an unknown source; Invalid for a trim the building
    /// model refuses or a page the source does not have (when its pages
    /// are recorded). The floor may carry a scale, reference point, walls
    /// and antennas (ids as the building model requires).
    std::string add_floor(building::Floor floor);
    /// Updates every field but the id (a new level must be free). When
    /// the trim changes, the floor's measured points are mapped through the
    /// page to the new trim (they stay where they were measured on the
    /// sheet, as plan::reanchor_floor does for walls and antennas); their
    /// metres follow the floor's scale.
    void update_floor(const building::Floor& floor);
    void rename_floor(const std::string& id, const std::string& name);
    /// Sets the level number and, when given, the floor-to-floor height.
    void set_floor_level(const std::string& id, int level, std::optional<double> height_m = std::nullopt);
    void set_floor_height(const std::string& id, double height_m);
    /// Reorders: moves a floor to position `index` (clamped) of the list
    /// ordered by level; the floors keep the same set of levels, handed
    /// out in the new order.
    void move_floor(const std::string& id, std::size_t index);
    /// Removes a floor (the UI confirms first, IND-04). InUse while
    /// sessions are on it (their measured points would be lost).
    void remove_floor(const std::string& id);
    /// The same; with `with_sessions` the floor's sessions and their
    /// points go with it, in the same transaction (all or none; the UI's
    /// confirmation names them). A session on another floor that
    /// `continues` one of them keeps its points and loses the link.
    void remove_floor(const std::string& id, bool with_sessions);

    // -- building model ------------------------------------------------
    /// The building model of the project: name, slab default, the
    /// wall-class table, cells, sources and floors (ordered by level) with
    /// their scales, reference points, walls and antennas.
    building::Building building() const;
    /// Replaces the building in one transaction: name, slab default,
    /// wall-class table, cells and floors (floors not in `b` are removed).
    /// Every source of `b` must be in the project with the same id (and
    /// SHA-256 when `b` gives one); sources are added with add_source*.
    /// Invalid when b.validate() fails.
    void replace_building(const building::Building& b);

    // -- scale and alignment (IND-08, IND-09) ----------------------------
    /// Sets a floor's scale (building/edit.h: two_point_scale, scale_bar,
    /// factor_scale, shared_scale; Scale{} clears it: "not to scale").
    /// Floors sharing this floor's scale follow it. Invalid for a scale
    /// the building model refuses, a Shared scale from a floor of another
    /// sheet (IND-08 "floors from one sheet", building::same_sheet), and
    /// clearing or sharing the scale of a floor others share from;
    /// NotFound for an unknown floor.
    void set_scale(const std::string& floor_id, const building::Scale& scale);
    /// The floors `floor_ids` share the scale of `from_id` (one
    /// transaction, all or none); as set_scale(id, shared_scale(from_id)).
    void share_scale(const std::string& from_id, const std::vector<std::string>& floor_ids);
    /// Sets or clears a floor's alignment point (plan units, IND-09).
    void set_reference_point(const std::string& floor_id, std::optional<building::Point2> point);

    // -- wall classes (IND-11) ---------------------------------------------
    /// The project's editable table, in table order; a new project has
    /// building::default_wall_classes().
    std::vector<building::WallClass> wall_classes() const;
    /// Adds a class (at the end) or replaces the class with its id.
    /// Invalid for a class the building model refuses (empty id, no band,
    /// bands not ascending, negative losses).
    void set_wall_class(const building::WallClass& wall_class);
    /// InUse while a wall uses it; NotFound when there is none.
    void remove_wall_class(const std::string& id);
    /// Puts the default classes back to their default values (adding
    /// missing ones at the end); the project's own classes stay.
    void reset_wall_classes();

    // -- walls (IND-11) ----------------------------------------------------
    /// The walls of a floor in drawing order; NotFound for an unknown floor.
    std::vector<building::Wall> walls(const std::string& floor_id) const;
    /// Adds a wall (a polyline in plan units, >= 2 finite points, a class
    /// of the table or its own attenuation) and returns its id: the given
    /// one when not empty (unused on the floor), else "wall-<n>" (never
    /// reused). Invalid for a wall the model refuses.
    std::string add_wall(const std::string& floor_id, building::Wall wall);
    /// Adds several walls in one transaction (all or none).
    std::vector<std::string> add_walls(const std::string& floor_id, std::vector<building::Wall> walls);
    /// Replaces the wall with wall.id on the floor (points, class,
    /// override); NotFound when there is none.
    void update_wall(const std::string& floor_id, const building::Wall& wall);
    void remove_wall(const std::string& floor_id, const std::string& wall_id);

    // -- cells and antennas (IND-12) ----------------------------------------
    std::vector<building::Cell> cells() const;
    /// Adds a cell and returns its id (given, else "cell-<n>").
    std::string add_cell(building::Cell cell);
    void update_cell(const building::Cell& cell);
    /// InUse while an antenna serves it; NotFound when there is none.
    void remove_cell(const std::string& id);
    /// The antennas of a floor in placement order; NotFound for an unknown floor.
    std::vector<building::Antenna> antennas(const std::string& floor_id) const;
    /// Places an antenna on a floor and returns its id (given and unused
    /// in the building, else "antenna-<n>"). Invalid for values the model
    /// refuses (pattern, elevation, losses, an unknown cell).
    std::string add_antenna(const std::string& floor_id, building::Antenna antenna);
    /// Replaces the antenna with antenna.id (it stays on its floor).
    void update_antenna(const building::Antenna& antenna);
    /// Moves an antenna to another floor (or the same) at `position`
    /// (plan units of that floor); it goes last in that floor's list.
    void move_antenna(const std::string& antenna_id, const std::string& floor_id, building::Point2 position);
    void remove_antenna(const std::string& antenna_id);

    // -- cell database (DB-01..05; schema 5) ------------------------------
    /// The project's cells as the cell database (its cells() plus the
    /// database columns, parameters, neighbour lists and match fields).
    celldb::CellDatabase cell_database() const;
    /// Makes `db` the project's cells: entries not in it are removed (InUse
    /// while an antenna serves one; nothing is changed then), the others
    /// updated or added in its order. Invalid for an entry the model or the
    /// database refuses. Notifies Change::Kind::Cells with an empty id.
    void set_cell_database(const celldb::CellDatabase& db);

    // -- recordings (REC-02, REC-03; schema 4) ------------------------------
    /// Adds a recording file and returns its id. Invalid for an empty
    /// name, a SHA-256 that is not 64 hex digits; Exists when the name is
    /// already listed.
    std::int64_t add_recording(RecordingFile file);
    /// In the order they were added.
    std::vector<RecordingFile> recordings() const;
    /// Takes it off the list (the file itself is not touched).
    void remove_recording(std::int64_t id);

    // -- sessions and points (IND-20, IND-30; schema 4) -------------------
    /// Adds a session and returns its id. NotFound for an unknown floor
    /// or `continues`; Invalid for another kind or walk mode.
    std::int64_t add_session(Session session);
    /// Updates name, ended_utc, radio, calibration, notes, walk mode,
    /// recording session and plugins (floor and kind stay).
    void update_session(const Session& session);
    std::optional<Session> session(std::int64_t id) const;
    /// The sessions of a floor ("" = all floors), by id.
    std::vector<Session> sessions(const std::string& floor_id = {}) const;
    /// Removes a session and its points.
    void remove_session(std::int64_t id);
    /// Adds points to a session in one transaction (all or none). Their
    /// sequences must be above the session's last; metres are computed
    /// here from the floor's scale (the given ones are ignored). Invalid
    /// for non-finite positions, a sequence not ascending or an unknown
    /// kind; NotFound for an unknown session.
    void add_points(std::int64_t session_id, std::vector<StoredPoint> points);
    /// Removes the points of one walk segment / manual point (`group`);
    /// returns how many.
    std::size_t remove_points(std::int64_t session_id, std::uint32_t group);
    /// The points of a session by sequence.
    std::vector<StoredPoint> points(std::int64_t session_id) const;
    /// One point of a session by its sequence (e.g. the hover readout,
    /// PP-01); nullopt when there is none.
    std::optional<StoredPoint> point(std::int64_t session_id, std::int64_t sequence) const;
    /// The number of points of a floor's sessions.
    std::size_t point_count(const std::string& floor_id) const;

    // -- propagation model and interpolation results (IND-40..45; schema 6)
    /// The ray-tracing settings (a new project: the defaults).
    PropagationSettings propagation() const;
    /// Invalid for a value outside its range. Notifies Propagation.
    void set_propagation(const PropagationSettings& settings);
    /// Stores a result (replacing the one with the same floor and key) in
    /// one transaction and returns its id. NotFound for an unknown floor;
    /// Invalid for an empty key, a fingerprint that is not 64 hex digits,
    /// JSON texts that are not objects, a grid that is not positive or
    /// cells of the wrong size. Notifies Interpolations (id: the floor).
    std::int64_t store_interpolation(StoredInterpolation result);
    /// The results of a floor ordered by key; without the cells unless
    /// asked for (they can be large).
    std::vector<StoredInterpolation> interpolations(const std::string& floor_id, bool with_cells = false) const;
    /// One result with its cells, or nullopt.
    std::optional<StoredInterpolation> interpolation(std::int64_t id) const;
    /// NotFound when there is none. Removing a floor removes its results.
    void remove_interpolation(std::int64_t id);

    // -- acceptance profiles (EXP-06; schema 7) ----------------------------
    /// The stored profiles in their order (a new project: none).
    std::vector<StoredAcceptanceProfile> acceptance_profiles() const;
    /// Replaces every stored profile with `profiles` (in this order) in one
    /// transaction. Invalid for an empty or repeated name or a definition
    /// that is not a JSON object. Notifies Acceptance.
    void set_acceptance_profiles(const std::vector<StoredAcceptanceProfile>& profiles);

    // -- source files (IND-03) -------------------------------------------
    /// Copies a file into the project unchanged; its id is
    /// "source-<n>". The same content added again returns the existing
    /// source. Io when it cannot be read or changes while it is copied.
    AddSourceResult add_source(const std::filesystem::path& file);
    /// Adds a reference to a cached licence-restricted file (IND-03,
    /// DEMO-12): its name, size and SHA-256 are stored, not its content.
    AddSourceResult add_source_reference(const std::filesystem::path& cached_file);
    /// The same without the file (DEMO-12, DEMO-13: an example project
    /// made where the licence-restricted file may not be): the file's
    /// name, media type, size and SHA-256 as described, `reference` as
    /// the path hint. Invalid for an empty name, a size of 0 or a SHA-256
    /// that is not 64 lower-case hex digits; the same SHA-256 again
    /// returns the existing source.
    AddSourceResult add_source_reference(const SourceReference& described);
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
    /// The recorded page sizes of a source, page 0 first; empty when not
    /// recorded yet. NotFound for an unknown source.
    std::vector<SourcePage> source_pages(const std::string& id) const;
    /// Records the page sizes of a source (replacing earlier ones).
    /// Invalid for an empty list, a size that is not > 0 or finite, or
    /// fewer pages than a floor of the source uses; NotFound for an
    /// unknown source.
    void set_source_pages(const std::string& id, const std::vector<SourcePage>& pages);

    // -- file ---------------------------------------------------------
    /// Folds the write-ahead log into the file (changes are already
    /// stored); afterwards the file alone holds the whole project.
    void save();
    /// Writes a compact copy to `path` and continues with that file; the
    /// old file stays as it was. Exists when `path` exists and `replace`
    /// is false (the UI asks first); Invalid for the open file itself.
    void save_as(const std::filesystem::path& path, bool replace = false);
    /// Writes a compact copy to `path` as save_as does, but keeps working
    /// on the open file (e.g. a scratch copy for a batch export, EXP-04).
    /// Exists, Locked and Invalid as save_as.
    void write_copy(const std::filesystem::path& path, bool replace = false) const;
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
