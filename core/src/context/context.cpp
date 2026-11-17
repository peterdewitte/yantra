// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Context snapshots (PLG-27, RAD-18, OA-03), see context.h.

#include "yantra/core/context/context.h"

#include "yantra/core/platform/clock.h"

namespace yantra::context {

namespace {

const std::string* scene_of(const Snapshot& s) {
    if (!s.plan) return nullptr;
    const auto it = s.scenes.find(s.plan->floor_id);
    return it == s.scenes.end() ? nullptr : &it->second;
}

}  // namespace

std::uint32_t changes(const Snapshot& before, const Snapshot& after) {
    std::uint32_t ev = 0;
    if (before.plan != after.plan) ev |= kPlanChanged;
    const std::string* a = scene_of(before);
    const std::string* b = scene_of(after);
    // A new plan or floor brings its own scene; SCENE_CHANGED is for an edit
    // of the shown floor's scene while it stays shown.
    if ((ev & kPlanChanged) == 0 && (a == nullptr) != (b == nullptr)) ev |= kSceneChanged;
    if ((ev & kPlanChanged) == 0 && a != nullptr && b != nullptr && *a != *b) ev |= kSceneChanged;
    if (before.cursor != after.cursor) ev |= kCursorMoved;
    if (before.walk != after.walk) ev |= kWalkPosition;
    return ev;
}

ContextPublisher::ContextPublisher() : current_(std::make_shared<const Snapshot>()) {}

std::shared_ptr<const Snapshot> ContextPublisher::snapshot() const {
    const std::lock_guard lock(mutex_);
    return current_;
}

std::uint64_t ContextPublisher::subscribe(std::function<void(std::uint32_t)> on_change) {
    const std::lock_guard lock(subscribers_mutex_);
    const std::uint64_t id = next_id_++;
    subscribers_.emplace(id, std::move(on_change));
    return id;
}

void ContextPublisher::unsubscribe(std::uint64_t id) {
    const std::lock_guard lock(subscribers_mutex_);
    subscribers_.erase(id);
}

std::uint32_t ContextPublisher::update(const std::function<void(Snapshot&)>& edit) {
    const std::lock_guard order(publish_mutex_);
    std::shared_ptr<const Snapshot> before = snapshot();
    auto next = std::make_shared<Snapshot>(*before);
    edit(*next);
    const std::uint32_t ev = changes(*before, *next);
    {
        const std::lock_guard lock(mutex_);
        current_ = std::move(next);
    }
    if (ev != 0) {
        // Subscribers only queue the bits (they must return at once); the
        // lock makes unsubscribe() final.
        const std::lock_guard lock(subscribers_mutex_);
        for (const auto& [id, fn] : subscribers_) fn(ev);
    }
    return ev;
}

std::uint32_t ContextPublisher::publish(Snapshot next) {
    const std::int64_t now = platform::monotonic_ns();
    for (auto* p : {&next.cursor, &next.walk})
        if (*p && (*p)->moved_monotonic_ns == 0) (*p)->moved_monotonic_ns = now;
    return update([&](Snapshot& s) { s = std::move(next); });
}

std::uint32_t ContextPublisher::set_cursor(std::optional<PlanPosition> cursor) {
    if (cursor && cursor->moved_monotonic_ns == 0) cursor->moved_monotonic_ns = platform::monotonic_ns();
    return update([&](Snapshot& s) { s.cursor = std::move(cursor); });
}

std::uint32_t ContextPublisher::set_walk(std::optional<PlanPosition> walk) {
    if (walk && walk->moved_monotonic_ns == 0) walk->moved_monotonic_ns = platform::monotonic_ns();
    return update([&](Snapshot& s) { s.walk = std::move(walk); });
}

}  // namespace yantra::context
