// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/measure/catalog.h"

namespace yantra::measure {

void TechnologyCatalog::add(const std::string& technology_id, const std::string& plugin_id,
                            const std::string& plugin_version,
                            const std::vector<plugin::Schema>& schemas,
                            std::shared_ptr<const TechnologyRules> rules) {
    const std::lock_guard lock(mutex_);
    auto& by_id = by_technology_[technology_id];
    for (std::size_t i = 0; i < schemas.size(); ++i) {
        TechnologySchema t;
        t.schema = std::make_shared<const plugin::Schema>(schemas[i]);
        t.index = static_cast<std::uint32_t>(i);
        t.technology_id = technology_id;
        t.plugin_id = plugin_id;
        t.plugin_version = plugin_version;
        t.rules = rules;
        by_id[schemas[i].id] = std::move(t);
    }
    ++generation_;
}

bool TechnologyCatalog::add(const plugin::PluginInfo& info) {
    if (!info.technology) return false;
    add(info.technology->technology_id, info.id, info.version, info.technology->schemas);
    return true;
}

bool TechnologyCatalog::add(const plugin::PluginInfo& info,
                            const yantra_plugin_descriptor* descriptor,
                            yantra_plugin_instance* instance) {
    if (!info.technology || descriptor == nullptr || descriptor->technology == nullptr)
        return false;
    auto rules = std::make_shared<TechnologyRules>();
    rules->api = descriptor->technology;
    rules->instance = instance;
    rules->cell_schema = static_cast<std::uint32_t>(info.technology->cell_schema);
    add(info.technology->technology_id, info.id, info.version, info.technology->schemas,
        std::move(rules));
    return true;
}

void TechnologyCatalog::remove_plugin(const std::string& plugin_id) {
    const std::lock_guard lock(mutex_);
    for (auto& [tech, by_id] : by_technology_) {
        for (auto it = by_id.begin(); it != by_id.end();) {
            if (it->second.plugin_id == plugin_id) it = by_id.erase(it);
            else ++it;
        }
    }
    ++generation_;
}

std::optional<TechnologySchema> TechnologyCatalog::find(const std::string& technology_id,
                                                        const std::string& schema_id) const {
    const std::lock_guard lock(mutex_);
    const auto t = by_technology_.find(technology_id);
    if (t == by_technology_.end()) return std::nullopt;
    const auto s = t->second.find(schema_id);
    if (s == t->second.end()) return std::nullopt;
    return s->second;
}

std::uint64_t TechnologyCatalog::generation() const {
    const std::lock_guard lock(mutex_);
    return generation_;
}

}  // namespace yantra::measure
