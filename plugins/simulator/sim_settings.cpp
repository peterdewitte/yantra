// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings, sensors and presets of the simulator (RAD-40..45, RAD-61),
// described as data. Setting list adapted from yantra 68ea20a
// plugins/demo_radio/src/settings.cpp (scenario, network, fading, fault).
#include "sim_device.hpp"
#include "sim_scenarios.hpp"

#include <cmath>
#include <cstring>
#include <deque>

namespace yantra::simulator {
namespace {

using plugins::make_sensor;
using plugins::make_setting;

const char* const kGroupScenario = "Scenario";
const char* const kGroupSignal = "Signal";
const char* const kGroupPosition = "Position";
const char* const kGroupCalibration = "Calibration";
const char* const kGroupReceiver = "Receiver";

// The scenario items: "auto", then one per embedded scenario.
const std::vector<yr_enum_item>& scenario_items()
{
    static const std::vector<yr_enum_item> items = [] {
        static std::deque<std::string> texts; // stable storage for the labels
        std::vector<yr_enum_item> out;
        out.push_back({0, "auto", "As the floor plan says"});
        int64_t k = 1;
        for (const gen::ScenarioInfo& s : scenario_list()) {
            texts.push_back(s.id);
            const char* name = texts.back().c_str();
            texts.push_back(s.name.empty() ? s.id : s.name);
            out.push_back({k++, name, texts.back().c_str()});
        }
        return out;
    }();
    return items;
}

const yr_enum_item kNetworkItems[] = {{kNetScenario, "scenario", "As in the scenario"},
                                      {kNetTest00101, "test_001_01", "Test network 001-01"},
                                      {kNetFictitious90199, "fictitious_901_99",
                                       "Fictitious network 901-99"}};
const yr_enum_item kRateItems[] = {{0, "scenario", "As in the scenario"},
                                   {1, "r250k", "250 kHz"},
                                   {2, "r500k", "500 kHz"},
                                   {3, "r1m", "1 MHz"},
                                   {4, "r2m", "2 MHz"}};
const yr_enum_item kNoiseItems[] = {{kNoiseScenario, "scenario", "As in the scenario"},
                                    {kNoiseOff, "off", "No noise"},
                                    {kNoiseCustom, "custom", "Noise floor below"}};
const yr_enum_item kFadingItems[] = {{0, "scenario", "As in the scenario"},
                                     {1, "none", "None"},
                                     {2, "rayleigh", "Rayleigh (flat)"},
                                     {3, "rician", "Rician (flat)"},
                                     {4, "static", "Static multipath"},
                                     {5, "tu50", "Typical urban, 50 km/h"},
                                     {6, "ht200", "Hilly terrain, 200 km/h"}};
const yr_enum_item kPositionItems[] = {
    {kPosAuto, "auto", "Walk position during a walk test, else the mouse"},
    {kPosMouse, "mouse", "Mouse position"},
    {kPosWalk, "walk", "Walk-test position"},
    {kPosFixed, "fixed", "Fixed point"}};

template <std::size_t N>
void set_items(yr_setting_def& s, const yr_enum_item (&items)[N])
{
    s.items = items;
    s.item_count = N;
}

yr_setting_def number(const char* key, uint32_t type, const char* label, const char* unit,
                      const char* group, const char* help, int32_t order, double lo, double hi,
                      double step, double def)
{
    yr_setting_def s = make_setting(key, type, label, unit, group, help, order);
    s.minimum = lo;
    s.maximum = hi;
    s.step = step;
    s.default_value = type == YR_TYPE_UINT  ? yr_value_uint(static_cast<uint64_t>(def))
                      : type == YR_TYPE_INT ? yr_value_int(static_cast<int64_t>(def))
                                            : yr_value_double(def);
    return s;
}

void unavailable(yr_setting_def& s, bool off, const char* reason)
{
    if (off) {
        s.flags |= YR_SETTING_UNAVAILABLE;
        s.unavailable_reason = reason;
    }
}

} // namespace

std::vector<yr_setting_def> settings_of(const Settings& now)
{
    std::vector<yr_setting_def> d;
    // Scenario
    yr_setting_def sc = make_setting(
        "scenario", YR_TYPE_ENUM, "Scenario", "", kGroupScenario,
        "The simulated building and network (RAD-74). \"As the floor plan says\": the scenario "
        "of a bundled example plan, else the default scenario with the cells of your plan.",
        1);
    sc.items = scenario_items().data();
    sc.item_count = static_cast<uint32_t>(scenario_items().size());
    sc.default_value = yr_value_enum(0);
    d.push_back(sc);
    yr_setting_def net = make_setting("network", YR_TYPE_ENUM, "Network", "", kGroupScenario,
                                      "Country and network code (MCC-MNC) the simulated cells "
                                      "broadcast.",
                                      2);
    set_items(net, kNetworkItems);
    net.default_value = yr_value_enum(kNetScenario);
    d.push_back(net);
    d.push_back(number("playback_speed", YR_TYPE_DOUBLE, "Playback speed", "x", kGroupScenario,
                       "Signal seconds per real second: 1 is real time; faster runs the "
                       "scenario quicker (as far as the computer keeps up; samples it cannot "
                       "process in time are dropped and reported).",
                       3, 1.0, 10.0, 0.5, 1.0));
    d.push_back(number("record_rate_hz", YR_TYPE_DOUBLE, "Records per second", "1/s",
                       kGroupScenario,
                       "Measurement records per carrier and second of signal (1-10), so a "
                       "slowly moved mouse gives a dense trail.",
                       4, 1.0, 10.0, 1.0, 5.0));
    d.push_back(number("seed", YR_TYPE_UINT, "Random seed", "", kGroupScenario,
                       "Seed of noise, fading and shadowing; the same seed gives the same "
                       "signal. 0: the scenario's own seed.",
                       5, 0.0, 4294967295.0, 1.0, 0.0));
    yr_setting_def rate = make_setting("receiver_rate", YR_TYPE_ENUM, "Sample rate", "",
                                       kGroupScenario,
                                       "Sample rate of the simulated receiver; it sets the "
                                       "span measured at once.",
                                       6);
    set_items(rate, kRateItems);
    rate.default_value = yr_value_enum(0);
    d.push_back(rate);
    // Signal
    d.push_back(number("signal_offset_db", YR_TYPE_DOUBLE, "Signal offset", "dB", kGroupSignal,
                       "Raises or lowers every simulated cell (and so the SNR) by this much.", 1,
                       -60.0, 30.0, 0.5, 0.0));
    yr_setting_def noise = make_setting("receiver_noise", YR_TYPE_ENUM, "Receiver noise", "",
                                        kGroupSignal,
                                        "Thermal noise of the simulated receiver: as the "
                                        "scenario's noise figure, none, or the noise floor "
                                        "below.",
                                        2);
    set_items(noise, kNoiseItems);
    noise.default_value = yr_value_enum(kNoiseScenario);
    d.push_back(noise);
    yr_setting_def nf = number("noise_floor_dbm", YR_TYPE_DOUBLE, "Noise floor", "dBm",
                               kGroupSignal,
                               "Noise in the 18 kHz TETRA channel bandwidth (-131 dBm is a "
                               "perfect receiver at room temperature).",
                               3, -131.0, -72.0, 0.5, -124.0);
    unavailable(nf, now.receiver_noise != kNoiseCustom,
                "set Receiver noise to \"Noise floor below\" first");
    d.push_back(nf);
    d.push_back(number("oscillator_ppm", YR_TYPE_DOUBLE, "Oscillator error", "ppm", kGroupSignal,
                       "Frequency error of the simulated receiver's oscillator, added to the "
                       "scenario's.",
                       4, -20.0, 20.0, 0.1, 0.0));
    yr_setting_def fad = make_setting("fading", YR_TYPE_ENUM, "Fading", "", kGroupSignal,
                                      "Fading model of the radio channel.", 5);
    set_items(fad, kFadingItems);
    fad.default_value = yr_value_enum(0);
    d.push_back(fad);
    yr_setting_def gain = number("gain_db", YR_TYPE_DOUBLE, "Gain", "dB", kGroupSignal,
                                 "Gain of the simulated receiver in front of the converter: "
                                 "it moves the dBFS levels (part of the calibration key).",
                                 6, -20.0, 40.0, 1.0, 0.0);
    gain.flags = YR_SETTING_CALIBRATION;
    d.push_back(gain);
    yr_setting_def clip = make_setting("clipping", YR_TYPE_BOOL, "Clipping", "", kGroupSignal,
                                       "Clip the samples at full scale like a real converter "
                                       "(strong signals are then flagged OVERLOAD).",
                                       7);
    clip.flags = YR_SETTING_CALIBRATION;
    clip.default_value = yr_value_bool(0);
    d.push_back(clip);
    // Position
    yr_setting_def pos = make_setting(
        "position_source", YR_TYPE_ENUM, "Position source", "", kGroupPosition,
        "Where on the plan the simulated receiver is: the walk-test position while a walk "
        "runs and else the mouse (auto), always the mouse, the walk position, or a fixed "
        "point. Without a plan or position the scenario's own levels are used.",
        1);
    set_items(pos, kPositionItems);
    pos.default_value = yr_value_enum(kPosAuto);
    d.push_back(pos);
    yr_setting_def fx = number("fixed_x_m", YR_TYPE_DOUBLE, "Fixed point x", "m",
                               kGroupPosition, "x of the fixed point in the plan frame.", 2,
                               -10000.0, 10000.0, 0.01, 0.0);
    yr_setting_def fy = number("fixed_y_m", YR_TYPE_DOUBLE, "Fixed point y", "m",
                               kGroupPosition, "y of the fixed point in the plan frame.", 3,
                               -10000.0, 10000.0, 0.01, 0.0);
    unavailable(fx, now.position_source != kPosFixed, "set Position source to \"Fixed point\"");
    unavailable(fy, now.position_source != kPosFixed, "set Position source to \"Fixed point\"");
    d.push_back(fx);
    d.push_back(fy);
    // Calibration
    yr_setting_def cg = make_setting(
        "calibration_generator", YR_TYPE_BOOL, "Simulated signal generator", "",
        kGroupCalibration,
        "Practice calibration: instead of the scenario, one TETRA carrier (the scenario's "
        "first cell) at the level below, without fading, like a signal generator on the "
        "antenna input.",
        1);
    cg.default_value = yr_value_bool(0);
    d.push_back(cg);
    yr_setting_def cl = number("calibration_level_dbm", YR_TYPE_DOUBLE, "Generator level", "dBm",
                               kGroupCalibration,
                               "True level of the simulated signal generator.", 2, -120.0,
                               -20.0, 0.5, -60.0);
    unavailable(cl, !now.calibration_generator, "switch the simulated signal generator on");
    d.push_back(cl);
    // Receiver
    d.push_back(plugins::ms_max_power_setting(kGroupReceiver, 1));
    yr_setting_def th = number("threads", YR_TYPE_UINT, "Receiver threads", "", kGroupReceiver,
                               "Worker threads of the receiver (records do not depend on it).",
                               2, 1.0, 8.0, 1.0, 2.0);
    th.flags = YR_SETTING_ADVANCED | YR_SETTING_RESTART;
    d.push_back(th);
    yr_setting_def buf = number("device_buffer_s", YR_TYPE_DOUBLE, "Device buffer", "s",
                                kGroupReceiver,
                                "How far the simulated receiver may fall behind real time "
                                "before it drops samples (OVERFLOW, RAD-24).",
                                3, 0.1, 10.0, 0.1, 1.0);
    buf.flags = YR_SETTING_ADVANCED;
    d.push_back(buf);
    plugins::add_gsmtap_settings(d, kGroupReceiver, 4);
    // Actions
    yr_setting_def fault = make_setting("inject_fault", YR_TYPE_ACTION, "Simulate a noisy receiver",
                                        "", kGroupReceiver,
                                        "Raises the receiver noise by 20 dB until \"Clear the "
                                        "fault\" (a simulated fault, RAD-61).",
                                        10);
    d.push_back(fault);
    yr_setting_def clear = make_setting("clear_fault", YR_TYPE_ACTION, "Clear the fault", "",
                                        kGroupReceiver, "Ends the simulated noisy receiver.",
                                        11);
    d.push_back(clear);
    return d;
}

yr_status get_setting(const Settings& s, const char* key, yr_value* out, char* text,
                      uint32_t text_size)
{
    auto is = [key](const char* k) { return std::strcmp(key, k) == 0; };
    if (is("scenario"))
        *out = yr_value_enum(s.scenario);
    else if (is("network"))
        *out = yr_value_enum(s.network);
    else if (is("playback_speed"))
        *out = yr_value_double(s.playback_speed);
    else if (is("record_rate_hz"))
        *out = yr_value_double(s.record_rate_hz);
    else if (is("seed"))
        *out = yr_value_uint(s.seed);
    else if (is("receiver_rate"))
        *out = yr_value_enum(s.sample_rate);
    else if (is("signal_offset_db"))
        *out = yr_value_double(s.signal_offset_db);
    else if (is("receiver_noise"))
        *out = yr_value_enum(s.receiver_noise);
    else if (is("noise_floor_dbm"))
        *out = yr_value_double(s.noise_floor_dbm);
    else if (is("oscillator_ppm"))
        *out = yr_value_double(s.oscillator_ppm);
    else if (is("fading"))
        *out = yr_value_enum(s.fading);
    else if (is("gain_db"))
        *out = yr_value_double(s.gain_db);
    else if (is("clipping"))
        *out = yr_value_bool(s.clipping ? 1 : 0);
    else if (is("position_source"))
        *out = yr_value_enum(s.position_source);
    else if (is("fixed_x_m"))
        *out = yr_value_double(s.fixed_x_m);
    else if (is("fixed_y_m"))
        *out = yr_value_double(s.fixed_y_m);
    else if (is("calibration_generator"))
        *out = yr_value_bool(s.calibration_generator ? 1 : 0);
    else if (is("calibration_level_dbm"))
        *out = yr_value_double(s.calibration_level_dbm);
    else if (is(plugins::kMsMaxPowerKey))
        *out = yr_value_double(s.ms_max_power_dbm);
    else if (is("threads"))
        *out = yr_value_uint(s.threads);
    else if (is("device_buffer_s"))
        *out = yr_value_double(s.device_buffer_s);
    else if (const auto st = plugins::gsmtap_get(s.gsmtap, key, out, text, text_size))
        return *st;
    else
        return YR_ERR_NOT_FOUND;
    return YR_OK;
}

void store_setting(Settings& s, const yr_setting_def& def, const yr_value& v)
{
    const char* key = def.key;
    auto is = [key](const char* k) { return std::strcmp(key, k) == 0; };
    const double x = plugins::on_raster(def, plugins::number_of(v));
    const auto e = static_cast<uint32_t>(v.u.i64);
    if (is("scenario"))
        s.scenario = e;
    else if (is("network"))
        s.network = e;
    else if (is("playback_speed"))
        s.playback_speed = x;
    else if (is("record_rate_hz"))
        s.record_rate_hz = x;
    else if (is("seed"))
        s.seed = v.u.u64;
    else if (is("receiver_rate"))
        s.sample_rate = e;
    else if (is("signal_offset_db"))
        s.signal_offset_db = x;
    else if (is("receiver_noise"))
        s.receiver_noise = e;
    else if (is("noise_floor_dbm"))
        s.noise_floor_dbm = x;
    else if (is("oscillator_ppm"))
        s.oscillator_ppm = x;
    else if (is("fading"))
        s.fading = e;
    else if (is("gain_db"))
        s.gain_db = x;
    else if (is("clipping"))
        s.clipping = v.u.i64 != 0;
    else if (is("position_source"))
        s.position_source = e;
    else if (is("fixed_x_m"))
        s.fixed_x_m = x;
    else if (is("fixed_y_m"))
        s.fixed_y_m = x;
    else if (is("calibration_generator"))
        s.calibration_generator = v.u.i64 != 0;
    else if (is("calibration_level_dbm"))
        s.calibration_level_dbm = x;
    else if (is(plugins::kMsMaxPowerKey))
        s.ms_max_power_dbm = x;
    else if (is("threads"))
        s.threads = static_cast<uint64_t>(x);
    else if (is("device_buffer_s"))
        s.device_buffer_s = x;
    else
        plugins::gsmtap_set(s.gsmtap, key, v);
}

// --- sensors (RAD-45) ---------------------------------------------------------------

std::vector<yr_setting_def> sensors_of()
{
    const char* g = "Simulation";
    return {
        make_sensor("scenario_id", YR_TYPE_STRING, "Scenario", "", g,
                    "Id of the scenario being simulated.", 1),
        make_sensor("simulating", YR_TYPE_STRING, "Simulating", "", g,
                    "Which plan the simulation follows (bundled example plan id, your own "
                    "plan, or none); debug window only.",
                    2),
        make_sensor("floor", YR_TYPE_STRING, "Floor", "", g,
                    "Floor id of the simulated receiver position, \"\" without one.", 3),
        make_sensor("position", YR_TYPE_STRING, "Position", "", g,
                    "Where the simulated receiver is and where that comes from.", 4),
        make_sensor("true_levels", YR_TYPE_STRING, "True levels", "", g,
                    "Ground truth: the level each cell's signal has in the samples now "
                    "(with fading), dBm.",
                    5),
        make_sensor("estimated_levels", YR_TYPE_STRING, "Estimated levels", "", g,
                    "The multi-wall estimate of every cell at the position (local mean, "
                    "RAD-73), dBm.",
                    6),
        make_sensor("true_noise_floor_dbm", YR_TYPE_DOUBLE, "True noise floor", "dBm", g,
                    "Ground truth: receiver noise in the 18 kHz channel bandwidth.", 7),
        make_sensor("clipped", YR_TYPE_UINT, "Clipped samples", "", g,
                    "Samples clipped at full scale since the stream started.", 8),
        make_sensor("generator_state", YR_TYPE_STRING, "Generator state", "", g,
                    "Per cell: on air, TDMA time (hyperframe-multiframe-frame) and bursts "
                    "sent.",
                    9),
        make_sensor("signal_time_s", YR_TYPE_DOUBLE, "Signal time", "s", g,
                    "Seconds of signal generated since the stream started.", 10),
        make_sensor("receiver_load", YR_TYPE_DOUBLE, "Receiver load", "", g,
                    "Processing time per signal second of generator and receiver (1: the "
                    "computer just keeps up).",
                    11),
        make_sensor("true_offset_db", YR_TYPE_DOUBLE, "True calibration offset", "dB", g,
                    "Ground truth of the level calibration: dBm = dBFS + this (what a "
                    "practice calibration should find).",
                    12),
    };
}

yr_status read_sensor(const Device& d, const char* key, yr_value* out, char* text,
                      uint32_t text_size)
{
    const Snapshot& s = d.snapshot;
    auto is = [key](const char* k) { return std::strcmp(key, k) == 0; };
    auto str = [&](const std::string& v) { return plugins::string_value(v, out, text, text_size); };
    if (is("scenario_id"))
        return str(s.scenario);
    if (is("simulating"))
        return str(s.simulating);
    if (is("floor"))
        return str(s.floor);
    if (is("position"))
        return str(s.position);
    if (is("true_levels"))
        return str(s.true_levels);
    if (is("estimated_levels"))
        return str(s.estimated_levels);
    if (is("generator_state"))
        return str(s.generator);
    if (is("true_noise_floor_dbm"))
        *out = s.valid ? yr_value_double(std::isfinite(s.noise_dbm) ? s.noise_dbm : -999.0)
                       : yr_value_none();
    else if (is("clipped"))
        *out = yr_value_uint(s.clipped);
    else if (is("signal_time_s"))
        *out = yr_value_double(s.signal_s);
    else if (is("receiver_load"))
        *out = yr_value_double(d.load.load());
    else if (is("true_offset_db"))
        *out = s.valid ? yr_value_double(s.offset_db) : yr_value_none();
    else
        return YR_ERR_NOT_FOUND;
    return YR_OK;
}

// --- presets (RAD-43) ---------------------------------------------------------------

const std::vector<yr_preset>& presets()
{
    static const std::vector<yr_preset> p = {
        {sizeof(yr_preset), 0, "default", "Default",
         "Every setting back to its default: the plan's scenario in real time."},
        {sizeof(yr_preset), 0, "weak_signal", "Weak signal",
         "All cells 30 dB weaker: decoding near the sensitivity limit."},
        {sizeof(yr_preset), 0, "noisy_receiver", "Noisy receiver",
         "Noise floor -105 dBm in 18 kHz: a receiver with a poor noise figure."},
        {sizeof(yr_preset), 0, "overload", "Overload",
         "Gain 40 dB with clipping: strong cells are flagged OVERLOAD."},
        {sizeof(yr_preset), 0, "fast_playback", "Fast playback",
         "Four times real time, two records per second."},
    };
    return p;
}

bool apply_preset(Settings& s, const char* id)
{
    const std::string p = id;
    if (p == "default")
        s = Settings{};
    else if (p == "weak_signal")
        s.signal_offset_db = -30.0;
    else if (p == "noisy_receiver") {
        s.receiver_noise = kNoiseCustom;
        s.noise_floor_dbm = -105.0;
    } else if (p == "overload") {
        s.gain_db = 40.0;
        s.clipping = true;
    } else if (p == "fast_playback") {
        s.playback_speed = 4.0;
        s.record_rate_hz = 2.0;
    } else
        return false;
    return true;
}

} // namespace yantra::simulator
