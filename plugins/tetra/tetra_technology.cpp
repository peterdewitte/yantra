// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA technology plugin (PLG-02, PLG-05, PLG-11): the record schemas of
// the TETRA radio plugins, the channel raster, C1 (RF-06), cell
// reselection (DB-08), parameter verification (DB-04) and the neighbour
// links (DB-05). Everything TETRA-specific of Yantra lives here; the core
// knows none of it (PLG-01). C++ behind the C ABI of plugin.h: static
// tables, no allocation and no exceptions in the rule functions, which are
// pure (PLG-07). docs/plugins/TETRA.md describes it.

#include <yantra/plugin.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include "tetra_calculator.h"
#include "tetra_raster.h"
#include "tetra_rules.h"

#ifndef YANTRA_TETRA_PLUGIN_VERSION
#error "YANTRA_TETRA_PLUGIN_VERSION must be defined by the build"
#endif

namespace {

namespace yt = yantra::tetra;

// ---------------------------------------------------------------------------
// 1. Schemas (PLG-05)
// ---------------------------------------------------------------------------

#include "tetra_schemas.inc"

enum : std::uint32_t { kCellSchema = 0, kNeighbourSchema = 1, kScanSchema = 2, kTerminalSchema = 3 };
constexpr const yantra_schema* kSchemas[] = {&kTetraCell, &kTetraNeighbour, &kTetraScan,
                                             &kTetraTerminalCell};

// A cell is the main carrier of a location area of a network: KEY fields
// mcc, mnc, la and frequency_hz (DB-01; the colour code is not part of it,
// as no neighbour list carries it, and is verified instead, DB-04).
//
// DB-05: a neighbour list entry names its cell by the main carrier
// frequency; the other KEY fields come from the broadcasting cell
// (plugin.h, yantra_field_link). D-NWRK-BROADCAST sends the neighbour's
// MCC, MNC and LA only when it chooses to (EN 300 392-2 18.4.1.4.1), and a
// linked field must be present, so they are not linked: an entry whose
// neighbour lies in another location area resolves to the broadcaster's
// LA (docs/plugins/TETRA.md, an open question for the cell database).
constexpr yantra_field_link kLinks[] = {
    {"neighbour_frequency_hz", "frequency_hz"},
};

// ---------------------------------------------------------------------------
// 2. Bands and channel raster (tetra_raster.h)
// ---------------------------------------------------------------------------

constexpr yantra_band yantra_band_of(const yt::BandDef& b)
{
    return {sizeof(yantra_band),
            0,
            b.id,
            b.label,
            b.band * yt::kBandHz + b.first_carrier * yt::kRasterHz,
            b.band * yt::kBandHz + b.last_carrier * yt::kRasterHz,
            yt::kRasterHz,
            yt::channel_number(b.band, b.first_carrier)};
}
constexpr yantra_band kBand0 = yantra_band_of(yt::kBands[0]);
constexpr yantra_band kBand1 = yantra_band_of(yt::kBands[1]);
constexpr yantra_band kBand2 = yantra_band_of(yt::kBands[2]);
constexpr yantra_band kBand3 = yantra_band_of(yt::kBands[3]);
constexpr yantra_band kBand4 = yantra_band_of(yt::kBands[4]);
constexpr const yantra_band* kBandList[] = {&kBand0, &kBand1, &kBand2, &kBand3, &kBand4};
static_assert(sizeof kBandList / sizeof kBandList[0] == yt::kBandCount);

yantra_status frequency_to_channel(yantra_plugin_instance*, std::uint64_t hz, std::uint64_t* out) noexcept
{
    if (out == nullptr)
        return YANTRA_ERR_INVALID_ARGUMENT;
    const auto ch = yt::channel_of(hz);
    if (!ch)
        return YANTRA_ERR_NOT_FOUND;
    *out = *ch;
    return YANTRA_OK;
}

yantra_status channel_to_frequency(yantra_plugin_instance*, std::uint64_t channel, std::uint64_t* out) noexcept
{
    if (out == nullptr)
        return YANTRA_ERR_INVALID_ARGUMENT;
    const auto hz = yt::frequency_of(channel);
    if (!hz)
        return YANTRA_ERR_NOT_FOUND;
    *out = *hz;
    return YANTRA_OK;
}

// ---------------------------------------------------------------------------
// 3. Rules: C1, reselection, verification (tetra_rules.h)
// ---------------------------------------------------------------------------

const yantra_value* value_at(const yantra_record* r, std::uint32_t field, std::uint32_t type) noexcept
{
    if (r == nullptr || r->values == nullptr || field >= r->value_count || r->values[field].type != type)
        return nullptr;
    return &r->values[field];
}

// The access parameters of a tetra_cell record; empty when it is another
// schema or the cell's MAC-SYSINFO was not decoded.
std::optional<yt::AccessParameters> access_of(const yantra_record* r) noexcept
{
    if (r == nullptr || r->schema_index != kCellSchema)
        return std::nullopt;
    const yantra_value* rx = value_at(r, kTetraCell_rxlev_access_min_dbm, YANTRA_TYPE_INT);
    const yantra_value* tx = value_at(r, kTetraCell_ms_txpwr_max_cell_dbm, YANTRA_TYPE_INT);
    if (rx == nullptr || tx == nullptr)
        return std::nullopt;
    return yt::AccessParameters{static_cast<double>(rx->u.i64), static_cast<double>(tx->u.i64)};
}

bool usable_power(double ms_power_dbm) noexcept
{
    return std::isfinite(ms_power_dbm) && ms_power_dbm > 0.0 && ms_power_dbm < 100.0;
}

yantra_status compute_c1(yantra_plugin_instance*, const yantra_c1_input* in, double* out) noexcept
{
    if (in == nullptr || out == nullptr || in->struct_size < sizeof(yantra_c1_input) || in->record == nullptr)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (!std::isfinite(in->level_dbm) || !usable_power(in->ms_power_dbm))
        return YANTRA_ERR_INVALID_ARGUMENT;
    const auto a = access_of(in->record);
    if (!a)
        return YANTRA_ERR_UNSUPPORTED;
    *out = yt::c1_db(in->level_dbm, *a, in->ms_power_dbm);
    return YANTRA_OK;
}

std::optional<yt::ReselectionThresholds> thresholds_of(const yantra_record* r) noexcept
{
    const yantra_value* srt = value_at(r, kTetraCell_srt_db, YANTRA_TYPE_UINT);
    const yantra_value* srh = value_at(r, kTetraCell_srh_db, YANTRA_TYPE_UINT);
    const yantra_value* frt = value_at(r, kTetraCell_frt_db, YANTRA_TYPE_UINT);
    const yantra_value* frh = value_at(r, kTetraCell_frh_db, YANTRA_TYPE_UINT);
    if (srt == nullptr || srh == nullptr || frt == nullptr || frh == nullptr)
        return std::nullopt;
    return yt::ReselectionThresholds{static_cast<double>(srt->u.u64), static_cast<double>(srh->u.u64),
                                     static_cast<double>(frt->u.u64), static_cast<double>(frh->u.u64)};
}

constexpr yantra_enum_item kServingStates[] = {
    {static_cast<std::int64_t>(yt::ServingState::Exclusive), "exclusive", "Exclusive"},
    {static_cast<std::int64_t>(yt::ServingState::Superior), "superior", "Superior"},
    {static_cast<std::int64_t>(yt::ServingState::Usable), "usable", "Usable"},
    {static_cast<std::int64_t>(yt::ServingState::Improvable), "improvable", "Improvable"},
    {static_cast<std::int64_t>(yt::ServingState::Relinquishable), "relinquishable", "Relinquishable"},
    {static_cast<std::int64_t>(yt::ServingState::RadioLinkFailure), "radio_link_failure", "Radio link failure"},
};

struct NeighbourContext {
    const yantra_reselection_input* in;
};

std::optional<double> neighbour_c2(const void* context, std::size_t i) noexcept
{
    const auto* ctx = static_cast<const NeighbourContext*>(context);
    const yantra_reselection_cell* n = ctx->in->neighbours[i];
    if (n == nullptr || !std::isfinite(n->level_dbm))
        return std::nullopt;
    const auto a = access_of(n->record);
    if (!a)
        return std::nullopt;
    return yt::c1_db(n->level_dbm, *a, ctx->in->ms_power_dbm);
}

yantra_status reselect(yantra_plugin_instance*, const yantra_reselection_input* in,
                       yantra_reselection_result* out) noexcept
{
    if (in == nullptr || out == nullptr || in->serving == nullptr ||
        (in->neighbour_count > 0 && in->neighbours == nullptr))
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (!usable_power(in->ms_power_dbm) || !std::isfinite(in->serving->level_dbm))
        return YANTRA_ERR_INVALID_ARGUMENT;
    const auto a = access_of(in->serving->record);
    if (!a)
        return YANTRA_ERR_UNSUPPORTED;
    const double c1 = yt::c1_db(in->serving->level_dbm, *a, in->ms_power_dbm);
    const NeighbourContext ctx{in};
    const auto r = yt::reselect(c1, thresholds_of(in->serving->record), in->neighbour_count, neighbour_c2, &ctx);
    if (!r)
        return YANTRA_ERR_UNSUPPORTED;
    out->chosen = r->target;
    out->serving_state = static_cast<std::int64_t>(r->state);
    std::snprintf(out->reason, sizeof out->reason, "%s", r->reason);
    return YANTRA_OK;
}

// DB-04: the dBm parameters broadcast as a code match when the stored
// value stands for the received code; everything else must be equal.
yantra_status values_match(yantra_plugin_instance*, std::uint32_t schema_index, std::uint32_t field_index,
                           const yantra_value* received, const yantra_value* stored, std::int32_t* out) noexcept
{
    if (received == nullptr || stored == nullptr || out == nullptr ||
        schema_index >= sizeof kSchemas / sizeof kSchemas[0] || field_index >= kSchemas[schema_index]->field_count)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (received->type != stored->type) {
        *out = 0;
        return YANTRA_OK;
    }
    if (schema_index == kCellSchema && received->type == YANTRA_TYPE_INT) {
        const yt::CodeGrid* grid = field_index == kTetraCell_rxlev_access_min_dbm    ? &yt::kRxlevAccessMinGrid
                                   : field_index == kTetraCell_ms_txpwr_max_cell_dbm ? &yt::kMsTxpwrMaxCellGrid
                                   : field_index == kTetraCell_access_parameter_dbm  ? &yt::kAccessParameterGrid
                                                                                     : nullptr;
        if (grid != nullptr) {
            *out = yt::grid_values_match(received->u.i64, stored->u.i64, *grid) ? 1 : 0;
            return YANTRA_OK;
        }
    }
    switch (received->type) {
    case YANTRA_TYPE_NONE: *out = 1; break;
    case YANTRA_TYPE_UINT: *out = received->u.u64 == stored->u.u64; break;
    case YANTRA_TYPE_DOUBLE: *out = received->u.f64 == stored->u.f64; break;
    case YANTRA_TYPE_STRING:
        *out = received->u.str != nullptr && stored->u.str != nullptr && std::strcmp(received->u.str, stored->u.str) == 0;
        break;
    default: *out = received->u.i64 == stored->u.i64; break;
    }
    return YANTRA_OK;
}

constexpr yantra_technology_api kTechnology = {
    sizeof(yantra_technology_api),
    0,
    "tetra",  // = yr_descriptor::technology_id of the TETRA radio plugins
    "TETRA",
    25000.0,  // channel bandwidth, Hz (EN 300 392-2 clause 5)
    yt::kBandCount,
    sizeof kSchemas / sizeof kSchemas[0],
    kBandList,
    kSchemas,
    kCellSchema,
    kNeighbourSchema,
    sizeof kLinks / sizeof kLinks[0],
    sizeof kServingStates / sizeof kServingStates[0],
    kLinks,
    kServingStates,
    frequency_to_channel,
    channel_to_frequency,
    compute_c1,
    reselect,
    values_match,
};

// ---------------------------------------------------------------------------
// 4. Descriptor and entry point (PLG-03, PLG-04, PLG-08, PLG-10)
// ---------------------------------------------------------------------------

constexpr yantra_translation kTranslations[] = {
#include "tetra_translations.inc"
};

}  // namespace

struct yantra_plugin_instance {
    const yantra_host_api* host;
    yantra::tetra::CalculatorState calculator;  // the tools window (DB-07); UI thread only
};

namespace {

yantra_plugin_instance the_instance{};

yantra_status tetra_init(const yantra_host_api* host, yantra_plugin_instance** out) noexcept
{
    if (host == nullptr || out == nullptr)
        return YANTRA_ERR_INVALID_ARGUMENT;
    the_instance.host = host;
    *out = &the_instance;
    return YANTRA_OK;
}

void tetra_shutdown(yantra_plugin_instance* instance) noexcept
{
    if (instance != nullptr)
        instance->host = nullptr;
}

// ---------------------------------------------------------------------------
// 5. Tools window (DB-07, PLG-06): tetra_calculator.h
// ---------------------------------------------------------------------------

constexpr yantra_ui_window_def kCalculatorWindowDef = {
    sizeof(yantra_ui_window_def), YANTRA_UI_WINDOW, 0, 0, yt::kCalculatorWindow, "TETRA calculator",
    "Channel <-> frequency (band, offset, duplex spacing, reverse operation) and the subscriber-class bitmask of "
    "TETRA cells (DB-07).",
    nullptr, 420.0, 0.0};
constexpr const yantra_ui_window_def* kWindows[] = {&kCalculatorWindowDef};
constexpr yantra_ui_menu_item kCalculatorMenu = {
    sizeof(yantra_ui_menu_item), YANTRA_MENU_TOOLS, 0, 0, "calculator_open", nullptr, "TETRA calculator",
    "Opens the TETRA channel, frequency and subscriber-class calculator (DB-07).", nullptr, yt::kCalculatorWindow};
constexpr const yantra_ui_menu_item* kMenuItems[] = {&kCalculatorMenu};

void tetra_draw(yantra_plugin_instance* instance, const char* window_id, const yantra_ui_api* ui) noexcept
{
    if (instance == nullptr || ui == nullptr || window_id == nullptr ||
        std::strcmp(window_id, yt::kCalculatorWindow) != 0)
        return;
    yt::draw_calculator(instance->calculator, ui);
}

constexpr yantra_plugin_ui_api kUi = {
    sizeof(yantra_plugin_ui_api), 1, 1, 0, kWindows, kMenuItems, tetra_draw, nullptr, nullptr, nullptr};

constexpr yantra_plugin_descriptor kDescriptor = {
    sizeof(yantra_plugin_descriptor),
    {YANTRA_PLUGIN_API_VERSION_MAJOR, YANTRA_PLUGIN_API_VERSION_MINOR},
    YANTRA_KIND_TECHNOLOGY,
    // Every category a field uses, nothing else (PLG-08).
    YANTRA_CATEGORY_SYSTEM_INFO | YANTRA_CATEGORY_LEVEL | YANTRA_CATEGORY_QUALITY | YANTRA_CATEGORY_PLAN_POSITION,
    sizeof kTranslations / sizeof kTranslations[0],
    "yantra.tetra",
    "TETRA",
    YANTRA_TETRA_PLUGIN_VERSION,
    "The Yantra contributors",
    "GPL-3.0-or-later",
    "TETRA cell records, channel raster, C1, cell reselection and parameter verification (EN 300 392-2).",
    nullptr,  // source_url: shipped with Yantra, its source is Yantra's
    kTranslations,
    tetra_init,
    tetra_shutdown,
    nullptr,  // on_timer
    &kTechnology,
    nullptr,  // feature
    nullptr,  // exporter
    &kUi,  // the TETRA calculator (DB-07)
};

}  // namespace

// The only exported symbol (PLG-03): checks the API major version and
// hands over the descriptor; Yantra checks the rest and then calls init.
extern "C" YANTRA_PLUGIN_EXPORT const yantra_plugin_descriptor* yantra_plugin_entry(const yantra_host_api* host)
{
    if (host == nullptr || host->api_version.major != YANTRA_PLUGIN_API_VERSION_MAJOR) {
        if (host != nullptr && host->log != nullptr)
            host->log(host->host, YANTRA_LOG_ERROR, "tetra: needs plugin API 1.x");
        return nullptr;
    }
    return &kDescriptor;
}
