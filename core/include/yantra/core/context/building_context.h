// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The context of the radio plugins (PLG-27, RAD-18, RAD-36) from the
// building model: the floor plan shown (plan name, bundled example id,
// floor id and name, plan size in metres and scale state, IND-08), the
// mouse and walk-test positions converted from plan units to metres in
// the floor's plan frame with the floor scale, and the scene JSON of
// every floor (core/radio/scene_export.h). It publishes snapshots on a
// ContextPublisher, which the radio host reads without waiting (OA-03).
//
// Threading: one thread (the UI thread) calls the setters; they only
// compute the snapshot and publish it. The plugins' callbacks read the
// publisher's snapshot from any thread.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/building/model.h"
#include "yantra/core/context/context.h"
#include "yantra/core/radio/scene_export.h"

namespace yantra::context {

/// The FloorPlan of floor `f` (RAD-18 get_floor_plan): scale_set only with
/// a scale and a plan size above 0 (as the scene), sizes 0 without.
FloorPlan floor_plan_of(const building::Building& b, const building::Floor& f);

/// A point on a floor in plan units.
struct FloorPoint {
    std::string floor_id;
    building::Point2 plan;
};

/// `p` as a context position in plan-frame metres (exactly plan units x
/// metres per unit); nullopt for an unknown floor or one without scale.
std::optional<PlanPosition> plan_position_of(const building::Building& b, const FloorPoint& p);

class BuildingContext {
public:
    explicit BuildingContext(std::shared_ptr<ContextPublisher> publisher,
                             radio::SceneExportOptions scene_options = {});

    /// Replaces the model (after any edit: walls, antennas, scale, floors)
    /// and re-exports every floor's scene; the shown floor stays shown
    /// while it exists, and the cursor and walk positions keep their plan
    /// points (their metres follow a new scale). Returns the ContextEvent
    /// bits published.
    std::uint32_t set_building(building::Building b);
    /// Shows floor `floor_id` (nullopt or unknown: no plan shown, NO_PLAN).
    /// The cursor is cleared (it was over the old plan).
    std::uint32_t show_floor(std::optional<std::string> floor_id);
    /// The mouse over the shown floor, in its plan units; nullopt when the
    /// mouse left the plan. Without scale there is no position (NO_POSITION).
    std::uint32_t set_cursor(std::optional<building::Point2> plan_units);
    /// The walk-test position (GEO-02) on a floor; nullopt when no walk runs.
    std::uint32_t set_walk(std::optional<FloorPoint> walk);

    const building::Building& building() const { return building_; }
    const std::optional<std::string>& shown_floor() const { return shown_; }
    /// The export warnings of each floor's scene (floor id -> warnings).
    const std::map<std::string, std::vector<std::string>>& scene_warnings() const { return warnings_; }

private:
    std::uint32_t publish();
    // A position from plan units, keeping the old time stamp when the
    // metres did not change (no CURSOR_MOVED / WALK_POSITION then).
    std::optional<PlanPosition> position(const std::optional<FloorPoint>& at,
                                         const std::optional<PlanPosition>& old) const;

    std::shared_ptr<ContextPublisher> publisher_;
    radio::SceneExportOptions options_;
    building::Building building_;
    std::optional<std::string> shown_;
    std::map<std::string, std::string> scenes_;
    std::map<std::string, std::vector<std::string>> warnings_;
    std::optional<building::Point2> cursor_plan_;  // over the shown floor
    std::optional<FloorPoint> walk_plan_;
};

}  // namespace yantra::context
