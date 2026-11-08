// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Feeds the measurement pipeline's records to the feature plugins
// (PLG-02: yantra_feature_api::on_record, e.g. the probe's live view,
// PLG-24): an observer of a pipeline (live or replay) that hands every
// accepted record, with its provenance and a C view of its technology
// schema, to PluginLoader::deliver_record on the record's delivery
// thread (a measurement thread, PLG-07). Nothing is copied when no
// feature plugin takes records. Technology-neutral (PLG-01).
#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/core/plugin/schema.h"

namespace yantra::measure {

class FeatureFeed final : public MeasurementObserver {
public:
    explicit FeatureFeed(std::shared_ptr<plugin::PluginLoader> loader);
    void on_record(const MappedRecord& record) override;

private:
    std::shared_ptr<const plugin::SchemaView> view_of(const std::shared_ptr<const plugin::Schema>& schema);

    std::shared_ptr<plugin::PluginLoader> loader_;
    std::mutex mutex_;
    // Keyed by the pipeline's schema object, which the entry keeps alive.
    std::map<const plugin::Schema*,
             std::pair<std::shared_ptr<const plugin::Schema>, std::shared_ptr<const plugin::SchemaView>>>
        views_;
};

}  // namespace yantra::measure
