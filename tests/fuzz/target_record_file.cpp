// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target record_file (TST-12, TST-23): a record file of the radio parts
// (RAD-65) through the record sink: read it, then feed every record and
// event into the measurement pipeline as the radio host would, with the
// pipeline clock following the record times and a cycle tick per line.
// NaN/Inf and out-of-range values, unknown schemas and out-of-order times
// are counted, never a crash; every record is counted once.

#include "fuzz.hpp"

#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/measure/record_file.h"
#include "yantra/core/platform/clock.h"

#include <memory>
#include <sstream>

namespace ym = yantra::measure;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 512 * 1024) return 0;
    std::istringstream in(yantra::fuzz::text_of(data, size));
    const auto parsed = ym::parse_record_file(in, "fuzz.records.jsonl");
    if (!parsed.ok()) {
        if (parsed.errors.empty()) fail("a refused record file has no error");
        return 0;
    }
    auto clock = std::make_shared<yantra::platform::ManualClock>();
    ym::MeasurementPipeline::Options o;
    o.clock = clock;
    o.config.max_catch_up = 50;  // bounded work per tick for huge time jumps
    ym::MeasurementPipeline pipeline(std::move(o));
    ym::ReplayOptions options;
    options.advance = [&](std::int64_t t) {
        if (t > clock->monotonic_ns()) clock->set(t);
        (void)pipeline.tick();
    };
    const std::size_t n = ym::replay(*parsed.value, pipeline, options);
    if (n != parsed.value->record_count) fail("replay did not feed every record");
    const ym::PipelineStats s = pipeline.stats();
    if (s.records != n) fail("the pipeline did not count every record");
    if (s.accepted + s.unmapped + s.implausible + s.out_of_order + s.duplicates > s.records)
        fail("pipeline counters exceed the records handed in");
    (void)pipeline.snapshot();
    return 0;
}
