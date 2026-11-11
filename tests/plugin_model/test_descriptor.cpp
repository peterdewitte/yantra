// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// parse_descriptor (PLG-02..04, PLG-06, PLG-08, PLG-10, API-04): the C
// test plugin of tests/plugin_api is accepted with every table read, and
// broken copies of it are refused with a reason that names the problem.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <yantra/core/plugin/descriptor.h>

namespace yp = yantra::plugin;

namespace {

const yantra_plugin_descriptor& c_plugin() {
    static yantra_host_api host = [] {
        yantra_host_api h{};
        h.struct_size = sizeof h;
        h.api_version = yantra_api_version_compiled();
        return h;
    }();
    const yantra_plugin_descriptor* d = yantra_plugin_entry(&host);
    REQUIRE(d != nullptr);
    return *d;
}

std::string all(const std::vector<std::string>& errors) {
    std::string s;
    for (const auto& e : errors) {
        s += e + "\n";
    }
    return s;
}

// Parses and expects a refusal mentioning `needle`.
void refused(const yantra_plugin_descriptor& d, const std::string& needle) {
    const auto r = yp::parse_descriptor(&d, yp::host_api_version(), "test" YANTRA_PLUGIN_SUFFIX);
    CHECK_FALSE(r.ok());
    INFO(all(r.errors));
    CHECK(all(r.errors).find(needle) != std::string::npos);
}

// A mutable deep copy of the C plugin's technology table and schemas.
struct TechCopy {
    yantra_technology_api tech;
    std::vector<yantra_schema> schemas;
    std::vector<const yantra_schema*> schema_ptrs;
    std::vector<yantra_field_def> cell_fields;
    std::vector<const yantra_field_def*> cell_field_ptrs;
    yantra_plugin_descriptor d;

    TechCopy() : tech(*c_plugin().technology), d(c_plugin()) {
        for (std::uint32_t i = 0; i < tech.schema_count; ++i) {
            schemas.push_back(*tech.schemas[i]);
        }
        const yantra_schema& cell = schemas[0];
        for (std::uint32_t i = 0; i < cell.field_count; ++i) {
            cell_fields.push_back(*cell.fields[i]);
        }
        relink();
    }
    void relink() {
        cell_field_ptrs.clear();
        for (auto& f : cell_fields) {
            cell_field_ptrs.push_back(&f);
        }
        schemas[0].fields = cell_field_ptrs.data();
        schemas[0].field_count = static_cast<std::uint32_t>(cell_field_ptrs.size());
        schema_ptrs.clear();
        for (auto& s : schemas) {
            schema_ptrs.push_back(&s);
        }
        tech.schemas = schema_ptrs.data();
        tech.schema_count = static_cast<std::uint32_t>(schema_ptrs.size());
        d.technology = &tech;
    }
    yantra_field_def& field(const std::string& name) {
        for (auto& f : cell_fields) {
            if (name == f.name) {
                return f;
            }
        }
        FAIL("no field " << name);
        return cell_fields[0];
    }
};

}  // namespace

TEST_CASE("the C test plugin is accepted with all its tables") {
    const auto r = yp::parse_descriptor(&c_plugin(), yp::host_api_version(), "ctest" YANTRA_PLUGIN_SUFFIX);
    INFO(all(r.errors));
    REQUIRE(r.ok());
    const yp::PluginInfo& p = *r.value;
    CHECK(p.id == "org.example.ctest");
    CHECK(p.licence == "GPL-3.0-or-later");
    CHECK(p.kinds == (YANTRA_KIND_TECHNOLOGY | YANTRA_KIND_FEATURE | YANTRA_KIND_EXPORTER));

    REQUIRE(p.technology);
    const yp::TechnologyInfo& t = *p.technology;
    CHECK(t.technology_id == "ctest");
    REQUIRE(t.bands.size() == 1);
    CHECK(t.bands[0].channel_count() == 40);
    CHECK(t.bands[0].first_channel == 100);
    REQUIRE(t.schemas.size() == 2);
    CHECK(t.cell_schema == 0);
    REQUIRE(t.neighbour_schema);
    CHECK(*t.neighbour_schema == 1);
    REQUIRE(t.links.size() == 1);
    CHECK(t.links[0].cell_field == "la");
    CHECK(t.serving_states.size() == 2);
    CHECK(t.has_c1);
    CHECK(t.has_reselection);
    CHECK_FALSE(t.has_frequency_to_channel);
    const yp::Schema& cell = t.schemas[0];
    CHECK(cell.key_fields() == std::vector<std::size_t>{4, 5, 6});
    CHECK(cell.fields[7].verify);
    CHECK(cell.fields[3].category == yp::Category::Level);

    REQUIRE(p.feature);
    REQUIRE(p.feature->schemas.size() == 1);
    CHECK(p.feature->schemas[0].kind == yp::SchemaKind::Recording);
    CHECK(p.feature->has_on_record);
    REQUIRE(p.exporter);
    CHECK(p.exporter->formats[0].extension == "txt");
    REQUIRE(p.ui.windows.size() == 1);
    CHECK(p.ui.menu_items[0].window_id == "ctest_panel");
    REQUIRE(p.translations.size() == 2);
    CHECK(p.translations[0].lang == "nl");
}

TEST_CASE("API version: refused from the frozen prefix with a clear message (PLG-04)") {
    yantra_plugin_descriptor d = c_plugin();
    const yantra_api_version host{1, 0};
    d.api_version = {2, 0};
    auto r = yp::parse_descriptor(&d, host, "new" YANTRA_PLUGIN_SUFFIX);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.errors.size() == 1);
    CHECK(r.errors[0].find("new" YANTRA_PLUGIN_SUFFIX) == 0);
    CHECK(r.errors[0].find("plugin API 2.0") != std::string::npos);
    d.api_version = {1, 1};
    r = yp::parse_descriptor(&d, host, "new" YANTRA_PLUGIN_SUFFIX);
    CHECK(all(r.errors).find("newer than") != std::string::npos);
    CHECK_FALSE(yp::parse_descriptor(nullptr, host, "x" YANTRA_PLUGIN_SUFFIX).ok());

    const auto major = yp::check_api_version({1, 4}, {2, 0}, "demo");
    CHECK(major.result == yp::ApiCompat::MajorMismatch);
    CHECK(major.message.find("'demo'") != std::string::npos);
    CHECK(yp::check_api_version({1, 4}, {1, 5}, "demo").result == yp::ApiCompat::MinorTooNew);
    CHECK(yp::check_api_version({1, 4}, {1, 2}, "demo").ok());
}

TEST_CASE("read_versioned zero-fills fields an older plugin does not know (PLG-03)") {
    yantra_band b{};
    b.struct_size = static_cast<std::uint32_t>(offsetof(yantra_band, first_channel));
    b.first_channel = 99;  // beyond struct_size: must not be read
    const auto copy = yp::read_versioned(&b, offsetof(yantra_band, first_channel));
    REQUIRE(copy);
    CHECK(copy->first_channel == 0);
    CHECK_FALSE(yp::read_versioned(&b));  // shorter than the 1.0 size
    CHECK_FALSE(yp::read_versioned<yantra_band>(nullptr));
}

TEST_CASE("identity, licence and life cycle are checked (PLG-10)") {
    yantra_plugin_descriptor d = c_plugin();
    d.licence = "LicenseRef-Proprietary";
    refused(d, "not GPL-3.0-compatible");
    d = c_plugin();
    d.licence = "";
    refused(d, "no SPDX licence");
    d = c_plugin();
    d.id = "Org.Example";
    refused(d, "not a plugin id");
    d = c_plugin();
    d.init = nullptr;
    refused(d, "init and shutdown");
    d = c_plugin();
    d.source_url = "ftp://example.org";
    refused(d, "source_url");
    CHECK(yp::is_plugin_id("org.example.ctest"));
    CHECK(yp::is_plugin_id("be.example-radio.x_1"));
    CHECK_FALSE(yp::is_plugin_id("org..example"));
    CHECK_FALSE(yp::is_plugin_id("org.example."));
}

TEST_CASE("kinds and their tables go together; categories are declared (PLG-02, PLG-08)") {
    yantra_plugin_descriptor d = c_plugin();
    d.exporter = nullptr;
    refused(d, "exporter kind declared without its table");
    d = c_plugin();
    d.kinds = YANTRA_KIND_TECHNOLOGY | YANTRA_KIND_FEATURE;
    refused(d, "exporter table given without its kind");
    d = c_plugin();
    d.kinds = 0x80;
    refused(d, "YANTRA_KIND_");
    d = c_plugin();
    d.data_categories &= ~YANTRA_CATEGORY_QUALITY;
    refused(d, "category not declared");
    d = c_plugin();
    d.data_categories |= 0x100;
    refused(d, "unknown bits");
}

TEST_CASE("technology table: bands, cell and neighbour schemas, links, rules (API-04)") {
    {
        TechCopy c;
        c.tech.cell_schema = 1;  // the neighbour schema
        refused(c.d, "cell_schema");
    }
    {
        TechCopy c;
        c.tech.neighbour_schema = 0;
        refused(c.d, "neighbour_schema");
    }
    {
        TechCopy c;
        static const yantra_field_link bad[] = {{"nb_la", "colour_code"}};
        c.tech.links = bad;
        refused(c.d, "not a KEY field");
    }
    {
        TechCopy c;
        c.tech.neighbour_schema = YANTRA_NO_SCHEMA;
        refused(c.d, "links need a neighbour_schema");
    }
    {
        TechCopy c;
        c.tech.serving_state_count = 0;
        refused(c.d, "reselect needs serving_states");
    }
    {
        TechCopy c;
        yantra_band band = *c.tech.bands[0];
        band.raster_hz = 30000;
        const yantra_band* bands[] = {&band};
        c.tech.bands = bands;
        refused(c.d, "raster");
    }
    {
        TechCopy c;
        c.tech.band_count = 0;
        refused(c.d, "no bands");
    }
    {
        TechCopy c;
        c.schemas[0].kind = YANTRA_SCHEMA_RECORDING;
        refused(c.d, "RECORDING schema");
    }
    {
        TechCopy c;  // the copy itself is still valid
        const auto r = yp::parse_descriptor(&c.d, yp::host_api_version(), "copy" YANTRA_PLUGIN_SUFFIX);
        INFO(all(r.errors));
        CHECK(r.ok());
    }
}

TEST_CASE("technology fields: scope boundary, units, keys, help (PLG-05, PLG-08, DOC-07)") {
    {
        TechCopy c;
        c.field("colour_code").name = "issi";
        refused(c.d, "scope boundary");
    }
    {
        TechCopy c;
        c.field("level").unit = "dBFS";
        refused(c.d, "LEVEL field is in dBm");
    }
    {
        TechCopy c;
        c.field("mcc").type = YANTRA_TYPE_DOUBLE;
        refused(c.d, "KEY field is neither OPTIONAL nor DOUBLE");
    }
    {
        TechCopy c;
        c.field("mer_pct").unit = "";
        refused(c.d, "QUALITY field needs a unit");
    }
    {
        TechCopy c;
        c.field("channel").help = "";
        refused(c.d, "help text");
    }
    {
        TechCopy c;
        c.field("role").enum_count = 0;
        refused(c.d, "ENUM field without items");
    }
    {
        TechCopy c;
        c.field("level").flags |= YANTRA_FIELD_OPTIONAL;
        refused(c.d, "'level' must keep the radio API's type");
    }
    {
        TechCopy c;
        c.cell_fields.erase(c.cell_fields.begin());  // frequency_hz
        c.relink();
        refused(c.d, "needs the field 'frequency_hz'");
    }
    {
        TechCopy c;
        c.field("la").flags = YANTRA_FIELD_DEFAULT;
        c.field("mcc").flags = YANTRA_FIELD_DEFAULT;
        c.field("mnc").flags = YANTRA_FIELD_DEFAULT;
        refused(c.d, "needs KEY fields");
    }
}

TEST_CASE("feature, exporter, UI and translation tables (PLG-06, UI-12)") {
    yantra_plugin_descriptor d = c_plugin();

    yantra_feature_api feature = *d.feature;
    const yantra_schema* tech_schemas[] = {d.technology->schemas[0]};
    feature.schemas = tech_schemas;
    d.feature = &feature;
    refused(d, "must be a RECORDING schema");

    d = c_plugin();
    yantra_export_format fmt = *d.exporter->formats[0];
    fmt.extension = ".txt";
    const yantra_export_format* formats[] = {&fmt};
    yantra_exporter_api exporter = *d.exporter;
    exporter.formats = formats;
    d.exporter = &exporter;
    refused(d, "extension");
    exporter.formats = c_plugin().exporter->formats;  // valid formats again
    exporter.abort = nullptr;
    refused(d, "abort are required");

    d = c_plugin();
    yantra_plugin_ui_api ui = *d.ui;
    yantra_ui_menu_item item = *ui.menu_items[0];
    item.tooltip = nullptr;
    const yantra_ui_menu_item* items[] = {&item};
    ui.menu_items = items;
    d.ui = &ui;
    refused(d, "tooltip (DOC-07)");
    item.tooltip = "Opens it.";
    item.window_id = "nowhere";
    refused(d, "'nowhere' is not declared");
    item.window_id = nullptr;
    refused(d, "menu_selected is required");
    item.window_id = "ctest_panel";
    ui.draw = nullptr;
    refused(d, "draw");

    d = c_plugin();
    static const yantra_translation bad_lang[] = {{"C test panel", "en", "C test panel"}};
    d.translations = bad_lang;
    d.translation_count = 1;
    refused(d, "not nl, fr or de");
    static const yantra_translation twice[] = {{"x", "nl", "a"}, {"x", "nl", "b"}};
    d.translations = twice;
    d.translation_count = 2;
    refused(d, "appears twice");
}
