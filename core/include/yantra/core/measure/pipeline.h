// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input side of the measurement pipeline (OA-13): what the radio host
// (PLG-25) hands on after its record sink checked and copied a radio
// record. The pipeline proper (calibration RF-02, antenna correction
// HW-09, the 1 Hz cycle per carrier, views, recording, walk test) comes in
// measure/measurement_pipeline.h and implements MeasurementInput;
// RecordQueue is the simple observer for tests and tools.
//
// Threading: the radio host calls a MeasurementInput from its delivery
// thread of each stream, never from a plugin thread; a slow input makes
// that stream's sink queue fill and the sink drop and count (RAD-24), the
// plugin never waits.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "yantra/core/plugin/record.h"

struct yr_schema;

namespace yantra::measure {

// One checked radio record with where it came from (PLG-12).
struct RadioRecord {
    std::string plugin_id;       // radio plugin (yr_descriptor::id)
    std::string plugin_version;
    std::string technology_id;   // yr_descriptor::technology_id, e.g. "tetra"
    std::string device_key;      // yr_device_info::key
    std::uint64_t stream_id = 0; // radio host's id of the stream (new after a reopen)
    std::uint32_t schema_index = 0;
    std::uint32_t schema_kind = 0;  // YR_SCHEMA_* (MEASUREMENT, NEIGHBOUR, SCAN)
    std::uint32_t present = 0;      // optional groups carried (YR_GROUP_* bits)
    std::uint32_t capabilities = 0; // the plugin's YR_CAP_* (SIMULATED, SELF_CALIBRATED ...)
    plugin::Record record;          // schema id/version, time, flags, one value per field
    // The schema's field names in value order (shared by all records of it).
    std::shared_ptr<const std::vector<std::string>> field_names;
    // The radio schema itself (the plugin's descriptor, or a record file's
    // RadioSchemaView): valid during on_radio_record only; nullptr when
    // unknown. Copies of the record must not use it later.
    const yr_schema* radio_schema = nullptr;
};

// A stream event (RAD-25) or a radio host status change.
struct RadioEvent {
    enum class Kind {
        Stream,      // a YR_EVENT_* of the plugin: `event`, `value`
        Started,     // the stream started (also after a reopen)
        Stopped,     // the stream stopped (stop, END_OF_STREAM, ERROR, DEVICE_LOST)
        Lost,        // the device was unplugged; the host waits for it
        Reopened,    // the device was reopened and the stream restarted (NFR-04)
        Failed,      // the plugin was disabled (`message` says why)
    };
    Kind kind = Kind::Stream;
    std::string plugin_id;
    std::string device_key;
    std::uint64_t stream_id = 0;
    std::uint32_t event = 0;  // YR_EVENT_* for Kind::Stream
    std::int64_t value = 0;
    std::int64_t monotonic_ns = 0;
    std::string message;
};

class MeasurementInput {
public:
    virtual ~MeasurementInput() = default;
    virtual void on_radio_record(const RadioRecord& record) = 0;
    virtual void on_radio_event(const RadioEvent& event) { (void)event; }
};

// Thread-safe queue of what arrived, with waits on observable conditions.
class RecordQueue final : public MeasurementInput {
public:
    void on_radio_record(const RadioRecord& record) override;
    void on_radio_event(const RadioEvent& event) override;

    // Waits until at least `count` records arrived in total (since
    // construction); false when `timeout` passed first.
    bool wait_records(std::size_t count, std::chrono::milliseconds timeout) const;
    // Waits until an event of `kind` arrived (in total at least `count`).
    bool wait_event(RadioEvent::Kind kind, std::size_t count,
                    std::chrono::milliseconds timeout) const;

    std::vector<RadioRecord> records() const;
    std::vector<RadioEvent> events() const;
    std::size_t record_count() const;

    // Test hook: while held, on_radio_record blocks (a pipeline that does
    // not keep up, RAD-24).
    void hold(bool held);

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::vector<RadioRecord> records_;
    std::vector<RadioEvent> events_;
    bool held_ = false;
};

}  // namespace yantra::measure
