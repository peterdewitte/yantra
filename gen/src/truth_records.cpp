// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/gen/truth_records.hpp"

#include "yantra/gen/downlink.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace yantra::gen {

using radios::Record;
using radios::SchemaDef;

namespace {

const radios::TetraSchemaOptions kTruthOptions{.level_dbm = true, .simulated = true, .scan = false};

uint64_t hz(double f) { return static_cast<uint64_t>(std::llround(f)); }

/// MS_TXPWR_MAX_CELL and RXLEV_ACCESS_MIN codes in dBm (EN 300 392-2
/// 21.4.4.1).
int txpwr_dbm(uint8_t code) { return code == 0 ? 0 : 15 + 5 * (code - 1); }
int rxlev_dbm(uint8_t code) { return -125 + 5 * code; }

}  // namespace

std::vector<SchemaDef> truth_schemas()
{
    return radios::tetra_schemas(kTruthOptions);
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

TruthRecorder::TruthRecorder(const Scenario& s)
    : s_(s), cell_(radios::tetra_cell_schema(kTruthOptions), 0),
      neighbour_(radios::tetra_neighbour_schema(kTruthOptions), 1)
{
}

int64_t TruthRecorder::monotonic_ns(const TruthRow& row)
{
    return static_cast<int64_t>(std::llround(row.t * 1e9));
}

TruthRecords TruthRecorder::convert(const TruthRow& row)
{
    using namespace radios;
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
        Record r = cell_.start(time, YR_RECORD_SIMULATED | YR_RECORD_LEVEL_DBM);
        cell_.set(r, YR_FIELD_FREQUENCY_HZ, value_uint(hz(c.frequency_hz)));
        cell_.set(r, YR_FIELD_CHANNEL, value_uint(sc ? sc->main_carrier : 0));
        cell_.set(r, YR_FIELD_ROLE, value_enum(role));
        cell_.set(r, YR_FIELD_LEVEL, value_double(c.level_dbm));
        cell_.set(r, YR_FIELD_MCC, value_uint(c.mcc));
        cell_.set(r, YR_FIELD_MNC, value_uint(c.mnc));
        cell_.set(r, YR_FIELD_LA, value_uint(c.location_area));
        cell_.set(r, YR_FIELD_COLOUR_CODE, value_uint(c.colour_code));
        cell_.set(r, YR_FIELD_SNR_DB, value_double(c.snr_db));
        cell_.set(r, YR_FIELD_FREQ_ERROR_HZ, value_double(c.cfo_hz));
        if (c.frequency_hz > 0.0)
            cell_.set(r, YR_FIELD_FREQ_ERROR_PPM, value_double(c.cfo_hz / c.frequency_hz * 1e6));
        if (sc) {
            cell_.set(r, "encryption", value_bool(sc->encryption));
            cell_.set(r, "ms_txpwr_max_cell", value_uint(sc->ms_txpwr_max_cell));
            cell_.set(r, "ms_txpwr_max_cell_dbm", value_int(txpwr_dbm(sc->ms_txpwr_max_cell)));
            cell_.set(r, "rxlev_access_min", value_uint(sc->rxlev_access_min));
            cell_.set(r, "rxlev_access_min_dbm", value_int(rxlev_dbm(sc->rxlev_access_min)));
            cell_.set(r, "main_carrier", value_uint(sc->main_carrier));
            cell_.set(r, "frequency_band", value_uint(sc->frequency_band));
            cell_.set(r, YR_FIELD_C1_DB,
                      value_double(tetra_c1_db(c.level_dbm, rxlev_dbm(sc->rxlev_access_min),
                                               txpwr_dbm(sc->ms_txpwr_max_cell))));
        }
        if (row.position) {
            cell_.set(r, YR_FIELD_SIM_FLOOR_ID, value_string(row.position->floor));
            cell_.set(r, YR_FIELD_SIM_X_M, value_double(row.position->x_m));
            cell_.set(r, YR_FIELD_SIM_Y_M, value_double(row.position->y_m));
        }
        cell_.set(r, "cell_id", value_string(c.cell));
        cell_.set(r, "level_inst_dbm", value_double(c.level_inst_dbm));
        cell_.set(r, "level_source", value_string(to_string(c.source)));
        if (!c.best_antenna.empty())
            cell_.set(r, "best_antenna", value_string(c.best_antenna));
        cell_.set(r, "walls", value_uint(static_cast<uint64_t>(std::max(0, c.walls))));
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
            Record r = neighbour_.start(time, YR_RECORD_SIMULATED | YR_RECORD_LEVEL_DBM);
            neighbour_.set(r, YR_FIELD_FREQUENCY_HZ, value_uint(f));
            neighbour_.set(r, YR_FIELD_ENTRY_INDEX, value_uint(k));
            neighbour_.set(r, YR_FIELD_ENTRY_COUNT, value_uint(list.size()));
            neighbour_.set(r, "cell_number", value_uint(n->cell_number));
            neighbour_.set(r, "main_carrier", value_uint(carrier));
            neighbour_.set(r, "neighbour_frequency_hz",
                           value_uint(hz(downlink_frequency_hz(n->frequency_band, carrier, n->offset))));
            if (list[k].location_area)
                neighbour_.set(r, "neighbour_la", value_uint(*list[k].location_area));
            neighbour_.set(r, "neighbour_cell_id", value_string(n->id));
            out.records.push_back(std::move(r));
        }
    }
    return out;
}

}  // namespace yantra::gen
