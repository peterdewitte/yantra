// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/measure/pipeline.h"

#include <algorithm>

namespace yantra::measure {

void RecordQueue::on_radio_record(const RadioRecord& record) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return !held_; });
    records_.push_back(record);
    cv_.notify_all();
}

void RecordQueue::on_radio_event(const RadioEvent& event) {
    const std::lock_guard lock(mutex_);
    events_.push_back(event);
    cv_.notify_all();
}

bool RecordQueue::wait_records(std::size_t count, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return records_.size() >= count; });
}

bool RecordQueue::wait_event(RadioEvent::Kind kind, std::size_t count,
                             std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] {
        return static_cast<std::size_t>(std::count_if(
                   events_.begin(), events_.end(),
                   [&](const RadioEvent& e) { return e.kind == kind; })) >= count;
    });
}

std::vector<RadioRecord> RecordQueue::records() const {
    const std::lock_guard lock(mutex_);
    return records_;
}

std::vector<RadioEvent> RecordQueue::events() const {
    const std::lock_guard lock(mutex_);
    return events_;
}

std::size_t RecordQueue::record_count() const {
    const std::lock_guard lock(mutex_);
    return records_.size();
}

void RecordQueue::hold(bool held) {
    const std::lock_guard lock(mutex_);
    held_ = held;
    cv_.notify_all();
}

}  // namespace yantra::measure
