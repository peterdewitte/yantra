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

// The receiver's useful range (the channelizer takes any rate; below
// 200 kHz no TETRA span is left, above 10 Msps one core does not keep up).
constexpr double kMinRate = 200e3;
constexpr double kMaxRate = 10e6;
constexpr double kCandidateRates[] = {250e3, 500e3,  1e6,    1.024e6, 1.2e6, 1.4e6, 1.8e6,
                                      2e6,   2.048e6, 2.4e6, 2.5e6,  3e6,   3.2e6, 4e6,
                                      5e6,   6e6,    8e6,    10e6};

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
    if (lower(g.name).find("amp") != std::string::npos)
        return g.min;
    double v = (g.min + g.max) / 2.0;
    if (g.step > 0.0)
        v = g.min + std::floor((v - g.min) / g.step) * g.step;
    return v;
}

uint32_t default_rate(const Caps& c)
{
    uint32_t best = 0;
    for (uint32_t i = 0; i < c.rates.size(); ++i)
        if (std::fabs(c.rates[i] - 2e6) < std::fabs(c.rates[best] - 2e6))
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
        if (x >= kMinRate && x <= kMaxRate)
            c.rates.push_back(x);
    if (c.rates.empty())
        throw std::runtime_error("the module offers no sample rate from 0.2 to 10 Msps");
    c.bandwidths = sdr.listBandwidths(rx, 0);
    for (const std::string& g : sdr.listGains(rx, 0)) {
        const SoapySDR::Range r = sdr.getGainRange(rx, 0, g);
        c.gains.push_back({g, r.minimum(), r.maximum(), r.step()});
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
    d.caps = std::move(c);
}

Settings default_settings(const Caps& c)
{
    Settings s;
    s.rate = default_rate(c);
    for (const GainElement& g : c.gains)
        s.gains.push_back(gain_default(g));
    for (const ModuleArg& a : c.args) {
        std::string v = a.default_text;
        if (a.type == YR_TYPE_ENUM && std::find(a.options.begin(), a.options.end(), v) == a.options.end())
            v = a.options.front();
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
    const Settings def = default_settings(c);
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
                                  "Sample rate of the receiver; 80 % of it is the span measured at "
                                  "once. Higher rates take more processor time.",
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
        std::string key = "gain_" + item_name(g.name, i);
        if (radio::is_out_of_scope_name(key))
            key = "gain_" + std::to_string(i);
        yr_setting_def s = number(t.keep(key), YR_TYPE_DOUBLE, t.keep(g.name + " gain"), "dB",
                                  kGroupGain,
                                  t.keep("Gain element " + g.name + " of the device, " +
                                         radio::number_text(g.min) + " to " + radio::number_text(g.max) +
                                         " dB."),
                                  static_cast<int32_t>(2 + i), g.min, g.max, g.step, def.gains[i]);
        s.flags |= YR_SETTING_CALIBRATION;
        out.push_back(s);
    }

    // Module settings (RAD-51): the module's own list.
    int32_t order = 1;
    for (const ModuleArg& a : c.args) {
        const bool power = lower(a.module_key).find("bias") != std::string::npos;
        const char* key = t.keep(a.key);
        const char* label = t.keep(power ? "Antenna port power (" + a.module_key + ")" : a.module_key);
        const char* help = t.keep("Setting " + a.module_key + " of the module.");
        yr_setting_def s;
        if (a.type == YR_TYPE_ENUM) {
            const auto it = std::find(a.options.begin(), a.options.end(), a.default_text);
            s = enum_of(t, key, label, kGroupModule, help, order, a.options,
                        static_cast<uint32_t>(it == a.options.end() ? 0 : it - a.options.begin()));
        } else {
            s = make_setting(key, a.type, label, "", kGroupModule, help, order);
            if (a.type == YR_TYPE_BOOL)
                s.default_value = yr_value_bool(a.default_text == "true" ? 1 : 0);
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

    // Receiver
    out.push_back(number("record_rate_hz", YR_TYPE_DOUBLE, "Records per second", "1/s",
                         kGroupReceiver, "Measurement records per carrier and second (1-10).", 1,
                         1.0, 10.0, 1.0, 5.0));
    yr_setting_def th = number("threads", YR_TYPE_UINT, "Receiver threads", "", kGroupReceiver,
                               "Processor threads of the receiver.", 2, 1.0, 8.0, 1.0, 2.0);
    th.flags |= YR_SETTING_RESTART | YR_SETTING_ADVANCED;
    out.push_back(th);
    out.push_back(plugins::ms_max_power_setting(kGroupReceiver, 3));
    plugins::add_gsmtap_settings(out, kGroupReceiver, 10);

    // Sensors
    t.sensors.push_back(make_sensor("module", YR_TYPE_STRING, "SoapySDR module", "", kGroupModule,
                                    "The module, its version and vendor library (RAD-52).", 1));
    t.sensors.push_back(make_sensor("hardware", YR_TYPE_STRING, "Hardware", "", kGroupModule,
                                    "What the module reports about the device.", 2));
    t.sensors.push_back(make_sensor("overflow_count", YR_TYPE_UINT, "Overflows", "", kGroupReceiver,
                                    "Device buffer overflows of the current stream (RAD-24).", 3));
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
    else if (auto g = plugins::gsmtap_get(s.gsmtap, key, out, text, size))
        return *g;
    else {
        for (std::size_t i = 0; i < d.caps.gains.size(); ++i)
            if (is(d.table.defs.at(7 + i).key)) {
                *out = yr_value_double(s.gains[i]);
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
    else if (plugins::gsmtap_set(s.gsmtap, def.key, v))
        return;
    else {
        for (std::size_t i = 0; i < d.caps.gains.size(); ++i)
            if (is(d.table.defs.at(7 + i).key)) {
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
