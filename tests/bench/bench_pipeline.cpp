// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Record pipeline benchmark (TST-41, RF-01), see CMakeLists.txt: records
// of a generic measurement schema (8 carriers, 50 records per carrier and
// second, levels and quality) through the measurement pipeline on the
// calling thread, a cycle each simulated second with a test clock. After
// the CPU calibration line; a time beyond limits.csv fails.

#include "bench_common.h"

#include <yantra/core/measure/measurement_pipeline.h>
#include <yantra/radio.h>

#include <cstdio>
#include <memory>
#include <thread>

namespace ym = yantra::measure;
namespace yp = yantra::plugin;
using yantra::platform::ManualClock;

namespace {

constexpr std::int64_t kSecond = 1'000'000'000;

const yr_enum_item kRoles[] = {{YR_ROLE_SERVING, "serving", "Serving"},
                               {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                               {YR_ROLE_OTHER, "other", "Other"}};

#define FIELD(var, name, type, group, flags, unit, items, count)                             \
    const yr_field_def var = {sizeof(yr_field_def), type, group, flags, name, "Label " name, \
                              unit, "Help " name, items, count, 0}

FIELD(f_freq, YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, "Hz", nullptr, 0);
FIELD(f_chan, YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_role, YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE, 0, "", kRoles, 3);
FIELD(f_level, YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_DEFAULT, "dBFS", nullptr, 0);
FIELD(f_mcc, YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_mnc, YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_la, YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_cc, YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_mer, YR_FIELD_MER_PCT, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, "%", nullptr, 0);
FIELD(f_evm, YR_FIELD_EVM_MER_DB, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, "dB", nullptr, 0);
FIELD(f_ferr, YR_FIELD_FREQ_ERROR_HZ, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, "Hz", nullptr, 0);

const yr_field_def* const kCellFields[] = {&f_freq, &f_chan, &f_role, &f_level, &f_mcc, &f_mnc,
                                           &f_la,   &f_cc,   &f_mer,  &f_evm,   &f_ferr};
const yr_schema kCell = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "x_cell", "Cell", 1, 11,
                         kCellFields, YR_GROUP_QUALITY, 0};

ym::RadioRecord cell(std::int64_t t, std::uint64_t hz, double level, std::uint32_t role) {
    ym::RadioRecord r;
    r.plugin_id = "bench.radio";
    r.plugin_version = "1.0.0";
    r.technology_id = "xtech";
    r.device_key = "dev:0";
    r.stream_id = 1;
    r.schema_index = 0;
    r.schema_kind = kCell.kind;
    r.radio_schema = &kCell;
    r.record.schema_id = kCell.id;
    r.record.schema_version = kCell.version;
    r.record.time.monotonic_ns = t;
    auto& v = r.record.values;
    v.resize(kCell.field_count);
    v[0] = hz;
    v[1] = std::uint64_t{3600};
    v[2] = yp::EnumValue{role};
    v[3] = level;
    v[4] = std::uint64_t{901};
    v[5] = std::uint64_t{9};
    v[6] = std::uint64_t{17};
    v[7] = std::uint64_t{1};
    v[8] = 2.5;
    v[9] = 31.0;
    v[10] = -12.0;
    r.present = YR_GROUP_QUALITY;
    return r;
}

struct Count final : ym::MeasurementObserver {
    std::uint64_t records = 0;
    std::uint64_t cycles = 0;
    void on_record(const ym::MappedRecord&) override { ++records; }
    void on_cycle(const ym::MeasurementCycle&) override { ++cycles; }
};

}  // namespace

int main(int argc, char** argv) {
    const bench::Args args = bench::parse_args(argc, argv, "bench_pipeline");
    if (!args.ok) return 2;
    const int seconds = args.limits_on ? 4000 : 100;
    constexpr int kCarriers = 8;
    constexpr int kPerSecond = 50;
    const double cal = bench::calibrate();
    std::printf("CPU calibration: %.4f s per unit (hardware threads %u)\n", cal, std::thread::hardware_concurrency());
    std::printf("record pipeline: %d carriers x %d records per second, %d s, one thread\n", kCarriers, kPerSecond,
                seconds);
    const auto limits = bench::read_limits(args.limits_file);
    bool ok = true;
    std::uint64_t accepted = 0;
    std::uint64_t cycles = 0;
    const double s = bench::best_of(3, [&] {
        auto clock = std::make_shared<ManualClock>(100 * kSecond);
        ym::MeasurementPipeline::Options o;
        o.clock = clock;
        ym::MeasurementPipeline p(std::move(o));
        auto out = std::make_shared<Count>();
        p.add_observer(out);
        for (int sec = 0; sec < seconds; ++sec) {
            const std::int64_t t0 = clock->monotonic_ns();
            for (int i = 0; i < kPerSecond; ++i)
                for (int c = 0; c < kCarriers; ++c) {
                    const std::int64_t t = t0 + (static_cast<std::int64_t>(i) * kCarriers + c) * (kSecond / 1000);
                    p.on_radio_record(cell(t, 390'012'500 + static_cast<std::uint64_t>(c) * 25'000,
                                           -60.0 - c - 0.01 * i, c == 0 ? YR_ROLE_SERVING : YR_ROLE_NEIGHBOUR));
                }
            clock->set(p.next_cycle_ns());
            p.tick();
        }
        accepted = p.stats().accepted;
        cycles = out->cycles;
    });
    ok = bench::check(args, limits, "pipeline_records", s, cal) && ok;
    const std::uint64_t want = static_cast<std::uint64_t>(seconds) * kCarriers * kPerSecond;
    std::printf("%llu records accepted of %llu, %llu cycles\n", static_cast<unsigned long long>(accepted),
                static_cast<unsigned long long>(want), static_cast<unsigned long long>(cycles));
    if (accepted != want || cycles < static_cast<std::uint64_t>(seconds)) {
        std::printf("records or cycles missing\n");
        ok = false;
    }
    return ok ? 0 : 1;
}
