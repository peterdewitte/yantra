// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real-time check of the TETRA measurement engine (RAD-114): a 1 MS/s
// capture with four carriers (generator, RAD-70) through the whole engine
// (spectrum search, four decoders, level meters, records at 1 Hz), single
// threaded. RAD-114 asks for one carrier plus the 1 Hz cycle in real time
// on a Raspberry Pi 4 or an old laptop; this run does four carriers at
// four times the minimum rate and must stay below half of real time on
// the build machine (a factor 2 margin that a Pi-class CPU loses, so a
// regression of several times shows before a Pi does). The signal is
// generated beforehand; only process() is timed (the engine's own stats).
// Exit 1 when it is too slow or a carrier does not synchronise (sanitizer
// builds: only the latter).

#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/channel.hpp"
#include "yantra/gen/downlink.hpp"
#include "yantra/receiver/measurement_engine.hpp"

#include <cstdio>
#include <vector>

// Sanitizer builds are several times slower by design: they check the
// synchronisation but not the time.
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

int main()
{
    constexpr double kRate = 1'000'000.0;
    constexpr double kCentre = 390.0e6;
    constexpr double kSeconds = 10.0;
    constexpr double kMaxLoad = 0.5;
    gen::MultiCarrierTransmitter tx(kRate);
    const double offsets[] = {-300000.0, -100000.0, 125000.0, 337500.0};
    int k = 0;
    for (double off : offsets) {
        gen::CellConfig c = gen::CellConfig::defaults();
        c.sync.colour_code = static_cast<uint8_t>(k + 1);
        tx.add(c, {off, -30.0 - 5.0 * k, 0.003 * k});
        ++k;
    }
    gen::Awgn noise(1e-9, 3);
    const auto total = static_cast<std::size_t>(kSeconds * kRate);
    std::vector<cf> x(total);
    tx.read(x.data(), total);
    noise.add(x.data(), total);

    tetra::MeasurementEngine e;
    tetra::EngineOutput out;
    const std::size_t block = static_cast<std::size_t>(kRate / 10.0);
    uint64_t records = 0;
    for (std::size_t i = 0; i + block <= total; i += block) {
        tetra::BlockInfo info;
        if (i == 0)
            info.monotonic_ns = 1;
        out.clear();
        e.process(x.data() + i, block, kCentre, kRate, info, out);
        records += out.records.size();
    }
    std::size_t synced = 0;
    for (const tetra::Carrier& c : e.search().carriers())
        synced += (c.state == tetra::CarrierState::Locked || c.state == tetra::CarrierState::Encrypted) ? 1 : 0;
    const tetra::EngineStats st = e.stats();
    std::printf("perf_engine: %.0f s of signal at %.0f S/s, %zu carriers synchronised, %llu records, "
                "%.3f s processing, load %.3f (limit %.2f)\n",
                st.signal_s, kRate, synced, static_cast<unsigned long long>(records), st.processing_s, st.load(),
                kMaxLoad);
    if (synced != 4) {
        std::printf("FAIL: %zu of 4 carriers synchronised\n", synced);
        return 1;
    }
    if (!kTimed) {
        std::printf("sanitizer build: time not checked\n");
        return 0;
    }
    if (st.load() >= kMaxLoad) {
        std::printf("FAIL: slower than %.2f of real time\n", kMaxLoad);
        return 1;
    }
    return 0;
}
