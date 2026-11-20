// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The example technology plugin (PLG-15): built by the main build, loaded
// through the PluginLoader, and its schemas match those of the yantra-radios
// beacon radio plugin (check_radio_schema, PLG-05), both in the golden record
// file (PLG-11) and in the radio plugin's own descriptor.

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>

#include <yantra/core/measure/pipeline.h>
#include <yantra/core/measure/record_file.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/plugin/schema.h>
#include <yantra/core/radio/radio_host.h>
#include <yantra/core/context/context.h>

namespace fs = std::filesystem;
namespace yp = yantra::plugin;
namespace ym = yantra::measure;

namespace {

std::string all(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& e : v) s += e + "\n";
    return s;
}

const yp::Schema* find(const yp::TechnologyInfo& t, const std::string& id) {
    for (const yp::Schema& s : t.schemas)
        if (s.id == id) return &s;
    return nullptr;
}

}  // namespace

TEST_CASE("example beacon: loads through the PluginLoader and matches its radio (PLG-15)") {
    yp::PluginLoaderConfig config;
    config.state = std::make_shared<yp::MemoryStateStore>();
    yp::PluginLoader loader(std::move(config));
    const yp::PluginEntry e = loader.load_file(EXAMPLE_BEACON_PATH);
    INFO(e.message);
    REQUIRE(e.status == yp::PluginStatus::Enabled);
    CHECK(e.id == "yantra.example.beacon");
    const yp::PluginInfo* info = loader.info(e.id);
    REQUIRE(info != nullptr);
    REQUIRE(info->technology);
    const yp::TechnologyInfo& tech = *info->technology;
    CHECK(tech.technology_id == "beacon");
    CHECK(tech.has_frequency_to_channel);
    CHECK(tech.has_c1);
    CHECK(tech.has_reselection);
    CHECK(tech.has_values_match);
    CHECK(info->ui.windows.size() == 1);

    // The schemas in the golden record file of the simulated radio.
    const auto file = ym::read_record_file(fs::path(YANTRA_SOURCE_DIR) /
                                           "plugins/beacon/testdata/beacon.records.jsonl");
    INFO(all(file.errors));
    REQUIRE(file.ok());
    CHECK(file.value->technology_id == tech.technology_id);
    REQUIRE(file.value->schemas.size() == 2);
    for (const ym::RecordFileSchema& rs : file.value->schemas) {
        const ym::RadioSchemaView view(rs);
        const yp::Schema* ts = find(tech, rs.id);
        REQUIRE_MESSAGE(ts != nullptr, rs.id);
        const auto problems = yp::check_radio_schema(*ts, view.get());
        INFO(all(problems));
        CHECK(problems.empty());
    }

    // The schemas the radio plugin itself declares.
    const fs::path radio = fs::path(RADIO_TEST_PLUGIN_DIR) / "beacon" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(radio)) {
        MESSAGE("no beacon radio plugin (prebuilt radio plugins): descriptor check skipped");
        return;
    }
    namespace yr = yantra::radio;
    yr::RadioHostConfig rc;
    rc.state = std::make_shared<yp::MemoryStateStore>();
    rc.pipeline = std::make_shared<ym::RecordQueue>();
    rc.context = std::make_shared<yantra::context::ContextPublisher>();
    yr::RadioHost host(std::move(rc));
    const yr::RadioPluginEntry re = host.load_file(radio);
    INFO(re.message);
    REQUIRE(re.status == yr::RadioPluginStatus::Enabled);
    CHECK(re.technology_id == tech.technology_id);
    const yr_descriptor* d = host.descriptor(re.id);
    REQUIRE(d != nullptr);
    for (std::uint32_t i = 0; i < d->schema_count; ++i) {
        const yp::Schema* ts = find(tech, d->schemas[i]->id);
        REQUIRE_MESSAGE(ts != nullptr, d->schemas[i]->id);
        const auto problems = yp::check_radio_schema(*ts, *d->schemas[i]);
        INFO(all(problems));
        CHECK(problems.empty());
    }
}
