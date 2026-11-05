// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the cell database needs from a technology plugin (PLG-02,
// plugin.h yantra_technology_api), in one place: its cell and neighbour
// schemas and links (DB-05), its channel raster (DB-02 channel columns),
// parameter verification (values_match, DB-04) and reselection (reselect,
// DB-08). The core gives the fields no meaning of its own (PLG-01); every
// rule is the plugin's. Rule functions are pure (plugin.h "Threading"), so
// a Technology may be used from any thread while its plugin stays loaded.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/celldb/database.h"
#include "yantra/core/plugin/descriptor.h"
#include "yantra/core/plugin/schema.h"
#include "yantra/plugin.h"

namespace yantra::celldb {

struct Technology {
    std::string id;  ///< technology id (PLG-12)
    std::string label;
    std::string plugin_id;
    std::string plugin_version;
    plugin::Schema cell_schema;
    std::uint32_t cell_schema_index = 0;
    std::optional<plugin::Schema> neighbour_schema;
    std::uint32_t neighbour_schema_index = 0;
    std::vector<plugin::FieldLink> links;          ///< neighbour field -> cell key field
    std::vector<plugin::EnumItem> serving_states;  ///< DB-08
    std::vector<plugin::Band> bands;
    /// The plugin's rule table and instance; nullptr: no rule calls
    /// (exact comparison, no reselection, channels from the bands).
    const yantra_technology_api* api = nullptr;
    yantra_plugin_instance* instance = nullptr;

    /// From a loaded technology plugin (PluginLoader::info, ::descriptor,
    /// ::instance); nullopt when it has no technology part.
    static std::optional<Technology> from_plugin(const plugin::PluginInfo& info,
                                                 const yantra_plugin_descriptor* descriptor,
                                                 yantra_plugin_instance* instance);

    /// The KEY fields of the cell schema (a cell's identity), in field order.
    std::vector<std::string> key_fields() const;
    /// The fields received cells are paired with entries by: `chosen`
    /// (the database's match fields) when not empty, else key_fields().
    std::vector<std::string> match_fields(const std::vector<std::string>& chosen) const;

    /// Channel number <-> frequency by the plugin (else its bands).
    std::optional<std::uint64_t> frequency_of_channel(std::uint64_t channel) const;
    std::optional<std::uint64_t> channel_of_frequency(std::uint64_t frequency_hz) const;

    /// DB-04: does a received value of a cell-schema field match the
    /// stored one? The plugin's values_match for VERIFY fields when it has
    /// one, else values_equal().
    bool values_match(std::string_view field, const FieldValue& received, const FieldValue& stored) const;

    /// A record of the cell schema with the fields that fit their field
    /// type (an integer for INT/UINT/BOOL/ENUM, a number for DOUBLE, a
    /// text for STRING); the others stay absent.
    plugin::Record cell_record(const FieldMap& fields) const;

    /// Machine name of a serving state value (DB-08), "" when unknown.
    std::string serving_state_name(std::int64_t value) const;
};

using Technologies = std::vector<Technology>;
const Technology* find_technology(const Technologies& technologies, std::string_view id) noexcept;

}  // namespace yantra::celldb
