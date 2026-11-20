// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-hackrf (RAD-66). Every device command goes through the HackRF
// plugin (Host::load, the radio API), except capture, which records IQ
// through the same built-in SoapySDR modules (RAD-63) while the plugin
// has the device closed. The tool never transmits (the HackRF TX path is
// not used) and turns antenna port power on only with --bias-tee.
#include "hackrf_app.hpp"

#include "capture_iq.hpp"
#include "host.hpp"

#include <yantra/radios/record_file.hpp>
#include <yantra/receiver/dependencies.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <thread>

#ifndef _WIN32
#include <sys/utsname.h>
#endif

namespace yantra::hackrf_tool {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using namespace yantra::radio_host;

const char* const kUsageText = R"(yantra-hackrf - tests a HackRF One through the HackRF plugin (RAD-66)

usage: yantra-hackrf COMMAND [options]

commands:
  list        HackRF devices with serial, board, firmware and USB speed
  info        device info, sensors and build info
  selftest    the plugin self-test with remedies
  settings    [list | get KEY... | set KEY=VALUE... | preset [ID]]
  measure     records for a plan: --plan serving|carriers|scan
              [--carrier F]... [--first F --last F --step F]
              [--duration S] [--records N] [--out FILE.yrec]
  capture     IQ to SigMF: --out FILE.sigmf-data [--duration S] [--rate R]
              [--centre F] [--lna DB] [--vga DB] [--amp on|off] [--format cs16|cs8|cf32]
  noise       noise floor per gain (antenna port terminated): [--gains LNA/VGA,...]
  level       level linearity: --frequency F --levels DBM,... [--step-cmd CMD]
              [--tolerance DB]; without --step-cmd it asks to set each level
  ppm         frequency error against a known carrier: --frequency F [--max-ppm P]
  throughput  each sample rate for --duration S: overflows and drops [--rates MSPS,...]
  replug      guided unplug/replug [--unplug-cmd CMD --replug-cmd CMD] [--timeout S]
  report      selftest, noise, throughput and replug, then a DEVICES.md row [--out FILE]

options:
  --plugin FILE   the HackRF plugin (default: next to the tool, then the install layout)
  --device KEY    hackrf:<serial> (default: the first HackRF)
  --set KEY=VALUE a setting before the command (repeatable); --preset ID a preset
  --bias-tee      confirms antenna port power (antenna_power=true): it powers
                  whatever is connected to the antenna port
  --json          JSON output
  --no-prompt     no questions (scripts)
  Frequencies and rates take k, M and G suffixes (392.5M, 2M).

exit codes: 0 pass, 1 fail, 2 no device, 3 usage error
)";

struct Options {
    std::string command;
    std::vector<std::string> positional;
    std::string plugin, device, out, preset;
    bool json = false, bias_tee = false, no_prompt = false;
    std::vector<std::pair<std::string, std::string>> sets;
    std::optional<double> duration;
    uint64_t records = 0;
    std::string plan = "serving";
    std::vector<double> carriers;
    double first = 0.0, last = 0.0, step = 25e3;
    double rate = 2e6, centre = 392.5e6;
    double lna = 16.0, vga = 24.0;
    bool amp = false;
    std::string format = "cs16";
    std::string gains = "0/20,8/20,16/20,24/20,32/20,40/20";
    std::vector<double> levels;
    double frequency = 0.0;
    std::string step_cmd, unplug_cmd, replug_cmd;
    double tolerance = 1.0, max_ppm = 30.0, timeout = 60.0;
    std::vector<double> rates;
};

struct Usage {
    std::string what;
};

bool parse_number(const std::string& t, double& out)
{
    if (t.empty())
        return false;
    double scale = 1.0;
    std::string s = t;
    switch (s.back()) {
    case 'k': case 'K': scale = 1e3; s.pop_back(); break;
    case 'M': scale = 1e6; s.pop_back(); break;
    case 'G': scale = 1e9; s.pop_back(); break;
    default: break;
    }
    try {
        std::size_t used = 0;
        const double v = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(v))
            return false;
        out = v * scale;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<double> parse_list(const std::string& t, const std::string& option)
{
    std::vector<double> out;
    std::stringstream ss(t);
    std::string item;
    while (std::getline(ss, item, ',')) {
        double v = 0.0;
        if (!parse_number(item, v))
            throw Usage{option + ": not a number: " + item};
        out.push_back(v);
    }
    if (out.empty())
        throw Usage{option + ": empty list"};
    return out;
}

bool truthy(std::string v)
{
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    return v == "true" || v == "1" || v == "on" || v == "yes";
}

Options parse(const std::vector<std::string>& args)
{
    Options o;
    if (args.empty())
        throw Usage{"no command"};
    o.command = args[0];
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size())
                throw Usage{a + " needs a value"};
            return args[++i];
        };
        auto number = [&]() {
            const std::string& v = value();
            double x = 0.0;
            if (!parse_number(v, x))
                throw Usage{a + ": not a number: " + v};
            return x;
        };
        if (a == "--plugin") o.plugin = value();
        else if (a == "--device") o.device = value();
        else if (a == "--out") o.out = value();
        else if (a == "--preset") o.preset = value();
        else if (a == "--json") o.json = true;
        else if (a == "--bias-tee") o.bias_tee = true;
        else if (a == "--no-prompt") o.no_prompt = true;
        else if (a == "--set") {
            const std::string& kv = value();
            const auto eq = kv.find('=');
            if (eq == std::string::npos || eq == 0)
                throw Usage{"--set needs KEY=VALUE"};
            o.sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        }
        else if (a == "--duration") o.duration = number();
        else if (a == "--records") o.records = static_cast<uint64_t>(std::max(0.0, number()));
        else if (a == "--plan") o.plan = value();
        else if (a == "--carrier") o.carriers.push_back(number());
        else if (a == "--first") o.first = number();
        else if (a == "--last") o.last = number();
        else if (a == "--step") o.step = number();
        else if (a == "--rate") o.rate = number();
        else if (a == "--centre") o.centre = number();
        else if (a == "--lna") o.lna = number();
        else if (a == "--vga") o.vga = number();
        else if (a == "--amp") o.amp = truthy(value());
        else if (a == "--format") o.format = value();
        else if (a == "--gains") o.gains = value();
        else if (a == "--levels") o.levels = parse_list(value(), a);
        else if (a == "--frequency") o.frequency = number();
        else if (a == "--step-cmd") o.step_cmd = value();
        else if (a == "--unplug-cmd") o.unplug_cmd = value();
        else if (a == "--replug-cmd") o.replug_cmd = value();
        else if (a == "--tolerance") o.tolerance = number();
        else if (a == "--max-ppm") o.max_ppm = number();
        else if (a == "--timeout") o.timeout = number();
        else if (a == "--rates") o.rates = parse_list(value(), a);
        else if (a.rfind("--", 0) == 0) throw Usage{"unknown option " + a};
        else o.positional.push_back(a);
    }
    // Antenna port power only with the confirmation (RAD-40 WARNING).
    auto check_power = [&](const std::string& k, const std::string& v) {
        if (k == "antenna_power" && truthy(v) && !o.bias_tee)
            throw Usage{"antenna_power=true powers whatever is connected to the antenna port "
                        "(a preamplifier needs it, a short or a DC-coupled input can be damaged): "
                        "confirm with --bias-tee"};
    };
    for (const auto& [k, v] : o.sets)
        check_power(k, v);
    if (o.command == "settings" && o.positional.size() > 1 && o.positional[0] == "set")
        for (std::size_t i = 1; i < o.positional.size(); ++i) {
            const auto eq = o.positional[i].find('=');
            if (eq == std::string::npos || eq == 0)
                throw Usage{"settings set needs KEY=VALUE"};
            check_power(o.positional[i].substr(0, eq), o.positional[i].substr(eq + 1));
        }
    return o;
}

std::string mhz(double hz)
{
    std::ostringstream s;
    s << std::fixed << std::setprecision(4) << hz / 1e6;
    return s.str();
}

std::string fixed(double v, int digits = 1)
{
    std::ostringstream s;
    s << std::fixed << std::setprecision(digits) << v;
    return s.str();
}

json to_json(const Value& v)
{
    switch (v.type) {
    case YR_TYPE_BOOL: return v.i64 != 0;
    case YR_TYPE_INT:
    case YR_TYPE_ENUM: return v.i64;
    case YR_TYPE_UINT: return v.u64;
    case YR_TYPE_DOUBLE: return v.f64;
    case YR_TYPE_STRING: return v.str;
    default: return nullptr;
    }
}

fs::path find_plugin(const Options& o, const Environment& env, std::vector<fs::path>& tried)
{
    if (!o.plugin.empty())
        return o.plugin;
    if (const char* e = std::getenv("YANTRA_HACKRF_PLUGIN"); e != nullptr && *e != '\0')
        return e;
    const fs::path dir = env.tool_path.parent_path();
    // Yantra's layout (build tree and application install, PLG-29), the
    // radio parts' own install (RAD-99), next to the tool.
    tried = {dir / ".." / "lib" / "yantra" / "plugins" / "radios" / YANTRA_HACKRF_PLUGIN_FILE,
             dir / ".." / YANTRA_HACKRF_LIBDIR / "yantra-radios" / "plugins" / YANTRA_HACKRF_PLUGIN_FILE,
             dir / "plugins" / YANTRA_HACKRF_PLUGIN_FILE};
    for (const fs::path& p : tried) {
        std::error_code ec;
        if (fs::exists(p, ec))
            return p;
    }
    return tried.front();
}

/// The plugin and, once opened, one HackRF.
struct Session {
    std::unique_ptr<Host> host;
    yr_device* device = nullptr;
    radios::DeviceInfo info;
    std::string hardware_info;
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() { close(); }
    void close()
    {
        if (device != nullptr) {
            host->descriptor().stream->close(device);
            device = nullptr;
        }
        if (host)
            host->wait_notifications_idle();
    }
    yr_status status_of(yr_status st, std::string& why) const
    {
        why = std::string(yr_status_name(st));
        const std::string e = host->last_error(device);
        if (!e.empty())
            why += ": " + e;
        return st;
    }
};

struct Fail {
    int code;
    std::string what;
};

/// Loads the plugin and finds the HackRF; opens it when `open`.
void start(Session& s, const Options& o, const Environment& env, bool open = true)
{
    std::vector<fs::path> tried;
    const fs::path plugin = find_plugin(o, env, tried);
    std::string error;
    s.host = Host::load(plugin, HostConfig{}, error);
    if (!s.host) {
        std::string where;
        for (const fs::path& p : tried)
            where += " " + p.string();
        throw Fail{kFail, "cannot load the HackRF plugin " + plugin.string() + ": " + error +
                              (where.empty() ? "" : " (looked in" + where + "; use --plugin)")};
    }
    if (!s.host->descriptor_problems().empty())
        throw Fail{kFail, "the plugin descriptor is not valid: " + s.host->descriptor_problems().front()};
    std::vector<radios::DeviceInfo> devices;
    std::string diag;
    const yr_status st = enumerate_devices(*s.host, devices, diag);
    std::erase_if(devices, [](const radios::DeviceInfo& d) { return d.driver != "hackrf"; });
    if (devices.empty())
        throw Fail{kNoDevice, "no HackRF found" + (st != YR_OK ? std::string(": ") + yr_status_name(st) : "") +
                                  (diag.empty() ? "" : ": " + diag) +
                                  ". Check the USB cable and the udev rules (udev/60-yantra-sdr.rules)."};
    for (const radios::DeviceInfo& d : devices)
        if (o.device.empty() || d.key == o.device || d.serial == o.device) {
            s.info = d;
            break;
        }
    if (s.info.key.empty())
        throw Fail{kNoDevice, "no HackRF " + o.device + " (found " + devices.front().key + ")"};
    if (!open)
        return;
    const yr_status ost = s.host->descriptor().stream->open(s.host->plugin(), s.info.key.c_str(), &s.device);
    if (ost != YR_OK || s.device == nullptr) {
        std::string why;
        s.status_of(ost, why);
        s.device = nullptr;
        throw Fail{kFail, "cannot open " + s.info.key + ": " + why};
    }
    s.host->descriptor().stream->device_info(
        s.device, [](void* u, const yr_device_info* i) { *static_cast<std::string*>(u) = i->hardware_info; },
        &s.hardware_info);
}

std::map<std::string, SettingDef> definitions(Session& s, bool sensors)
{
    std::vector<SettingDef> defs;
    std::map<std::string, SettingDef> out;
    if (list_settings(*s.host, s.device, sensors, defs) == YR_OK)
        for (SettingDef& d : defs)
            out.emplace(d.key, std::move(d));
    return out;
}

std::optional<Value> read(Session& s, const std::string& key, bool sensor)
{
    Value v;
    if (read_value(*s.host, s.device, key, sensor, v) != YR_OK)
        return std::nullopt;
    return v;
}

std::string text_of(Session& s, const std::string& key, bool sensor, const std::string& otherwise)
{
    const auto v = read(s, key, sensor);
    return v && v->type == YR_TYPE_STRING ? v->str : otherwise;
}

/// Writes KEY=VALUE and reads it back; returns the read-back text.
std::string set(Session& s, const std::map<std::string, SettingDef>& defs, const std::string& key,
                const std::string& text, json& j)
{
    const auto it = defs.find(key);
    if (it == defs.end())
        throw Fail{kUsage, "no setting " + key};
    Value v;
    std::string error;
    if (!parse_value(it->second, text, v, error))
        throw Fail{kUsage, key + ": " + error};
    const yr_status st = write_value(*s.host, s.device, key, v);
    if (st != YR_OK) {
        std::string why;
        s.status_of(st, why);
        throw Fail{kFail, "cannot set " + key + ": " + why};
    }
    const auto back = read(s, key, false);
    const std::string shown = back ? format_value(*back, it->second.items) : "?";
    j[key] = back ? to_json(*back) : json(nullptr);
    return shown;
}

void set_value(Session& s, const std::string& key, const Value& v)
{
    const yr_status st = write_value(*s.host, s.device, key, v);
    if (st != YR_OK) {
        std::string why;
        s.status_of(st, why);
        throw Fail{kFail, "cannot set " + key + ": " + why};
    }
}

void set_double(Session& s, const std::string& key, double x)
{
    Value v;
    v.type = YR_TYPE_DOUBLE;
    v.f64 = x;
    set_value(s, key, v);
}

void set_bool(Session& s, const std::string& key, bool on)
{
    Value v;
    v.type = YR_TYPE_BOOL;
    v.i64 = on ? 1 : 0;
    set_value(s, key, v);
}

/// --preset and --set.
void apply_options(Session& s, const Options& o, json& j, std::ostream& text)
{
    if (!o.preset.empty()) {
        const yr_status st = s.host->descriptor().device->apply_preset(s.device, o.preset.c_str());
        if (st != YR_OK) {
            std::string why;
            s.status_of(st, why);
            throw Fail{st == YR_ERR_NOT_FOUND ? kUsage : kFail, "preset " + o.preset + ": " + why};
        }
        j["preset"] = o.preset;
        text << "preset " << o.preset << " applied\n";
    }
    if (o.sets.empty())
        return;
    const auto defs = definitions(s, false);
    json& set_j = j["set"];
    for (const auto& [k, v] : o.sets)
        text << k << " = " << set(s, defs, k, v, set_j) << " (read back)\n";
}

struct Stream {
    StreamResult result;
    std::vector<radios::Record> records;
    std::vector<radios::Event> events;
};

/// Streams `plan` for `seconds` (or until `enough` says so).
Stream stream(Session& s, const yr_plan& plan, double seconds, const Environment& env,
              const std::function<bool(const radios::Record&)>& enough = {},
              radios::RecordFileWriter* writer = nullptr, uint64_t max_records = 0)
{
    Stream out;
    std::atomic<bool> stop{false};
    std::atomic<bool> forward{false};
    std::thread watcher;
    StreamLimits limits;
    limits.max_seconds = seconds;
    limits.max_records = max_records;
    limits.interrupt = &stop;
    // Ctrl+C stops the stream too.
    std::atomic<bool> done{false};
    if (env.interrupt != nullptr)
        watcher = std::thread([&] {
            while (!done) {
                if (*env.interrupt) {
                    stop = true;
                    forward = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    out.result = run_stream(
        *s.host, s.device, plan, limits,
        [&](const radios::Record& r) {
            if (writer != nullptr)
                writer->write_record(r);
            out.records.push_back(r);
            if (enough && enough(r))
                stop = true;
        },
        [&](const radios::Event& e) {
            if (writer != nullptr)
                writer->write_event(e);
            out.events.push_back(e);
        });
    done = true;
    if (watcher.joinable())
        watcher.join();
    if (out.result.start_status != YR_OK)
        throw Fail{kFail, std::string("start_stream: ") + yr_status_name(out.result.start_status) +
                              (out.result.start_error.empty() ? "" : ": " + out.result.start_error)};
    return out;
}

yr_plan make_plan(uint32_t type)
{
    yr_plan p{};
    p.struct_size = sizeof p;
    p.type = type;
    return p;
}

bool stream_ok(const StreamResult& r)
{
    return r.invalid == 0 && r.late == 0 && r.stop_status == YR_OK && r.stop_reason != "error" &&
           r.stop_reason != "device_lost";
}

json stream_json(const StreamResult& r)
{
    return json{{"records", r.records}, {"events", r.events}, {"invalid", r.invalid},
                {"dropped_records", r.dropped + (r.stats_ok ? r.stats.records_dropped : 0)},
                {"overflows", r.stats_ok ? r.stats.overflows : 0},
                {"samples_dropped", r.stats_ok ? r.stats.samples_dropped : 0},
                {"stop", r.stop_reason}};
}

bool prompt(const Options& o, const Environment& env, std::ostream& text, const std::string& what)
{
    text << what << "\n";
    if (o.no_prompt || env.in == nullptr)
        return true;
    *env.err << what << " Press Enter to continue." << std::endl;
    std::string line;
    return static_cast<bool>(std::getline(*env.in, line));
}

int run_command(const std::string& cmd, const std::map<std::string, std::string>& vars)
{
    std::string c = cmd;
    for (const auto& [k, v] : vars)
        for (std::size_t at = c.find(k); at != std::string::npos; at = c.find(k, at + v.size()))
            c.replace(at, k.size(), v);
    return std::system(c.c_str());
}

double seconds_or(const Options& o, double d) { return o.duration.value_or(d); }

// ---- commands -----------------------------------------------------------

int cmd_list(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    Session s;
    start(s, o, env, false);
    std::vector<radios::DeviceInfo> devices;
    std::string diag;
    enumerate_devices(*s.host, devices, diag);
    json& list = j["devices"] = json::array();
    for (const radios::DeviceInfo& d : devices) {
        if (d.driver != "hackrf")
            continue;
        Options one = o;
        one.device = d.key;
        Session ds;
        json e{{"key", d.key}, {"serial", d.serial}, {"module", d.module_version}};
        try {
            start(ds, one, env);
            std::string usb = "not reported";
            std::istringstream lines(ds.hardware_info);
            for (std::string line; std::getline(lines, line);)
                if (line.rfind("usb", 0) == 0 || line.rfind("USB", 0) == 0)
                    usb = line.substr(line.find('=') + 1);
            e["board"] = text_of(ds, "board_id", true, "?");
            e["firmware"] = text_of(ds, "firmware_version", true, "?");
            e["usb_speed"] = usb;
        } catch (const Fail& f) {
            e["error"] = f.what;
        }
        text << e["key"].get<std::string>() << "  board " << e.value("board", "?") << ", firmware "
             << e.value("firmware", "?") << ", USB speed " << e.value("usb_speed", "?")
             << (e.contains("error") ? ", error: " + e["error"].get<std::string>() : "") << "\n";
        list.push_back(std::move(e));
    }
    return kPass;
}

void sensors_to(Session& s, json& j, std::ostream& text)
{
    for (const auto& [key, def] : definitions(s, true)) {
        const auto v = read(s, key, true);
        j[key] = v ? to_json(*v) : json(nullptr);
        text << "  " << def.label << " (" << key << "): "
             << (v && v->type != YR_TYPE_NONE ? format_value(*v, def.items) : "-")
             << (def.unit.empty() || !v || v->type == YR_TYPE_NONE ? "" : " " + def.unit) << "\n";
    }
}

int cmd_info(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    const yr_descriptor& d = s.host->descriptor();
    j["plugin"] = json{{"id", d.id}, {"name", d.name}, {"version", d.version}, {"source", d.source_url}};
    j["device"] = json{{"key", s.info.key}, {"label", s.info.label}, {"serial", s.info.serial},
                   {"module", s.info.module_version}, {"hardware_info", s.hardware_info}};
    text << "plugin " << d.id << " " << d.version << " (" << d.name << "), source " << d.source_url << "\n"
         << "device " << s.info.key << " (" << s.info.label << "), module " << s.info.module_version << "\n";
    std::istringstream hw(s.hardware_info);
    for (std::string line; std::getline(hw, line);)
        text << "  " << line << "\n";
    text << "sensors:\n";
    sensors_to(s, j["sensors"], text);
    text << "build info (bundled libraries):\n";
    json& b = j["build"] = json::array();
    for (const receiver::BundledLibrary& l : receiver::bundled_libraries()) {
        b.push_back({{"name", l.name}, {"version", l.version}, {"licence", l.licence}});
        text << "  " << l.name << " " << l.version << " (" << l.licence << ")\n";
    }
    return kPass;
}

int cmd_selftest(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    std::vector<SelfTestItem> items;
    const yr_status st = run_self_test(*s.host, s.device, items);
    if (st != YR_OK) {
        std::string why;
        s.status_of(st, why);
        throw Fail{kFail, "self_test: " + why};
    }
    static const std::map<uint32_t, const char*> names = {
        {YR_TEST_PASS, "pass"}, {YR_TEST_WARN, "warn"}, {YR_TEST_FAIL, "fail"}, {YR_TEST_SKIPPED, "skipped"}};
    bool failed = false;
    json& list = j["items"] = json::array();
    for (const SelfTestItem& it : items) {
        const char* r = names.count(it.result) ? names.at(it.result) : "?";
        failed = failed || it.result == YR_TEST_FAIL;
        list.push_back({{"name", it.name}, {"label", it.label}, {"result", r}, {"detail", it.detail},
                        {"remedy", it.remedy}});
        text << "[" << r << "] " << it.label << ": " << it.detail << "\n";
        if (!it.remedy.empty())
            text << "       remedy: " << it.remedy << "\n";
    }
    return failed ? kFail : kPass;
}

int cmd_settings(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    const std::string sub = o.positional.empty() ? "list" : o.positional[0];
    const auto defs = definitions(s, false);
    auto show = [&](const SettingDef& def) {
        const auto v = read(s, def.key, false);
        json e{{"key", def.key}, {"label", def.label}, {"value", v ? to_json(*v) : json(nullptr)}};
        text << def.key << " = " << (v && v->type != YR_TYPE_NONE ? format_value(*v, def.items) : "-")
             << (def.unit.empty() ? "" : " " + def.unit) << "  (" << def.label << ")";
        if ((def.flags & YR_SETTING_UNAVAILABLE) != 0) {
            e["unavailable"] = def.unavailable_reason;
            text << " unavailable: " << def.unavailable_reason;
        }
        if ((def.flags & YR_SETTING_WARNING) != 0) {
            e["warning"] = def.warning;
            text << " WARNING: " << def.warning;
        }
        text << "\n";
        return e;
    };
    if (sub == "list") {
        json& list = j["settings"] = json::array();
        for (const auto& [key, def] : defs)
            list.push_back(show(def));
        return kPass;
    }
    if (sub == "get") {
        if (o.positional.size() < 2)
            throw Usage{"settings get needs KEY..."};
        json& list = j["settings"] = json::array();
        for (std::size_t i = 1; i < o.positional.size(); ++i) {
            const auto it = defs.find(o.positional[i]);
            if (it == defs.end())
                throw Fail{kUsage, "no setting " + o.positional[i]};
            list.push_back(show(it->second));
        }
        return kPass;
    }
    if (sub == "set") {
        if (o.positional.size() < 2)
            throw Usage{"settings set needs KEY=VALUE..."};
        json& back = j["read_back"];
        for (std::size_t i = 1; i < o.positional.size(); ++i) {
            const std::string& kv = o.positional[i];
            const auto eq = kv.find('=');
            const std::string key = kv.substr(0, eq);
            text << key << " = " << set(s, defs, key, kv.substr(eq + 1), back) << " (read back)\n";
        }
        return kPass;
    }
    if (sub == "preset") {
        if (o.positional.size() < 2) {
            std::vector<PresetDef> presets;
            list_presets(*s.host, s.device, presets);
            json& list = j["presets"] = json::array();
            for (const PresetDef& p : presets) {
                list.push_back({{"id", p.id}, {"label", p.label}, {"help", p.help}});
                text << p.id << "  " << p.label << ": " << p.help << "\n";
            }
            return kPass;
        }
        const std::string& id = o.positional[1];
        const yr_status st = s.host->descriptor().device->apply_preset(s.device, id.c_str());
        if (st != YR_OK) {
            std::string why;
            s.status_of(st, why);
            throw Fail{st == YR_ERR_NOT_FOUND ? kUsage : kFail, "preset " + id + ": " + why};
        }
        j["preset"] = id;
        text << "preset " << id << " applied; read back:\n";
        json& list = j["settings"] = json::array();
        for (const auto& [key, def] : definitions(s, false))
            if ((def.flags & YR_SETTING_CALIBRATION) != 0)
                list.push_back(show(def));
        return kPass;
    }
    throw Usage{"settings: unknown subcommand " + sub};
}

std::string record_line(const radios::RecordFileHeader& h, const radios::Record& r)
{
    const radios::SchemaDef& sc = h.schemas.at(r.schema_index);
    std::ostringstream s;
    s << sc.id << " " << mhz(static_cast<double>(r.values[YR_CORE_FREQUENCY_HZ].u64)) << " MHz";
    if (r.values[YR_CORE_LEVEL].type == YR_TYPE_DOUBLE)
        s << " " << fixed(r.values[YR_CORE_LEVEL].f64) << " dBFS";
    if (r.values[YR_CORE_LA].type == YR_TYPE_UINT)
        s << " MCC " << r.values[YR_CORE_MCC].u64 << " MNC " << r.values[YR_CORE_MNC].u64 << " LA "
          << r.values[YR_CORE_LA].u64 << " CC " << r.values[YR_CORE_COLOUR_CODE].u64;
    if ((r.flags & YR_RECORD_NO_SYNC) != 0)
        s << " no-sync";
    if ((r.flags & YR_RECORD_OVERLOAD) != 0)
        s << " OVERLOAD";
    return s.str();
}

json record_json(const radios::RecordFileHeader& h, const radios::Record& r)
{
    const radios::SchemaDef& sc = h.schemas.at(r.schema_index);
    json e{{"schema", sc.id}, {"flags", r.flags}};
    for (std::size_t i = 0; i < r.values.size() && i < sc.fields.size(); ++i)
        if (r.values[i].type != YR_TYPE_NONE)
            e[sc.fields[i].name] = to_json(r.values[i]);
    return e;
}

int cmd_measure(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    yr_plan plan{};
    if (o.plan == "serving") {
        plan = make_plan(YR_PLAN_SERVING);
    } else if (o.plan == "carriers") {
        if (o.carriers.empty())
            throw Usage{"--plan carriers needs --carrier F"};
        plan = make_plan(YR_PLAN_CARRIERS);
    } else if (o.plan == "scan") {
        if (o.first <= 0.0 || o.last < o.first)
            throw Usage{"--plan scan needs --first F --last F"};
        plan = make_plan(YR_PLAN_SCAN);
        plan.scan_first_hz = static_cast<uint64_t>(std::llround(o.first));
        plan.scan_last_hz = static_cast<uint64_t>(std::llround(o.last));
        plan.scan_step_hz = static_cast<uint64_t>(std::llround(o.step));
    } else {
        throw Usage{"--plan serving|carriers|scan"};
    }
    std::vector<uint64_t> carriers;
    for (double f : o.carriers)
        carriers.push_back(static_cast<uint64_t>(std::llround(f)));
    plan.carriers_hz = carriers.empty() ? nullptr : carriers.data();
    plan.carrier_count = static_cast<uint32_t>(carriers.size());

    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    std::ofstream file;
    std::optional<radios::RecordFileWriter> writer;
    const radios::RecordFileHeader header = [&] {
        radios::RecordFileHeader h = s.host->header();
        h.producer = std::string("yantra-hackrf ") + YANTRA_HACKRF_VERSION;
        h.created_utc_ns = s.host->host_api().utc_now_ns(s.host->host_api().host);
        h.device = s.info;
        h.plan = radios::plan_from_c(plan);
        return h;
    }();
    if (!o.out.empty()) {
        file.open(o.out, std::ios::binary | std::ios::trunc);
        if (!file)
            throw Fail{kUsage, "cannot write " + o.out};
        writer.emplace(file);
        writer->write_header(header);
    }
    const Stream r = stream(s, plan, seconds_or(o, 10.0), env, {}, writer ? &*writer : nullptr, o.records);
    if (writer) {
        writer->write_end({writer->records(), writer->events(),
                           r.result.dropped + (r.result.stats_ok ? r.result.stats.records_dropped : 0)});
        file.flush();
        j["out"] = o.out;
    } else {
        json& list = j["records"] = json::array();
        for (const radios::Record& rec : r.records) {
            list.push_back(record_json(header, rec));
            text << record_line(header, rec) << "\n";
        }
    }
    j["stream"] = stream_json(r.result);
    for (const std::string& p : r.result.problems)
        text << "invalid record: " << p << "\n";
    text << "records " << r.result.records << ", events " << r.result.events << ", stop "
         << r.result.stop_reason << "\n";
    return stream_ok(r.result) && r.result.records > 0 ? kPass : kFail;
}

int cmd_capture(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    if (o.out.empty())
        throw Usage{"capture needs --out FILE.sigmf-data"};
    Session s;
    start(s, o, env, false); // the plugin finds the device; capture opens it itself
    CaptureOptions c;
    c.device = s.info.key;
    c.out = o.out;
    if (o.format == "cs16") c.format = IqFormat::cs16;
    else if (o.format == "cs8") c.format = IqFormat::cs8;
    else if (o.format == "cf32") c.format = IqFormat::cf32;
    else throw Usage{"--format cs16|cs8|cf32"};
    c.rate_hz = o.rate;
    c.centre_hz = o.centre;
    c.duration_s = seconds_or(o, 10.0);
    c.gains = {{"LNA", o.lna}, {"VGA", o.vga}, {"AMP", o.amp ? 14.0 : 0.0}};
    c.interrupt = env.interrupt;
    s.close();
    s.host.reset();
    CaptureResult r;
    std::string error;
    if (!capture_iq(c, r, error))
        throw Fail{kFail, "capture: " + error};
    j["capture"] = {{"device", r.device}, {"module", r.module}, {"rate_hz", r.rate_hz},
                    {"centre_hz", r.centre_hz}, {"samples", r.samples}, {"lost", r.lost},
                    {"overflows", r.overflows}, {"meta", r.meta_path}};
    text << "captured " << r.samples << " samples at " << fixed(r.rate_hz / 1e6, 3) << " Msps, "
         << mhz(r.centre_hz) << " MHz to " << o.out << " (" << r.meta_path << "); lost " << r.lost
         << " in " << r.overflows << " overflows\n";
    return r.samples > 0 && r.lost == 0 ? kPass : kFail;
}

struct GainPoint {
    double lna, vga;
};

std::vector<GainPoint> parse_gains(const std::string& t)
{
    std::vector<GainPoint> out;
    std::stringstream ss(t);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto slash = item.find('/');
        GainPoint g{};
        if (slash == std::string::npos || !parse_number(item.substr(0, slash), g.lna) ||
            !parse_number(item.substr(slash + 1), g.vga))
            throw Usage{"--gains LNA/VGA,... (dB), not " + item};
        out.push_back(g);
    }
    if (out.empty())
        throw Usage{"--gains is empty"};
    return out;
}

int cmd_noise(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    const std::vector<GainPoint> points = parse_gains(o.gains);
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    prompt(o, env, text, "Terminate the antenna port with 50 ohm (no antenna) for the noise floor.");
    bool ok = true;
    json& list = j["points"] = json::array();
    for (const GainPoint& g : points) {
        set_double(s, "gain_lna", g.lna);
        set_double(s, "gain_vga", g.vga);
        set_bool(s, "rf_amplifier", o.amp);
        // Until the plugin has measured the floor at this gain (a slow
        // machine takes longer than the signal time); --duration is the limit.
        const Stream r = stream(s, make_plan(YR_PLAN_SERVING), seconds_or(o, 30.0), env,
                                [&](const radios::Record& rec) {
                                    if (rec.schema_index != 0)
                                        return false;
                                    const auto f = read(s, "noise_floor_dbfs", true);
                                    return f && f->type == YR_TYPE_DOUBLE;
                                });
        const auto floor = read(s, "noise_floor_dbfs", true);
        const double total = g.lna + g.vga + (o.amp ? 14.0 : 0.0);
        json e{{"lna_db", g.lna}, {"vga_db", g.vga}, {"amp", o.amp}, {"total_gain_db", total}};
        if (floor && floor->type == YR_TYPE_DOUBLE && stream_ok(r.result)) {
            e["noise_floor_dbfs"] = floor->f64;
            e["input_referred_db"] = floor->f64 - total;
            text << "LNA " << g.lna << " dB, VGA " << g.vga << " dB" << (o.amp ? ", AMP on" : "")
                 << ": noise floor " << fixed(floor->f64) << " dBFS (" << fixed(floor->f64 - total)
                 << " dB below full scale at 0 dB gain)\n";
        } else {
            ok = false;
            e["noise_floor_dbfs"] = nullptr;
            text << "LNA " << g.lna << " dB, VGA " << g.vga << " dB: no noise floor (" << r.result.stop_reason
                 << ")\n";
        }
        list.push_back(std::move(e));
    }
    return ok ? kPass : kFail;
}

/// Mean level (dBFS) of the records of the carrier at `hz`.
std::optional<double> carrier_level(const Stream& r, double hz)
{
    double sum = 0.0;
    int n = 0;
    for (const radios::Record& rec : r.records)
        if (rec.schema_index == 0 && rec.values[YR_CORE_LEVEL].type == YR_TYPE_DOUBLE &&
            std::fabs(static_cast<double>(rec.values[YR_CORE_FREQUENCY_HZ].u64) - hz) < 13'000.0) {
            sum += rec.values[YR_CORE_LEVEL].f64;
            ++n;
        }
    if (n == 0)
        return std::nullopt;
    return sum / n;
}

yr_plan carrier_plan(std::vector<uint64_t>& carriers)
{
    yr_plan p = make_plan(YR_PLAN_CARRIERS);
    p.carriers_hz = carriers.data();
    p.carrier_count = static_cast<uint32_t>(carriers.size());
    return p;
}

int cmd_level(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    if (o.frequency <= 0.0 || o.levels.empty())
        throw Usage{"level needs --frequency F and --levels DBM,..."};
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    std::vector<uint64_t> carriers{static_cast<uint64_t>(std::llround(o.frequency))};
    struct Row {
        double set, measured;
    };
    std::vector<Row> rows;
    bool ok = true;
    for (double dbm : o.levels) {
        const std::string what = "Set the signal generator to " + fixed(dbm) + " dBm at " + mhz(o.frequency) + " MHz.";
        if (!o.step_cmd.empty()) {
            text << what << "\n";
            if (run_command(o.step_cmd, {{"{dbm}", fixed(dbm, 2)}}) != 0)
                throw Fail{kFail, "--step-cmd failed for " + fixed(dbm) + " dBm"};
        } else if (!prompt(o, env, text, what)) {
            throw Fail{kFail, "no answer: the level steps need Enter after each setting (or --step-cmd)"};
        }
        // Two records of the carrier after the step (--duration is the limit).
        int seen = 0;
        const Stream r = stream(s, carrier_plan(carriers), seconds_or(o, 30.0), env, [&](const radios::Record& rec) {
            return rec.schema_index == 0 && rec.values[YR_CORE_LEVEL].type == YR_TYPE_DOUBLE &&
                   std::fabs(static_cast<double>(rec.values[YR_CORE_FREQUENCY_HZ].u64) - o.frequency) < 13'000.0 &&
                   ++seen >= 2;
        });
        const auto level = carrier_level(r, o.frequency);
        if (!level) {
            ok = false;
            text << fixed(dbm) << " dBm: no level measured\n";
            continue;
        }
        rows.push_back({dbm, *level});
    }
    double mean = 0.0;
    for (const Row& r : rows)
        mean += r.measured - r.set;
    mean = rows.empty() ? 0.0 : mean / static_cast<double>(rows.size());
    double worst = 0.0;
    json& list = j["steps"] = json::array();
    for (const Row& r : rows) {
        const double err = r.measured - r.set - mean;
        worst = std::max(worst, std::fabs(err));
        list.push_back({{"set_dbm", r.set}, {"measured_dbfs", r.measured}, {"linearity_error_db", err}});
        text << fixed(r.set) << " dBm -> " << fixed(r.measured, 2) << " dBFS (error " << fixed(err, 2) << " dB)\n";
    }
    ok = ok && rows.size() == o.levels.size() && worst <= o.tolerance;
    j["offset_db"] = mean;
    j["worst_error_db"] = worst;
    j["tolerance_db"] = o.tolerance;
    text << "dBFS = dBm + " << fixed(mean, 2) << " dB at these gains (c1_level_offset_db " << fixed(-mean, 2)
         << "); worst linearity error " << fixed(worst, 2) << " dB (tolerance " << fixed(o.tolerance, 2) << ")\n";
    return ok ? kPass : kFail;
}

int cmd_ppm(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    if (o.frequency <= 0.0)
        throw Usage{"ppm needs --frequency F of a known carrier"};
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    std::vector<uint64_t> carriers{static_cast<uint64_t>(std::llround(o.frequency))};
    const auto& sc = s.host->header().schemas.at(0);
    const int hz_i = sc.field_index(YR_FIELD_FREQ_ERROR_HZ);
    const int ppm_i = sc.field_index(YR_FIELD_FREQ_ERROR_PPM);
    if (hz_i < 0 || ppm_i < 0)
        throw Fail{kFail, "the plugin's records carry no frequency error"};
    std::vector<double> hz, ppm;
    const Stream r = stream(s, carrier_plan(carriers), seconds_or(o, 15.0), env, [&](const radios::Record& rec) {
        if (rec.schema_index != 0)
            return false;
        const Value& p = rec.values[static_cast<std::size_t>(ppm_i)];
        if (p.type == YR_TYPE_DOUBLE) {
            ppm.push_back(p.f64);
            hz.push_back(rec.values[static_cast<std::size_t>(hz_i)].f64);
        }
        return ppm.size() >= 5;
    });
    const auto setting = read(s, "ppm", false);
    j["setting_ppm"] = setting ? to_json(*setting) : json(nullptr);
    if (ppm.empty()) {
        text << "no frequency error: no carrier decoded at " << mhz(o.frequency) << " MHz ("
             << r.result.stop_reason << ")\n";
        j["error_ppm"] = nullptr;
        return kFail;
    }
    const auto mean = [](const std::vector<double>& v) {
        double sum = 0.0;
        for (double x : v)
            sum += x;
        return sum / static_cast<double>(v.size());
    };
    j["error_hz"] = mean(hz);
    j["error_ppm"] = mean(ppm);
    j["max_ppm"] = o.max_ppm;
    text << "frequency error at " << mhz(o.frequency) << " MHz: " << fixed(mean(hz)) << " Hz, "
         << fixed(mean(ppm), 2) << " ppm (ppm setting "
         << (setting ? format_value(*setting) : "?") << "; adjust it until the error is near 0)\n";
    return std::fabs(mean(ppm)) <= o.max_ppm ? kPass : kFail;
}

int cmd_throughput(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    const auto defs = definitions(s, false);
    const auto it = defs.find("receiver_rate");
    if (it == defs.end())
        throw Fail{kFail, "the plugin has no receiver_rate setting"};
    // The rates by their labels ("2 Msps").
    std::vector<std::pair<double, int64_t>> offered;
    for (const radios::EnumItem& e : it->second.items) {
        double v = 0.0;
        if (parse_number(e.label.substr(0, e.label.find(' ')), v))
            offered.emplace_back(v * 1e6, e.value);
    }
    std::vector<double> rates = o.rates;
    if (rates.empty())
        for (const auto& [hz, value] : offered)
            rates.push_back(hz);
    bool ok = true;
    json& list = j["rates"] = json::array();
    for (double want : rates) {
        if (want < 1000.0)
            want *= 1e6; // Msps
        const auto pick = std::find_if(offered.begin(), offered.end(),
                                       [&](const auto& p) { return std::fabs(p.first - want) < 1.0; });
        if (pick == offered.end())
            throw Fail{kUsage, "the plugin does not offer " + fixed(want / 1e6, 3) + " Msps"};
        Value v;
        v.type = YR_TYPE_ENUM;
        v.i64 = pick->second;
        set_value(s, "receiver_rate", v);
        uint64_t overflow_events = 0, lost = 0;
        const Stream r = stream(s, make_plan(YR_PLAN_SERVING), seconds_or(o, 5.0), env);
        for (const radios::Event& e : r.events) {
            if (e.event == YR_EVENT_OVERFLOW)
                ++overflow_events;
            if (e.event == YR_EVENT_DROPPED)
                lost += static_cast<uint64_t>(std::max<int64_t>(0, e.value));
        }
        const auto load = read(s, "receiver_load", true);
        json e = stream_json(r.result);
        e["rate_hz"] = want;
        e["overflow_events"] = overflow_events;
        e["receiver_load"] = load ? to_json(*load) : json(nullptr);
        const bool pass = stream_ok(r.result) && overflow_events == 0 && lost == 0 &&
                          (!r.result.stats_ok || (r.result.stats.overflows == 0 && r.result.stats.records_dropped == 0)) &&
                          r.result.dropped == 0;
        e["result"] = pass ? "pass" : "fail";
        ok = ok && pass;
        text << fixed(want / 1e6, 3) << " Msps: " << (pass ? "pass" : "FAIL") << ", overflows "
             << overflow_events << ", samples lost " << lost << ", records " << r.result.records
             << ", receiver load " << (load && load->type == YR_TYPE_DOUBLE ? fixed(load->f64, 2) : "?") << "\n";
        list.push_back(std::move(e));
    }
    return ok ? kPass : kFail;
}

bool wait_state(Session& s, uint32_t want, double seconds, const Environment& env)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (s.host->descriptor().stream->state(s.device) != want) {
        if (std::chrono::steady_clock::now() > end || (env.interrupt != nullptr && *env.interrupt))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

int cmd_replug(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    Session s;
    start(s, o, env);
    apply_options(s, o, j, text);
    auto step = [&](const std::string& cmd, const std::string& what) {
        text << what << "\n";
        if (!cmd.empty()) {
            if (run_command(cmd, {}) != 0)
                throw Fail{kFail, "command failed: " + cmd};
        } else if (!o.no_prompt) {
            *env.err << what << std::endl;
        }
    };
    step(o.unplug_cmd, "Unplug the HackRF now.");
    const bool lost = wait_state(s, YR_DEVICE_LOST, o.timeout, env);
    j["lost_seen"] = lost;
    if (!lost) {
        text << "the plugin did not report the device lost within " << o.timeout << " s\n";
        return kFail;
    }
    text << "device lost reported\n";
    step(o.replug_cmd, "Plug the HackRF back in.");
    const bool back = wait_state(s, YR_DEVICE_OPEN, o.timeout, env);
    j["reopened"] = back;
    if (!back) {
        text << "the plugin did not reopen the device within " << o.timeout << " s\n";
        return kFail;
    }
    const auto power = read(s, "antenna_power", false);
    const bool power_off = !power || power->i64 == 0;
    j["antenna_power_off"] = power_off;
    int n = 0;
    const Stream r = stream(s, make_plan(YR_PLAN_SERVING), seconds_or(o, 20.0), env,
                            [&](const radios::Record&) { return ++n >= 2; });
    j["records_after"] = r.result.records;
    const bool ok = power_off && r.result.records >= 2 && stream_ok(r.result);
    text << "reopened; antenna port power " << (power_off ? "off" : "ON (should be off)") << "; "
         << r.result.records << " records after the replug\n";
    return ok ? kPass : kFail;
}

std::string os_name()
{
#ifdef _WIN32
    return "Windows";
#else
    utsname u{};
    if (uname(&u) != 0)
        return "Linux";
    return std::string(u.sysname) + " " + u.release;
#endif
}

std::string today()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}

int dispatch(const Options& o, const Environment& env, json& j, std::ostream& text);

int cmd_report(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    std::string board = "?", firmware = "?", module = "?";
    {
        Session s;
        start(s, o, env);
        board = text_of(s, "board_id", true, "?");
        firmware = text_of(s, "firmware_version", true, "?");
        module = s.info.module_version;
    }
    std::vector<std::string> tested, issues;
    bool ok = true;
    for (const char* c : {"selftest", "noise", "throughput", "replug"}) {
        Options sub = o;
        sub.command = c;
        sub.out.clear();
        // --duration is the throughput time per rate; noise stops by
        // itself once the floor is measured (this is only its limit).
        if (std::string(c) == "noise")
            sub.duration = std::max(120.0, o.duration.value_or(0.0));
        std::ostringstream t;
        json& sj = j[c];
        int code = kFail;
        try {
            code = dispatch(sub, env, sj, t);
        } catch (const Fail& f) {
            code = f.code;
            sj["error"] = f.what;
            t << "error: " << f.what << "\n";
        }
        sj["result"] = code == kPass ? "pass" : "fail";
        text << "== " << c << " ==\n" << t.str();
        ok = ok && code == kPass;
        std::string what = c;
        if (std::string(c) == "noise" && sj.contains("points")) {
            double lo = 1e9, hi = -1e9;
            for (const json& p : sj["points"])
                if (p["noise_floor_dbfs"].is_number()) {
                    lo = std::min(lo, p["noise_floor_dbfs"].get<double>());
                    hi = std::max(hi, p["noise_floor_dbfs"].get<double>());
                }
            if (lo <= hi)
                what += " " + fixed(lo) + ".." + fixed(hi) + " dBFS";
        }
        if (std::string(c) == "throughput" && sj.contains("rates")) {
            double best = 0.0;
            for (const json& r : sj["rates"])
                if (r["result"] == "pass")
                    best = std::max(best, r["rate_hz"].get<double>());
            what += best > 0.0 ? " up to " + fixed(best / 1e6, 0) + " Msps" : "";
        }
        tested.push_back(what + (code == kPass ? " pass" : " FAIL"));
        if (code != kPass)
            issues.push_back(std::string(c) + " failed");
    }
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (const std::string& x : v)
            s += (s.empty() ? "" : "; ") + x;
        return s;
    };
    const std::string row = "| " + board + " (firmware " + firmware + ") | " + module + " | " +
                            os_name() + " | " + join(tested) +
                            " | not measured (yantra-hackrf level) | " +
                            (issues.empty() ? "none" : join(issues)) + " | " + today() + " |";
    j["devices_md_row"] = row;
    text << "DEVICES.md row:\n" << row << "\n";
    if (!o.out.empty()) {
        std::ofstream f(o.out, std::ios::app);
        if (!f)
            throw Fail{kUsage, "cannot write " + o.out};
        f << row << "\n";
    }
    return ok ? kPass : kFail;
}

int dispatch(const Options& o, const Environment& env, json& j, std::ostream& text)
{
    static const std::map<std::string, int (*)(const Options&, const Environment&, json&, std::ostream&)> commands = {
        {"list", cmd_list},         {"info", cmd_info},       {"selftest", cmd_selftest},
        {"settings", cmd_settings}, {"measure", cmd_measure}, {"capture", cmd_capture},
        {"noise", cmd_noise},       {"level", cmd_level},     {"ppm", cmd_ppm},
        {"throughput", cmd_throughput}, {"replug", cmd_replug}, {"report", cmd_report}};
    const auto it = commands.find(o.command);
    if (it == commands.end())
        throw Usage{"unknown command " + o.command};
    return it->second(o, env, j, text);
}

} // namespace

const char* usage() { return kUsageText; }

int run(const std::vector<std::string>& args, const Environment& env)
{
    std::ostream& out = *env.out;
    std::ostream& err = *env.err;
    if (!args.empty() && (args[0] == "--help" || args[0] == "-h" || args[0] == "help")) {
        out << kUsageText;
        return kPass;
    }
    Options o;
    try {
        o = parse(args);
    } catch (const Usage& u) {
        err << "error: " << u.what << "\n\n" << kUsageText;
        return kUsage;
    }
    json j{{"command", o.command}};
    std::ostringstream text;
    int code = kFail;
    std::string error;
    try {
        code = dispatch(o, env, j, text);
    } catch (const Usage& u) {
        code = kUsage;
        error = u.what;
    } catch (const Fail& f) {
        code = f.code;
        error = f.what;
    } catch (const std::exception& e) {
        code = kFail;
        error = e.what();
    }
    j["result"] = code == kPass ? "pass" : code == kNoDevice ? "no_device" : code == kUsage ? "usage" : "fail";
    j["exit_code"] = code;
    if (!error.empty())
        j["error"] = error;
    if (o.json) {
        out << j.dump(2) << "\n";
    } else {
        out << text.str();
        if (!error.empty())
            err << "error: " << error << "\n";
        if (code == kUsage && !error.empty())
            err << "(yantra-hackrf --help for the usage)\n";
    }
    return code;
}

} // namespace yantra::hackrf_tool
