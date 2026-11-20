// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-gen, the TETRA test-signal generator command (RAD-71): runs a
// scenario file (RAD-74; data/scenarios) or a network given on the
// command line through the scenario engine (RAD-72, RAD-73) and writes
// the receiver samples as cf32, cs16, cs8 or cu8 with SigMF metadata
// ("yantra:simulated": true), a ground-truth CSV, a cell CSV and a
// ground-truth record file (RAD-65). The pieces are a library so the tests
// can check them without starting a process. Usage and file formats:
// docs/YANTRA_GEN.md.
// Copied from yantra 68ea20a: tools/yantra-gen/gen_app.h, adapted for
// RAD-71 (scenario files of RAD-74, four sample formats, record files,
// command-line cells, impairment options, stdout streaming).
#pragma once

#include "yantra/gen/scenario.hpp"
#include "yantra/gen/scenario_engine.hpp"

#include <nlohmann/json.hpp>

#include <complex>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace yantra::gentool {

/// Exit codes of yantra-gen.
inline constexpr int kExitOk = 0;
inline constexpr int kExitError = 1;     ///< generation failed (internal error)
inline constexpr int kExitUsage = 2;     ///< bad command line
inline constexpr int kExitScenario = 3;  ///< scenario unknown, unreadable or invalid
inline constexpr int kExitOutput = 4;    ///< an output file could not be written

enum class SampleFormat { cf32, cs16, cs8, cu8 };
const char* format_name(SampleFormat f);
/// SigMF core:datatype: cf32_le, ci16_le, ci8, cu8.
const char* sigmf_datatype(SampleFormat f);
size_t bytes_per_sample(SampleFormat f);

/// Headroom of the automatic integer full scale above the highest total
/// instantaneous level of the run (peaks of pi/4-DQPSK, several carriers
/// and noise adding up in one component).
inline constexpr double kAutoHeadroomDb = 10.0;

/// One --cell option: comma-separated key=value pairs.
struct CellOption {
    gen::ScenarioCell cell;
    std::vector<std::string> neighbours;  ///< cell ids
};

struct Options {
    std::string scenario;  ///< file path or scenario id; "" = default
    std::string example;   ///< --example: scenario of a RAD-18 example id
    std::vector<CellOption> cells;  ///< command-line network instead of a scenario
    std::optional<uint16_t> mcc, mnc;
    std::optional<double> duration_s;
    std::optional<double> sample_rate_hz;
    std::optional<double> centre_hz;
    std::optional<uint64_t> seed;
    std::optional<double> truth_interval_s;
    SampleFormat format = SampleFormat::cf32;
    std::optional<double> full_scale_dbm;
    // Impairments (RAD-72) replacing the scenario's.
    std::optional<double> noise_figure_db;
    bool no_noise = false;
    std::optional<double> rx_ppm, clock_ppm, gain_db, iq_gain_db, iq_phase_deg, dc_dbfs;
    bool swap_iq = false;
    std::optional<std::string> fading;
    std::optional<double> rician_k_db, doppler_hz, doppler_shift_hz;
    std::optional<double> drops_first_s, drops_interval_s;
    std::optional<uint64_t> drops_samples;
    // Outputs.
    std::string out;  ///< data file, "-" for stdout
    std::string meta, truth, cells_csv, records;  ///< explicit side-file names
    bool no_side_files = false;
    bool list = false;
    bool help = false;
    bool version = false;
    bool quiet = false;
    std::vector<std::string> scenario_dirs;  ///< --scenario-dir (searched first)
};

class UsageError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
class ScenarioLoadError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
class OutputError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

const char* usage_text();
std::string version_text();
std::string version_number();

/// Parses the arguments (without the program name); UsageError on any
/// problem. Frequencies and rates take an optional k/M/G suffix.
Options parse_args(const std::vector<std::string>& args);
/// A number with an optional k/M/G suffix; nullopt if it is not one.
std::optional<double> parse_si_number(const std::string& text);
/// One --cell value, e.g. "id=A,carrier=3600,la=10,cc=5,level=-70".
CellOption parse_cell_option(const std::string& text, size_t index);

/// The scenario of the options: a file, a scenario id or example id from
/// the scenario folders, the default scenario, or the command-line cells;
/// overrides applied and validated. ScenarioLoadError on any problem.
gen::Scenario load_scenario(const Options& o, const std::vector<std::string>& dirs);

/// File names of one run. Without an extension the data file gets
/// ".sigmf-data"; the side files replace the extension: STEM.sigmf-meta,
/// STEM.truth.csv, STEM.cells.csv, STEM.records.jsonl. With "-" (stdout)
/// only explicitly named side files are written.
struct OutputPaths {
    std::string data, meta, truth, cells, records;  ///< "" = not written
    bool to_stdout = false;
};
OutputPaths output_paths(const Options& o);

/// Automatic full scale (dBm of a full-scale component): the highest
/// total instantaneous level (all cells, noise, receiver gain) on a 1 ms
/// grid (at most 20000 points over the run, the first 10 s of an endless
/// one) plus kAutoHeadroomDb, as an amplitude plus the receiver's DC
/// offset, rounded up to a whole dB.
double auto_full_scale_dbm(const gen::ScenarioEngine& e);

/// Converts engine samples (power 1 = reference level) to an integer
/// format: a component of amplitude `full_scale_amplitude` maps to the
/// largest code (cs16 32767, cs8 127, cu8 255 = 127.5 + 127.5); beyond it
/// values are clipped (and counted). cu8: code = round(127.5 + 127.5 x/A).
class SampleConverter {
public:
    SampleConverter(SampleFormat f, double full_scale_amplitude);
    /// Writes n samples to `out` (bytes_per_sample(f) * n bytes).
    void convert(const std::complex<float>* in, size_t n, uint8_t* out);
    uint64_t clipped() const { return clipped_; }

private:
    SampleFormat f_;
    double gain_ = 1.0;
    uint64_t clipped_ = 0;
};

/// Statistics of a finished run (also in the SigMF metadata).
struct RunStats {
    uint64_t samples = 0;  ///< output samples
    uint64_t dropped = 0;
    std::optional<double> full_scale_dbm;  ///< integer formats
    uint64_t clipped = 0;  ///< samples with a clipped component
    size_t truth_rows = 0;
    uint64_t records = 0;
};

/// Ground-truth CSV (one row per time and cell) and cell CSV.
std::string truth_csv_header();
std::string truth_csv_rows(const gen::TruthRow& row);
std::string cells_csv(const gen::Scenario& s);

/// SigMF metadata of a run: global (core fields + yantra:*), captures
/// (one segment, plus one after every sample drop with core:global_index
/// the signal sample index), annotations (one per cell over the whole
/// recording with its channel edges; one per event).
nlohmann::json sigmf_meta(const gen::ScenarioEngine& e, SampleFormat format, const RunStats& stats,
                          const OutputPaths& paths);

/// Runs the scenario and writes all outputs (files via a temporary name
/// renamed at the end). Progress lines go to `log` when not null.
RunStats generate(const gen::Scenario& s, const Options& o, std::ostream* log);

/// The scenario folders searched: --scenario-dir, $YANTRA_SCENARIO_DIR,
/// <exe>/../share/yantra-radios/scenarios (also in the build tree), the
/// install folder.
std::vector<std::string> scenario_dirs(const Options& o, const std::string& exe_path);

/// The whole command: returns the exit code. `exe_path` locates the
/// installed scenarios.
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
        const std::string& exe_path = "");

}  // namespace yantra::gentool
