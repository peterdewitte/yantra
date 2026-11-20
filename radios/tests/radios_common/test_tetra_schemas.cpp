// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shared TETRA record schemas (RAD-12, RAD-21..23): valid plugin
// schemas for each option set (the radio API's own validator), the core
// layout, standard optional fields, the same names/types/units whatever
// the options, the record filler, C1 and the C views.

#include <doctest/doctest.h>

#include <yantra/radio.hpp>
#include <yantra/radios/tetra_schemas.hpp>

#include <limits>
#include <set>
#include <sstream>
#include <string>

using namespace yantra::radios;

namespace {

uint32_t caps_for(const TetraSchemaOptions& o)
{
    uint32_t c = 0;
    c |= o.level_dbm ? YR_CAP_SELF_CALIBRATED : 0u;
    c |= o.simulated ? YR_CAP_SIMULATED : 0u;
    c |= o.scan ? YR_CAP_SCAN : 0u;
    return c;
}

}  // namespace

TEST_CASE("tetra schemas: valid radio API schemas for every option set (RAD-12, RAD-21)")
{
    for (bool dbm : {false, true})
        for (bool sim : {false, true})
            for (bool scan : {false, true}) {
                const TetraSchemaOptions o{dbm, sim, scan};
                CAPTURE(dbm);
                CAPTURE(sim);
                CAPTURE(scan);
                const std::vector<SchemaDef> v = tetra_schemas(o);
                REQUIRE(v.size() == (scan ? 3u : 2u));
                CHECK(v[0].id == kTetraCellSchema);
                CHECK(v[1].id == kTetraNeighbourSchema);
                for (const SchemaDef& s : v) {
                    CAPTURE(s.id);
                    const SchemaView view(s);
                    const std::vector<std::string> problems =
                        yantra::radio::validate_schema(view.schema_c, caps_for(o));
                    for (const std::string& p : problems)
                        MESSAGE(p);
                    CHECK(problems.empty());
                }
                CHECK(v[0].fields[YR_CORE_LEVEL].unit == (dbm ? "dBm" : "dBFS"));
                CHECK(((v[0].groups & YR_GROUP_SIM_POSITION) != 0) == sim);
            }
}

TEST_CASE("tetra schemas: names, types, units and groups do not depend on the options")
{
    const SchemaDef full = tetra_cell_schema({.level_dbm = true, .simulated = true});
    for (bool sim : {false, true}) {
        const SchemaDef s = tetra_cell_schema({.level_dbm = true, .simulated = sim});
        for (const FieldDef& f : s.fields) {
            CAPTURE(f.name);
            const int i = full.field_index(f.name);
            REQUIRE(i >= 0);
            const FieldDef& g = full.fields[static_cast<std::size_t>(i)];
            CHECK(f.type == g.type);
            CHECK(f.unit == g.unit);
            CHECK(f.group == g.group);
        }
    }
    // The fields the measurement engine and yantra-gen both fill.
    for (const char* name : {"mer_pct", "crc_error_pct_aach", "ber_pct", "snr_db", "evm_mer_db", "freq_error_hz",
                             "freq_error_ppm", "c1_db", "hyperframe", "encryption", "security_class",
                             "encrypted_blocks", "ms_txpwr_max_cell", "rxlev_access_min", "rxlev_access_min_dbm",
                             "ms_txpwr_max_cell_dbm", "main_carrier", "cell_load_pct", "srt_db"})
        CHECK(full.field_index(name) >= 0);
    for (std::string_view c : kTetraCrcChannels)
        CHECK(full.field_index("crc_error_pct_" + std::string(c)) >= 0);
    const SchemaDef n = tetra_neighbour_schema({});
    for (const char* name : {"cell_number", "main_carrier", "neighbour_frequency_hz", "neighbour_la"})
        CHECK(n.field_index(name) >= 0);
    CHECK(n.field_index("neighbour_cell_id") < 0);
    CHECK(tetra_neighbour_schema({.simulated = true}).field_index("neighbour_cell_id") >= 0);
    CHECK(tetra_scan_schema({}).fields.size() == YR_CORE_FIELD_COUNT);
}

TEST_CASE("tetra schemas: record filler and C1")
{
    const SchemaDef s = tetra_cell_schema({});
    const RecordFiller fill(s, 0);
    yr_time t{};
    t.monotonic_ns = 5;
    Record r = fill.start(t, YR_RECORD_NO_SYNC);
    CHECK(r.values.size() == s.fields.size());
    CHECK(r.present == 0);
    fill.set(r, YR_FIELD_FREQUENCY_HZ, value_uint(390000000));
    fill.set(r, YR_FIELD_ROLE, value_enum(YR_ROLE_OTHER));
    fill.set(r, YR_FIELD_LEVEL, value_double(-42.5));
    CHECK(r.present == 0);
    fill.set(r, YR_FIELD_SNR_DB, value_double(17.0));
    CHECK(r.present == YR_GROUP_QUALITY);
    fill.set(r, YR_FIELD_MER_PCT, value_double(std::numeric_limits<double>::quiet_NaN()));
    CHECK(r.values[static_cast<std::size_t>(s.field_index(YR_FIELD_MER_PCT))].type == YR_TYPE_NONE);
    CHECK_THROWS_AS(fill.set(r, "no_such_field", value_uint(1)), std::invalid_argument);
    CHECK(fill.has("srt_db"));
    // The record is valid for a record file of these schemas.
    RecordFileHeader h;
    h.producer = "test";
    h.plugin.id = "test";
    h.plugin.name = "test";
    h.plugin.version = "1";
    h.plugin.technology_id = "tetra";
    h.plugin.licence = "GPL-3.0-or-later";
    h.schemas = tetra_schemas({});
    std::ostringstream out;
    RecordFileWriter w(out);
    w.write_header(h);
    CHECK_NOTHROW(w.write_record(r));
    // C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - 30 dBm).
    CHECK(tetra_c1_db(-80.0, -115.0, 30.0) == doctest::Approx(35.0));
    CHECK(tetra_c1_db(-80.0, -115.0, 35.0) == doctest::Approx(30.0));
    CHECK(tetra_c1_db(-80.0, -115.0, 15.0) == doctest::Approx(35.0));
    CHECK(tetra_c1_db(-80.0, -115.0, 40.0, 40.0) == doctest::Approx(35.0));
}
