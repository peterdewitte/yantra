// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// capture-iq (RAD-63): SoapySDR stream to a SigMF recording.
#include "capture_iq.hpp"

#include <yantra/plugins/soapy_plugin.hpp>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Errors.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Registry.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <vector>

namespace yantra::radio_host {
namespace {

using cf = std::complex<float>;

struct Segment {
    uint64_t sample_start = 0; ///< in the kept samples
    uint64_t global_index = 0; ///< in the signal (lost samples included)
};

const char* sigmf_datatype(IqFormat f)
{
    switch (f) {
    case IqFormat::cf32: return "cf32_le";
    case IqFormat::cs16: return "ci16_le";
    case IqFormat::cs8: return "ci8";
    case IqFormat::cu8: return "cu8";
    }
    return "cf32_le";
}

// The inverse of convert_samples (iq_file.cpp): full scale 1.
void encode(const cf* x, std::size_t n, IqFormat f, std::vector<unsigned char>& out)
{
    out.resize(n * iq_sample_bytes(f));
    auto q = [](float v, float scale, float lo, float hi, float offset) {
        return std::clamp(std::round(v * scale + offset), lo, hi);
    };
    for (std::size_t i = 0; i < n; ++i) {
        const float re = x[i].real(), im = x[i].imag();
        switch (f) {
        case IqFormat::cf32:
            std::memcpy(&out[i * 8], &x[i], 8);
            break;
        case IqFormat::cs16: {
            const int16_t v[2] = {static_cast<int16_t>(q(re, 32767.0f, -32767.0f, 32767.0f, 0.0f)),
                                  static_cast<int16_t>(q(im, 32767.0f, -32767.0f, 32767.0f, 0.0f))};
            std::memcpy(&out[i * 4], v, 4);
            break;
        }
        case IqFormat::cs8:
            out[i * 2] = static_cast<unsigned char>(static_cast<int8_t>(q(re, 127.0f, -127.0f, 127.0f, 0.0f)));
            out[i * 2 + 1] = static_cast<unsigned char>(static_cast<int8_t>(q(im, 127.0f, -127.0f, 127.0f, 0.0f)));
            break;
        case IqFormat::cu8:
            out[i * 2] = static_cast<unsigned char>(q(re, 127.5f, 0.0f, 255.0f, 127.5f));
            out[i * 2 + 1] = static_cast<unsigned char>(q(im, 127.5f, 0.0f, 255.0f, 127.5f));
            break;
        }
    }
}

std::string utc_text()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string meta_path_of(const std::string& data)
{
    const std::string suffix = ".sigmf-data";
    if (data.size() > suffix.size() && data.compare(data.size() - suffix.size(), suffix.size(), suffix) == 0)
        return data.substr(0, data.size() - suffix.size()) + ".sigmf-meta";
    return data + ".sigmf-meta";
}

} // namespace

bool capture_iq(const CaptureOptions& o, CaptureResult& result, std::string& error)
{
    result = CaptureResult{};
    if (o.out.empty()) {
        error = "capture-iq needs --out FILE.sigmf-data";
        return false;
    }
    const soapy::DriverFilter all;
    const std::vector<soapy::Found> found = soapy::find_devices(all);
    const auto it = std::find_if(found.begin(), found.end(), [&o](const soapy::Found& f) {
        return o.device.empty() || f.key == o.device;
    });
    if (it == found.end()) {
        error = (o.device.empty() ? std::string("no SDR found") : "no device " + o.device) + ": " +
                soapy::diagnostics(all);
        return false;
    }
    result.device = it->key;
    for (const soapy::ModuleInfo& m : soapy::modules())
        if (m.driver == it->driver)
            result.module = m.module + " " + m.version;
    if (result.module.empty())
        result.module = it->driver;

    const double rolling = o.rolling_s > 0.0 ? std::min(o.rolling_s, o.duration_s) : 0.0;
    if (rolling * o.rate_hz * sizeof(cf) > 2.0 * 1024 * 1024 * 1024) {
        error = "the rolling buffer would need more than 2 GiB; shorten --rolling or lower --rate";
        return false;
    }
    try {
        const auto makes = SoapySDR::Registry::listMakeFunctions();
        SoapySDR::Kwargs args(it->args.begin(), it->args.end());
        std::unique_ptr<SoapySDR::Device> sdr(makes.at(it->driver)(args));
        const int rx = SOAPY_SDR_RX;
        sdr->setSampleRate(rx, 0, o.rate_hz);
        if (o.bandwidth_hz)
            sdr->setBandwidth(rx, 0, *o.bandwidth_hz);
        if (!o.antenna.empty())
            sdr->setAntenna(rx, 0, o.antenna);
        if (sdr->hasGainMode(rx, 0))
            sdr->setGainMode(rx, 0, false);
        if (o.gain_db)
            sdr->setGain(rx, 0, *o.gain_db);
        const std::vector<std::string> names = sdr->listGains(rx, 0);
        for (const auto& [name, db] : o.gains) {
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                std::string list;
                for (const std::string& n : names)
                    list += (list.empty() ? "" : ", ") + n;
                error = "the module has no gain element " + name + " (it has: " + list + ")";
                return false;
            }
            sdr->setGain(rx, 0, name, db);
        }
        double lo_factor = 1.0;
        if (o.ppm != 0.0) {
            if (sdr->hasFrequencyCorrection(rx, 0))
                sdr->setFrequencyCorrection(rx, 0, o.ppm);
            else
                lo_factor = 1.0 + o.ppm * 1e-6;
        }
        sdr->setFrequency(rx, 0, o.centre_hz / lo_factor);
        result.rate_hz = sdr->getSampleRate(rx, 0);
        result.centre_hz = sdr->getFrequency(rx, 0) * lo_factor;
        const double total_gain = sdr->getGain(rx, 0);

        std::ofstream data;
        if (rolling == 0.0) {
            data.open(o.out, std::ios::binary | std::ios::trunc);
            if (!data) {
                error = "cannot write " + o.out;
                return false;
            }
        }
        const auto ring_len = static_cast<std::size_t>(std::ceil(rolling * result.rate_hz));
        std::vector<cf> ring(ring_len);
        uint64_t written = 0; // samples taken (written or put in the ring)
        uint64_t global = 0;  // signal samples, lost ones included
        std::vector<Segment> segments{{0, 0}};
        const auto wanted = static_cast<uint64_t>(std::llround(o.duration_s * result.rate_hz));

        SoapySDR::Stream* st = sdr->setupStream(rx, SOAPY_SDR_CF32, {0});
        if (const int r = sdr->activateStream(st); r != 0) {
            sdr->closeStream(st);
            error = std::string("the module could not start the stream: ") + SoapySDR::errToStr(r);
            return false;
        }
        std::vector<cf> buf(std::max<std::size_t>(sdr->getStreamMTU(st), 16384));
        std::vector<unsigned char> bytes;
        long long expected_ns = -1;
        bool gap = false;
        auto last_data = std::chrono::steady_clock::now();
        while (written < wanted && !(o.interrupt != nullptr && o.interrupt->load())) {
            void* b[] = {buf.data()};
            int flags = 0;
            long long t = 0;
            const int n = sdr->readStream(st, b, buf.size(), flags, t, 200000);
            if (n == SOAPY_SDR_OVERFLOW) {
                ++result.overflows;
                gap = true;
                continue;
            }
            if (n <= 0) {
                if (std::chrono::steady_clock::now() - last_data > std::chrono::seconds(3)) {
                    sdr->deactivateStream(st);
                    sdr->closeStream(st);
                    error = "no samples for 3 s: the device stopped (unplugged?)";
                    return false;
                }
                continue;
            }
            last_data = std::chrono::steady_clock::now();
            uint64_t lost = 0;
            if ((flags & SOAPY_SDR_HAS_TIME) != 0 && expected_ns >= 0) {
                const double g = static_cast<double>(t - expected_ns) * 1e-9 * result.rate_hz;
                if (g >= 1.0)
                    lost = static_cast<uint64_t>(std::llround(g));
            }
            expected_ns = (flags & SOAPY_SDR_HAS_TIME) != 0
                              ? t + static_cast<long long>(std::llround(n / result.rate_hz * 1e9))
                              : -1;
            if (lost > 0 || gap) {
                global += lost;
                result.lost += lost;
                segments.push_back({written, global});
                gap = false;
            }
            const auto m = static_cast<std::size_t>(std::min<uint64_t>(static_cast<uint64_t>(n), wanted - written));
            if (rolling == 0.0) {
                encode(buf.data(), m, o.format, bytes);
                data.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            } else {
                for (std::size_t i = 0; i < m; ++i)
                    ring[(written + i) % ring_len] = buf[i];
            }
            written += m;
            global += m;
        }
        sdr->deactivateStream(st);
        sdr->closeStream(st);

        // The rolling buffer: the last ring_len samples, segments shifted.
        uint64_t first = 0;
        if (rolling > 0.0) {
            first = written > ring_len ? written - ring_len : 0;
            data.open(o.out, std::ios::binary | std::ios::trunc);
            if (!data) {
                error = "cannot write " + o.out;
                return false;
            }
            std::vector<cf> chunk;
            for (uint64_t i = first; i < written;) {
                const uint64_t m = std::min<uint64_t>(65536, written - i);
                chunk.resize(m);
                for (uint64_t k = 0; k < m; ++k)
                    chunk[k] = ring[(i + k) % ring_len];
                encode(chunk.data(), m, o.format, bytes);
                data.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                i += m;
            }
        }
        data.close();
        if (!data) {
            error = "writing " + o.out + " failed";
            return false;
        }
        result.samples = written - first;

        nlohmann::json captures = nlohmann::json::array();
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const Segment& s = segments[i];
            const uint64_t end = i + 1 < segments.size() ? segments[i + 1].sample_start : written;
            if (end <= first && i + 1 < segments.size())
                continue; // entirely before the kept samples
            const uint64_t start = std::max(s.sample_start, first);
            captures.push_back({{"core:sample_start", start - first},
                                {"core:global_index", s.global_index + (start - s.sample_start)},
                                {"core:frequency", result.centre_hz}});
        }
        captures.front()["core:datetime"] = utc_text();
        result.segments = captures.size();
        const nlohmann::json meta = {
            {"global",
             {{"core:datatype", sigmf_datatype(o.format)},
              {"core:sample_rate", result.rate_hz},
              {"core:version", "1.0.0"},
              {"core:recorder", "yantra-radio-host capture-iq (RAD-63)"},
              {"core:hw", result.module + ", driver " + it->driver},
              {"core:description", "TETRA IQ capture for receiver development"},
              {"yantra:gain_db", total_gain}}},
            {"captures", captures},
            {"annotations", nlohmann::json::array()}};
        result.meta_path = meta_path_of(o.out);
        std::ofstream m(result.meta_path, std::ios::trunc);
        m << meta.dump(2) << '\n';
        if (!m) {
            error = "cannot write " + result.meta_path;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = std::string("the module failed: ") + e.what();
        return false;
    }
}

} // namespace yantra::radio_host
