// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-radio-host: the radio test host CLI (RAD-91). Loads a radio
// plugin, lists its devices, settings, presets and sensors, runs its
// self-test, streams records to a record file (RAD-65) with emulated host
// callbacks for SCENE plugins (RAD-18), and checks record files. See kUsage.
// The command layout follows yantra 68ea20a tools/yantra-plugin-test/main.cpp.
#include "conformance.hpp"
#include "host.hpp"
#ifdef YANTRA_HOST_DECODE_IQ
#include "decode_iq.hpp"
#endif
#ifdef YANTRA_HOST_CAPTURE_IQ
#include "capture_iq.hpp"

#include <yantra/plugins/soapy_plugin.hpp>
#endif

#include <yantra/radio.hpp>

#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace yantra::radio_host;
namespace radios = yantra::radios;

constexpr int kExitPass = 0;
constexpr int kExitFail = 1;
constexpr int kExitUsage = 2;

const char* const kUsage = R"(usage: yantra-radio-host COMMAND [options]

Commands:
  info      --plugin FILE
      Loads the plugin, checks the radio API version (RAD-11) and the
      descriptor, and prints it with its record schemas.
  devices   --plugin FILE
      Lists the devices (RAD-30), or the plugin's "no device" diagnostic.
  settings  --plugin FILE [--device KEY] [--set KEY=VALUE]... [--preset ID]
            [--action KEY]...
      Opens a device, applies the --set, --preset and --action options in
      that order, then prints the settings with their values, the presets
      and the sensors with their values (RAD-40..45).
  selftest  --plugin FILE [--device KEY]
      Runs the self-test (RAD-46).
  stream    --plugin FILE [--device KEY] [plan] [limits] [output] [context]
            [--set KEY=VALUE]... [--preset ID]
      Streams records and events into a record file (RAD-65). Every record
      is checked against its schema (RAD-21); pushes after stop are counted.
  conformance --plugin FILE [--device KEY] [--scene FILE]... [--report FILE]
            [--duration S] [--unplug CMD --replug CMD]
      Runs the conformance kit (RAD-91): every check of the radio API rules
      (RAD-10..49) it can, each reported as pass, fail or skip. --report
      writes the JSON report ("-" = standard output); --duration sets the
      records check's stream length (default 2.5 s); --unplug and --replug
      are shell commands that unplug and replug the device (HOTPLUG).
  read      FILE
      Checks a record file (RAD-65) and prints a summary.
  decode-iq --iq FILE --format cs8|cu8|cs16|cf32|sigmf [--rate HZ]
            [--center HZ] [--serving [HZ] | --carriers HZ[,HZ...]]
            [--scan-records] [--threads N] [--full-scale-dbm DBM]
            [--uncalibrated] [--usable-fraction F] [--gsmtap ADDR]
            [--gsmtap-port PORT] [--out FILE] [--quiet]
      Decodes an IQ file (RAD-63) with the TETRA receiver (the measurement
      engine of the TETRA plugins) into a record file and prints a summary.
      Raw files need --rate and --center; SigMF gives them (core:datatype,
      core:sample_rate, captures) and yantra-gen's calibration
      (yantra:full_scale_dbm, yantra:reference_level_dbm): levels in dBm,
      else dBFS. --full-scale-dbm sets the dBm of full scale;
      --usable-fraction the share of the capture searched (default 0.9).
      --gsmtap sends the decoded broadcast blocks as GSMTAP over UDP to a
      numeric address (port 4729 or --gsmtap-port) for Wireshark (RAD-115).
  capture-iq --out FILE.sigmf-data [--device KEY] [--format cs16|cs8|cu8|cf32]
             [--rate HZ] [--center HZ] [--duration S] [--rolling S]
             [--gain DB | --gain NAME=DB ...] [--antenna NAME] [--bandwidth HZ]
             [--ppm PPM]
      IQ capture of an SDR to SigMF (RAD-63) through the SoapySDR modules
      built into the host (those of the SDR plugins), for receiver
      development; decode-iq reads it back. Defaults: the first device,
      cs16, 2 Msps at 392.5 MHz, 10 s. --rolling keeps only the last S
      seconds (written at the end or on Ctrl+C); --gain sets the overall
      gain or one gain element (repeatable). IQ stays in this tool: it
      never crosses the radio API.

Plan (stream, default --serving):
  --serving [HZ]            follow the strongest (or the given) carrier
  --carriers HZ[,HZ...]     measure these carriers
  --scan FIRST:LAST[:STEP]  sweep a band (SCAN plugins)
  --neighbours              also measure the announced neighbours
Limits (stream; without them it runs until the stream ends or Ctrl+C):
  --records N               stop after N records
  --duration S              stop after S seconds
Output (stream):
  --out FILE                record file, "-" = standard output (default)
  --quiet                   write no record file
  --sensors                 read the sensors after the stream
Context, emulated Yantra for SCENE plugins (RAD-18):
  --scene FILE              scene JSON of a floor (repeatable); shows a plan
  --floor ID                floor shown (default: the first scene's)
  --plan-name TEXT          name of the plan shown (shows a plan)
  --example-id ID           bundled example plan id
  --no-plan                 no plan shown (NO_PLAN), even with --scene
  --no-scene                the floors have no scene (get_scene NOT_FOUND:
                            "no scene, use the defaults"); shows a plan
  --cursor X,Y[;X,Y...]     mouse position, or a path of positions
  --walk X,Y[;X,Y...]       walk-test position, or a path
  --path-step N             records between two path points (0: stay)
Logging:
  --log-level debug|info|warning|error   plugin log lines to stderr (default warning)

Exit codes: 0 pass, 1 plugin failure or problems found, 2 usage or file error.
)";

std::atomic<bool> g_interrupt{false};

extern "C" void on_signal(int)
{
    g_interrupt.store(true);
}

struct Options {
    std::string command;
    std::string plugin;
    std::string device;
    std::vector<std::pair<std::string, std::string>> sets;
    std::string preset;
    std::vector<std::string> actions;
    yr_plan plan{};
    std::vector<uint64_t> carriers;
    uint64_t max_records = 0;
    double duration = 0.0;
    std::string out = "-";
    bool quiet = false;
    bool sensors = false;
    bool no_plan = false;
    ContextOptions context;
    uint32_t log_level = YR_LOG_WARNING;
    std::string file; // read
    std::string iq, iq_format;
    std::optional<double> iq_rate, iq_centre, full_scale_dbm;
    unsigned threads = 1;
    double usable_fraction = 0.9;
    std::string gsmtap_host;
    uint16_t gsmtap_port = 4729;
    bool uncalibrated = false;
    bool scan_records = false;
    std::string report;           // conformance
    std::string unplug, replug;   // conformance
    double rolling_s = 0.0;       // capture-iq
    std::map<std::string, double> gains;
    std::optional<double> gain_db, bandwidth_hz;
    std::string antenna;
    double ppm = 0.0;
    std::vector<std::string> kit_scenes;
};

int usage_error(const std::string& message)
{
    std::cerr << "error: " << message << "\n\n" << kUsage;
    return kExitUsage;
}

bool parse_u64(const std::string& t, uint64_t& out)
{
    if (t.empty() || t[0] == '-')
        return false;
    char* end = nullptr;
    errno = 0;
    out = std::strtoull(t.c_str(), &end, 10);
    return *end == '\0' && errno == 0;
}

bool parse_double(const std::string& t, double& out)
{
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return !t.empty() && *end == '\0' && std::isfinite(out);
}

std::vector<std::string> split(const std::string& t, char sep)
{
    std::vector<std::string> out;
    std::string part;
    std::istringstream in(t);
    while (std::getline(in, part, sep))
        out.push_back(part);
    return out;
}

bool parse_points(const std::string& t, std::vector<PlanPoint>& out)
{
    out.clear();
    for (const std::string& p : split(t, ';')) {
        const auto xy = split(p, ',');
        PlanPoint pt;
        if (xy.size() != 2 || !parse_double(xy[0], pt.x_m) || !parse_double(xy[1], pt.y_m))
            return false;
        out.push_back(pt);
    }
    return !out.empty();
}

bool read_text(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

std::string bits_text(uint32_t bits, const std::vector<std::pair<uint32_t, const char*>>& names)
{
    std::string out;
    for (const auto& [bit, name] : names)
        if ((bits & bit) != 0)
            out += (out.empty() ? "" : " ") + std::string(name);
    return out.empty() ? "none" : out;
}

std::string caps_text(uint32_t caps)
{
    return bits_text(caps, {{YR_CAP_SCAN, "scan"},
                            {YR_CAP_HOTPLUG, "hotplug"},
                            {YR_CAP_RETUNE_LIVE, "retune_live"},
                            {YR_CAP_SIMULATED, "simulated"},
                            {YR_CAP_SCENE, "scene"},
                            {YR_CAP_SELF_CALIBRATED, "self_calibrated"}});
}

std::string setting_flags_text(uint32_t flags)
{
    return bits_text(flags, {{YR_SETTING_READ_ONLY, "read_only"},
                             {YR_SETTING_UNAVAILABLE, "unavailable"},
                             {YR_SETTING_WARNING, "warning"},
                             {YR_SETTING_ADVANCED, "advanced"},
                             {YR_SETTING_CALIBRATION, "calibration"},
                             {YR_SETTING_RESTART, "restart"}});
}

std::string status_text(Host& host, yr_device* device, yr_status st)
{
    std::string out = yr_status_name(st);
    if (st != YR_OK) {
        const std::string why = host.last_error(device);
        if (!why.empty())
            out += ": " + why;
    }
    return out;
}

void print_problems(const std::vector<std::string>& problems)
{
    if (problems.empty()) {
        std::cout << "problems: none\n";
        return;
    }
    std::cout << "problems: " << problems.size() << "\n";
    for (const std::string& p : problems)
        std::cout << "  " << p << "\n";
}

std::unique_ptr<Host> load(const Options& o, int& exit_code)
{
    HostConfig cfg;
    cfg.print_level = o.log_level;
    cfg.log = &std::cerr;
    cfg.context = o.context;
    std::string error;
    auto host = Host::load(o.plugin, cfg, error);
    if (!host) {
        std::cerr << "error: " << error << "\n";
        exit_code = kExitFail;
    }
    return host;
}

// Opens --device, or the first device listed.
yr_device* open_device(Host& host, const std::string& wanted, radios::DeviceInfo& info)
{
    std::vector<radios::DeviceInfo> devices;
    std::string diag;
    const yr_status st = enumerate_devices(host, devices, diag);
    if (st != YR_OK || devices.empty()) {
        std::cerr << "error: no device: " << yr_status_name(st)
                  << (diag.empty() ? "" : ": " + diag) << "\n";
        return nullptr;
    }
    std::string key = wanted;
    if (key.empty())
        key = devices.front().key;
    for (const radios::DeviceInfo& d : devices)
        if (d.key == key)
            info = d;
    if (info.key.empty())
        info.key = key;
    yr_device* device = nullptr;
    const yr_status ost = host.descriptor().stream->open(host.plugin(), key.c_str(), &device);
    if (ost != YR_OK || device == nullptr) {
        std::cerr << "error: cannot open " << key << ": " << status_text(host, nullptr, ost)
                  << "\n";
        return nullptr;
    }
    return device;
}

// --set, --preset and --action; false when one failed.
bool apply_changes(Host& host, yr_device* device, const Options& o, std::ostream& out)
{
    bool ok = true;
    std::vector<SettingDef> defs;
    list_settings(host, device, false, defs);
    for (const auto& [key, text] : o.sets) {
        const SettingDef* def = nullptr;
        for (const SettingDef& d : defs)
            if (d.key == key)
                def = &d;
        if (def == nullptr) {
            out << "set " << key << ": unknown setting\n";
            ok = false;
            continue;
        }
        Value v;
        std::string error;
        if (!parse_value(*def, text, v, error)) {
            out << "set " << key << ": " << error << "\n";
            ok = false;
            continue;
        }
        const yr_status st = write_value(host, device, key, v);
        if (st != YR_OK) {
            out << "set " << key << " = " << text << ": refused: " << status_text(host, device, st)
                << "\n";
            ok = false;
            continue;
        }
        Value back;
        read_value(host, device, key, false, back);
        out << "set " << key << " = " << text << ": OK, reads back "
            << format_value(back, def->items) << "\n";
    }
    if (!o.preset.empty()) {
        const yr_device_control& dc = *host.descriptor().device;
        const yr_status st = dc.apply_preset != nullptr
                                 ? dc.apply_preset(device, o.preset.c_str())
                                 : YR_ERR_UNSUPPORTED;
        out << "preset " << o.preset << ": " << status_text(host, device, st) << "\n";
        ok = ok && st == YR_OK;
    }
    for (const std::string& key : o.actions) {
        std::string result;
        const yr_status st = run_action(host, device, key, result);
        out << "action " << key << ": " << status_text(host, device, st)
            << (result.empty() ? "" : " - " + result) << "\n";
        ok = ok && st == YR_OK;
    }
    return ok;
}

// Prints settings or sensors with their values; false on problems.
bool print_settings(Host& host, yr_device* device, bool sensors)
{
    std::vector<SettingDef> defs;
    const yr_status st = list_settings(host, device, sensors, defs);
    const char* what = sensors ? "sensors" : "settings";
    if (st == YR_ERR_UNSUPPORTED) {
        std::cout << what << ": none\n";
        return true;
    }
    if (st != YR_OK) {
        std::cout << what << ": " << status_text(host, device, st) << "\n";
        return false;
    }
    bool ok = true;
    std::cout << what << ":\n";
    for (const SettingDef& d : defs) {
        std::cout << "  " << d.key << " (" << type_text(d.type)
                  << (d.group.empty() ? "" : ", " + d.group) << ")";
        if (d.type != YR_TYPE_ACTION) {
            Value v;
            const yr_status rst = read_value(host, device, d.key, sensors, v);
            if (rst == YR_OK) {
                std::cout << " = " << format_value(v, d.items);
            } else {
                std::cout << " = <" << status_text(host, device, rst) << ">";
                ok = false;
            }
            if (!d.unit.empty())
                std::cout << " " << d.unit;
        }
        // A range only where one is declared (sensors often have none).
        if ((d.type == YR_TYPE_INT || d.type == YR_TYPE_UINT || d.type == YR_TYPE_DOUBLE) &&
            (std::isfinite(d.minimum) || std::isfinite(d.maximum))) {
            std::ostringstream r;
            r << " [" << d.minimum << ".." << d.maximum;
            if (d.step > 0)
                r << " step " << d.step;
            r << "]";
            std::cout << r.str();
        }
        if (d.flags != 0)
            std::cout << " flags: " << setting_flags_text(d.flags);
        std::cout << " - " << d.label << "\n";
        for (const std::string& p : d.problems) {
            std::cout << "    problem: " << p << "\n";
            ok = false;
        }
    }
    return ok;
}

int cmd_info(const Options& o)
{
    int code = kExitPass;
    auto host = load(o, code);
    if (!host)
        return code;
    const yr_descriptor& d = host->descriptor();
    auto s = [](const char* t) { return t != nullptr ? t : "(null)"; };
    std::cout << "plugin: " << s(d.id) << " " << s(d.version) << " \"" << s(d.name) << "\"\n"
              << "radio API: " << yantra::radio::version_text(d.api_version) << " (host "
              << yantra::radio::version_text(yr_api_version_compiled()) << ")\n"
              << "capabilities: " << caps_text(d.capabilities) << "\n"
              << "technology: " << s(d.technology_id) << "\n"
              << "licence: " << s(d.licence) << "\n"
              << "author: " << s(d.author) << "\n"
              << "source: " << s(d.source_url) << "\n"
              << "description: " << s(d.description) << "\n"
              << "translations: " << d.translation_count << "\n"
              << "schemas: " << host->header().schemas.size() << "\n";
    for (const radios::SchemaDef& sc : host->header().schemas) {
        std::cout << "  " << sc.id << " v" << sc.version << " "
                  << radios::schema_kind_name(sc.kind) << ", " << sc.fields.size()
                  << " fields - " << sc.label << "\n";
        for (const radios::FieldDef& f : sc.fields) {
            std::cout << "    " << f.name << " " << type_text(f.type) << " "
                      << (f.group == YR_GROUP_CORE ? std::string("core")
                                                   : std::string(radios::group_name(f.group)));
            if (!f.unit.empty())
                std::cout << " [" << f.unit << "]";
            std::cout << "\n";
        }
    }
    print_problems(host->descriptor_problems());
    return host->descriptor_problems().empty() ? kExitPass : kExitFail;
}

int cmd_devices(const Options& o)
{
    int code = kExitPass;
    auto host = load(o, code);
    if (!host)
        return code;
    std::vector<radios::DeviceInfo> devices;
    std::string diag;
    const yr_status st = enumerate_devices(*host, devices, diag);
    if (st == YR_ERR_NO_DEVICE) {
        std::cout << "devices: none" << (diag.empty() ? "" : " - " + diag) << "\n";
        return kExitPass;
    }
    if (st != YR_OK) {
        std::cout << "devices: " << yr_status_name(st) << (diag.empty() ? "" : ": " + diag)
                  << "\n";
        return kExitFail;
    }
    std::cout << "devices: " << devices.size() << "\n";
    for (const radios::DeviceInfo& dv : devices)
        std::cout << "  " << dv.key << " \"" << dv.label << "\" driver " << dv.driver
                  << (dv.serial.empty() ? "" : " serial " + dv.serial)
                  << ((dv.flags & YR_DEVICE_SIMULATED) != 0 ? " simulated" : "")
                  << ((dv.flags & YR_DEVICE_LEVEL_DBM) != 0 ? " level-dbm" : "")
                  << ((dv.flags & YR_DEVICE_IN_USE) != 0 ? " in-use" : "")
                  << ((dv.flags & YR_DEVICE_NO_PERMISSION) != 0 ? " no-permission" : "") << "\n";
    return kExitPass;
}

int cmd_settings(const Options& o)
{
    int code = kExitPass;
    auto host = load(o, code);
    if (!host)
        return code;
    radios::DeviceInfo info;
    yr_device* device = open_device(*host, o.device, info);
    if (device == nullptr)
        return kExitFail;
    std::cout << "device: " << info.key << " \"" << info.label << "\"\n";
    bool ok = apply_changes(*host, device, o, std::cout);
    ok = print_settings(*host, device, false) && ok;
    std::vector<PresetDef> presets;
    const yr_status pst = list_presets(*host, device, presets);
    if (pst == YR_OK) {
        std::cout << "presets:\n";
        for (const PresetDef& p : presets)
            std::cout << "  " << p.id << " - " << p.label << "\n";
    } else {
        std::cout << "presets: none\n";
    }
    ok = print_settings(*host, device, true) && ok;
    std::cout << "settings_changed: " << host->settings_changed_count() << "\n";
    host->descriptor().stream->close(device);
    return ok ? kExitPass : kExitFail;
}

int cmd_selftest(const Options& o)
{
    int code = kExitPass;
    auto host = load(o, code);
    if (!host)
        return code;
    radios::DeviceInfo info;
    yr_device* device = open_device(*host, o.device, info);
    if (device == nullptr)
        return kExitFail;
    std::vector<SelfTestItem> items;
    const yr_status st = run_self_test(*host, device, items);
    bool ok = st == YR_OK || st == YR_ERR_UNSUPPORTED;
    std::cout << "selftest: " << (st == YR_ERR_UNSUPPORTED ? "not offered"
                                                            : status_text(*host, device, st))
              << "\n";
    for (const SelfTestItem& it : items) {
        std::cout << "  " << test_result_text(it.result) << " " << it.name << " - " << it.label
                  << ": " << it.detail << (it.remedy.empty() ? "" : " (" + it.remedy + ")")
                  << "\n";
        ok = ok && it.result != YR_TEST_FAIL;
    }
    host->descriptor().stream->close(device);
    return ok ? kExitPass : kExitFail;
}

int cmd_stream(const Options& o)
{
    int code = kExitPass;
    auto host = load(o, code);
    if (!host)
        return code;
    radios::DeviceInfo info;
    yr_device* device = open_device(*host, o.device, info);
    if (device == nullptr)
        return kExitFail;
    bool ok = apply_changes(*host, device, o, std::cerr);

    yr_plan plan = o.plan;
    plan.struct_size = sizeof plan;
    plan.carriers_hz = o.carriers.empty() ? nullptr : o.carriers.data();
    plan.carrier_count = static_cast<uint32_t>(o.carriers.size());

    std::ofstream file;
    std::ostream* out = nullptr;
    if (!o.quiet) {
        if (o.out == "-") {
            out = &std::cout;
        } else {
            file.open(o.out, std::ios::binary | std::ios::trunc);
            if (!file) {
                std::cerr << "error: cannot write " << o.out << "\n";
                host->descriptor().stream->close(device);
                return kExitUsage;
            }
            out = &file;
        }
    }
    std::optional<radios::RecordFileWriter> writer;
    if (out != nullptr) {
        radios::RecordFileHeader header = host->header();
        header.producer = std::string("yantra-radio-host ") + host_version();
        header.created_utc_ns = host->host_api().utc_now_ns(host->host_api().host);
        header.device = info;
        header.plan = radios::plan_from_c(plan);
        writer.emplace(*out);
        writer->write_header(header);
    }

    StreamLimits limits;
    limits.max_records = o.max_records;
    limits.max_seconds = o.duration;
    limits.interrupt = &g_interrupt;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    const StreamResult r = run_stream(
        *host, device, plan, limits,
        [&](const radios::Record& rec) {
            if (writer)
                writer->write_record(rec);
        },
        [&](const radios::Event& ev) {
            if (writer)
                writer->write_event(ev);
        });
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);

    if (r.start_status != YR_OK) {
        std::cerr << "error: start_stream: " << yr_status_name(r.start_status)
                  << (r.start_error.empty() ? "" : ": " + r.start_error) << "\n";
        host->descriptor().stream->close(device);
        return kExitFail;
    }
    if (writer) {
        writer->write_end({writer->records(), writer->events(),
                           r.dropped + (r.stats_ok ? r.stats.records_dropped : 0)});
        out->flush();
    }
    if (o.sensors) {
        host->wait_notifications_idle();
        std::ostringstream sensors;
        std::streambuf* old = std::cout.rdbuf(sensors.rdbuf());
        ok = print_settings(*host, device, true) && ok;
        std::cout.rdbuf(old);
        std::cerr << sensors.str();
    }
    host->descriptor().stream->close(device);
    host->wait_notifications_idle();

    std::cerr << "stream: records " << r.records << ", events " << r.events << ", invalid "
              << r.invalid << ", dropped " << r.dropped << ", late " << r.late << ", stop "
              << (r.stop_reason.empty() ? "?" : r.stop_reason) << "\n";
    if (r.stats_ok)
        std::cerr << "plugin stats: delivered " << r.stats.records_delivered << ", dropped "
                  << r.stats.records_dropped << ", overflows " << r.stats.overflows << "\n";
    const auto sent = host->notifications_sent();
    if (!sent.empty()) {
        static const std::map<uint32_t, const char*> names = {
            {YR_CONTEXT_PLAN_CHANGED, "plan_changed"},
            {YR_CONTEXT_SCENE_CHANGED, "scene_changed"},
            {YR_CONTEXT_CURSOR_MOVED, "cursor_moved"},
            {YR_CONTEXT_WALK_POSITION, "walk_position"}};
        std::cerr << "notifications:";
        for (const auto& [event, n] : sent)
            std::cerr << " " << (names.count(event) ? names.at(event) : "?") << " " << n;
        std::cerr << "\n";
    }
    for (const std::string& p : r.problems)
        std::cerr << "invalid record: " << p << "\n";
    if (r.stop_status != YR_OK)
        std::cerr << "error: stop_stream: " << yr_status_name(r.stop_status) << "\n";
    if (r.late != 0)
        std::cerr << "error: " << r.late << " pushes after stop_stream returned (RAD-33)\n";
    ok = ok && r.invalid == 0 && r.late == 0 && r.stop_status == YR_OK &&
         r.stop_reason != "error" && r.stop_reason != "device_lost";
    return ok ? kExitPass : kExitFail;
}

int cmd_read(const Options& o)
{
    radios::RecordFileHeader h;
    std::vector<radios::RecordFileLine> lines;
    std::string error;
    if (!radios::read_record_file(o.file, h, lines, error)) {
        std::cerr << "error: " << error << "\n";
        return kExitFail;
    }
    std::map<std::string, uint64_t> per_schema;
    uint64_t records = 0;
    uint64_t events = 0;
    const radios::EndSummary* end = nullptr;
    for (const radios::RecordFileLine& l : lines) {
        if (l.kind == radios::RecordFileLine::Kind::record) {
            ++records;
            ++per_schema[h.schemas[l.record.schema_index].id];
        } else if (l.kind == radios::RecordFileLine::Kind::event) {
            ++events;
        } else {
            end = &l.end;
        }
    }
    std::cout << "format: " << radios::record_file_format << " " << h.version << "\n"
              << "producer: " << h.producer << "\n"
              << "plugin: " << h.plugin.id << " " << h.plugin.version << "\n"
              << "device: " << (h.device ? h.device->key : std::string("-")) << "\n"
              << "plan: "
              << (h.plan ? std::string(radios::plan_type_name(h.plan->type)) : std::string("-"))
              << "\n"
              << "schemas: " << h.schemas.size() << "\n"
              << "records: " << records << "\n";
    for (const auto& [id, n] : per_schema)
        std::cout << "  " << id << ": " << n << "\n";
    std::cout << "events: " << events << "\n";
    if (end == nullptr) {
        std::cout << "end: missing (the file was cut off)\n";
        return kExitPass;
    }
    std::cout << "end: records " << end->records << ", events " << end->events << ", dropped "
              << end->dropped << "\n";
    if (end->records != records || end->events != events) {
        std::cout << "error: the end line does not match the file\n";
        return kExitFail;
    }
    return kExitPass;
}

// RAD-63: decode-iq through the receiver, capture-iq through the SoapySDR
// modules built into the host.
int cmd_iq(const Options& o)
{
    if (o.command == "capture-iq") {
#ifdef YANTRA_HOST_CAPTURE_IQ
        if (o.out == "-" || o.out.empty())
            return usage_error("capture-iq needs --out FILE.sigmf-data");
        CaptureOptions c;
        c.device = o.device;
        c.out = o.out;
        if (!o.iq_format.empty()) {
            const auto f = iq_format_from_string(o.iq_format);
            if (!f)
                return usage_error("--format needs cs16, cs8, cu8 or cf32");
            c.format = *f;
        }
        if (o.iq_rate)
            c.rate_hz = *o.iq_rate;
        if (o.iq_centre)
            c.centre_hz = *o.iq_centre;
        if (o.duration > 0.0)
            c.duration_s = o.duration;
        c.rolling_s = o.rolling_s;
        c.gains = o.gains;
        c.gain_db = o.gain_db;
        c.bandwidth_hz = o.bandwidth_hz;
        c.antenna = o.antenna;
        c.ppm = o.ppm;
        c.interrupt = &g_interrupt;
        std::signal(SIGINT, on_signal);
        yantra::soapy::module_table() = YANTRA_SOAPY_MODULE_TABLE;
        CaptureResult r;
        std::string error;
        if (!capture_iq(c, r, error)) {
            std::cerr << "error: " << error << '\n';
            return kExitFail;
        }
        std::cout << "captured " << r.samples << " samples (" << r.samples / r.rate_hz << " s) of "
                  << r.device << " (" << r.module << ") at " << r.centre_hz << " Hz, "
                  << r.rate_hz << " Hz to " << o.out << " and " << r.meta_path << "; "
                  << r.overflows << " overflows, " << r.lost << " samples lost, " << r.segments
                  << " capture segment(s)\n";
        return kExitPass;
#else
        std::cerr << "error: capture-iq is not in this build (it needs the SoapySDR layer)\n";
        return kExitFail;
#endif
    }
    static const char* const formats[] = {"cs8", "cu8", "cs16", "cf32", "sigmf"};
    bool known = false;
    for (const char* f : formats)
        known = known || o.iq_format == f;
    if (o.iq.empty() || !known)
        return usage_error("decode-iq needs --iq FILE and --format cs8|cu8|cs16|cf32|sigmf");
#ifdef YANTRA_HOST_DECODE_IQ
    yantra::radio_host::DecodeIqOptions d;
    d.iq = o.iq;
    d.format = o.iq_format;
    d.rate_hz = o.iq_rate;
    d.centre_hz = o.iq_centre;
    d.out = o.quiet ? std::string() : o.out;
    if (o.plan.type == YR_PLAN_CARRIERS)
        for (uint64_t c : o.carriers)
            d.carriers.push_back(static_cast<double>(c));
    else if (o.plan.serving_hz != 0)
        d.serving_hz = static_cast<double>(o.plan.serving_hz);
    d.scan_records = o.scan_records;
    d.threads = o.threads;
    d.usable_fraction = o.usable_fraction;
    d.full_scale_dbm = o.full_scale_dbm;
    d.uncalibrated = o.uncalibrated;
    d.gsmtap_host = o.gsmtap_host;
    d.gsmtap_port = o.gsmtap_port;
    yantra::radio_host::DecodeIqResult r;
    std::string error;
    if (!yantra::radio_host::decode_iq(d, r, error)) {
        std::cerr << "error: " << error << "\n";
        return kExitUsage;
    }
    std::ostream& msg = d.out == "-" ? std::cerr : std::cout;
    msg << "decode-iq: samples " << r.samples << ", seconds " << r.seconds << ", records " << r.records
        << ", events " << r.events << ", carriers " << r.carriers << ", synchronised " << r.synced
        << ", segments " << r.segments << ", clipped blocks " << r.clipped_blocks << ", level "
        << (r.calibrated ? "dBm" : "dBFS") << ", load " << r.load;
    if (!d.gsmtap_host.empty())
        msg << ", gsmtap datagrams " << r.gsmtap_sent;
    msg << "\n";
    if (r.truncated)
        msg << "warning: the file ends with a partial sample\n";
    return kExitPass;
#else
    std::cerr << "error: decode-iq is not available: this build has no TETRA receiver (osmo-tetra not found)\n";
    return kExitFail;
#endif
}

} // namespace

int cmd_conformance(const Options& o)
{
    if ((o.unplug.empty()) != (o.replug.empty()))
        return usage_error("--unplug and --replug go together");
    ConformanceOptions k;
    k.device = o.device;
    k.scene_json = o.kit_scenes;
    if (o.duration > 0.0)
        k.stream_seconds = std::max(1.5, o.duration);
    auto shell = [](const std::string& cmd) {
        return [cmd](std::string& error) {
#ifdef _WIN32
            // cmd.exe /c strips the first and the last quote of the line:
            // enclosed once more, a command with a quoted program path
            // ("C:/x/cmake.exe" -E touch "f") reaches cmd.exe unchanged.
            const int rc = std::system(("\"" + cmd + "\"").c_str());
#else
            const int rc = std::system(cmd.c_str());
#endif
            if (rc != 0)
                error = "\"" + cmd + "\" exited with " + std::to_string(rc);
            return rc == 0;
        };
    };
    if (!o.unplug.empty()) {
        k.unplug = shell(o.unplug);
        k.replug = shell(o.replug);
    }
    const bool json_to_stdout = o.report == "-";
    k.progress = json_to_stdout ? &std::cerr : nullptr;
    const ConformanceReport report = run_conformance(o.plugin, k);
    print_conformance(report, json_to_stdout ? std::cerr : std::cout);
    if (!o.report.empty()) {
        const std::string json = conformance_json(report);
        if (json_to_stdout) {
            std::cout << json;
        } else {
            std::ofstream f(o.report, std::ios::binary);
            f << json;
            if (!f) {
                std::cerr << "error: cannot write " << o.report << "\n";
                return kExitUsage;
            }
        }
    }
    return report.passed() ? kExitPass : kExitFail;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::cout << kUsage;
        return args.empty() ? kExitUsage : kExitPass;
    }
    Options o;
    o.command = args[0];
    o.plan.struct_size = sizeof o.plan;
    o.plan.type = YR_PLAN_SERVING;
    bool context_plan = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= args.size())
                return false;
            out = args[++i];
            return true;
        };
        std::string v;
        if (a == "--quiet") {
            o.quiet = true;
        } else if (a == "--sensors") {
            o.sensors = true;
        } else if (a == "--scan-records") {
            o.scan_records = true;
        } else if (a == "--uncalibrated") {
            o.uncalibrated = true;
        } else if (a == "--neighbours") {
            o.plan.flags |= YR_PLAN_MEASURE_NEIGHBOURS;
        } else if (a == "--no-plan") {
            o.no_plan = true;
        } else if (a == "--no-scene") {
            o.context.no_scene = true;
            context_plan = true;
        } else if (a == "--serving") {
            o.plan.type = YR_PLAN_SERVING;
            if (i + 1 < args.size() && parse_u64(args[i + 1], o.plan.serving_hz))
                ++i;
        } else if (o.command == "read" && a.rfind("--", 0) != 0 && o.file.empty()) {
            o.file = a;
        } else if (!value(v)) {
            return usage_error("unknown option or missing value: " + a);
        } else if (a == "--plugin") {
            o.plugin = v;
        } else if (a == "--device") {
            o.device = v;
        } else if (a == "--set") {
            const auto eq = v.find('=');
            if (eq == std::string::npos || eq == 0)
                return usage_error("--set needs KEY=VALUE");
            o.sets.emplace_back(v.substr(0, eq), v.substr(eq + 1));
        } else if (a == "--preset") {
            o.preset = v;
        } else if (a == "--action") {
            o.actions.push_back(v);
        } else if (a == "--carriers") {
            o.plan.type = YR_PLAN_CARRIERS;
            o.carriers.clear();
            for (const std::string& c : split(v, ',')) {
                uint64_t hz = 0;
                if (!parse_u64(c, hz) || hz == 0)
                    return usage_error("--carriers needs HZ[,HZ...]");
                o.carriers.push_back(hz);
            }
        } else if (a == "--scan") {
            const auto parts = split(v, ':');
            o.plan.type = YR_PLAN_SCAN;
            if (parts.size() < 2 || parts.size() > 3 ||
                !parse_u64(parts[0], o.plan.scan_first_hz) ||
                !parse_u64(parts[1], o.plan.scan_last_hz) ||
                (parts.size() == 3 && !parse_u64(parts[2], o.plan.scan_step_hz)) ||
                o.plan.scan_last_hz < o.plan.scan_first_hz)
                return usage_error("--scan needs FIRST:LAST[:STEP] in Hz");
        } else if (a == "--records") {
            if (!parse_u64(v, o.max_records) || o.max_records == 0)
                return usage_error("--records needs a count above 0");
        } else if (a == "--duration") {
            if (!parse_double(v, o.duration) || o.duration <= 0)
                return usage_error("--duration needs seconds above 0");
        } else if (a == "--out") {
            o.out = v;
        } else if (a == "--scene") {
            std::string json;
            if (!read_text(v, json)) {
                std::cerr << "error: cannot read " << v << "\n";
                return kExitUsage;
            }
            o.kit_scenes.push_back(json);
            o.context.scene_json.push_back(std::move(json));
            context_plan = true;
        } else if (a == "--floor") {
            o.context.shown_floor = v;
        } else if (a == "--plan-name") {
            o.context.plan_name = v;
            context_plan = true;
        } else if (a == "--example-id") {
            o.context.example_id = v;
        } else if (a == "--cursor") {
            if (!parse_points(v, o.context.cursor))
                return usage_error("--cursor needs X,Y[;X,Y...]");
        } else if (a == "--walk") {
            if (!parse_points(v, o.context.walk))
                return usage_error("--walk needs X,Y[;X,Y...]");
        } else if (a == "--path-step") {
            uint64_t n = 0;
            if (!parse_u64(v, n) || n > UINT32_MAX)
                return usage_error("--path-step needs a count");
            o.context.path_step_records = static_cast<uint32_t>(n);
        } else if (a == "--log-level") {
            static const std::map<std::string, uint32_t> levels = {{"debug", YR_LOG_DEBUG},
                                                                   {"info", YR_LOG_INFO},
                                                                   {"warning", YR_LOG_WARNING},
                                                                   {"error", YR_LOG_ERROR}};
            if (levels.count(v) == 0)
                return usage_error("--log-level needs debug, info, warning or error");
            o.log_level = levels.at(v);
        } else if (a == "--report") {
            o.report = v;
        } else if (a == "--rolling" || a == "--bandwidth" || a == "--ppm") {
            double x = 0;
            if (!parse_double(v, x) || (a != "--ppm" && x <= 0))
                return usage_error(a + " needs a number" + (a != "--ppm" ? " above 0" : ""));
            if (a == "--rolling")
                o.rolling_s = x;
            else if (a == "--bandwidth")
                o.bandwidth_hz = x;
            else
                o.ppm = x;
        } else if (a == "--gain") {
            const auto eq = v.find('=');
            double x = 0;
            if (!parse_double(eq == std::string::npos ? v : v.substr(eq + 1), x) || eq == 0)
                return usage_error("--gain needs DB or NAME=DB");
            if (eq == std::string::npos)
                o.gain_db = x;
            else
                o.gains[v.substr(0, eq)] = x;
        } else if (a == "--antenna") {
            o.antenna = v;
        } else if (a == "--unplug") {
            o.unplug = v;
        } else if (a == "--replug") {
            o.replug = v;
        } else if (a == "--iq") {
            o.iq = v;
        } else if (a == "--format") {
            o.iq_format = v;
        } else if (a == "--rate" || a == "--center") {
            double x = 0;
            if (!parse_double(v, x) || x <= 0)
                return usage_error(a + " needs a number above 0");
            (a == "--rate" ? o.iq_rate : o.iq_centre) = x;
        } else if (a == "--full-scale-dbm") {
            double x = 0;
            if (!parse_double(v, x))
                return usage_error("--full-scale-dbm needs a number");
            o.full_scale_dbm = x;
        } else if (a == "--usable-fraction") {
            double x = 0;
            if (!parse_double(v, x) || x <= 0.0 || x > 1.0)
                return usage_error("--usable-fraction needs a number in (0, 1]");
            o.usable_fraction = x;
        } else if (a == "--gsmtap") {
            o.gsmtap_host = v;
        } else if (a == "--gsmtap-port") {
            uint64_t port = 0;
            if (!parse_u64(v, port) || port == 0 || port > 65535)
                return usage_error("--gsmtap-port needs 1..65535");
            o.gsmtap_port = static_cast<uint16_t>(port);
        } else if (a == "--threads") {
            uint64_t t = 0;
            if (!parse_u64(v, t) || t == 0 || t > 64)
                return usage_error("--threads needs 1..64");
            o.threads = static_cast<unsigned>(t);
        } else {
            return usage_error("unknown option: " + a);
        }
    }
    o.context.plan = context_plan && !o.no_plan;

    if (o.command == "read") {
        if (o.file.empty())
            return usage_error("read needs a FILE");
        return cmd_read(o);
    }
    if (o.command == "decode-iq" || o.command == "capture-iq")
        return cmd_iq(o);
    if (o.plugin.empty())
        return usage_error("--plugin is required");
    if (o.command == "info")
        return cmd_info(o);
    if (o.command == "devices")
        return cmd_devices(o);
    if (o.command == "settings")
        return cmd_settings(o);
    if (o.command == "selftest")
        return cmd_selftest(o);
    if (o.command == "stream")
        return cmd_stream(o);
    if (o.command == "decode-iq" || o.command == "capture-iq")
        return cmd_iq(o);
    if (o.command == "conformance")
        return cmd_conformance(o);
    return usage_error("unknown command: " + o.command);
}
