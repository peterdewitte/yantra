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

namespace {

constexpr double kHeadroom = 2.0;

double best_of(int n, const std::function<void()>& f) {
    double best = 1e300;
    for (int i = 0; i < n; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    return best;
}

/// About 1e8 dependent operations that neither vectorise nor touch memory.
/// Seconds, best of 5.
double calibrate() {
    volatile std::uint64_t sink = 0;
    return best_of(5, [&] {
        double x = 1.0;
        std::uint64_t h = 1469598103934665603ull;
        for (int i = 0; i < 20'000'000; ++i) {
            x = x * 1.0000001 + 1e-9;
            h = (h ^ static_cast<std::uint64_t>(i)) * 1099511628211ull;
        }
        sink = h + static_cast<std::uint64_t>(x);
    });
}

/// name -> limit in calibrated units, from "name,limit" lines.
std::map<std::string, double> read_limits(const std::string& path) {
    std::map<std::string, double> m;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name, limit;
        std::getline(ss, name, ',');
        std::getline(ss, limit, ',');
        m[name] = std::stod(limit);
    }
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: bench_raytrace limits.csv [--no-limits] [--update]\n");
        return 2;
    }
    bool limits_on = true;
    bool update = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-limits") == 0) limits_on = false;
        if (std::strcmp(argv[i], "--update") == 0) update = true;
    }
    const building::Building b = test::office_building();
    raytrace::ModelParams p;
    p.grid_m = limits_on ? 0.25 : 1.0;
    const double cal = calibrate();
    std::printf("CPU calibration: %.4f s per unit (hardware threads %u, SIMD built: %s)\n", cal,
                std::thread::hardware_concurrency(), raytrace::simd_built() ? "yes" : "no");
    std::printf("office floor 60 x 30 m, 120 walls, 4 antennas, grid %.2f m, %d reflections, one thread\n", p.grid_m,
                p.max_reflections);
    const auto limits = read_limits(argv[1]);
    bool ok = true;
    raytrace::CoverageMap maps[2];
    const raytrace::Isa isas[2] = {raytrace::Isa::Simd, raytrace::Isa::Scalar};
    const char* names[2] = {"raytrace_office_simd", "raytrace_office_scalar"};
    for (int k = 0; k < 2; ++k) {
        raytrace::RunControl run;
        run.isa = isas[k];
        run.threads = 1;
        const double s = best_of(3, [&] { maps[k] = raytrace::compute_map(b, "office", p, run); });
        const double units = s / cal;
        std::printf("  %-24s %8.3f s  %7.2f units", names[k], s, units);
        if (update) {
            std::printf("  -> %s,%.2f\n", names[k], units * kHeadroom);
            continue;
        }
        const auto it = limits.find(names[k]);
        if (!limits_on || it == limits.end()) {
            std::printf("%s\n", limits_on ? "  (no limit)" : "");
            if (limits_on) ok = false;
            continue;
        }
        const bool pass = units <= it->second;
        std::printf("  limit %.2f %s\n", it->second, pass ? "ok" : "EXCEEDED");
        ok = ok && pass;
    }
    if (maps[0].best_dbm != maps[1].best_dbm || maps[0].rays != maps[1].rays) {
        std::printf("SIMD and scalar maps differ\n");
        ok = false;
    }
    std::printf("%zu cells, %zu rays\n", maps[0].grid.size(), maps[0].rays);
    return ok ? 0 : 1;
}
