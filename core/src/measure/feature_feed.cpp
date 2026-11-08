// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Feature-plugin record feed (PLG-02), see feature_feed.h.

#include "yantra/core/measure/feature_feed.h"

#include "yantra/core/measure/technology_map.h"

namespace yantra::measure {

FeatureFeed::FeatureFeed(std::shared_ptr<plugin::PluginLoader> loader) : loader_(std::move(loader)) {}

std::shared_ptr<const plugin::SchemaView> FeatureFeed::view_of(
    const std::shared_ptr<const plugin::Schema>& schema) {
    const std::lock_guard lock(mutex_);
    auto it = views_.find(schema.get());
    if (it == views_.end())
        it = views_.emplace(schema.get(), std::make_pair(schema, std::make_shared<plugin::SchemaView>(*schema)))
                 .first;
    return it->second.second;
}

void FeatureFeed::on_record(const MappedRecord& r) {
    if (!loader_ || !loader_->has_record_receivers() || !r.schema) return;
    const std::shared_ptr<const plugin::SchemaView> view = view_of(r.schema);
    const RecordView record(r.record, r.schema_index);
    const yantra_provenance provenance = plugin::to_c(r.provenance);
    loader_->deliver_record(provenance, view->c(), *record.get());
}

}  // namespace yantra::measure
