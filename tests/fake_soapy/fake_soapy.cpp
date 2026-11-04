// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fake SoapySDR module (RAD-92): imitates SoapyHackRF 0.3.6 as its
// interface reports a HackRF One (gain elements LNA 0-40/8, VGA 0-62/2,
// AMP 0/14 dB, no AGC, port TX/RX, rates 1-20 Msps, the MAX2837 baseband
// filters, native CS8, no frequency correction, the setting bias_tx,
// hardware info version/part id/serial/clock source) and streams the IQ
// of a yantra-gen scenario (RAD-71) at the frequency and rate it is tuned
// to, paced in real time, so the SDR plugins are tested end to end
// without hardware. Test-only, never installed: compiled into the test
// builds of the plugins and the test host, where its static
// SoapySDR::Registry registers the driver YANTRA_FAKE_SOAPY_DRIVER.
//
// Configuration (no device without it): environment variable
// YANTRA_FAKE_SOAPY, a JSON object (or the path of a file with one):
//   { "control": "<folder>",            unplug/overflow control files
//     "devices": [ {
//       "serial": "...", "label": "...",
//       "scenario": "<scenario file>",  the signal; none: noise only
//       "seed": n,                      replaces the scenario's seed
//       "reference_gain_db": 40,        total gain at which the samples
//                                       carry the scenario's levels
//       "buffer_s": 0.5,                device buffer: a reader further
//                                       behind loses samples (OVERFLOW)
//       "paced": true,                  false: samples as fast as they are
//                                       read (no real time, no natural
//                                       overflow; for tests that check
//                                       values, not timing)
//       "overflow_at": n,               one overflow once n samples of a
//                                       stream are out (deterministic)
//       "unplug_mode": "timeout"|"error", what reads of an unplugged
//                                       device return (SoapyHackRF: timeout)
//       "clock_sources": [...], "sensors": {k: v}, "frequency_correction": bool,
//       "rates": [Hz...], "gains": [{name, min, max, step}],
//       "open_error": "..." } ] }
// Control files in "control": <serial>.unplugged (present: unplugged;
// removing it replugs; a replug never revives an old handle, as with
// real USB devices), <serial>.overflow (holds n: the next n reads
// report SOAPY_SDR_OVERFLOW; consumed), <serial>.level_offset (holds a
// level step in dB applied to the whole signal, as a signal generator
// stepped by hand; read at every read while present).
//
// Adapted from yantra 68ea20a tests/fake_soapy/fake_module.cpp (device
// model, pacing, hackrf profile); the signal is the scenario engine's.
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Registry.hpp>

#include <yantra/gen/scenario.hpp>
#include <yantra/gen/scenario_engine.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef YANTRA_FAKE_SOAPY_DRIVER
#define YANTRA_FAKE_SOAPY_DRIVER "yantrafake"
#endif

namespace {

using json = nlohmann::json;
namespace gen = yantra::gen;
using Clock = std::chrono::steady_clock;
using cf = std::complex<float>;
namespace fs = std::filesystem;

struct Gain {
    std::string name;
    double min = 0.0, max = 0.0, step = 1.0;
};

struct Spec {
    std::string serial, label;
    std::string scenario;
    std::optional<uint64_t> seed;
    double reference_gain_db = 40.0;
    double buffer_s = 0.5;
    bool paced = true;
    uint64_t overflow_at = 0;
    bool unplug_error = false;
    std::vector<double> rates;
    std::vector<double> bandwidths{1.75e6, 2.5e6, 3.5e6, 5e6, 5.5e6, 6e6,  7e6,  8e6,
                                   9e6,    10e6,  12e6,  14e6, 15e6, 20e6, 24e6, 28e6};
    std::vector<Gain> gains{{"LNA", 0.0, 40.0, 8.0}, {"VGA", 0.0, 62.0, 2.0}, {"AMP", 0.0, 14.0, 14.0}};
    std::vector<std::string> clock_sources;
    std::map<std::string, std::string> sensors;
    bool freq_correction = false;
    std::string open_error;
};

struct Config {
    std::string control;
    std::vector<Spec> devices;
};

std::string text_of(const json& v) { return v.is_string() ? v.get<std::string>() : v.dump(); }

// The configuration, read again on every call (tests change it between
// cases); an empty one without the environment variable.
Config read_config()
{
    Config c;
    const char* env = std::getenv("YANTRA_FAKE_SOAPY");
    if (env == nullptr || *env == '\0')
        return c;
    std::string text = env;
    if (text.front() != '{') {
        std::ifstream in(text);
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str();
    }
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object())
        return c;
    c.control = j.value("control", "");
    for (const json& d : j.value("devices", json::array())) {
        Spec s;
        s.serial = d.value("serial", "0000000000000000f00dcafe0000000" + std::to_string(c.devices.size()));
        s.label = d.value("label", "HackRF One #" + std::to_string(c.devices.size()) + " " +
                                       s.serial.substr(s.serial.size() > 8 ? s.serial.size() - 8 : 0));
        s.scenario = d.value("scenario", "");
        if (d.contains("seed"))
            s.seed = d["seed"].get<uint64_t>();
        s.reference_gain_db = d.value("reference_gain_db", s.reference_gain_db);
        s.buffer_s = d.value("buffer_s", s.buffer_s);
        s.paced = d.value("paced", s.paced);
        s.overflow_at = d.value("overflow_at", s.overflow_at);
        s.unplug_error = d.value("unplug_mode", "timeout") == "error";
        if (d.contains("rates"))
            s.rates = d["rates"].get<std::vector<double>>();
        else
            for (int m = 1; m <= 20; ++m)
                s.rates.push_back(m * 1e6);
        if (d.contains("gains")) {
            s.gains.clear();
            for (const json& g : d["gains"])
                s.gains.push_back({g.at("name").get<std::string>(), g.value("min", 0.0),
                                   g.value("max", 0.0), g.value("step", 0.0)});
        }
        if (d.contains("clock_sources"))
            s.clock_sources = d["clock_sources"].get<std::vector<std::string>>();
        if (d.contains("sensors"))
            for (const auto& [k, v] : d["sensors"].items())
                s.sensors[k] = text_of(v);
        s.freq_correction = d.value("frequency_correction", false);
        s.open_error = d.value("open_error", "");
        c.devices.push_back(std::move(s));
    }
    return c;
}

bool control_file(const Config& c, const std::string& serial, const char* suffix)
{
    std::error_code ec;
    return !c.control.empty() && fs::exists(fs::path(c.control) / (serial + suffix), ec);
}

bool present(const Config& c, const std::string& serial)
{
    return !control_file(c, serial, ".unplugged");
}

// Takes the overflow count of <serial>.overflow (0 when there is none).
int take_overflows(const Config& c, const std::string& serial)
{
    if (!control_file(c, serial, ".overflow"))
        return 0;
    const fs::path p = fs::path(c.control) / (serial + ".overflow");
    int n = 1;
    std::ifstream(p) >> n;
    std::error_code ec;
    fs::remove(p, ec);
    return std::max(n, 0);
}

// The level step of <serial>.level_offset, dB (0 when there is none).
double level_offset_db(const Config& c, const std::string& serial)
{
    if (!control_file(c, serial, ".level_offset"))
        return 0.0;
    double db = 0.0;
    std::ifstream(fs::path(c.control) / (serial + ".level_offset")) >> db;
    return std::isfinite(db) ? db : 0.0;
}

double nearest(const std::vector<double>& list, double v)
{
    double best = list.front();
    for (double x : list)
        if (std::fabs(x - v) < std::fabs(best - v))
            best = x;
    return best;
}

// The signal: the scenario at the tuned frequency and rate, cells outside
// the span left out; noise alone when no cell fits.
class Signal {
public:
    Signal(const Spec& spec, double centre_hz, double rate_hz)
    {
        if (spec.scenario.empty())
            return;
        try {
            gen::Scenario s = gen::load_scenario_file(spec.scenario);
            s.duration_s = 0.0;
            s.sample_rate_hz = rate_hz;
            s.centre_frequency_hz = centre_hz;
            if (spec.seed)
                s.seed = *spec.seed;
            std::vector<gen::ScenarioCell> kept;
            for (const gen::ScenarioCell& c : s.cells)
                if (std::fabs(gen::cell_frequency_hz(c) - centre_hz) + 12.5e3 < 0.45 * rate_hz)
                    kept.push_back(c);
            if (kept.empty())
                return;
            // Neighbour entries of cells outside the span go too (they
            // would name an unknown cell); a band scan tunes such spans.
            for (gen::ScenarioCell& c : kept)
                std::erase_if(c.neighbours, [&](const gen::ScenarioNeighbour& n) {
                    return std::none_of(kept.begin(), kept.end(),
                                        [&](const gen::ScenarioCell& k) { return k.id == n.cell; });
                });
            s.cells = kept;
            engine_ = std::make_unique<gen::ScenarioEngine>(std::move(s));
        } catch (const std::exception&) {
            engine_.reset(); // noise only
        }
    }
    void read(cf* out, std::size_t n)
    {
        if (engine_) {
            std::size_t got = 0;
            while (got < n)
                got += engine_->read(out + got, n - got);
            return;
        }
        // About -100 dBFS of noise: an empty band.
        for (std::size_t i = 0; i < n; ++i)
            out[i] = cf(noise_(rng_), noise_(rng_)) * 7.07e-6f;
    }

private:
    std::unique_ptr<gen::ScenarioEngine> engine_;
    std::mt19937_64 rng_{1};
    std::normal_distribution<float> noise_{0.0f, 1.0f};
};

struct FakeStream {
    // activateStream/deactivateStream may run on another thread than
    // readStream (SoapyRemote's server reads on its own thread from
    // setupStream on); SoapyHackRF locks too.
    std::mutex mutex;
    std::string format;
    bool active = false;
    Clock::time_point t0;
    uint64_t produced = 0; ///< samples handed out (or lost) since t0
    bool scheduled_overflow_done = false;
    double rate = 0.0;
    double centre = 0.0;
    std::unique_ptr<Signal> signal;
    std::vector<cf> buffer;
};

class FakeDevice final : public SoapySDR::Device {
public:
    explicit FakeDevice(Spec spec) : spec_(std::move(spec))
    {
        rate_ = spec_.rates.front();
        bandwidth_ = 0.0;
        for (const Gain& g : spec_.gains)
            gains_[g.name] = g.min;
        if (!spec_.clock_sources.empty())
            clock_ = spec_.clock_sources.front();
    }

    std::string getDriverKey() const override { return "HackRF"; }
    std::string getHardwareKey() const override { return "HackRF One"; }
    SoapySDR::Kwargs getHardwareInfo() const override
    {
        return {{"version", "2024.02.1"},
                {"part id", "a000cb3c00f1f1f1"},
                {"serial", spec_.serial},
                {"clock source", clock_ == "external" ? "external" : "internal"}};
    }
    size_t getNumChannels(const int dir) const override { return dir == SOAPY_SDR_RX ? 1 : 0; }
    bool getFullDuplex(const int, const size_t) const override { return false; }

    std::vector<std::string> listAntennas(const int, const size_t) const override { return {"TX/RX"}; }
    void setAntenna(const int, const size_t, const std::string& name) override
    {
        if (name != "TX/RX")
            throw std::invalid_argument("unknown antenna " + name);
    }
    std::string getAntenna(const int, const size_t) const override { return "TX/RX"; }

    std::vector<std::string> listClockSources() const override { return spec_.clock_sources; }
    void setClockSource(const std::string& source) override
    {
        if (std::find(spec_.clock_sources.begin(), spec_.clock_sources.end(), source) ==
            spec_.clock_sources.end())
            throw std::invalid_argument("unknown clock source " + source);
        std::lock_guard lock(mutex_);
        clock_ = source;
    }
    std::string getClockSource() const override
    {
        std::lock_guard lock(mutex_);
        return clock_;
    }

    bool hasGainMode(const int, const size_t) const override { return false; }
    std::vector<std::string> listGains(const int, const size_t) const override
    {
        std::vector<std::string> names;
        for (const Gain& g : spec_.gains)
            names.push_back(g.name);
        return names;
    }
    void setGain(const int, const size_t, const std::string& name, const double value) override
    {
        for (const Gain& g : spec_.gains)
            if (g.name == name) {
                double v = std::clamp(value, g.min, g.max);
                if (g.step > 0.0)
                    v = std::min(g.max, g.min + std::round((v - g.min) / g.step) * g.step);
                std::lock_guard lock(mutex_);
                gains_[name] = v;
                return;
            }
        throw std::invalid_argument("unknown gain " + name);
    }
    double getGain(const int, const size_t, const std::string& name) const override
    {
        std::lock_guard lock(mutex_);
        const auto it = gains_.find(name);
        return it == gains_.end() ? 0.0 : it->second;
    }
    SoapySDR::Range getGainRange(const int, const size_t, const std::string& name) const override
    {
        for (const Gain& g : spec_.gains)
            if (g.name == name)
                return SoapySDR::Range(g.min, g.max, g.step);
        return SoapySDR::Range(0.0, 0.0);
    }

    void setFrequency(const int, const size_t, const double frequency, const SoapySDR::Kwargs&) override
    {
        std::lock_guard lock(mutex_);
        frequency_ = std::round(std::clamp(frequency, 1e6, 6000e6));
    }
    double getFrequency(const int, const size_t) const override
    {
        std::lock_guard lock(mutex_);
        return frequency_;
    }
    SoapySDR::RangeList getFrequencyRange(const int, const size_t) const override
    {
        return {SoapySDR::Range(1e6, 6000e6)};
    }
    bool hasFrequencyCorrection(const int, const size_t) const override { return spec_.freq_correction; }
    void setFrequencyCorrection(const int, const size_t, const double ppm) override
    {
        if (!spec_.freq_correction)
            throw std::runtime_error("no frequency correction");
        ppm_ = ppm;
    }
    double getFrequencyCorrection(const int, const size_t) const override { return ppm_; }

    void setSampleRate(const int, const size_t, const double rate) override
    {
        std::lock_guard lock(mutex_);
        rate_ = nearest(spec_.rates, rate);
    }
    double getSampleRate(const int, const size_t) const override
    {
        std::lock_guard lock(mutex_);
        return rate_;
    }
    std::vector<double> listSampleRates(const int, const size_t) const override { return spec_.rates; }
    SoapySDR::RangeList getSampleRateRange(const int, const size_t) const override
    {
        SoapySDR::RangeList r;
        for (double x : spec_.rates)
            r.emplace_back(x, x);
        return r;
    }
    void setBandwidth(const int, const size_t, const double bw) override
    {
        std::lock_guard lock(mutex_);
        bandwidth_ = bw <= 0.0 ? 0.0 : nearest(spec_.bandwidths, bw);
    }
    double getBandwidth(const int, const size_t) const override
    {
        std::lock_guard lock(mutex_);
        return bandwidth_;
    }
    std::vector<double> listBandwidths(const int, const size_t) const override { return spec_.bandwidths; }
    SoapySDR::RangeList getBandwidthRange(const int, const size_t) const override
    {
        SoapySDR::RangeList r;
        for (double x : spec_.bandwidths)
            r.emplace_back(x, x);
        return r;
    }

    std::vector<std::string> listSensors() const override
    {
        std::vector<std::string> names;
        for (const auto& [k, v] : spec_.sensors)
            names.push_back(k);
        return names;
    }
    SoapySDR::ArgInfo getSensorInfo(const std::string& key) const override
    {
        SoapySDR::ArgInfo i;
        i.key = key;
        i.name = key;
        i.type = SoapySDR::ArgInfo::STRING;
        i.description = "Fake sensor " + key;
        return i;
    }
    std::string readSensor(const std::string& key) const override
    {
        const auto it = spec_.sensors.find(key);
        if (it == spec_.sensors.end())
            throw std::invalid_argument("unknown sensor " + key);
        return it->second;
    }
    SoapySDR::ArgInfoList getSettingInfo() const override
    {
        SoapySDR::ArgInfo bias;
        bias.key = "bias_tx";
        bias.value = "false";
        bias.name = "Antenna Bias";
        bias.description = "Antenna port power control.";
        bias.type = SoapySDR::ArgInfo::BOOL;
        return {bias};
    }
    void writeSetting(const std::string& key, const std::string& value) override
    {
        if (key != "bias_tx")
            throw std::invalid_argument("unknown setting " + key);
        std::lock_guard lock(mutex_);
        bias_ = value == "true";
    }
    std::string readSetting(const std::string& key) const override
    {
        std::lock_guard lock(mutex_);
        return key == "bias_tx" ? (bias_ ? "true" : "false") : "";
    }

    std::vector<std::string> getStreamFormats(const int, const size_t) const override
    {
        return {SOAPY_SDR_CS8, SOAPY_SDR_CS16, SOAPY_SDR_CF32};
    }
    std::string getNativeStreamFormat(const int, const size_t, double& full_scale) const override
    {
        full_scale = 128.0;
        return SOAPY_SDR_CS8;
    }
    SoapySDR::Stream* setupStream(const int dir, const std::string& format,
                                  const std::vector<size_t>& channels, const SoapySDR::Kwargs&) override
    {
        if (dir != SOAPY_SDR_RX || (!channels.empty() && (channels.size() != 1 || channels[0] != 0)))
            throw std::invalid_argument("fake: one RX channel only");
        if (format != SOAPY_SDR_CF32 && format != SOAPY_SDR_CS16 && format != SOAPY_SDR_CS8)
            throw std::invalid_argument("fake: unsupported format " + format);
        auto* s = new FakeStream;
        s->format = format;
        return reinterpret_cast<SoapySDR::Stream*>(s);
    }
    void closeStream(SoapySDR::Stream* stream) override { delete reinterpret_cast<FakeStream*>(stream); }
    size_t getStreamMTU(SoapySDR::Stream*) const override { return 131072; }
    int activateStream(SoapySDR::Stream* stream, const int, const long long, const size_t) override
    {
        auto* s = reinterpret_cast<FakeStream*>(stream);
        std::lock_guard lock(s->mutex);
        restart(*s);
        s->active = true;
        return 0;
    }
    int deactivateStream(SoapySDR::Stream* stream, const int, const long long) override
    {
        auto* s = reinterpret_cast<FakeStream*>(stream);
        std::lock_guard lock(s->mutex);
        s->active = false;
        return 0;
    }
    int readStream(SoapySDR::Stream* stream, void* const* buffs, const size_t numElems, int& flags,
                   long long& timeNs, const long timeoutUs) override;

private:
    double total_gain_db() const
    {
        std::lock_guard lock(mutex_);
        double sum = 0.0;
        for (const auto& [k, v] : gains_)
            sum += v;
        return sum;
    }
    // A new signal for the current tuning; pacing starts again.
    void restart(FakeStream& s)
    {
        {
            std::lock_guard lock(mutex_);
            s.rate = rate_;
            s.centre = frequency_;
        }
        s.signal = std::make_unique<Signal>(spec_, s.centre, s.rate);
        s.t0 = Clock::now();
        s.produced = 0;
        s.scheduled_overflow_done = false;
    }

    Spec spec_;
    mutable std::mutex mutex_;
    double rate_ = 0.0;
    double bandwidth_ = 0.0;
    double frequency_ = 390e6;
    double ppm_ = 0.0;
    bool bias_ = false;
    std::string clock_;
    std::map<std::string, double> gains_;
    bool lost_ = false; ///< unplugged once: this handle never works again
    int overflows_ = 0;
};

int FakeDevice::readStream(SoapySDR::Stream* stream, void* const* buffs, const size_t numElems,
                           int& flags, long long& timeNs, const long timeoutUs)
{
    auto* s = reinterpret_cast<FakeStream*>(stream);
    flags = 0;
    timeNs = 0;
    const auto deadline = Clock::now() + std::chrono::microseconds(std::max(timeoutUs, 0L));
    std::unique_lock stream_lock(s->mutex);
    if (!s->active) {
        stream_lock.unlock();
        // As SoapyHackRF: no samples before activateStream, a timeout
        // (SoapyRemote's server reads from setupStream on).
        std::this_thread::sleep_until(deadline);
        return SOAPY_SDR_TIMEOUT;
    }
    const Config cfg = read_config();
    if (!lost_ && !present(cfg, spec_.serial))
        lost_ = true;
    if (lost_) {
        if (spec_.unplug_error)
            return SOAPY_SDR_STREAM_ERROR;
        std::this_thread::sleep_until(deadline);
        return SOAPY_SDR_TIMEOUT;
    }
    bool retuned = false;
    {
        std::lock_guard lock(mutex_);
        retuned = rate_ != s->rate || frequency_ != s->centre;
    }
    if (retuned)
        restart(*s); // the samples start over at the new tuning
    overflows_ += take_overflows(cfg, spec_.serial);

    const auto available = [&] {
        const double elapsed = std::chrono::duration<double>(Clock::now() - s->t0).count();
        return static_cast<int64_t>(elapsed * s->rate) - static_cast<int64_t>(s->produced);
    };
    const auto lose = [&](int64_t n) {
        // The device buffer overflowed: the time stamps skip the lost
        // samples. The signal is not generated for them: on a slow machine
        // (sanitizers) generating what was lost would take longer than the
        // loss itself and a reader would never catch up.
        s->produced += static_cast<uint64_t>(n);
    };
    if (spec_.overflow_at > 0 && !s->scheduled_overflow_done && s->produced >= spec_.overflow_at) {
        s->scheduled_overflow_done = true;
        ++overflows_;
    }
    if (overflows_ > 0) {
        --overflows_;
        lose(static_cast<int64_t>(0.01 * s->rate));
        return SOAPY_SDR_OVERFLOW;
    }
    int64_t avail = spec_.paced ? available() : static_cast<int64_t>(numElems);
    if (spec_.paced && avail > static_cast<int64_t>(spec_.buffer_s * s->rate)) {
        lose(avail - static_cast<int64_t>(0.01 * s->rate));
        return SOAPY_SDR_OVERFLOW;
    }
    while (avail < static_cast<int64_t>(std::min<std::size_t>(numElems, 8192))) {
        if (Clock::now() >= deadline)
            return SOAPY_SDR_TIMEOUT;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        avail = available();
    }
    const auto n = static_cast<std::size_t>(std::min<int64_t>(avail, static_cast<int64_t>(numElems)));
    s->buffer.resize(n);
    s->signal->read(s->buffer.data(), n);
    timeNs = static_cast<long long>(static_cast<double>(s->produced) / s->rate * 1e9);
    s->produced += n;
    flags |= SOAPY_SDR_HAS_TIME;
    // The level follows the gain: the scenario's levels at reference_gain_db.
    const auto g = static_cast<float>(std::pow(
        10.0, (total_gain_db() - spec_.reference_gain_db + level_offset_db(cfg, spec_.serial)) / 20.0));
    if (s->format == SOAPY_SDR_CF32) {
        auto* out = static_cast<cf*>(buffs[0]);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = s->buffer[i] * g;
    } else if (s->format == SOAPY_SDR_CS16) {
        auto* out = static_cast<int16_t*>(buffs[0]);
        for (std::size_t i = 0; i < n; ++i) {
            out[2 * i] = static_cast<int16_t>(std::clamp(std::lround(s->buffer[i].real() * g * 32767.0f), -32768L, 32767L));
            out[2 * i + 1] = static_cast<int16_t>(std::clamp(std::lround(s->buffer[i].imag() * g * 32767.0f), -32768L, 32767L));
        }
    } else {
        auto* out = static_cast<int8_t*>(buffs[0]);
        for (std::size_t i = 0; i < n; ++i) {
            out[2 * i] = static_cast<int8_t>(std::clamp(std::lround(s->buffer[i].real() * g * 127.0f), -128L, 127L));
            out[2 * i + 1] = static_cast<int8_t>(std::clamp(std::lround(s->buffer[i].imag() * g * 127.0f), -128L, 127L));
        }
    }
    return static_cast<int>(n);
}

SoapySDR::KwargsList find_fake(const SoapySDR::Kwargs& args)
{
    const Config cfg = read_config();
    SoapySDR::KwargsList out;
    for (std::size_t i = 0; i < cfg.devices.size(); ++i) {
        const Spec& s = cfg.devices[i];
        if (!present(cfg, s.serial))
            continue;
        const auto it = args.find("serial");
        if (it != args.end() && !it->second.empty() && it->second != s.serial)
            continue;
        out.push_back({{"device", "HackRF One"},
                       {"label", s.label},
                       {"part_id", "a000cb3c00f1f1f1"},
                       {"serial", s.serial},
                       {"version", "2024.02.1"}});
    }
    return out;
}

SoapySDR::Device* make_fake(const SoapySDR::Kwargs& args)
{
    const Config cfg = read_config();
    const auto it = args.find("serial");
    for (const Spec& s : cfg.devices) {
        if (it != args.end() && !it->second.empty() && it->second != s.serial)
            continue;
        if (!present(cfg, s.serial))
            continue;
        if (!s.open_error.empty())
            throw std::runtime_error(s.open_error);
        return new FakeDevice(s);
    }
    throw std::runtime_error("no HackRF boards found");
}

const SoapySDR::Registry register_fake(YANTRA_FAKE_SOAPY_DRIVER, &find_fake, &make_fake, SOAPY_SDR_ABI_VERSION);
const SoapySDR::ModuleVersion fake_version("0.3.6-yantra-fake");

} // namespace

// Keeps the registration when the object file comes from a static archive.
extern "C" void yantra_fake_soapy_link_anchor() {}
