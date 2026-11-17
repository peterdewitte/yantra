// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolated coverage map (IND-41, IND-43, IND-45, EXP-06): IDW of
// the measured points; see interpolation.h and docs/INTERPOLATION.md.
//
// Storage, statistics, readout and job copied from yantra-v2 68ea20a:
// core/src/survey/interpolation.cpp, adapted (IDW instead of the
// ray-tracing model, Yantra 0.4).

#include "yantra/core/survey/interpolation.h"

#include "idw_kernels.h"
#include "parallel.h"

#include "yantra/core/building/model_json.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/util/sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace yantra::survey {

using json = nlohmann::json;
using project::ProjectError;
using Code = ProjectError::Code;

namespace {

constexpr int kFormat = 2;  // version of the parameters/result JSON (1: the ray-tracing map of 0.3)

std::string fmt(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

// ---- JSON of the parameters and results ---------------------------------

template <class E, std::size_t N>
const char* name_of(E e, const std::pair<E, const char*> (&names)[N]) {
    for (const auto& [v, n] : names)
        if (v == e) return n;
    return names[0].second;
}
template <class E, std::size_t N>
E value_of(const std::string& s, const std::pair<E, const char*> (&names)[N], const char* what) {
    for (const auto& [v, n] : names)
        if (s == n) return v;
    throw std::invalid_argument(std::string(what) + ": " + s);
}

constexpr std::pair<Metric::Carrier, const char*> kCarrier[] = {
    {Metric::Carrier::Serving, "serving"},
    {Metric::Carrier::Strongest, "strongest"},
    {Metric::Carrier::Frequency, "frequency"}};
constexpr std::pair<Metric::Value, const char*> kValue[] = {
    {Metric::Value::Level, "level"}, {Metric::Value::C1, "c1"}, {Metric::Value::Quality, "quality"}};

json to_json(const Metric& m) {
    json failed = json::array();
    for (const CellRef& c : m.failed_cells) failed.push_back({{"frequency_hz", c.frequency_hz}, {"cell", c.cell}});
    return {{"carrier", name_of(m.carrier, kCarrier)},
            {"value", name_of(m.value, kValue)},
            {"frequency_hz", m.frequency_hz},
            {"quality_field", m.quality_field},
            {"device", m.device},
            {"failed_cells", failed}};
}
Metric metric_of(const json& j) {
    Metric m;
    m.carrier = value_of(j.at("carrier").get<std::string>(), kCarrier, "carrier");
    m.value = value_of(j.at("value").get<std::string>(), kValue, "value");
    m.frequency_hz = j.at("frequency_hz").get<std::uint64_t>();
    m.quality_field = j.at("quality_field").get<std::string>();
    m.device = j.at("device").get<std::string>();
    for (const json& c : j.at("failed_cells"))
        m.failed_cells.push_back({c.at("frequency_hz").get<std::uint64_t>(), c.at("cell").get<std::string>()});
    return m;
}

json to_json(const project::InterpolationSettings& s) {
    return {{"grid_m", s.grid_m}, {"power", s.power}, {"radius_m", s.radius_m}, {"full_confidence_m", s.full_confidence_m}};
}
project::InterpolationSettings settings_of(const json& j) {
    project::InterpolationSettings s;
    s.grid_m = j.at("grid_m").get<double>();
    s.power = j.at("power").get<double>();
    s.radius_m = j.at("radius_m").get<double>();
    s.full_confidence_m = j.at("full_confidence_m").get<double>();
    return s;
}

json to_json(const InterpolationRequest& r) {
    return {{"floor_id", r.floor_id},
            {"metric", to_json(r.metric)},
            {"sessions", r.sessions},
            {"settings", r.settings ? to_json(*r.settings) : json(nullptr)}};
}
InterpolationRequest request_of(const json& j) {
    InterpolationRequest r;
    r.floor_id = j.at("floor_id").get<std::string>();
    r.metric = metric_of(j.at("metric"));
    r.sessions = j.at("sessions").get<std::vector<std::int64_t>>();
    if (!j.at("settings").is_null()) r.settings = settings_of(j.at("settings"));
    return r;
}

json result_json(const Interpolation& r) {
    json points = json::array();
    for (const auto& p : r.points)
        points.push_back(json::array({p.session_id, p.sequence, p.position_m.x, p.position_m.y, p.measured_dbm}));
    return {{"format", kFormat}, {"label", r.label()}, {"floor_cells", r.floor_cells}, {"points", points}};
}

void read_result(const json& j, Interpolation& r) {
    if (j.at("format").get<int>() != kFormat) throw std::invalid_argument("unknown result format");
    r.floor_cells = j.at("floor_cells").get<std::size_t>();
    for (const json& p : j.at("points"))
        r.points.push_back({p.at(0).get<std::int64_t>(), p.at(1).get<std::int64_t>(),
                            Point2{p.at(2).get<double>(), p.at(3).get<double>()}, p.at(4).get<double>()});
}

// ---- cells ------------------------------------------------------------------

void put_f32(std::vector<std::uint8_t>& out, float v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
}
float get_f32(const std::uint8_t* p) {
    std::uint32_t u = 0;
    for (int k = 0; k < 4; ++k) u |= static_cast<std::uint32_t>(p[k]) << (8 * k);
    float v = 0.0f;
    std::memcpy(&v, &u, 4);
    return v;
}

/// 1 for the cells of `grid` holding one of `positions`.
std::vector<std::uint8_t> measured_cells(const Grid& grid, const std::vector<Point2>& positions) {
    std::vector<std::uint8_t> out(grid.size(), 0);
    for (const Point2& p : positions)
        if (auto c = grid.cell_at(p)) out[*c] = 1;
    return out;
}

// ---- inputs -----------------------------------------------------------------

std::string number(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}

/// The points with a level in dBm and a position in metres.
struct Measured {
    std::vector<const SurveyPoint*> points;
    std::vector<Point2> metres;
    std::vector<double> dbm;
};
Measured measured_points(const PointList& list) {
    Measured m;
    for (const SurveyPoint& p : list.points) {
        if (!p.value || p.unit != "dBm" || !p.metres) continue;
        m.points.push_back(&p);
        m.metres.push_back(*p.metres);
        m.dbm.push_back(*p.value);
    }
    return m;
}

PointList request_points(const project::Project& project, const InterpolationRequest& r) {
    PointQuery q;
    q.floor_id = r.floor_id;
    q.sessions = r.sessions;
    q.metric = r.metric;
    return point_list(project, q);
}

std::string points_fingerprint(const InterpolationRequest& r, const Measured& m) {
    util::Sha256 h;
    h.update("yantra-interpolation-points 1\n" + r.floor_id + "\n" + to_json(r.metric).dump() + "\n");
    for (std::size_t i = 0; i < m.points.size(); ++i) {
        const SurveyPoint& p = *m.points[i];
        h.update(std::to_string(p.session_id) + "," + std::to_string(p.sequence) + "," + number(m.metres[i].x) +
                 "," + number(m.metres[i].y) + "," + number(m.dbm[i]) + "," + std::to_string(p.frequency_hz) + "\n");
    }
    return h.finish_hex();
}

std::string building_fingerprint(const building::Building& b) {
    util::Sha256 h;
    h.update("yantra-interpolation-building 1\n");
    h.update(building::building_to_json(b));
    return h.finish_hex();
}

project::InterpolationSettings settings_for(const project::Project& project, const InterpolationRequest& r) {
    return r.settings ? *r.settings : project.interpolation_settings();
}

std::string settings_fingerprint(const project::InterpolationSettings& s, const InterpolationRequest& r) {
    const json j{{"settings", to_json(s)}, {"request", to_json(r)}};
    util::Sha256 h;
    h.update("yantra-interpolation-settings 2\n" + j.dump());
    return h.finish_hex();
}

void check_settings(const project::InterpolationSettings& s) {
    const auto problems = s.validate();
    if (!problems.empty()) throw std::invalid_argument("interpolation settings: " + problems.front());
}

// ---- IDW --------------------------------------------------------------------

/// The samples sorted into square buckets of side `size` (row-major by
/// bucket, then sample order), as structure of arrays for the kernel.
struct Buckets {
    double size = 1.0;
    std::vector<double> x, y, v;
    std::map<std::pair<std::int64_t, std::int64_t>, std::pair<std::size_t, std::size_t>> ranges;

    std::int64_t idx(double c) const { return static_cast<std::int64_t>(std::floor(c / size)); }

    Buckets(const std::vector<IdwSample>& s, double bucket) : size(bucket) {
        std::vector<std::pair<std::pair<std::int64_t, std::int64_t>, std::size_t>> order;
        order.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i)
            order.push_back({{idx(s[i].position_m.y), idx(s[i].position_m.x)}, i});
        std::sort(order.begin(), order.end());
        for (std::size_t k = 0; k < order.size(); ++k) {
            const auto& [key, i] = order[k];
            auto [it, fresh] = ranges.try_emplace(key, k, k);
            it->second.second = k + 1;
            (void)fresh;
            x.push_back(s[i].position_m.x);
            y.push_back(s[i].position_m.y);
            v.push_back(s[i].value_db);
        }
    }
};

double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

}  // namespace

bool simd_built() noexcept { return YANTRA_IDW_SIMD != 0; }

std::optional<std::size_t> Grid::cell_at(Point2 m) const {
    const double cx = std::floor((m.x - origin_m.x) / cell_m);
    const double cy = std::floor((m.y - origin_m.y) / cell_m);
    if (!(cx >= 0.0 && cy >= 0.0 && cx < cols && cy < rows)) return std::nullopt;
    return index(static_cast<int>(cx), static_cast<int>(cy));
}

Grid floor_grid(const building::Building& b, const building::Floor& f, double cell_m,
                const std::vector<Point2>& points_m) {
    const auto mpu = b.metres_per_unit(f);
    if (!mpu) throw std::logic_error("floor '" + f.id + "' has no scale");
    if (!std::isfinite(cell_m) || cell_m < 0.05 || cell_m > 10.0)
        throw std::invalid_argument("grid cell size " + fmt(cell_m) + " m is outside 0.05 .. 10 m");
    Point2 lo;
    Point2 hi;
    if (f.plan_width > 0.0 && f.plan_height > 0.0) {
        hi = {f.plan_width * *mpu, f.plan_height * *mpu};
    } else {
        if (points_m.empty()) throw std::invalid_argument("floor '" + f.id + "' has no plan size and no points");
        lo = hi = points_m.front();
        for (const Point2& m : points_m) {
            lo = {std::min(lo.x, m.x), std::min(lo.y, m.y)};
            hi = {std::max(hi.x, m.x), std::max(hi.y, m.y)};
        }
        lo = {lo.x - 1.0, lo.y - 1.0};
        hi = {hi.x + 1.0, hi.y + 1.0};
    }
    Grid g;
    g.origin_m = lo;
    g.cell_m = cell_m;
    const double cols = std::ceil((hi.x - lo.x) / cell_m - 1e-9);
    const double rows = std::ceil((hi.y - lo.y) / cell_m - 1e-9);
    if (!(cols >= 1.0 && rows >= 1.0) || cols * rows > static_cast<double>(kMaxGridCells))
        throw std::invalid_argument("grid of floor '" + f.id + "' would have " + fmt(cols) + " x " + fmt(rows) +
                                    " cells (1 .. " + std::to_string(kMaxGridCells) + ")");
    g.cols = static_cast<int>(cols);
    g.rows = static_cast<int>(rows);
    return g;
}

IdwField idw(const Grid& grid, const std::vector<IdwSample>& samples, const project::InterpolationSettings& s,
             const RunControl& run) {
    check_settings(s);
    if (grid.cols <= 0 || grid.rows <= 0 || !(grid.cell_m > 0.0)) throw std::invalid_argument("idw: empty grid");
    detail::Dist2Fn dist2 = detail::dist2_scalar;
#if YANTRA_IDW_SIMD
    if (run.isa == Isa::Simd) dist2 = detail::dist2_simd;
#endif
    const std::size_t n = grid.size();
    IdwField out;
    out.value_db.assign(n, -std::numeric_limits<float>::infinity());
    out.confidence.assign(n, 0.0f);
    out.measured.assign(n, 0);

    // The measured cells: the median of their points.
    std::map<std::size_t, std::vector<double>> in_cell;
    for (const IdwSample& p : samples)
        if (auto c = grid.cell_at(p.position_m)) in_cell[*c].push_back(p.value_db);

    const Buckets buckets(samples, s.radius_m);
    const double r2 = s.radius_m * s.radius_m;
    const double half_power = s.power / 2.0;
    const bool square = s.power == 2.0;
    const std::size_t rows = static_cast<std::size_t>(grid.rows);
    const std::size_t cols = static_cast<std::size_t>(grid.cols);
    // One job per grid row; every cell is computed on its own, in a fixed
    // order of the points, so the thread count does not change a value.
    const bool done = detail::run_parallel(run, rows, [&](std::size_t row) {
        std::vector<double> d2;
        for (std::size_t col = 0; col < cols; ++col) {
            const std::size_t i = row * cols + col;
            const Point2 c = grid.centre(static_cast<int>(col), static_cast<int>(row));
            const std::int64_t bi = buckets.idx(c.x);
            const std::int64_t bj = buckets.idx(c.y);
            double sw = 0.0;
            double swv = 0.0;
            double nearest2 = std::numeric_limits<double>::infinity();
            for (std::int64_t j = bj - 1; j <= bj + 1; ++j)
                for (std::int64_t k = bi - 1; k <= bi + 1; ++k) {
                    auto it = buckets.ranges.find({j, k});
                    if (it == buckets.ranges.end()) continue;
                    const auto [b0, b1] = it->second;
                    d2.resize(b1 - b0);
                    dist2(buckets.x.data() + b0, buckets.y.data() + b0, b1 - b0, c.x, c.y, d2.data());
                    for (std::size_t q = 0; q < d2.size(); ++q) {
                        const double dd = d2[q];
                        if (!(dd <= r2)) continue;
                        nearest2 = std::min(nearest2, dd);
                        const double w = square ? 1.0 / dd : std::pow(dd, -half_power);
                        sw += w;
                        swv += w * buckets.v[b0 + q];
                    }
                }
            if (auto m = in_cell.find(i); m != in_cell.end()) {
                std::vector<double> v = m->second;
                out.value_db[i] = static_cast<float>(median(v));
                out.measured[i] = 1;
            } else if (sw > 0.0) {
                out.value_db[i] = static_cast<float>(swv / sw);
            }
            if (std::isfinite(nearest2)) {
                const double d = std::sqrt(nearest2);
                double conf = 0.0;
                if (d <= s.full_confidence_m)
                    conf = 1.0;
                else if (d < s.radius_m)
                    conf = (s.radius_m - d) / (s.radius_m - s.full_confidence_m);
                out.confidence[i] = static_cast<float>(conf);
            }
        }
    });
    out.cancelled = !done;
    return out;
}

std::string InterpolationRequest::key() const {
    std::string k = std::string(name_of(metric.value, kValue)) + "|" + name_of(metric.carrier, kCarrier);
    if (metric.carrier == Metric::Carrier::Frequency) k += ":" + std::to_string(metric.frequency_hz);
    if (!metric.device.empty()) k += "|device:" + metric.device;
    return k;
}

std::string Interpolation::label() const { return kInterpolatedLabel; }

std::vector<bool> floor_cells(const building::Building& b, const building::Floor& f, const Grid& grid) {
    std::vector<bool> out(grid.size(), false);
    const auto mpu = b.metres_per_unit(f);
    if (!mpu) return out;
    std::vector<Point2> outline;  // plan-frame metres
    if (f.trim.shape != building::Trim::Shape::None && f.trim.outline.size() >= 3)
        for (const Point2& page : f.trim.outline) {
            const Point2 u = plan::trim_page_to_floor(f.trim, page);
            outline.push_back({u.x * *mpu, u.y * *mpu});
        }
    const bool has_size = f.plan_width > 0.0 && f.plan_height > 0.0;
    for (int row = 0; row < grid.rows; ++row)
        for (int col = 0; col < grid.cols; ++col) {
            const Point2 c = grid.centre(col, row);
            bool in = true;
            if (!outline.empty())
                in = point_in_polygon(c, outline);
            else if (has_size)
                in = c.x >= 0.0 && c.y >= 0.0 && c.x <= f.plan_width * *mpu && c.y <= f.plan_height * *mpu;
            out[grid.index(col, row)] = in;
        }
    return out;
}

InputFingerprints fingerprints(const project::Project& project, const InterpolationRequest& request) {
    const auto list = request_points(project, request);
    return {building_fingerprint(project.building()), points_fingerprint(request, measured_points(list)),
            settings_fingerprint(settings_for(project, request), request)};
}

std::optional<Interpolation> interpolate(const project::Project& project, const InterpolationRequest& request,
                                         const RunControl& run,
                                         const std::function<void(const std::string&)>& stage) {
    auto tell = [&](const char* s) {
        if (stage) stage(s);
    };
    if (request.metric.value != Metric::Value::Level)
        throw ProjectError(Code::Invalid, "interpolation: the metric must be a level");
    const project::InterpolationSettings settings = settings_for(project, request);
    if (const auto problems = settings.validate(); !problems.empty())
        throw ProjectError(Code::Invalid, "interpolation: " + problems.front());
    tell("Points");
    const building::Building b = project.building();
    const building::Floor* bf = b.find_floor(request.floor_id);
    if (bf == nullptr) throw ProjectError(Code::NotFound, "no floor " + request.floor_id);
    if (!b.has_scale(*bf))
        throw ProjectError(Code::Invalid, "interpolation: floor " + request.floor_id + " has no scale");
    const PointList list = request_points(project, request);
    const Measured m = measured_points(list);
    if (m.points.empty()) throw ProjectError(Code::Invalid, "interpolation: no measured point with a level in dBm");

    Interpolation out;
    out.request = request;
    out.params = settings;
    out.building_sha256 = building_fingerprint(b);
    out.points_sha256 = points_fingerprint(request, m);
    out.settings_sha256 = settings_fingerprint(settings, request);
    std::vector<IdwSample> samples;
    samples.reserve(m.points.size());
    for (std::size_t i = 0; i < m.points.size(); ++i) {
        const SurveyPoint& sp = *m.points[i];
        out.points.push_back({sp.session_id, sp.sequence, m.metres[i], m.dbm[i]});
        samples.push_back({m.metres[i], m.dbm[i]});
    }
    try {
        out.grid = floor_grid(b, *bf, settings.grid_m, m.metres);
    } catch (const std::exception& e) {
        throw ProjectError(Code::Invalid, std::string("interpolation: ") + e.what());
    }

    tell("Map");
    IdwField field = idw(out.grid, samples, settings, run);
    if (field.cancelled || (run.cancel != nullptr && run.cancel->load())) return std::nullopt;
    const std::vector<bool> on_floor = floor_cells(b, *bf, out.grid);
    for (std::size_t i = 0; i < on_floor.size(); ++i) {
        if (!on_floor[i]) field.confidence[i] = 0.0f;
        out.floor_cells += on_floor[i] ? 1 : 0;
    }
    out.value_dbm = std::move(field.value_db);
    out.confidence = std::move(field.confidence);
    out.measured = std::move(field.measured);
    return out;
}

project::StoredInterpolation to_stored(const Interpolation& r) {
    project::StoredInterpolation s;
    s.id = r.id;
    s.floor_id = r.request.floor_id;
    s.key = r.request.key();
    s.created_utc = r.created_utc;
    s.created_with = r.created_with;
    s.building_sha256 = r.building_sha256;
    s.points_sha256 = r.points_sha256;
    s.settings_sha256 = r.settings_sha256;
    s.parameters_json =
        json{{"format", kFormat}, {"request", to_json(r.request)}, {"params", to_json(r.params)}}.dump();
    s.result_json = result_json(r).dump();
    s.origin_m = r.grid.origin_m;
    s.cell_m = r.grid.cell_m;
    s.cols = r.grid.cols;
    s.rows = r.grid.rows;
    s.cells.reserve(r.grid.size() * 8);
    for (std::size_t i = 0; i < r.grid.size(); ++i) {
        put_f32(s.cells, r.value_dbm.at(i));
        put_f32(s.cells, r.confidence.at(i));
    }
    return s;
}

Interpolation from_stored(const project::StoredInterpolation& s) {
    Interpolation r;
    try {
        const json pj = json::parse(s.parameters_json);
        if (pj.at("format").get<int>() != kFormat) throw std::invalid_argument("unknown parameters format");
        r.request = request_of(pj.at("request"));
        r.params = settings_of(pj.at("params"));
        read_result(json::parse(s.result_json), r);
    } catch (const std::exception& e) {
        throw ProjectError(Code::Damaged, "interpolation " + std::to_string(s.id) + ": " + e.what());
    }
    if (s.cols <= 0 || s.rows <= 0 || !(s.cell_m > 0.0) ||
        s.cells.size() != static_cast<std::size_t>(s.cols) * static_cast<std::size_t>(s.rows) * 8u)
        throw ProjectError(Code::Damaged,
                           "interpolation " + std::to_string(s.id) + ": the cells do not match the grid");
    r.id = s.id;
    r.created_utc = s.created_utc;
    r.created_with = s.created_with;
    r.building_sha256 = s.building_sha256;
    r.points_sha256 = s.points_sha256;
    r.settings_sha256 = s.settings_sha256;
    r.grid = {s.origin_m, s.cell_m, s.cols, s.rows};
    const std::size_t n = r.grid.size();
    r.value_dbm.resize(n);
    r.confidence.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* c = s.cells.data() + i * 8;
        r.value_dbm[i] = get_f32(c);
        r.confidence[i] = get_f32(c + 4);
    }
    std::vector<Point2> positions;
    for (const auto& p : r.points) positions.push_back(p.position_m);
    r.measured = measured_cells(r.grid, positions);
    return r;
}

std::int64_t store(project::Project& project, const Interpolation& interpolation) {
    return project.store_interpolation(to_stored(interpolation));
}

std::vector<Interpolation> stored_interpolations(const project::Project& project, const std::string& floor_id) {
    std::vector<Interpolation> out;
    for (const auto& s : project.interpolations(floor_id, true)) out.push_back(from_stored(s));
    return out;
}

std::optional<Interpolation> stored_interpolation(const project::Project& project, std::int64_t id) {
    auto s = project.interpolation(id);
    if (!s) return std::nullopt;
    return from_stored(*s);
}

Staleness staleness(const project::Project& project, const Interpolation& r) {
    Staleness out;
    if (!project.floor(r.request.floor_id)) {
        out.stale = true;
        out.reasons.emplace_back("the floor no longer exists");
        return out;
    }
    const auto now = fingerprints(project, r.request);
    if (now.building != r.building_sha256) out.reasons.emplace_back("floors or scales changed");
    if (now.points != r.points_sha256) out.reasons.emplace_back("measured points changed");
    if (now.settings != r.settings_sha256) out.reasons.emplace_back("interpolation settings changed");
    out.stale = !out.reasons.empty();
    return out;
}

Readout readout(const Interpolation& r, Point2 pos) {
    Readout out;
    out.label = kInterpolatedLabel;
    double best = std::numeric_limits<double>::infinity();
    for (const auto& p : r.points) {
        const double d = std::hypot(p.position_m.x - pos.x, p.position_m.y - pos.y);
        if (d < best) {
            best = d;
            out.nearest = p;
        }
    }
    if (out.nearest) out.nearest_m = best;
    const auto cell = r.grid.cell_at(pos);
    if (!cell) return out;
    out.on_map = true;
    out.confidence = r.confidence[*cell];
    if (*cell < r.measured.size() && r.measured[*cell] != 0) {
        out.measured = true;
        out.label = "measured";
    }
    if (std::isfinite(r.value_dbm[*cell])) out.value_dbm = r.value_dbm[*cell];
    return out;
}

std::vector<std::uint32_t> cell_colours(const Interpolation& r, const ColourScale& scale) {
    std::vector<std::uint32_t> out(r.value_dbm.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::optional<double> v =
            std::isfinite(r.value_dbm[i]) ? std::optional<double>(r.value_dbm[i]) : std::nullopt;
        const auto alpha =
            static_cast<std::uint32_t>(std::lround(std::clamp(r.confidence[i], 0.0f, 1.0f) * 255.0f));
        out[i] = (alpha << 24) | (scale.colour_of(v) & 0xffffffu);
    }
    return out;
}
InterpolatedAreaStats area_stats(const Interpolation& r, const std::vector<double>& thresholds,
                                 double min_confidence) {
    InterpolatedAreaStats s;
    s.min_confidence = min_confidence;
    const double cell_area = r.grid.cell_m * r.grid.cell_m;
    s.floor_area_m2 = static_cast<double>(r.floor_cells) * cell_area;
    std::vector<std::size_t> above(thresholds.size(), 0);
    for (std::size_t i = 0; i < r.value_dbm.size(); ++i) {
        if (!(r.confidence[i] >= min_confidence) || r.confidence[i] <= 0.0f) continue;
        ++s.assessed_cells;
        for (std::size_t t = 0; t < thresholds.size(); ++t)
            if (r.value_dbm[i] >= thresholds[t]) ++above[t];
    }
    s.assessed_area_m2 = static_cast<double>(s.assessed_cells) * cell_area;
    for (std::size_t t = 0; t < thresholds.size(); ++t) {
        AreaShare a;
        a.threshold = thresholds[t];
        a.area_m2 = static_cast<double>(above[t]) * cell_area;
        if (s.assessed_cells > 0) {
            a.percent = 100.0 * static_cast<double>(above[t]) / static_cast<double>(s.assessed_cells);
            a.percent_below = 100.0 - a.percent;
        }
        s.thresholds.push_back(a);
    }
    return s;
}

AcceptanceResult evaluate_area(const AcceptanceProfile& profile, const std::vector<const Interpolation*>& floors) {
    if (profile.basis != AcceptanceProfile::Basis::FloorArea)
        throw std::invalid_argument("evaluate_area: profile '" + profile.name + "' is not on the floor-area basis");
    if (profile.unit != "dBm")
        throw std::invalid_argument("evaluate_area: the interpolated map is in dBm, not " + profile.unit);
    AcceptanceResult r;
    r.profile = profile.name;
    r.basis = profile.basis;
    std::vector<double> thresholds;
    for (const auto& c : profile.criteria) thresholds.push_back(c.threshold);
    std::vector<double> above_m2(thresholds.size(), 0.0);
    for (const Interpolation* f : floors) {
        const InterpolatedAreaStats s = area_stats(*f, thresholds, profile.min_confidence);
        r.points += f->points.size();
        r.cells += s.assessed_cells;
        r.area_m2 += s.assessed_area_m2;
        for (std::size_t t = 0; t < thresholds.size(); ++t) above_m2[t] += s.thresholds[t].area_m2;
    }
    r.pass = r.cells > 0;
    for (std::size_t t = 0; t < thresholds.size(); ++t) {
        CriterionResult c;
        c.criterion = profile.criteria[t];
        // Area-weighted over floors (grids of different cell sizes).
        c.percent = r.area_m2 > 0.0 ? 100.0 * above_m2[t] / r.area_m2 : 0.0;
        c.pass = r.cells > 0 && c.percent >= c.criterion.min_percent;
        r.pass = r.pass && c.pass;
        r.criteria.push_back(c);
    }
    r.note = kFloorAreaLabel;
    return r;
}

CoverageSummary coverage_summary(const project::Project& project, const Interpolation& r,
                                 const std::vector<double>& thresholds, double min_confidence) {
    CoverageSummary s;
    StatsOptions o;
    o.thresholds = thresholds;
    s.measured = coverage_stats(request_points(project, r.request), o);
    s.interpolated = area_stats(r, thresholds, min_confidence);
    s.stale = staleness(project, r).stale;
    return s;
}

// ---- background job ---------------------------------------------------------

struct InterpolationJob::Shared {
    mutable std::mutex mutex;
    std::optional<Interpolation> result;
};

std::unique_ptr<InterpolationJob> InterpolationJob::start(std::shared_ptr<project::Project> project,
                                                          InterpolationRequest request, Options options,
                                                          util::Job::Listener listener) {
    if (!project) throw std::invalid_argument("InterpolationJob: no project");
    std::unique_ptr<InterpolationJob> j(new InterpolationJob());
    j->shared_ = std::make_shared<Shared>();
    auto shared = j->shared_;
    j->job_ = util::Job::start(
        [project = std::move(project), request = std::move(request), options, shared](util::JobContext& ctx) {
            RunControl run;
            run.isa = options.isa;
            run.threads = options.threads;
            run.cancel = ctx.cancel_flag();
            // Shares of the progress per stage (from typical runs).
            double base = 0.0;
            double span = 0.05;
            run.progress = [&](double f) { ctx.progress(base + span * f); };
            auto stage = [&](const std::string& s) {
                ctx.stage(s);
                struct Share {
                    const char* stage;
                    double base, span;
                };
                static constexpr Share kShares[] = {{"Points", 0.0, 0.05}, {"Map", 0.05, 0.9}};
                for (const Share& x : kShares)
                    if (s == x.stage) {
                        base = x.base;
                        span = x.span;
                    }
                ctx.progress(base);
            };
            auto r = interpolate(*project, request, run, stage);
            if (!r) return;  // cancelled
            ctx.stage("Storing");
            if (!ctx.commit()) return;
            if (options.store) {
                r->id = store(*project, *r);
                if (auto back = project->interpolation(r->id)) {
                    r->created_utc = back->created_utc;
                    r->created_with = back->created_with;
                }
            }
            const std::lock_guard l(shared->mutex);
            shared->result = std::move(r);
        },
        std::move(listener));
    return j;
}

InterpolationJob::~InterpolationJob() = default;

util::JobStatus InterpolationJob::status() const { return job_->status(); }
void InterpolationJob::cancel() { job_->cancel(); }
void InterpolationJob::wait() const { job_->wait(); }

std::optional<Interpolation> InterpolationJob::result() const {
    if (job_->status().state != util::JobState::Done) return std::nullopt;
    const std::lock_guard l(shared_->mutex);
    return shared_->result;
}

}  // namespace yantra::survey
