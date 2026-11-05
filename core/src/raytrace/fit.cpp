// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fit of the ray-tracing model to measured points (IND-41) and the source
// search (IND-42). docs/PROPAGATION.md "Fit to the measured points".
//
// Every ray's level in dB is linear in the fitted parameters theta:
//   d_r = c_r + sum_j A_rj theta_j
// (EIRP: +1 for the ray's antenna; exponent: -10 log10(len / d0) beyond
// d0, 0 within; wall class: -crossings; dB per floor: -slabs). A point's
// level is the power sum over the rays of its group,
// L = 10 log10(sum 10^(d_r/10)), so dL/dtheta_j = sum_r w_r A_rj with w_r
// the ray's share of the power. The solver is Levenberg-Marquardt with
// Marquardt's diagonal scaling, bounds kept by clamping and by freezing a
// parameter that sits on a bound while the gradient pushes it outwards.
//
// Copied from yantra-v2 68ea20a: core/src/raytrace/fit.cpp, adapted
// (per-group carriers and class losses, free space below d0, per-class
// reflection losses, the fitted dB per floor, plan-frame metres, the
// estimated cell).

#include "yantra/core/raytrace/fit.h"

#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace yantra::raytrace {

namespace {

std::string fmt(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

void check_bounds(std::vector<std::string>& out, const std::string& name, const Bounds& b, double lo, double hi) {
    if (!std::isfinite(b.lo) || !std::isfinite(b.hi) || b.lo > b.hi || b.lo < lo || b.hi > hi)
        out.push_back(name + ": [" + fmt(b.lo) + ", " + fmt(b.hi) + "] is not inside " + fmt(lo) + " .. " + fmt(hi));
}

/// One ray in the linear form of the parameters.
struct LinRay {
    int group = 0;
    double c = 0.0;                         ///< the part that does not depend on theta, dB
    std::vector<std::pair<int, double>> a;  ///< (parameter, coefficient)
};

struct Problem {
    std::vector<std::vector<LinRay>> rays;  ///< per used point
    std::vector<double> measured;           ///< per used point
    int target = -1;                        ///< group, -1: the strongest
    std::size_t groups = 0;
};

/// Level of one point and its gradient (size of theta). kNoLevel when no
/// ray of the target reaches it.
double point_level(const Problem& pr, std::size_t i, const std::vector<double>& theta, std::vector<double>* grad,
                   std::vector<double>& group_db, std::vector<double>& group_max) {
    const auto& rays = pr.rays[i];
    group_max.assign(pr.groups, -std::numeric_limits<double>::infinity());
    std::vector<double> d(rays.size());
    for (std::size_t r = 0; r < rays.size(); ++r) {
        double v = rays[r].c;
        for (const auto& [j, coef] : rays[r].a) v += coef * theta[static_cast<std::size_t>(j)];
        d[r] = v;
        auto& m = group_max[static_cast<std::size_t>(rays[r].group)];
        m = std::max(m, v);
    }
    // Power sum per group, shifted by the group's largest ray (no underflow).
    group_db.assign(pr.groups, kNoLevel);
    std::vector<double> sum(pr.groups, 0.0);
    for (std::size_t r = 0; r < rays.size(); ++r) {
        const auto g = static_cast<std::size_t>(rays[r].group);
        sum[g] += std::pow(10.0, (d[r] - group_max[g]) / 10.0);
    }
    for (std::size_t g = 0; g < pr.groups; ++g)
        if (sum[g] > 0.0) group_db[g] = group_max[g] + 10.0 * std::log10(sum[g]);
    int g = pr.target;
    if (g < 0) {
        double best = kNoLevel;
        for (std::size_t k = 0; k < pr.groups; ++k)
            if (group_db[k] > best) {
                best = group_db[k];
                g = static_cast<int>(k);
            }
    }
    if (g < 0 || group_db[static_cast<std::size_t>(g)] == kNoLevel) return kNoLevel;
    const auto gu = static_cast<std::size_t>(g);
    if (grad != nullptr) {
        std::fill(grad->begin(), grad->end(), 0.0);
        for (std::size_t r = 0; r < rays.size(); ++r) {
            if (rays[r].group != g) continue;
            const double w = std::pow(10.0, (d[r] - group_max[gu]) / 10.0) / sum[gu];
            for (const auto& [j, coef] : rays[r].a) (*grad)[static_cast<std::size_t>(j)] += w * coef;
        }
    }
    return group_db[gu];
}

double cost_of(const Problem& pr, const std::vector<double>& theta) {
    std::vector<double> gdb, gmax;
    double s = 0.0;
    for (std::size_t i = 0; i < pr.rays.size(); ++i) {
        const double e = point_level(pr, i, theta, nullptr, gdb, gmax) - pr.measured[i];
        s += e * e;
    }
    return s;
}

/// Solves M x = v (M symmetric, n x n, row-major) by Gaussian elimination
/// with partial pivoting. False when M is singular.
bool solve(std::vector<double> m, std::vector<double> v, std::size_t n, std::vector<double>& x) {
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t piv = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::fabs(m[r * n + c]) > std::fabs(m[piv * n + c])) piv = r;
        if (!(std::fabs(m[piv * n + c]) > 1e-300)) return false;
        if (piv != c) {
            for (std::size_t k = 0; k < n; ++k) std::swap(m[c * n + k], m[piv * n + k]);
            std::swap(v[c], v[piv]);
        }
        for (std::size_t r = c + 1; r < n; ++r) {
            const double f = m[r * n + c] / m[c * n + c];
            if (f == 0.0) continue;
            for (std::size_t k = c; k < n; ++k) m[r * n + k] -= f * m[c * n + k];
            v[r] -= f * v[c];
        }
    }
    x.assign(n, 0.0);
    for (std::size_t c = n; c-- > 0;) {
        double s = v[c];
        for (std::size_t k = c + 1; k < n; ++k) s -= m[c * n + k] * x[k];
        x[c] = s / m[c * n + c];
    }
    for (double xi : x)
        if (!std::isfinite(xi)) return false;
    return true;
}

struct SolveOutcome {
    int iterations = 0;
    std::string reason;
    bool cancelled = false;
};

// Stop criteria (docs/PROPAGATION.md): an accepted step whose largest
// parameter change is below kStepTol; a cost below kCostTol (exact fit);
// an accepted step that lowers the cost by less than kRelTol of it; the
// damping above kMaxLambda (no step lowers the cost: a minimum within the
// bounds); the projected gradient zero; or the iteration limit.
constexpr double kStepTol = 1e-9;
constexpr double kCostTol = 1e-20;
constexpr double kRelTol = 1e-14;
constexpr double kMaxLambda = 1e12;

SolveOutcome levenberg_marquardt(const Problem& pr, std::vector<double>& theta, const std::vector<Bounds>& bounds,
                                 int max_iterations, const std::atomic<bool>* cancel) {
    const std::size_t m = theta.size();
    SolveOutcome out;
    if (m == 0) {
        out.reason = "nothing to fit";
        return out;
    }
    double lambda = 1e-3;
    double cost = cost_of(pr, theta);
    std::vector<double> grad(m), jtj(m * m), jte(m), gdb, gmax;
    for (out.iterations = 0; out.iterations < max_iterations;) {
        if (cancel != nullptr && cancel->load()) {
            out.cancelled = true;
            out.reason = "cancelled";
            return out;
        }
        if (cost <= kCostTol) {
            out.reason = "converged: exact fit";
            return out;
        }
        std::fill(jtj.begin(), jtj.end(), 0.0);
        std::fill(jte.begin(), jte.end(), 0.0);
        for (std::size_t i = 0; i < pr.rays.size(); ++i) {
            const double e = point_level(pr, i, theta, &grad, gdb, gmax) - pr.measured[i];
            for (std::size_t j = 0; j < m; ++j) {
                if (grad[j] == 0.0) continue;
                jte[j] += grad[j] * e;
                for (std::size_t k = 0; k < m; ++k) jtj[j * m + k] += grad[j] * grad[k];
            }
        }
        // Free parameters: not on a bound that the descent direction -g
        // would cross.
        std::vector<std::size_t> free;
        for (std::size_t j = 0; j < m; ++j) {
            const bool at_lo = theta[j] <= bounds[j].lo && jte[j] > 0.0;
            const bool at_hi = theta[j] >= bounds[j].hi && jte[j] < 0.0;
            if (!at_lo && !at_hi) free.push_back(j);
        }
        double gmax_abs = 0.0;
        for (std::size_t j : free) gmax_abs = std::max(gmax_abs, std::fabs(jte[j]));
        if (free.empty() || gmax_abs == 0.0) {
            out.reason = "converged: gradient zero within the bounds";
            return out;
        }
        double dmax = 0.0;
        for (std::size_t j : free) dmax = std::max(dmax, jtj[j * m + j]);
        bool accepted = false;
        while (!accepted) {
            const std::size_t n = free.size();
            std::vector<double> a(n * n), v(n), x;
            for (std::size_t r = 0; r < n; ++r) {
                for (std::size_t c = 0; c < n; ++c) a[r * n + c] = jtj[free[r] * m + free[c]];
                const double d = std::max(jtj[free[r] * m + free[r]], 1e-12 * std::max(dmax, 1.0));
                a[r * n + r] += lambda * d;
                v[r] = -jte[free[r]];
            }
            std::vector<double> trial = theta;
            double step = 0.0;
            if (solve(a, v, n, x)) {
                for (std::size_t r = 0; r < n; ++r) {
                    const std::size_t j = free[r];
                    trial[j] = std::clamp(theta[j] + x[r], bounds[j].lo, bounds[j].hi);
                    step = std::max(step, std::fabs(trial[j] - theta[j]));
                }
            }
            const double trial_cost = step > 0.0 ? cost_of(pr, trial) : std::numeric_limits<double>::infinity();
            if (trial_cost < cost) {
                accepted = true;
                const double drop = cost - trial_cost;
                theta = trial;
                cost = trial_cost;
                lambda = std::max(lambda / 10.0, 1e-12);
                ++out.iterations;
                if (step < kStepTol) {
                    out.reason = "converged: step below 1e-9";
                    return out;
                }
                if (drop < kRelTol * (cost + drop)) {
                    out.reason = "converged: cost change below 1e-14 relative";
                    return out;
                }
            } else {
                lambda *= 10.0;
                if (lambda > kMaxLambda) {
                    out.reason = "converged: no step lowers the cost";
                    return out;
                }
            }
        }
    }
    out.reason = "iteration limit reached";
    return out;
}

int find_group(const PathSet& s, const std::optional<std::string>& key) {
    if (!key) return -1;
    for (std::size_t g = 0; g < s.groups.size(); ++g)
        if (s.groups[g].key == *key) return static_cast<int>(g);
    throw std::invalid_argument("no source group '" + *key + "' among the traced antennas");
}

double rms(const std::vector<double>& e) {
    if (e.empty()) return 0.0;
    double s = 0.0;
    for (double x : e) s += x * x;
    return std::sqrt(s / static_cast<double>(e.size()));
}

/// The loss of class `id` at `hz` under `p` (its fitted value, else the
/// table), as the ray tracer takes it.
double class_loss(const building::Building& b, const ModelParams& p, const std::string& id, double hz) {
    if (auto ov = p.wall_class_loss_db.find(id); ov != p.wall_class_loss_db.end()) return ov->second;
    const building::WallClass* wc = b.find_class(id);
    if (wc == nullptr) throw std::invalid_argument("unknown wall class '" + id + "'");
    return wc->attenuation_at_db(hz / 1e6);
}

}  // namespace

std::vector<std::string> FitOptions::validate() const {
    std::vector<std::string> out;
    check_bounds(out, "eirp_dbm", eirp_dbm, -50.0, 80.0);
    check_bounds(out, "exponent", exponent, 1.0, 8.0);
    for (const auto& [id, b] : wall_class_bounds) check_bounds(out, "wall class '" + id + "'", b, 0.0, 100.0);
    check_bounds(out, "floor_loss_db", floor_loss_db, 0.0, 100.0);
    if (max_iterations < 1 || max_iterations > 10000)
        out.push_back("max_iterations: " + std::to_string(max_iterations) + " is outside 1 .. 10000");
    return out;
}

std::vector<std::string> SourceSearchOptions::validate() const {
    std::vector<std::string> out;
    if (max_sources < 1 || max_sources > 8)
        out.push_back("max_sources: " + std::to_string(max_sources) + " is outside 1 .. 8");
    if (!std::isfinite(min_improvement_db) || min_improvement_db < 0.0 || min_improvement_db > 100.0)
        out.push_back("min_improvement_db: " + fmt(min_improvement_db) + " is outside 0 .. 100");
    if (!std::isfinite(step_m) || (step_m != 0.0 && (step_m < 0.1 || step_m > 100.0)))
        out.push_back("step_m: " + fmt(step_m) + " is neither 0 nor inside 0.1 .. 100");
    if (!std::isfinite(height_m) || height_m < 0.0 || height_m > 100.0)
        out.push_back("height_m: " + fmt(height_m) + " is outside 0 .. 100");
    check_bounds(out, "eirp_dbm", eirp_dbm, -50.0, 80.0);
    return out;
}

FitResult fit_model(const building::Building& b, const PathSet& rays, const std::vector<double>& measured_dbm,
                    const ModelParams& start, const FitOptions& options, const std::atomic<bool>* cancel) {
    auto problems = options.validate();
    for (auto& x : start.validate()) problems.push_back(x);
    if (!problems.empty()) {
        std::string m = "invalid fit options:";
        for (const auto& x : problems) m += " " + x + ";";
        throw std::invalid_argument(m);
    }
    if (measured_dbm.size() != rays.points_m.size() || rays.rays.size() != rays.points_m.size())
        throw std::invalid_argument("fit: measured values and traced points differ in number");
    const int target = find_group(rays, options.group_key);
    const std::vector<double> freq = group_frequencies(b, rays, start);

    FitResult res;
    res.params = start;

    // Which classes, antennas and slabs the measured points depend on.
    std::vector<bool> class_seen(rays.wall_classes.size(), false);
    std::vector<bool> ant_seen(rays.antennas.size(), false);
    bool slabs_seen = false;
    for (std::size_t i = 0; i < rays.rays.size(); ++i) {
        if (!std::isfinite(measured_dbm[i])) continue;
        for (const RayTerm& r : rays.rays[i]) {
            if (target >= 0 && rays.antenna_group[static_cast<std::size_t>(r.antenna)] != target) continue;
            ant_seen[static_cast<std::size_t>(r.antenna)] = true;
            slabs_seen = slabs_seen || r.slabs > 0;
            for (const auto& [c, n] : r.class_crossings)
                if (n > 0) class_seen[static_cast<std::size_t>(c)] = true;
        }
    }

    std::vector<int> class_param(rays.wall_classes.size(), -1);
    std::vector<int> ant_param(rays.antennas.size(), -1);
    std::vector<double> eirp;
    std::vector<double> theta;
    std::vector<Bounds> bounds;
    std::vector<FitParameter> params;

    for (std::size_t a = 0; a < rays.antennas.size(); ++a) {
        const std::string& id = rays.antennas[a];
        const building::Antenna* ant = b.find_antenna(id).second;
        auto ov = start.antenna_eirp_dbm.find(id);
        const bool given = ov != start.antenna_eirp_dbm.end();
        const bool known = given || (ant != nullptr && ant->effective_eirp_dbm());
        eirp.push_back(given ? ov->second : known ? *ant->effective_eirp_dbm() : start.default_eirp_dbm);
        const bool wanted = options.eirp == EirpFit::All || (options.eirp == EirpFit::Unknown && !known);
        if (!wanted) continue;
        if (!ant_seen[a]) {
            res.not_fitted.push_back("EIRP of antenna '" + id + "': no measured point depends on it");
            continue;
        }
        ant_param[a] = static_cast<int>(theta.size());
        theta.push_back(std::clamp(eirp.back(), options.eirp_dbm.lo, options.eirp_dbm.hi));
        bounds.push_back(options.eirp_dbm);
        params.push_back({FitParameter::Kind::Eirp, id, theta.back(), 0.0, options.eirp_dbm, false});
    }
    int exp_param = -1;
    if (options.fit_exponent) {
        exp_param = static_cast<int>(theta.size());
        theta.push_back(std::clamp(start.path_loss_exponent, options.exponent.lo, options.exponent.hi));
        bounds.push_back(options.exponent);
        params.push_back(
            {FitParameter::Kind::Exponent, "path_loss_exponent", theta.back(), 0.0, options.exponent, false});
    }
    // The start value of a fitted class: at the frequency of the target
    // group (else the first group); fixed classes keep their value per group.
    const double class_hz = freq.empty() ? 0.0 : freq[static_cast<std::size_t>(std::max(target, 0))];
    for (std::size_t c = 0; c < rays.wall_classes.size(); ++c) {
        const std::string& id = rays.wall_classes[c];
        if (!options.fit_wall_classes) continue;
        if (!class_seen[c]) {
            res.not_fitted.push_back("wall class '" + id + "': no measured point behind such a wall");
            continue;
        }
        const double v = class_loss(b, start, id, class_hz);
        Bounds bd{0.0, std::min(100.0, 2.0 * v + 10.0)};
        if (auto it = options.wall_class_bounds.find(id); it != options.wall_class_bounds.end()) bd = it->second;
        class_param[c] = static_cast<int>(theta.size());
        theta.push_back(std::clamp(v, bd.lo, bd.hi));
        bounds.push_back(bd);
        params.push_back({FitParameter::Kind::WallClass, id, theta.back(), 0.0, bd, false});
    }
    int floor_param = -1;
    if (options.fit_floor_loss) {
        if (!slabs_seen) {
            res.not_fitted.push_back("dB per floor: no measured point depends on an antenna of another floor");
        } else {
            floor_param = static_cast<int>(theta.size());
            const double v = start.floor_loss_db ? *start.floor_loss_db : b.floor_attenuation_db;
            theta.push_back(std::clamp(v, options.floor_loss_db.lo, options.floor_loss_db.hi));
            bounds.push_back(options.floor_loss_db);
            params.push_back(
                {FitParameter::Kind::FloorLoss, "floor_loss_db", theta.back(), 0.0, options.floor_loss_db, false});
        }
    }

    // The fixed class losses per group (at the group's frequency).
    std::vector<std::vector<double>> class_db(freq.size());
    for (std::size_t g = 0; g < freq.size(); ++g)
        for (const std::string& id : rays.wall_classes) class_db[g].push_back(class_loss(b, start, id, freq[g]));

    // The linear form of every ray to a used point.
    Problem pr;
    pr.target = target;
    pr.groups = rays.groups.size();
    const double d0 = start.reference_distance_m;
    std::vector<std::size_t> used_index;
    for (std::size_t i = 0; i < rays.rays.size(); ++i) {
        if (!std::isfinite(measured_dbm[i])) continue;
        std::vector<LinRay> lr;
        for (const RayTerm& r : rays.rays[i]) {
            LinRay x;
            const auto ai = static_cast<std::size_t>(r.antenna);
            x.group = rays.antenna_group[ai];
            const auto g = static_cast<std::size_t>(x.group);
            // Free space up to d0 (no exponent), log-distance beyond.
            const bool beyond = r.length_m > d0;
            const double pl0 = path_loss_db(beyond ? d0 : r.length_m, freq[g], start.path_loss_exponent, d0);
            const double lg = beyond ? -10.0 * std::log10(r.length_m / d0) : 0.0;
            x.c = -pl0 - r.fixed_wall_db - r.default_reflections * start.reflection_loss_db - r.class_reflection_db -
                  r.pattern_loss_db;
            if (floor_param >= 0) {
                if (r.slabs > 0) x.a.emplace_back(floor_param, -static_cast<double>(r.slabs));
            } else {
                x.c -= start.floor_loss_db ? r.slabs * *start.floor_loss_db : r.slab_db;
            }
            if (ant_param[ai] >= 0)
                x.a.emplace_back(ant_param[ai], 1.0);
            else
                x.c += eirp[ai];
            if (exp_param >= 0) {
                if (beyond) x.a.emplace_back(exp_param, lg);
            } else {
                x.c += start.path_loss_exponent * lg;
            }
            for (const auto& [c, n] : r.class_crossings) {
                const auto cu = static_cast<std::size_t>(c);
                if (class_param[cu] >= 0)
                    x.a.emplace_back(class_param[cu], -static_cast<double>(n));
                else
                    x.c -= n * class_db[g][cu];
            }
            lr.push_back(std::move(x));
        }
        pr.rays.push_back(std::move(lr));
        pr.measured.push_back(measured_dbm[i]);
        used_index.push_back(i);
    }
    // Points the model does not reach are left out.
    {
        std::vector<double> gdb, gmax;
        Problem kept;
        kept.target = pr.target;
        kept.groups = pr.groups;
        std::vector<std::size_t> kept_index;
        for (std::size_t k = 0; k < pr.rays.size(); ++k) {
            if (point_level(pr, k, theta, nullptr, gdb, gmax) == kNoLevel) continue;
            kept.rays.push_back(std::move(pr.rays[k]));
            kept.measured.push_back(pr.measured[k]);
            kept_index.push_back(used_index[k]);
        }
        pr = std::move(kept);
        used_index = std::move(kept_index);
    }

    // Deterministic start: the fitted EIRPs move together by the mean
    // residual (the closed-form best common offset), within the bounds.
    if (!pr.rays.empty()) {
        std::vector<double> gdb, gmax;
        double mean = 0.0;
        for (std::size_t k = 0; k < pr.rays.size(); ++k)
            mean += pr.measured[k] - point_level(pr, k, theta, nullptr, gdb, gmax);
        mean /= static_cast<double>(pr.rays.size());
        for (std::size_t j = 0; j < params.size(); ++j)
            if (params[j].kind == FitParameter::Kind::Eirp) {
                theta[j] = std::clamp(theta[j] + mean, bounds[j].lo, bounds[j].hi);
                params[j].start = theta[j];
            }
    }
    res.quality.points = pr.rays.size();
    res.quality.start_rms_db =
        pr.rays.empty() ? 0.0 : std::sqrt(cost_of(pr, theta) / static_cast<double>(pr.rays.size()));

    if (pr.rays.empty()) {
        res.stop_reason = "no measured point with a modelled level";
    } else {
        const auto o = levenberg_marquardt(pr, theta, bounds, options.max_iterations, cancel);
        res.iterations = o.iterations;
        res.stop_reason = o.reason;
        res.cancelled = o.cancelled;
    }

    for (std::size_t j = 0; j < params.size(); ++j) {
        params[j].value = theta[j];
        params[j].at_bound = theta[j] <= bounds[j].lo || theta[j] >= bounds[j].hi;
        switch (params[j].kind) {
        case FitParameter::Kind::Eirp:
            res.params.antenna_eirp_dbm[params[j].id] = theta[j];
            break;
        case FitParameter::Kind::Exponent:
            res.params.path_loss_exponent = theta[j];
            break;
        case FitParameter::Kind::WallClass:
            res.params.wall_class_loss_db[params[j].id] = theta[j];
            break;
        case FitParameter::Kind::FloorLoss:
            res.params.floor_loss_db = theta[j];
            break;
        }
    }
    res.parameters = std::move(params);

    // The reported quality comes from evaluate(), the model the map uses.
    const PointLevels lv = evaluate(b, rays, res.params);
    const auto& model = target >= 0 ? lv.level_dbm[static_cast<std::size_t>(target)] : lv.best_dbm;
    res.model_dbm = model;
    res.used.assign(rays.points_m.size(), false);
    std::vector<double> err;
    for (std::size_t i : used_index) {
        res.used[i] = true;
        err.push_back(model[i] - measured_dbm[i]);
    }
    if (!err.empty()) {
        res.quality.rms_db = rms(err);
        res.quality.mean_db = std::accumulate(err.begin(), err.end(), 0.0) / static_cast<double>(err.size());
        for (double e : err) res.quality.max_abs_db = std::max(res.quality.max_abs_db, std::fabs(e));
    }
    return res;
}

SourceEstimate estimate_sources(const building::Building& b, const std::string& floor_id,
                                const std::vector<Point2>& points_m, const std::vector<double>& measured_dbm,
                                const ModelParams& p, const SourceSearchOptions& options, const RunControl& run) {
    auto problems = options.validate();
    if (!problems.empty()) throw std::invalid_argument("invalid source search options: " + problems.front());
    if (measured_dbm.size() != points_m.size())
        throw std::invalid_argument("source search: measured values and points differ in number");
    if (!p.frequency_hz) throw std::invalid_argument("source search: ModelParams::frequency_hz is not set");
    // The building without antennas, traced with direct rays only.
    building::Building bare = b;
    for (auto& f : bare.floors) f.antennas.clear();
    const building::Floor* rf = bare.find_floor(floor_id);
    if (rf == nullptr) throw std::invalid_argument("no floor '" + floor_id + "'");
    if (!bare.has_scale(*rf)) throw std::logic_error("floor '" + floor_id + "' has no scale");
    const std::size_t floor_index = static_cast<std::size_t>(rf - bare.floors.data());
    ModelParams tp = p;
    tp.max_reflections = 0;
    tp.antenna_eirp_dbm.clear();

    const Grid area = floor_grid(bare, *rf, 1.0);
    const double w = area.cols * area.cell_m;
    const double h = area.rows * area.cell_m;
    const double step = options.step_m > 0.0 ? options.step_m : std::max(0.5, std::max(w, h) / 32.0);
    std::vector<Point2> coarse;
    for (double y = area.origin_m.y + step / 2; y < area.origin_m.y + h; y += step)
        for (double x = area.origin_m.x + step / 2; x < area.origin_m.x + w; x += step) coarse.push_back({x, y});
    auto inside = [&](Point2 c) {
        return c.x >= area.origin_m.x && c.x <= area.origin_m.x + w && c.y >= area.origin_m.y &&
               c.y <= area.origin_m.y + h;
    };

    FitOptions fo;
    fo.eirp = EirpFit::All;
    fo.eirp_dbm = options.eirp_dbm;
    fo.fit_exponent = false;
    fo.fit_wall_classes = false;
    fo.fit_floor_loss = false;
    fo.max_iterations = 100;

    auto make_antenna = [&](std::size_t j, Point2 m) {
        building::Antenna a;
        a.id = "estimated-" + std::to_string(j + 1);
        a.name = "Estimated source " + std::to_string(j + 1);
        a.model = "estimated";
        a.position = *bare.from_plan_metres(*rf, m);
        a.height_m = options.height_m;
        a.gain_dbi = 0.0;
        return a;
    };
    // The rays of source j at m (one antenna, its own group).
    auto trace_one = [&](std::size_t j, Point2 m) {
        building::Building one = bare;
        one.floors[floor_index].antennas.push_back(make_antenna(j, m));
        RunControl single;
        single.isa = run.isa;
        single.threads = 1;
        return trace_points(one, floor_id, points_m, tp, single);
    };
    // The rays of several sources together (antenna j = parts[j]).
    auto merge = [&](const std::vector<const PathSet*>& parts) {
        PathSet m = *parts.front();
        for (std::size_t k = 1; k < parts.size(); ++k) {
            const PathSet& c = *parts[k];
            const int a0 = static_cast<int>(m.antennas.size());
            m.antennas.push_back(c.antennas.at(0));
            if (options.one_cell) {
                m.antenna_group.push_back(0);
                m.groups[0].antenna_ids.push_back(c.antennas.at(0));
            } else {
                m.antenna_group.push_back(static_cast<int>(m.groups.size()));
                m.groups.push_back(c.groups.at(0));
            }
            for (std::size_t i = 0; i < m.rays.size(); ++i)
                for (RayTerm r : c.rays[i]) {
                    r.antenna += a0;
                    m.rays[i].push_back(std::move(r));
                }
        }
        return m;
    };
    auto fit_parts = [&](const std::vector<const PathSet*>& parts) {
        return fit_model(bare, merge(parts), measured_dbm, tp, fo, run.cancel);
    };
    auto rms_of = [](const FitResult& r) {
        return r.quality.points != 0 ? r.quality.rms_db : std::numeric_limits<double>::infinity();
    };

    std::vector<Point2> pos;    // kept sources
    std::vector<PathSet> sets;  // their rays
    SourceEstimate out;
    const auto stages = static_cast<std::size_t>(options.max_sources);
    constexpr int kFine = 4;  // finer rounds, spacing halved each round (down to step / 16)
    constexpr int kPolishPasses = 2;
    // Searches of a full run, for the progress fraction (an early stop
    // jumps to 1).
    const std::size_t total_work =
        stages * (1 + kFine) + kPolishPasses * (stages * (stages + 1) / 2 - 1) * (1 + kFine);
    std::size_t work_done = 0;

    // The best position of source j among candidates, the other kept
    // sources fixed; the first candidate in order wins a tie. False when
    // cancelled.
    auto search = [&](std::size_t j, const std::vector<Point2>& cands, Point2& best_pos, double& best) {
        std::vector<double> q(cands.size(), std::numeric_limits<double>::infinity());
        RunControl rc;
        rc.threads = run.threads;
        rc.cancel = run.cancel;
        if (run.progress) {
            const double base = static_cast<double>(work_done) / static_cast<double>(total_work);
            rc.progress = [&, base](double f) {
                run.progress(std::min(1.0, base + f / static_cast<double>(total_work)));
            };
        }
        const bool complete = detail::run_parallel(rc, cands.size(), [&](std::size_t i) {
            const PathSet c = trace_one(j, cands[i]);
            std::vector<const PathSet*> parts;
            for (std::size_t k = 0; k < sets.size(); ++k) parts.push_back(k == j ? &c : &sets[k]);
            if (j == sets.size()) parts.push_back(&c);
            q[i] = rms_of(fit_parts(parts));
        });
        ++work_done;
        if (!complete) return false;
        for (std::size_t i = 0; i < cands.size(); ++i)
            if (q[i] < best) {
                best = q[i];
                best_pos = cands[i];
            }
        return true;
    };
    auto local = [&](Point2 c, double spacing) {
        std::vector<Point2> cands;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const Point2 x{c.x + dx * spacing, c.y + dy * spacing};
                if (inside(x)) cands.push_back(x);
            }
        return cands;
    };
    auto cancelled = [&] {
        out.cancelled = true;
        return out;
    };

    for (std::size_t stage = 0; stage < stages; ++stage) {
        // A new source: coarse grid, then finer rounds around the best.
        const std::size_t j = sets.size();
        Point2 best_pos{};
        double best = std::numeric_limits<double>::infinity();
        if (!search(j, coarse, best_pos, best)) return cancelled();
        double spacing = step;
        for (int r = 0; r < kFine; ++r) {
            spacing /= 2.0;
            if (!search(j, local(best_pos, spacing), best_pos, best)) return cancelled();
        }
        if (!std::isfinite(best)) break;
        auto trial_pos = pos;
        auto trial_sets = sets;
        trial_pos.push_back(best_pos);
        trial_sets.push_back(trace_one(j, best_pos));
        std::swap(pos, trial_pos);
        std::swap(sets, trial_sets);
        // Polish: every kept source is searched again over the whole floor,
        // the others fixed (the first ones were placed without the later
        // ones), until no position changes (at most kPolishPasses passes).
        if (pos.size() > 1)
            for (int pass = 0; pass < kPolishPasses; ++pass) {
                bool moved = false;
                for (std::size_t k = 0; k < pos.size(); ++k) {
                    Point2 bp{};
                    double q = std::numeric_limits<double>::infinity();
                    if (!search(k, coarse, bp, q)) return cancelled();
                    double sp = step;
                    for (int r = 0; r < kFine; ++r) {
                        sp /= 2.0;
                        if (!search(k, local(bp, sp), bp, q)) return cancelled();
                    }
                    if (!(bp == pos[k])) {
                        moved = true;
                        pos[k] = bp;
                        sets[k] = trace_one(k, bp);
                    }
                }
                if (!moved) break;
            }
        std::vector<const PathSet*> parts;
        for (const auto& x : sets) parts.push_back(&x);
        const FitResult fr = fit_parts(parts);
        const double now = rms_of(fr);
        if (!out.rms_db.empty() && out.rms_db.back() - now < options.min_improvement_db) {
            // Not worth a further source: keep the previous set.
            std::swap(pos, trial_pos);
            std::swap(sets, trial_sets);
            break;
        }
        out.antennas.clear();
        for (std::size_t k = 0; k < pos.size(); ++k) {
            auto a = make_antenna(k, pos[k]);
            a.eirp_dbm = fr.params.antenna_eirp_dbm.at(a.id);
            out.antennas.push_back(a);
        }
        out.rms_db.push_back(now);
        // Later candidates start from the kept EIRPs.
        tp.antenna_eirp_dbm = fr.params.antenna_eirp_dbm;
        if (now < 1e-6) break;
    }
    if (options.one_cell && !out.antennas.empty()) {
        building::Cell c;
        c.id = kEstimatedCellId;
        c.label = "Estimated sources";
        c.carrier_hz = static_cast<std::uint64_t>(std::llround(*p.frequency_hz));
        out.cell = c;
        for (auto& a : out.antennas) a.cell = c.id;
    }
    if (run.progress) run.progress(1.0);
    return out;
}

void add_estimated_sources(building::Building& b, const std::string& floor_id, const SourceEstimate& estimate) {
    building::Floor* f = b.find_floor(floor_id);
    if (f == nullptr) throw std::invalid_argument("no floor '" + floor_id + "'");
    for (const auto& a : estimate.antennas)
        if (b.find_antenna(a.id).second != nullptr)
            throw std::invalid_argument("antenna id '" + a.id + "' is already used");
    if (estimate.cell) {
        auto it = std::find_if(b.cells.begin(), b.cells.end(), [&](const auto& c) { return c.id == estimate.cell->id; });
        if (it != b.cells.end())
            *it = *estimate.cell;
        else
            b.cells.push_back(*estimate.cell);
    }
    for (const auto& a : estimate.antennas) f->antennas.push_back(a);
}

}  // namespace yantra::raytrace
