// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolated coverage map (IND-41..45, EXP-06); see interpolation.h
// and docs/PROPAGATION.md "Interpolation of a floor".
//
// Copied from yantra-v2 68ea20a: core/src/survey/interpolation.cpp, adapted
// (survey::Metric, plan-frame metres, per-cell carriers and the group of
// the metric's carrier, the floor outline, the floor-area acceptance
// basis, a shared project in the job).

#include "yantra/core/survey/interpolation.h"

#include "yantra/core/building/model_json.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/util/sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace yantra::survey {

using json = nlohmann::json;
using project::ProjectError;
using Code = ProjectError::Code;

namespace {

constexpr int kFormat = 1;  // version of the parameters/result JSON

// ---- JSON of the parameters and results ---------------------------------

json bounds_json(const raytrace::Bounds& b) { return json::array({b.lo, b.hi}); }
raytrace::Bounds bounds_of(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>()}; }

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

constexpr std::pair<raytrace::EirpFit, const char*> kEirp[] = {
    {raytrace::EirpFit::Unknown, "unknown"}, {raytrace::EirpFit::All, "all"}, {raytrace::EirpFit::None, "none"}};
constexpr std::pair<Metric::Carrier, const char*> kCarrier[] = {
    {Metric::Carrier::Serving, "serving"},
    {Metric::Carrier::Strongest, "strongest"},
    {Metric::Carrier::Frequency, "frequency"}};
constexpr std::pair<Metric::Value, const char*> kValue[] = {
    {Metric::Value::Level, "level"}, {Metric::Value::C1, "c1"}, {Metric::Value::Quality, "quality"}};
constexpr std::pair<raytrace::FitParameter::Kind, const char*> kKind[] = {
    {raytrace::FitParameter::Kind::Eirp, "eirp"},
    {raytrace::FitParameter::Kind::Exponent, "exponent"},
    {raytrace::FitParameter::Kind::WallClass, "wall_class"},
    {raytrace::FitParameter::Kind::FloorLoss, "floor_loss"}};

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

json to_json(const raytrace::FitOptions& o) {
    json wc = json::object();
    for (const auto& [id, b] : o.wall_class_bounds) wc[id] = bounds_json(b);
    json j{{"eirp", name_of(o.eirp, kEirp)},
           {"eirp_dbm", bounds_json(o.eirp_dbm)},
           {"fit_exponent", o.fit_exponent},
           {"exponent", bounds_json(o.exponent)},
           {"fit_wall_classes", o.fit_wall_classes},
           {"wall_class_bounds", wc},
           {"fit_floor_loss", o.fit_floor_loss},
           {"floor_loss_db", bounds_json(o.floor_loss_db)},
           {"max_iterations", o.max_iterations},
           {"group_key", nullptr}};
    if (o.group_key) j["group_key"] = *o.group_key;
    return j;
}
raytrace::FitOptions fit_options_of(const json& j) {
    raytrace::FitOptions o;
    o.eirp = value_of(j.at("eirp").get<std::string>(), kEirp, "eirp");
    o.eirp_dbm = bounds_of(j.at("eirp_dbm"));
    o.fit_exponent = j.at("fit_exponent").get<bool>();
    o.exponent = bounds_of(j.at("exponent"));
    o.fit_wall_classes = j.at("fit_wall_classes").get<bool>();
    for (const auto& [id, b] : j.at("wall_class_bounds").items()) o.wall_class_bounds[id] = bounds_of(b);
    o.fit_floor_loss = j.at("fit_floor_loss").get<bool>();
    o.floor_loss_db = bounds_of(j.at("floor_loss_db"));
    o.max_iterations = j.at("max_iterations").get<int>();
    if (!j.at("group_key").is_null()) o.group_key = j.at("group_key").get<std::string>();
    return o;
}

json to_json(const raytrace::ResidualOptions& o) {
    return {{"radius_m", o.radius_m},
            {"power", o.power},
            {"walls_limit", o.walls_limit},
            {"full_confidence_m", o.full_confidence_m},
            {"zero_confidence_m", o.zero_confidence_m}};
}
raytrace::ResidualOptions residual_options_of(const json& j) {
    raytrace::ResidualOptions o;
    o.radius_m = j.at("radius_m").get<double>();
    o.power = j.at("power").get<double>();
    o.walls_limit = j.at("walls_limit").get<bool>();
    o.full_confidence_m = j.at("full_confidence_m").get<double>();
    o.zero_confidence_m = j.at("zero_confidence_m").get<double>();
    return o;
}

json to_json(const raytrace::SourceSearchOptions& o) {
    return {{"max_sources", o.max_sources},           {"min_improvement_db", o.min_improvement_db},
            {"step_m", o.step_m},                     {"height_m", o.height_m},
            {"eirp_dbm", bounds_json(o.eirp_dbm)}, {"one_cell", o.one_cell}};
}
raytrace::SourceSearchOptions source_options_of(const json& j) {
    raytrace::SourceSearchOptions o;
    o.max_sources = j.at("max_sources").get<int>();
    o.min_improvement_db = j.at("min_improvement_db").get<double>();
    o.step_m = j.at("step_m").get<double>();
    o.height_m = j.at("height_m").get<double>();
    o.eirp_dbm = bounds_of(j.at("eirp_dbm"));
    o.one_cell = j.at("one_cell").get<bool>();
    return o;
}

json to_json(const InterpolationRequest& r) {
    json j{{"floor_id", r.floor_id},
           {"metric", to_json(r.metric)},
           {"sessions", r.sessions},
           {"frequency_hz", nullptr},
           {"grid_m", nullptr},
           {"fit", to_json(r.fit)},
           {"residuals", to_json(r.residuals)},
           {"sources", to_json(r.sources)}};
    if (r.frequency_hz) j["frequency_hz"] = *r.frequency_hz;
    if (r.grid_m) j["grid_m"] = *r.grid_m;
    return j;
}
InterpolationRequest request_of(const json& j) {
    InterpolationRequest r;
    r.floor_id = j.at("floor_id").get<std::string>();
    r.metric = metric_of(j.at("metric"));
    r.sessions = j.at("sessions").get<std::vector<std::int64_t>>();
    if (!j.at("frequency_hz").is_null()) r.frequency_hz = j.at("frequency_hz").get<double>();
    if (!j.at("grid_m").is_null()) r.grid_m = j.at("grid_m").get<double>();
    r.fit = fit_options_of(j.at("fit"));
    r.residuals = residual_options_of(j.at("residuals"));
    r.sources = source_options_of(j.at("sources"));
    return r;
}

json to_json(const raytrace::ModelParams& p) {
    json j{{"frequency_hz", nullptr},
           {"grid_m", p.grid_m},
           {"max_reflections", p.max_reflections},
           {"rx_height_m", p.rx_height_m},
           {"other_floors", p.other_floors},
           {"path_loss_exponent", p.path_loss_exponent},
           {"reference_distance_m", p.reference_distance_m},
           {"reflection_loss_db", p.reflection_loss_db},
           {"floor_loss_db", nullptr},
           {"wall_class_loss_db", p.wall_class_loss_db},
           {"antenna_eirp_dbm", p.antenna_eirp_dbm},
           {"default_eirp_dbm", p.default_eirp_dbm}};
    if (p.frequency_hz) j["frequency_hz"] = *p.frequency_hz;
    if (p.floor_loss_db) j["floor_loss_db"] = *p.floor_loss_db;
    return j;
}
raytrace::ModelParams params_of(const json& j) {
    raytrace::ModelParams p;
    if (!j.at("frequency_hz").is_null()) p.frequency_hz = j.at("frequency_hz").get<double>();
    p.grid_m = j.at("grid_m").get<double>();
    p.max_reflections = j.at("max_reflections").get<int>();
    p.rx_height_m = j.at("rx_height_m").get<double>();
    p.other_floors = j.at("other_floors").get<bool>();
    p.path_loss_exponent = j.at("path_loss_exponent").get<double>();
    p.reference_distance_m = j.at("reference_distance_m").get<double>();
    p.reflection_loss_db = j.at("reflection_loss_db").get<double>();
    if (!j.at("floor_loss_db").is_null()) p.floor_loss_db = j.at("floor_loss_db").get<double>();
    p.wall_class_loss_db = j.at("wall_class_loss_db").get<std::map<std::string, double>>();
    p.antenna_eirp_dbm = j.at("antenna_eirp_dbm").get<std::map<std::string, double>>();
    p.default_eirp_dbm = j.at("default_eirp_dbm").get<double>();
    return p;
}

json result_json(const Interpolation& r) {
    json fitted = json::array();
    for (const auto& f : r.fitted)
        fitted.push_back({{"kind", name_of(f.kind, kKind)},
                          {"id", f.id},
                          {"start", f.start},
                          {"value", f.value},
                          {"bounds", bounds_json(f.bounds)},
                          {"at_bound", f.at_bound}});
    json sources = json::array();
    for (const auto& a : r.sources)
        sources.push_back({{"id", a.id},
                           {"name", a.name},
                           {"x", a.position.x},
                           {"y", a.position.y},
                           {"height_m", a.height_m},
                           {"eirp_dbm", a.eirp_dbm ? json(*a.eirp_dbm) : json(nullptr)},
                           {"cell", a.cell}});
    json points = json::array();
    for (const auto& p : r.points)
        points.push_back(json::array({p.session_id, p.sequence, p.position_m.x, p.position_m.y, p.measured_dbm,
                                      p.model_dbm, p.residual_db}));
    return {{"format", kFormat},
            {"label", r.label()},
            {"group_key", r.group_key},
            {"quality",
             {{"points", r.quality.points},
              {"rms_db", r.quality.rms_db},
              {"mean_db", r.quality.mean_db},
              {"max_abs_db", r.quality.max_abs_db},
              {"start_rms_db", r.quality.start_rms_db}}},
            {"fitted", fitted},
            {"not_fitted", r.not_fitted},
            {"iterations", r.iterations},
            {"stop_reason", r.stop_reason},
            {"estimated_sources", r.estimated_sources},
            {"sources", sources},
            {"floor_cells", r.floor_cells},
            {"points", points}};
}

void read_result(const json& j, Interpolation& r) {
    if (j.at("format").get<int>() != kFormat) throw std::invalid_argument("unknown result format");
    r.group_key = j.at("group_key").get<std::string>();
    const json& q = j.at("quality");
    r.quality.points = q.at("points").get<std::size_t>();
    r.quality.rms_db = q.at("rms_db").get<double>();
    r.quality.mean_db = q.at("mean_db").get<double>();
    r.quality.max_abs_db = q.at("max_abs_db").get<double>();
    r.quality.start_rms_db = q.at("start_rms_db").get<double>();
    for (const json& f : j.at("fitted"))
        r.fitted.push_back({value_of(f.at("kind").get<std::string>(), kKind, "parameter kind"),
                            f.at("id").get<std::string>(), f.at("start").get<double>(), f.at("value").get<double>(),
                            bounds_of(f.at("bounds")), f.at("at_bound").get<bool>()});
    r.not_fitted = j.at("not_fitted").get<std::vector<std::string>>();
    r.iterations = j.at("iterations").get<int>();
    r.stop_reason = j.at("stop_reason").get<std::string>();
    r.estimated_sources = j.at("estimated_sources").get<bool>();
    for (const json& s : j.at("sources")) {
        building::Antenna a;
        a.id = s.at("id").get<std::string>();
        a.name = s.at("name").get<std::string>();
        a.model = "estimated";
        a.position = {s.at("x").get<double>(), s.at("y").get<double>()};
        a.height_m = s.at("height_m").get<double>();
        if (!s.at("eirp_dbm").is_null()) a.eirp_dbm = s.at("eirp_dbm").get<double>();
        a.cell = s.at("cell").get<std::string>();
        a.gain_dbi = 0.0;
        r.sources.push_back(a);
    }
    r.floor_cells = j.at("floor_cells").get<std::size_t>();
    for (const json& p : j.at("points"))
        r.points.push_back({p.at(0).get<std::int64_t>(), p.at(1).get<std::int64_t>(),
                            {p.at(2).get<double>(), p.at(3).get<double>()}, p.at(4).get<double>(),
                            p.at(5).get<double>(), p.at(6).get<double>()});
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

json settings_json(const project::PropagationSettings& s) {
    return {{"grid_m", s.grid_m},
            {"max_reflections", s.max_reflections},
            {"path_loss_exponent", s.path_loss_exponent},
            {"reference_distance_m", s.reference_distance_m},
            {"reflection_loss_db", s.reflection_loss_db},
            {"rx_height_m", s.rx_height_m},
            {"other_floors", s.other_floors},
            {"default_eirp_dbm", s.default_eirp_dbm}};
}

std::string settings_fingerprint(const project::PropagationSettings& s, const InterpolationRequest& r) {
    const json j{{"propagation", settings_json(s)}, {"request", to_json(r)}};
    util::Sha256 h;
    h.update("yantra-interpolation-settings 1\n" + j.dump());
    return h.finish_hex();
}

void check_request(const InterpolationRequest& r) {
    if (r.metric.value != Metric::Value::Level)
        throw ProjectError(Code::Invalid, "interpolation: the metric must be a level");
    std::vector<std::string> problems = r.fit.validate();
    for (auto& x : r.residuals.validate()) problems.push_back(x);
    for (auto& x : r.sources.validate()) problems.push_back(x);
    if (r.grid_m && !(std::isfinite(*r.grid_m) && *r.grid_m >= 0.05 && *r.grid_m <= 10.0))
        problems.push_back("grid_m: outside 0.05 .. 10");
    if (r.frequency_hz && !(std::isfinite(*r.frequency_hz) && *r.frequency_hz > 0.0))
        problems.push_back("frequency_hz: must be > 0");
    if (!problems.empty()) throw ProjectError(Code::Invalid, "interpolation: " + problems.front());
}

/// The antennas that reach the floor in the model (on the floor, or on
/// another scaled floor with other_floors), not outdoor donor directions.
std::vector<const building::Antenna*> reaching(const building::Building& b, const std::string& floor_id,
                                               bool other_floors) {
    std::vector<const building::Antenna*> out;
    for (const auto& f : b.floors) {
        if (!b.has_scale(f) || (f.id != floor_id && !other_floors)) continue;
        for (const auto& a : f.antennas)
            if (a.type != building::AntennaType::OutdoorDonor) out.push_back(&a);
    }
    return out;
}

/// A free cell id for the estimated sources (kEstimatedCellId unless used).
std::string free_cell_id(const building::Building& b) {
    std::string id = raytrace::kEstimatedCellId;
    for (int n = 2; b.find_cell(id) != nullptr; ++n) id = std::string(raytrace::kEstimatedCellId) + "-" + std::to_string(n);
    return id;
}

}  // namespace

raytrace::ModelParams model_params(const project::PropagationSettings& s) {
    raytrace::ModelParams p;
    p.grid_m = s.grid_m;
    p.max_reflections = s.max_reflections;
    p.path_loss_exponent = s.path_loss_exponent;
    p.reference_distance_m = s.reference_distance_m;
    p.reflection_loss_db = s.reflection_loss_db;
    p.rx_height_m = s.rx_height_m;
    p.other_floors = s.other_floors;
    p.default_eirp_dbm = s.default_eirp_dbm;
    return p;
}

std::string InterpolationRequest::key() const {
    std::string k = std::string(name_of(metric.value, kValue)) + "|" + name_of(metric.carrier, kCarrier);
    if (metric.carrier == Metric::Carrier::Frequency) k += ":" + std::to_string(metric.frequency_hz);
    if (!metric.device.empty()) k += "|device:" + metric.device;
    k += "|" + (fit.group_key ? *fit.group_key : std::string("auto"));
    return k;
}

std::string Interpolation::label() const {
    return estimated_sources ? std::string(kInterpolatedLabel) + ", " + kEstimatedSourcesLabel
                             : std::string(kInterpolatedLabel);
}

std::vector<bool> floor_cells(const building::Building& b, const building::Floor& f, const raytrace::Grid& grid) {
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
            settings_fingerprint(project.propagation(), request)};
}

std::optional<Interpolation> interpolate(const project::Project& project, const InterpolationRequest& request,
                                         const raytrace::RunControl& run,
                                         const std::function<void(const std::string&)>& stage) {
    check_request(request);
    auto tell = [&](const char* s) {
        if (stage) stage(s);
    };
    auto cancelled = [&] { return run.cancel != nullptr && run.cancel->load(); };
    tell("Points");
    building::Building b = project.building();
    const building::Floor* bf = b.find_floor(request.floor_id);
    if (bf == nullptr) throw ProjectError(Code::NotFound, "no floor " + request.floor_id);
    if (!b.has_scale(*bf))
        throw ProjectError(Code::Invalid, "interpolation: floor " + request.floor_id + " has no scale");
    const std::size_t floor_index = static_cast<std::size_t>(bf - b.floors.data());
    const project::PropagationSettings settings = project.propagation();
    const PointList list = request_points(project, request);
    const Measured m = measured_points(list);
    if (m.points.empty()) throw ProjectError(Code::Invalid, "interpolation: no measured point with a level in dBm");

    Interpolation out;
    out.request = request;
    out.building_sha256 = building_fingerprint(b);
    out.points_sha256 = points_fingerprint(request, m);
    out.settings_sha256 = settings_fingerprint(settings, request);

    // The measured carrier: the request's, the metric's, else the lower
    // median of the points' carriers (a measured carrier).
    std::optional<double> freq = request.frequency_hz;
    if (!freq && request.metric.carrier == Metric::Carrier::Frequency && request.metric.frequency_hz > 0)
        freq = static_cast<double>(request.metric.frequency_hz);
    if (!freq) {
        std::vector<std::uint64_t> f;
        for (const SurveyPoint* p : m.points)
            if (p->frequency_hz > 0) f.push_back(p->frequency_hz);
        if (!f.empty()) {
            std::sort(f.begin(), f.end());
            freq = static_cast<double>(f[(f.size() - 1) / 2]);
        }
    }
    raytrace::ModelParams p0 = model_params(settings);
    if (request.grid_m) p0.grid_m = *request.grid_m;
    if (request.frequency_hz) p0.frequency_hz = *request.frequency_hz;
    const auto sources = reaching(b, request.floor_id, p0.other_floors);
    // Sources whose cell has no carrier need the measured one.
    if (!p0.frequency_hz)
        for (const building::Antenna* a : sources) {
            const building::Cell* c = a->cell.empty() ? nullptr : b.find_cell(a->cell);
            if (c == nullptr || c->carrier_hz == 0) {
                if (!freq)
                    throw ProjectError(Code::Invalid, "interpolation: antenna " + a->id +
                                                          " has no carrier and the points give none");
                p0.frequency_hz = *freq;
                break;
            }
        }
    out.frequency_hz = freq.value_or(0.0);
    out.start_params = p0;

    raytrace::FitOptions fit_options = request.fit;
    // IND-42: no antenna reaches the floor - estimate the sources.
    if (sources.empty()) {
        tell("Sources");
        if (!freq) throw ProjectError(Code::Invalid, "interpolation: no carrier frequency for estimated sources");
        raytrace::ModelParams sp = p0;
        sp.frequency_hz = *freq;
        auto est = raytrace::estimate_sources(b, request.floor_id, m.metres, m.dbm, sp, request.sources, run);
        if (est.cancelled || cancelled()) return std::nullopt;
        if (est.antennas.empty())
            throw ProjectError(Code::Invalid, "interpolation: no source could be estimated from the points");
        if (est.cell) {
            const std::string id = free_cell_id(b);
            est.cell->id = id;
            for (auto& a : est.antennas) a.cell = id;
        } else {
            p0.frequency_hz = *freq;  // sources without a cell
        }
        raytrace::add_estimated_sources(b, request.floor_id, est);
        bf = &b.floors[floor_index];
        out.estimated_sources = true;
        out.sources = est.antennas;
        fit_options.eirp = raytrace::EirpFit::All;
        fit_options.group_key.reset();  // the estimated sources' own group(s)
    } else if (!fit_options.group_key && request.metric.carrier == Metric::Carrier::Frequency) {
        // The cell on the metric's carrier, when an antenna serves it.
        for (const building::Antenna* a : sources) {
            const building::Cell* c = a->cell.empty() ? nullptr : b.find_cell(a->cell);
            if (c != nullptr && c->carrier_hz == request.metric.frequency_hz) {
                fit_options.group_key = "cell:" + c->id;
                break;
            }
        }
    }
    out.group_key = fit_options.group_key.value_or("");

    tell("Fit");
    raytrace::RunControl sub = run;
    sub.progress = nullptr;
    const auto rays = raytrace::trace_points(b, request.floor_id, m.metres, p0, sub);
    if (rays.cancelled || cancelled()) return std::nullopt;
    const auto fit = raytrace::fit_model(b, rays, m.dbm, p0, fit_options, run.cancel);
    if (fit.cancelled || cancelled()) return std::nullopt;
    out.params = fit.params;
    out.quality = fit.quality;
    out.fitted = fit.parameters;
    out.not_fitted = fit.not_fitted;
    out.iterations = fit.iterations;
    out.stop_reason = fit.stop_reason;
    if (out.estimated_sources)
        for (auto& a : out.sources) a.eirp_dbm = fit.params.antenna_eirp_dbm.at(a.id);

    tell("Map");
    const auto map = raytrace::compute_map(b, request.floor_id, fit.params, run);
    if (map.cancelled || cancelled()) return std::nullopt;
    int group = -1;
    if (fit_options.group_key)
        for (std::size_t g = 0; g < map.groups.size(); ++g)
            if (map.groups[g].key == *fit_options.group_key) group = static_cast<int>(g);
    const auto& model = group >= 0 ? map.level_dbm[static_cast<std::size_t>(group)] : map.best_dbm;

    tell("Residuals");
    std::vector<raytrace::Residual> res;
    for (std::size_t i = 0; i < m.points.size(); ++i) {
        if (!fit.used[i]) continue;
        const SurveyPoint& sp = *m.points[i];
        const double resid = m.dbm[i] - fit.model_dbm[i];
        out.points.push_back({sp.session_id, sp.sequence, m.metres[i], m.dbm[i], fit.model_dbm[i], resid});
        res.push_back({m.metres[i], resid});
    }
    const auto field = raytrace::interpolate_residuals(b, request.floor_id, map.grid, res, request.residuals, sub);
    if (field.cancelled || cancelled()) return std::nullopt;

    out.grid = map.grid;
    const std::vector<bool> on_floor = floor_cells(b, *bf, map.grid);
    const std::size_t n = map.grid.size();
    out.value_dbm.resize(n);
    out.model_dbm.resize(n);
    out.confidence.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.model_dbm[i] = static_cast<float>(model[i]);
        out.value_dbm[i] = std::isfinite(model[i]) ? static_cast<float>(model[i] + field.correction_db[i])
                                                   : -std::numeric_limits<float>::infinity();
        out.confidence[i] = on_floor[i] ? field.confidence[i] : 0.0f;
        out.floor_cells += on_floor[i] ? 1 : 0;
    }
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
    s.parameters_json = json{{"format", kFormat},
                             {"request", to_json(r.request)},
                             {"frequency_hz", r.frequency_hz},
                             {"start_params", to_json(r.start_params)},
                             {"params", to_json(r.params)}}
                            .dump();
    s.result_json = result_json(r).dump();
    s.origin_m = r.grid.origin_m;
    s.cell_m = r.grid.cell_m;
    s.cols = r.grid.cols;
    s.rows = r.grid.rows;
    s.cells.reserve(r.grid.size() * 12);
    for (std::size_t i = 0; i < r.grid.size(); ++i) {
        put_f32(s.cells, r.value_dbm.at(i));
        put_f32(s.cells, r.model_dbm.at(i));
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
        r.frequency_hz = pj.at("frequency_hz").get<double>();
        r.start_params = params_of(pj.at("start_params"));
        r.params = params_of(pj.at("params"));
        read_result(json::parse(s.result_json), r);
    } catch (const std::exception& e) {
        throw ProjectError(Code::Damaged, "interpolation " + std::to_string(s.id) + ": " + e.what());
    }
    if (s.cols <= 0 || s.rows <= 0 || !(s.cell_m > 0.0) ||
        s.cells.size() != static_cast<std::size_t>(s.cols) * static_cast<std::size_t>(s.rows) * 12u)
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
    r.model_dbm.resize(n);
    r.confidence.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* c = s.cells.data() + i * 12;
        r.value_dbm[i] = get_f32(c);
        r.model_dbm[i] = get_f32(c + 4);
        r.confidence[i] = get_f32(c + 8);
    }
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
    if (now.building != r.building_sha256)
        out.reasons.emplace_back("walls, antennas, cells, wall classes or scales changed");
    if (now.points != r.points_sha256) out.reasons.emplace_back("measured points changed");
    if (now.settings != r.settings_sha256) out.reasons.emplace_back("propagation settings or parameters changed");
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
    out.model_dbm = r.model_dbm[*cell];
    out.confidence = r.confidence[*cell];
    if (out.nearest && best <= r.grid.cell_m / 2.0) {
        out.measured = true;
        out.label = "measured";
        out.value_dbm = out.nearest->measured_dbm;
    } else if (std::isfinite(r.value_dbm[*cell])) {
        out.value_dbm = r.value_dbm[*cell];
    }
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
    s.estimated_sources = r.estimated_sources;
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
    s.fit = r.quality;
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
            raytrace::RunControl run;
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
                static constexpr Share kShares[] = {{"Points", 0.0, 0.05}, {"Sources", 0.05, 0.4},
                                                    {"Fit", 0.45, 0.05},   {"Map", 0.5, 0.35},
                                                    {"Residuals", 0.85, 0.1}};
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
