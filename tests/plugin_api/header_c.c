/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The plugin API header in plain C11 (PLG-03): plugins may be written in
 * C. This file checks the ABI layout in C (abi_layout.h) and implements a
 * small but complete plugin with static tables only: a made-up "ctest"
 * technology (bands, cell and neighbour schemas, C1, reselection), a
 * feature with a recording schema, an exporter and a UI panel with a
 * menu entry. It is linked into the tests, not loaded as a plugin.
 * The network codes are made up (MCC 901 is not a country).
 */
#include "abi_layout.h"
#include "header_c.h"

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
#error "header_c.c must be compiled as C"
#endif

/* --- Cell schema: the radio core fields plus a parameter -------------- */

static const yantra_enum_item role_items[] = {
    {1, "serving", "Serving"},
    {2, "neighbour", "Neighbour"},
    {3, "other", "Other"},
};

#define FIELD(var, nm, ty, cat, fl, lbl, un, hlp)                                        \
    static const yantra_field_def var = {sizeof(yantra_field_def), ty, cat, fl, nm, lbl, un, \
                                         hlp, NULL, 0, 0}

FIELD(f_frequency, "frequency_hz", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
      YANTRA_FIELD_DEFAULT, "Frequency", "Hz", "Carrier frequency.");
FIELD(f_channel, "channel", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, YANTRA_FIELD_DEFAULT,
      "Channel", "", "Channel number of the band plan.");
static const yantra_field_def f_role = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM,
                                        YANTRA_CATEGORY_SYSTEM_INFO, YANTRA_FIELD_DEFAULT,
                                        "role", "Role", "", "Serving, neighbour or other.",
                                        role_items, 3, 0};
FIELD(f_level, "level", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL, YANTRA_FIELD_DEFAULT, "Level",
      "dBm", "Calibrated signal level.");
FIELD(f_mcc, "mcc", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
      YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "MCC", "", "Country code.");
FIELD(f_mnc, "mnc", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
      YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "MNC", "", "Network code.");
FIELD(f_la, "la", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
      YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "LA", "", "Location area.");
FIELD(f_cc, "colour_code", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, YANTRA_FIELD_VERIFY,
      "Colour code", "", "Colour code.");
FIELD(f_rx_min, "rx_level_min", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL,
      YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "Minimum RX level", "dBm",
      "Broadcast minimum receive level for access.");
FIELD(f_mer, "mer_pct", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY, YANTRA_FIELD_OPTIONAL,
      "MER", "%", "Message erasure rate.");

static const yantra_field_def* const cell_fields[] = {
    &f_frequency, &f_channel, &f_role, &f_level, &f_mcc, &f_mnc, &f_la, &f_cc, &f_rx_min, &f_mer,
};
static const yantra_schema cell_schema = {sizeof(yantra_schema), YANTRA_SCHEMA_MEASUREMENT,
                                          "ctest_cell", "Cell", 1, 10, cell_fields};

/* --- Neighbour schema (one list entry) -------------------------------- */

FIELD(n_frequency, "frequency_hz", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
      YANTRA_FIELD_KEY, "Broadcast on", "Hz", "Carrier that broadcast the list.");
FIELD(n_index, "entry_index", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, YANTRA_FIELD_KEY,
      "Entry", "", "0-based entry index.");
FIELD(n_count, "entry_count", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, 0, "Entries", "",
      "Entries in this list.");
FIELD(n_la, "nb_la", YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, YANTRA_FIELD_DEFAULT,
      "Neighbour LA", "", "Location area of the neighbour.");

static const yantra_field_def* const neighbour_fields[] = {&n_frequency, &n_index, &n_count,
                                                           &n_la};
static const yantra_schema neighbour_schema = {sizeof(yantra_schema), YANTRA_SCHEMA_NEIGHBOUR,
                                               "ctest_neighbour", "Neighbour", 1, 4,
                                               neighbour_fields};

static const yantra_schema* const tech_schemas[] = {&cell_schema, &neighbour_schema};

static const yantra_band band_a = {sizeof(yantra_band), 0, "ctest_430", "430 MHz test band",
                                   430000000u, 430975000u, 25000u, 100u};
static const yantra_band* const bands[] = {&band_a};

static const yantra_field_link links[] = {{"nb_la", "la"}};

static const yantra_enum_item serving_states[] = {
    {0, "usable", "Usable"},
    {1, "relinquishable", "Relinquishable"},
};

/* --- Technology rules (pure) ------------------------------------------ */

static yantra_status c1_of(const yantra_record* r, double level_dbm, double* out)
{
    if (r == NULL || r->value_count <= 8 || r->values[8].type != YANTRA_TYPE_DOUBLE)
        return YANTRA_ERR_UNSUPPORTED;
    *out = level_dbm - r->values[8].u.f64;
    return YANTRA_OK;
}

static yantra_status tech_compute_c1(yantra_plugin_instance* instance,
                                     const yantra_c1_input* input, double* out_c1_db)
{
    (void)instance;
    if (input == NULL || out_c1_db == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    return c1_of(input->record, input->level_dbm, out_c1_db);
}

static yantra_status tech_reselect(yantra_plugin_instance* instance,
                                   const yantra_reselection_input* in,
                                   yantra_reselection_result* out)
{
    double serving_c1 = 0.0;
    double best = 0.0;
    uint32_t i;
    (void)instance;
    if (in == NULL || out == NULL || in->serving == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (c1_of(in->serving->record, in->serving->level_dbm, &serving_c1) != YANTRA_OK)
        return YANTRA_ERR_UNSUPPORTED;
    out->chosen = -1;
    best = serving_c1 + 3.0; /* made-up 3 dB hysteresis */
    for (i = 0; i < in->neighbour_count; ++i) {
        double c1 = 0.0;
        if (c1_of(in->neighbours[i]->record, in->neighbours[i]->level_dbm, &c1) == YANTRA_OK &&
            c1 > best) {
            best = c1;
            out->chosen = (int32_t)i;
        }
    }
    out->serving_state = out->chosen >= 0 ? 1 : 0;
    snprintf(out->reason, sizeof out->reason, "%s", out->chosen >= 0
                                                       ? "a neighbour is 3 dB better"
                                                       : "no neighbour is 3 dB better");
    return YANTRA_OK;
}

static const yantra_technology_api technology = {
    sizeof(yantra_technology_api), 0, "ctest", "C test technology", 25000.0, 1, 2, bands,
    tech_schemas, 0, 1, 1, 2, links, serving_states,
    NULL, NULL, tech_compute_c1, tech_reselect, NULL};

/* --- Feature with a recording schema ----------------------------------- */

FIELD(r_temp, "device_temperature", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_DEVICE,
      YANTRA_FIELD_DEFAULT, "Probe temperature", "degC", "Temperature of the measuring probe.");
static const yantra_field_def* const recording_fields[] = {&r_temp};
static const yantra_schema recording_schema = {sizeof(yantra_schema), YANTRA_SCHEMA_RECORDING,
                                               "ctest_health", "Probe health", 1, 1,
                                               recording_fields};
static const yantra_schema* const feature_schemas[] = {&recording_schema};

struct yantra_plugin_instance {
    const yantra_host_api* host;
    int records_seen;
};

static void feature_on_record(yantra_plugin_instance* instance, const yantra_provenance* p,
                              const yantra_schema* schema, const yantra_record* record)
{
    (void)p;
    (void)schema;
    (void)record;
    ++instance->records_seen;
}

static const yantra_feature_api feature = {
    sizeof(yantra_feature_api), 0, "ctest_feature", "C test feature", 1, 0, feature_schemas,
    NULL, NULL, NULL, feature_on_record};

/* --- Exporter: counts rows ---------------------------------------------- */

static const yantra_export_format format_rows = {sizeof(yantra_export_format),
                                                 YANTRA_EXPORT_BATCH, "ctest_rows", "Row count",
                                                 "txt", "Writes the number of rows per table."};
static const yantra_export_format* const formats[] = {&format_rows};

struct yantra_export_job {
    uint64_t rows;
};
static struct yantra_export_job the_job;

static yantra_status exp_begin(yantra_plugin_instance* instance, const yantra_export_request* r,
                               yantra_export_job** out_job)
{
    (void)instance;
    if (r == NULL || out_job == NULL || strcmp(r->format_id, "ctest_rows") != 0)
        return YANTRA_ERR_INVALID_ARGUMENT;
    the_job.rows = 0;
    *out_job = &the_job;
    return YANTRA_OK;
}
static yantra_status exp_write(yantra_export_job* job, const yantra_provenance* p,
                               const yantra_schema* s, const yantra_record* const* rows,
                               uint64_t row_count)
{
    (void)p;
    (void)s;
    (void)rows;
    job->rows += row_count;
    return YANTRA_OK;
}
static yantra_status exp_finish(yantra_export_job* job)
{
    return job->rows > 0 ? YANTRA_OK : YANTRA_ERR_FAILED;
}
static void exp_abort(yantra_export_job* job)
{
    job->rows = 0;
}

static const yantra_exporter_api exporter = {sizeof(yantra_exporter_api), 1, formats, exp_begin,
                                             exp_write, exp_finish, exp_abort};

/* --- UI: one panel opened from the View menu ---------------------------- */

static const yantra_ui_window_def panel = {sizeof(yantra_ui_window_def), YANTRA_UI_PANEL, 0, 0,
                                           "ctest_panel", "C test panel",
                                           "Shows what the C test plugin has seen.", NULL, 0.0,
                                           0.0};
static const yantra_ui_window_def* const windows[] = {&panel};
static const yantra_ui_menu_item menu_open = {sizeof(yantra_ui_menu_item), YANTRA_MENU_VIEW, 0,
                                              0, "ctest_show", NULL, "C test panel",
                                              "Opens the C test panel.", NULL, "ctest_panel"};
static const yantra_ui_menu_item* const menu_items[] = {&menu_open};

static int draw_count;

static void ui_draw(yantra_plugin_instance* instance, const char* window_id,
                    const yantra_ui_api* ui)
{
    static const yantra_ui_control reset = {sizeof(yantra_ui_control), 0, "reset", "Reset",
                                            "Sets the record counter to zero.", NULL, NULL, NULL};
    static const yantra_ui_control count = {sizeof(yantra_ui_control), 0, "records", "Records",
                                            "Records seen since the last reset.", NULL, NULL,
                                            NULL};
    char line[64];
    (void)window_id;
    ++draw_count;
    snprintf(line, sizeof line, "%d", instance->records_seen);
    ui->heading(ui->ui, "Records", NULL);
    ui->value(ui->ui, &count, line, "");
    if (ui->button(ui->ui, &reset))
        instance->records_seen = 0;
}

static const yantra_plugin_ui_api ui_table = {sizeof(yantra_plugin_ui_api), 1, 1, 0, windows,
                                              menu_items, ui_draw, NULL, NULL, NULL};

/* --- Descriptor ---------------------------------------------------------- */

static const yantra_translation translations[] = {
    {"C test panel", "nl", "C-testpaneel"},
    {"field.ctest_cell.level.label", "de", "Pegel"},
};

static struct yantra_plugin_instance the_instance;

static yantra_status plugin_init(const yantra_host_api* host, yantra_plugin_instance** out)
{
    if (host == NULL || out == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (!YANTRA_HAS_FIELD(host, yantra_host_api, ui_close_window))
        return YANTRA_ERR_VERSION;
    the_instance.host = host;
    the_instance.records_seen = 0;
    *out = &the_instance;
    host->log(host->host, YANTRA_LOG_INFO, "ctest plugin ready");
    return YANTRA_OK;
}
static void plugin_shutdown(yantra_plugin_instance* instance)
{
    instance->host = NULL;
}

static const yantra_plugin_descriptor descriptor = {
    sizeof(yantra_plugin_descriptor),
    {YANTRA_PLUGIN_API_VERSION_MAJOR, YANTRA_PLUGIN_API_VERSION_MINOR},
    YANTRA_KIND_TECHNOLOGY | YANTRA_KIND_FEATURE | YANTRA_KIND_EXPORTER,
    YANTRA_CATEGORY_SYSTEM_INFO | YANTRA_CATEGORY_LEVEL | YANTRA_CATEGORY_QUALITY |
        YANTRA_CATEGORY_DEVICE,
    2,
    "org.example.ctest",
    "C test plugin",
    "1.0.0",
    "The Yantra contributors",
    "GPL-3.0-or-later",
    "A plugin of every kind written in C, for the API tests.",
    NULL,
    translations,
    plugin_init,
    plugin_shutdown,
    NULL,
    &technology,
    &feature,
    &exporter,
    &ui_table,
};

static int entry_count;

YANTRA_PLUGIN_EXPORT const yantra_plugin_descriptor* yantra_plugin_entry(
    const yantra_host_api* host)
{
    ++entry_count;
    if (host == NULL || host->api_version.major != YANTRA_PLUGIN_API_VERSION_MAJOR)
        return NULL;
    return &descriptor;
}

/* --- Helpers for the C++ tests ------------------------------------------ */

size_t yantra_test_c_sizeof(int which)
{
    switch (which) {
    case 0: return sizeof(yantra_plugin_descriptor);
    case 1: return sizeof(yantra_host_api);
    case 2: return sizeof(yantra_value);
    case 3: return sizeof(yantra_record);
    case 4: return sizeof(yantra_technology_api);
    case 5: return sizeof(yantra_ui_api);
    case 6: return sizeof(yantra_reselection_result);
    default: return 0;
    }
}

int yantra_test_c_compatible(uint32_t host_major, uint32_t host_minor, uint32_t plugin_major,
                             uint32_t plugin_minor)
{
    yantra_api_version host;
    yantra_api_version plugin;
    host.major = host_major;
    host.minor = host_minor;
    plugin.major = plugin_major;
    plugin.minor = plugin_minor;
    return yantra_api_compatible(host, plugin);
}

int yantra_test_c_draw_count(void)
{
    return draw_count;
}

int yantra_test_c_entry_count(void)
{
    return entry_count;
}
