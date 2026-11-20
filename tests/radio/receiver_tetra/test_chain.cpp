// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Channelizer + demodulator (RAD-100) on a wideband capture of several
// TETRA carriers from the generator (RAD-70): every carrier on the 25 kHz
// raster demodulates to its exact bits next to stronger neighbours, the
// symbol times map back to the capture, and the adjacent-channel
// rejection after the matched filter is measured.

#include "signal.hpp"

#include "yantra/gen/random.hpp"
#include "yantra/receiver/fft.hpp"
#include "yantra/receiver/fir.hpp"
#include "yantra/receiver/spectrum.hpp"
#include "yantra/gen/burst.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <numbers>

using namespace dsptest;
using namespace yantra;
namespace gen = yantra::gen;
namespace rx = yantra::receiver::tetra;

namespace {

gen::CellConfig cell(uint8_t colour_code, uint16_t carrier)
{
    gen::CellConfig c = gen::CellConfig::defaults();
    c.sync.colour_code = colour_code;
    c.sysinfo.main_carrier = carrier;
    c.start = {1, 16, 1, 0};
    return c;
}

struct Carrier {
    uint8_t cc;
    uint16_t carrier;
    double offset;
    double power_db;
};

}  // namespace

TEST_CASE("Carriers on the 25 kHz raster demodulate to their exact bits next to stronger neighbours")
{
    const std::vector<Carrier> carriers = {
        {1, 3592, -200000.0, 0.0}, {2, 3594, -150000.0, -30.0}, {3, 3600, 0.0, -10.0},
        {4, 3601, 25000.0, 0.0},   {5, 3608, 200000.0, -20.0},
    };
    const int bursts = 24;
    for (double fs : {500000.0, 1e6, 2.4e6}) {
        CAPTURE(fs);
        gen::MultiCarrierTransmitter span(fs);
        std::vector<double> offsets;
        for (const Carrier& c : carriers) {
            span.add(cell(c.cc, c.carrier), {.offset_hz = c.offset, .level_db = c.power_db});
            offsets.push_back(c.offset);
        }
        std::vector<cf> iq(static_cast<std::size_t>((bursts * gen::kSlotSymbols + 48) * fs / gen::kSymbolRate));
        span.read(iq.data(), iq.size());
        // Receiver noise far below the weakest carrier (-30 dB): 60 dB
        // below the strongest in the full span.
        gen::Awgn(1e-6, 3).add(iq.data(), iq.size());

        receiver::Channelizer ch(rx::channelizer_config(fs), offsets);
        std::vector<std::vector<cf>> out;
        for (std::size_t pos = 0; pos < iq.size(); pos += 10000)
            ch.process(iq.data() + pos, std::min<std::size_t>(10000, iq.size() - pos), out);

        for (std::size_t i = 0; i < carriers.size(); ++i) {
            CAPTURE(i);
            gen::CellTransmitter ref(cell(carriers[i].cc, carriers[i].carrier), fs);
            TestSignal t;
            for (int b = 0; b < bursts; ++b) {
                const gen::Bits bb = ref.next_burst();
                t.bits.insert(t.bits.end(), bb.begin(), bb.end());
            }
            receiver::Pi4DqpskDemodulator d(rx::demod_config());
            std::vector<receiver::DemodSymbol> sym;
            d.process(out[i].data(), out[i].size(), sym);
            // Channel samples back to capture time: subtract the
            // channelizer delay (in channel samples).
            for (auto& s : sym)
                s.sample_pos -= ch.delay_seconds() * rx::kChannelRateHz;
            const Comparison c = compare(sym, t, rx::kSamplesPerSymbol, 300);
            CHECK(c.misaligned == 0);
            CHECK(c.errors == 0);
            CHECK(c.compared > 2 * 5500);
            CHECK(d.status().locked);
            CHECK(std::fabs(d.status().frequency_offset_hz) < 10.0);
            std::printf("fs %.0f carrier %zu (%+.0f dB): MER %.1f dB\n", fs, i, carriers[i].power_db,
                        d.status().mer_db);
            CHECK(d.status().mer_db > 30.0);
        }
    }
}

TEST_CASE("The coarse offset estimate is not pulled by a much stronger adjacent carrier")
{
    // A weak carrier 25 kHz above one 20 or 30 dB stronger, both shifted
    // by a common tuning error: the stronger carrier's skirt reaches into
    // the weak channel's band before the matched filter. (A centroid of
    // that band was pulled to the search limit and never locked.)
    const double fs = 100000.0;
    for (double stronger_db : {20.0, 30.0}) {
        for (double tune_err : {-1500.0, 0.0, 1200.0}) {
            CAPTURE(stronger_db);
            CAPTURE(tune_err);
            const int bursts = 24;
            gen::MultiCarrierTransmitter span(fs);
            span.add(cell(1, 3600), {.offset_hz = tune_err, .level_db = stronger_db});
            span.add(cell(2, 3601), {.offset_hz = 25000.0 + tune_err, .level_db = 0.0});
            std::vector<cf> iq(static_cast<std::size_t>((bursts * gen::kSlotSymbols + 48) * fs / gen::kSymbolRate));
            span.read(iq.data(), iq.size());
            gen::Awgn(1e-7, 5).add(iq.data(), iq.size());
            receiver::Channelizer ch(rx::channelizer_config(fs), {25000.0});
            std::vector<std::vector<cf>> out;
            ch.process(iq.data(), iq.size(), out);
            gen::CellTransmitter ref(cell(2, 3601), fs);
            TestSignal t;
            for (int b = 0; b < bursts; ++b) {
                const gen::Bits bb = ref.next_burst();
                t.bits.insert(t.bits.end(), bb.begin(), bb.end());
            }
            receiver::Pi4DqpskDemodulator d(rx::demod_config());
            std::vector<receiver::DemodSymbol> sym;
            d.process(out[0].data(), out[0].size(), sym);
            for (auto& s : sym)
                s.sample_pos -= ch.delay_seconds() * rx::kChannelRateHz;
            CHECK(std::fabs(d.status().coarse_offset_hz - tune_err) < 600.0);
            CHECK(std::fabs(d.status().frequency_offset_hz - tune_err) < 10.0);
            CHECK(d.status().locked);
            const Comparison c = compare(sym, t, rx::kSamplesPerSymbol, 300);
            CHECK(c.misaligned == 0);
            CHECK(c.errors == 0);
        }
    }
}

TEST_CASE("Adjacent-channel rejection after channelizer and matched filter")
{
    const double fs = 1e6;
    const int bursts = 8;
    const std::size_t n = static_cast<std::size_t>((bursts * gen::kSlotSymbols + 16) * fs / gen::kSymbolRate);
    auto power_after_mf = [&](const std::vector<cf>& iq) {
        receiver::Channelizer ch(rx::channelizer_config(fs), {0.0});
        std::vector<std::vector<cf>> out;
        ch.process(iq.data(), iq.size(), out);
        receiver::FirFilter mf(receiver::rrc_taps(rx::kRollOff, rx::kSamplesPerSymbol, rx::demod_config().rrc_span_symbols));
        std::vector<cf> y;
        mf.process(out[0].data(), out[0].size(), y);
        double p = 0.0;
        for (std::size_t k = 400; k + 400 < y.size(); ++k)
            p += std::norm(y[k]);
        return 10.0 * std::log10(p / static_cast<double>(y.size() - 800));
    };
    auto carrier_at = [&](double offset) {
        gen::MultiCarrierTransmitter span(fs);
        span.add(cell(1, 3600), {.offset_hz = offset, .level_db = 0.0});
        std::vector<cf> iq(n);
        span.read(iq.data(), iq.size());
        return iq;
    };
    const double own = power_after_mf(carrier_at(0.0));

    // A neighbour with a strictly band-limited spectrum (Gaussian noise
    // shaped to the raised-cosine spectrum of the TETRA pulse, nothing
    // beyond +/-12.15 kHz of its centre, made by one inverse FFT): this
    // measures the receiver alone.
    for (double adj : {-50000.0, -25000.0, 25000.0, 50000.0}) {
        CAPTURE(adj);
        const std::size_t size = 1u << 18;
        receiver::Fft fft(size);
        std::vector<cf> x(size);
        gen::Rng rng(static_cast<uint64_t>(adj + 1e6));
        double p = 0.0;
        for (std::size_t k = 0; k < size; ++k) {
            const double f = (k < size / 2 ? static_cast<double>(k) : static_cast<double>(k) - size) * fs / size - adj;
            const double a = rx::kRollOff;
            const double af = std::fabs(f) / rx::kSymbolRateHz;  // in symbol rates
            double rc = 0.0;
            if (af <= (1.0 - a) / 2.0)
                rc = 1.0;
            else if (af < (1.0 + a) / 2.0)
                rc = 0.5 * (1.0 + std::cos(std::numbers::pi / a * (af - (1.0 - a) / 2.0)));
            x[k] = cf(static_cast<float>(rng.gaussian()), static_cast<float>(rng.gaussian())) *
                   static_cast<float>(std::sqrt(rc));
        }
        fft.inverse(x.data());
        for (const cf& v : x)
            p += std::norm(v);
        const float scale = static_cast<float>(1.0 / std::sqrt(p / static_cast<double>(size)));
        for (cf& v : x)
            v *= scale;
        const double acr = own - power_after_mf(x);
        std::printf("band-limited neighbour at %+.0f kHz: rejection %.1f dB\n", adj / 1000.0, acr);
        CHECK(acr >= 70.0);
    }

    // The generator's own carriers leak about -53 dB into the neighbouring
    // channel (its pulse is cut at +/-8 symbols); that leak lands inside
    // the wanted band and bounds what any receiver can reject.
    for (double adj : {-50000.0, -25000.0, 25000.0, 50000.0}) {
        CAPTURE(adj);
        const auto iq = carrier_at(adj);
        receiver::SpectrumConfig sc;
        sc.fft_size = 16384;
        sc.averages = n / 16384;
        receiver::SpectrumAnalyzer sa(sc);
        sa.set_stream(0.0, fs);
        sa.push(iq.data(), sc.averages * 16384);
        receiver::SpectrumFrame f;
        REQUIRE(sa.pop(f));
        const double leak = f.band_power_dbfs(-12150.0, 12150.0) - f.band_power_dbfs(-fs, fs);
        const double acr = own - power_after_mf(iq);
        std::printf("generator neighbour at %+.0f kHz: leak into the channel %.1f dB, rejection %.1f dB\n",
                    adj / 1000.0, leak, acr);
        CHECK(acr >= 55.0);
        CHECK(acr >= -leak);  // the receiver adds nothing on top of the leak
    }
}
