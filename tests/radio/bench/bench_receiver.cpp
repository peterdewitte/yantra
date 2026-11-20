// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Receiver DSP micro-benchmark (RAD-100, groundwork for RAD-127).
// Generates one second of a capture
// with three TETRA carriers (generator, RAD-70), then times, for the
// scalar and the SIMD kernels: the FFT spectrum, the channelizer per
// channel and the demodulator per carrier. Prints CPU time per second of
// signal and the real-time factor. Exit code 1 only if a carrier does not
// demodulate to its bits (a broken benchmark), never because of time.

#include "yantra/gen/channel.hpp"
#include "yantra/receiver/channelizer.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"
#include "yantra/receiver/spectrum.hpp"
#include "yantra/gen/burst.hpp"
#include "yantra/gen/cell_tx.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace yantra;
using receiver::cf;


namespace {

constexpr double kSeconds = 1.0;

// Best of three runs, in seconds.
double best_of_3(const std::function<void()>& f)
{
    double best = 1e30;
    for (int i = 0; i < 3; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    }
    return best;
}

gen::CellConfig cell(uint8_t cc)
{
    auto c = gen::CellConfig::defaults();
    c.sync.colour_code = cc;
    return c;
}

}  // namespace

int main()
{
    std::printf("Receiver DSP benchmark (RAD-100, RAD-127): CPU time per second of signal, one core, best of 3.\n");
    std::printf("SIMD kernels built: %s\n\n", receiver::simd_built() ? "yes" : "no (scalar tree)");
    bool ok = true;
    const std::vector<double> offsets = {-150000.0, 25000.0, 50000.0};
    for (double fs : {1e6, 2.4e6}) {
        // The capture: three carriers and receiver noise.
        gen::MultiCarrierTransmitter span(fs);
        for (std::size_t i = 0; i < offsets.size(); ++i)
            span.add(cell(static_cast<uint8_t>(i + 1)), {.offset_hz = offsets[i], .level_db = -3.0 * static_cast<double>(i)});
        std::vector<cf> iq(static_cast<std::size_t>(kSeconds * fs));
        span.read(iq.data(), iq.size());
        gen::Awgn(1e-5, 1).add(iq.data(), iq.size());

        for (receiver::Isa isa : {receiver::Isa::Scalar, receiver::Isa::Simd}) {
            if (isa == receiver::Isa::Simd && !receiver::simd_built())
                continue;
            const char* name = receiver::kernels(isa).name;
            // Spectrum: 4096 bins, Blackman-Harris, all samples.
            const double t_spec = best_of_3([&] {
                receiver::SpectrumConfig sc;
                sc.fft_size = 4096;
                sc.averages = 16;
                sc.isa = isa;
                receiver::SpectrumAnalyzer sa(sc);
                sa.push(iq.data(), iq.size());
            });
            // Channelizer with 1 and with 3 channels: the difference per
            // added channel is the cost of one channel.
            std::vector<std::vector<cf>> out;
            const double t_ch1 = best_of_3([&] {
                receiver::Channelizer ch(receiver::tetra::channelizer_config(fs, isa), {offsets[0]});
                std::vector<std::vector<cf>> o;
                for (std::size_t pos = 0; pos < iq.size(); pos += 65536)
                    ch.process(iq.data() + pos, std::min<std::size_t>(65536, iq.size() - pos), o);
            });
            const double t_ch3 = best_of_3([&] {
                receiver::Channelizer ch(receiver::tetra::channelizer_config(fs, isa), offsets);
                out.clear();
                for (std::size_t pos = 0; pos < iq.size(); pos += 65536)
                    ch.process(iq.data() + pos, std::min<std::size_t>(65536, iq.size() - pos), out);
            });
            // Demodulator on the first channel.
            std::vector<receiver::DemodSymbol> sym;
            const double t_dem = best_of_3([&] {
                receiver::Pi4DqpskDemodulator d(receiver::tetra::demod_config(isa));
                sym.clear();
                for (std::size_t pos = 0; pos < out[0].size(); pos += 4096)
                    d.process(out[0].data() + pos, std::min<std::size_t>(4096, out[0].size() - pos), sym);
                if (!d.status().locked)
                    ok = false;
            });
            // Check: the first carrier's bits after acquisition.
            gen::CellTransmitter ref(cell(1), fs);
            gen::Bits bits;
            while (bits.size() < 2 * (sym.size() + 64)) {
                const auto b = ref.next_burst();
                bits.insert(bits.end(), b.begin(), b.end());
            }
            std::size_t errors = 0;
            std::size_t compared = 0;
            receiver::Channelizer probe(receiver::tetra::channelizer_config(fs, isa), {offsets[0]});
            for (std::size_t i = 600; i < sym.size(); ++i) {
                const double t = sym[i].sample_pos / receiver::tetra::kChannelRateHz - probe.delay_seconds();
                const long k = std::lround(t * receiver::tetra::kSymbolRateHz - 8.0);
                if (k < 1 || static_cast<std::size_t>(2 * k + 1) >= bits.size())
                    continue;
                errors += ((sym[i].dibit() >> 1) & 1) != bits[static_cast<std::size_t>(2 * k)];
                errors += (sym[i].dibit() & 1) != bits[static_cast<std::size_t>(2 * k + 1)];
                compared += 2;
            }
            if (errors != 0 || compared < 20000)
                ok = false;

            const double per_channel = std::max(0.0, (t_ch3 - t_ch1) / 2.0);
            const double per_carrier = per_channel + t_dem;
            std::printf("input %.1f Msps, %-6s: spectrum %6.1f ms/s | channelizer 1 ch %6.1f ms/s, +%5.1f ms/s per "
                        "channel | demodulator %5.1f ms/s | per carrier %5.1f ms/s = %4.1f %% of a core, %5.0fx real "
                        "time | check %zu/%zu bit errors\n",
                        fs / 1e6, name, 1e3 * t_spec / kSeconds, 1e3 * t_ch1 / kSeconds, 1e3 * per_channel / kSeconds,
                        1e3 * t_dem / kSeconds, 1e3 * per_carrier / kSeconds, 100.0 * per_carrier / kSeconds,
                        kSeconds / per_carrier, errors, compared);
        }
    }
    std::printf("\n%s\n", ok ? "all carriers demodulated to their bits" : "FAILED: a carrier did not demodulate");
    return ok ? 0 : 1;
}
