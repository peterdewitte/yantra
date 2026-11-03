// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/gen/truth_records.hpp"

#include "yantra/gen/downlink.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace yantra::gen {

using radios::FieldDef;
using radios::Record;
using radios::SchemaDef;
using radios::Value;

namespace {

FieldDef field(const char* name, uint32_t type, uint32_t group, const char* unit, const char* label,
               uint32_t flags = 0)
{
    FieldDef f;
    f.name = name;
    f.type = type;
    f.group = group;
    f.unit = unit;
    f.label = label;
    f.flags = flags;
    return f;
}

// Field indexes of the cell schema.
enum CellField : size_t {
    kFreq, kChannel, kRole, kLevel, kMcc, kMnc, kLa, kCc,
    kSnr, kFreqError,
    kEncryption, kTxpwr, kRxlev,
    kSimFloor, kSimX, kSimY,
    kCellId, kLevelInst, kSource, kBestAntenna, kWalls,
    kCellFieldCount
};

// Field indexes of the neighbour schema.
enum NeighbourField : size_t {
    kNFreq, kNIndex, kNCount, kNCellNumber, kNCarrier, kNFrequency, kNLa, kNCellId, kNeighbourFieldCount
};

Value v_uint(uint64_t x)
{
    Value v;
    v.type = YR_TYPE_UINT;
    v.u64 = x;
    return v;
}

Value v_double(double x)
{
    Value v;
    if (std::isfinite(x)) {
        v.type = YR_TYPE_DOUBLE;
        v.f64 = x;
    }
    return v;
}

Value v_bool(bool x)
{
    Value v;
    v.type = YR_TYPE_BOOL;
    v.i64 = x ? 1 : 0;
    return v;
}

Value v_enum(int64_t x)
{
    Value v;
    v.type = YR_TYPE_ENUM;
    v.i64 = x;
    return v;
}

Value v_string(const std::string& s)
{
    Value v;
    v.type = YR_TYPE_STRING;
    v.str = s;
    return v;
}

uint64_t hz(double f) { return static_cast<uint64_t>(std::llround(f)); }

}  // namespace

std::vector<SchemaDef> truth_schemas()
{
    SchemaDef c;
    c.id = kTruthCellSchema;
    c.label = "TETRA cell (ground truth)";
    c.kind = YR_SCHEMA_MEASUREMENT;
    c.version = 1;
    c.groups = YR_GROUP_QUALITY | YR_GROUP_BROADCAST | YR_GROUP_SIM_POSITION | YR_GROUP_OTHER;
    const uint32_t key = YR_FIELD_KEY | YR_FIELD_DEFAULT;
    c.fields = {
        field(YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, "Hz", "Frequency", key),
        field(YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE, "", "Main carrier", YR_FIELD_DEFAULT),
        field(YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE, "", "Role", YR_FIELD_DEFAULT),
        field(YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, "dBm", "Level", YR_FIELD_DEFAULT),
        field(YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE, "", "MCC", key),
        field(YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE, "", "MNC", key),
        field(YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, "", "Location area", key),
        field(YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE, "", "Colour code", key),
        field(YR_FIELD_SNR_DB, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, "dB", "SNR"),
        field(YR_FIELD_FREQ_ERROR_HZ, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, "Hz", "Frequency error"),
        field("encryption", YR_TYPE_BOOL, YR_GROUP_BROADCAST, "", "Air-interface encryption"),
        field("ms_txpwr_max_cell", YR_TYPE_UINT, YR_GROUP_BROADCAST, "", "MS_TXPWR_MAX_CELL (code)"),
        field("rxlev_access_min", YR_TYPE_UINT, YR_GROUP_BROADCAST, "", "RXLEV_ACCESS_MIN (code)"),
        field(YR_FIELD_SIM_FLOOR_ID, YR_TYPE_STRING, YR_GROUP_SIM_POSITION, "", "Floor"),
        field(YR_FIELD_SIM_X_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, "m", "x"),
        field(YR_FIELD_SIM_Y_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, "m", "y"),
        field("cell_id", YR_TYPE_STRING, YR_GROUP_OTHER, "", "Scenario cell"),
        field("level_inst_dbm", YR_TYPE_DOUBLE, YR_GROUP_OTHER, "dBm", "Instantaneous level"),
        field("level_source", YR_TYPE_STRING, YR_GROUP_OTHER, "", "Level source"),
        field("best_antenna", YR_TYPE_STRING, YR_GROUP_OTHER, "", "Strongest antenna"),
        field("walls", YR_TYPE_UINT, YR_GROUP_OTHER, "", "Walls crossed"),
    };
    c.fields[kRole].items = {{YR_ROLE_SERVING, "serving", "Serving"},
                             {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                             {YR_ROLE_OTHER, "other", "Other"}};

    SchemaDef n;
    n.id = kTruthNeighbourSchema;
    n.label = "TETRA neighbour list entry (ground truth)";
    n.kind = YR_SCHEMA_NEIGHBOUR;
    n.version = 1;
    n.groups = YR_GROUP_NEIGHBOURS;
    n.fields = {
        field(YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, "Hz", "Broadcasting carrier", key),
        field(YR_FIELD_ENTRY_INDEX, YR_TYPE_UINT, YR_GROUP_CORE, "", "Entry", key),
        field(YR_FIELD_ENTRY_COUNT, YR_TYPE_UINT, YR_GROUP_CORE, "", "Entries"),
        field("cell_number", YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, "", "Neighbour cell identifier", YR_FIELD_DEFAULT),
        field("main_carrier", YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, "", "Main carrier", YR_FIELD_DEFAULT),
        field("neighbour_frequency_hz", YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, "Hz", "Neighbour frequency",
              YR_FIELD_DEFAULT),
        field("neighbour_la", YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, "", "Location area (when broadcast)"),
        field("neighbour_cell_id", YR_TYPE_STRING, YR_GROUP_NEIGHBOURS, "", "Scenario cell"),
    };
    return {c, n};
}

radios::RecordFileHeader truth_record_header(const Scenario& s, const std::string& version)
{
    radios::RecordFileHeader h;
    h.producer = "yantra-gen " + version;
    h.created_utc_ns = 0;
    h.plugin.id = "yantra-gen";
    h.plugin.name = "yantra-gen ground truth";
    h.plugin.version = version;
    h.plugin.technology_id = "tetra";
    h.plugin.licence = "GPL-3.0-or-later";
    h.plugin.capabilities = YR_CAP_SIMULATED | YR_CAP_SELF_CALIBRATED;
    radios::PlanInfo plan;
    plan.type = YR_PLAN_CARRIERS;
    plan.flags = 0;
    for (const auto& c : s.cells)
        plan.carriers_hz.push_back(hz(cell_frequency_hz(c)));
    h.plan = plan;
    h.schemas = truth_schemas();
    return h;
}

TruthRecorder::TruthRecorder(const Scenario& s) : s_(s) {}

int64_t TruthRecorder::monotonic_ns(const TruthRow& row)
{
    return static_cast<int64_t>(std::llround(row.t * 1e9));
}

TruthRecords TruthRecorder::convert(const TruthRow& row)
{
    TruthRecords out;
    // Serving: the strongest on-air cell (first one on a tie).
    std::optional<size_t> serving;
    double best = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < row.cells.size(); ++i) {
        const CellTruth& c = row.cells[i];
        if (c.on_air && std::isfinite(c.level_dbm) && c.level_dbm > best) {
            best = c.level_dbm;
            serving = i;
        }
    }
    const ScenarioCell* serving_cell = serving ? s_.find_cell(row.cells[*serving].cell) : nullptr;
    bool list_due = false;
    if (serving && (!serving_ || *serving_ != *serving)) {
        out.serving_changed_hz = hz(row.cells[*serving].frequency_hz);
        list_due = true;
    }
    serving_ = serving;
    if (serving && row.t - last_list_t_ >= kTruthNeighbourIntervalS - 1e-9)
        list_due = true;

    yr_time time{};
    time.monotonic_ns = monotonic_ns(row);
    time.utc_ns = 0;
    time.utc_source = YR_TIME_SOURCE_NONE;

    for (size_t i = 0; i < row.cells.size(); ++i) {
        const CellTruth& c = row.cells[i];
        if (!c.on_air || !std::isfinite(c.level_dbm))
            continue;
        const ScenarioCell* sc = s_.find_cell(c.cell);
        uint32_t role = YR_ROLE_OTHER;
        if (serving && i == *serving) {
            role = YR_ROLE_SERVING;
        } else if (serving_cell) {
            for (const auto& n : serving_cell->neighbours)
                if (n.cell == c.cell)
                    role = YR_ROLE_NEIGHBOUR;
        }
        Record r;
        r.schema_index = 0;
        r.time = time;
        r.flags = YR_RECORD_SIMULATED;
        r.present = YR_GROUP_QUALITY | YR_GROUP_BROADCAST | YR_GROUP_OTHER;
        r.values.resize(kCellFieldCount);
        r.values[kFreq] = v_uint(hz(c.frequency_hz));
        r.values[kChannel] = v_uint(sc ? sc->main_carrier : 0);
        r.values[kRole] = v_enum(role);
        r.values[kLevel] = v_double(c.level_dbm);
        r.values[kMcc] = v_uint(c.mcc);
        r.values[kMnc] = v_uint(c.mnc);
        r.values[kLa] = v_uint(c.location_area);
        r.values[kCc] = v_uint(c.colour_code);
        r.values[kSnr] = v_double(c.snr_db);
        r.values[kFreqError] = v_double(c.cfo_hz);
        if (sc) {
            r.values[kEncryption] = v_bool(sc->encryption);
            r.values[kTxpwr] = v_uint(sc->ms_txpwr_max_cell);
            r.values[kRxlev] = v_uint(sc->rxlev_access_min);
        }
        if (row.position) {
            r.present |= YR_GROUP_SIM_POSITION;
            r.values[kSimFloor] = v_string(row.position->floor);
            r.values[kSimX] = v_double(row.position->x_m);
            r.values[kSimY] = v_double(row.position->y_m);
        }
        r.values[kCellId] = v_string(c.cell);
        r.values[kLevelInst] = v_double(c.level_inst_dbm);
        r.values[kSource] = v_string(to_string(c.source));
        if (!c.best_antenna.empty())
            r.values[kBestAntenna] = v_string(c.best_antenna);
        r.values[kWalls] = v_uint(static_cast<uint64_t>(std::max(0, c.walls)));
        out.records.push_back(std::move(r));
    }

    if (list_due && serving_cell) {
        last_list_t_ = row.t;
        const uint64_t f = hz(row.cells[*serving].frequency_hz);
        const auto& list = serving_cell->neighbours;
        for (size_t k = 0; k < list.size(); ++k) {
            const ScenarioCell* n = s_.find_cell(list[k].cell);
            if (!n)
                continue;
            const uint16_t carrier = list[k].main_carrier.value_or(n->main_carrier);
            Record r;
            r.schema_index = 1;
            r.time = time;
            r.flags = YR_RECORD_SIMULATED;
            r.present = YR_GROUP_NEIGHBOURS;
            r.values.resize(kNeighbourFieldCount);
            r.values[kNFreq] = v_uint(f);
            r.values[kNIndex] = v_uint(k);
            r.values[kNCount] = v_uint(list.size());
            r.values[kNCellNumber] = v_uint(n->cell_number);
            r.values[kNCarrier] = v_uint(carrier);
            r.values[kNFrequency] = v_uint(hz(downlink_frequency_hz(n->frequency_band, carrier, n->offset)));
            if (list[k].location_area)
                r.values[kNLa] = v_uint(*list[k].location_area);
            r.values[kNCellId] = v_string(n->id);
            out.records.push_back(std::move(r));
        }
    }
    return out;
}

}  // namespace yantra::gen
