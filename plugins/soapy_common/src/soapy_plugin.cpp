// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio API of a SoapySDR plugin (RAD-54, RAD-62): open/close by
// stable key, plans, the stream into the measurement engine with
// overflow and drop events (RAD-24, RAD-25), hot-plug (RAD-35),
// generated settings and sensors (RAD-51), diagnostics (RAD-52), self
// test. Glue modelled on plugins/simulator (same state machine, RAD-34).
#include "soapy_device.hpp"

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
#include <set>
#include <sstream>

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

Plugin* P(yr_plugin* p) { return reinterpret_cast<Plugin*>(p); }
Device* D(yr_device* d) { return reinterpret_cast<Device*>(d); }

const plugins::TetraSchemaTable& schema_table()
{
    static const plugins::TetraSchemaTable t(radios::TetraSchemaOptions{false, false, false});
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
    explicit Stream(Device* d) : d_(d), p_(*d->plugin) {}
    void run();

private:
    void build(const Settings& s);
    void deliver(EngineOutput& out)
    {
        plugins::push_output(*p_.host, d_->sink, out, d_->counters);
        out.clear();
    }
    void event(uint32_t e, int64_t v) { p_.host->stream_event(d_->sink, e, v); }

    Device* d_;
    const Plugin& p_;
    std::unique_ptr<MeasurementEngine> eng_;
    double rate_ = 0.0;
    double centre_ = 0.0; ///< true centre frequency of the samples
    uint64_t version_ = 0;
    plugins::PlanSetup plan_;
};

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
}

void Stream::run()
{
    Settings s;
    {
        std::lock_guard lock(d_->mutex);
        s = d_->settings;
        version_ = d_->settings_version;
        plan_ = d_->plan_setup;
    }
    SoapySDR::Device& sdr = *d_->sdr;
    apply_settings(*d_, s, true);
    double centre = s.tune_mhz * 1e6;
    if (plan_.serving_hz)
        centre = *plan_.serving_hz;
    else if (!plan_.carriers.empty())
        centre = (*std::min_element(plan_.carriers.begin(), plan_.carriers.end()) +
                  *std::max_element(plan_.carriers.begin(), plan_.carriers.end())) / 2.0;
    // Without a correction in the module, tune off by the oscillator error.
    const double lo_factor = d_->caps.frequency_correction ? 1.0 : 1.0 + s.ppm * 1e-6;
    sdr.setFrequency(SOAPY_SDR_RX, 0, centre / lo_factor);
    centre_ = sdr.getFrequency(SOAPY_SDR_RX, 0) * lo_factor;
    rate_ = sdr.getSampleRate(SOAPY_SDR_RX, 0);
    d_->rate_hz = rate_;
    build(s);

    SoapySDR::Stream* st = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32, {0});
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
    if (const int r = sdr.activateStream(st); r != 0)
        throw std::runtime_error(std::string("the module could not start the stream: ") +
                                 SoapySDR::errToStr(r));
    const std::size_t mtu = std::max<std::size_t>(sdr.getStreamMTU(st), 1024);
    std::vector<std::complex<float>> buf(std::max<std::size_t>(mtu, 16384));
    EngineOutput out;
    bool discontinuity = false;
    bool need_base = true;
    long long expected_ns = -1;
    double busy_s = 0.0, signal_s = 0.0;
    d_->last_data_ns = now_ns(p_);
    int errors = 0;
    for (;;) {
        {
            std::lock_guard lock(d_->mutex);
            if (d_->stop)
                break;
            if (d_->settings_version != version_) {
                version_ = d_->settings_version;
                const Settings now = d_->settings;
                const bool rebuild = now.record_rate_hz != s.record_rate_hz ||
                                     now.ms_max_power_dbm != s.ms_max_power_dbm;
                s = now;
                apply_settings(*d_, s, false);
                if (rebuild) {
                    eng_->finish(out);
                    deliver(out);
                    build(s);
                }
                discontinuity = true; // gains or port changed: the levels start over
            }
        }
        void* buffs[] = {buf.data()};
        int flags = 0;
        long long time_ns = 0;
        const int n = sdr.readStream(st, buffs, buf.size(), flags, time_ns, kReadTimeoutUs);
        if (n == SOAPY_SDR_TIMEOUT)
            continue;
        if (n == SOAPY_SDR_OVERFLOW) {
            const uint64_t count = ++d_->overflows;
            event(YR_EVENT_OVERFLOW, static_cast<int64_t>(count));
            discontinuity = true;
            need_base = true;
            continue;
        }
        if (n < 0) {
            // Unplugged or failing: the hot-plug watcher decides (RAD-35).
            if (++errors == 50)
                log(p_, d_, YR_LOG_WARNING,
                    std::string("the module keeps failing to read: ") + SoapySDR::errToStr(n));
            std::unique_lock lock(d_->mutex);
            d_->wake.wait_for(lock, std::chrono::milliseconds(20), [this] { return d_->stop; });
            continue;
        }
        errors = 0;
        if (n == 0)
            continue;
        const int64_t t_now = now_ns(p_);
        d_->last_data_ns = t_now;
        const bool timed = (flags & SOAPY_SDR_HAS_TIME) != 0;
        if (timed && expected_ns >= 0) {
            const double gap = static_cast<double>(time_ns - expected_ns) * 1e-9 * rate_;
            if (gap >= 1.0) {
                const auto lost = static_cast<uint64_t>(std::llround(gap));
                d_->samples_dropped += lost;
                event(YR_EVENT_DROPPED, static_cast<int64_t>(lost));
                discontinuity = true;
                need_base = true;
            }
        } else if (discontinuity && !timed) {
            event(YR_EVENT_DROPPED, 0); // lost samples, count unknown
        }
        expected_ns = timed ? time_ns + static_cast<long long>(std::llround(n / rate_ * 1e9)) : -1;
        const auto t0 = std::chrono::steady_clock::now();
        BlockInfo bi;
        bi.discontinuity = discontinuity;
        if (need_base) {
            bi.monotonic_ns = t_now - static_cast<int64_t>(std::llround(n / rate_ * 1e9));
            bi.utc_ns = p_.host->utc_now_ns(p_.host->host) + (bi.monotonic_ns - t_now);
            bi.utc_source = YR_TIME_SOURCE_SYSTEM;
        }
        for (int i = 0; i < n && !bi.overload; ++i)
            bi.overload = std::fabs(buf[i].real()) >= 0.999f || std::fabs(buf[i].imag()) >= 0.999f;
        eng_->process(buf.data(), static_cast<std::size_t>(n), centre_, rate_, bi, out);
        discontinuity = false;
        need_base = false;
        busy_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        signal_s += n / rate_;
        if (signal_s >= 1.0) {
            d_->load = busy_s / signal_s;
            busy_s = signal_s = 0.0;
        }
        deliver(out);
    }
    eng_->finish(out);
    deliver(out);
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
        std::set<std::string> keys;
        for (const Found& f : found)
            keys.insert(f.key);
        std::vector<std::pair<yr_stream*, uint32_t>> events;
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
                    if (d->sink != nullptr)
                        events.emplace_back(d->sink, YR_EVENT_DEVICE_BACK);
                    d->sink = nullptr;
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
                    events.emplace_back(d->sink, YR_EVENT_DEVICE_LOST);
                lock.unlock();
                d->wake.notify_all();
            }
            std::vector<std::string> now(keys.begin(), keys.end());
            if (now != p->listed) {
                p->listed = now;
                changed = true;
            }
        }
        for (const auto& [sink, e] : events)
            p->host->stream_event(sink, e, 0);
        if (changed)
            p->host->devices_changed(p->host->host);
    }
}

// --- the radio API ------------------------------------------------------------------

const Options* g_options = nullptr;

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
    yr_status st = plugins::plan_setup(*plan, carriers, d->caps.first_hz, d->caps.last_hz,
                                       kMaxCarriers, false, setup, why);
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
        d->plan = *plan;
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
    const yr_plan_ranges r = {sizeof r, (1u << YR_PLAN_SERVING) | (1u << YR_PLAN_CARRIERS), 1,
                              kMaxCarriers, &range, kSpanFraction * rate};
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
        d->plan = *plan;
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
        if (radio::check_setting_value(*def, *value, &why) != YR_OK) {
            d->error = why;
            return YR_ERR_INVALID_ARGUMENT;
        }
        if ((def->flags & YR_SETTING_UNAVAILABLE) != 0) {
            d->error = std::string(def->label) + ": " + def->unavailable_reason;
            return YR_ERR_UNSUPPORTED;
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

yr_status sp_list_presets(yr_device*, yr_preset_cb, void*) { return YR_OK; }

yr_status sp_apply_preset(yr_device* device, const char*)
{
    set_device_error(D(device), "unknown preset");
    return YR_ERR_NOT_FOUND;
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
        YR_CAP_HOTPLUG,
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
