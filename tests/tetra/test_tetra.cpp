// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TETRA technology plugin (plugins/tetra): channel raster against the
// standard's values (EN 300 392-2 21.4.4.1, TS 100 392-15), C1 (RF-06)
// against hand-calculated values, yantra-radios' tetra_c1_db and the C1
// of radio records, reselection (DB-08) and verification (DB-04) cases,
// and its schemas against every shipped radio plugin (PLG-05, PLG-11).

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <yantra/core/measure/record_file.h>
#include <yantra/core/measure/technology_map.h>
#include <yantra/core/platform/dynlib.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/plugin/schema.h>
#include <yantra/plugin.h>
#include <yantra/radio.h>
#include <yantra/radios/tetra_schemas.hpp>

#include "tetra_raster.h"
#include "tetra_rules.h"

namespace fs = std::filesystem;
namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace yt = yantra::tetra;

namespace {

constexpr std::uint64_t MHz = 1'000'000;

// The TETRA plugin, loaded once through Yantra's plugin loader.
struct Tetra {
    yp::PluginLoader loader{yp::PluginLoaderConfig{}};
    const yp::PluginInfo* info = nullptr;
    const yantra_technology_api* api = nullptr;
    yantra_plugin_instance* instance = nullptr;
    Tetra() {
        const yp::PluginEntry e = loader.load_file(TETRA_PLUGIN_PATH);
        INFO(e.message);
        REQUIRE(e.status == yp::PluginStatus::Enabled);
        info = loader.info("yantra.tetra");
        REQUIRE(info != nullptr);
        REQUIRE(info->technology);
        api = loader.descriptor("yantra.tetra")->technology;
        instance = loader.instance("yantra.tetra");
        REQUIRE(api != nullptr);
    }
    const yp::Schema* find(const std::string& id) const {
        for (const yp::Schema& s : info->technology->schemas)
            if (s.id == id) return &s;
        return nullptr;
    }
    const yp::Schema& schema(const std::string& id) const {
        const yp::Schema* s = find(id);
        REQUIRE(s != nullptr);
        return *s;
    }
    std::uint32_t index(const std::string& id) const {
        for (std::uint32_t i = 0; i < info->technology->schemas.size(); ++i)
            if (info->technology->schemas[i].id == id) return i;
        FAIL("no schema " << id);
        return 0;
    }
};

Tetra& tetra() {
    static Tetra t;
    return t;
}

// A tetra_cell record with the access parameters (dBm) and the four
// reselection thresholds (dB) set; other fields absent.
struct CellRecord {
    std::vector<yantra_value> values;
    yantra_record record{};
    CellRecord(std::int64_t rxlev_dbm, std::int64_t txpwr_dbm, std::uint64_t srt = 20, std::uint64_t srh = 4,
               std::uint64_t frt = 10, std::uint64_t frh = 6) {
        const yp::Schema& s = tetra().schema("tetra_cell");
        values.assign(s.fields.size(), yantra_value_none());
        auto set_int = [&](const char* n, std::int64_t v) { values[*s.field_index(n)] = yantra_value_int(v); };
        auto set_uint = [&](const char* n, std::uint64_t v) { values[*s.field_index(n)] = yantra_value_uint(v); };
        set_int("rxlev_access_min_dbm", rxlev_dbm);
        set_int("ms_txpwr_max_cell_dbm", txpwr_dbm);
        set_uint("srt_db", srt);
        set_uint("srh_db", srh);
        set_uint("frt_db", frt);
        set_uint("frh_db", frh);
        record.struct_size = sizeof record;
        record.schema_index = tetra().index("tetra_cell");
        record.value_count = static_cast<std::uint32_t>(values.size());
        record.values = values.data();
    }
    void clear(const char* name) { values[*tetra().schema("tetra_cell").field_index(name)] = yantra_value_none(); }
};

double plugin_c1(const yantra_record* r, double level_dbm, double ms_dbm, yantra_status* status = nullptr) {
    yantra_c1_input in{};
    in.struct_size = sizeof in;
    in.record = r;
    in.level_dbm = level_dbm;
    in.ms_power_dbm = ms_dbm;
    double c1 = std::nan("");
    const yantra_status st = tetra().api->compute_c1(tetra().instance, &in, &c1);
    if (status != nullptr) *status = st;
    else REQUIRE(st == YANTRA_OK);
    return c1;
}

}  // namespace

TEST_CASE("tetra raster: downlink frequencies of carrier, band and offset (EN 300 392-2 21.4.4.1)") {
    // band x 100 MHz + carrier x 25 kHz + offset (0, +6.25, -6.25, +12.5 kHz)
    CHECK(yt::downlink_hz({3, 3200, 0}) == 380 * MHz);
    CHECK(yt::downlink_hz({3, 3600, 0}) == 390 * MHz);
    CHECK(yt::downlink_hz({3, 3600, 1}) == 390'006'250);
    CHECK(yt::downlink_hz({3, 3600, 2}) == 389'993'750);
    CHECK(yt::downlink_hz({3, 3600, 3}) == 390'012'500);
    CHECK(yt::downlink_hz({4, 400, 0}) == 410 * MHz);
    CHECK(yt::downlink_hz({4, 2799, 0}) == 469'975'000);
    CHECK_FALSE(yt::downlink_hz({16, 0, 0}));
    CHECK_FALSE(yt::downlink_hz({3, 4096, 0}));
    CHECK_FALSE(yt::downlink_hz({3, 0, 4}));
    // And back: the carrier of a frequency is unique.
    for (const yt::Carrier c : {yt::Carrier{3, 3600, 0}, yt::Carrier{3, 3601, 1}, yt::Carrier{3, 3602, 2},
                                yt::Carrier{4, 2000, 3}, yt::Carrier{4, 1199, 0}}) {
        CAPTURE(c.carrier);
        CHECK(yt::carrier_of(*yt::downlink_hz(c)) == c);
    }
    CHECK_FALSE(yt::carrier_of(390'001'000));  // on no offset raster
    // The simulator's offset carrier (sim fixture): carrier 3603, -6.25 kHz.
    CHECK(yt::carrier_of(390'068'750) == yt::Carrier{3, 3603, 2});
}

TEST_CASE("tetra raster: duplex spacing and uplink (TS 100 392-15)") {
    CHECK(yt::duplex_spacing_hz(3, 0) == 10 * MHz);   // 380-400 MHz: 10 MHz
    CHECK(yt::duplex_spacing_hz(4, 0) == 10 * MHz);   // 410-430 / 450-470 MHz: 10 MHz
    CHECK(yt::duplex_spacing_hz(4, 1) == 7 * MHz);
    CHECK(yt::duplex_spacing_hz(4, 4) == 5 * MHz);
    CHECK(yt::duplex_spacing_hz(4, 5) == 9'500'000);
    CHECK(yt::duplex_spacing_hz(3, 2) == 0u);         // simplex in every band
    CHECK_FALSE(yt::duplex_spacing_hz(3, 6));         // reserved
    CHECK_FALSE(yt::duplex_spacing_hz(5, 1));
    CHECK(yt::uplink_hz({3, 3600, 0}, 0, false) == 380 * MHz);
    CHECK(yt::uplink_hz({3, 3600, 0}, 0, true) == 400 * MHz);  // reverse operation
    CHECK(yt::uplink_hz({4, 2400, 3}, 0, false) == 450'012'500);
    CHECK_FALSE(yt::uplink_hz({3, 3600, 0}, 7, false));
}

TEST_CASE("tetra raster: the plugin's bands and channel numbers (PLG-02)") {
    const auto& bands = tetra().info->technology->bands;
    REQUIRE(bands.size() == 3);
    CHECK(bands[0].id == "tetra_380");
    CHECK(bands[0].first_hz == 380 * MHz);
    CHECK(bands[0].last_hz == 399'975'000);
    CHECK(bands[0].first_channel == 3200);
    CHECK(bands[1].first_hz == 410 * MHz);
    CHECK(bands[1].first_channel == 400);
    CHECK(bands[2].first_hz == 450 * MHz);
    CHECK(bands[2].last_hz == 469'975'000);
    CHECK(bands[2].first_channel == 2000);
    for (const auto& b : bands) CHECK(b.raster_hz == 25'000);
    std::uint64_t ch = 0;
    std::uint64_t hz = 0;
    // The channel is the carrier number (the radio records' `channel`), at any offset.
    CHECK(tetra().api->frequency_to_channel(tetra().instance, 390'012'500, &ch) == YANTRA_OK);
    CHECK(ch == 3600);
    CHECK(tetra().api->frequency_to_channel(tetra().instance, 389'993'750, &ch) == YANTRA_OK);
    CHECK(ch == 3600);
    CHECK(tetra().api->channel_to_frequency(tetra().instance, 2000, &hz) == YANTRA_OK);
    CHECK(hz == 450 * MHz);
    // 350-370 MHz and 400-410 MHz are not bands of the plugin.
    CHECK(tetra().api->frequency_to_channel(tetra().instance, 360 * MHz, &ch) == YANTRA_ERR_NOT_FOUND);
    CHECK(tetra().api->frequency_to_channel(tetra().instance, 405 * MHz, &ch) == YANTRA_ERR_NOT_FOUND);
    CHECK(tetra().api->channel_to_frequency(tetra().instance, 1500, &hz) == YANTRA_ERR_NOT_FOUND);
    CHECK(tetra().api->channel_to_frequency(tetra().instance, 99999, &hz) == YANTRA_ERR_NOT_FOUND);
}

TEST_CASE("tetra C1: hand-calculated values and yantra-radios' formula give the same number (RF-06)") {
    // C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS power)
    CHECK(yt::c1_db(-65.0, {-115.0, 35.0}, 30.0) == 45.0);   // 50 - 5
    CHECK(yt::c1_db(-65.0, {-115.0, 35.0}, 35.0) == 50.0);   // mobile: no penalty
    CHECK(yt::c1_db(-100.0, {-110.0, 30.0}, 30.0) == 10.0);
    CHECK(yt::c1_db(-120.0, {-110.0, 45.0}, 30.0) == -25.0);
    CHECK(yt::kPortableMsPowerDbm == 30.0);
    CHECK(yt::kPortableMsPowerDbm == yantra::radios::kTetraC1MsMaxPowerDbm);
    for (double level = -125.0; level <= -40.0; level += 3.7)
        for (int rx = -125; rx <= -50; rx += 5)
            for (int tx = 15; tx <= 45; tx += 5)
                for (double ms : {15.0, 30.0, 35.0, 45.0}) {
                    CellRecord r(rx, tx);
                    const double radios = yantra::radios::tetra_c1_db(level, rx, tx, ms);
                    CHECK(yt::c1_db(level, {double(rx), double(tx)}, ms) == radios);
                    CHECK(plugin_c1(&r.record, level, ms) == radios);
                }
}

TEST_CASE("tetra C1: the plugin answers UNSUPPORTED without the access parameters and refuses bad input") {
    CellRecord r(-115, 35);
    yantra_status st = YANTRA_OK;
    CHECK(plugin_c1(&r.record, -65.0, 30.0) == 45.0);
    r.clear("ms_txpwr_max_cell_dbm");
    plugin_c1(&r.record, -65.0, 30.0, &st);
    CHECK(st == YANTRA_ERR_UNSUPPORTED);
    CellRecord n(-115, 35);
    n.record.schema_index = tetra().index("tetra_neighbour");
    plugin_c1(&n.record, -65.0, 30.0, &st);
    CHECK(st == YANTRA_ERR_UNSUPPORTED);
    plugin_c1(&r.record, std::nan(""), 30.0, &st);
    CHECK(st == YANTRA_ERR_INVALID_ARGUMENT);
    plugin_c1(&r.record, -65.0, std::nan(""), &st);
    CHECK(st == YANTRA_ERR_INVALID_ARGUMENT);
}

TEST_CASE("tetra C1: the plugin gives the C1 the radios put in their records (RF-06, RAD-61)") {
    // yantra-gen (dBm levels, MS 30 dBm) and the simulator radio through
    // yantra-radio-host (dBFS levels; its true offset at gain 0 is -20 dB).
    struct Fixture {
        const char* file;
        double level_to_dbm_db;
    };
    for (const Fixture fx : {Fixture{"tetra_gen.records.jsonl", 0.0}, Fixture{"tetra_sim.records.jsonl", -20.0}}) {
        CAPTURE(fx.file);
        const auto file = ym::read_record_file(fs::path(TETRA_TESTDATA) / fx.file);
        REQUIRE_MESSAGE(file.ok(), (file.errors.empty() ? "" : file.errors[0]));
        std::size_t compared = 0;
        for (const ym::RecordFileLine& line : file.value->lines) {
            const auto* rr = std::get_if<ym::RadioRecord>(&line);
            if (rr == nullptr || rr->record.schema_id != "tetra_cell") continue;
            const yp::Schema& tech = tetra().schema("tetra_cell");
            const ym::RecordFileSchema* hs = nullptr;
            for (const auto& s : file.value->schemas)
                if (s.id == "tetra_cell") hs = &s;
            REQUIRE(hs != nullptr);
            const ym::RadioSchemaView view(*hs);
            const auto map = ym::SchemaMap::make(tech, tetra().index("tetra_cell"), view.get());
            REQUIRE(map.ok());
            const yp::Record mapped = map.value->map(rr->record, ym::Calibration{});  // the radio level as is
            const auto& v = mapped.values;
            const auto c1 = v[*tech.field_index("c1_db")];
            const auto level = v[*tech.field_index("level")];
            if (!std::holds_alternative<double>(c1) || !std::holds_alternative<double>(level)) continue;
            const ym::RecordView rv(mapped, tetra().index("tetra_cell"));
            const double dbm = std::get<double>(level) + fx.level_to_dbm_db;
            CHECK(plugin_c1(rv.get(), dbm, 30.0) == doctest::Approx(std::get<double>(c1)).epsilon(1e-12));
            ++compared;
        }
        CHECK(compared >= 10);
    }
}

TEST_CASE("tetra reselection: serving-cell states and reselections (DB-08)") {
    struct Case {
        double c1;
        std::vector<double> c2;
        bool thresholds;
        yt::ServingState state;
        yt::Reselection reselection;
        int target;
    };
    const yt::ReselectionThresholds t{20.0, 4.0, 10.0, 6.0};  // SRT, SRH, FRT, FRH
    const Case cases[] = {
        {-3.0, {5.0, 8.0}, true, yt::ServingState::RadioLinkFailure, yt::Reselection::LinkFailure, 1},
        {-3.0, {-1.0}, true, yt::ServingState::RadioLinkFailure, yt::Reselection::None, -1},
        {12.0, {}, false, yt::ServingState::Exclusive, yt::Reselection::None, -1},
        {12.0, {-2.0, 0.0}, false, yt::ServingState::Exclusive, yt::Reselection::None, -1},
        {30.0, {25.0, 30.0}, false, yt::ServingState::Superior, yt::Reselection::None, -1},
        {5.0, {12.0}, true, yt::ServingState::Relinquishable, yt::Reselection::Fast, 0},   // 12 > 5 + 6
        {5.0, {11.0}, true, yt::ServingState::Relinquishable, yt::Reselection::None, -1},  // 11 = 5 + 6
        {15.0, {16.0, 19.5}, true, yt::ServingState::Improvable, yt::Reselection::Slow, 1}, // 19.5 > 15 + 4
        {15.0, {19.0}, true, yt::ServingState::Improvable, yt::Reselection::None, -1},
        {25.0, {40.0}, true, yt::ServingState::Usable, yt::Reselection::None, -1},
    };
    for (const Case& c : cases) {
        CAPTURE(c.c1);
        CAPTURE(c.c2.size());
        const auto c2 = [](const void* ctx, std::size_t i) -> std::optional<double> {
            return (*static_cast<const std::vector<double>*>(ctx))[i];
        };
        const auto r = yt::reselect(c.c1, c.thresholds ? std::optional(t) : std::nullopt, c.c2.size(), c2, &c.c2);
        REQUIRE(r);
        CHECK(r->state == c.state);
        CHECK(r->reselection == c.reselection);
        CHECK(r->target == c.target);
        CHECK(std::string(r->reason).find("C1 ") == 0);
    }
    // A better neighbour without the thresholds: no answer.
    const std::vector<double> better{20.0};
    CHECK_FALSE(yt::reselect(10.0, std::nullopt, 1,
                             [](const void* ctx, std::size_t i) -> std::optional<double> {
                                 return (*static_cast<const std::vector<double>*>(ctx))[i];
                             },
                             &better));
    CHECK(std::string(yt::serving_state_name(yt::ServingState::RadioLinkFailure)) == "radio_link_failure");
}

TEST_CASE("tetra reselection through the plugin: C1 and C2 from the records (DB-08)") {
    // Serving -95 dBm, RXLEV_ACCESS_MIN -105: C1 10 < SRT 20 and >= FRT 10:
    // improvable; the neighbour's C2 -80 - -105 - (40 - 30) = 15 > 10 + 4.
    CellRecord serving(-105, 30);
    CellRecord neighbour(-105, 40);
    CellRecord unknown(-105, 40);
    unknown.clear("rxlev_access_min_dbm");
    yantra_reselection_cell s{sizeof(yantra_reselection_cell), 0, &serving.record, -95.0};
    yantra_reselection_cell n{sizeof(yantra_reselection_cell), 0, &neighbour.record, -80.0};
    yantra_reselection_cell u{sizeof(yantra_reselection_cell), 0, &unknown.record, -40.0};
    const yantra_reselection_cell* list[] = {&u, &n};
    yantra_reselection_input in{sizeof in, 2, &s, list, 30.0};
    yantra_reselection_result out{};
    out.struct_size = sizeof out;
    REQUIRE(tetra().api->reselect(tetra().instance, &in, &out) == YANTRA_OK);
    CHECK(out.serving_state == static_cast<std::int64_t>(yt::ServingState::Improvable));
    CHECK(out.chosen == 1);
    CHECK(std::string(out.reason).find("slow reselection to neighbour 1") != std::string::npos);
    // The serving state is one of the plugin's serving states.
    bool listed = false;
    for (const auto& st : tetra().info->technology->serving_states) listed |= st.value == out.serving_state;
    CHECK(listed);
    // Without the serving cell's access parameters: UNSUPPORTED.
    serving.clear("rxlev_access_min_dbm");
    CHECK(tetra().api->reselect(tetra().instance, &in, &out) == YANTRA_ERR_UNSUPPORTED);
}

TEST_CASE("tetra verification: broadcast codes and exact values (DB-04)") {
    const auto match = [](const char* field, yantra_value a, yantra_value b) {
        const yp::Schema& s = tetra().schema("tetra_cell");
        std::int32_t eq = -1;
        REQUIRE(tetra().api->values_match(tetra().instance, tetra().index("tetra_cell"),
                                          static_cast<std::uint32_t>(*s.field_index(field)), &a, &b,
                                          &eq) == YANTRA_OK);
        return eq == 1;
    };
    // RXLEV_ACCESS_MIN is sent in 5 dB steps: a stored -113 stands for -115.
    CHECK(match("rxlev_access_min_dbm", yantra_value_int(-115), yantra_value_int(-113)));
    CHECK(match("rxlev_access_min_dbm", yantra_value_int(-115), yantra_value_int(-115)));
    CHECK_FALSE(match("rxlev_access_min_dbm", yantra_value_int(-115), yantra_value_int(-110)));
    CHECK(match("ms_txpwr_max_cell_dbm", yantra_value_int(35), yantra_value_int(36)));
    CHECK_FALSE(match("ms_txpwr_max_cell_dbm", yantra_value_int(35), yantra_value_int(40)));
    // 2 dB steps; a stored value between two codes stands for the higher one.
    CHECK(match("access_parameter_dbm", yantra_value_int(-45), yantra_value_int(-46)));
    CHECK_FALSE(match("access_parameter_dbm", yantra_value_int(-45), yantra_value_int(-44)));
    // Everything else is exact.
    CHECK(match("colour_code", yantra_value_uint(3), yantra_value_uint(3)));
    CHECK_FALSE(match("colour_code", yantra_value_uint(3), yantra_value_uint(4)));
    CHECK_FALSE(match("main_carrier", yantra_value_uint(3600), yantra_value_int(3600)));  // type differs
    CHECK(yt::snap_to_grid(-200, yt::kRxlevAccessMinGrid) == -125);
    CHECK(yt::snap_to_grid(0, yt::kRxlevAccessMinGrid) == -50);
    CHECK(yt::snap_to_grid(-118, yt::kRxlevAccessMinGrid) == -120);
    CHECK(yt::snap_to_grid(-117, yt::kRxlevAccessMinGrid) == -115);
    // The VERIFY fields are the broadcast parameters of the cell schema.
    const yp::Schema& s = tetra().schema("tetra_cell");
    for (const char* f : {"colour_code", "rxlev_access_min_dbm", "ms_txpwr_max_cell_dbm", "srt_db", "trunking"})
        CHECK(s.fields[*s.field_index(f)].verify);
    CHECK_FALSE(s.fields[*s.field_index("level")].verify);
}

TEST_CASE("tetra schemas: every shipped radio plugin's schemas match the technology schemas (PLG-05, PLG-11)") {
    std::size_t checked = 0;
    for (const char* name : {"simulator.so", "hackrf.so", "soapysdr.so", "pei.so"}) {
        CAPTURE(name);
        const fs::path path = fs::path(RADIO_PLUGIN_DIR) / name;
        REQUIRE(fs::exists(path));
        std::string error;
        auto lib = yantra::platform::DynamicLibrary::open(path, error);
        REQUIRE_MESSAGE(lib, error);
        auto entry = reinterpret_cast<yr_entry_fn>(lib->symbol(YANTRA_RADIO_ENTRY_SYMBOL, error));
        REQUIRE_MESSAGE(entry, error);
        const yr_descriptor* d = entry();
        REQUIRE(d != nullptr);
        CHECK(std::string(d->technology_id) == "tetra");
        for (std::uint32_t i = 0; i < d->schema_count; ++i) {
            const yr_schema& radio = *d->schemas[i];
            CAPTURE(radio.id);
            const yp::Schema* tech = tetra().find(radio.id);
            REQUIRE(tech != nullptr);
            const auto problems = yp::check_radio_schema(*tech, radio);
            CHECK_MESSAGE(problems.empty(), (problems.empty() ? "" : problems[0]));
            CHECK(ym::SchemaMap::make(*tech, tetra().index(radio.id), radio).ok());
            ++checked;
        }
    }
    CHECK(checked == 8);
    // The cell schema identifies a cell by network, location area and main carrier (DB-01).
    const yp::Schema& cell = tetra().info->technology->schemas[tetra().info->technology->cell_schema];
    CHECK(cell.id == "tetra_cell");
    std::vector<std::string> keys;
    for (const std::size_t k : cell.key_fields()) keys.push_back(cell.fields[k].name);
    CHECK(keys == std::vector<std::string>{"frequency_hz", "mcc", "mnc", "la"});
}
