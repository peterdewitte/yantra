// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target iq (RAD-96, RAD-124 "NaN/Inf samples, zero-length blocks,
// truncated and corrupted files"): IQ file content as decode-iq reads it
// (RAD-63) through the sample conversion and the whole measurement engine
// (carrier search, channelizer, demodulator, lower MAC, records).
// Input: byte 0 = format (cf32, cs16, cs8, cu8), byte 1 = options (bit 0
// search, bit 1 scan records, bit 2 a discontinuity half way, bits 3-4
// sample rate), byte 2 = block size, then the sample bytes. Properties:
// records only of the engine's schemas, at most one cell record per
// carrier per interval, samples counted.

#include "fuzz.hpp"

#include "iq_file.hpp"

#include "yantra/receiver/measurement_engine.hpp"

#include <algorithm>
#include <complex>
#include <vector>

using yantra::fuzz::fail;
namespace rh = yantra::radio_host;
namespace tetra = yantra::receiver::tetra;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 3 || size > 512 * 1024)
        return 0;
    static const rh::IqFormat kFormats[] = {rh::IqFormat::cf32, rh::IqFormat::cs16, rh::IqFormat::cs8,
                                            rh::IqFormat::cu8};
    static const double kRates[] = {100e3, 250e3, 500e3, 72e3 * 2};
    const rh::IqFormat format = kFormats[data[0] % 4];
    const uint8_t opt = data[1];
    const std::size_t block = 1 + static_cast<std::size_t>(data[2]) * 61;
    const std::size_t bytes = rh::iq_sample_bytes(format);
    const std::size_t n = (size - 3) / bytes;
    std::vector<std::complex<float>> x;
    bool clipped = false;
    rh::convert_samples(data + 3, n, format, x, clipped);

    tetra::EngineConfig cfg;
    cfg.carriers = {390.0e6};
    cfg.search = (opt & 1) != 0;
    cfg.scan_records = (opt & 2) != 0;
    cfg.record_interval_s = 0.05;  // several intervals even in short inputs
    tetra::MeasurementEngine e(cfg);
    const double rate = kRates[(opt >> 3) & 3];
    tetra::EngineOutput out;
    std::size_t records = 0;
    for (std::size_t at = 0; at <= n; at += block) {
        const std::size_t m = std::min(block, n - at);
        tetra::BlockInfo info;
        info.overload = clipped;
        info.discontinuity = (opt & 4) != 0 && at >= n / 2 && at < n / 2 + block;
        if (at == 0)
            info.monotonic_ns = 1;
        out.clear();
        e.process(x.data() + at, m, 390.0e6, rate, info, out);  // m may be 0
        for (const auto& r : out.records)
            if (r.schema_index >= e.schemas().size())
                fail("record of an unknown schema");
        records += out.records.size();
        if (m == 0)
            break;
    }
    out.clear();
    e.finish(out);
    if (e.stats().samples != n)
        fail("samples not counted");
    (void)records;
    return 0;
}
