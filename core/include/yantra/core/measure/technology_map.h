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
// The measurement pipeline (measure/measurement_pipeline.h) gets the
// numbers of a Calibration from its level-correction stage
// (measure/correction.h: the RF-02 calibration table keyed per HW-01b and
// the HW-09 antenna correction); a dBFS level mapped without a
// calibration is flagged YANTRA_RECORD_UNCALIBRATED.
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

// What is added to the LEVEL fields (technology unit "dBm") of a record:
// offset_db to radio levels in dBFS (RF-02: dBm = dBFS + offset_db), and
// correction_db to every such level, dBFS or dBm (HW-09 antenna gain,
// cable loss, attenuator).
struct Calibration {
    bool calibrated = false;  // false: a dBFS level is flagged UNCALIBRATED
    double offset_db = 0.0;
    double correction_db = 0.0;
    // The record's level is dBm whatever its schema's unit says (radio API
    // 1.1 YR_RECORD_LEVEL_DBM, yr_record_level_is_dbm(); HW-01b): no
    // offset_db, never UNCALIBRATED.
    bool level_dbm = false;
};

// A technology schema derived from a radio schema for a radio whose
// technology has no technology plugin loaded (generic views, PLG-12):
// same id, kind, version and fields; the field `level` of the core layout
// (RAD-21) is a LEVEL field in "dBm", fields of the QUALITY group are
// QUALITY, of the SIM_POSITION group PLAN_POSITION, of the C1 group LEVEL
// ("dB"), the rest SYSTEM_INFO; every field except frequency_hz, role and
// level is OPTIONAL. Technology-neutral: it follows the radio API only.
plugin::Schema generic_schema(const yr_schema& radio);

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
    // calibration offset (UNCALIBRATED when there is none), every dBm
    // level field the correction; the time and the validity flags are
    // kept.
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
    std::vector<bool> level_;          // per radio field: a LEVEL field in dBm (corrected)
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
