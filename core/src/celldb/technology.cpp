// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The technology plugin's part of the cell database (technology.h).

#include "yantra/core/celldb/technology.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "yantra/core/measure/technology_map.h"

namespace yantra::celldb {

namespace {

// The record value of a field value for a field type; monostate when it
// does not fit.
plugin::Value to_record_value(const FieldValue& v, plugin::FieldType type) {
    const auto* i = std::get_if<std::int64_t>(&v);
    const auto* d = std::get_if<double>(&v);
    const auto* s = std::get_if<std::string>(&v);
    switch (type) {
    case plugin::FieldType::Bool:
        if (i && (*i == 0 || *i == 1)) return *i == 1;
        break;
    case plugin::FieldType::Int:
        if (i) return *i;
        break;
    case plugin::FieldType::UInt:
        if (i && *i >= 0) return static_cast<std::uint64_t>(*i);
        if (d && *d >= 0.0 && *d < 18446744073709551616.0 && std::floor(*d) == *d) return static_cast<std::uint64_t>(*d);
        break;
    case plugin::FieldType::Double:
        if (d) return *d;
        if (i) return static_cast<double>(*i);
        break;
    case plugin::FieldType::String:
        if (s) return *s;
        break;
    case plugin::FieldType::Enum:
        if (i) return plugin::EnumValue{*i};
        break;
    }
    return std::monostate{};
}

}  // namespace

std::optional<Technology> Technology::from_plugin(const plugin::PluginInfo& info,
                                                  const yantra_plugin_descriptor* descriptor,
                                                  yantra_plugin_instance* instance) {
    if (!info.technology) return std::nullopt;
    const plugin::TechnologyInfo& t = *info.technology;
    if (t.cell_schema >= t.schemas.size()) return std::nullopt;
    Technology out;
    out.id = t.technology_id;
    out.label = t.label;
    out.plugin_id = info.id;
    out.plugin_version = info.version;
    out.cell_schema = t.schemas[t.cell_schema];
    out.cell_schema_index = static_cast<std::uint32_t>(t.cell_schema);
    if (t.neighbour_schema && *t.neighbour_schema < t.schemas.size()) {
        out.neighbour_schema = t.schemas[*t.neighbour_schema];
        out.neighbour_schema_index = static_cast<std::uint32_t>(*t.neighbour_schema);
    }
    out.links = t.links;
    out.serving_states = t.serving_states;
    out.bands = t.bands;
    if (descriptor != nullptr) out.api = descriptor->technology;
    out.instance = instance;
    return out;
}

std::vector<std::string> Technology::key_fields() const {
    std::vector<std::string> out;
    for (const std::size_t k : cell_schema.key_fields()) out.push_back(cell_schema.fields[k].name);
    return out;
}

std::vector<std::string> Technology::match_fields(const std::vector<std::string>& chosen) const {
    return chosen.empty() ? key_fields() : chosen;
}

std::optional<std::uint64_t> Technology::frequency_of_channel(std::uint64_t channel) const {
    if (api != nullptr && api->channel_to_frequency != nullptr) {
        std::uint64_t hz = 0;
        if (api->channel_to_frequency(instance, channel, &hz) == YANTRA_OK) return hz;
        return std::nullopt;
    }
    for (const plugin::Band& b : bands)
        if (channel >= b.first_channel && channel - b.first_channel < b.channel_count())
            return b.first_hz + (channel - b.first_channel) * b.raster_hz;
    return std::nullopt;
}

std::optional<std::uint64_t> Technology::channel_of_frequency(std::uint64_t hz) const {
    if (api != nullptr && api->frequency_to_channel != nullptr) {
        std::uint64_t ch = 0;
        if (api->frequency_to_channel(instance, hz, &ch) == YANTRA_OK) return ch;
        return std::nullopt;
    }
    for (const plugin::Band& b : bands)
        if (hz >= b.first_hz && hz <= b.last_hz && b.raster_hz != 0 && (hz - b.first_hz) % b.raster_hz == 0)
            return b.first_channel + (hz - b.first_hz) / b.raster_hz;
    return std::nullopt;
}

bool Technology::values_match(std::string_view field, const FieldValue& received, const FieldValue& stored) const {
    const auto i = cell_schema.field_index(field);
    if (!i || api == nullptr || api->values_match == nullptr || !cell_schema.fields[*i].verify)
        return values_equal(received, stored);
    const plugin::FieldType type = cell_schema.fields[*i].type;
    plugin::Record a;
    a.values.resize(cell_schema.fields.size());
    plugin::Record b = a;
    a.values[*i] = to_record_value(received, type);
    b.values[*i] = to_record_value(stored, type);
    if (std::holds_alternative<std::monostate>(a.values[*i]) || std::holds_alternative<std::monostate>(b.values[*i]))
        return values_equal(received, stored);
    const measure::RecordView va(a, cell_schema_index);
    const measure::RecordView vb(b, cell_schema_index);
    std::int32_t equal = 0;
    if (api->values_match(instance, cell_schema_index, static_cast<std::uint32_t>(*i), &va.get()->values[*i],
                          &vb.get()->values[*i], &equal) != YANTRA_OK)
        return values_equal(received, stored);
    return equal != 0;
}

plugin::Record Technology::cell_record(const FieldMap& fields) const {
    plugin::Record r;
    r.schema_id = cell_schema.id;
    r.schema_version = cell_schema.version;
    r.values.resize(cell_schema.fields.size());
    for (std::size_t k = 0; k < cell_schema.fields.size(); ++k) {
        const auto it = fields.find(cell_schema.fields[k].name);
        if (it != fields.end()) r.values[k] = to_record_value(it->second, cell_schema.fields[k].type);
    }
    return r;
}

std::string Technology::serving_state_name(std::int64_t value) const {
    for (const plugin::EnumItem& s : serving_states)
        if (s.value == value) return s.name;
    return {};
}

const Technology* find_technology(const Technologies& technologies, std::string_view id) noexcept {
    for (const Technology& t : technologies)
        if (t.id == id) return &t;
    return nullptr;
}

}  // namespace yantra::celldb
