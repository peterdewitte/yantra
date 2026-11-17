// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the user is looking at, as snapshots for the radio plugins' context
// callbacks (PLG-27, RAD-18, RAD-19, OA-03): the floor plan shown (name,
// bundled example id, floor id and name, plan size and scale state), the
// mouse position over it in metres in the floor's plan frame, the
// walk-test position, and the scene JSON (RAD-36) of each floor. Plan
// coordinates only, never geographic (spec 1.2).
//
// The UI thread publishes a new snapshot whenever something changes
// (ContextPublisher::publish / set_cursor / set_walk); readers on any
// thread take the current snapshot without waiting for the UI (a
// shared_ptr copy under a short lock). Subscribers learn what changed as
// ContextEvent bits; they are called on the publishing thread and must
// return at once (the radio host only queues them for its notifier
// thread). The project and building model of later phases (IND-08,
// IND-09, GEO-02) feeds a ContextPublisher; the tests feed one by hand.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace yantra::context {

// The floor plan shown in the project window or walk-test view.
struct FloorPlan {
    std::string plan_name;   // e.g. "Fraunces Tavern, first floor"; never into records (RAD-19)
    std::string example_id;  // bundled example plan id (DEMO-05), "" for an own plan
    std::string floor_id;
    std::string floor_name;
    bool scale_set = false;  // IND-08; without scale there are no positions
    double width_m = 0.0;    // plan size in metres (0 without scale)
    double height_m = 0.0;
    bool operator==(const FloorPlan&) const = default;
};

// A position on a floor plan, metres in the floor's plan frame.
struct PlanPosition {
    std::string floor_id;
    std::string floor_name;
    double x_m = 0.0;
    double y_m = 0.0;
    std::int64_t moved_monotonic_ns = 0;  // when it last changed (platform/clock.h)
    bool operator==(const PlanPosition&) const = default;
};

struct Snapshot {
    std::optional<FloorPlan> plan;        // nullopt: no plan shown
    std::optional<PlanPosition> cursor;   // nullopt: mouse not over the plan
    std::optional<PlanPosition> walk;     // nullopt: no walk runs
    std::map<std::string, std::string> scenes;  // floor id -> scene JSON (RAD-36)
};

// What changed between two snapshots (bits; values as RAD-18's events).
enum ContextEvent : std::uint32_t {
    kPlanChanged = 1u << 0,   // another plan or floor is shown, or none
    kSceneChanged = 1u << 1,  // the shown floor's scene changed
    kCursorMoved = 1u << 2,   // the mouse moved over the plan (or left it)
    kWalkPosition = 1u << 3,  // the walk position changed
};

// The events that turn `before` into `after`.
std::uint32_t changes(const Snapshot& before, const Snapshot& after);

// Where the radio host gets the context from.
class ContextProvider {
public:
    virtual ~ContextProvider() = default;
    // The current snapshot; never null, never blocks on the UI. Any thread.
    virtual std::shared_ptr<const Snapshot> snapshot() const = 0;
    // `on_change` gets the ContextEvent bits of each change, on the
    // publishing thread; it must return at once. Returns an id for
    // unsubscribe. Any thread.
    virtual std::uint64_t subscribe(std::function<void(std::uint32_t events)> on_change) = 0;
    // After it returns, `on_change` of `id` is not called any more.
    virtual void unsubscribe(std::uint64_t id) = 0;
};

// Thread-safe snapshot holder: the UI publishes, every thread reads.
class ContextPublisher final : public ContextProvider {
public:
    ContextPublisher();

    std::shared_ptr<const Snapshot> snapshot() const override;
    std::uint64_t subscribe(std::function<void(std::uint32_t events)> on_change) override;
    void unsubscribe(std::uint64_t id) override;

    // Replaces the whole snapshot; returns the change bits sent.
    std::uint32_t publish(Snapshot next);
    // Changes only the cursor or only the walk position (every mouse move,
    // every walk step); `moved_monotonic_ns` is set to now when 0.
    std::uint32_t set_cursor(std::optional<PlanPosition> cursor);
    std::uint32_t set_walk(std::optional<PlanPosition> walk);

private:
    std::uint32_t update(const std::function<void(Snapshot&)>& edit);

    mutable std::mutex mutex_;       // snapshot pointer (short)
    std::mutex publish_mutex_;       // one publisher at a time, in order
    std::mutex subscribers_mutex_;
    std::shared_ptr<const Snapshot> current_;
    std::uint64_t next_id_ = 1;
    std::map<std::uint64_t, std::function<void(std::uint32_t)>> subscribers_;
};

}  // namespace yantra::context
