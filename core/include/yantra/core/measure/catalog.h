// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Technology schemas known to the measurement pipeline (PLG-05, OA-13):
// for a radio's technology id and a radio schema id, the schema of the
// technology plugin with that technology id (plugin.h "Radio records and
// technology schemas") and the plugin's id and version (PLG-12). The app
// fills it from the enabled technology plugins of the PluginLoader; tests
// add schemas directly. Technology-neutral (PLG-01): it is a lookup table.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/plugin/descriptor.h"
#include "yantra/core/plugin/schema.h"
#include "yantra/plugin.h"

namespace yantra::measure {

// The rule functions of a technology plugin that the measurement pipeline
// calls (RF-06: compute_c1 for records without C1). The table and the
// instance belong to the plugin loader: they stay valid while the plugin
// is enabled, so remove the plugin from the catalog (remove_plugin)
// before it is disabled or unloaded, while no radio streams.
struct TechnologyRules {
    const yantra_technology_api* api = nullptr;
    yantra_plugin_instance* instance = nullptr;
    std::uint32_t cell_schema = 0;  // yantra_technology_api::cell_schema
};

struct TechnologySchema {
    std::shared_ptr<const plugin::Schema> schema;
    std::uint32_t index = 0;  // in the technology plugin's schema list
    std::string technology_id;
    std::string plugin_id;    // the technology plugin
    std::string plugin_version;
    // The plugin's rules (shared by its schemas); nullptr when added
    // without them (schemas only).
    std::shared_ptr<const TechnologyRules> rules;
};

// Thread-safe. generation() changes with every add/remove, so users can
// rebuild what they derived from it.
class TechnologyCatalog {
public:
    // Adds (or replaces) the schemas of one technology plugin.
    void add(const std::string& technology_id, const std::string& plugin_id,
             const std::string& plugin_version, const std::vector<plugin::Schema>& schemas,
             std::shared_ptr<const TechnologyRules> rules = nullptr);
    // Adds a loaded plugin's technology schemas; false when it has no
    // technology part.
    bool add(const plugin::PluginInfo& info);
    // The same with the plugin's rule functions (PluginLoader::descriptor
    // and ::instance of the enabled plugin), so the pipeline can ask it
    // for C1 (RF-06); false without a technology part or descriptor.
    bool add(const plugin::PluginInfo& info, const yantra_plugin_descriptor* descriptor,
             yantra_plugin_instance* instance);
    void remove_plugin(const std::string& plugin_id);

    std::optional<TechnologySchema> find(const std::string& technology_id,
                                         const std::string& schema_id) const;
    std::uint64_t generation() const;

private:
    mutable std::mutex mutex_;
    // technology id -> schema id -> entry
    std::map<std::string, std::map<std::string, TechnologySchema>> by_technology_;
    std::uint64_t generation_ = 1;
};

}  // namespace yantra::measure
