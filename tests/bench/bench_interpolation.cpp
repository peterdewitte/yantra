// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Interpolation benchmark (TST-41, IND-41..43), see CMakeLists.txt: on the
// office floor of tests/raytrace (60 x 30 m, 120 walls, 4 antennas), 400
// measured points made with the ray tracer at other parameters, one
// thread: the fit (rays to the points with 2 reflections plus the solver),
// the residual field with its confidence mask on the 0.25 m grid (SIMD and
// scalar, equal bit for bit), and the source search of IND-42. After the
// CPU calibration line; a time beyond limits.csv fails.

#include "bench_common.h"
#include "office.h"
#include "yantra/core/raytrace/fit.h"
#include "yantra/core/raytrace/residuals.h"

#include <cstdio>
#include <random>
#include <thread>

using namespace yantra;

int main(int argc, char** argv) {
    const bench::Args args = bench::parse_args(argc, argv, "bench_interpolation");
    if (!args.ok) return 2;
    building::Building truth = test::office_building();
    for (auto& a : truth.floors[0].antennas) a.eirp_dbm = 13.0;
    building::Building b = test::office_building();
    for (auto& a : b.floors[0].antennas) a.eirp_dbm.reset();
    std::mt19937 rng(17);
    std::uniform_real_distribution<double> ux(0.3, 59.7);
    std::uniform_real_distribution<double> uy(0.3, 29.7);
    std::vector<building::Point2> pts;
    for (int i = 0; i < 400; ++i) pts.push_back({ux(rng), uy(rng)});
    raytrace::ModelParams p;
    p.grid_m = args.limits_on ? 0.25 : 1.0;
    auto tp = p;
    tp.path_loss_exponent = 2.6;
    const auto measured = raytrace::evaluate(truth, raytrace::trace_points(truth, "office", pts, tp), tp).best_dbm;

    const double cal = bench::calibrate();
    std::printf("CPU calibration: %.4f s per unit (hardware threads %u, SIMD built: %s)\n", cal,
                std::thread::hardware_concurrency(), raytrace::simd_built() ? "yes" : "no");
    std::printf("office floor, 400 points, grid %.2f m, %d reflections, one thread\n", p.grid_m, p.max_reflections);
    const auto limits = bench::read_limits(args.limits_file);
    bool ok = true;
    raytrace::RunControl one;
    one.threads = 1;

    raytrace::FitResult fit;
    const double t_fit = bench::best_of(3, [&] {
        fit = raytrace::fit_model(b, raytrace::trace_points(b, "office", pts, p, one), measured, p);
    });
    ok = bench::check(args, limits, "interpolation_fit", t_fit, cal) && ok;
    std::printf("  fit: %zu points, rms %.6f dB, %d iterations (%s)\n", fit.quality.points, fit.quality.rms_db,
                fit.iterations, fit.stop_reason.c_str());
    if (!(fit.quality.rms_db < 1e-3)) {
        std::printf("fit did not recover the parameters\n");
        ok = false;
    }

    std::vector<raytrace::Residual> res;
    std::normal_distribution<double> noise(0.0, 2.0);
    for (std::size_t i = 0; i < pts.size(); ++i) res.push_back({pts[i], noise(rng)});
    const raytrace::Grid grid = raytrace::floor_grid(b, b.floors[0], p.grid_m);
    raytrace::ResidualField fields[2];
    const raytrace::Isa isas[2] = {raytrace::Isa::Simd, raytrace::Isa::Scalar};
    const char* names[2] = {"interpolation_residuals_simd", "interpolation_residuals_scalar"};
    for (int k = 0; k < 2; ++k) {
        raytrace::RunControl run = one;
        run.isa = isas[k];
        const double s = bench::best_of(3, [&] {
            fields[k] = raytrace::interpolate_residuals(b, "office", grid, res, {}, run);
        });
        ok = bench::check(args, limits, names[k], s, cal) && ok;
    }
    if (fields[0].correction_db != fields[1].correction_db || fields[0].confidence != fields[1].confidence) {
        std::printf("SIMD and scalar residual fields differ\n");
        ok = false;
    }

    // IND-42: the office without antennas, 4 m search step.
    building::Building bare = b;
    bare.floors[0].antennas.clear();
    raytrace::ModelParams sp = p;
    sp.frequency_hz = 390e6;
    raytrace::SourceSearchOptions so;
    so.step_m = args.limits_on ? 4.0 : 8.0;
    raytrace::SourceEstimate est;
    const double t_src = bench::best_of(1, [&] {
        est = raytrace::estimate_sources(bare, "office", pts, measured, sp, so, one);
    });
    ok = bench::check(args, limits, "interpolation_sources", t_src, cal) && ok;
    std::printf("  sources: %zu estimated, rms %.3f dB\n", est.antennas.size(),
                est.rms_db.empty() ? 0.0 : est.rms_db.back());
    std::printf("%zu cells\n", grid.size());
    return ok ? 0 : 1;
}
