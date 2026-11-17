// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Interpolation benchmark (TST-41, IND-41, IND-43), see CMakeLists.txt: the
// point interpolation (IDW) of 2000 measured points spread over a 60 x 30 m
// floor, one thread: on the default 0.5 m grid (7200 cells) and on a fine
// 0.1 m grid (180 000 cells) with SIMD and scalar distances (equal bit for
// bit). After the CPU calibration line; a time beyond limits.csv fails.

#include "bench_common.h"
#include "yantra/core/survey/interpolation.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <thread>

using namespace yantra;

int main(int argc, char** argv) {
    const bench::Args args = bench::parse_args(argc, argv, "bench_interpolation");
    if (!args.ok) return 2;
    std::mt19937 rng(17);
    std::uniform_real_distribution<double> ux(0.3, 59.7);
    std::uniform_real_distribution<double> uy(0.3, 29.7);
    std::uniform_real_distribution<double> uv(-110.0, -40.0);
    std::vector<survey::IdwSample> pts;
    for (int i = 0; i < 2000; ++i) pts.push_back({{ux(rng), uy(rng)}, uv(rng)});
    const project::InterpolationSettings settings;  // power 2, radius 10 m

    const double cal = bench::calibrate();
    std::printf("CPU calibration: %.4f s per unit (hardware threads %u, SIMD built: %s)\n", cal,
                std::thread::hardware_concurrency(), survey::simd_built() ? "yes" : "no");
    std::printf("60 x 30 m floor, %zu points, power %.1f, radius %.1f m, one thread\n", pts.size(), settings.power,
                settings.radius_m);
    const auto limits = bench::read_limits(args.limits_file);
    bool ok = true;
    survey::RunControl one;
    one.threads = 1;

    const survey::Grid def{{0.0, 0.0}, 0.5, 120, 60};
    survey::IdwField field;
    const double t_def = bench::best_of(3, [&] { field = survey::idw(def, pts, settings, one); });
    ok = bench::check(args, limits, "interpolation_idw_0_5m", t_def, cal) && ok;

    const double fine_m = args.limits_on ? 0.1 : 0.5;
    const survey::Grid fine{{0.0, 0.0}, fine_m, static_cast<int>(60.0 / fine_m), static_cast<int>(30.0 / fine_m)};
    survey::IdwField fields[2];
    const survey::Isa isas[2] = {survey::Isa::Simd, survey::Isa::Scalar};
    const char* names[2] = {"interpolation_idw_fine_simd", "interpolation_idw_fine_scalar"};
    for (int k = 0; k < 2; ++k) {
        survey::RunControl run = one;
        run.isa = isas[k];
        const double s = bench::best_of(3, [&] { fields[k] = survey::idw(fine, pts, settings, run); });
        // Without SIMD built the SIMD run is the scalar path: its limit.
        ok = bench::check(args, limits, names[k], s, cal, survey::simd_built() ? nullptr : names[1]) && ok;
    }
    if (std::memcmp(fields[0].value_db.data(), fields[1].value_db.data(), fields[0].value_db.size() * sizeof(float)) != 0 ||
        fields[0].confidence != fields[1].confidence) {
        std::printf("SIMD and scalar maps differ\n");
        ok = false;
    }
    std::printf("%zu and %zu cells\n", def.size(), fine.size());
    return ok ? 0 : 1;
}
