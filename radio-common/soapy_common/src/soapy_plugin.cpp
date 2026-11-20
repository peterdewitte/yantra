// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio API of a SoapySDR plugin (RAD-54, RAD-62): open/close by
// stable key, plans, the stream into the measurement engine with
// overflow and drop events (RAD-24, RAD-25), hot-plug (RAD-35),
// generated settings and sensors (RAD-51), diagnostics (RAD-52), self
// test. Glue modelled on plugins/simulator (same state machine, RAD-34).
//
// Spectrum (radio API 1.2, RAD-140): with YR_PLAN_SPECTRUM, once a second
// the stream feeds the next block of its own IQ samples to a windowed,
// averaged FFT (receiver/spectrum.hpp on pocketfft, RAD-04) until one
// average is done (16 frames, a few tens of ms), and pushes the bins of
// the plan's band within the span received at once (the usable 0.8 of
// the sample rate around the tuned centre); between two spectra no FFT
// runs. Levels in dBFS per bin (the window's noise bandwidth divided out,
// so a carrier's bins add up to its level), OVERLOAD when the averaged
// samples clipped. Only the bins leave the plugin, never IQ (RAD-26).
#include "soapy_device.hpp"

#include <yantra/receiver/spectrum.hpp>

#include <yantra/radio.hpp>

#include <SoapySDR/Errors.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Registry.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace yantra::soapy {

using receiver::tetra::BlockInfo;
using receiver::tetra::EngineConfig;
using receiver::tetra::EngineOutput;
using receiver::tetra::MeasurementEngine;

void log(const Plugin& p, Device* d, uint32_t level, const std::string& text)
{
    p.host->log(p.host->host, reinterpret_cast<yr_device*>(d), level, text.c_str());
}

namespace {

constexpr uint32_t kMaxCarriers = 8;
constexpr double kSpanFraction = 0.8;   ///< of the sample rate measured at once
constexpr auto kScanPeriod = std::chrono::milliseconds(500);
constexpr int64_t kStallNs = 1'000'000'000; ///< no samples this long while streaming: stalled
constexpr long kReadTimeoutUs = 100'000;
/// Samples per engine call: a stop waits for at most one piece (RAD-15).
constexpr std::size_t kProcessPiece = 16384;
/// FFT frames averaged into one spectrum (RAD-140).
constexpr std::size_t kSpectrumAverages = 16;

Plugin* P(yr_plugin* p) { return reinterpret_cast<Plugin*>(p); }
Device* D(yr_device* d) { return reinterpret_cast<Device*>(d); }

const Options* g_options = nullptr;

// Built at the first descriptor() call, after g_options is set.
const plugins::TetraSchemaTable& schema_table()
{
    static const plugins::TetraSchemaTable t(
        radios::TetraSchemaOptions{false, false, g_options != nullptr && g_options->profile.scan});
    return t;
}

void set_device_error(Device* d, std::string text)
{
    std::lock_guard lock(d->mutex);
    d->error = std::move(text);
}

int64_t now_ns(const Plugin& p) { return p.host->monotonic_ns(p.host->host); }

// SoapySDR's own log goes to the host log (debug and up as debug, so a
// module's chatter does not fill the user's log).
const yr_host_api* g_log_host = nullptr;
void soapy_log(const SoapySDRLogLevel level, const char* message)
{
    const yr_host_api* h = g_log_host;
    if (h == nullptr)
        return;
    const uint32_t l = level <= SOAPY_SDR_ERROR ? YR_LOG_ERROR
                       : level <= SOAPY_SDR_WARNING ? YR_LOG_WARNING
                                                    : YR_LOG_DEBUG;
    h->log(h->host, nullptr, l, (std::string("SoapySDR: ") + message).c_str());
}

std::string module_text(const std::string& driver)
{
    for (const ModuleInfo& m : modules())
        if (m.driver == driver)
            return m.module + " " + m.version + " (" + m.library + ")";
    return driver;
}

SoapySDR::Device* make_device(const Found& f)
{
    const auto makes = SoapySDR::Registry::listMakeFunctions();
    const auto it = makes.find(f.driver);
    if (it == makes.end())
        throw std::runtime_error("the module of driver " + f.driver + " is not in this plugin");
    SoapySDR::Kwargs args(f.args.begin(), f.args.end());
    SoapySDR::Device* d = it->second(args);
    if (d == nullptr)
        throw std::runtime_error("the module returned no device");
    return d;
}

void fill_info(Device& d)
{
    d.info_text[0] = d.found.key;
    d.info_text[1] = d.found.label;
    d.info_text[2] = d.found.driver;
    d.info_text[3] = d.found.serial;
    d.info_text[4] = d.module_text;
    d.info_text[5] = d.caps.hardware_info;
    d.info = {sizeof(yr_device_info), 0, d.info_text[0].c_str(), d.info_text[1].c_str(),
              d.info_text[2].c_str(), d.info_text[3].c_str(), d.info_text[4].c_str(),
              d.info_text[5].c_str(), ""};
}

void report(const Found& f, yr_device_cb cb, void* user)
{
    std::string hw;
    for (const auto& [k, v] : f.args)
        if (k != "driver" && k != "label" && !radio::is_out_of_scope_name(k))
            hw += (hw.empty() ? "" : "\n") + k + "=" + v;
    const std::string mod = module_text(f.driver);
    const yr_device_info info = {sizeof(yr_device_info), 0, f.key.c_str(), f.label.c_str(),
                                 f.driver.c_str(), f.serial.c_str(), mod.c_str(), hw.c_str(), ""};
    cb(user, &info);
}

// --- the stream ---------------------------------------------------------------------

class Stream {
public:
    explicit Stream(Device* d) : d_(d), p_(*d->plugin), pr_(d->plugin->options->profile) {}
    void run();

private:
    void build(const Settings& s);
    void tune(double lo_hz);
    void annotate(EngineOutput& out);
    void deliver(EngineOutput& out)
    {
        annotate(out);
        plugins::push_output(*p_.host, d_->sink, out, d_->counters);
        out.clear();
    }
    void event(uint32_t e, int64_t v) { p_.host->stream_event(d_->sink, e, v); }
    /// Applies changed settings (true: the engine was rebuilt).
    bool poll_settings(EngineOutput& out);
    /// One read into buf_ through the engine; false: stop.
    bool step(EngineOutput& out);
    bool stopping() const; ///< stop_stream asked to stop
    void measure_floor();
    void run_scan(EngineOutput& out);
    /// Feeds `n` samples of buf_ to the spectrum when one is due (RAD-140).
    void feed_spectrum(std::size_t n, int64_t t_now);

    Device* d_;
    const Plugin& p_;
    const Profile& pr_;
    Settings s_;
    SoapySDR::Device* sdr_ = nullptr;
    SoapySDR::Stream* st_ = nullptr;
    std::unique_ptr<MeasurementEngine> eng_;
    std::unique_ptr<radios::RecordFiller> cell_;
    std::vector<std::complex<float>> buf_;
    double rate_ = 0.0;
    double centre_ = 0.0; ///< true centre frequency of the samples
    double lo_factor_ = 1.0;
    uint64_t version_ = 0;
    plugins::PlanSetup plan_;
    bool discontinuity_ = false;
    bool need_base_ = true;
    long long expected_ns_ = -1;
    double busy_s_ = 0.0, signal_s_ = 0.0;
    uint64_t clipped_ = 0, values_ = 0;
    double floor_s_ = 0.0;
    int errors_ = 0;
    uint64_t last_samples_ = 0; ///< samples of the last step (0: none)
    plugins::SpectrumPlan spectrum_;                    ///< spectra asked for (RAD-140)
    std::unique_ptr<receiver::SpectrumAnalyzer> spec_;  ///< averaged FFT of the samples
    double spec_centre_ = 0.0, spec_rate_ = 0.0;        ///< what spec_ was fed
    int64_t spec_next_ns_ = 0;                          ///< host time of the next spectrum (0: now)
    bool spec_overload_ = false;                        ///< the averaged samples clipped
};

// The FFT size for a sample rate: about 1 kHz bins, 256..4096 (at most
// YR_SPECTRUM_MAX_BINS, as the band's bins are a part of them).
std::size_t spectrum_fft_size(double rate)
{
    std::size_t n = 256;
    while (n < YR_SPECTRUM_MAX_BINS && static_cast<double>(n) * 1000.0 < rate)
        n *= 2;
    return n;
}

void Stream::feed_spectrum(std::size_t n, int64_t t_now)
{
    if (!spectrum_.on || n == 0 || (spec_next_ns_ != 0 && t_now < spec_next_ns_))
        return;
    if (!spec_ || spec_centre_ != centre_ || spec_rate_ != rate_) {
        receiver::SpectrumConfig cfg;
        cfg.fft_size = spectrum_fft_size(rate_);
        cfg.averages = kSpectrumAverages;
        spec_ = std::make_unique<receiver::SpectrumAnalyzer>(cfg);
        spec_->set_stream(centre_, rate_);
        spec_centre_ = centre_;
        spec_rate_ = rate_;
        spec_overload_ = false;
    }
    for (std::size_t i = 0; i < n && !spec_overload_; ++i)
        spec_overload_ = std::fabs(buf_[i].real()) >= 0.999f || std::fabs(buf_[i].imag()) >= 0.999f;
    if (spec_->push(buf_.data(), n) == 0)
        return;
    receiver::SpectrumFrame f;
    spec_->pop(f);
    spec_->reset();
    const bool overload = spec_overload_;
    spec_overload_ = false;
    // Once a second; after a stall the next one is a second from now.
    spec_next_ns_ = spec_next_ns_ == 0 || t_now - spec_next_ns_ > 1'000'000'000
                        ? t_now + 1'000'000'000
                        : spec_next_ns_ + 1'000'000'000;
    // The plan's band within the span received now and the tunable range.
    const double half = kSpanFraction * rate_ / 2.0;
    double first = 0.0, last = 0.0;
    if (!plugins::clip_band(spectrum_.first_hz, spectrum_.last_hz,
                            std::max(centre_ - half, d_->caps.first_hz),
                            std::min(centre_ + half, d_->caps.last_hz), first, last))
        return;
    const double enbw_db = 10.0 * std::log10(f.enbw_bins);
    plugins::SpectrumOut out;
    out.bin_width_hz = f.bin_hz();
    for (std::size_t i = 0; i < f.size() && out.power_db.size() < YR_SPECTRUM_MAX_BINS; ++i) {
        const double hz = f.frequency_hz(i);
        if (hz < first || hz > last)
            continue;
        if (out.power_db.empty())
            out.start_hz = hz - out.bin_width_hz / 2.0;
        // A finite floor for silent bins (a zero sample block).
        out.power_db.push_back(std::max(static_cast<double>(f.dbfs[i]) - enbw_db, -300.0));
    }
    out.flags = overload ? YR_RECORD_OVERLOAD : 0u;
    out.time.monotonic_ns = t_now;
    out.time.utc_ns = p_.host->utc_now_ns(p_.host->host);
    out.time.utc_source = YR_TIME_SOURCE_SYSTEM;
    plugins::push_spectrum(*p_.host, d_->sink, out);
}

void Stream::build(const Settings& s)
{
    EngineConfig cfg;
    cfg.record_interval_s = 1.0 / s.record_rate_hz;
    plugins::apply_plan(plan_, cfg);
    cfg.threads = static_cast<unsigned>(s.threads);
    cfg.ms_max_power_dbm = s.ms_max_power_dbm;
    std::string warning;
    cfg.gsmtap = plugins::gsmtap_options(s.gsmtap, warning);
    if (!warning.empty())
        log(p_, d_, YR_LOG_WARNING, "GSMTAP output off: " + warning);
    eng_ = std::make_unique<MeasurementEngine>(cfg);
    cell_ = std::make_unique<radios::RecordFiller>(eng_->schemas().at(0), 0);
}

void Stream::tune(double lo_hz)
{
    // Without a correction in the module, tune off by the oscillator error.
    sdr_->setFrequency(SOAPY_SDR_RX, 0, lo_hz / lo_factor_);
    centre_ = sdr_->getFrequency(SOAPY_SDR_RX, 0) * lo_factor_;
    d_->tuned_hz = centre_;
}

// C1 from the dBFS level and the calibration offset (Profile::c1).
void Stream::annotate(EngineOutput& out)
{
    if (!pr_.c1 || !s_.c1_enabled)
        return;
    for (radios::Record& r : out.records)
        if (r.schema_index == 0)
            plugins::set_c1(r, *cell_, s_.c1_offset_db, s_.ms_max_power_dbm);
}

bool Stream::poll_settings(EngineOutput& out)
{
    std::lock_guard lock(d_->mutex);
    if (d_->settings_version == version_)
        return false;
    version_ = d_->settings_version;
    const Settings now = d_->settings;
    const bool rebuild = now.record_rate_hz != s_.record_rate_hz || now.ms_max_power_dbm != s_.ms_max_power_dbm;
    s_ = now;
    apply_settings(*d_, s_, false);
    discontinuity_ = true; // gains or port changed: the levels start over
    if (rebuild) {
        eng_->finish(out);
        deliver(out);
        build(s_);
    }
    return rebuild;
}

void Stream::measure_floor()
{
    std::vector<double> levels;
    for (const receiver::tetra::ChannelLevel& c : eng_->search().channel_levels())
        if (!c.carrier && c.level_dbfs > -250.0)
            levels.push_back(c.level_dbfs);
    if (levels.empty())
        return;
    // The 20 % quietest channels: the noise floor without the carriers.
    const std::size_t k = levels.size() / 5;
    std::nth_element(levels.begin(), levels.begin() + static_cast<std::ptrdiff_t>(k), levels.end());
    d_->noise_floor_dbfs = levels[k];
}

bool Stream::stopping() const
{
    std::lock_guard lock(d_->mutex);
    return d_->stop;
}

bool Stream::step(EngineOutput& out)
{
    last_samples_ = 0;
    if (stopping())
        return false;
    poll_settings(out);
    void* buffs[] = {buf_.data()};
    int flags = 0;
    long long time_ns = 0;
    const int n = sdr_->readStream(st_, buffs, buf_.size(), flags, time_ns, kReadTimeoutUs);
    if (n == SOAPY_SDR_TIMEOUT)
        return true;
    if (n == SOAPY_SDR_OVERFLOW) {
        const uint64_t count = ++d_->overflows;
        event(YR_EVENT_OVERFLOW, static_cast<int64_t>(count));
        discontinuity_ = true;
        need_base_ = true;
        return true;
    }
    if (n < 0) {
        // Unplugged or failing: the hot-plug watcher decides (RAD-35).
        if (++errors_ == 50)
            log(p_, d_, YR_LOG_WARNING, std::string("the module keeps failing to read: ") + SoapySDR::errToStr(n));
        std::unique_lock lock(d_->mutex);
        d_->wake.wait_for(lock, std::chrono::milliseconds(20), [this] { return d_->stop; });
        return true;
    }
    errors_ = 0;
    if (n == 0)
        return true;
    const int64_t t_now = now_ns(p_);
    d_->last_data_ns = t_now;
    const bool timed = (flags & SOAPY_SDR_HAS_TIME) != 0;
    if (timed && expected_ns_ >= 0) {
        const double gap = static_cast<double>(time_ns - expected_ns_) * 1e-9 * rate_;
        if (gap >= 1.0) {
            const auto lost = static_cast<uint64_t>(std::llround(gap));
            d_->samples_dropped += lost;
            event(YR_EVENT_DROPPED, static_cast<int64_t>(lost));
            discontinuity_ = true;
            need_base_ = true;
        }
    } else if (discontinuity_ && !timed) {
        event(YR_EVENT_DROPPED, 0); // lost samples, count unknown
    }
    expected_ns_ = timed ? time_ns + static_cast<long long>(std::llround(n / rate_ * 1e9)) : -1;
    const auto t0 = std::chrono::steady_clock::now();
    BlockInfo bi;
    bi.discontinuity = discontinuity_;
    if (need_base_) {
        bi.monotonic_ns = t_now - static_cast<int64_t>(std::llround(n / rate_ * 1e9));
        bi.utc_ns = p_.host->utc_now_ns(p_.host->host) + (bi.monotonic_ns - t_now);
        bi.utc_source = YR_TIME_SOURCE_SYSTEM;
    }
    // The block goes through the engine in pieces, and a stop between two
    // pieces drops the rest: stop_stream waits for this thread, and on a
    // slow or loaded machine a whole MTU (131072 samples) took seconds,
    // more than the RAD-15 call limit.
    std::size_t done = 0;
    while (done < static_cast<std::size_t>(n)) {
        const std::size_t piece = std::min(kProcessPiece, static_cast<std::size_t>(n) - done);
        uint64_t clipped = 0;
        for (std::size_t i = done; i < done + piece; ++i)
            if (std::fabs(buf_[i].real()) >= 0.999f || std::fabs(buf_[i].imag()) >= 0.999f)
                ++clipped;
        bi.overload = clipped > 0;
        clipped_ += clipped;
        values_ += piece;
        eng_->process(buf_.data() + done, piece, centre_, rate_, bi, out);
        done += piece;
        bi = BlockInfo{}; // the next piece continues this one
        if (done < static_cast<std::size_t>(n) && stopping())
            break;
    }
    discontinuity_ = false;
    need_base_ = false;
    busy_s_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    signal_s_ += static_cast<double>(done) / rate_;
    floor_s_ += static_cast<double>(done) / rate_;
    d_->clipped_percent = 100.0 * static_cast<double>(clipped_) / static_cast<double>(values_);
    if (signal_s_ >= 1.0) {
        d_->load = busy_s_ / signal_s_;
        busy_s_ = signal_s_ = 0.0;
        clipped_ = values_ = 0;
    }
    if (floor_s_ >= 0.5) {
        measure_floor();
        floor_s_ = 0.0;
    }
    feed_spectrum(done, t_now);
    last_samples_ = done;
    return true;
}

// Band scan (RAD-23): the sweep's centres one after the other, each for
// the dwell time; per pass one scan record per raster channel of the
// band (with the cell identity where a carrier was decoded there), then
// SCAN_DONE. Cell records do not go out during a scan.
void Stream::run_scan(EngineOutput& out)
{
    const double first = d_->plan.scan_first_hz, last = d_->plan.scan_last_hz;
    std::vector<double> centres = receiver::tetra::sweep_centres(first, last, rate_, kSpanFraction);
    if (centres.empty())
        centres.push_back((first + last) / 2.0);
    const bool retune = centres.size() > 1;
    std::size_t at = 0;
    int64_t pass_count = 0;
    std::set<uint64_t> sent; // channels of this pass already out
    for (;;) {
        std::map<uint64_t, radios::Record> scan, cells;
        if (retune || eng_ == nullptr) {
            tune(centres[at]);
            build(s_);
            discontinuity_ = true;
            need_base_ = true;
            expected_ns_ = -1;
        }
        double dwell = 0.0;
        while (dwell < s_.scan_dwell_s) {
            if (!step(out))
                return;
            dwell += static_cast<double>(last_samples_) / rate_;
            for (radios::Record& r : out.records) {
                const uint64_t hz = r.values.at(YR_CORE_FREQUENCY_HZ).u64;
                if (r.schema_index == 2)
                    scan[hz] = std::move(r);
                else if (r.schema_index == 0 && r.values.at(YR_CORE_LA).type == YR_TYPE_UINT)
                    cells[hz] = std::move(r);
            }
            out.records.clear();
            std::erase_if(out.events, [](const radios::Event& e) { return e.event == YR_EVENT_SCAN_DONE; });
            plugins::push_output(*p_.host, d_->sink, out, d_->counters);
            out.clear();
        }
        // The channels this centre holds: inside the band, not yet sent.
        for (auto& [hz, r] : scan) {
            const double f = static_cast<double>(hz);
            if (f < first - 1.0 || f > last + 1.0 || !sent.insert(hz).second)
                continue;
            if (retune && (f < centres[at] - (0.4 * rate_ - 12.5e3) || f > centres[at] + (0.4 * rate_ - 12.5e3)))
                continue;
            // A decoded carrier within 3 kHz: its identity (RAD-23).
            for (const auto& [chz, c] : cells)
                if (std::fabs(static_cast<double>(chz) - f) <= 3e3) {
                    for (unsigned k : {YR_CORE_CHANNEL, YR_CORE_MCC, YR_CORE_MNC, YR_CORE_LA, YR_CORE_COLOUR_CODE})
                        r.values.at(static_cast<std::size_t>(k)) = c.values.at(static_cast<std::size_t>(k));
                    r.flags &= ~YR_RECORD_NO_SYNC;
                    break;
                }
            out.records.push_back(std::move(r));
        }
        pass_count += static_cast<int64_t>(out.records.size());
        plugins::push_output(*p_.host, d_->sink, out, d_->counters);
        out.clear();
        at = (at + 1) % centres.size();
        if (at == 0 && pass_count > 0) {
            event(YR_EVENT_SCAN_DONE, pass_count);
            pass_count = 0;
            sent.clear();
        }
    }
}

void Stream::run()
{
    {
        std::lock_guard lock(d_->mutex);
        s_ = d_->settings;
        version_ = d_->settings_version;
        plan_ = d_->plan_setup;
        spectrum_ = plugins::spectrum_plan(*p_.host, d_->plan);
    }
    sdr_ = d_->sdr;
    SoapySDR::Device& sdr = *sdr_;
    apply_settings(*d_, s_, true);
    rate_ = sdr.getSampleRate(SOAPY_SDR_RX, 0);
    d_->rate_hz = rate_;
    lo_factor_ = d_->caps.frequency_correction ? 1.0 : 1.0 + s_.ppm * 1e-6;
    d_->noise_floor_dbfs = -1e9;
    d_->clipped_percent = 0.0;

    SoapySDR::Stream* st = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32, {0});
    st_ = st;
    struct Closer {
        SoapySDR::Device& sdr;
        SoapySDR::Stream* st;
        ~Closer()
        {
            try {
                sdr.deactivateStream(st);
                sdr.closeStream(st);
            } catch (...) {
            }
        }
    } closer{sdr, st};
    if (plan_.type != YR_PLAN_SCAN) {
        double centre = s_.tune_mhz * 1e6;
        if (plan_.serving_hz)
            centre = *plan_.serving_hz;
        else if (!plan_.carriers.empty())
            centre = (*std::min_element(plan_.carriers.begin(), plan_.carriers.end()) +
                      *std::max_element(plan_.carriers.begin(), plan_.carriers.end())) / 2.0;
        const double offset = pr_.offset_tuning_hz > 0.0 ? s_.offset_khz * 1e3 : 0.0;
        tune(choose_lo(plan_.carriers, centre, rate_, offset));
        build(s_);
    }
    if (const int r = sdr.activateStream(st); r != 0)
        throw std::runtime_error(std::string("the module could not start the stream: ") + SoapySDR::errToStr(r));
    const std::size_t mtu = std::max<std::size_t>(sdr.getStreamMTU(st), 1024);
    buf_.resize(std::max<std::size_t>(mtu, 16384));
    EngineOutput out;
    d_->last_data_ns = now_ns(p_);
    if (plan_.type == YR_PLAN_SCAN) {
        run_scan(out);
    } else {
        while (step(out))
            deliver(out);
    }
    if (eng_) {
        if (plan_.type == YR_PLAN_SCAN) {
            out.clear();
        } else {
            eng_->finish(out);
            deliver(out);
        }
    }
}

void stream_main(Device* d)
{
    try {
        Stream(d).run();
    } catch (const std::exception& e) {
        std::lock_guard lock(d->mutex);
        if (d->state == YR_DEVICE_STREAMING && !d->stop) {
            d->error = std::string("the stream stopped: ") + e.what();
            d->state = YR_DEVICE_OPEN;
            d->plugin->host->stream_event(d->sink, YR_EVENT_ERROR, 0);
        }
    }
}

// --- hot-plug (RAD-35) --------------------------------------------------------------

// Closes the module's handle of a device that went away.
void drop_handle(Device* d)
{
    join_stream(d);
    try {
        delete d->sdr;
    } catch (...) {
    }
    d->sdr = nullptr;
}

void watch(Plugin* p)
{
    for (;;) {
        {
            std::unique_lock lock(p->mutex);
            if (p->quit_cv.wait_for(lock, kScanPeriod, [p] { return p->quit; }))
                return;
        }
        std::vector<Found> found = find_devices(p->options->filter);
        std::vector<Device*> back;
        std::set<std::string> keys;
        for (const Found& f : found)
            keys.insert(f.key);
        // Delivered below under the locks, so no event reaches a sink after
        // stop_stream returned (RAD-24/25).
        std::vector<std::tuple<Device*, yr_stream*, uint32_t>> events;
        bool changed = false;
        {
            std::lock_guard plock(p->mutex);
            if (p->quit)
                return;
            for (Device* d : p->devices) {
                const bool present = keys.count(d->found.key) != 0;
                std::unique_lock lock(d->mutex);
                if (d->state == YR_DEVICE_LOST) {
                    if (!present)
                        continue;
                    // Back: a new handle; the stream starts again from Yantra.
                    lock.unlock();
                    drop_handle(d);
                    const auto it = std::find_if(found.begin(), found.end(),
                                                 [d](const Found& f) { return f.key == d->found.key; });
                    SoapySDR::Device* fresh = nullptr;
                    try {
                        fresh = make_device(*it);
                    } catch (const std::exception& e) {
                        log(*p, d, YR_LOG_WARNING,
                            std::string("the device is back but could not be opened: ") + e.what());
                    }
                    lock.lock();
                    if (fresh == nullptr) {
                        keys.erase(d->found.key);
                        continue;
                    }
                    d->sdr = fresh;
                    d->state = YR_DEVICE_OPEN;
                    d->absent_scans = 0;
                    // A replugged device starts with the antenna port unpowered (RAD-60).
                    try {
                        power_off(*d);
                    } catch (const std::exception& e) {
                        log(*p, d, YR_LOG_WARNING, std::string("antenna port power off failed: ") + e.what());
                    }
                    ++d->settings_version;
                    back.push_back(d);
                    if (d->sink != nullptr)
                        events.emplace_back(d, d->sink, YR_EVENT_DEVICE_BACK);
                    continue;
                }
                if (present) {
                    d->absent_scans = 0;
                    continue;
                }
                // Not found: a module may not list a device it has open, so
                // it is gone only when its samples stopped too (streaming) or
                // after two scans (open).
                bool lost = false;
                if (d->state == YR_DEVICE_STREAMING)
                    lost = now_ns(*p) - d->last_data_ns.load() > kStallNs;
                else
                    lost = ++d->absent_scans >= 2;
                if (!lost) {
                    keys.insert(d->found.key);
                    continue;
                }
                d->state = YR_DEVICE_LOST;
                d->stop = true;
                d->error = "the device was unplugged or stopped answering";
                if (d->sink != nullptr)
                    events.emplace_back(d, d->sink, YR_EVENT_DEVICE_LOST);
                lock.unlock();
                d->wake.notify_all();
            }
            std::vector<std::string> now(keys.begin(), keys.end());
            if (now != p->listed) {
                p->listed = now;
                changed = true;
            }
        }
        if (!events.empty()) {
            // The device and its sink are checked again: sp_close removes a
            // device under the plugin mutex and sp_stop clears the sink under
            // the device mutex, so a sink gets no event once its stream was
            // stopped. The host's stream_event only queues the event.
            std::lock_guard plock(p->mutex);
            for (const auto& [d, sink, e] : events) {
                if (std::find(p->devices.begin(), p->devices.end(), d) == p->devices.end())
                    continue;
                std::lock_guard lock(d->mutex);
                if (d->sink != sink)
                    continue;
                p->host->stream_event(sink, e, 0);
                if (e == YR_EVENT_DEVICE_BACK)
                    d->sink = nullptr; // the stream starts again from Yantra
            }
        }
        for (Device* d : back)
            p->host->settings_changed(p->host->host, reinterpret_cast<yr_device*>(d));
        if (changed)
            p->host->devices_changed(p->host->host);
    }
}

// --- the radio API ------------------------------------------------------------------

yr_status sp_init(const yr_host_api* host, yr_plugin** out)
{
    YANTRA_PLUGIN_GUARD({
        if (host == nullptr || out == nullptr || g_options == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        auto* p = new Plugin();
        p->host = host;
        p->options = g_options;
        g_log_host = host;
        SoapySDR::registerLogHandler(soapy_log);
        SoapySDR::setLogLevel(SOAPY_SDR_WARNING);
        for (const Found& f : find_devices(p->options->filter))
            p->listed.push_back(f.key);
        std::sort(p->listed.begin(), p->listed.end());
        log(*p, nullptr, YR_LOG_INFO, diagnostics(p->options->filter));
        p->watcher = std::thread(watch, p);
        *out = reinterpret_cast<yr_plugin*>(p);
        return YR_OK;
    })
}

void sp_shutdown(yr_plugin* plugin)
{
    Plugin* p = P(plugin);
    {
        std::lock_guard lock(p->mutex);
        p->quit = true;
    }
    p->quit_cv.notify_all();
    if (p->watcher.joinable())
        p->watcher.join();
    SoapySDR::registerLogHandler(nullptr);
    g_log_host = nullptr;
    delete p;
}

yr_status sp_enumerate(yr_plugin* plugin, yr_device_cb cb, void* user)
{
    YANTRA_PLUGIN_GUARD({
        Plugin* p = P(plugin);
        std::string problems;
        std::vector<Found> found = find_devices(p->options->filter, &problems);
        std::lock_guard lock(p->mutex);
        // Open devices a module does not list while it has them open.
        for (Device* d : p->devices) {
            std::lock_guard dl(d->mutex);
            if (d->state != YR_DEVICE_LOST &&
                std::none_of(found.begin(), found.end(), [d](const Found& f) { return f.key == d->found.key; }))
                found.push_back(d->found);
        }
        for (const Found& f : found)
            report(f, cb, user);
        p->error = found.empty() ? diagnostics(p->options->filter) : problems;
        return found.empty() ? YR_ERR_NO_DEVICE : YR_OK; // RAD-30
    })
}

yr_status sp_open(yr_plugin* plugin, const char* key, yr_device** out)
{
    YANTRA_PLUGIN_GUARD({
        Plugin* p = P(plugin);
        if (key == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        {
            std::lock_guard lock(p->mutex);
            for (Device* d : p->devices)
                if (d->found.key == key) {
                    p->error = std::string(key) + " is already open; close it first";
                    return YR_ERR_BUSY;
                }
        }
        const std::vector<Found> found = find_devices(p->options->filter);
        const auto it = std::find_if(found.begin(), found.end(), [key](const Found& f) { return f.key == key; });
        if (it == found.end()) {
            std::lock_guard lock(p->mutex);
            p->error = std::string("no device ") + key + "; " + diagnostics(p->options->filter);
            return YR_ERR_NOT_FOUND;
        }
        auto d = std::make_unique<Device>();
        d->plugin = p;
        d->found = *it;
        d->module_text = module_text(it->driver);
        try {
            d->sdr = make_device(*it);
            read_caps(*d);
            build_table(*d);
            power_off(*d); // antenna port power is never left on from an earlier user (RAD-60)
        } catch (const std::exception& e) {
            delete d->sdr;
            std::lock_guard lock(p->mutex);
            p->error = std::string("could not open ") + key + ": " + e.what() +
                       " (is it in use by another program? are the udev rules installed?)";
            return YR_ERR_IO;
        }
        fill_info(*d);
        d->plan.struct_size = sizeof d->plan;
        d->plan.type = YR_PLAN_SERVING;
        std::lock_guard lock(p->mutex);
        p->devices.push_back(d.get());
        p->error.clear();
        *out = reinterpret_cast<yr_device*>(d.release());
        return YR_OK;
    })
}

yr_status check_plan(Device* d, const yr_plan* plan, plugins::PlanSetup& setup)
{
    if (plan == nullptr || !YR_HAS_FIELD(plan, yr_plan, scan_step_hz) ||
        (plan->type == YR_PLAN_CARRIERS && plan->carrier_count > 0 && plan->carriers_hz == nullptr)) {
        set_device_error(d, "a plan is needed");
        return YR_ERR_INVALID_ARGUMENT;
    }
    std::vector<uint64_t> carriers;
    if (plan->type == YR_PLAN_CARRIERS)
        carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
    std::string why;
    const bool scan = d->plugin->options->profile.scan;
    yr_status st = plugins::plan_setup(*plan, carriers, d->caps.first_hz, d->caps.last_hz,
                                       kMaxCarriers, scan, setup, why);
    if (st == YR_OK && plan->type == YR_PLAN_SCAN) {
        const double first = static_cast<double>(plan->scan_first_hz);
        const double last = static_cast<double>(plan->scan_last_hz);
        if (first > last || first < d->caps.first_hz || last > d->caps.last_hz) {
            why = "the scan band " + radio::number_text(first / 1e6) + " to " + radio::number_text(last / 1e6) +
                  " MHz is empty or outside the device's range";
            st = YR_ERR_INVALID_ARGUMENT;
        } else if (plan->scan_step_hz != 0 && plan->scan_step_hz != 25'000) {
            why = "the scan raster is 25 kHz (TETRA channels)";
            st = YR_ERR_INVALID_ARGUMENT;
        }
    }
    if (st == YR_OK && !carriers.empty()) {
        double rate = 0.0;
        {
            std::lock_guard lock(d->mutex);
            rate = d->caps.rates.at(d->settings.rate);
        }
        const auto [lo, hi] = std::minmax_element(carriers.begin(), carriers.end());
        const double span = static_cast<double>(*hi - *lo) + 25e3;
        if (span > kSpanFraction * rate) {
            why = "the carriers span " + radio::number_text(std::round(span / 1e3) / 1e3) +
                  " MHz, more than the " + radio::number_text(kSpanFraction * rate / 1e6) +
                  " MHz measured at once at this sample rate";
            st = YR_ERR_INVALID_ARGUMENT;
        }
    }
    if (st != YR_OK)
        set_device_error(d, why);
    return st;
}

yr_status sp_start(yr_device* device, const yr_plan* plan, yr_stream* sink)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (sink == nullptr) {
            set_device_error(d, "start_stream needs a sink");
            return YR_ERR_INVALID_ARGUMENT;
        }
        {
            std::lock_guard lock(d->mutex);
            if (d->state == YR_DEVICE_LOST) {
                d->error = "the device was unplugged; plug it in again";
                return YR_ERR_DEVICE_LOST;
            }
            if (d->state != YR_DEVICE_OPEN)
                return YR_ERR_BUSY;
        }
        plugins::PlanSetup setup;
        if (const yr_status st = check_plan(d, plan, setup); st != YR_OK)
            return st;
        join_stream(d); // a stream that stopped on an error
        std::lock_guard lock(d->mutex);
        yr_plan_read(plan, &d->plan); // only what the host's struct_size holds (RAD-10)
        d->carriers.clear();
        if (plan->type == YR_PLAN_CARRIERS)
            d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        d->plan.carriers_hz = nullptr; // valid during the call only (RAD-10)
        d->plan_setup = setup;
        d->sink = sink;
        d->stop = false;
        d->counters.reset();
        d->samples_dropped = 0;
        d->overflows = 0;
        d->state = YR_DEVICE_STREAMING;
        d->last_data_ns = now_ns(*d->plugin);
        std::lock_guard jl(d->join_mutex);
        d->thread = std::thread(stream_main, d);
        return YR_OK;
    })
}

yr_status sp_stop(yr_device* device)
{
    Device* d = D(device);
    {
        std::lock_guard lock(d->mutex);
        d->stop = true;
    }
    d->wake.notify_all();
    join_stream(d);
    std::lock_guard lock(d->mutex);
    if (d->state != YR_DEVICE_LOST)
        d->state = YR_DEVICE_OPEN;
    d->sink = nullptr;
    return YR_OK;
}

void sp_close(yr_device* device)
{
    Device* d = D(device);
    sp_stop(device);
    {
        std::lock_guard lock(d->plugin->mutex);
        auto& v = d->plugin->devices;
        v.erase(std::remove(v.begin(), v.end(), d), v.end());
    }
    drop_handle(d);
    delete d;
}

yr_status sp_device_info(yr_device* device, yr_device_cb cb, void* user)
{
    cb(user, &D(device)->info);
    return YR_OK;
}

uint32_t sp_state(yr_device* device)
{
    std::lock_guard lock(D(device)->mutex);
    return D(device)->state;
}

yr_status sp_plan_ranges(yr_device* device, yr_plan_ranges_cb cb, void* user)
{
    Device* d = D(device);
    double rate = 0.0;
    {
        std::lock_guard lock(d->mutex);
        rate = d->caps.rates.at(d->settings.rate);
    }
    const yr_freq_range range = {static_cast<uint64_t>(std::ceil(d->caps.first_hz)),
                                 static_cast<uint64_t>(std::floor(d->caps.last_hz))};
    const uint32_t types = (1u << YR_PLAN_SERVING) | (1u << YR_PLAN_CARRIERS) |
                           (d->plugin->options->profile.scan ? (1u << YR_PLAN_SCAN) : 0u);
    const yr_plan_ranges r = {sizeof r, types, 1, kMaxCarriers, &range, kSpanFraction * rate};
    cb(user, &r);
    return YR_OK;
}

yr_status sp_set_plan(yr_device* device, const yr_plan* plan)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        {
            std::lock_guard lock(d->mutex);
            if (d->state == YR_DEVICE_STREAMING) {
                d->error = "stop the stream to change the plan";
                return YR_ERR_BUSY;
            }
        }
        plugins::PlanSetup setup;
        if (const yr_status st = check_plan(d, plan, setup); st != YR_OK)
            return st;
        std::lock_guard lock(d->mutex);
        yr_plan_read(plan, &d->plan); // only what the host's struct_size holds (RAD-10)
        d->carriers.clear();
        if (plan->type == YR_PLAN_CARRIERS)
            d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        d->plan.carriers_hz = nullptr;
        return YR_OK;
    })
}

yr_status sp_stats(yr_device* device, yr_stream_stats* out)
{
    if (out == nullptr || !YR_HAS_FIELD(out, yr_stream_stats, sample_rate_hz))
        return YR_ERR_INVALID_ARGUMENT;
    Device* d = D(device);
    out->records_delivered = d->counters.delivered.load();
    out->records_dropped = 0;
    out->samples_dropped = d->samples_dropped.load();
    out->overflows = d->overflows.load();
    out->receiver_load = d->load.load();
    out->sample_rate_hz = d->rate_hz.load();
    return YR_OK;
}

yr_status sp_last_error(yr_plugin* plugin, yr_device* device, char* buffer, uint32_t size)
{
    std::string text;
    if (device != nullptr) {
        std::lock_guard lock(D(device)->mutex);
        text = D(device)->error;
    } else if (plugin != nullptr) {
        std::lock_guard lock(P(plugin)->mutex);
        text = P(plugin)->error;
    }
    return radio::copy_text(text, buffer, size);
}


yr_status sp_list_settings(yr_device* device, yr_setting_cb cb, void* user)
{
    for (const yr_setting_def& s : D(device)->table.defs)
        cb(user, &s);
    return YR_OK;
}

yr_status sp_get(yr_device* device, const char* key, yr_value* out, char* text, uint32_t size)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (key == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        std::lock_guard lock(d->mutex);
        const yr_status st = get_setting(*d, key, out, text, size);
        if (st == YR_ERR_NOT_FOUND)
            d->error = std::string("unknown setting ") + key;
        return st;
    })
}

yr_status sp_set(yr_device* device, const char* key, const yr_value* value)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        std::unique_lock lock(d->mutex);
        const yr_setting_def* def = plugins::find_setting(d->table.defs, key);
        if (def == nullptr || value == nullptr) {
            d->error = std::string("unknown setting ") + (key != nullptr ? key : "(null)");
            return YR_ERR_NOT_FOUND;
        }
        if (d->state == YR_DEVICE_LOST) {
            d->error = "the device was unplugged; plug it in again";
            return YR_ERR_DEVICE_LOST;
        }
        std::string why;
        if ((def->flags & YR_SETTING_UNAVAILABLE) != 0) {
            d->error = std::string(def->label) + ": " + def->unavailable_reason;
            return YR_ERR_UNSUPPORTED;
        }
        if (radio::check_setting_value(*def, *value, &why) != YR_OK) {
            d->error = why;
            return YR_ERR_INVALID_ARGUMENT;
        }
        if ((def->flags & YR_SETTING_RESTART) != 0 && d->state == YR_DEVICE_STREAMING) {
            d->error = std::string(def->label) + " changes only while stopped";
            return YR_ERR_BUSY;
        }
        store_setting(*d, *def, *value);
        ++d->settings_version; // a running stream applies it
        return YR_OK;
    })
}

yr_status sp_action(yr_device* device, const char*, char*, uint32_t)
{
    set_device_error(D(device), "unknown action");
    return YR_ERR_NOT_FOUND;
}

yr_status sp_list_presets(yr_device* device, yr_preset_cb cb, void* user)
{
    for (const Preset& p : D(device)->plugin->options->profile.presets) {
        const yr_preset x = {sizeof(yr_preset), 0, p.id, p.label, p.help};
        cb(user, &x);
    }
    return YR_OK;
}

yr_status sp_apply_preset(yr_device* device, const char* id)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (id == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        {
            std::lock_guard lock(d->mutex);
            if (d->state == YR_DEVICE_LOST) {
                d->error = "the device was unplugged; plug it in again";
                return YR_ERR_DEVICE_LOST;
            }
            if (!apply_preset(*d, id))
                return YR_ERR_NOT_FOUND;
            ++d->settings_version; // a running stream applies the gains
        }
        // Several settings changed at once (RAD-44).
        d->plugin->host->settings_changed(d->plugin->host->host, device);
        return YR_OK;
    })
}

yr_status sp_list_sensors(yr_device* device, yr_setting_cb cb, void* user)
{
    for (const yr_setting_def& s : D(device)->table.sensors)
        cb(user, &s);
    return YR_OK;
}

yr_status sp_read_sensor(yr_device* device, const char* key, yr_value* out, char* text, uint32_t size)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (key == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        std::lock_guard lock(d->mutex);
        if (d->state == YR_DEVICE_LOST) {
            d->error = "the device was unplugged; plug it in again";
            return YR_ERR_DEVICE_LOST;
        }
        const yr_status st = read_sensor(*d, key, out, text, size);
        if (st == YR_ERR_NOT_FOUND)
            d->error = std::string("unknown sensor ") + key;
        return st;
    })
}

yr_status sp_self_test(yr_device* device, yr_selftest_cb cb, void* user)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        {
            std::lock_guard lock(d->mutex);
            if (d->state == YR_DEVICE_STREAMING)
                return YR_ERR_BUSY;
            if (d->state == YR_DEVICE_LOST)
                return YR_ERR_DEVICE_LOST;
        }
        using plugins::selftest_item;
        const yr_selftest_item a =
            selftest_item(YR_TEST_PASS, "module", "SoapySDR module", d->module_text.c_str(), "");
        cb(user, &a);
        {
            std::lock_guard lock(d->mutex);
            profile_self_test(*d, [&](uint32_t result, const std::string& name, const std::string& label,
                                      const std::string& detail, const std::string& remedy) {
                const yr_selftest_item x =
                    selftest_item(result, name.c_str(), label.c_str(), detail.c_str(), remedy.c_str());
                cb(user, &x);
            });
        }
        // A short read: the module streams and the samples arrive.
        std::string detail;
        bool ok = false;
        try {
            SoapySDR::Device& sdr = *d->sdr;
            Settings s;
            {
                std::lock_guard lock(d->mutex);
                s = d->settings;
            }
            apply_settings(*d, s, true);
            SoapySDR::Stream* st = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32, {0});
            std::vector<std::complex<float>> buf(16384);
            std::size_t got = 0;
            if (sdr.activateStream(st) == 0) {
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (got < 100000 && std::chrono::steady_clock::now() < end) {
                    void* b[] = {buf.data()};
                    int flags = 0;
                    long long t = 0;
                    const int n = sdr.readStream(st, b, buf.size(), flags, t, kReadTimeoutUs);
                    if (n > 0)
                        got += static_cast<std::size_t>(n);
                }
                sdr.deactivateStream(st);
            }
            sdr.closeStream(st);
            ok = got >= 100000;
            detail = std::to_string(got) + " values read in at most 2 s at " +
                     radio::number_text(sdr.getSampleRate(SOAPY_SDR_RX, 0) / 1e6) + " Msps";
        } catch (const std::exception& e) {
            detail = std::string("the module failed: ") + e.what();
        }
        const yr_selftest_item b = selftest_item(ok ? YR_TEST_PASS : YR_TEST_FAIL, "stream",
                                                 "Receive stream", detail.c_str(),
                                                 ok ? "" : "check the USB connection and power");
        cb(user, &b);
        return YR_OK;
    })
}

const yr_stream_control kStreamControl = {
    sizeof(yr_stream_control), 0,        sp_enumerate,   sp_open,     sp_close,
    sp_device_info,            sp_state, sp_plan_ranges, sp_set_plan, sp_start,
    sp_stop,                   sp_stats, sp_last_error};

const yr_device_control kDeviceControl = {
    sizeof(yr_device_control), 0, sp_list_settings, sp_get, sp_set, sp_action,
    sp_list_presets, sp_apply_preset, sp_list_sensors, sp_read_sensor, sp_self_test};

} // namespace

void stream_thread(Device* d) { stream_main(d); }

void join_stream(Device* d)
{
    std::lock_guard lock(d->join_mutex);
    if (d->thread.joinable() && d->thread.get_id() != std::this_thread::get_id())
        d->thread.join();
}

const yr_descriptor* descriptor(const Options& o)
{
    g_options = &o;
    module_table() = o.modules;
    static const yr_descriptor desc = {
        sizeof(yr_descriptor),
        yr_api_version_compiled(),
        YR_CAP_HOTPLUG | YR_CAP_SPECTRUM | (o.profile.scan ? YR_CAP_SCAN : 0u),
        o.name,
        o.label,
        o.version,
        "The Yantra contributors",
        "GPL-3.0-or-later AND AGPL-3.0-or-later", // the receiver uses osmo-tetra (RAD-05)
        o.description,
        o.source_url,
        "tetra",
        schema_table().count(),
        0,
        schema_table().schemas(),
        nullptr,
        sp_init,
        sp_shutdown,
        &kStreamControl,
        &kDeviceControl,
        nullptr, // no SCENE capability
    };
    return &desc;
}

} // namespace yantra::soapy
