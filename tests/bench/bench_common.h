// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared parts of the benchmarks (TST-41): the CPU calibration line, the
// limits file and the check of one timing against its limit. See
// CMakeLists.txt.
// Copied from tests/bench/bench_raytrace.cpp (this repository), which took
// the calibration from yantra-radios tests/bench/bench_limits.cpp.
#pragma once

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

namespace bench {

inline constexpr double kHeadroom = 2.0;

/// Seconds of the fastest of `n` runs of `f`.
inline double best_of(int n, const std::function<void()>& f) {
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
inline double calibrate() {
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
inline std::map<std::string, double> read_limits(const std::string& path) {
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

/// The command line: limits.csv [--no-limits] [--update].
struct Args {
    std::string limits_file;
    bool limits_on = true;
    bool update = false;
    bool ok = false;
};
inline Args parse_args(int argc, char** argv, const char* name) {
    Args a;
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s limits.csv [--no-limits] [--update]\n", name);
        return a;
    }
    a.limits_file = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-limits") == 0) a.limits_on = false;
        if (std::strcmp(argv[i], "--update") == 0) a.update = true;
    }
    a.ok = true;
    return a;
}

/// Prints one timing and checks it against its limit (the limit of
/// `limit_name` when given); false when beyond it or without a limit
/// (limits on).
inline bool check(const Args& a, const std::map<std::string, double>& limits, const char* name, double seconds,
                  double cal, const char* limit_name = nullptr) {
    const double units = seconds / cal;
    std::printf("  %-30s %8.3f s  %7.2f units", name, seconds, units);
    if (a.update) {
        std::printf("  -> %s,%.2f\n", name, units * kHeadroom);
        return true;
    }
    const auto it = limits.find(limit_name != nullptr ? limit_name : name);
    if (!a.limits_on || it == limits.end()) {
        std::printf("%s\n", a.limits_on ? "  (no limit)" : "");
        return !a.limits_on;
    }
    const bool pass = units <= it->second;
    std::printf("  limit %.2f %s\n", it->second, pass ? "ok" : "EXCEEDED");
    return pass;
}

}  // namespace bench
