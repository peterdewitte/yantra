// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Context snapshots from the building model (PLG-27, RAD-18, RAD-36,
// IND-08) and the fixed demo scenes (DEMO-05), see building_context.h.

#include "yantra/core/context/building_context.h"

#include <utility>

#include <nlohmann/json.hpp>

#include "yantra/core/demo/demo_project.h"

namespace yantra::context {

using building::Building;
using building::Floor;

FloorPlan floor_plan_of(const Building& b, const Floor& f) {
    FloorPlan p;
    p.plan_name = building::plan_name(b, f);
    p.example_id = f.example_id;
    p.floor_id = f.id;
    p.floor_name = f.name;
    const auto size = b.plan_size_m(f);
    p.scale_set = size.has_value() && f.plan_width > 0.0 && f.plan_height > 0.0;
    if (p.scale_set) {
        p.width_m = size->x;
        p.height_m = size->y;
    }
    return p;
}

std::optional<PlanPosition> plan_position_of(const Building& b, const FloorPoint& p) {
    const Floor* f = b.find_floor(p.floor_id);
    if (f == nullptr) return std::nullopt;
    const auto m = b.to_plan_metres(*f, p.plan);
    if (!m) return std::nullopt;
    PlanPosition pos;
    pos.floor_id = f->id;
    pos.floor_name = f->name;
    pos.x_m = m->x;
    pos.y_m = m->y;
    return pos;
}

SceneSource demo_scenes(std::vector<std::filesystem::path> folders) {
    // example id -> file text (nullopt: none); the UI thread only.
    auto cache = std::make_shared<std::map<std::string, std::optional<std::string>>>();
    return [folders = std::move(folders), cache](const Building&, const Floor& f) -> std::optional<std::string> {
        if (f.example_id.empty()) return std::nullopt;
        auto it = cache->find(f.example_id);
        if (it == cache->end()) {
            std::optional<std::string> text;
            for (const auto& folder : folders)
                if ((text = demo::read_demo_scene(folder, f.example_id))) break;
            it = cache->emplace(f.example_id, std::move(text)).first;
        }
        if (!it->second) return std::nullopt;
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(*it->second);
        } catch (const nlohmann::json::exception&) {
            return it->second;  // served as it is; the plugin refuses it (RAD-36)
        }
        if (!j.is_object() || !j.contains("floor") || !j["floor"].is_object()) return it->second;
        nlohmann::json& fl = j["floor"];
        if (fl.value("id", "") == f.id) return it->second;
        fl["id"] = f.id;
        return j.dump();
    };
}

BuildingContext::BuildingContext(std::shared_ptr<ContextPublisher> publisher, SceneSource scenes)
    : publisher_(std::move(publisher)), source_(std::move(scenes)) {}

std::optional<PlanPosition> BuildingContext::position(const std::optional<FloorPoint>& at,
                                                      const std::optional<PlanPosition>& old) const {
    if (!at) return std::nullopt;
    auto pos = plan_position_of(building_, *at);
    if (pos && old && old->floor_id == pos->floor_id && old->floor_name == pos->floor_name &&
        old->x_m == pos->x_m && old->y_m == pos->y_m)
        pos->moved_monotonic_ns = old->moved_monotonic_ns;
    return pos;
}

std::uint32_t BuildingContext::set_building(Building b) {
    building_ = std::move(b);
    scenes_.clear();
    if (source_)
        for (const Floor& f : building_.floors)
            if (auto scene = source_(building_, f)) scenes_[f.id] = std::move(*scene);
    if (shown_ && building_.find_floor(*shown_) == nullptr) {
        shown_.reset();
        cursor_plan_.reset();
    }
    if (walk_plan_ && building_.find_floor(walk_plan_->floor_id) == nullptr) walk_plan_.reset();
    return publish();
}

std::uint32_t BuildingContext::show_floor(std::optional<std::string> floor_id) {
    if (floor_id && building_.find_floor(*floor_id) == nullptr) floor_id.reset();
    shown_ = std::move(floor_id);
    cursor_plan_.reset();
    return publish();
}

std::uint32_t BuildingContext::set_cursor(std::optional<building::Point2> plan_units) {
    cursor_plan_ = shown_ ? plan_units : std::nullopt;
    const std::optional<FloorPoint> at =
        cursor_plan_ ? std::optional<FloorPoint>(FloorPoint{*shown_, *cursor_plan_}) : std::nullopt;
    return publisher_->set_cursor(position(at, std::nullopt));
}

std::uint32_t BuildingContext::set_walk(std::optional<FloorPoint> walk) {
    walk_plan_ = std::move(walk);
    return publisher_->set_walk(position(walk_plan_, std::nullopt));
}

std::uint32_t BuildingContext::publish() {
    const auto before = publisher_->snapshot();
    Snapshot s;
    if (shown_) s.plan = floor_plan_of(building_, *building_.find_floor(*shown_));
    const std::optional<FloorPoint> at =
        cursor_plan_ && shown_ ? std::optional<FloorPoint>(FloorPoint{*shown_, *cursor_plan_}) : std::nullopt;
    s.cursor = position(at, before->cursor);
    s.walk = position(walk_plan_, before->walk);
    s.scenes = scenes_;
    return publisher_->publish(std::move(s));
}

}  // namespace yantra::context
