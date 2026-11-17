// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bundled demo project (DEMO-05, DEMO-06, DEMO-08; docs/DEMO.md): a
// description in data/demo/<id>/<id>.json (format "yantra-demo-project"
// version 1: plan sheets, floors with trims, scale bars, reference
// points, the cell database and one walk route per floor) from which the demo project is made reproducibly:
//
// 1. create_project: the building in a new project file (the sheets
//    copied in unchanged and checked against their SHA-256, the floors
//    trimmed from them with their example ids, RAD-18).
// 2. walk_scenario: per floor a yantra-radios scenario (RAD-74) for
//    yantra-gen (RAD-71): the network of the scenario the floor's example
//    id selects, the floor's fixed demo scene (data/demo/scenes, RAD-36,
//    read_demo_scene) and the walk as the receiver path. yantra-gen writes its ground truth as a
//    record file (RAD-65) without any IQ reaching Yantra.
// 3. record_walk: that record file replayed through the measurement
//    pipeline on a clock that follows the recorded times, into a
//    continuous walk test that clicks each waypoint when the walker
//    reaches it, recorded (REC-01) with a fixed session id; the points go
//    into the project and the files into its recording list.
// 4. interpolation_request: the point interpolation (IND-41, IND-45) of
//    each floor's walk, stored in the project.
//
// The MAK example buildings of the tour (DEMO-12, DEMO-13) use the same
// format with a licence-restricted sheet: never shipped or copied, the
// project references it by SHA-256 (IND-03); create_project checks a
// cached file against the description or, for the build, which never has
// the file, references it from the description alone (CreateOptions).
//
// The steps take their inputs only from the description, the sheets and
// the record files, so the same inputs give the same project content and
// the same recording files (tests/demo). The core holds no TETRA or radio
// code here: the cells, their identity values and the scenario are data.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "yantra/core/building/model.h"
#include "yantra/core/measure/catalog.h"
#include "yantra/core/measure/record_file.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/interpolation.h"
#include "yantra/core/survey/points.h"

namespace yantra::demo {

using building::Point2;

/// A description that cannot be read or used (the message names the
/// file and the place).
class DemoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr const char* kFormat = "yantra-demo-project";
inline constexpr int kFormatVersion = 1;

/// Licence and credit of a licence-restricted sheet (DEMO-12), shown with
/// the plan and in its reports.
struct DemoCredit {
    std::string holder;            ///< e.g. "MAK - Museum of Applied Arts, Vienna"
    std::string designer;
    std::string year;
    std::string inventory_number;
};

/// A plan sheet of the demo: bundled next to the description
/// (data/demo/<id>/), or licence-restricted (DEMO-12): never shipped,
/// downloaded by the user's app on first use and referenced by its
/// SHA-256 (IND-03).
struct DemoSheet {
    std::string id;
    std::string file;        ///< file name next to the description (bundled) or in the cache (restricted)
    std::string sha256;      ///< of the file, checked whenever the file is used
    std::string source_url;  ///< where it is fetched (unchanged)
    std::string item_url;    ///< the catalogue page
    std::string title;
    bool restricted = false;  ///< licence-restricted: referenced, never copied (`licence_restricted`)
    std::uint64_t size_bytes = 0;  ///< restricted: the file's size
    std::string media_type;        ///< restricted: e.g. image/jpeg
    int width = 0;                 ///< restricted: page size in pixels (page units)
    int height = 0;
    double units_per_inch = 0.0;   ///< the image's stored resolution, 0 unknown
    std::string licence;           ///< e.g. "CC BY-NC-ND 4.0"
    std::string licence_url;
    DemoCredit credit;
};

struct DemoWalk {
    std::string name;
    double speed_mps = 1.0;
    std::vector<Point2> waypoints;  ///< plan units of the floor, at least 2
};

struct DemoFloor {
    std::string id;
    std::string example_id;  ///< RAD-18, e.g. "fraunces-1"
    std::string name;
    int level = 0;
    double height_m = 3.0;
    std::string sheet;       ///< DemoSheet::id
    Point2 trim_a, trim_b;   ///< trim rectangle corners, page units of the sheet
    /// The scale: a scale bar (ends in plan units and its length), or
    /// metres per plan unit given directly (`scale`: Factor).
    Point2 scale_a, scale_b; ///< the sheet's scale bar ends, plan units
    double scale_length_m = 0.0;     ///< > 0 for a scale bar
    double metres_per_unit = 0.0;    ///< > 0 for a factor
    bool scale_approximate = false;  ///< DEMO-12: marked "approximate"
    std::string scale_note;          ///< how the scale was found
    Point2 reference_point;  ///< plan units (IND-09)
    DemoWalk walk;
};

struct DemoDescription {
    std::string id;
    std::string label;  ///< the demo label (DEMO-07)
    project::ProjectInfo project;
    std::string building_name;
    std::string start_utc;          ///< ISO 8601 UTC: the walks' clock
    std::int64_t start_utc_ns = 0;  ///< the same in ns since 1970
    std::string cell_database;      ///< CSV file name next to the description
    std::vector<DemoSheet> sheets;
    std::vector<DemoFloor> floors;
    std::string interpolation_cell;  ///< the cell whose level is interpolated
    std::filesystem::path folder;    ///< the folder of the description (data)

    const DemoFloor* find_floor(const std::string& id) const;
    const DemoSheet* find_sheet(const std::string& id) const;
};

/// Reads and checks a description (DemoError with the reason): the
/// fields, each trim inside its sheet (when the sheet's size is
/// described) and every walk waypoint and the reference point inside its
/// floor's trim.
DemoDescription read_description(const std::filesystem::path& file);

/// RAD-74 / RAD-18: an `example_ids` entry matches an example id exactly,
/// or, ending in "*", every id starting with the rest.
bool example_id_matches(const std::string& pattern, const std::string& example_id);

/// The scenario file of `scenario_dir` (*.json, format
/// "yantra-radios-scenario") whose `example_ids` match `example_id`, in
/// file name order; nullopt when none does.
std::optional<std::filesystem::path> scenario_for_example(const std::filesystem::path& scenario_dir,
                                                          const std::string& example_id);

/// Where create_project finds the sheets.
struct CreateOptions {
    /// The folder of the licence-restricted sheets (the user's cache,
    /// files named DemoSheet::file); "" = the description's folder.
    /// Bundled sheets are always read next to the description.
    std::filesystem::path sheet_folder;
    /// False: a restricted sheet that is not in `sheet_folder` is
    /// referenced by its described name, size and SHA-256 (the build of
    /// the example projects, which never has the file, DEMO-13); true: it
    /// must be there.
    bool require_restricted_sheets = true;
};

/// Makes a new project file `file` (replacing one that exists) with the
/// description's building: info, building name, the cell database, the
/// sheets and the floors with trim, scale and reference point. Bundled sheets are copied in; restricted
/// sheets are referenced by SHA-256 (IND-03), never copied, with the page
/// size of the description. Every sheet file used is checked against its
/// SHA-256 (and a restricted one against its size and page size).
/// DemoError for a sheet that is missing or not the described file (the
/// message names both hashes and where to get the file); ProjectError
/// from the project.
std::shared_ptr<project::Project> create_project(const DemoDescription& d, const std::filesystem::path& file,
                                                 const CreateOptions& options = {});

/// The sheet's file in `folder` checked against the description: nullopt
/// when it is not there, DemoError when it is not the described file
/// (SHA-256, and for a restricted sheet its size and page size).
std::optional<std::filesystem::path> check_sheet_file(const DemoSheet& sheet, const std::filesystem::path& folder);

/// Seconds from the walk's start at which the walker reaches each
/// waypoint (metres on the floor's scale at the walk's speed).
std::vector<double> waypoint_times(const building::Building& b, const DemoFloor& f);

/// The yantra-gen scenario (JSON text, format yantra-radios-scenario) of
/// a floor's walk: `base_scenario` (the JSON text of the scenario the
/// floor's example id selects) with `scene` (the floor's fixed demo
/// scene, JSON text in the RAD-36 format: read_demo_scene), the walk as a
/// linear path in plan-frame metres, a duration one second past the last
/// waypoint and the given truth interval. DemoError when the scene is not
/// JSON or the base is not a scenario.
std::string walk_scenario(const std::string& base_scenario, const building::Building& b, const DemoFloor& f,
                          const std::string& scene, double truth_interval_s);

/// DEMO-05, PLG-27: the fixed demo scene of a bundled example floor is the
/// file <scene_dir>/<example id>.json (data/demo/scenes, RAD-36 format;
/// docs/DEMO.md "Fixed demo scenes"). The simulator gets it through
/// get_scene (context::demo_scenes) and yantra-gen through walk_scenario;
/// a floor without an example id has no scene.
std::filesystem::path demo_scene_file(const std::filesystem::path& scene_dir, const std::string& example_id);

/// The text of the floor's fixed demo scene in `scene_dir`; nullopt when
/// the example id is empty or has no scene file there.
std::optional<std::string> read_demo_scene(const std::filesystem::path& scene_dir, const std::string& example_id);

/// The fixed recording session id of a floor's walk: the first 16 hex
/// digits of SHA-256("<demo id>/<floor id>").
std::string walk_session_id(const DemoDescription& d, const DemoFloor& f);

struct WalkRecording {
    std::int64_t session_id = 0;              ///< project session
    std::string recording_session;            ///< the files' session id
    std::size_t points = 0;
    std::vector<std::filesystem::path> files;  ///< in file order
};

/// Replays the ground-truth record file of a floor's walk (times from 0)
/// through a measurement pipeline (technology schemas from `catalog`) on
/// a clock that starts at the description's start time, into a
/// continuous walk test of `project` that clicks each waypoint at its
/// time, recorded into `recordings` (name "<floor id>_walk", fixed
/// session id). The files are added to the project's recording list
/// with the reference "<folder name of recordings>/<file>".
WalkRecording record_walk(const std::shared_ptr<project::Project>& project, const DemoDescription& d,
                          const DemoFloor& f, const measure::RecordFile& truth,
                          std::shared_ptr<measure::TechnologyCatalog> catalog,
                          const std::filesystem::path& recordings);

/// The demo's point metric: the level on the interpolation cell's
/// carrier.
survey::Metric demo_metric(const project::Project& project, const DemoDescription& d);

/// The interpolation request of a floor: the demo metric over every
/// session of the floor, the project's interpolation settings.
survey::InterpolationRequest interpolation_request(const project::Project& project, const DemoDescription& d,
                                                   const DemoFloor& f);

/// One selectable demo scenario of yantra-radios (DEMO-09): its id in
/// the radios' scenario folder (the simulator's `scenario` setting item),
/// a title and what it shows.
struct DemoScenario {
    std::string id;
    std::string title;
    std::string shows;
};

/// Reads data/demo/scenarios.json (format "yantra-demo-scenarios" 1).
std::vector<DemoScenario> read_demo_scenarios(const std::filesystem::path& file);

}  // namespace yantra::demo
