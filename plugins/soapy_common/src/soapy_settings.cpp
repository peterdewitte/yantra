// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings generated from what the SoapySDR module reports (RAD-51):
// sample rate, baseband filter, every gain element, AGC, antenna port,
// frequency correction, the 10 MHz reference (RAD-53), the module's own
// settings and sensors. A control the module does not offer is shown
// greyed out with the reason (RAD-51), never hidden behind a fixed list.
#include "soapy_device.hpp"

#include <yantra/radio.hpp>
#include <yantra/receiver/dependencies.hpp>

#include <SoapySDR/Formats.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace yantra::soapy {
namespace {

using plugins::make_sensor;
using plugins::make_setting;

constexpr const char* kGroupRf = "RF";
constexpr const char* kGroupGain = "Gain";
constexpr const char* kGroupReceiver = "Receiver";
constexpr const char* kGroupModule = "Module";

// Rates tried on a module with a continuous range; the profile's range
// (Profile::min_rate_hz..max_rate_hz) picks from them. The generic range
// is 0.2-10 Msps: below 200 kHz no TETRA span is left, above 10 Msps one
// core does not keep up with a band search.
constexpr double kCandidateRates[] = {250e3, 500e3, 1e6,  1.024e6, 1.2e6, 1.4e6, 1.8e6, 2e6,
                                      2.048e6, 2.4e6, 2.5e6, 3e6, 3.2e6, 4e6, 5e6, 6e6,
                                      8e6,   10e6,  12.5e6, 16e6, 20e6};

const Profile& profile_of(const Device& d) { return d.plugin->options->profile; }

bool same_name(const std::string& a, const char* b)
{
    if (a.size() != std::strlen(b))
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string mhz_text(double hz)
{
    return radio::number_text(std::round(hz / 1e3) / 1e3) + " MHz";
}

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool in_ranges(const SoapySDR::RangeList& ranges, double v)
{
    for (const SoapySDR::Range& r : ranges)
        if (v >= r.minimum() - 1.0 && v <= r.maximum() + 1.0)
            return true;
    return false;
}

std::string item_name(const std::string& text, std::size_t index)
{
    std::string out;
    for (char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            out += static_cast<char>(std::tolower(u));
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    if (out.empty() || radio::is_out_of_scope_name(out))
        out = "item_" + std::to_string(index);
    return out;
}

void unavailable(yr_setting_def& s, const char* reason)
{
    s.flags |= YR_SETTING_UNAVAILABLE;
    s.unavailable_reason = reason;
}

yr_setting_def number(const char* key, uint32_t type, const char* label, const char* unit,
                      const char* group, const char* help, int32_t order, double lo, double hi,
                      double step, double def)
{
    yr_setting_def s = make_setting(key, type, label, unit, group, help, order);
    s.minimum = lo;
    s.maximum = hi;
    s.step = step;
    s.default_value = type == YR_TYPE_UINT ? yr_value_uint(static_cast<uint64_t>(def)) : yr_value_double(def);
    return s;
}

// An enum over texts; the item values are the indices.
yr_setting_def enum_of(SettingTable& t, const char* key, const char* label, const char* group,
                       const char* help, int32_t order, const std::vector<std::string>& labels,
                       uint32_t def)
{
    yr_setting_def s = make_setting(key, YR_TYPE_ENUM, label, "", group, help, order);
    std::vector<yr_enum_item> items;
    std::vector<std::string> used;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        std::string name = item_name(labels[i], i);
        if (std::find(used.begin(), used.end(), name) != used.end())
            name = "item_" + std::to_string(i);
        used.push_back(name);
        items.push_back({static_cast<int64_t>(i), t.keep(name), t.keep(labels[i])});
    }
    t.items.push_back(std::move(items));
    s.items = t.items.back().data();
    s.item_count = static_cast<uint32_t>(t.items.back().size());
    s.default_value = yr_value_enum(def);
    return s;
}

double gain_default(const GainElement& g)
{
    // An amplifier stage (a single on step) starts off; the others in the
    // middle of their range, on their raster.
    if (g.as_switch || lower(g.name).find("amp") != std::string::npos)
        return g.min;
    double v = (g.min + g.max) / 2.0;
    if (g.step > 0.0)
        v = g.min + std::floor((v - g.min) / g.step) * g.step;
    return v;
}

uint32_t default_rate(const Caps& c, double want)
{
    uint32_t best = 0;
    for (uint32_t i = 0; i < c.rates.size(); ++i)
        if (std::fabs(c.rates[i] - want) < std::fabs(c.rates[best] - want))
            best = i;
    return best;
}

std::string text_of_arg(const SoapySDR::ArgInfo& a)
{
    return a.value;
}

} // namespace

double total_gain_db(const Settings& s)
{
    double sum = 0.0;
    for (double g : s.gains)
        sum += g;
    return sum;
}

void read_caps(Device& d)
{
    SoapySDR::Device& sdr = *d.sdr;
    const Profile& pr = profile_of(d);
    Caps c;
    const int rx = SOAPY_SDR_RX;
    std::vector<double> listed = sdr.listSampleRates(rx, 0);
    const SoapySDR::RangeList ranges = sdr.getSampleRateRange(rx, 0);
    bool continuous = false;
    for (const SoapySDR::Range& r : ranges)
        continuous = continuous || r.maximum() - r.minimum() > 1.0;
    if (continuous)
        for (double x : kCandidateRates)
            if (in_ranges(ranges, x))
                listed.push_back(x);
    std::sort(listed.begin(), listed.end());
    listed.erase(std::unique(listed.begin(), listed.end()), listed.end());
    for (double x : listed)
        if (x >= pr.min_rate_hz - 1.0 && x <= pr.max_rate_hz + 1.0)
            c.rates.push_back(x);
    if (c.rates.empty())
        throw std::runtime_error("the module offers no sample rate from " +
                                 radio::number_text(pr.min_rate_hz / 1e6) + " to " +
                                 radio::number_text(pr.max_rate_hz / 1e6) + " Msps");
    c.bandwidths = sdr.listBandwidths(rx, 0);
    for (const std::string& g : sdr.listGains(rx, 0)) {
        const SoapySDR::Range r = sdr.getGainRange(rx, 0, g);
        GainElement e{g, r.minimum(), r.maximum(), r.step(), "", false};
        e.key = "gain_" + item_name(g, c.gains.size());
        if (radio::is_out_of_scope_name(e.key))
            e.key = "gain_" + std::to_string(c.gains.size());
        for (const GainControl& gc : pr.gains)
            if (same_name(g, gc.module_name)) {
                e.key = gc.key;
                e.as_switch = gc.as_switch;
            }
        c.gains.push_back(std::move(e));
    }
    c.agc = sdr.hasGainMode(rx, 0);
    c.antennas = sdr.listAntennas(rx, 0);
    c.frequency_correction = sdr.hasFrequencyCorrection(rx, 0);
    c.clock_sources = sdr.listClockSources();
    for (const SoapySDR::ArgInfo& a : sdr.getSettingInfo()) {
        ModuleArg m;
        m.module_key = a.key;
        m.key = arg_setting_key(a.key);
        if (m.key.empty())
            continue;
        m.default_text = text_of_arg(a);
        m.power = lower(a.key).find("bias") != std::string::npos;
        for (const ArgControl& ac : pr.args)
            if (a.key == ac.module_key) {
                m.control = &ac;
                m.key = ac.key;
                m.power = ac.power;
            }
        if (!a.options.empty()) {
            m.type = YR_TYPE_ENUM;
            m.options = a.options;
        } else if (a.type == SoapySDR::ArgInfo::BOOL) {
            m.type = YR_TYPE_BOOL;
        } else if (a.type == SoapySDR::ArgInfo::INT) {
            m.type = YR_TYPE_INT;
        } else if (a.type == SoapySDR::ArgInfo::FLOAT) {
            m.type = YR_TYPE_DOUBLE;
        } else {
            m.type = YR_TYPE_STRING;
        }
        c.args.push_back(std::move(m));
    }
    for (const std::string& s : sdr.listSensors()) {
        const std::string key = sensor_setting_key(s);
        if (!key.empty())
            c.sensors.emplace_back(s, key);
    }
    const SoapySDR::RangeList f = sdr.getFrequencyRange(rx, 0);
    if (f.empty())
        throw std::runtime_error("the module reports no frequency range");
    c.first_hz = f.front().minimum();
    c.last_hz = f.front().maximum();
    for (const SoapySDR::Range& r : f) {
        c.first_hz = std::min(c.first_hz, r.minimum());
        c.last_hz = std::max(c.last_hz, r.maximum());
    }
    std::ostringstream hw;
    hw << "driver key=" << sdr.getDriverKey() << "\nhardware=" << sdr.getHardwareKey();
    for (const auto& [k, v] : sdr.getHardwareInfo())
        hw << '\n' << k << '=' << v;
    c.hardware_info = hw.str();
    c.hardware_key = sdr.getHardwareKey();
    for (const auto& [k, v] : sdr.getHardwareInfo())
        c.hardware[k] = v;
    d.caps = std::move(c);
}

Settings default_settings(const Options& o, const Caps& c)
{
    Settings s;
    s.rate = default_rate(c, o.profile.default_rate_hz);
    s.offset_khz = o.profile.offset_tuning_hz / 1e3;
    for (const GainElement& g : c.gains)
        s.gains.push_back(gain_default(g));
    for (const ModuleArg& a : c.args) {
        std::string v = a.default_text;
        if (a.type == YR_TYPE_ENUM && std::find(a.options.begin(), a.options.end(), v) == a.options.end())
            v = a.options.front();
        if (a.power)
            v = "false"; // antenna port power starts off, whatever the module says
        s.args.push_back(v);
    }
    for (uint32_t i = 0; i < c.antennas.size(); ++i)
        if (lower(c.antennas[i]).find("rx") != std::string::npos) {
            s.antenna = i;
            break;
        }
    return s;
}

void build_table(Device& d)
{
    const Caps& c = d.caps;
    SettingTable& t = d.table;
    const Profile& pr = profile_of(d);
    const Settings def = default_settings(*d.plugin->options, c);
    std::vector<yr_setting_def>& out = t.defs;

    // RF
    out.push_back(number("tune_mhz", YR_TYPE_DOUBLE, "Centre frequency", "MHz", kGroupRf,
                         "Tuned when the measurement plan names no carrier: the middle of the "
                         "span searched for cells. With carriers, the receiver tunes to their "
                         "middle.",
                         1, std::ceil(c.first_hz / 1e3) / 1e3, std::floor(c.last_hz / 1e3) / 1e3,
                         0.0125, std::clamp(392.5, c.first_hz / 1e6, c.last_hz / 1e6)));
    std::vector<std::string> rates;
    for (double r : c.rates)
        rates.push_back(radio::number_text(r / 1e6) + " Msps");
    yr_setting_def rate = enum_of(t, "receiver_rate", "Sample rate", kGroupRf,
                                  t.keep("Sample rate of the receiver (" +
                                         radio::number_text(pr.min_rate_hz / 1e6) + " to " +
                                         radio::number_text(pr.max_rate_hz / 1e6) +
                                         " Msps); 80 % of it is the span measured at once. "
                                         "Higher rates take more processor time."),
                                  2, rates, def.rate);
    rate.flags |= YR_SETTING_RESTART;
    out.push_back(rate);
    std::vector<std::string> bws{"Automatic (module default)"};
    for (double b : c.bandwidths)
        bws.push_back(mhz_text(b));
    yr_setting_def bw = enum_of(t, "bandwidth", "Baseband filter", kGroupRf,
                                "Analogue filter in front of the converter; automatic lets the "
                                "module choose for the sample rate.",
                                3, bws, 0);
    bw.flags |= YR_SETTING_RESTART | YR_SETTING_ADVANCED;
    if (c.bandwidths.empty())
        unavailable(bw, "the module does not offer a choice of filters");
    out.push_back(bw);
    std::vector<std::string> ants = c.antennas.empty() ? std::vector<std::string>{"Default"} : c.antennas;
    yr_setting_def ant = enum_of(t, "antenna", "Antenna port", kGroupRf,
                                 "Receive port of the device.", 4, ants, def.antenna);
    ant.flags |= YR_SETTING_CALIBRATION;
    if (c.antennas.size() < 2)
        unavailable(ant, "the device has one receive port");
    out.push_back(ant);
    yr_setting_def ppm = number("ppm", YR_TYPE_DOUBLE, "Frequency correction", "ppm", kGroupRf,
                                c.frequency_correction
                                    ? "Oscillator error of the device, corrected by the module."
                                    : "Oscillator error of the device; the module has no correction, "
                                      "so the receiver tunes off by this much.",
                                5, -100.0, 100.0, 0.1, 0.0);
    ppm.flags |= YR_SETTING_ADVANCED;
    out.push_back(ppm);
    std::vector<std::string> clocks = c.clock_sources.empty() ? std::vector<std::string>{"Internal"}
                                                              : c.clock_sources;
    yr_setting_def clk = enum_of(t, "clock_source", "Reference clock", kGroupRf,
                                 "Frequency reference of the device: internal oscillator or an "
                                 "external 10 MHz reference (RAD-53).",
                                 6, clocks, 0);
    clk.flags |= YR_SETTING_RESTART | YR_SETTING_ADVANCED;
    if (c.clock_sources.size() < 2)
        unavailable(clk, "the module offers no external reference for this device");
    out.push_back(clk);
    if (pr.offset_tuning_hz > 0.0) {
        yr_setting_def off = number("offset_khz", YR_TYPE_DOUBLE, "Offset tuning", "kHz", kGroupRf,
                                    "The receiver tunes this far beside the carriers so that "
                                    "none sits on the DC spike in the middle of the span. 0 "
                                    "tunes to their middle.",
                                    7, 0.0, 2000.0, 5.0, pr.offset_tuning_hz / 1e3);
        off.flags |= YR_SETTING_RESTART | YR_SETTING_ADVANCED;
        out.push_back(off);
    }

    // Gain
    yr_setting_def agc = make_setting("agc", YR_TYPE_BOOL, "Automatic gain", "", kGroupGain,
                                      "The device sets its gain itself. Levels of a calibrated "
                                      "device are only right at fixed gains.",
                                      1);
    agc.default_value = yr_value_bool(0);
    agc.flags |= YR_SETTING_CALIBRATION;
    if (!c.agc)
        unavailable(agc, "the module does not offer automatic gain");
    out.push_back(agc);
    for (std::size_t i = 0; i < c.gains.size(); ++i) {
        const GainElement& g = c.gains[i];
        const GainControl* gc = nullptr;
        for (const GainControl& x : pr.gains)
            if (same_name(g.name, x.module_name))
                gc = &x;
        const std::string range = radio::number_text(g.min) + " to " + radio::number_text(g.max) + " dB";
        const char* label = gc != nullptr ? gc->label : t.keep(g.name + " gain");
        const char* help = gc != nullptr ? t.keep(std::string(gc->help) + " (" + range + ")")
                                         : t.keep("Gain element " + g.name + " of the device, " + range + ".");
        yr_setting_def s;
        if (g.as_switch) {
            s = make_setting(t.keep(g.key), YR_TYPE_BOOL, label, "", kGroupGain, help,
                             static_cast<int32_t>(2 + i));
            s.default_value = yr_value_bool(0);
        } else {
            s = number(t.keep(g.key), YR_TYPE_DOUBLE, label, "dB", kGroupGain, help,
                       static_cast<int32_t>(2 + i), g.min, g.max, g.step, def.gains[i]);
        }
        s.flags |= YR_SETTING_CALIBRATION;
        out.push_back(s);
    }
    // Gain elements the profile names but the module does not report.
    for (const GainControl& gc : pr.gains) {
        if (std::any_of(c.gains.begin(), c.gains.end(),
                        [&](const GainElement& g) { return same_name(g.name, gc.module_name); }))
            continue;
        yr_setting_def s = make_setting(gc.key, gc.as_switch ? YR_TYPE_BOOL : YR_TYPE_DOUBLE, gc.label,
                                        gc.as_switch ? "" : "dB", kGroupGain, gc.help, 20);
        s.default_value = gc.as_switch ? yr_value_bool(0) : yr_value_double(0.0);
        s.flags |= YR_SETTING_CALIBRATION;
        unavailable(s, gc.missing);
        out.push_back(s);
    }

    // Module settings (RAD-51): the module's own list.
    int32_t order = 1;
    for (const ModuleArg& a : c.args) {
        const bool power = a.power;
        const char* key = t.keep(a.key);
        const char* label = a.control != nullptr
                                ? a.control->label
                                : t.keep(power ? "Antenna port power (" + a.module_key + ")" : a.module_key);
        const char* help = a.control != nullptr ? a.control->help
                                                : t.keep("Setting " + a.module_key + " of the module.");
        const char* group = a.control != nullptr ? a.control->group : kGroupModule;
        yr_setting_def s;
        if (a.type == YR_TYPE_ENUM) {
            const auto it = std::find(a.options.begin(), a.options.end(), a.default_text);
            s = enum_of(t, key, label, group, help, order, a.options,
                        static_cast<uint32_t>(it == a.options.end() ? 0 : it - a.options.begin()));
        } else {
            s = make_setting(key, a.type, label, "", group, help, order);
            if (a.type == YR_TYPE_BOOL)
                s.default_value = yr_value_bool(!power && a.default_text == "true" ? 1 : 0);
            else if (a.type == YR_TYPE_INT)
                s.default_value = yr_value_int(std::strtoll(a.default_text.c_str(), nullptr, 10));
            else if (a.type == YR_TYPE_DOUBLE)
                s.default_value = yr_value_double(std::strtod(a.default_text.c_str(), nullptr));
            else
                s.default_value = yr_value_string(t.keep(a.default_text));
        }
        s.flags |= YR_SETTING_ADVANCED;
        if (power) {
            s.flags |= YR_SETTING_WARNING;
            s.warning = "This powers the antenna port (bias tee). Only switch it on with an "
                        "active antenna or amplifier that needs it: it can damage other equipment.";
        }
        out.push_back(s);
        ++order;
    }
    for (const ArgControl& ac : pr.args) {
        if (std::any_of(c.args.begin(), c.args.end(), [&](const ModuleArg& a) { return a.control == &ac; }))
            continue;
        yr_setting_def s = make_setting(ac.key, YR_TYPE_BOOL, ac.label, "", ac.group, ac.help, order++);
        s.default_value = yr_value_bool(0);
        s.flags |= YR_SETTING_ADVANCED;
        unavailable(s, ac.missing);
        out.push_back(s);
    }

    // Receiver
    out.push_back(number("record_rate_hz", YR_TYPE_DOUBLE, "Records per second", "1/s",
                         kGroupReceiver, "Measurement records per carrier and second (1-10).", 1,
                         1.0, 10.0, 1.0, 5.0));
    yr_setting_def th = number("threads", YR_TYPE_UINT, "Receiver threads", "", kGroupReceiver,
                               "Processor threads of the receiver.", 2, 1.0, 8.0, 1.0, 2.0);
    th.flags |= YR_SETTING_RESTART | YR_SETTING_ADVANCED;
    out.push_back(th);
    out.push_back(plugins::ms_max_power_setting(kGroupReceiver, 3));
    if (pr.c1) {
        yr_setting_def on = make_setting(
            "c1_enabled", YR_TYPE_BOOL, "C1 from a level offset", "", kGroupReceiver,
            "Adds C1 (EN 300 392-2 clause 23.3) to the cell records, from the dBFS level plus the "
            "level offset below. The levels themselves stay in dBFS. Off: no C1 (Yantra "
            "calculates it from its own calibration).",
            4);
        on.default_value = yr_value_bool(0);
        out.push_back(on);
        out.push_back(number("c1_level_offset_db", YR_TYPE_DOUBLE, "Level offset for C1", "dB",
                             kGroupReceiver,
                             "dBm at the antenna connector = dBFS + this offset, for the gains in "
                             "use (from a calibration against a signal generator). Only used "
                             "for C1.",
                             5, -200.0, 50.0, 0.1, 0.0));
    }
    if (pr.scan)
        out.push_back(number("scan_dwell_s", YR_TYPE_DOUBLE, "Scan dwell time", "s", kGroupReceiver,
                             "A band scan wider than the span retunes in steps and measures "
                             "this long at each step.",
                             6, 0.2, 10.0, 0.1, 1.0));
    plugins::add_gsmtap_settings(out, kGroupReceiver, 10);

    // Sensors
    t.sensors.push_back(make_sensor("module", YR_TYPE_STRING, "SoapySDR module", "", kGroupModule,
                                    "The module, its version and vendor library (RAD-52).", 1));
    t.sensors.push_back(make_sensor("hardware", YR_TYPE_STRING, "Hardware", "", kGroupModule,
                                    "What the module reports about the device.", 2));
    t.sensors.push_back(make_sensor("overflow_count", YR_TYPE_UINT, "Overflows", "", kGroupReceiver,
                                    "Device buffer overflows of the current stream (RAD-24).", 3));
    t.sensors.push_back(make_sensor("gain_state", YR_TYPE_STRING, "Gains", "", kGroupGain,
                                    "The gains and amplifier state in use.", 4));
    t.sensors.push_back(make_sensor("clipped_percent", YR_TYPE_DOUBLE, "Clipping", "%", kGroupReceiver,
                                    "Share of the values at full scale in the last second of the "
                                    "stream (OVERLOAD).",
                                    5));
    t.sensors.push_back(make_sensor("overload", YR_TYPE_STRING, "Saturation warning", "", kGroupReceiver,
                                    "What to do when the receiver clips or saturates.", 6));
    t.sensors.push_back(make_sensor("noise_floor_dbfs", YR_TYPE_DOUBLE, "Noise floor", "dBFS",
                                    kGroupReceiver,
                                    "Level of the quietest channels (25 kHz) of the span, "
                                    "while streaming.",
                                    7));
    t.sensors.push_back(make_sensor("receiver_load", YR_TYPE_DOUBLE, "Receiver load", "", kGroupReceiver,
                                    "Processor time of the receiver per second of signal (0..1; "
                                    "above 1 it cannot keep up and drops).",
                                    8));
    t.sensors.push_back(make_sensor("tuned_mhz", YR_TYPE_DOUBLE, "Tuned frequency", "MHz", kGroupReceiver,
                                    "Where the device is tuned while streaming (with offset "
                                    "tuning beside the carriers).",
                                    9));
    t.sensors.push_back(make_sensor("build_info", YR_TYPE_STRING, "Build info", "", kGroupModule,
                                    "The libraries built into the plugin with their versions "
                                    "(RAD-136); their licences are in the licence bundle.",
                                    99));
    int32_t io = 1;
    for (const InfoSensor& is : pr.info_sensors)
        t.sensors.push_back(make_sensor(is.key, YR_TYPE_STRING, is.label, "", kGroupModule, is.help, 100 + io++));
    int32_t so = 10;
    for (const auto& [module_key, key] : c.sensors)
        t.sensors.push_back(make_sensor(t.keep(key), YR_TYPE_STRING, t.keep(module_key), "",
                                        kGroupModule, "Sensor of the module.", so++));
    d.settings = def;
}

yr_status get_setting(const Device& d, const char* key, yr_value* out, char* text, uint32_t size)
{
    const Settings& s = d.settings;
    auto is = [key](const char* k) { return std::strcmp(key, k) == 0; };
    if (is("tune_mhz"))
        *out = yr_value_double(s.tune_mhz);
    else if (is("receiver_rate"))
        *out = yr_value_enum(s.rate);
    else if (is("bandwidth"))
        *out = yr_value_enum(s.bandwidth);
    else if (is("antenna"))
        *out = yr_value_enum(s.antenna);
    else if (is("ppm"))
        *out = yr_value_double(s.ppm);
    else if (is("clock_source"))
        *out = yr_value_enum(s.clock_source);
    else if (is("agc"))
        *out = yr_value_bool(s.agc ? 1 : 0);
    else if (is("record_rate_hz"))
        *out = yr_value_double(s.record_rate_hz);
    else if (is("threads"))
        *out = yr_value_uint(s.threads);
    else if (is(plugins::kMsMaxPowerKey))
        *out = yr_value_double(s.ms_max_power_dbm);
    else if (is("offset_khz") && profile_of(d).offset_tuning_hz > 0.0)
        *out = yr_value_double(s.offset_khz);
    else if (is("c1_enabled") && profile_of(d).c1)
        *out = yr_value_bool(s.c1_enabled ? 1 : 0);
    else if (is("c1_level_offset_db") && profile_of(d).c1)
        *out = yr_value_double(s.c1_offset_db);
    else if (is("scan_dwell_s") && profile_of(d).scan)
        *out = yr_value_double(s.scan_dwell_s);
    else if (auto g = plugins::gsmtap_get(s.gsmtap, key, out, text, size))
        return *g;
    else {
        for (std::size_t i = 0; i < d.caps.gains.size(); ++i) {
            const GainElement& e = d.caps.gains[i];
            if (!is(e.key.c_str()))
                continue;
            *out = e.as_switch ? yr_value_bool(s.gains[i] > e.min + 0.5 ? 1 : 0) : yr_value_double(s.gains[i]);
            return YR_OK;
        }
        for (std::size_t i = 0; i < d.caps.args.size(); ++i) {
            const ModuleArg& a = d.caps.args[i];
            if (!is(a.key.c_str()))
                continue;
            const std::string& v = s.args[i];
            switch (a.type) {
            case YR_TYPE_ENUM: {
                const auto it = std::find(a.options.begin(), a.options.end(), v);
                *out = yr_value_enum(it == a.options.end() ? 0 : it - a.options.begin());
                return YR_OK;
            }
            case YR_TYPE_BOOL:
                *out = yr_value_bool(v == "true" ? 1 : 0);
                return YR_OK;
            case YR_TYPE_INT:
                *out = yr_value_int(std::strtoll(v.c_str(), nullptr, 10));
                return YR_OK;
            case YR_TYPE_DOUBLE:
                *out = yr_value_double(std::strtod(v.c_str(), nullptr));
                return YR_OK;
            default:
                return plugins::string_value(v, out, text, size);
            }
        }
        // A setting the profile names but the module lacks (UNAVAILABLE).
        if (const yr_setting_def* def = plugins::find_setting(d.table.defs, key);
            def != nullptr && (def->flags & YR_SETTING_UNAVAILABLE) != 0) {
            *out = def->default_value;
            return YR_OK;
        }
        return YR_ERR_NOT_FOUND;
    }
    return YR_OK;
}

void store_setting(Device& d, const yr_setting_def& def, const yr_value& v)
{
    Settings& s = d.settings;
    auto is = [&def](const char* k) { return std::strcmp(def.key, k) == 0; };
    if (is("tune_mhz"))
        s.tune_mhz = plugins::on_raster(def, v.u.f64);
    else if (is("receiver_rate"))
        s.rate = static_cast<uint32_t>(v.u.i64);
    else if (is("bandwidth"))
        s.bandwidth = static_cast<uint32_t>(v.u.i64);
    else if (is("antenna"))
        s.antenna = static_cast<uint32_t>(v.u.i64);
    else if (is("ppm"))
        s.ppm = plugins::on_raster(def, v.u.f64);
    else if (is("clock_source"))
        s.clock_source = static_cast<uint32_t>(v.u.i64);
    else if (is("agc"))
        s.agc = v.u.i64 != 0;
    else if (is("record_rate_hz"))
        s.record_rate_hz = plugins::on_raster(def, v.u.f64);
    else if (is("threads"))
        s.threads = v.u.u64;
    else if (is(plugins::kMsMaxPowerKey))
        s.ms_max_power_dbm = plugins::on_raster(def, v.u.f64);
    else if (is("offset_khz"))
        s.offset_khz = plugins::on_raster(def, v.u.f64);
    else if (is("c1_enabled"))
        s.c1_enabled = v.u.i64 != 0;
    else if (is("c1_level_offset_db"))
        s.c1_offset_db = plugins::on_raster(def, v.u.f64);
    else if (is("scan_dwell_s"))
        s.scan_dwell_s = plugins::on_raster(def, v.u.f64);
    else if (plugins::gsmtap_set(s.gsmtap, def.key, v))
        return;
    else {
        for (std::size_t i = 0; i < d.caps.gains.size(); ++i) {
            const GainElement& g = d.caps.gains[i];
            if (!is(g.key.c_str()))
                continue;
            if (g.as_switch)
                s.gains[i] = v.u.i64 != 0 ? g.max : g.min;
            else
                s.gains[i] = plugins::on_raster(def, v.u.f64);
            return;
        }
        for (std::size_t i = 0; i < d.caps.args.size(); ++i) {
            const ModuleArg& a = d.caps.args[i];
            if (!is(a.key.c_str()))
                continue;
            switch (a.type) {
            case YR_TYPE_ENUM:
                s.args[i] = a.options.at(static_cast<std::size_t>(v.u.i64));
                break;
            case YR_TYPE_BOOL:
                s.args[i] = v.u.i64 != 0 ? "true" : "false";
                break;
            case YR_TYPE_INT:
                s.args[i] = std::to_string(v.u.i64);
                break;
            case YR_TYPE_DOUBLE:
                s.args[i] = radio::number_text(v.u.f64);
                break;
            default:
                s.args[i] = v.u.str != nullptr ? v.u.str : "";
            }
            return;
        }
    }
}

yr_status read_sensor(Device& d, const char* key, yr_value* out, char* text, uint32_t size)
{
    if (std::strcmp(key, "module") == 0)
        return plugins::string_value(d.module_text, out, text, size);
    if (std::strcmp(key, "hardware") == 0)
        return plugins::string_value(d.caps.hardware_info, out, text, size);
    if (std::strcmp(key, "overflow_count") == 0) {
        *out = yr_value_uint(d.overflows.load());
        return YR_OK;
    }
    if (std::strcmp(key, "gain_state") == 0) {
        std::string v;
        if (d.settings.agc) {
            v = "automatic";
        } else {
            for (std::size_t i = 0; i < d.caps.gains.size(); ++i) {
                const GainElement& g = d.caps.gains[i];
                v += (v.empty() ? "" : ", ") + g.name + " ";
                v += g.as_switch ? (d.settings.gains[i] > g.min + 0.5 ? std::string("on") : std::string("off"))
                                 : radio::number_text(d.settings.gains[i]) + " dB";
            }
        }
        return plugins::string_value(v.empty() ? "no gain elements" : v, out, text, size);
    }
    if (std::strcmp(key, "clipped_percent") == 0) {
        *out = yr_value_double(d.clipped_percent.load());
        return YR_OK;
    }
    if (std::strcmp(key, "overload") == 0) {
        const double p = d.clipped_percent.load();
        const std::string v = p >= 0.01 ? "clipping (" + radio::number_text(std::round(p * 100.0) / 100.0) +
                                              " % of the values): " + profile_of(d).saturation_remedy
                                        : "none";
        return plugins::string_value(v, out, text, size);
    }
    if (std::strcmp(key, "noise_floor_dbfs") == 0) {
        const double n = d.noise_floor_dbfs.load();
        *out = n > -1e8 ? yr_value_double(n) : yr_value_none();
        return YR_OK;
    }
    if (std::strcmp(key, "tuned_mhz") == 0) {
        const double hz = d.tuned_hz.load();
        *out = hz > 0.0 ? yr_value_double(hz / 1e6) : yr_value_none();
        return YR_OK;
    }
    if (std::strcmp(key, "receiver_load") == 0) {
        *out = yr_value_double(d.load.load());
        return YR_OK;
    }
    if (std::strcmp(key, "build_info") == 0) {
        std::string v;
        for (const receiver::BundledLibrary& l : receiver::bundled_libraries())
            v += (v.empty() ? "" : ", ") + std::string(l.name) + " " + l.version;
        if (v.size() > YR_MAX_STRING_VALUE_BYTES) // the full list is in the licence bundle
            v = v.substr(0, v.rfind(", ", YR_MAX_STRING_VALUE_BYTES - 5)) + ", ...";
        return plugins::string_value(v, out, text, size);
    }
    for (const InfoSensor& is : profile_of(d).info_sensors)
        if (std::strcmp(key, is.key) == 0) {
            std::string v;
            if (*is.info_key == '\0') {
                v = d.caps.hardware_key;
            } else if (const auto it = d.caps.hardware.find(is.info_key); it != d.caps.hardware.end()) {
                v = it->second;
            }
            if (v.empty() && is.fallback_serial)
                v = d.found.serial;
            return plugins::string_value(v.empty() ? "not reported by the module" : v, out, text, size);
        }
    for (const auto& [module_key, k] : d.caps.sensors)
        if (k == key) {
            std::string v;
            try {
                v = d.sdr->readSensor(module_key);
            } catch (const std::exception& e) {
                d.error = std::string("the module could not read ") + module_key + ": " + e.what();
                return YR_ERR_IO;
            }
            return plugins::string_value(v, out, text, size);
        }
    return YR_ERR_NOT_FOUND;
}

void apply_settings(Device& d, const Settings& s, bool all)
{
    SoapySDR::Device& sdr = *d.sdr;
    const Caps& c = d.caps;
    const int rx = SOAPY_SDR_RX;
    if (all) {
        if (c.clock_sources.size() >= 2)
            sdr.setClockSource(c.clock_sources.at(s.clock_source));
        sdr.setSampleRate(rx, 0, c.rates.at(s.rate));
        if (!c.bandwidths.empty() && s.bandwidth > 0)
            sdr.setBandwidth(rx, 0, c.bandwidths.at(s.bandwidth - 1));
    }
    if (c.antennas.size() >= 2)
        sdr.setAntenna(rx, 0, c.antennas.at(s.antenna));
    if (c.agc)
        sdr.setGainMode(rx, 0, s.agc);
    if (!s.agc)
        for (std::size_t i = 0; i < c.gains.size(); ++i)
            sdr.setGain(rx, 0, c.gains[i].name, s.gains[i]);
    if (c.frequency_correction)
        sdr.setFrequencyCorrection(rx, 0, s.ppm);
    for (std::size_t i = 0; i < c.args.size(); ++i)
        sdr.writeSetting(c.args[i].module_key, s.args[i]);
}

} // namespace yantra::soapy

namespace yantra::soapy {

void power_off(Device& d)
{
    for (std::size_t i = 0; i < d.caps.args.size(); ++i)
        if (d.caps.args[i].power) {
            d.settings.args[i] = "false";
            if (d.sdr != nullptr)
                d.sdr->writeSetting(d.caps.args[i].module_key, "false");
        }
}

bool apply_preset(Device& d, const char* id)
{
    const Profile& pr = profile_of(d);
    const auto it = std::find_if(pr.presets.begin(), pr.presets.end(),
                                 [id](const Preset& p) { return std::strcmp(p.id, id) == 0; });
    if (it == pr.presets.end()) {
        d.error = std::string("unknown preset ") + id;
        return false;
    }
    for (const PresetValue& v : it->values) {
        const yr_setting_def* def = plugins::find_setting(d.table.defs, v.key);
        if (def == nullptr || (def->flags & YR_SETTING_UNAVAILABLE) != 0)
            continue;
        yr_value x = def->type == YR_TYPE_BOOL ? yr_value_bool(v.value != 0.0 ? 1 : 0) : yr_value_double(v.value);
        if (def->type == YR_TYPE_DOUBLE)
            x.u.f64 = std::clamp(v.value, def->minimum, def->maximum);
        store_setting(d, *def, x);
    }
    return true;
}

void profile_self_test(Device& d, const std::function<void(uint32_t, const std::string&, const std::string&,
                                                           const std::string&, const std::string&)>& item)
{
    const Profile& pr = profile_of(d);
    const Caps& c = d.caps;
    // Rates: the module must offer the profile's range ends.
    const bool lo = !c.rates.empty() && c.rates.front() <= pr.min_rate_hz + 1.0;
    const bool hi = !c.rates.empty() && c.rates.back() >= pr.max_rate_hz - 1.0;
    std::string rates;
    for (double r : c.rates)
        rates += (rates.empty() ? "" : ", ") + radio::number_text(r / 1e6);
    item(lo && hi ? YR_TEST_PASS : YR_TEST_WARN, "rates", "Sample rates", rates + " Msps",
         lo && hi ? "" : "the module does not offer the full range; update the firmware and the plugin");
    if (!pr.gains.empty()) {
        std::string missing;
        for (const GainControl& gc : pr.gains)
            if (std::none_of(c.gains.begin(), c.gains.end(),
                             [&](const GainElement& g) { return same_name(g.name, gc.module_name); }))
                missing += (missing.empty() ? "" : ", ") + std::string(gc.module_name);
        std::string all;
        for (const GainElement& g : c.gains)
            all += (all.empty() ? "" : ", ") + g.name;
        item(missing.empty() ? YR_TEST_PASS : YR_TEST_WARN, "gains", "Gain elements",
             missing.empty() ? all : "missing: " + missing,
             missing.empty() ? "" : "the module does not report every gain element; check the module version");
    }
    for (const InfoSensor& is : pr.info_sensors) {
        if (*is.info_key == '\0')
            continue;
        const auto it = c.hardware.find(is.info_key);
        const bool ok = it != c.hardware.end() && !it->second.empty();
        item(ok || is.fallback_serial ? YR_TEST_PASS : YR_TEST_WARN, std::string("info_") + is.key, is.label,
             ok ? it->second : (is.fallback_serial ? d.found.serial : "not reported by the module"),
             ok || is.fallback_serial ? "" : "update the device firmware");
    }
    for (std::size_t i = 0; i < c.args.size(); ++i) {
        if (!c.args[i].power)
            continue;
        const bool on = d.settings.args[i] == "true";
        item(on ? YR_TEST_WARN : YR_TEST_PASS, "antenna_power", "Antenna port power", on ? "on" : "off",
             on ? "switch it off unless an active antenna or amplifier needs it" : "");
    }
}

double choose_lo(const std::vector<double>& carriers, double centre, double rate, double offset_hz)
{
    if (offset_hz <= 0.0)
        return centre;
    // Carriers must stay inside the usable part, half a channel from its edges.
    const double half = 0.4 * rate - 12.5e3 - 1e3;
    double lo_min = -1e300, hi_max = 1e300;
    for (double f : carriers) {
        lo_min = std::max(lo_min, f - half);
        hi_max = std::min(hi_max, f + half);
    }
    double best = centre, best_gap = -1.0;
    for (double cand : {centre - offset_hz, centre + offset_hz, centre}) {
        const double lo = carriers.empty() ? cand : std::clamp(cand, std::min(lo_min, hi_max), hi_max);
        double gap = 1e300;
        for (double f : carriers)
            gap = std::min(gap, std::fabs(f - lo));
        if (carriers.empty())
            gap = offset_hz;
        if (gap >= 25e3)
            return lo;
        if (gap > best_gap) {
            best_gap = gap;
            best = lo;
        }
    }
    return best;
}

} // namespace yantra::soapy
