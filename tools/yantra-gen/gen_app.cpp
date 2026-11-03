// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-gen (RAD-71). Copied from yantra 68ea20a:
// tools/yantra-gen/gen_app.cpp, adapted for RAD-71 (see gen_app.hpp).

#include "gen_app.hpp"

#include "yantra/gen/channel.hpp"
#include "yantra/gen/downlink.hpp"
#include "yantra/gen/truth_records.hpp"

#include <yantra/radios/record_file.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifndef YANTRA_GEN_VERSION
#define YANTRA_GEN_VERSION "0.0.0"
#endif
#ifndef YANTRA_GEN_INSTALL_SCENARIO_DIR
#define YANTRA_GEN_INSTALL_SCENARIO_DIR ""
#endif

namespace yantra::gentool {

namespace fs = std::filesystem;
using gen::Scenario;
using gen::ScenarioEngine;
using gen::TruthRow;

namespace {

const char* const kUsage = R"(usage: yantra-gen [options] --out FILE|-
       yantra-gen --list | --help | --version

Simulated TETRA downlink I/Q for tests (RAD-71). Runs a scenario file
(format yantra-radios-scenario, data/scenarios) or a network given with
--cell, and writes FILE (samples), STEM.sigmf-meta (SigMF metadata,
"yantra:simulated": true), STEM.truth.csv (ground truth per time and cell),
STEM.cells.csv (what each cell broadcasts) and STEM.records.jsonl (the
ground truth as radio records, RAD-65). Without an extension FILE gets
".sigmf-data". With --out - the samples go to stdout and only the side
files named with --meta/--truth/--cells/--records are written.

Scenario:
  --scenario FILE|ID    scenario file, or the id of a scenario in the
                        scenario folders (default: the default scenario)
  --example ID          the scenario of a bundled plan's example id (RAD-18)
  --scenario-dir DIR    search DIR first (also $YANTRA_SCENARIO_DIR)
  --cell K=V,...        a cell on the command line instead of a scenario
                        (repeat for more); keys: id, carrier (required),
                        band (3), offset (0), la (1), cc (1), number,
                        level (dBm, -70), ppm (0), encryption (0|1),
                        neighbours (ids joined with +)
  --mcc N, --mnc N      network of the --cell cells (901, 1)
  --list                list the scenarios of the scenario folders
Run:
  --duration S          length in seconds (default: the scenario's; 0 with
                        --out - streams without end)
  --rate HZ             sample rate, e.g. 500k or 1M; 50k..20M
  --centre HZ           span centre, e.g. 390.05M (--center, --freq)
  --seed N              random seed; same scenario, options and seed give
                        identical output
  --truth-interval S    ground-truth row interval
  --format F            cf32 (default), cs16, cs8 or cu8
  --full-scale-dbm DBM  integer formats: dBm of a full-scale component;
                        default: the scenario's receiver full scale, else
                        automatic (10 dB above the highest total level)
Impairments (RAD-72; replace the scenario's):
  --noise-figure DB  --no-noise  --ppm PPM (receiver LO)  --clock-ppm PPM
  --gain-db DB  --iq-gain-db DB  --iq-phase-deg DEG  --swap-iq
  --dc-dbfs DBFS  --fading none|rayleigh|rician|static|tu50|ht200
  --rician-k DB  --doppler HZ  --doppler-shift HZ
  --drops FIRST_S,INTERVAL_S,SAMPLES
Output:
  --out FILE|-          data file (required)
  --meta F  --truth F  --cells F  --records F   side-file names
  --no-side-files       samples only
  --quiet               no progress output (stderr)
  --help, --version

Exit codes: 0 done, 1 generation error, 2 usage error, 3 scenario unknown,
unreadable or invalid, 4 output not writable.
Details: docs/YANTRA_GEN.md.
)";

std::string fmt(int decimals, double v)
{
    if (std::isinf(v))
        return v > 0 ? "inf" : "-inf";
    if (std::isnan(v))
        return "nan";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s == "-0" || s.find_first_not_of("-0.") == std::string::npos)
        s.erase(0, s[0] == '-' ? 1 : 0);  // no "-0.00"
    return s;
}

// A file written under a temporary name and renamed into place by commit().
class OutFile {
public:
    explicit OutFile(std::string path) : path_(std::move(path)), tmp_(path_ + ".part")
    {
        f_.open(tmp_, std::ios::binary | std::ios::trunc);
        if (!f_)
            throw OutputError("cannot write " + tmp_ + ": " + std::strerror(errno));
    }
    ~OutFile()
    {
        if (!done_) {
            f_.close();
            std::error_code ec;
            fs::remove(tmp_, ec);
        }
    }
    OutFile(const OutFile&) = delete;
    OutFile& operator=(const OutFile&) = delete;
    std::ostream& stream() { return f_; }
    void write(const void* p, size_t n)
    {
        f_.write(static_cast<const char*>(p), static_cast<std::streamsize>(n));
        check();
    }
    void write(const std::string& s) { write(s.data(), s.size()); }
    void check()
    {
        if (!f_)
            throw OutputError("cannot write " + tmp_ + ": " + std::strerror(errno));
    }
    void commit()
    {
        f_.close();
        if (!f_)
            throw OutputError("cannot write " + tmp_);
        std::error_code ec;
        fs::rename(tmp_, path_, ec);
        if (ec)
            throw OutputError("cannot rename " + tmp_ + " to " + path_ + ": " + ec.message());
        done_ = true;
    }

private:
    std::string path_, tmp_;
    std::ofstream f_;
    bool done_ = false;
};

// The sample sink: a file or stdout.
class DataSink {
public:
    explicit DataSink(const OutputPaths& p)
    {
        if (p.to_stdout) {
#ifdef _WIN32
            _setmode(_fileno(stdout), _O_BINARY);
#endif
        } else {
            file_ = std::make_unique<OutFile>(p.data);
        }
    }
    void write(const void* d, size_t n)
    {
        if (file_) {
            file_->write(d, n);
        } else if (std::fwrite(d, 1, n, stdout) != n) {
            throw OutputError(std::string("cannot write to stdout: ") + std::strerror(errno));
        }
    }
    void commit()
    {
        if (file_)
            file_->commit();
        else if (std::fflush(stdout) != 0)
            throw OutputError("cannot write to stdout");
    }

private:
    std::unique_ptr<OutFile> file_;
};

uint64_t parse_uint(const std::string& opt, const std::string& v, uint64_t max)
{
    char* end = nullptr;
    errno = 0;
    const unsigned long long x = std::strtoull(v.c_str(), &end, 10);
    if (v.empty() || v[0] == '-' || v[0] == '+' || *end != '\0' || errno == ERANGE || x > max)
        throw UsageError(opt + " must be a whole number 0.." + std::to_string(max) + ": " + v);
    return x;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string exe_dir(const std::string& exe_path)
{
    std::error_code ec;
    fs::path p = exe_path;
#ifdef __linux__
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec)
        p = self;
#endif
    if (p.empty())
        return "";
    return p.parent_path().string();
}

}  // namespace

const char* format_name(SampleFormat f)
{
    switch (f) {
    case SampleFormat::cf32: return "cf32";
    case SampleFormat::cs16: return "cs16";
    case SampleFormat::cs8: return "cs8";
    case SampleFormat::cu8: return "cu8";
    }
    return "?";
}

const char* sigmf_datatype(SampleFormat f)
{
    switch (f) {
    case SampleFormat::cf32: return "cf32_le";
    case SampleFormat::cs16: return "ci16_le";
    case SampleFormat::cs8: return "ci8";
    case SampleFormat::cu8: return "cu8";
    }
    return "?";
}

size_t bytes_per_sample(SampleFormat f)
{
    switch (f) {
    case SampleFormat::cf32: return 8;
    case SampleFormat::cs16: return 4;
    case SampleFormat::cs8:
    case SampleFormat::cu8: return 2;
    }
    return 0;
}

const char* usage_text() { return kUsage; }
std::string version_number() { return YANTRA_GEN_VERSION; }
std::string version_text() { return std::string("yantra-gen ") + YANTRA_GEN_VERSION + "\n"; }

std::optional<double> parse_si_number(const std::string& text)
{
    if (text.empty())
        return std::nullopt;
    std::string t = text;
    double mult = 1.0;
    switch (t.back()) {
    case 'k': case 'K': mult = 1e3; t.pop_back(); break;
    case 'M': mult = 1e6; t.pop_back(); break;
    case 'G': case 'g': mult = 1e9; t.pop_back(); break;
    default: break;
    }
    if (t.empty())
        return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(v))
        return std::nullopt;
    return v * mult;
}

CellOption parse_cell_option(const std::string& text, size_t index)
{
    CellOption c;
    c.cell.id = "C" + std::to_string(index + 1);
    c.cell.cell_number = static_cast<uint8_t>(std::min<size_t>(index + 1, 31));
    bool have_carrier = false;
    auto num = [&](const std::string& k, const std::string& v) {
        std::optional<double> d = parse_si_number(v);
        if (!d || std::isalpha(static_cast<unsigned char>(v.back())))
            throw UsageError("--cell " + k + ": not a number: " + v);
        return *d;
    };
    auto whole = [&](const std::string& k, const std::string& v, uint64_t max) {
        return parse_uint("--cell " + k, v, max);
    };
    for (const std::string& kv : split(text, ',')) {
        const size_t eq = kv.find('=');
        if (eq == std::string::npos || eq == 0)
            throw UsageError("--cell: expected key=value, got \"" + kv + "\"");
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        if (k == "id") {
            if (v.empty())
                throw UsageError("--cell id: empty");
            c.cell.id = v;
        } else if (k == "label") {
            c.cell.label = v;
        } else if (k == "carrier") {
            c.cell.main_carrier = static_cast<uint16_t>(whole(k, v, 4095));
            have_carrier = true;
        } else if (k == "band") {
            c.cell.frequency_band = static_cast<uint8_t>(whole(k, v, 15));
        } else if (k == "offset") {
            c.cell.offset = static_cast<uint8_t>(whole(k, v, 3));
        } else if (k == "la") {
            c.cell.location_area = static_cast<uint16_t>(whole(k, v, 16383));
        } else if (k == "cc") {
            c.cell.colour_code = static_cast<uint8_t>(whole(k, v, 63));
        } else if (k == "number") {
            c.cell.cell_number = static_cast<uint8_t>(whole(k, v, 31));
        } else if (k == "level") {
            c.cell.level_dbm = num(k, v);
        } else if (k == "ppm") {
            c.cell.ppm = num(k, v);
        } else if (k == "encryption") {
            c.cell.encryption = whole(k, v, 1) == 1;
        } else if (k == "neighbours") {
            for (const std::string& n : split(v, '+'))
                if (!n.empty())
                    c.neighbours.push_back(n);
        } else {
            throw UsageError("--cell: unknown key \"" + k + "\"");
        }
    }
    if (!have_carrier)
        throw UsageError("--cell: carrier=N is required");
    return c;
}

Options parse_args(const std::vector<std::string>& args)
{
    Options o;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size())
                throw UsageError(a + " needs a value");
            return args[++i];
        };
        auto number = [&](bool si) {
            const std::string& v = value();
            const bool suffix = !v.empty() && std::isalpha(static_cast<unsigned char>(v.back()));
            std::optional<double> d = parse_si_number(v);
            if (!d || (!si && suffix))
                throw UsageError("not a number: " + a + " " + v);
            return *d;
        };
        if (a == "--help" || a == "-h") {
            o.help = true;
        } else if (a == "--version") {
            o.version = true;
        } else if (a == "--list") {
            o.list = true;
        } else if (a == "--quiet" || a == "-q") {
            o.quiet = true;
        } else if (a == "--scenario") {
            o.scenario = value();
            if (o.scenario.empty())
                throw UsageError("--scenario needs a file or id");
        } else if (a == "--example") {
            o.example = value();
            if (o.example.empty())
                throw UsageError("--example needs an example id");
        } else if (a == "--scenario-dir") {
            o.scenario_dirs.push_back(value());
        } else if (a == "--cell") {
            o.cells.push_back(parse_cell_option(value(), o.cells.size()));
        } else if (a == "--mcc") {
            o.mcc = static_cast<uint16_t>(parse_uint(a, value(), 1023));
        } else if (a == "--mnc") {
            o.mnc = static_cast<uint16_t>(parse_uint(a, value(), 16383));
        } else if (a == "--duration") {
            const double d = number(false);
            if (!(d >= 0.0 && d <= 86400.0))
                throw UsageError("--duration must be 0..86400 s");
            o.duration_s = d;
        } else if (a == "--rate") {
            const double r = number(true);
            if (!(r >= 50e3 && r <= 20e6))
                throw UsageError("--rate must be between 50k and 20M");
            o.sample_rate_hz = r;
        } else if (a == "--centre" || a == "--center" || a == "--freq") {
            const double f = number(true);
            if (!(f >= 1e6 && f <= 6e9))
                throw UsageError(a + " must be between 1M and 6G (Hz)");
            o.centre_hz = f;
        } else if (a == "--seed") {
            o.seed = parse_uint(a, value(), static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        } else if (a == "--truth-interval") {
            const double d = number(false);
            if (!(d >= 1e-3 && d <= 3600.0))
                throw UsageError("--truth-interval must be 0.001..3600 s");
            o.truth_interval_s = d;
        } else if (a == "--format") {
            const std::string& v = value();
            if (v == "cf32") o.format = SampleFormat::cf32;
            else if (v == "cs16") o.format = SampleFormat::cs16;
            else if (v == "cs8") o.format = SampleFormat::cs8;
            else if (v == "cu8") o.format = SampleFormat::cu8;
            else throw UsageError("--format must be cf32, cs16, cs8 or cu8: " + v);
        } else if (a == "--full-scale-dbm") {
            const double d = number(false);
            if (d < -150.0 || d > 30.0)
                throw UsageError("--full-scale-dbm must be between -150 and 30");
            o.full_scale_dbm = d;
        } else if (a == "--noise-figure") {
            const double d = number(false);
            if (d < 0.0 || d > 60.0)
                throw UsageError("--noise-figure must be 0..60 dB");
            o.noise_figure_db = d;
        } else if (a == "--no-noise") {
            o.no_noise = true;
        } else if (a == "--ppm") {
            o.rx_ppm = number(false);
        } else if (a == "--clock-ppm") {
            o.clock_ppm = number(false);
        } else if (a == "--gain-db") {
            o.gain_db = number(false);
        } else if (a == "--iq-gain-db") {
            o.iq_gain_db = number(false);
        } else if (a == "--iq-phase-deg") {
            o.iq_phase_deg = number(false);
        } else if (a == "--swap-iq") {
            o.swap_iq = true;
        } else if (a == "--dc-dbfs") {
            o.dc_dbfs = number(false);
        } else if (a == "--fading") {
            o.fading = value();
            try {
                (void)gen::fading_model_from_string(*o.fading);
            } catch (const std::exception&) {
                throw UsageError("--fading must be none, rayleigh, rician, static, tu50 or ht200: " + *o.fading);
            }
        } else if (a == "--rician-k") {
            o.rician_k_db = number(false);
        } else if (a == "--doppler") {
            const double d = number(false);
            if (d < 0.0)
                throw UsageError("--doppler must be >= 0");
            o.doppler_hz = d;
        } else if (a == "--doppler-shift") {
            o.doppler_shift_hz = number(false);
        } else if (a == "--drops") {
            const std::string& v = value();
            const auto parts = split(v, ',');
            if (parts.size() != 3)
                throw UsageError("--drops needs FIRST_S,INTERVAL_S,SAMPLES: " + v);
            auto f = parse_si_number(parts[0]);
            auto in = parse_si_number(parts[1]);
            if (!f || !in || *f < 0.0 || !(*in > 0.0))
                throw UsageError("--drops needs FIRST_S >= 0, INTERVAL_S > 0: " + v);
            o.drops_first_s = *f;
            o.drops_interval_s = *in;
            o.drops_samples = parse_uint("--drops SAMPLES", parts[2], 1u << 30);
        } else if (a == "--out" || a == "-o") {
            o.out = value();
            if (o.out.empty())
                throw UsageError("--out needs a file name or -");
        } else if (a == "--meta") {
            o.meta = value();
        } else if (a == "--truth") {
            o.truth = value();
        } else if (a == "--cells") {
            o.cells_csv = value();
        } else if (a == "--records") {
            o.records = value();
        } else if (a == "--no-side-files") {
            o.no_side_files = true;
        } else {
            throw UsageError("unknown option: " + a);
        }
    }
    if (o.help || o.version || o.list)
        return o;
    if (o.out.empty())
        throw UsageError("--out is required");
    if (o.full_scale_dbm && o.format == SampleFormat::cf32)
        throw UsageError("--full-scale-dbm applies to the integer formats (cs16, cs8, cu8) only");
    if (!o.cells.empty() && (!o.scenario.empty() || !o.example.empty()))
        throw UsageError("--cell cannot be combined with --scenario or --example");
    if (!o.scenario.empty() && !o.example.empty())
        throw UsageError("--scenario and --example exclude each other");
    if ((o.mcc || o.mnc) && o.cells.empty())
        throw UsageError("--mcc and --mnc apply to --cell networks only");
    if (o.duration_s && *o.duration_s == 0.0 && o.out != "-")
        throw UsageError("--duration 0 (endless) needs --out -");
    if (o.no_side_files && (!o.meta.empty() || !o.truth.empty() || !o.cells_csv.empty() || !o.records.empty()))
        throw UsageError("--no-side-files cannot be combined with side-file names");
    return o;
}

std::vector<std::string> scenario_dirs(const Options& o, const std::string& exe_path)
{
    std::vector<std::string> dirs = o.scenario_dirs;
    if (const char* env = std::getenv("YANTRA_SCENARIO_DIR"); env && *env)
        dirs.emplace_back(env);
    const std::string d = exe_dir(exe_path);
    if (!d.empty())
        dirs.push_back((fs::path(d) / ".." / "share" / "yantra-radios" / "scenarios").lexically_normal().string());
    if (*YANTRA_GEN_INSTALL_SCENARIO_DIR)
        dirs.emplace_back(YANTRA_GEN_INSTALL_SCENARIO_DIR);
    // Existing folders only; load_scenario() and --list use the first one
    // with scenario files, so an installed and a build copy never mix.
    std::vector<std::string> out;
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (fs::is_directory(dir, ec) && std::find(out.begin(), out.end(), dir) == out.end())
            out.push_back(dir);
    }
    return out;
}

Scenario load_scenario(const Options& o, const std::vector<std::string>& dirs)
{
    Scenario s;
    try {
        if (!o.cells.empty()) {
            s.id = "command-line";
            s.name = "Command-line network";
            s.description = "Cells given with --cell";
            s.mcc = o.mcc.value_or(901);
            s.mnc = o.mnc.value_or(1);
            for (const CellOption& c : o.cells) {
                gen::ScenarioCell cell = c.cell;
                for (const auto& n : c.neighbours)
                    cell.neighbours.push_back(gen::ScenarioNeighbour{n, std::nullopt, std::nullopt});
                s.cells.push_back(cell);
            }
        } else if (!o.scenario.empty() && fs::is_regular_file(o.scenario)) {
            s = gen::load_scenario_file(o.scenario);
        } else {
            std::optional<gen::ScenarioInfo> pick;
            std::vector<gen::ScenarioInfo> all;
            for (const auto& dir : dirs) {
                all = gen::list_scenarios(dir);
                if (!all.empty())
                    break;
            }
            if (!o.scenario.empty()) {
                for (const auto& i : all)
                    if (i.id == o.scenario)
                        pick = i;
                if (!pick) {
                    std::string ids;
                    for (const auto& i : all)
                        ids += (ids.empty() ? "" : ", ") + i.id;
                    throw ScenarioLoadError("no scenario file or id \"" + o.scenario + "\"" +
                                            (ids.empty() ? std::string(" (no scenario folder found)")
                                                         : " (ids: " + ids + ")"));
                }
            } else {
                pick = gen::select_scenario(all, o.example);
                if (!pick)
                    throw ScenarioLoadError(all.empty() ? "no scenario folder found (use --scenario FILE)"
                                                        : "no scenario for example id \"" + o.example +
                                                              "\" and no default scenario");
            }
            s = gen::load_scenario_file(pick->path);
        }
        if (o.duration_s) s.duration_s = *o.duration_s;
        if (o.sample_rate_hz) s.sample_rate_hz = *o.sample_rate_hz;
        if (o.centre_hz) s.centre_frequency_hz = *o.centre_hz;
        if (o.seed) s.seed = *o.seed;
        if (o.truth_interval_s) s.truth_interval_s = *o.truth_interval_s;
        gen::ReceiverSpec& rx = s.receiver;
        if (o.noise_figure_db) rx.noise_figure_db = *o.noise_figure_db;
        if (o.no_noise) rx.noise = false;
        if (o.rx_ppm) rx.ppm = *o.rx_ppm;
        if (o.clock_ppm) rx.clock_ppm = *o.clock_ppm;
        if (o.gain_db) rx.gain_db = *o.gain_db;
        if (o.iq_gain_db) rx.iq_gain_db = *o.iq_gain_db;
        if (o.iq_phase_deg) rx.iq_phase_deg = *o.iq_phase_deg;
        if (o.swap_iq) rx.swap_iq = true;
        if (o.dc_dbfs) rx.dc_dbfs = *o.dc_dbfs;
        if (o.drops_interval_s) {
            rx.drops_first_s = *o.drops_first_s;
            rx.drops_interval_s = *o.drops_interval_s;
            rx.drops_samples = *o.drops_samples;
        }
        if (o.fading) s.channel.fading = gen::fading_model_from_string(*o.fading);
        if (o.rician_k_db) s.channel.rician_k_db = *o.rician_k_db;
        if (o.doppler_hz) s.channel.doppler_hz = *o.doppler_hz;
        if (o.doppler_shift_hz) s.channel.doppler_shift_hz = *o.doppler_shift_hz;
        gen::validate_scenario(s);
    } catch (const ScenarioLoadError&) {
        throw;
    } catch (const std::exception& e) {
        const std::string what = !o.cells.empty() ? std::string("command-line cells")
                                 : !o.scenario.empty() ? "scenario \"" + o.scenario + "\""
                                                       : std::string("scenario");
        throw ScenarioLoadError(what + ": " + e.what());
    }
    return s;
}

OutputPaths output_paths(const Options& o)
{
    OutputPaths r;
    if (o.out == "-") {
        r.to_stdout = true;
        if (!o.no_side_files) {
            r.meta = o.meta;
            r.truth = o.truth;
            r.cells = o.cells_csv;
            r.records = o.records;
        }
        return r;
    }
    fs::path p(o.out);
    if (!p.has_extension())
        p += ".sigmf-data";
    r.data = p.string();
    if (o.no_side_files)
        return r;
    fs::path stem = p;
    stem.replace_extension();
    r.meta = o.meta.empty() ? stem.string() + ".sigmf-meta" : o.meta;
    r.truth = o.truth.empty() ? stem.string() + ".truth.csv" : o.truth;
    r.cells = o.cells_csv.empty() ? stem.string() + ".cells.csv" : o.cells_csv;
    r.records = o.records.empty() ? stem.string() + ".records.jsonl" : o.records;
    return r;
}

double auto_full_scale_dbm(const ScenarioEngine& e)
{
    const Scenario& s = e.scenario();
    const gen::ReceiverSpec& rx = s.receiver;
    const double ref = rx.reference_level_dbm;
    // The front end's DC offset per component (engine units), as the
    // engine adds it: relative to the receiver's full scale.
    double dc = 0.0;
    if (rx.dc_dbfs)
        dc = std::sqrt(gen::dbm_to_power(rx.full_scale_dbm.value_or(ref), ref)) * std::pow(10.0, *rx.dc_dbfs / 20.0) /
             std::sqrt(2.0);
    const double headroom = std::pow(10.0, kAutoHeadroomDb / 20.0);
    const double span = s.duration_s > 0.0 ? s.duration_s : 10.0;
    const double step = std::max(1e-3, span / 20000.0);
    const uint64_t steps = static_cast<uint64_t>(std::ceil(span / step));
    double peak = 0.0;
    for (uint64_t k = 0; k <= steps; ++k) {
        const TruthRow r = e.truth_at(std::min(static_cast<double>(k) * step, span));
        double p = e.noise_power();
        for (const auto& c : r.cells)
            if (c.on_air && std::isfinite(c.level_inst_dbm))
                p += gen::dbm_to_power(c.level_inst_dbm, ref);
        p *= std::pow(10.0, r.rx_gain_db / 10.0);
        peak = std::max(peak, dc + std::sqrt(p) * headroom);
    }
    if (!(peak > 0.0))
        return ref;
    return std::ceil(ref + 20.0 * std::log10(peak));
}

SampleConverter::SampleConverter(SampleFormat f, double full_scale_amplitude) : f_(f)
{
    double top = 1.0;
    switch (f) {
    case SampleFormat::cf32: top = 1.0; break;
    case SampleFormat::cs16: top = 32767.0; break;
    case SampleFormat::cs8: top = 127.0; break;
    case SampleFormat::cu8: top = 127.5; break;
    }
    gain_ = f == SampleFormat::cf32 ? 1.0 : top / full_scale_amplitude;
}

void SampleConverter::convert(const std::complex<float>* in, size_t n, uint8_t* out)
{
    if (f_ == SampleFormat::cf32) {
        std::memcpy(out, in, n * sizeof(std::complex<float>));
        return;
    }
    double lo = 0.0, hi = 0.0, off = 0.0;
    switch (f_) {
    case SampleFormat::cs16: lo = -32767.0; hi = 32767.0; break;
    case SampleFormat::cs8: lo = -127.0; hi = 127.0; break;
    case SampleFormat::cu8: lo = 0.0; hi = 255.0; off = 127.5; break;
    case SampleFormat::cf32: break;
    }
    for (size_t i = 0; i < n; ++i) {
        double c[2] = {std::nearbyint(off + static_cast<double>(in[i].real()) * gain_),
                       std::nearbyint(off + static_cast<double>(in[i].imag()) * gain_)};
        bool clip = false;
        for (double& v : c) {
            if (v > hi) { v = hi; clip = true; }
            if (v < lo) { v = lo; clip = true; }
        }
        clipped_ += clip ? 1 : 0;
        switch (f_) {
        case SampleFormat::cs16:
            for (int k = 0; k < 2; ++k) {
                const auto u = static_cast<uint16_t>(static_cast<int16_t>(c[k]));
                out[4 * i + 2 * k] = static_cast<uint8_t>(u & 0xff);  // little endian
                out[4 * i + 2 * k + 1] = static_cast<uint8_t>(u >> 8);
            }
            break;
        case SampleFormat::cs8:
            out[2 * i] = static_cast<uint8_t>(static_cast<int8_t>(c[0]));
            out[2 * i + 1] = static_cast<uint8_t>(static_cast<int8_t>(c[1]));
            break;
        case SampleFormat::cu8:
            out[2 * i] = static_cast<uint8_t>(c[0]);
            out[2 * i + 1] = static_cast<uint8_t>(c[1]);
            break;
        case SampleFormat::cf32: break;
        }
    }
}

std::string truth_csv_header()
{
    return "t_s,sample,output_sample,floor,x_m,y_m,noise_dbm,rx_gain_db,cell,mcc,mnc,la,colour_code,"
           "frequency_hz,offset_hz,cfo_hz,on_air,source,level_dbm,level_inst_dbm,snr_db,best_antenna,walls\n";
}

std::string truth_csv_rows(const TruthRow& row)
{
    std::string head = fmt(6, row.t) + "," + std::to_string(row.sample) + "," + std::to_string(row.output_sample) + ",";
    if (row.position)
        head += row.position->floor + "," + fmt(3, row.position->x_m) + "," + fmt(3, row.position->y_m) + ",";
    else
        head += ",,,";
    head += fmt(2, row.noise_dbm) + "," + fmt(2, row.rx_gain_db) + ",";
    std::string out;
    for (const auto& c : row.cells) {
        out += head + c.cell + "," + std::to_string(c.mcc) + "," + std::to_string(c.mnc) + "," +
               std::to_string(c.location_area) + "," + std::to_string(c.colour_code) + "," + fmt(1, c.frequency_hz) +
               "," + fmt(1, c.offset_hz) + "," + fmt(3, c.cfo_hz) + "," + (c.on_air ? "1" : "0") + "," +
               gen::to_string(c.source) + "," + fmt(2, c.level_dbm) + "," + fmt(2, c.level_inst_dbm) + "," +
               fmt(2, c.snr_db) + "," + c.best_antenna + "," + std::to_string(c.walls) + "\n";
    }
    return out;
}

std::string cells_csv(const Scenario& s)
{
    std::string out =
        "cell,label,mcc,mnc,la,colour_code,cell_number,main_carrier,frequency_band,offset,frequency_hz,"
        "offset_hz,encryption,on_air,neighbours\n";
    const double centre = gen::centre_frequency_hz(s);
    for (const auto& c : s.cells) {
        // Neighbours as broadcast: id:carrier[:LA], planted values included.
        std::string nb;
        for (const auto& n : c.neighbours) {
            const gen::ScenarioCell* nc = s.find_cell(n.cell);
            const uint16_t carrier = n.main_carrier.value_or(nc ? nc->main_carrier : 0);
            nb += (nb.empty() ? "" : " ") + n.cell + ":" + std::to_string(carrier);
            if (n.location_area)
                nb += ":LA" + std::to_string(*n.location_area);
        }
        std::string label = c.label;
        std::replace(label.begin(), label.end(), ',', ';');
        const double f = gen::cell_frequency_hz(c);
        out += c.id + "," + label + "," + std::to_string(s.mcc) + "," + std::to_string(s.mnc) + "," +
               std::to_string(c.location_area) + "," + std::to_string(c.colour_code) + "," +
               std::to_string(c.cell_number) + "," + std::to_string(c.main_carrier) + "," +
               std::to_string(c.frequency_band) + "," + std::to_string(c.offset) + "," + fmt(1, f) + "," +
               fmt(1, f - centre) + "," + (c.encryption ? "1" : "0") + "," + (c.on_air ? "1" : "0") + "," + nb +
               "\n";
    }
    return out;
}

nlohmann::json sigmf_meta(const ScenarioEngine& e, SampleFormat format, const RunStats& stats,
                          const OutputPaths& paths)
{
    using nlohmann::json;
    const Scenario& s = e.scenario();
    auto file_name = [](const std::string& p) { return p.empty() ? json(nullptr) : json(fs::path(p).filename().string()); };
    json g = json::object();
    g["core:datatype"] = sigmf_datatype(format);
    g["core:sample_rate"] = s.sample_rate_hz;
    g["core:version"] = "1.2.0";
    g["core:num_channels"] = 1;
    g["core:description"] = "Simulated TETRA downlink, scenario " + s.id + " (no real signal)";
    g["core:author"] = "yantra-gen";
    g["core:recorder"] = "yantra-gen " + version_number();
    g["core:hw"] = "none (simulated by yantra-gen)";
    g["core:extensions"] = json::array({json{{"name", "yantra"}, {"version", "1.0.0"}, {"optional", true}}});
    g["yantra:simulated"] = true;
    g["yantra:generator_version"] = version_number();
    g["yantra:scenario_id"] = s.id;
    g["yantra:scenario_name"] = s.name;
    g["yantra:scenario_format"] = std::string(gen::kScenarioFormat) + "/" + std::to_string(gen::kScenarioVersion);
    g["yantra:seed"] = s.seed;
    g["yantra:duration_s"] = s.duration_s;
    g["yantra:reference_level_dbm"] = s.receiver.reference_level_dbm;
    if (stats.full_scale_dbm)
        g["yantra:full_scale_dbm"] = *stats.full_scale_dbm;
    g["yantra:clipped_samples"] = stats.clipped;
    g["yantra:dropped_samples"] = stats.dropped;
    g["yantra:true_sample_rate"] = e.true_sample_rate();
    g["yantra:truth_file"] = file_name(paths.truth);
    g["yantra:cells_file"] = file_name(paths.cells);
    g["yantra:records_file"] = file_name(paths.records);
    g["yantra:planted"] = s.planted;

    // Captures: one segment, then one after every drop (core:global_index =
    // signal sample index the segment continues at).
    json caps = json::array();
    caps.push_back(json{{"core:sample_start", 0}, {"core:global_index", 0}, {"core:frequency", e.centre_frequency_hz()}});
    const gen::ReceiverSpec& rx = s.receiver;
    gen::DropSpec ds;
    if (rx.drops_interval_s > 0.0 && rx.drops_samples > 0) {
        ds.first = static_cast<uint64_t>(std::llround(rx.drops_first_s * e.true_sample_rate()));
        ds.interval = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(rx.drops_interval_s * e.true_sample_rate())));
        ds.length = rx.drops_samples;
    }
    const gen::SampleDropper drops(ds);
    const uint64_t signal_total = stats.samples + stats.dropped;
    if (ds.interval > 0) {
        for (uint64_t start = ds.first; start < signal_total; start += ds.interval) {
            const uint64_t resume = start + ds.length;
            if (resume >= signal_total)
                break;
            caps.push_back(json{{"core:sample_start", resume - drops.dropped_before(resume)},
                                {"core:global_index", resume},
                                {"core:frequency", e.centre_frequency_hz()},
                                {"yantra:dropped_before", ds.length}});
        }
    }

    // Annotations: every cell over the whole recording, then the events.
    json ann = json::array();
    for (size_t i = 0; i < e.cells(); ++i) {
        const gen::ScenarioCell& c = e.cell(i);
        const double f = gen::cell_frequency_hz(c);
        json a{{"core:sample_start", 0},
               {"core:freq_lower_edge", f - 12500.0},
               {"core:freq_upper_edge", f + 12500.0},
               {"core:label", c.id},
               {"core:comment", "TETRA cell MCC " + std::to_string(s.mcc) + " MNC " + std::to_string(s.mnc) + " LA " +
                                    std::to_string(c.location_area) + " CC " + std::to_string(c.colour_code) +
                                    " carrier " + std::to_string(c.main_carrier)},
               {"yantra:cell", c.id},
               {"yantra:mcc", s.mcc},
               {"yantra:mnc", s.mnc},
               {"yantra:la", c.location_area},
               {"yantra:colour_code", c.colour_code},
               {"yantra:main_carrier", c.main_carrier},
               {"yantra:encryption", c.encryption},
               {"yantra:cfo_hz", e.cell_cfo_hz(i)}};
        if (stats.samples > 0)
            a["core:sample_count"] = stats.samples;
        ann.push_back(a);
    }
    for (const gen::ScenarioEvent& ev : s.events) {
        const uint64_t sig = static_cast<uint64_t>(std::llround(ev.t * e.true_sample_rate()));
        if (s.duration_s > 0.0 && sig >= signal_total)
            continue;
        const uint64_t out_index = sig - drops.dropped_before(sig);
        std::string label;
        switch (ev.type) {
        case gen::EventType::cell_off: label = "cell_off " + ev.cell; break;
        case gen::EventType::cell_on: label = "cell_on " + ev.cell; break;
        case gen::EventType::cell_level: label = "cell_level " + ev.cell + " " + fmt(1, ev.db) + " dB"; break;
        case gen::EventType::rx_gain: label = "rx_gain " + fmt(1, ev.db) + " dB"; break;
        }
        json a{{"core:sample_start", out_index}, {"core:label", label}, {"yantra:event_t_s", ev.t}};
        ann.push_back(a);
    }
    std::stable_sort(ann.begin(), ann.end(), [](const json& a, const json& b) {
        return a["core:sample_start"].get<uint64_t>() < b["core:sample_start"].get<uint64_t>();
    });
    json m = json::object();
    m["global"] = g;
    m["captures"] = caps;
    m["annotations"] = ann;
    return m;
}

RunStats generate(const Scenario& s, const Options& o, std::ostream* log)
{
    const OutputPaths paths = output_paths(o);
    ScenarioEngine e(s);
    RunStats st;
    const double ref = s.receiver.reference_level_dbm;
    double amplitude = 1.0;
    if (o.format != SampleFormat::cf32) {
        st.full_scale_dbm = o.full_scale_dbm ? *o.full_scale_dbm
                            : s.receiver.full_scale_dbm ? *s.receiver.full_scale_dbm
                                                        : auto_full_scale_dbm(e);
        amplitude = std::sqrt(gen::dbm_to_power(*st.full_scale_dbm, ref));
    }
    SampleConverter conv(o.format, amplitude);
    if (log)
        *log << "yantra-gen: scenario " << s.id << ", "
             << (s.duration_s > 0.0 ? fmt(3, s.duration_s) + " s" : std::string("endless")) << " at "
             << fmt(1, s.sample_rate_hz / 1e3) << " ksps, centre " << fmt(4, e.centre_frequency_hz() / 1e6)
             << " MHz, seed " << s.seed << ", " << format_name(o.format)
             << (st.full_scale_dbm ? ", full scale " + fmt(0, *st.full_scale_dbm) + " dBm" : std::string()) << "\n";

    DataSink data(paths);
    std::unique_ptr<OutFile> truth, records;
    std::unique_ptr<radios::RecordFileWriter> writer;
    gen::TruthRecorder recorder(s);
    if (!paths.truth.empty()) {
        truth = std::make_unique<OutFile>(paths.truth);
        truth->write(truth_csv_header());
    }
    if (!paths.records.empty()) {
        records = std::make_unique<OutFile>(paths.records);
        writer = std::make_unique<radios::RecordFileWriter>(records->stream());
        writer->write_header(gen::truth_record_header(s, version_number()));
        records->check();
    }
    auto take_truth = [&]() {
        for (const auto& row : e.take_truth()) {
            ++st.truth_rows;
            if (truth)
                truth->write(truth_csv_rows(row));
            if (writer) {
                gen::TruthRecords tr = recorder.convert(row);
                if (tr.serving_changed_hz) {
                    radios::Event ev;
                    ev.event = YR_EVENT_SERVING_CHANGED;
                    ev.value = static_cast<int64_t>(*tr.serving_changed_hz);
                    ev.monotonic_ns = gen::TruthRecorder::monotonic_ns(row);
                    writer->write_event(ev);
                }
                for (const auto& r : tr.records)
                    writer->write_record(r);
                records->check();
            }
        }
    };

    constexpr size_t kBlock = 65536;
    std::vector<std::complex<float>> buf(kBlock);
    std::vector<uint8_t> bytes(kBlock * bytes_per_sample(o.format));
    int last_pct = -1;
    while (!e.done()) {
        const size_t n = e.read(buf.data(), buf.size());
        take_truth();
        if (n == 0) {
            if (e.done())
                break;
            continue;  // a whole block dropped
        }
        conv.convert(buf.data(), n, bytes.data());
        data.write(bytes.data(), n * bytes_per_sample(o.format));
        st.samples += n;
        if (log && e.total_samples() > 0) {
            const int pct = static_cast<int>(100 * e.position() / e.total_samples());
            if (pct / 10 != last_pct / 10) {
                *log << "  " << pct << " %\n";
                last_pct = pct;
            }
        }
    }
    take_truth();
    st.clipped = conv.clipped();
    st.dropped = e.dropped();
    if (writer) {
        radios::Event end;
        end.event = YR_EVENT_END_OF_STREAM;
        end.value = 0;
        end.monotonic_ns = static_cast<int64_t>(std::llround(static_cast<double>(e.position()) / e.true_sample_rate() * 1e9));
        writer->write_event(end);
        radios::EndSummary sum;
        sum.records = writer->records();
        sum.events = writer->events() + 0;
        sum.dropped = 0;
        st.records = writer->records();
        writer->write_end(sum);
        records->check();
    }
    std::unique_ptr<OutFile> cells, meta;
    if (!paths.cells.empty()) {
        cells = std::make_unique<OutFile>(paths.cells);
        cells->write(cells_csv(s));
    }
    if (!paths.meta.empty()) {
        meta = std::make_unique<OutFile>(paths.meta);
        meta->write(sigmf_meta(e, o.format, st, paths).dump(2) + "\n");
    }
    data.commit();
    if (truth) truth->commit();
    if (records) records->commit();
    if (cells) cells->commit();
    if (meta) meta->commit();
    if (log)
        *log << "wrote " << (paths.to_stdout ? std::string("stdout") : paths.data) << " (" << st.samples
             << " samples, " << st.truth_rows << " truth rows, " << st.records << " records)\n";
    return st;
}

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err, const std::string& exe_path)
{
    Options o;
    try {
        o = parse_args(args);
    } catch (const UsageError& e) {
        err << "yantra-gen: error: " << e.what() << "\n(yantra-gen --help shows the options)\n";
        return kExitUsage;
    }
    if (o.help) {
        out << usage_text();
        return kExitOk;
    }
    if (o.version) {
        out << version_text();
        return kExitOk;
    }
    const std::vector<std::string> dirs = scenario_dirs(o, exe_path);
    if (o.list) {
        for (const auto& dir : dirs) {
            const auto all = gen::list_scenarios(dir);
            if (all.empty())
                continue;
            out << "# " << dir << "\n";
            for (const auto& i : all) {
                std::string ex;
                for (const auto& x : i.example_ids)
                    ex += (ex.empty() ? "" : " ") + x;
                out << i.id << "\t" << i.name << (i.is_default ? "\t(default)" : "\t") << "\t" << ex << "\n";
            }
            return kExitOk;
        }
        err << "yantra-gen: error: no scenario folder found\n";
        return kExitScenario;
    }
    Scenario s;
    try {
        s = load_scenario(o, dirs);
        if (s.duration_s == 0.0 && o.out != "-")
            throw ScenarioLoadError("scenario \"" + s.id + "\" is endless (duration 0): give --duration or --out -");
    } catch (const ScenarioLoadError& e) {
        err << "yantra-gen: error: " << e.what() << "\n";
        return kExitScenario;
    }
    try {
        const RunStats st = generate(s, o, o.quiet ? nullptr : &err);
        if (st.clipped > 0)
            err << "yantra-gen: warning: " << st.clipped << " samples clipped (full scale "
                << fmt(0, st.full_scale_dbm.value_or(0.0)) << " dBm); raise --full-scale-dbm\n";
    } catch (const OutputError& e) {
        err << "yantra-gen: error: " << e.what() << "\n";
        return kExitOutput;
    } catch (const std::exception& e) {
        err << "yantra-gen: error: " << e.what() << "\n";
        return kExitError;
    }
    return kExitOk;
}

}  // namespace yantra::gentool
