// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenario files of the TETRA test-signal generator (RAD-74): format
// "yantra-radios-scenario", version 1 (JSON), documented in
// docs/SCENARIO_FORMAT.md, files in data/scenarios/. A scenario holds a
// fictitious network (cells with their broadcast parameters, neighbours
// and fallback levels, optional outdoor sites), the floors of a building
// as scenes (RAD-36, inline or by file), antenna power overrides, the
// channel (RAD-72) and multi-wall (RAD-73) parameters, the receiver with
// its impairments, the receiver's path over time and a time line of
// events. The simulator (RAD-61), yantra-gen (RAD-71) and the tests use the
// same files; the scenario engine (scenario_engine.hpp) runs them.
// The scenario structure of yantra 68ea20a
// (plugins/tetra/scenario/include/yantra/tetra/scenario/scenario.h) was
// the starting point, made indoor only (scenes instead of the building
// model, no routes on maps, no geo origin).
#pragma once

#include "channel.hpp"
#include "downlink.hpp"
#include "multiwall.hpp"
#include "pdu.hpp"

#include <yantra/radios/scene.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::gen {

inline constexpr const char* kScenarioFormat = "yantra-radios-scenario";
inline constexpr uint32_t kScenarioVersion = 1;

/// A problem in a scenario: "<path>: <what>", e.g.
/// "cells[1].neighbours[0].cell: unknown cell \"X9\"".
struct ScenarioError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// One entry of a cell's broadcast neighbour list. The optional values
/// plant a finding: the neighbour is announced with a value that differs
/// from what that cell broadcasts. Without a planted MCC/MNC an entry
/// carries the neighbour's own MCC and MNC when its network differs from
/// the broadcasting cell's (EN 300 392-2 neighbour cell information;
/// RAD-22), else none (the receiver then takes the broadcaster's).
struct ScenarioNeighbour {
    std::string cell;
    std::optional<uint16_t> main_carrier;
    std::optional<uint16_t> location_area;
    std::optional<uint16_t> mcc;  ///< planted MCC (10 bits)
    std::optional<uint16_t> mnc;  ///< planted MNC (14 bits)
};

/// An outdoor site (a macro cell seen from inside the building), in the
/// floors' plan frame (metres; may lie outside the plan).
struct ScenarioSite {
    double x_m = 0.0;
    double y_m = 0.0;
    double height_m = 30.0;  ///< above the receiver's floor
    double eirp_dbm = 45.0;
    radios::ScenePattern pattern;  ///< omni or sector
};

struct ScenarioCell {
    std::string id;     ///< unique; matches the scene's cells[].id
    std::string label;
    /// The cell's own network when it is not the scenario's (a neighbour
    /// network); see cell_mcc() and cell_mnc().
    std::optional<uint16_t> mcc;
    std::optional<uint16_t> mnc;
    uint8_t cell_number = 1;  ///< 0..31, the neighbour cell identifier
    uint16_t location_area = 1;
    uint8_t colour_code = 1;
    uint16_t main_carrier = 3600;
    uint8_t frequency_band = 3;
    uint8_t offset = 0;  ///< 0 none, 1 +6.25, 2 -6.25, 3 +12.5 kHz
    bool encryption = false;  ///< air-interface encryption flag only
    uint8_t rxlev_access_min = 2;
    uint8_t ms_txpwr_max_cell = 5;
    CellReselectParameters reselect;
    bool network_time = true;
    std::vector<ScenarioNeighbour> neighbours;
    /// Level without a scene or position (and for a cell that has no
    /// antenna on the receiver's floor and no site), dBm.
    double level_dbm = -70.0;
    std::optional<ScenarioSite> site;
    double ppm = 0.0;            ///< base-station oscillator error
    double time_offset_s = 0.0;  ///< transmitting this long at t = 0 (0..61.2 s)
    bool on_air = true;          ///< false: a failed site from the start
};

/// One floor: its scene, inline or from a file (path relative to the
/// scenario file).
struct ScenarioFloor {
    radios::Scene scene;
    std::string file;  ///< as written in the scenario, "" for an inline scene
};

/// EIRP override for an antenna id (on any floor, also scenes the host
/// provides at run time).
struct AntennaOverride {
    std::string id;
    double eirp_dbm = 0.0;
};

enum class FadingModel { none, rayleigh, rician, static_channel, tu50, ht200 };
FadingModel fading_model_from_string(const std::string& s);
const char* to_string(FadingModel m);

struct ChannelSpec {
    /// Site cells: log-distance path loss, building penetration and
    /// correlated shadowing at the receiver position.
    LogDistanceModel path_loss{3.5, 1.0, std::nullopt};
    double penetration_loss_db = 12.0;
    double shadowing_sigma_db = 0.0;
    double shadowing_decorrelation_m = 20.0;
    /// Scene cells (RAD-73); receiver_height_m and seed are set from the
    /// receiver and the scenario.
    MultiWallParams multiwall;
    FadingModel fading = FadingModel::none;
    double rician_k_db = 6.0;
    int sinusoids = 32;
    /// Maximum Doppler of flat fading; none: from the path speed, at
    /// least min_doppler_hz.
    std::optional<double> doppler_hz;
    double min_doppler_hz = 1.0;
    /// A fixed Doppler shift of every carrier.
    double doppler_shift_hz = 0.0;
};

struct ReceiverSpec {
    double noise_figure_db = 7.0;
    bool noise = true;
    /// Level of sample power 1, dBm.
    double reference_level_dbm = -20.0;
    double height_m = 1.5;
    double ppm = 0.0;        ///< LO error: every carrier moves by -ppm * centre
    double clock_ppm = 0.0;  ///< sample-clock error
    double gain_db = 0.0;
    double iq_gain_db = 0.0;
    double iq_phase_deg = 0.0;
    bool swap_iq = false;
    /// DC spike relative to full scale (dBFS); none: no DC.
    std::optional<double> dc_dbfs;
    /// Level of a full-scale component amplitude (power of a constant
    /// sample (A, 0)), dBm; none: the reference level (A = 1).
    std::optional<double> full_scale_dbm;
    bool clip = false;
    int quantise_bits = 0;  ///< 0 off, 8 like cs8
    double drops_first_s = 0.0;
    double drops_interval_s = 0.0;  ///< 0: no drops
    uint64_t drops_samples = 0;
};

/// A receiver position at a time.
struct PathPoint {
    double t = 0.0;
    std::string floor;  ///< floor (scene) id; "" outside any floor
    double x_m = 0.0;
    double y_m = 0.0;
};

struct ReceiverPath {
    /// false: each point is held until the next point's time; true: linear
    /// between points on the same floor (a floor change is a jump).
    bool linear = false;
    std::vector<PathPoint> points;  ///< increasing t; empty: no position
};

enum class EventType { cell_off, cell_on, cell_level, rx_gain };

struct ScenarioEvent {
    double t = 0.0;
    EventType type = EventType::cell_off;
    std::string cell;  ///< cell_off, cell_on, cell_level
    double db = 0.0;   ///< cell_level: offset added to the cell's level; rx_gain: receiver gain
};

struct Scenario {
    uint32_t version = kScenarioVersion;
    std::string id;
    std::string name;
    std::string description;
    /// Bundled example plans (RAD-18 example ids) this scenario belongs to;
    /// an entry ending in '*' matches every id with that prefix.
    std::vector<std::string> example_ids;
    bool is_default = false;  ///< the scenario for plans without one
    uint64_t seed = 1;
    double duration_s = 10.0;  ///< 0: endless (the simulator)
    double truth_interval_s = 0.1;
    double sample_rate_hz = 500e3;
    std::optional<double> centre_frequency_hz;
    uint16_t mcc = 901;
    uint16_t mnc = 1;
    std::vector<ScenarioCell> cells;
    std::vector<ScenarioFloor> floors;
    std::vector<AntennaOverride> antennas;
    ChannelSpec channel;
    ReceiverSpec receiver;
    ReceiverPath path;
    std::vector<ScenarioEvent> events;  ///< increasing t
    std::vector<std::string> planted;   ///< what the scenario plants on purpose

    const ScenarioCell* find_cell(std::string_view cell_id) const;
    const radios::Scene* find_scene(std::string_view floor_id) const;
};

/// Reads and validates a scenario document; scene files are read relative
/// to `base_dir` ("" for the current directory). ScenarioError naming the
/// place of the problem; a newer version or another format is refused.
/// Unknown members are ignored (version 1 may gain members compatibly).
Scenario parse_scenario(std::string_view json_text, const std::string& base_dir = "");
/// Returns the text of a scene file named in a scenario ("scene_file" as
/// written); throws ScenarioError when there is none.
using ScenarioFileReader = std::function<std::string(const std::string& file)>;
/// parse_scenario() with scene files from `read_scene` instead of the file
/// system (the simulator's scenarios compiled into the plugin, RAD-61).
Scenario parse_scenario(std::string_view json_text, const ScenarioFileReader& read_scene);
/// parse_scenario() of a file, relative scene files from its folder.
Scenario load_scenario_file(const std::string& path);

/// Checks limits and cross-references of a scenario (also one built or
/// changed in code): ScenarioError on the first problem.
void validate_scenario(const Scenario& s);

/// Downlink frequency of a cell (band x 100 MHz + carrier x 25 kHz + offset).
double cell_frequency_hz(const ScenarioCell& c);
/// The span centre: centre_frequency_hz or the middle of the lowest and
/// highest carrier.
double centre_frequency_hz(const Scenario& s);
/// The broadcast configuration of a cell (SYNC, SYSINFO,
/// D-NWRK-BROADCAST with its neighbour list; start time not set).
CellConfig cell_config(const Scenario& s, const ScenarioCell& c);
/// The MCC and MNC cell `c` broadcasts: its own, else the scenario's.
uint16_t cell_mcc(const Scenario& s, const ScenarioCell& c);
uint16_t cell_mnc(const Scenario& s, const ScenarioCell& c);
/// The MCC and MNC that `from`'s neighbour entry `ref` (naming cell `to`)
/// announces: the planted values, else `to`'s own network when it differs
/// from `from`'s, else none (RAD-22).
std::pair<std::optional<uint16_t>, std::optional<uint16_t>> announced_network(const Scenario& s,
                                                                            const ScenarioCell& from,
                                                                            const ScenarioNeighbour& ref,
                                                                            const ScenarioCell& to);
/// The scene with the scenario's antenna EIRP overrides applied.
radios::Scene apply_overrides(const Scenario& s, radios::Scene scene);

/// Adds the cells of `scene` that the scenario lacks (by id), with the
/// scene's LA, colour code and carrier (band, carrier number and offset
/// worked out from carrier_hz) and the other values from `defaults`: for a
/// user's own plan, whose cells the simulator's default scenario does not
/// know. A scene cell on the carrier of a scenario cell that the scene does
/// not name replaces that cell (RAD-61: the plan's cell is calculated from
/// the scene, not given the scenario's level): it takes the old cell's
/// place and neighbour cell identifier, the other cells' neighbour entries
/// name it, and the old cell's events and scenario-floor antennas are
/// dropped. Carriers 6.25 or 12.5 kHz apart are different carriers.
/// ScenarioError for a carrier that is not on the TETRA raster.
void add_scene_cells(Scenario& s, const radios::Scene& scene, const ScenarioCell& defaults = {});

/// A scenario file in a folder.
struct ScenarioInfo {
    std::string path;
    std::string id;
    std::string name;
    std::vector<std::string> example_ids;
    bool is_default = false;
};
/// The valid scenario files (*.json) of a folder, sorted by file name;
/// files that are not scenarios are skipped.
std::vector<ScenarioInfo> list_scenarios(const std::string& dir);
/// The scenario for a plan's example id (RAD-18): the one listing it, else
/// the default scenario; nullopt when there is neither.
std::optional<ScenarioInfo> select_scenario(const std::vector<ScenarioInfo>& list, const std::string& example_id);

}  // namespace yantra::gen
