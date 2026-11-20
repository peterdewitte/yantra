// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Receiver benchmarks with limits (RAD-127; docs/BENCHMARKS.md): the
// channelizer, the demodulator, the decoder and the level meter (the
// measurement engine with one pinned carrier: channelizer, decoder and
// level meter together), each timed as CPU time per second of signal
// (one core, best of 5). A CPU calibration line first: a fixed scalar
// workload (dependent floating-point and integer arithmetic, no memory
// traffic) gives this runner's speed; every time is divided by it into
// "calibrated units", so the limits in limits.csv hold on slower and
// faster runners alike. A benchmark whose units exceed its limit fails
// (exit 1, the ctest bench_limits, label slow). Sanitizer builds only
// check that every benchmark runs. --update prints limits.csv lines from
// this run (units times kHeadroom), for a deliberate change.

#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/channel.hpp"
#include "yantra/receiver/carrier_decoder.hpp"
#include "yantra/receiver/channelizer.hpp"
#include "yantra/receiver/measurement_engine.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kTimed = false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr bool kTimed = false;
#else
constexpr bool kTimed = true;
#endif
#else
constexpr bool kTimed = true;
#endif

using namespace yantra;
using receiver::cf;
namespace tetra = receiver::tetra;

namespace {

/// Limits are this many times the units measured when they were set.
constexpr double kHeadroom = 2.0;

double best_of(int runs, const std::function<void()>& f)
{
    double best = 1e30;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    }
    return best;
}

/// The calibration workload: about 1e8 dependent operations that neither
/// vectorise nor touch memory, so its time follows the core's clock and
/// pipeline, not the SIMD width or the caches. Seconds, best of 5.
double calibrate()
{
    volatile uint64_t sink = 0;
    return best_of(5, [&] {
        double x = 1.0;
        uint64_t h = 1469598103934665603ull;
        for (int i = 0; i < 20'000'000; ++i) {
            x = x * 1.0000001 + 1e-9;
            h = (h ^ static_cast<uint64_t>(i)) * 1099511628211ull;
        }
        sink = h + static_cast<uint64_t>(x);
    });
}

std::map<std::string, double> read_limits(const std::string& path, const std::string& column)
{
    std::map<std::string, double> m;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ss(line);
        std::string name, simd, scalar;
        std::getline(ss, name, ',');
        std::getline(ss, simd, ',');
        std::getline(ss, scalar, ',');
        m[name] = std::stod(column == "simd" ? simd : scalar);
    }
    return m;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string limits_path;
    bool update = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--update") == 0)
            update = true;
        else
            limits_path = argv[i];
    }
    const bool simd = receiver::simd_built();
    const receiver::Isa isa = simd ? receiver::Isa::Simd : receiver::Isa::Scalar;
    const double cal = calibrate();
    std::printf("calibration: %.1f ms for the reference workload (1 unit = that time); kernels %s\n", 1e3 * cal,
                receiver::kernels(isa).name);

    // Signals: 2 s of a 1 Msps capture with three carriers, its first
    // channel at 72 kHz, and a 250 ksps capture with one carrier.
    constexpr double kSeconds = 2.0;
    constexpr double kWide = 1e6;
    const std::vector<double> offsets = {-150000.0, 25000.0, 300000.0};
    gen::MultiCarrierTransmitter span(kWide);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        gen::CellConfig c = gen::CellConfig::defaults();
        c.sync.colour_code = static_cast<uint8_t>(i + 1);
        span.add(c, {offsets[i], -20.0 - 3.0 * static_cast<double>(i), 0.0});
    }
    std::vector<cf> wide(static_cast<std::size_t>(kSeconds * kWide));
    span.read(wide.data(), wide.size());
    gen::Awgn(1e-7, 1).add(wide.data(), wide.size());
    std::vector<std::vector<cf>> channels;
    {
        receiver::Channelizer ch(tetra::channelizer_config(kWide, isa), {offsets[0]});
        ch.process(wide.data(), wide.size(), channels);
    }
    const std::vector<cf>& narrow = channels.at(0);
    constexpr double kNarrowRate = 250000.0;
    gen::MultiCarrierTransmitter one(kNarrowRate);
    one.add(gen::CellConfig::defaults(), {25000.0, -30.0, 0.0});
    std::vector<cf> engine_in(static_cast<std::size_t>(kSeconds * kNarrowRate));
    one.read(engine_in.data(), engine_in.size());
    gen::Awgn(1e-9, 2).add(engine_in.data(), engine_in.size());

    bool ok = true;
    std::map<std::string, double> units;
    // Channelizer: three channels of the 1 Msps capture.
    units["channelizer_3ch_1msps"] = best_of(5, [&] {
        receiver::Channelizer ch(tetra::channelizer_config(kWide, isa), offsets);
        std::vector<std::vector<cf>> o;
        for (std::size_t p = 0; p < wide.size(); p += 65536)
            ch.process(wide.data() + p, std::min<std::size_t>(65536, wide.size() - p), o);
    });
    // Demodulator: one carrier at 72 kHz.
    units["demodulator"] = best_of(5, [&] {
        receiver::Pi4DqpskDemodulator d(tetra::demod_config(isa));
        std::vector<receiver::DemodSymbol> sym;
        for (std::size_t p = 0; p < narrow.size(); p += 4096)
            d.process(narrow.data() + p, std::min<std::size_t>(4096, narrow.size() - p), sym);
        ok = ok && d.status().locked;
    });
    // Decoder: demodulator, burst sync, lower MAC, osmo-tetra, upper layers.
    units["decoder"] = best_of(5, [&] {
        tetra::CarrierDecoderConfig cfg;
        cfg.isa = isa;
        tetra::CarrierDecoder dec(cfg);
        tetra::DecoderOutput out;
        bool cell = false;
        for (std::size_t p = 0; p < narrow.size(); p += 4096) {
            out.clear();
            dec.process(narrow.data() + p, std::min<std::size_t>(4096, narrow.size() - p), out);
            cell = cell || !out.cells.empty();
        }
        ok = ok && cell;
    });
    // Level meter in the engine: one pinned carrier of a 250 ksps capture
    // (channelizer + decoder + level meter + records).
    units["engine_level_meter"] = best_of(5, [&] {
        tetra::EngineConfig cfg;
        cfg.carriers = {390.0e6 + 25000.0};
        cfg.search = false;
        cfg.isa = isa;
        tetra::MeasurementEngine e(cfg);
        tetra::EngineOutput out;
        uint64_t records = 0;
        const std::size_t block = 25000;
        for (std::size_t p = 0; p + block <= engine_in.size(); p += block) {
            tetra::BlockInfo info;
            if (p == 0)
                info.monotonic_ns = 1;
            out.clear();
            e.process(engine_in.data() + p, block, 390.0e6, kNarrowRate, info, out);
            records += out.records.size();
        }
        ok = ok && records >= 1;
    });

    for (auto& [name, t] : units)
        t = t / kSeconds / cal;  // calibrated units per second of signal
    if (!ok)
        std::printf("FAIL: a benchmark did not lock or decode its carrier\n");

    if (update) {
        for (const auto& [name, u] : units)
            std::printf("%s: %.3f units -> limit %.2f\n", name.c_str(), u, u * kHeadroom);
        return ok ? 0 : 1;
    }
    const std::map<std::string, double> limits =
        limits_path.empty() ? std::map<std::string, double>{} : read_limits(limits_path, simd ? "simd" : "scalar");
    for (const auto& [name, u] : units) {
        const auto it = limits.find(name);
        const double limit = it == limits.end() ? 0.0 : it->second;
        const bool pass = !kTimed || limit <= 0.0 || u <= limit;
        std::printf("%-24s %7.3f units/s of signal (%6.1f ms/s)  limit %6.2f  %s\n", name.c_str(), u,
                    1e3 * u * cal, limit, !kTimed ? "not timed (sanitizer)" : (pass ? "ok" : "FAIL"));
        if (kTimed && limit <= 0.0) {
            std::printf("FAIL: no limit for %s in %s\n", name.c_str(), limits_path.c_str());
            ok = false;
        }
        ok = ok && pass;
    }
    return ok ? 0 : 1;
}
