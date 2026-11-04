// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// From radio records to technology records (PLG-05, OA-13): the step of
// the measurement pipeline that copies a record of a radio plugin's
// schema (radio API, RAD-21) into a record of the technology plugin's
// schema with the same id, field by field by name, and turns its LEVEL
// fields into dBm (plugin.h "Radio records and technology schemas"). The
// rule is generic: the core knows no technology (PLG-01).
//
// Calibration proper (RF-01..03: the dBFS -> dBm table per device, gain
// and frequency, HW-09 antenna correction) comes with the pipeline in a
// later phase; until then a Calibration is one offset, and a dBFS level
// mapped without one is flagged YANTRA_RECORD_UNCALIBRATED.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <yantra/radio.h>

#include "yantra/core/plugin/abi.h"
#include "yantra/core/plugin/record.h"
#include "yantra/core/plugin/schema.h"

namespace yantra::measure {

// dBm = dBFS + offset_db for radio levels in dBFS (RF-02 placeholder).
struct Calibration {
    bool calibrated = false;  // false: the level is flagged UNCALIBRATED
    double offset_db = 0.0;
};

// The field mapping of one radio schema onto the technology schema of the
// same id; built once per (radio schema, technology schema) pair.
class SchemaMap {
public:
    // Checks the pair with plugin::check_radio_schema (PLG-05) and builds
    // the mapping; the errors are its problems. `tech_index` is the
    // technology schema's index in its plugin's schema list.
    static plugin::Parsed<SchemaMap> make(const plugin::Schema& tech, std::uint32_t tech_index,
                                          const yr_schema& radio);

    // A record of the technology schema from a radio record (values in the
    // radio schema's field order; record.schema_id is the shared id).
    // Fields the radio does not deliver stay absent; dBFS levels get the
    // calibration offset (UNCALIBRATED when there is none); the time and
    // the validity flags are kept.
    plugin::Record map(const plugin::Record& radio, const Calibration& calibration) const;

    std::uint32_t tech_index() const noexcept { return tech_index_; }
    const std::string& schema_id() const noexcept { return schema_id_; }

private:
    std::string schema_id_;
    std::uint32_t tech_index_ = 0;
    std::uint32_t tech_version_ = 0;
    std::size_t tech_field_count_ = 0;
    std::vector<std::size_t> target_;  // per radio field: technology field index
    std::vector<bool> dbfs_level_;     // per radio field: a dBFS level to convert
};

// A C view of a host-side record for calls into a plugin (PLG-03): the
// values point into `record`, which must outlive the view.
class RecordView {
public:
    RecordView(const plugin::Record& record, std::uint32_t schema_index);
    RecordView(const RecordView&) = delete;
    RecordView& operator=(const RecordView&) = delete;
    const yantra_record* get() const noexcept { return &record_; }

private:
    std::vector<yantra_value> values_;
    yantra_record record_{};
};

}  // namespace yantra::measure
