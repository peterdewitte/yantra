/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The radio API header in plain C11 (RAD-10): plugins may be written in C.
 * This file checks the ABI layout in C (abi_layout.h) and builds a small
 * but complete plugin descriptor with C static tables only; the C++ tests
 * validate it (test_header.cpp). It is not loaded as a plugin.
 */
#include "abi_layout.h"

#include <yantra/radio.h>

#include <string.h>

const char *yantra_test_radio_entry_symbol_c(void);
const char *yantra_test_radio_entry_symbol_c(void)
{
    return YANTRA_RADIO_ENTRY_SYMBOL;
}

/* --- Record schemas (RAD-21..23) ---------------------------------------- */

static const yr_enum_item role_items[] = {
    {YR_ROLE_SERVING, "serving", "Serving"},
    {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
    {YR_ROLE_OTHER, "other", "Other"},
};

#define FIELD(name, type, group, flags, label, unit, help) \
    {sizeof(yr_field_def), type, group, flags, name, label, unit, help, NULL, 0, 0}

static const yr_field_def f_freq = FIELD(YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE,
                                         YR_FIELD_KEY | YR_FIELD_DEFAULT, "Frequency", "Hz",
                                         "Carrier centre frequency.");
static const yr_field_def f_channel = FIELD(YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE,
                                            YR_FIELD_DEFAULT, "Channel", "",
                                            "Channel number of the band plan.");
static const yr_field_def f_role = {sizeof(yr_field_def), YR_TYPE_ENUM, YR_GROUP_CORE,
                                    YR_FIELD_DEFAULT, YR_FIELD_ROLE, "Role", "",
                                    "Serving, neighbour or other carrier.", role_items, 3, 0};
static const yr_field_def f_level = FIELD(YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE,
                                          YR_FIELD_DEFAULT, "Level", "dBFS",
                                          "Mean carrier power at the current gain.");
static const yr_field_def f_mcc = FIELD(YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY,
                                        "MCC", "", "Mobile country code (sync).");
static const yr_field_def f_mnc = FIELD(YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY,
                                        "MNC", "", "Mobile network code (sync).");
static const yr_field_def f_la = FIELD(YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY,
                                       "LA", "", "Location area (sysinfo).");
static const yr_field_def f_cc = FIELD(YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE,
                                       YR_FIELD_KEY, "Colour code", "", "Colour code (sync).");
static const yr_field_def f_mer = FIELD(YR_FIELD_MER_PCT, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0,
                                        "MER", "%", "Message erasure rate over 100 slots.");
static const yr_field_def f_hyper = FIELD(YR_FIELD_HYPERFRAME, YR_TYPE_UINT, YR_GROUP_FRAME, 0,
                                          "Hyperframe", "", "Hyperframe number.");
static const yr_field_def f_c1 = FIELD(YR_FIELD_C1_DB, YR_TYPE_DOUBLE, YR_GROUP_C1, 0, "C1",
                                       "dB", "Path loss criterion C1.");

static const yr_field_def *const measurement_fields[] = {
    &f_freq, &f_channel, &f_role, &f_level, &f_mcc, &f_mnc, &f_la, &f_cc, &f_mer, &f_hyper, &f_c1,
};
static const yr_schema measurement_schema = {
    sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "beacon_measurement", "Beacon measurement", 1,
    (uint32_t)(sizeof measurement_fields / sizeof measurement_fields[0]), measurement_fields,
    YR_GROUP_QUALITY | YR_GROUP_FRAME | YR_GROUP_C1, 0};

static const yr_field_def *const scan_fields[] = {
    &f_freq, &f_channel, &f_role, &f_level, &f_mcc, &f_mnc, &f_la, &f_cc,
};
static const yr_schema scan_schema = {sizeof(yr_schema), YR_SCHEMA_SCAN, "beacon_scan",
                                      "Beacon scan", 1, 8, scan_fields, 0, 0};

static const yr_field_def f_entry_index = FIELD(YR_FIELD_ENTRY_INDEX, YR_TYPE_UINT, YR_GROUP_CORE,
                                                0, "Entry", "", "Index in the list.");
static const yr_field_def f_entry_count = FIELD(YR_FIELD_ENTRY_COUNT, YR_TYPE_UINT, YR_GROUP_CORE,
                                                0, "Entries", "", "Entries in the list.");
static const yr_field_def f_cell_id = FIELD("cell_id", YR_TYPE_UINT, YR_GROUP_NEIGHBOURS,
                                            YR_FIELD_KEY, "Cell id", "",
                                            "Neighbour cell identifier (D-NWRK-BROADCAST).");
static const yr_field_def *const neighbour_fields[] = {&f_freq, &f_entry_index, &f_entry_count,
                                                       &f_cell_id};
static const yr_schema neighbour_schema = {sizeof(yr_schema), YR_SCHEMA_NEIGHBOUR,
                                           "beacon_neighbour", "Beacon neighbour", 1, 4,
                                           neighbour_fields, YR_GROUP_NEIGHBOURS, 0};

static const yr_schema *const schemas[] = {&measurement_schema, &scan_schema, &neighbour_schema};

static const yr_translation translations[] = {
    {"setting.gain.label", "nl", "Versterking"},
    {"setting.gain.label", "fr", "Gain"},
    {"setting.gain.label", "de", "Verstaerkung"},
};

/* --- Stub functions: a C plugin fills the same tables ------------------- */

static yr_status c_init(const yr_host_api *host, yr_plugin **out)
{
    (void)host;
    *out = NULL;
    return YR_ERR_UNSUPPORTED;
}
static void c_shutdown(yr_plugin *plugin)
{
    (void)plugin;
}
static yr_status c_enumerate(yr_plugin *plugin, yr_device_cb cb, void *user)
{
    (void)plugin;
    (void)cb;
    (void)user;
    return YR_ERR_NO_DEVICE;
}
static yr_status c_open(yr_plugin *plugin, const char *key, yr_device **out)
{
    (void)plugin;
    (void)key;
    *out = NULL;
    return YR_ERR_NO_DEVICE;
}
static void c_close(yr_device *device)
{
    (void)device;
}
static yr_status c_device_info(yr_device *device, yr_device_cb cb, void *user)
{
    (void)device;
    (void)cb;
    (void)user;
    return YR_ERR_UNSUPPORTED;
}
static uint32_t c_state(yr_device *device)
{
    (void)device;
    return YR_DEVICE_CLOSED;
}
static yr_status c_plan_ranges(yr_device *device, yr_plan_ranges_cb cb, void *user)
{
    (void)device;
    (void)cb;
    (void)user;
    return YR_ERR_UNSUPPORTED;
}
static yr_status c_set_plan(yr_device *device, const yr_plan *plan)
{
    (void)device;
    (void)plan;
    return YR_ERR_UNSUPPORTED;
}
static yr_status c_start(yr_device *device, const yr_plan *plan, yr_stream *sink)
{
    (void)device;
    (void)plan;
    (void)sink;
    return YR_ERR_UNSUPPORTED;
}
static yr_status c_stop(yr_device *device)
{
    (void)device;
    return YR_OK;
}
static yr_status c_stats(yr_device *device, yr_stream_stats *out)
{
    (void)device;
    (void)out;
    return YR_ERR_UNSUPPORTED;
}
static yr_status c_last_error(yr_plugin *plugin, yr_device *device, char *buffer, uint32_t size)
{
    (void)plugin;
    (void)device;
    if (buffer == NULL || size == 0)
        return YR_ERR_BUFFER_TOO_SMALL;
    buffer[0] = '\0';
    return YR_OK;
}
static yr_status c_list(yr_device *device, yr_setting_cb cb, void *user)
{
    (void)device;
    (void)cb;
    (void)user;
    return YR_OK;
}
static yr_status c_get(yr_device *device, const char *key, yr_value *out, char *text,
                       uint32_t text_size)
{
    (void)device;
    (void)key;
    (void)text;
    (void)text_size;
    *out = yr_value_none();
    return YR_ERR_NOT_FOUND;
}
static yr_status c_set(yr_device *device, const char *key, const yr_value *value)
{
    (void)device;
    (void)key;
    (void)value;
    return YR_ERR_NOT_FOUND;
}

static const yr_stream_control stream_control = {
    sizeof(yr_stream_control), 0, c_enumerate, c_open, c_close, c_device_info, c_state,
    c_plan_ranges, c_set_plan, c_start, c_stop, c_stats, c_last_error};
static const yr_device_control device_control = {
    sizeof(yr_device_control), 0, c_list, c_get, c_set, NULL, NULL, NULL, NULL, NULL, NULL};

static const yr_descriptor descriptor = {
    sizeof(yr_descriptor),
    {YR_API_VERSION_MAJOR, YR_API_VERSION_MINOR},
    YR_CAP_SCAN,
    "c-test",
    "C test plugin",
    "0.1.0",
    "The Yantra contributors",
    "GPL-3.0-or-later",
    "A descriptor written in C (radio API compile test).",
    "https://example.invalid/source",
    "beacon",
    3,
    3,
    schemas,
    translations,
    c_init,
    c_shutdown,
    &stream_control,
    &device_control,
    NULL,
};

const yr_descriptor *yantra_test_c_descriptor(void);
const yr_descriptor *yantra_test_c_descriptor(void)
{
    return &descriptor;
}

/* --- Header helpers used from C ----------------------------------------- */

int yantra_test_c_helpers(void);
int yantra_test_c_helpers(void)
{
    yr_value v = yr_value_double(-71.5);
    yr_stream_stats stats;
    yr_api_version host = yr_api_version_compiled();
    yr_api_version newer = host;
    int ok = 1;
    memset(&stats, 0, sizeof stats);
    stats.struct_size = (uint32_t)offsetof(yr_stream_stats, receiver_load);
    newer.minor += 1;
    ok = ok && v.type == YR_TYPE_DOUBLE && v.u.f64 == -71.5;
    ok = ok && yr_value_none().type == YR_TYPE_NONE;
    ok = ok && yr_value_bool(5).u.i64 == 1;
    ok = ok && yr_value_string("x").u.str[0] == 'x';
    ok = ok && yr_api_compatible(host, host) && !yr_api_compatible(host, newer);
    ok = ok && YR_HAS_FIELD(&stats, yr_stream_stats, overflows);
    ok = ok && !YR_HAS_FIELD(&stats, yr_stream_stats, receiver_load);
    ok = ok && strcmp(yr_status_name(YR_ERR_BUSY), "BUSY") == 0;
    {
        /* 1.2 (RAD-140): a 1.1 host table has no spectrum sink; a 1.0/1.1
         * plan is read with the band zero. */
        yr_host_api h;
        yr_plan small, kept;
        memset(&h, 0, sizeof h);
        h.struct_size = (uint32_t)offsetof(yr_host_api, push_spectrum);
        ok = ok && yr_host_spectrum_sink(&h) == NULL && yr_host_spectrum_sink(NULL) == NULL;
        memset(&small, 0xFF, sizeof small);
        small.struct_size = (uint32_t)offsetof(yr_plan, spectrum_first_hz);
        small.type = YR_PLAN_SERVING;
        yr_plan_read(&small, &kept);
        ok = ok && kept.struct_size == sizeof(yr_plan) && kept.type == YR_PLAN_SERVING &&
             kept.spectrum_first_hz == 0 && kept.spectrum_last_hz == 0;
        ok = ok && (YR_CAP_ALL_MASK & YR_CAP_SPECTRUM) != 0 && (YR_CAP_ALL_MASK_1_1 & YR_CAP_SPECTRUM) == 0;
    }
    return ok;
}
