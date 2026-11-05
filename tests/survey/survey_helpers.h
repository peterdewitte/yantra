// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers of the survey tests (synthetic measurement cycles, a
// two-floor project): test_survey.cpp and test_postprocess.cpp.
#pragma once

#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/walk_test.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace ys = yantra::survey;
using nlohmann::json;

namespace survey_test {

constexpr std::int64_t kS = 1'000'000'000;
constexpr std::int64_t kUtc0 = 1'793'000'000LL * kS;

inline fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

inline std::shared_ptr<const yp::Schema> cell_schema() {
    auto s = std::make_shared<yp::Schema>();
    s->id = "test_cell";
    auto field = [&](std::string name, yp::FieldType type, bool key, yp::Category cat = yp::Category::SystemInfo) {
        yp::Field f;
        f.name = std::move(name);
        f.type = type;
        f.key = key;
        f.category = cat;
        s->fields.push_back(f);
    };
    field("frequency_hz", yp::FieldType::UInt, true);
    field("cell_id", yp::FieldType::Int, true);
    field("c1_db", yp::FieldType::Double, false, yp::Category::Level);
    field("sim_floor_id", yp::FieldType::String, false, yp::Category::PlanPosition);
    field("sim_x_m", yp::FieldType::Double, false, yp::Category::PlanPosition);
    field("sim_y_m", yp::FieldType::Double, false, yp::Category::PlanPosition);
    return s;
}

struct Carrier {
    std::uint64_t hz = 390'000'000;
    double level = -70.0;
    ym::CarrierRole role = ym::CarrierRole::Serving;
    std::int64_t cell = 1;
    std::optional<double> c1;
    std::string sim_floor;
    double sim_x = 0, sim_y = 0;
    std::uint32_t flags = 0;
    std::uint64_t entry = 0;
    Carrier& w_entry(std::uint64_t v) { return entry = v, *this; }
    Carrier& w_hz(std::uint64_t v) { return hz = v, *this; }
    Carrier& w_level(double v) { return level = v, *this; }
    Carrier& w_role(ym::CarrierRole v) { return role = v, *this; }
    Carrier& w_cell(std::int64_t v) { return cell = v, *this; }
    Carrier& w_c1(double v) { return c1 = v, *this; }
    Carrier& w_sim_floor(const std::string& v) { return sim_floor = v, *this; }
    Carrier& w_sim_x(double v) { return sim_x = v, *this; }
    Carrier& w_sim_y(double v) { return sim_y = v, *this; }
    Carrier& w_flags(std::uint32_t v) { return flags = v, *this; }
};

// A metric built field by field.
struct MB {
    ys::Metric m;
    MB& w_carrier(ys::Metric::Carrier v) { return m.carrier = v, *this; }
    MB& w_value(ys::Metric::Value v) { return m.value = v, *this; }
    MB& w_frequency_hz(std::uint64_t v) { return m.frequency_hz = v, *this; }
    MB& w_quality_field(const std::string& v) { return m.quality_field = v, *this; }
    operator ys::Metric() const { return m; }  // NOLINT(google-explicit-constructor)
};

inline ym::MeasurementCycle cycle(std::int64_t t, const std::vector<Carrier>& carriers) {
    static const auto schema = cell_schema();
    ym::MeasurementCycle c;
    c.sequence = static_cast<std::uint64_t>(t / kS);
    c.time.monotonic_ns = t;
    c.time.utc_ns = kUtc0 + t;
    c.period_ns = kS;
    for (const Carrier& k : carriers) {
        ym::CarrierSample s;
        ym::MappedRecord& m = s.latest;
        m.provenance = {"test", "test.tech", "1.0.0", "test_cell", 1};
        m.radio_plugin_id = "fake";
        m.radio_plugin_version = "0.1.0";
        m.device_key = "fake:0";
        m.schema = schema;
        m.role = k.role;
        m.frequency_hz = k.hz;
        m.level = k.level;
        m.record.schema_id = "test_cell";
        m.record.values = {yp::Value{k.hz}, yp::Value{k.cell},
                           k.c1 ? yp::Value{*k.c1} : yp::Value{},
                           k.sim_floor.empty() ? yp::Value{} : yp::Value{k.sim_floor},
                           k.sim_floor.empty() ? yp::Value{} : yp::Value{k.sim_x},
                           k.sim_floor.empty() ? yp::Value{} : yp::Value{k.sim_y}};
        if (k.c1) m.c1_source = ym::C1Source::Radio;
        m.flags = k.flags;
        m.entry_index = k.entry;
        s.flags = k.flags;
        s.age_ns = 200'000'000;
        c.samples.push_back(s);
    }
    return c;
}

inline ym::MeasurementCycle level_cycle(std::int64_t t, double level) { return cycle(t, {Carrier{}.w_level(level)}); }

// A project with a 20 x 10 m ground floor (400 x 200 units, 0.05 m per
// unit) and a first floor of the same size.
struct Fixture {
    std::shared_ptr<pj::Project> p;
    std::string ground, first;
    explicit Fixture(const std::string& name) {
        const fs::path dir = scratch(name);
        p = pj::Project::create(dir / "s.yantra", pj::ProjectInfo{"Survey", "", "", ""});
        for (int level : {0, 1}) {
            bm::Floor f;
            f.name = level == 0 ? "Ground floor" : "First floor";
            f.level = level;
            f.plan_width = 400;
            f.plan_height = 200;
            (level == 0 ? ground : first) = p->add_floor(f);
        }
        p->set_scale(ground, *bm::factor_scale(0.05));
        p->set_scale(first, *bm::factor_scale(0.05));
    }
    std::shared_ptr<ys::WalkTest> walk(const std::string& kind = "walk", ys::WalkMode mode = ys::WalkMode::PointAndClick,
                                       std::vector<std::optional<yantra::context::FloorPoint>>* walker = nullptr) {
        ys::WalkTestOptions o;
        o.project = p;
        o.floor_id = ground;
        o.kind = kind;
        o.mode = mode;
        o.name = "Walk";
        o.calibration = R"({"key":"fake|0|"})";
        o.notes = "test";
        if (walker != nullptr)
            o.walker = [walker](const std::optional<yantra::context::FloorPoint>& at) { walker->push_back(at); };
        return std::make_shared<ys::WalkTest>(o);
    }
};

inline std::vector<bm::Point2> plans(const std::vector<pj::StoredPoint>& pts) {
    std::vector<bm::Point2> out;
    for (const auto& p : pts) out.push_back(p.plan);
    return out;
}

inline ys::SurveyPoint valued(double v, double x = 0, double y = 0, std::string cell = "cell_id=1", std::int64_t session = 1) {
    ys::SurveyPoint p;
    p.session_id = session;
    p.plan = {x, y};
    p.value = v;
    p.unit = "dBm";
    p.serving_cell = std::move(cell);
    return p;
}

}  // namespace survey_test
using namespace survey_test;
