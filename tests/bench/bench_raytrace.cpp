// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ray-tracer benchmark (TST-41, IND-40), see CMakeLists.txt. A CPU
// calibration line first: a fixed scalar workload (dependent
// floating-point and integer arithmetic, no memory traffic) gives this
// runner's speed; every time is divided by it into "calibrated units", so
// the limits in limits.csv hold on slower and faster runners alike. A
// benchmark beyond its limit fails (exit 1). The SIMD and scalar maps must
// be equal bit for bit. --no-limits (sanitizer trees): a 1 m grid, times
// printed only. --update prints limits.csv lines from this run (units
// times kHeadroom), for a deliberate change.
// Copied from yantra-v2 68ea20a: tests/raytrace_bench/bench_raytrace.cpp,
// adapted; the calibration from yantra-radios tests/bench/bench_limits.cpp.

#include "bench_common.h"
#include "office.h"
#include "yantra/core/raytrace/model.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>

using namespace yantra;

int main(int argc, char** argv) {
    const bench::Args args = bench::parse_args(argc, argv, "bench_raytrace");
    if (!args.ok) return 2;
    const bool limits_on = args.limits_on;
    const building::Building b = test::office_building();
    raytrace::ModelParams p;
    p.grid_m = limits_on ? 0.25 : 1.0;
    const double cal = bench::calibrate();
    std::printf("CPU calibration: %.4f s per unit (hardware threads %u, SIMD built: %s)\n", cal,
                std::thread::hardware_concurrency(), raytrace::simd_built() ? "yes" : "no");
    std::printf("office floor 60 x 30 m, 120 walls, 4 antennas, grid %.2f m, %d reflections, one thread\n", p.grid_m,
                p.max_reflections);
    const auto limits = bench::read_limits(args.limits_file);
    bool ok = true;
    raytrace::CoverageMap maps[2];
    const raytrace::Isa isas[2] = {raytrace::Isa::Simd, raytrace::Isa::Scalar};
    const char* names[2] = {"raytrace_office_simd", "raytrace_office_scalar"};
    for (int k = 0; k < 2; ++k) {
        raytrace::RunControl run;
        run.isa = isas[k];
        run.threads = 1;
        const double s = bench::best_of(3, [&] { maps[k] = raytrace::compute_map(b, "office", p, run); });
        ok = bench::check(args, limits, names[k], s, cal) && ok;
    }
    if (maps[0].best_dbm != maps[1].best_dbm || maps[0].rays != maps[1].rays) {
        std::printf("SIMD and scalar maps differ\n");
        ok = false;
    }
    std::printf("%zu cells, %zu rays\n", maps[0].grid.size(), maps[0].rays);
    return ok ? 0 : 1;
}
