// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fit of the model to measured points (IND-41), the inter-floor term in
// the fit (IND-44), estimated sources (IND-42), residual interpolation and
// confidence mask (IND-41, IND-43); the ray-tracer part of TST-36: the fit
// recovers known model parameters from data made with known parameters
// within stated tolerances, SIMD and scalar agree. The simulator part of
// TST-36 is in tests/measure_host/test_measure_raytrace.cpp.
//
// Copied from yantra-v2 68ea20a: tests/raytrace/test_fit.cpp, adapted for
// IND-41..44 (data from the ray tracer at the true parameters instead of
// the old in-core generator, a seeded shadowing field made here, per-cell
// carriers, plan-frame metres).

#include "office.h"
#include "yantra/core/raytrace/fit.h"
#include "yantra/core/raytrace/residuals.h"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

using namespace yantra;
using building::Point2;
using raytrace::ModelParams;

namespace {

constexpr double kMhz = 390.0;  // the office cell's carrier

/// Points spread over the office floor (60 x 30 m), deterministic.
std::vector<Point2> office_points(int n, unsigned seed = 11) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> ux(0.3, 59.7);
    std::uniform_real_distribution<double> uy(0.3, 29.7);
    std::vector<Point2> pts;
    for (int i = 0; i < n; ++i) pts.push_back({ux(rng), uy(rng)});
    return pts;
}

/// The office with the "true" parameters: other wall losses than the
/// default table, two antennas at 13 dBm and two at 7 dBm.
building::Building true_office() {
    building::Building b = test::office_building();
    b.floors[0] = test::office_floor("g", 0);
    for (auto& c : b.wall_classes) {
        for (auto& e : c.bands) {
            if (c.id == "brick") e.attenuation_db = 10.5;
            if (c.id == "door") e.attenuation_db = 2.5;
            if (c.id == "plasterboard") e.attenuation_db = 4.5;
        }
    }
    auto& a = b.floors[0].antennas;
    a[0].eirp_dbm = 13.0;
    a[1].eirp_dbm = 7.0;
    a[2].eirp_dbm = 13.0;
    a[3].eirp_dbm = 7.0;
    return b;
}

/// The same office as the fit sees it: the default wall table and no
/// EIRP known.
building::Building fit_office() {
    building::Building b = test::office_building();
    b.floors[0] = test::office_floor("g", 0);
    for (auto& a : b.floors[0].antennas) a.eirp_dbm.reset();
    return b;
}

ModelParams start_params(int reflections) {
    ModelParams p;
    p.max_reflections = reflections;
    p.grid_m = 0.5;
    return p;
}

/// The levels the ray tracer gives at the true parameters (exponent 2.6).
std::vector<double> true_levels(const building::Building& truth, const std::vector<Point2>& pts, int reflections) {
    auto tp = start_params(reflections);
    tp.path_loss_exponent = 2.6;
    return raytrace::evaluate(truth, raytrace::trace_points(truth, "g", pts, tp), tp).best_dbm;
}

/// A seeded, spatially correlated shadowing field: N(0, sigma) values on a
/// lattice of `spacing_m`, interpolated bilinearly. Deterministic.
class Shadowing {
public:
    Shadowing(double sigma, double spacing_m, std::uint32_t seed) : spacing_(spacing_m) {
        std::mt19937 rng(seed);
        std::normal_distribution<double> n(0.0, sigma);
        for (auto& v : lattice_) v = n(rng);
    }
    double value(Point2 p) const {
        const double gx = p.x / spacing_;
        const double gy = p.y / spacing_;
        const int ix = static_cast<int>(std::floor(gx));
        const int iy = static_cast<int>(std::floor(gy));
        const double fx = gx - ix;
        const double fy = gy - iy;
        auto at = [&](int x, int y) { return lattice_[static_cast<std::size_t>(y * kSide + x)]; };
        return (1 - fx) * (1 - fy) * at(ix, iy) + fx * (1 - fy) * at(ix + 1, iy) + (1 - fx) * fy * at(ix, iy + 1) +
               fx * fy * at(ix + 1, iy + 1);
    }

private:
    static constexpr int kSide = 32;
    double spacing_;
    std::array<double, kSide * kSide> lattice_{};
};

double wall_loss(const building::Building& b, const char* cls) { return b.find_class(cls)->attenuation_at_db(kMhz); }

const raytrace::FitParameter& param(const raytrace::FitResult& r, const std::string& id) {
    for (const auto& x : r.parameters)
        if (x.id == id) return x;
    throw std::runtime_error("no parameter " + id);
}

/// A floor in metres (1 plan unit = 1 m).
building::Floor metre_floor(const std::string& id, int level, double w, double h) {
    building::Floor f;
    f.id = id;
    f.level = level;
    f.height_m = 3.0;
    f.scale = test::factor_scale(1.0);
    f.plan_width = w;
    f.plan_height = h;
    return f;
}

building::Antenna antenna(const std::string& id, Point2 pos, double eirp, const std::string& cell) {
    building::Antenna a;
    a.id = id;
    a.position = pos;
    a.height_m = 2.7;
    a.eirp_dbm = eirp;
    a.cell = cell;
    return a;
}

}  // namespace

TEST_CASE("fit recovers the true parameters exactly without reflections (TST-36)") {
    const auto truth = true_office();
    const auto b = fit_office();
    const auto pts = office_points(400);
    const auto measured = true_levels(truth, pts, 0);
    const auto p = start_params(0);
    const auto rays = raytrace::trace_points(b, "g", pts, p);
    const auto r = raytrace::fit_model(b, rays, measured, p);
    INFO(r.stop_reason);
    CHECK(r.stop_reason.rfind("converged", 0) == 0);
    CHECK(r.quality.points == 400);
    CHECK(r.quality.start_rms_db > 3.0);
    // Stated tolerances (noise-free data from the same model): exponent
    // 1e-6, EIRP and wall losses 1e-4 dB, rms 1e-6 dB.
    CHECK(r.quality.rms_db < 1e-6);
    CHECK(std::fabs(r.params.path_loss_exponent - 2.6) < 1e-6);
    CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das1") - 13.0) < 1e-4);
    CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das2") - 7.0) < 1e-4);
    CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das3") - 13.0) < 1e-4);
    CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das4") - 7.0) < 1e-4);
    CHECK(std::fabs(r.params.wall_class_loss_db.at("brick") - wall_loss(truth, "brick")) < 1e-4);
    CHECK(std::fabs(r.params.wall_class_loss_db.at("door") - wall_loss(truth, "door")) < 1e-4);
    CHECK(std::fabs(r.params.wall_class_loss_db.at("plasterboard") - wall_loss(truth, "plasterboard")) < 1e-4);
    // No point lies behind the outer walls: those classes are not fitted.
    bool glass_listed = false;
    for (const auto& n : r.not_fitted) glass_listed = glass_listed || n.find("'glass'") != std::string::npos;
    CHECK(glass_listed);
    CHECK(r.params.wall_class_loss_db.count("glass") == 0);
    // One floor: no inter-floor term to fit.
    for (const auto& x : r.parameters) CHECK(x.kind != raytrace::FitParameter::Kind::FloorLoss);
    // The fitted model reproduces the data through evaluate() as well.
    const auto lv = raytrace::evaluate(b, rays, r.params);
    for (std::size_t i = 0; i < pts.size(); ++i) CHECK(std::fabs(lv.best_dbm[i] - measured[i]) < 1e-5);
    // Deterministic: the same inputs give the same result.
    const auto again = raytrace::fit_model(b, rays, measured, p);
    CHECK(again.params == r.params);
    CHECK(again.iterations == r.iterations);
}

TEST_CASE("fit with reflections and shadowing noise stays within the stated tolerances (TST-36)") {
    // Data: the ray tracer with 2 reflections at the true parameters plus
    // a seeded correlated shadowing field (5 m lattice), eight seeds; the
    // fit traces with 2 reflections too. Correlated shadowing trades the
    // single EIRPs against the exponent, so the EIRP tolerance is wide;
    // the stated measure of recovery is the fitted model against the
    // noise-free truth. sigma is the field's own rms over the points.
    const auto truth = true_office();
    const auto b = fit_office();
    const auto pts = office_points(600, 5);
    const auto p = start_params(2);
    const auto clean = true_levels(truth, pts, 2);
    const auto rays = raytrace::trace_points(b, "g", pts, p);
    for (std::uint32_t seed = 1; seed <= 8; ++seed) {
        CAPTURE(seed);
        const Shadowing shadow(4.0, 5.0, seed);
        std::vector<double> measured;
        double ss = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const double s = shadow.value(pts[i]);
            ss += s * s;
            measured.push_back(clean[i] + s);
        }
        const double sigma = std::sqrt(ss / static_cast<double>(pts.size()));
        CAPTURE(sigma);
        const auto r = raytrace::fit_model(b, rays, measured, p);
        INFO(r.stop_reason);
        CHECK(r.stop_reason.rfind("converged", 0) == 0);
        // Stated tolerances: rms 0.5 .. 1.1 sigma; fitted model against
        // the truth rms below 2/3 sigma; exponent +/- 0.35; wall classes
        // +/- 2 dB; single EIRPs +/- 6 dB.
        CHECK(r.quality.rms_db > 0.5 * sigma);
        CHECK(r.quality.rms_db < 1.1 * sigma);
        double se = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i) se += (r.model_dbm[i] - clean[i]) * (r.model_dbm[i] - clean[i]);
        CHECK(std::sqrt(se / static_cast<double>(pts.size())) < 2.0 * sigma / 3.0);
        CHECK(std::fabs(r.params.path_loss_exponent - 2.6) < 0.35);
        for (const char* c : {"brick", "plasterboard", "door"})
            CHECK(std::fabs(r.params.wall_class_loss_db.at(c) - wall_loss(truth, c)) < 2.0);
        CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das1") - 13.0) < 6.0);
        CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das2") - 7.0) < 6.0);
        CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das3") - 13.0) < 6.0);
        CHECK(std::fabs(r.params.antenna_eirp_dbm.at("g_das4") - 7.0) < 6.0);
    }
}

TEST_CASE("fit: bounds hold, fixed parameters stay, options are checked") {
    const auto truth = true_office();
    const auto b = fit_office();
    const auto pts = office_points(200);
    const auto measured = true_levels(truth, pts, 0);
    auto p = start_params(0);
    const auto rays = raytrace::trace_points(b, "g", pts, p);

    raytrace::FitOptions o;
    o.exponent = {1.5, 2.2};  // the truth 2.6 is outside
    o.wall_class_bounds["brick"] = {0.0, 5.0};
    auto r = raytrace::fit_model(b, rays, measured, p, o);
    CHECK(param(r, "path_loss_exponent").value == doctest::Approx(2.2));
    CHECK(param(r, "path_loss_exponent").at_bound);
    CHECK(param(r, "brick").value <= 5.0);
    for (const auto& x : r.parameters) {
        CHECK(x.value >= x.bounds.lo);
        CHECK(x.value <= x.bounds.hi);
    }
    CHECK(r.quality.rms_db > 0.1);

    o = {};
    o.fit_exponent = false;
    o.fit_wall_classes = false;
    r = raytrace::fit_model(b, rays, measured, p, o);
    CHECK(r.params.path_loss_exponent == 2.0);
    CHECK(r.params.wall_class_loss_db.empty());
    CHECK(r.parameters.size() == 4);  // the four unknown EIRPs

    // Known EIRPs are not fitted unless asked for.
    o = {};
    auto known = b;
    for (auto& a : known.floors[0].antennas) a.eirp_dbm = 10.0;
    r = raytrace::fit_model(known, raytrace::trace_points(known, "g", pts, p), measured, p, o);
    CHECK(r.params.antenna_eirp_dbm.empty());
    o.eirp = raytrace::EirpFit::All;
    r = raytrace::fit_model(known, raytrace::trace_points(known, "g", pts, p), measured, p, o);
    CHECK(r.params.antenna_eirp_dbm.size() == 4);
    o.eirp = raytrace::EirpFit::None;
    r = raytrace::fit_model(b, rays, measured, p, o);
    CHECK(r.params.antenna_eirp_dbm.empty());

    // A group target and a missing group.
    o = {};
    o.group_key = rays.groups.at(0).key;
    CHECK(o.group_key == std::string("cell:") + test::kOfficeCell);
    CHECK(raytrace::fit_model(b, rays, measured, p, o).quality.rms_db < 1e-6);
    o.group_key = "nope";
    CHECK_THROWS_AS(raytrace::fit_model(b, rays, measured, p, o), std::invalid_argument);
    o = {};
    o.exponent = {0.5, 3.0};
    CHECK_THROWS_AS(raytrace::fit_model(b, rays, measured, p, o), std::invalid_argument);
    o = {};
    o.max_iterations = 0;
    CHECK_THROWS_AS(raytrace::fit_model(b, rays, measured, p, o), std::invalid_argument);
    CHECK_THROWS_AS(raytrace::fit_model(b, rays, std::vector<double>(3, -70.0), p), std::invalid_argument);

    // Points without a value are left out; none at all fits nothing.
    auto some = measured;
    for (std::size_t i = 0; i < some.size(); i += 2) some[i] = std::nan("");
    r = raytrace::fit_model(b, rays, some, p);
    CHECK(r.quality.points == 100);
    CHECK_FALSE(r.used[0]);
    CHECK(r.used[1]);
    std::vector<double> none(pts.size(), std::nan(""));
    r = raytrace::fit_model(b, rays, none, p);
    CHECK(r.quality.points == 0);
    CHECK(r.stop_reason == "no measured point with a modelled level");

    // Cancelled before the first step.
    std::atomic<bool> cancel{true};
    r = raytrace::fit_model(b, rays, measured, p, {}, &cancel);
    CHECK(r.cancelled);
}

TEST_CASE("fit of the inter-floor term S: the dB per floor is recovered (IND-44, TST-36)") {
    // Two floors in metres, one cell: an antenna on the ground floor and
    // one on the floor above; the points on the ground floor see both.
    // Truth: 22 dB per floor; the fit starts at the building's 15 dB.
    building::Building b;
    b.cells.push_back(test::tetra_cell("c", 400'000'000));
    b.floors.push_back(metre_floor("f", 0, 30.0, 20.0));
    b.floors.push_back(metre_floor("u", 1, 30.0, 20.0));
    b.floors[0].antennas.push_back(antenna("down", {4.0, 5.0}, 10.0, "c"));
    b.floors[1].antennas.push_back(antenna("up", {26.0, 15.0}, 20.0, "c"));
    std::vector<Point2> pts;
    for (double y = 1.0; y < 20.0; y += 2.0)
        for (double x = 1.0; x < 30.0; x += 2.0) pts.push_back({x, y});
    ModelParams p = start_params(0);
    p.other_floors = true;
    auto tp = p;
    tp.floor_loss_db = 22.0;
    const auto rays = raytrace::trace_points(b, "f", pts, p);
    const auto measured = raytrace::evaluate(b, rays, tp).best_dbm;
    raytrace::FitOptions o;
    o.fit_exponent = false;
    o.eirp = raytrace::EirpFit::None;
    const auto r = raytrace::fit_model(b, rays, measured, p, o);
    INFO(r.stop_reason);
    REQUIRE(r.params.floor_loss_db.has_value());
    CHECK(param(r, "floor_loss_db").kind == raytrace::FitParameter::Kind::FloorLoss);
    CHECK(param(r, "floor_loss_db").start == 15.0);
    CHECK(std::fabs(*r.params.floor_loss_db - 22.0) < 1e-4);
    CHECK(r.quality.rms_db < 1e-6);
    // With the floor's own slab value the start is that value; the fitted
    // value replaces it in the model.
    auto b2 = b;
    b2.floors[0].slab_attenuation_db = 30.0;
    const auto r2 = raytrace::fit_model(b2, raytrace::trace_points(b2, "f", pts, p), measured, p, o);
    CHECK(std::fabs(*r2.params.floor_loss_db - 22.0) < 1e-4);
    // Fixed: not fitted, the model keeps the building's value.
    o.fit_floor_loss = false;
    const auto r3 = raytrace::fit_model(b, rays, measured, p, o);
    CHECK_FALSE(r3.params.floor_loss_db.has_value());
    CHECK(r3.quality.rms_db > 1.0);
    // Without other floors no slab is seen: listed as not fitted.
    o = {};
    o.fit_exponent = false;
    o.eirp = raytrace::EirpFit::None;
    p.other_floors = false;
    const auto r4 = raytrace::fit_model(b, raytrace::trace_points(b, "f", pts, p), measured, p, o);
    CHECK_FALSE(r4.params.floor_loss_db.has_value());
    bool listed = false;
    for (const auto& n : r4.not_fitted) listed = listed || n.find("floor") != std::string::npos;
    CHECK(listed);
    o.floor_loss_db = {10.0, 5.0};
    CHECK_THROWS_AS(raytrace::fit_model(b, rays, measured, p, o), std::invalid_argument);
}

TEST_CASE("estimated sources: positions and EIRP from the measurements alone (IND-42)") {
    // A 30 x 20 m floor with a few walls and two sources of different
    // power; the search sees only the measured levels.
    building::Building truth;
    truth.cells.push_back(test::tetra_cell("c", 392'500'000));
    building::Floor f = metre_floor("s", 0, 30.0, 20.0);
    f.walls.push_back({"w1", "brick", {{10, 0}, {10, 12}}, std::nullopt});
    f.walls.push_back({"w2", "plasterboard", {{20, 8}, {20, 20}}, std::nullopt});
    f.antennas.push_back(antenna("t1", {5.3, 6.1}, 12.0, "c"));
    f.antennas.push_back(antenna("t2", {24.6, 13.8}, 5.0, "c"));
    truth.floors.push_back(f);
    std::vector<Point2> pts;
    for (double y = 1.0; y < 20.0; y += 1.5)
        for (double x = 1.0; x < 30.0; x += 1.5) pts.push_back({x, y});
    auto p = start_params(0);
    p.frequency_hz = 392.5e6;
    const auto measured = raytrace::evaluate(truth, raytrace::trace_points(truth, "s", pts, p), p).best_dbm;

    auto bare = truth;
    bare.floors[0].antennas.clear();
    bare.cells.clear();
    std::vector<double> prog;
    raytrace::RunControl run;
    run.progress = [&](double x) { prog.push_back(x); };
    raytrace::SourceSearchOptions o;
    o.step_m = 2.0;
    const auto est = raytrace::estimate_sources(bare, "s", pts, measured, p, o, run);
    REQUIRE(est.antennas.size() >= 2);
    REQUIRE(est.rms_db.size() == est.antennas.size());
    auto near = [&](Point2 want) {
        double d = 1e9;
        for (const auto& e : est.antennas)
            d = std::min(d, std::hypot(e.position.x - want.x, e.position.y - want.y));
        return d;
    };
    // Stated tolerances (noise-free, direct rays): positions within 0.2 m,
    // rms below 0.5 dB.
    CHECK(near({5.3, 6.1}) < 0.2);
    CHECK(near({24.6, 13.8}) < 0.2);
    CHECK(est.antennas.size() == 2);  // a third source would not lower the rms by 1 dB
    CHECK(est.rms_db.back() < 0.5);
    CHECK(est.antennas[0].id == "estimated-1");
    CHECK(est.antennas[0].model == "estimated");
    CHECK(est.antennas[0].eirp_dbm.has_value());
    REQUIRE(est.cell.has_value());
    CHECK(est.cell->id == raytrace::kEstimatedCellId);
    CHECK(est.cell->carrier_hz == 392'500'000u);
    for (const auto& a : est.antennas) CHECK(a.cell == raytrace::kEstimatedCellId);
    REQUIRE_FALSE(prog.empty());
    CHECK(prog.back() == 1.0);
    for (std::size_t i = 1; i < prog.size(); ++i) CHECK(prog[i] >= prog[i - 1]);

    // Put on the floor, they model the measured levels again.
    auto placed = bare;
    raytrace::add_estimated_sources(placed, "s", est);
    REQUIRE(placed.floors[0].antennas.size() == est.antennas.size());
    REQUIRE(placed.find_cell(raytrace::kEstimatedCellId) != nullptr);
    auto pp = start_params(0);  // each cell's carrier: the estimated cell's
    const auto back = raytrace::evaluate(placed, raytrace::trace_points(placed, "s", pts, pp), pp).best_dbm;
    double se = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) se += (back[i] - measured[i]) * (back[i] - measured[i]);
    CHECK(std::sqrt(se / static_cast<double>(pts.size())) < 0.5);
    CHECK_THROWS_AS(raytrace::add_estimated_sources(placed, "s", est), std::invalid_argument);
    CHECK_THROWS_AS(raytrace::add_estimated_sources(bare, "nope", est), std::invalid_argument);

    // Deterministic, any thread count and Isa.
    raytrace::RunControl one;
    one.threads = 2;
    one.isa = raytrace::Isa::Scalar;
    const auto again = raytrace::estimate_sources(bare, "s", pts, measured, p, o, one);
    CHECK(again.antennas == est.antennas);
    CHECK(again.rms_db == est.rms_db);

    // Several cells: each its own source, no cell.
    o.one_cell = false;
    const auto sep = raytrace::estimate_sources(bare, "s", pts, measured, p, o);
    CHECK_FALSE(sep.cell.has_value());
    for (const auto& a : sep.antennas) CHECK(a.cell.empty());

    o = {};
    o.max_sources = 0;
    CHECK_THROWS_AS(raytrace::estimate_sources(bare, "s", pts, measured, p, o), std::invalid_argument);
    auto nofreq = p;
    nofreq.frequency_hz.reset();
    CHECK_THROWS_AS(raytrace::estimate_sources(bare, "s", pts, measured, nofreq), std::invalid_argument);
    std::atomic<bool> cancel{true};
    run.cancel = &cancel;
    CHECK(raytrace::estimate_sources(bare, "s", pts, measured, p, {}, run).cancelled);
}

TEST_CASE("residual interpolation: hand-computed values, limited by walls, fading mask (IND-41, IND-43)") {
    building::Building b;
    building::Floor f = metre_floor("r", 0, 20.0, 10.0);
    f.walls.push_back({"w", "concrete", {{10, 0}, {10, 10}}, std::nullopt});
    b.floors.push_back(f);
    const std::vector<raytrace::Residual> res{{{2.0, 5.0}, 4.0}, {{6.0, 5.0}, -2.0}, {{6.0, 5.0}, -1.0}};
    raytrace::ResidualOptions o;
    // At the measured points: the residual; coincident points: their mean.
    CHECK(raytrace::residual_at(b, "r", {2.0, 5.0}, res, o).correction_db == 4.0);
    CHECK(raytrace::residual_at(b, "r", {6.0, 5.0}, res, o).correction_db == -1.5);
    CHECK(raytrace::residual_at(b, "r", {2.0, 5.0}, res, o).confidence == 1.0);
    // Between (2 m from each): weights 1/d^2 = 1/4 each, pulled towards 0
    // by w0 = R^-2 = 0.01: (1 - 0.5 - 0.25) / (0.01 + 0.75) = 0.25 / 0.76.
    const auto mid = raytrace::residual_at(b, "r", {4.0, 5.0}, res, o);
    CHECK(mid.correction_db == doctest::Approx(0.25 / 0.76).epsilon(1e-12));
    CHECK(mid.confidence == 1.0);  // within 2 m of a point
    // Off the line, (4, 8): d^2 = 13 to both: (4/13 - 3/13) / (0.01 + 3/13).
    const auto off = raytrace::residual_at(b, "r", {4.0, 8.0}, res, o);
    CHECK(off.correction_db == doctest::Approx((1.0 / 13.0) / (0.01 + 3.0 / 13.0)).epsilon(1e-12));
    // Mask: nearest point sqrt(13) m: (10 - sqrt 13) / 8.
    CHECK(off.confidence == doctest::Approx((10.0 - std::sqrt(13.0)) / 8.0).epsilon(1e-12));
    // Behind the wall: no correction, no confidence.
    const auto behind = raytrace::residual_at(b, "r", {11.0, 5.0}, res, o);
    CHECK(behind.correction_db == 0.0);
    CHECK(behind.confidence == 0.0);
    o.walls_limit = false;
    CHECK(raytrace::residual_at(b, "r", {11.0, 5.0}, res, o).correction_db != 0.0);
    // Far from every point (13 m): no confidence even without walls.
    CHECK(raytrace::residual_at(b, "r", {19.0, 5.0}, res, o).confidence == 0.0);
    CHECK(raytrace::residual_at(b, "r", {19.0, 5.0}, res, o).correction_db == 0.0);
    o = {};
    // The mask fades linearly from 2 m to 10 m.
    CHECK(raytrace::residual_at(b, "r", {2.0, 9.0}, res, o).confidence == doctest::Approx(0.75));

    const auto grid = raytrace::floor_grid(b, b.floors[0], 0.25);
    const auto field = raytrace::interpolate_residuals(b, "r", grid, res, o);
    REQUIRE(field.correction_db.size() == grid.size());
    for (int row = 0; row < grid.rows; ++row)
        for (int col = 0; col < grid.cols; ++col) {
            const auto c = grid.centre(col, row);
            const auto i = grid.index(col, row);
            const auto at = raytrace::residual_at(b, "r", c, res, o);
            CHECK(field.correction_db[i] == at.correction_db);
            CHECK(field.confidence[i] == static_cast<float>(at.confidence));
            if (c.x > 10.0) CHECK(field.confidence[i] == 0.0f);
            // Confidence never rises with the distance to the nearest point.
            if (std::hypot(c.x - 2.0, c.y - 5.0) > 10.0 && std::hypot(c.x - 6.0, c.y - 5.0) > 10.0)
                CHECK(field.confidence[i] == 0.0f);
        }
    // SIMD, scalar and threads agree exactly.
    raytrace::RunControl scalar;
    scalar.isa = raytrace::Isa::Scalar;
    scalar.threads = 1;
    const auto f2 = raytrace::interpolate_residuals(b, "r", grid, res, o, scalar);
    CHECK(f2.correction_db == field.correction_db);
    CHECK(f2.confidence == field.confidence);

    o.power = 9.0;
    CHECK_THROWS_AS(raytrace::interpolate_residuals(b, "r", grid, res, o), std::invalid_argument);
    o = {};
    o.full_confidence_m = 20.0;
    CHECK_THROWS_AS(raytrace::residual_at(b, "r", {1, 1}, res, o), std::invalid_argument);
}

TEST_CASE("fit and interpolation give the same results with and without SIMD (TST-36)") {
    const auto truth = true_office();
    const auto b = fit_office();
    const auto pts = office_points(150, 3);
    const Shadowing shadow(2.0, 5.0, 7);
    auto measured = true_levels(truth, pts, 2);
    for (std::size_t i = 0; i < pts.size(); ++i) measured[i] += shadow.value(pts[i]);
    const auto p = start_params(2);
    raytrace::RunControl simd;
    raytrace::RunControl scalar;
    scalar.isa = raytrace::Isa::Scalar;
    scalar.threads = 1;
    const auto r1 = raytrace::fit_model(b, raytrace::trace_points(b, "g", pts, p, simd), measured, p);
    const auto r2 = raytrace::fit_model(b, raytrace::trace_points(b, "g", pts, p, scalar), measured, p);
    CHECK(r1.params == r2.params);
    CHECK(r1.model_dbm == r2.model_dbm);
    std::vector<raytrace::Residual> res;
    for (std::size_t i = 0; i < pts.size(); ++i) res.push_back({pts[i], measured[i] - r1.model_dbm[i]});
    const auto grid = raytrace::floor_grid(b, b.floors[0], 1.0);
    const auto a = raytrace::interpolate_residuals(b, "g", grid, res, {}, simd);
    const auto c = raytrace::interpolate_residuals(b, "g", grid, res, {}, scalar);
    CHECK(a.correction_db == c.correction_db);
    CHECK(a.confidence == c.confidence);
}
