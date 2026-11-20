/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The example technology plugin (PLG-15): a complete, small technology
 * plugin for the made-up "beacon" technology, written in plain C against
 * the public header yantra/plugin.h only. docs/PLUGINS.md builds it step
 * by step; the blocks between "[snippet:...]" markers are quoted there and
 * checked by ctest (doc_snippets), so the tutorial always shows this code.
 *
 * The beacon technology is NOT a real radio standard. Its records come
 * from the example radio plugin of the radio parts (RAD-64, plugin id
 * "beacon"), which makes its own beacon signal, measures it and delivers
 * records through the radio API. That radio plugin is also this
 * technology's simulated radio (PLG-11), and
 * testdata/beacon.records.jsonl is a golden file it recorded.
 *
 * A technology plugin does no signal processing (PLG-02). It tells Yantra:
 *   1. what the records of its radios look like (schemas, PLG-05),
 *      field by field matching the radio's schemas of the same id;
 *   2. its band plan (bands, channel <-> frequency);
 *   3. the technology rules Yantra applies: C1 (RF-06), cell
 *      reselection (DB-08), parameter verification (DB-04) and the
 *      neighbour cross-check (DB-05);
 *   4. optionally its own UI (PLG-06): here a channel calculator under
 *      Tools, translated to Dutch, French and German.
 *
 * Threads (PLG-07): the entry point, init and shutdown run on Yantra's
 * main thread; the rule functions are pure (no state, no blocking) and may
 * run on any thread; draw runs on the UI thread. Only draw touches the
 * instance state, so no lock is needed.
 */
#include <yantra/plugin.h>

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * 1. Schemas (PLG-05)
 *
 * The radio plugin "beacon" delivers two record schemas, "beacon" (one
 * measured beacon) and "beacon_neighbour" (one entry of the neighbour list
 * a beacon broadcasts). The technology schemas have the same ids and the
 * same field names, types and units, so Yantra can copy every radio record
 * into them by name (check_radio_schema proves it). Two differences are
 * allowed and used: the level is "dBm" here (Yantra calibrates the radio's
 * dBFS), and the technology adds what the radio API has no word for: the
 * data category of each field (PLG-08), which fields form the cell
 * identity (KEY), which are broadcast parameters to verify (VERIFY), and
 * which may be absent (OPTIONAL: the radio's optional groups).
 * ------------------------------------------------------------------------ */

/* [snippet:schema] */
static const yantra_enum_item roles[] = {
    {1, "serving", "Serving"}, {2, "neighbour", "Neighbour"}, {3, "other", "Other"}};
static const yantra_enum_item classes[] = {
    {0, "indoor", "Indoor"}, {1, "outdoor", "Outdoor"}, {2, "test", "Test"}};

#define SYSINFO YANTRA_CATEGORY_SYSTEM_INFO
#define FIELD(name, type, category, flags, label, unit, help) \
    {sizeof(yantra_field_def), type, category, flags, name, label, unit, help, NULL, 0, 0}
#define ENUM_FIELD(name, flags, label, help, items) \
    {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, SYSINFO, flags, name, label, "", help, \
     items, sizeof items / sizeof items[0], 0}

/* The cell: the eight radio core fields (RAD-21), then the quality values
 * and the broadcast parameters of the radio's optional groups. A beacon is
 * identified by its network code and beacon id; the channel may change. */
static const yantra_field_def beacon_fields[] = {
    FIELD("frequency_hz", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_DEFAULT, "Frequency", "Hz",
          "Centre frequency of the beacon channel."),
    FIELD("channel", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_DEFAULT, "Channel", "",
          "Beacon channel: frequency = 430 MHz + channel x 25 kHz."),
    ENUM_FIELD("role", YANTRA_FIELD_DEFAULT, "Role",
               "Serving: the beacon followed; neighbour: announced by it.", roles),
    FIELD("level", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL, YANTRA_FIELD_DEFAULT, "Level",
          "dBm", "Received level of the beacon frame, calibrated by Yantra."),
    FIELD("mcc", YANTRA_TYPE_UINT, SYSINFO, 0, "MCC", "",
          "Country code: always 901 (the beacon network is made up)."),
    FIELD("mnc", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY, "Network", "",
          "Network code broadcast in the frame."),
    FIELD("la", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT,
          "Beacon id", "", "Beacon id broadcast in the frame."),
    FIELD("colour_code", YANTRA_TYPE_UINT, SYSINFO, 0, "Colour code", "",
          "Not used by beacons: always 0."),
    FIELD("snr_db", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY, YANTRA_FIELD_OPTIONAL, "SNR",
          "dB", "Frame power over the noise power measured before the frame."),
    FIELD("freq_error_hz", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY, YANTRA_FIELD_OPTIONAL,
          "Frequency error", "Hz", "Carrier offset measured on the preamble."),
    ENUM_FIELD("beacon_class", YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY | YANTRA_FIELD_DEFAULT,
               "Class", "Beacon class broadcast in the frame.", classes),
    FIELD("tx_power_dbm", YANTRA_TYPE_INT, SYSINFO, YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY,
          "TX power", "dBm", "Transmit power the beacon announces."),
    FIELD("sequence", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_OPTIONAL, "Sequence", "",
          "Frame counter of the beacon (wraps at 256)."),
};
/* Field indexes used by the rules below (the record's value order). */
enum { F_LEVEL = 3, F_TX_POWER = 11, BEACON_FIELD_COUNT = 13 };

/* One entry of a beacon's broadcast neighbour list (RAD-22). */
static const yantra_field_def neighbour_fields[] = {
    FIELD("frequency_hz", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY, "Broadcast on", "Hz",
          "Channel of the beacon that broadcast the list."),
    FIELD("entry_index", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY, "Entry", "",
          "Position in the broadcast neighbour list, from 0."),
    FIELD("entry_count", YANTRA_TYPE_UINT, SYSINFO, 0, "Entries", "",
          "Number of neighbours in the list."),
    FIELD("neighbour_id", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_DEFAULT, "Neighbour id", "",
          "Beacon id of the announced neighbour."),
};

/* Schemas and lists of versioned structs are arrays of pointers (PLG-03). */
static const yantra_field_def* const beacon_field_list[] = {
    &beacon_fields[0], &beacon_fields[1], &beacon_fields[2],  &beacon_fields[3],
    &beacon_fields[4], &beacon_fields[5], &beacon_fields[6],  &beacon_fields[7],
    &beacon_fields[8], &beacon_fields[9], &beacon_fields[10], &beacon_fields[11],
    &beacon_fields[12]};
static const yantra_field_def* const neighbour_field_list[] = {
    &neighbour_fields[0], &neighbour_fields[1], &neighbour_fields[2], &neighbour_fields[3]};

static const yantra_schema beacon_schema = {
    sizeof(yantra_schema), YANTRA_SCHEMA_MEASUREMENT, "beacon", "Beacon", 1, BEACON_FIELD_COUNT,
    beacon_field_list};
static const yantra_schema neighbour_schema = {
    sizeof(yantra_schema), YANTRA_SCHEMA_NEIGHBOUR, "beacon_neighbour", "Beacon neighbour", 1, 4,
    neighbour_field_list};
static const yantra_schema* const schemas[] = {&beacon_schema, &neighbour_schema};
enum { CELL_SCHEMA = 0, NEIGHBOUR_SCHEMA = 1 };

/* The neighbour cross-check (DB-05): an entry's neighbour_id is the "la"
 * (beacon id) of the cell it announces; the other KEY field of the cell,
 * the network code, is that of the beacon that broadcast the list. */
static const yantra_field_link links[] = {{"neighbour_id", "la"}};
/* [/snippet] */

/* ------------------------------------------------------------------------
 * 2. Band plan
 *
 * Eight channels of 25 kHz above 430 MHz, numbered 1..8. With bands alone
 * Yantra can already convert channels and frequencies; the two optional
 * functions below do the same in code, to show the pattern for a
 * technology whose raster is not a plain grid.
 * ------------------------------------------------------------------------ */

/* [snippet:bands] */
#define BEACON_BASE_HZ 430000000u
#define BEACON_RASTER_HZ 25000u
#define BEACON_FIRST_CHANNEL 1u
#define BEACON_LAST_CHANNEL 8u

static const yantra_band band_430 = {
    sizeof(yantra_band), 0, "beacon_430", "Beacon 430 MHz",
    BEACON_BASE_HZ + BEACON_FIRST_CHANNEL * BEACON_RASTER_HZ, /* first channel centre */
    BEACON_BASE_HZ + BEACON_LAST_CHANNEL * BEACON_RASTER_HZ,  /* last channel centre */
    BEACON_RASTER_HZ, BEACON_FIRST_CHANNEL};
static const yantra_band* const bands[] = {&band_430};

static yantra_status frequency_to_channel(yantra_plugin_instance* instance, uint64_t hz,
                                          uint64_t* out_channel)
{
    uint64_t channel;
    (void)instance;
    if (out_channel == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (hz < BEACON_BASE_HZ || (hz - BEACON_BASE_HZ) % BEACON_RASTER_HZ != 0)
        return YANTRA_ERR_NOT_FOUND;
    channel = (hz - BEACON_BASE_HZ) / BEACON_RASTER_HZ;
    if (channel < BEACON_FIRST_CHANNEL || channel > BEACON_LAST_CHANNEL)
        return YANTRA_ERR_NOT_FOUND;
    *out_channel = channel;
    return YANTRA_OK;
}

static yantra_status channel_to_frequency(yantra_plugin_instance* instance, uint64_t channel,
                                          uint64_t* out_hz)
{
    (void)instance;
    if (out_hz == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (channel < BEACON_FIRST_CHANNEL || channel > BEACON_LAST_CHANNEL)
        return YANTRA_ERR_NOT_FOUND;
    *out_hz = BEACON_BASE_HZ + channel * BEACON_RASTER_HZ;
    return YANTRA_OK;
}
/* [/snippet] */

/* ------------------------------------------------------------------------
 * 3. Technology rules (RF-06, DB-08, DB-04)
 *
 * Pure functions: they read only their arguments, keep no state and never
 * block, so Yantra may call them from any thread at any time. Records
 * arrive in the cell schema's value order; a value may be absent
 * (YANTRA_TYPE_NONE) when the radio did not deliver it.
 * ------------------------------------------------------------------------ */

/* [snippet:c1] */
/* Made-up beacon access rule: a terminal may use a beacon when
 *   C1 = (level - BEACON_ACCESS_MIN_DBM) - max(0, MS power - beacon TX power)
 * is positive (a weak beacon that transmits less than the terminal is
 * penalised, like TETRA's C1 penalises a cell the MS cannot reach). */
#define BEACON_ACCESS_MIN_DBM (-100.0)

static int c1_of(const yantra_record* record, double level_dbm, double ms_power_dbm,
                 double* out_c1)
{
    const yantra_value* tx;
    double penalty;
    if (record == NULL || record->value_count <= F_TX_POWER)
        return 0;
    tx = &record->values[F_TX_POWER];
    if (tx->type != YANTRA_TYPE_INT)
        return 0; /* the beacon's TX power was not received */
    penalty = ms_power_dbm - (double)tx->u.i64;
    *out_c1 = (level_dbm - BEACON_ACCESS_MIN_DBM) - (penalty > 0.0 ? penalty : 0.0);
    return 1;
}

static yantra_status compute_c1(yantra_plugin_instance* instance, const yantra_c1_input* input,
                                double* out_c1_db)
{
    (void)instance;
    if (input == NULL || out_c1_db == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    return c1_of(input->record, input->level_dbm, input->ms_power_dbm, out_c1_db)
               ? YANTRA_OK
               : YANTRA_ERR_UNSUPPORTED;
}
/* [/snippet] */

/* [snippet:reselect] */
/* Serving-cell states of reselection (DB-08) and the made-up rule: the
 * terminal moves to the neighbour with the best C1 when it beats the
 * serving beacon by BEACON_HYSTERESIS_DB. */
#define BEACON_HYSTERESIS_DB 4.0
enum { STATE_USABLE = 0, STATE_WEAK = 1, STATE_LOST = 2 };
static const yantra_enum_item serving_states[] = {
    {STATE_USABLE, "usable", "Usable"},
    {STATE_WEAK, "weak", "Weak (C1 below 10 dB)"},
    {STATE_LOST, "lost", "Lost (C1 below 0 dB)"},
};

static yantra_status reselect(yantra_plugin_instance* instance,
                              const yantra_reselection_input* in, yantra_reselection_result* out)
{
    double serving_c1;
    double best;
    uint32_t i;
    (void)instance;
    if (in == NULL || out == NULL || in->serving == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (!c1_of(in->serving->record, in->serving->level_dbm, in->ms_power_dbm, &serving_c1))
        return YANTRA_ERR_UNSUPPORTED;
    out->serving_state = serving_c1 < 0.0    ? STATE_LOST
                         : serving_c1 < 10.0 ? STATE_WEAK
                                             : STATE_USABLE;
    out->chosen = -1;
    best = serving_c1 + BEACON_HYSTERESIS_DB;
    for (i = 0; i < in->neighbour_count; ++i) {
        double c1;
        const yantra_reselection_cell* n = in->neighbours[i];
        if (n != NULL && c1_of(n->record, n->level_dbm, in->ms_power_dbm, &c1) && c1 > best) {
            best = c1;
            out->chosen = (int32_t)i;
        }
    }
    if (out->chosen >= 0)
        snprintf(out->reason, sizeof out->reason,
                 "neighbour %d has C1 %.1f dB, more than %.0f dB above the serving %.1f dB",
                 (int)out->chosen, best, BEACON_HYSTERESIS_DB, serving_c1);
    else
        snprintf(out->reason, sizeof out->reason,
                 "no neighbour beats the serving C1 %.1f dB by %.0f dB", serving_c1,
                 BEACON_HYSTERESIS_DB);
    return YANTRA_OK;
}
/* [/snippet] */

/* [snippet:values_match] */
/* Parameter verification (DB-04): the announced TX power may differ by
 * 1 dB from the stored value (rounding in the beacon); everything else
 * must be equal. */
static yantra_status values_match(yantra_plugin_instance* instance, uint32_t schema_index,
                                  uint32_t field_index, const yantra_value* received,
                                  const yantra_value* stored, int32_t* out_equal)
{
    (void)instance;
    if (received == NULL || stored == NULL || out_equal == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (received->type != stored->type) {
        *out_equal = 0;
        return YANTRA_OK;
    }
    if (schema_index == CELL_SCHEMA && field_index == F_TX_POWER &&
        received->type == YANTRA_TYPE_INT) {
        const int64_t d = received->u.i64 - stored->u.i64;
        *out_equal = d >= -1 && d <= 1;
        return YANTRA_OK;
    }
    switch (received->type) {
    case YANTRA_TYPE_UINT: *out_equal = received->u.u64 == stored->u.u64; break;
    case YANTRA_TYPE_DOUBLE: *out_equal = received->u.f64 == stored->u.f64; break;
    case YANTRA_TYPE_STRING: *out_equal = strcmp(received->u.str, stored->u.str) == 0; break;
    default: *out_equal = received->u.i64 == stored->u.i64; break;
    }
    return YANTRA_OK;
}
/* [/snippet] */

/* [snippet:technology] */
static const yantra_technology_api technology = {
    sizeof(yantra_technology_api),
    0,
    "beacon", /* = the radio plugin's technology_id (yr_descriptor) */
    "Beacon (example)",
    12500.0, /* occupied bandwidth of one channel, Hz */
    1,       /* bands */
    2,       /* schemas */
    bands,
    schemas,
    CELL_SCHEMA,
    NEIGHBOUR_SCHEMA,
    1, /* links */
    3, /* serving states */
    links,
    serving_states,
    frequency_to_channel,
    channel_to_frequency,
    compute_c1,
    reselect,
    values_match,
};
/* [/snippet] */

/* ------------------------------------------------------------------------
 * 4. UI (PLG-06): Tools > Beacon channels
 *
 * The plugin declares its window and menu entry as data; Yantra draws the
 * controls itself, so they follow the app's theme, touch layout and
 * language. Every control has a tooltip (DOC-07), and every English text
 * has Dutch, French and German translations (beacon_translations.inc).
 * ------------------------------------------------------------------------ */

/* [snippet:ui] */
struct yantra_plugin_instance {
    const yantra_host_api* host;
    double channel; /* the calculator's channel; only draw (UI thread) uses it */
};

static const yantra_ui_window_def channels_window = {
    sizeof(yantra_ui_window_def), YANTRA_UI_WINDOW, 0, 0, "beacon_channels", "Beacon channels",
    "Converts beacon channel numbers to frequencies (made-up beacon band plan).", NULL, 360.0,
    0.0};
static const yantra_ui_window_def* const windows[] = {&channels_window};
static const yantra_ui_menu_item channels_menu = {
    sizeof(yantra_ui_menu_item), YANTRA_MENU_TOOLS, 0, 0, "beacon_channels_open", NULL,
    "Beacon channels", "Opens the beacon channel calculator.", NULL, "beacon_channels"};
static const yantra_ui_menu_item* const menu_items[] = {&channels_menu};

static void draw(yantra_plugin_instance* instance, const char* window_id, const yantra_ui_api* ui)
{
    static const yantra_ui_control channel = {
        sizeof(yantra_ui_control), 0, "channel", "Channel", "Channel number, 1 to 8.", NULL,
        NULL, NULL};
    static const yantra_ui_control frequency = {
        sizeof(yantra_ui_control), 0, "frequency", "Frequency",
        "Centre frequency of the channel: 430 MHz + channel x 25 kHz.", NULL, NULL, NULL};
    static const yantra_ui_control plan = {sizeof(yantra_ui_control), 0, "band_plan", "Band plan",
                                           "Every channel of the beacon band.", NULL, NULL, NULL};
    static const char* const headers[] = {"Channel", "Frequency (MHz)"};
    static const char* const header_tooltips[] = {"Beacon channel number.",
                                                  "Centre frequency in MHz."};
    char text[32];
    char cells[2 * BEACON_LAST_CHANNEL][16];
    const char* cell_list[2 * BEACON_LAST_CHANNEL];
    yantra_ui_table table;
    uint64_t hz = 0;
    uint32_t c;
    (void)window_id; /* the only window */

    ui->heading(ui->ui, "Channel calculator", NULL);
    ui->number(ui->ui, &channel, &instance->channel, BEACON_FIRST_CHANNEL, BEACON_LAST_CHANNEL,
               1.0, "");
    if (channel_to_frequency(instance, (uint64_t)instance->channel, &hz) == YANTRA_OK)
        snprintf(text, sizeof text, "%.3f", (double)hz / 1e6);
    else
        snprintf(text, sizeof text, "-");
    ui->value(ui->ui, &frequency, text, "MHz");

    ui->separator(ui->ui);
    for (c = 0; c < BEACON_LAST_CHANNEL; ++c) {
        channel_to_frequency(instance, c + BEACON_FIRST_CHANNEL, &hz);
        snprintf(cells[2 * c], sizeof cells[0], "%u", (unsigned)(c + BEACON_FIRST_CHANNEL));
        snprintf(cells[2 * c + 1], sizeof cells[0], "%.3f", (double)hz / 1e6);
        cell_list[2 * c] = cells[2 * c];
        cell_list[2 * c + 1] = cells[2 * c + 1];
    }
    table.struct_size = sizeof table;
    table.column_count = 2;
    table.row_count = BEACON_LAST_CHANNEL;
    table.headers = headers;
    table.header_tooltips = header_tooltips;
    table.cells = cell_list;
    ui->table(ui->ui, &plan, &table);
}

static const yantra_plugin_ui_api ui_table = {
    sizeof(yantra_plugin_ui_api), 1, 1, 0, windows, menu_items, draw, NULL, NULL, NULL};
/* [/snippet] */

/* ------------------------------------------------------------------------
 * 5. Descriptor and entry point (PLG-03, PLG-04, PLG-08, PLG-10)
 * ------------------------------------------------------------------------ */

static const yantra_translation translations[] = {
#include "beacon_translations.inc"
};

/* [snippet:descriptor] */
/* One instance per load (Yantra loads a plugin once); static storage. */
static struct yantra_plugin_instance the_instance;

static yantra_status beacon_init(const yantra_host_api* host,
                                 yantra_plugin_instance** out_instance)
{
    if (host == NULL || out_instance == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    /* A host older than this plugin's API 1.0 tables cannot run it. */
    if (!YANTRA_HAS_FIELD(host, yantra_host_api, ui_close_window))
        return YANTRA_ERR_VERSION;
    the_instance.host = host;
    the_instance.channel = BEACON_FIRST_CHANNEL;
    *out_instance = &the_instance;
    host->log(host->host, YANTRA_LOG_INFO, "beacon technology ready");
    return YANTRA_OK;
}

static void beacon_shutdown(yantra_plugin_instance* instance)
{
    instance->host = NULL;
}

static const yantra_plugin_descriptor descriptor = {
    sizeof(yantra_plugin_descriptor),
    {YANTRA_PLUGIN_API_VERSION_MAJOR, YANTRA_PLUGIN_API_VERSION_MINOR},
    YANTRA_KIND_TECHNOLOGY,
    /* Every category a field uses, nothing else (PLG-08). */
    YANTRA_CATEGORY_SYSTEM_INFO | YANTRA_CATEGORY_LEVEL | YANTRA_CATEGORY_QUALITY,
    sizeof translations / sizeof translations[0],
    "yantra.example.beacon",
    "Beacon technology (example)",
    "1.0.0",
    "The Yantra contributors",
    "GPL-3.0-or-later", /* a GPL-3.0-compatible SPDX expression (PLG-10) */
    "Example technology plugin for the made-up beacon of the radio parts.",
    NULL, /* source_url: none for an example */
    translations,
    beacon_init,
    beacon_shutdown,
    NULL, /* on_timer: no timers */
    &technology,
    NULL, /* feature */
    NULL, /* exporter */
    &ui_table,
};
/* [/snippet] */

/* [snippet:entry] */
/* The only exported symbol (PLG-03). It only checks the version and hands
 * over the descriptor; Yantra checks the rest and then calls init. */
YANTRA_PLUGIN_EXPORT const yantra_plugin_descriptor* yantra_plugin_entry(
    const yantra_host_api* host)
{
    if (host == NULL || host->api_version.major != YANTRA_PLUGIN_API_VERSION_MAJOR) {
        if (host != NULL && host->log != NULL)
            host->log(host->host, YANTRA_LOG_ERROR, "beacon: needs plugin API 1.x");
        return NULL;
    }
    return &descriptor;
}
/* [/snippet] */
