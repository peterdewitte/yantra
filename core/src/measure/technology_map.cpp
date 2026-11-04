// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/measure/technology_map.h"

#include <type_traits>
#include <variant>

namespace yantra::measure {

plugin::Schema generic_schema(const yr_schema& radio) {
    plugin::Schema s;
    s.id = plugin::text_of(radio.id);
    s.label = plugin::text_of(radio.label);
    s.kind = static_cast<plugin::SchemaKind>(radio.kind);
    s.version = radio.version;
    for (std::uint32_t i = 0; i < radio.field_count && radio.fields != nullptr; ++i) {
        const yr_field_def* rf = radio.fields[i];
        if (rf == nullptr) continue;
        plugin::Field f;
        f.name = plugin::text_of(rf->name);
        f.label = plugin::text_of(rf->label);
        f.help = plugin::text_of(rf->help);
        f.unit = plugin::text_of(rf->unit);
        f.type = static_cast<plugin::FieldType>(rf->type);
        f.key = (rf->flags & YR_FIELD_KEY) != 0;
        f.shown_by_default = (rf->flags & YR_FIELD_DEFAULT) != 0;
        f.optional = f.name != YR_FIELD_FREQUENCY_HZ && f.name != YR_FIELD_ROLE &&
                     f.name != YR_FIELD_LEVEL;
        if (f.name == YR_FIELD_LEVEL && (f.unit == "dBFS" || f.unit == "dBm")) {
            f.category = plugin::Category::Level;
            f.unit = "dBm";
        } else if (rf->group == YR_GROUP_QUALITY) {
            f.category = plugin::Category::Quality;
        } else if (rf->group == YR_GROUP_SIM_POSITION) {
            f.category = plugin::Category::PlanPosition;
        } else if (rf->group == YR_GROUP_C1) {
            f.category = plugin::Category::Level;
        } else {
            f.category = plugin::Category::SystemInfo;
        }
        for (std::uint32_t k = 0; k < rf->enum_count && rf->enum_items != nullptr; ++k) {
            f.enum_items.push_back({rf->enum_items[k].value, plugin::text_of(rf->enum_items[k].name),
                                    plugin::text_of(rf->enum_items[k].label)});
        }
        s.fields.push_back(std::move(f));
    }
    return s;
}

plugin::Parsed<SchemaMap> SchemaMap::make(const plugin::Schema& tech, std::uint32_t tech_index,
                                          const yr_schema& radio) {
    plugin::Parsed<SchemaMap> out;
    out.errors = plugin::check_radio_schema(tech, radio);
    if (!out.errors.empty()) return out;
    SchemaMap m;
    m.schema_id_ = tech.id;
    m.tech_index_ = tech_index;
    m.tech_version_ = tech.version;
    m.tech_field_count_ = tech.fields.size();
    // The unit of the radio's own measurements: that of its core level
    // (dBFS, or dBm for a SELF_CALIBRATED radio, RAD-21).
    std::string measured_unit = "dBFS";
    for (std::uint32_t i = 0; i < radio.field_count; ++i)
        if (plugin::text_of(radio.fields[i]->name) == YR_FIELD_LEVEL)
            measured_unit = plugin::text_of(radio.fields[i]->unit);
    for (std::uint32_t i = 0; i < radio.field_count; ++i) {
        const yr_field_def& rf = *radio.fields[i];
        const std::size_t ti = *tech.field_index(plugin::text_of(rf.name));
        const plugin::Field& tf = tech.fields[ti];
        m.target_.push_back(ti);
        // A level reading of this radio (RF-02, HW-09): a LEVEL field in dBm
        // of the technology schema, a DOUBLE in the radio's measurement unit
        // and not a broadcast value (check_radio_schema refuses LEVEL for
        // the BROADCAST and NEIGHBOURS groups). Other dBm values (e.g. a
        // simulator's ground truth in dBm next to dBFS readings) pass
        // unchanged.
        const std::string unit = plugin::text_of(rf.unit);
        const bool level = tf.category == plugin::Category::Level && tf.unit == "dBm" &&
                           rf.type == YR_TYPE_DOUBLE && unit == measured_unit;
        m.level_.push_back(level);
        m.dbfs_level_.push_back(level && unit == "dBFS");
    }
    out.value = std::move(m);
    return out;
}

plugin::Record SchemaMap::map(const plugin::Record& radio, const Calibration& calibration) const {
    plugin::Record out;
    out.schema_id = schema_id_;
    out.schema_version = tech_version_;
    out.time = radio.time;
    out.flags = radio.flags;
    out.values.resize(tech_field_count_);
    for (std::size_t i = 0; i < target_.size() && i < radio.values.size(); ++i) {
        plugin::Value v = radio.values[i];
        if (level_[i]) {
            if (const double* d = std::get_if<double>(&v)) {
                double dbm = *d + calibration.correction_db;
                if (dbfs_level_[i]) {
                    dbm += calibration.offset_db;
                    if (!calibration.calibrated) out.flags |= YANTRA_RECORD_UNCALIBRATED;
                }
                v = dbm;
            }
        }
        out.values[target_[i]] = std::move(v);
    }
    return out;
}

RecordView::RecordView(const plugin::Record& record, std::uint32_t schema_index) {
    values_.reserve(record.values.size());
    for (const plugin::Value& v : record.values) {
        values_.push_back(std::visit(
            [](const auto& x) -> yantra_value {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::monostate>) return yantra_value_none();
                else if constexpr (std::is_same_v<T, bool>) return yantra_value_bool(x ? 1 : 0);
                else if constexpr (std::is_same_v<T, std::int64_t>) return yantra_value_int(x);
                else if constexpr (std::is_same_v<T, std::uint64_t>) return yantra_value_uint(x);
                else if constexpr (std::is_same_v<T, double>) return yantra_value_double(x);
                else if constexpr (std::is_same_v<T, std::string>)
                    return yantra_value_string(x.c_str());
                else return yantra_value_enum(x.value);
            },
            v));
    }
    record_.struct_size = sizeof(yantra_record);
    record_.schema_index = schema_index;
    record_.time = record.time;
    record_.flags = record.flags;
    record_.value_count = static_cast<std::uint32_t>(values_.size());
    record_.values = values_.data();
}

}  // namespace yantra::measure
