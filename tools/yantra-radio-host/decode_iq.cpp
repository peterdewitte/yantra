// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decode_iq.hpp"

#include "host.hpp"
#include "iq_file.hpp"

#include <yantra/radios/record_file.hpp>
#include <yantra/receiver/measurement_engine.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>

namespace yantra::radio_host {

namespace tetra = yantra::receiver::tetra;
namespace radios = yantra::radios;

namespace {

constexpr double kBlockS = 0.1;  ///< samples per engine call

}  // namespace

bool decode_iq(const DecodeIqOptions& o, DecodeIqResult& result, std::string& error)
{
    result = {};
    IqMeta meta;
    if (o.format == "sigmf") {
        if (!read_sigmf_meta(o.iq, meta, error))
            return false;
    } else {
        const auto f = iq_format_from_string(o.format);
        if (!f || o.format.find('_') != std::string::npos || o.format == "ci8" || o.format == "ci16") {
            error = "--format must be cs8, cu8, cs16, cf32 or sigmf";
            return false;
        }
        meta.format = *f;
        meta.data_path = o.iq;
        meta.segments.push_back({});
    }
    if (o.rate_hz)
        meta.sample_rate_hz = o.rate_hz;
    if (o.centre_hz) {
        meta.centre_hz = o.centre_hz;
        for (IqSegment& s : meta.segments)
            s.frequency_hz.reset();
    }
    if (!meta.sample_rate_hz || !(*meta.sample_rate_hz > 0.0)) {
        error = "the sample rate is unknown: give --rate HZ";
        return false;
    }
    if (!meta.centre_hz) {
        error = "the centre frequency is unknown: give --center HZ";
        return false;
    }
    const double rate = *meta.sample_rate_hz;

    tetra::EngineConfig cfg;
    cfg.record_interval_s = o.record_interval_s;
    cfg.carriers = o.carriers;
    cfg.serving_hz = o.serving_hz;
    cfg.scan_records = o.scan_records;
    cfg.threads = std::max(1u, o.threads);
    cfg.simulated = meta.simulated;
    cfg.search_config.usable_fraction = o.usable_fraction;
    std::optional<double> fs = o.full_scale_dbm ? o.full_scale_dbm : meta.full_scale_dbm;
    if (o.uncalibrated)
        fs.reset();
    if (fs) {
        cfg.calibrated = true;
        cfg.level_offset_db = *fs;
    }
    result.calibrated = cfg.calibrated;
    tetra::MeasurementEngine engine(cfg);

    IqReader reader;
    if (!reader.open(meta.data_path, meta.format, error))
        return false;

    std::ofstream file;
    std::ostream* out = nullptr;
    if (o.out == "-") {
        out = &std::cout;
    } else if (!o.out.empty()) {
        file.open(o.out, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = o.out + ": cannot write";
            return false;
        }
        out = &file;
    }
    std::unique_ptr<radios::RecordFileWriter> writer;
    if (out != nullptr) {
        writer = std::make_unique<radios::RecordFileWriter>(*out);
        radios::RecordFileHeader h;
        h.producer = std::string("yantra-radio-host ") + host_version() + " decode-iq";
        h.plugin.id = "tetra-decode-iq";
        h.plugin.name = "TETRA receiver (yantra-radio-host decode-iq)";
        h.plugin.version = host_version();
        h.plugin.technology_id = "tetra";
        h.plugin.licence = "GPL-3.0-or-later";
        h.plugin.capabilities = (cfg.calibrated ? YR_CAP_SELF_CALIBRATED : 0u) |
                                (cfg.simulated ? YR_CAP_SIMULATED : 0u) | (cfg.scan_records ? YR_CAP_SCAN : 0u);
        radios::PlanInfo plan;
        if (!o.carriers.empty()) {
            plan.type = YR_PLAN_CARRIERS;
            for (double c : o.carriers)
                plan.carriers_hz.push_back(static_cast<uint64_t>(std::llround(c)));
        } else {
            plan.type = YR_PLAN_SERVING;
            plan.serving_hz = o.serving_hz ? static_cast<uint64_t>(std::llround(*o.serving_hz)) : 0;
        }
        h.plan = plan;
        h.schemas = engine.schemas();
        writer->write_header(h);
    }
    auto write = [&](const tetra::EngineOutput& eo) {
        for (const radios::Event& e : eo.events) {
            if (writer)
                writer->write_event(e);
            ++result.events;
        }
        for (const radios::Record& r : eo.records) {
            if (writer)
                writer->write_record(r);
            ++result.records;
        }
    };

    result.segments = meta.segments.size();
    const auto block = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(kBlockS * rate)));
    std::vector<std::complex<float>> x;
    tetra::EngineOutput eo;
    std::size_t seg = 0;
    bool first = true;
    for (;;) {
        // Blocks end at the next capture segment.
        std::size_t n = block;
        if (seg + 1 < meta.segments.size())
            n = static_cast<std::size_t>(
                std::min<uint64_t>(n, meta.segments[seg + 1].sample_start - reader.position()));
        bool clipped = false;
        const uint64_t at = reader.position();
        const std::size_t got = reader.read(n, x, clipped);
        if (got == 0)
            break;
        const IqSegment& s = meta.segments[seg];
        tetra::BlockInfo info;
        info.overload = clipped;
        if (first || at == s.sample_start) {
            info.discontinuity = !first;
            info.monotonic_ns =
                static_cast<int64_t>(std::llround(static_cast<double>(s.global_index + (at - s.sample_start)) / rate * 1e9));
        }
        first = false;
        result.clipped_blocks += clipped ? 1 : 0;
        const double centre = s.frequency_hz.value_or(*meta.centre_hz);
        eo.clear();
        engine.process(x.data(), got, centre, rate, info, eo);
        write(eo);
        if (seg + 1 < meta.segments.size() && reader.position() >= meta.segments[seg + 1].sample_start)
            ++seg;
    }
    eo.clear();
    engine.finish(eo);
    write(eo);
    if (writer) {
        radios::EndSummary end;
        end.records = writer->records();
        end.events = writer->events();
        writer->write_end(end);
        out->flush();
        if (!*out) {
            error = o.out + ": write failed";
            return false;
        }
    }
    const tetra::EngineStats st = engine.stats();
    result.samples = st.samples;
    result.seconds = st.signal_s;
    result.load = st.load();
    result.truncated = reader.truncated();
    result.carriers = engine.search().carriers().size();
    for (const tetra::Carrier& c : engine.search().carriers())
        result.synced += (c.state == tetra::CarrierState::Locked || c.state == tetra::CarrierState::Encrypted) ? 1 : 0;
    return true;
}

}  // namespace yantra::radio_host
