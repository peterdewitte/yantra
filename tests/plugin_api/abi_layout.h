/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ABI layout of the plugin API on 64-bit targets (PLG-03, PLG-04): sizes,
 * offsets and alignments of every struct. Included by a C11 and a C++20
 * translation unit, so both languages and the compiler agree. A change
 * here is an ABI change: appending a field to a versioned struct is a
 * minor version step, anything else a major one (PLG-04, DOC-18).
 * Copied from yantra-radios fb194f5: tests/radio_api/abi_layout.h,
 * adapted for the plugin API.
 */
#ifndef YANTRA_TEST_PLUGIN_ABI_LAYOUT_H
#define YANTRA_TEST_PLUGIN_ABI_LAYOUT_H

#include <yantra/plugin.h>

#ifdef __cplusplus
#  define YP_TEST_ASSERT(cond, msg) static_assert(cond, msg)
#  define YP_TEST_ALIGNOF(t) alignof(t)
#else
#  define YP_TEST_ASSERT(cond, msg) _Static_assert(cond, msg)
#  define YP_TEST_ALIGNOF(t) _Alignof(t)
#endif

#define YP_TEST_SIZE(t, n) YP_TEST_ASSERT(sizeof(t) == (n), "sizeof(" #t ")")
#define YP_TEST_OFF(t, f, n) YP_TEST_ASSERT(offsetof(t, f) == (n), "offsetof(" #t ", " #f ")")

/* Frozen prefix of the descriptor: the same on every target (PLG-04). */
YP_TEST_OFF(yantra_plugin_descriptor, struct_size, 0);
YP_TEST_OFF(yantra_plugin_descriptor, api_version, 4);
YP_TEST_OFF(yantra_plugin_descriptor, kinds, 12);
YP_TEST_SIZE(yantra_api_version, 8);
YP_TEST_ASSERT(sizeof(yantra_status) == 4, "yantra_status is int32_t");

/* Frozen structs: the same on every target (and as the radio API's). */
YP_TEST_SIZE(yantra_time, 24);
YP_TEST_OFF(yantra_time, utc_ns, 8);
YP_TEST_OFF(yantra_time, utc_source, 16);
YP_TEST_SIZE(yantra_value, 16);
YP_TEST_OFF(yantra_value, u, 8);

/* Constants shared with the radio API keep their values. */
YP_TEST_ASSERT(YANTRA_SCHEMA_MEASUREMENT == 1u && YANTRA_SCHEMA_SCAN == 2u &&
                   YANTRA_SCHEMA_NEIGHBOUR == 3u,
               "schema kinds equal YR_SCHEMA_*");
YP_TEST_ASSERT(YANTRA_TYPE_BOOL == 1u && YANTRA_TYPE_ENUM == 6u, "types equal YR_TYPE_*");
YP_TEST_ASSERT(YANTRA_RECORD_NO_SYNC == 1u && YANTRA_RECORD_SIMULATED == 8u,
               "record flags equal YR_RECORD_*");
YP_TEST_ASSERT(YANTRA_ERR_BUSY == -8 && YANTRA_ERR_VERSION == -9 && YANTRA_ERR_IO == -11,
               "status codes equal YR_ERR_*");

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
/* 64-bit targets (Linux LP64, Windows LLP64): natural layout without
 * hidden padding. */
YP_TEST_ASSERT(YP_TEST_ALIGNOF(yantra_value) == 8, "alignof(yantra_value)");
YP_TEST_ASSERT(YP_TEST_ALIGNOF(yantra_time) == 8, "alignof(yantra_time)");
YP_TEST_SIZE(yantra_enum_item, 24);
YP_TEST_SIZE(yantra_translation, 24);
YP_TEST_SIZE(yantra_field_link, 16);

YP_TEST_SIZE(yantra_field_def, 64);
YP_TEST_OFF(yantra_field_def, category, 8);
YP_TEST_OFF(yantra_field_def, name, 16);
YP_TEST_OFF(yantra_field_def, help, 40);
YP_TEST_OFF(yantra_field_def, enum_items, 48);
YP_TEST_OFF(yantra_field_def, enum_count, 56);

YP_TEST_SIZE(yantra_schema, 40);
YP_TEST_OFF(yantra_schema, id, 8);
YP_TEST_OFF(yantra_schema, version, 24);
YP_TEST_OFF(yantra_schema, fields, 32);

YP_TEST_SIZE(yantra_record, 48);
YP_TEST_OFF(yantra_record, time, 8);
YP_TEST_OFF(yantra_record, flags, 32);
YP_TEST_OFF(yantra_record, value_count, 36);
YP_TEST_OFF(yantra_record, values, 40);

YP_TEST_SIZE(yantra_provenance, 40);
YP_TEST_OFF(yantra_provenance, technology_id, 8);
YP_TEST_OFF(yantra_provenance, schema_id, 32);

YP_TEST_SIZE(yantra_host_api, 128);
YP_TEST_OFF(yantra_host_api, api_version, 8);
YP_TEST_OFF(yantra_host_api, host, 16);
YP_TEST_OFF(yantra_host_api, log, 24);
YP_TEST_OFF(yantra_host_api, settings_get, 40);
YP_TEST_OFF(yantra_host_api, now, 64);
YP_TEST_OFF(yantra_host_api, timer_start, 72);
YP_TEST_OFF(yantra_host_api, emit_record, 88);
YP_TEST_OFF(yantra_host_api, ui_close_window, 104);
YP_TEST_OFF(yantra_host_api, control, 112);
YP_TEST_OFF(yantra_host_api, data_folder, 120);

YP_TEST_SIZE(yantra_band, 56);
YP_TEST_OFF(yantra_band, first_hz, 24);
YP_TEST_OFF(yantra_band, first_channel, 48);
YP_TEST_SIZE(yantra_c1_input, 32);
YP_TEST_OFF(yantra_c1_input, level_dbm, 16);
YP_TEST_SIZE(yantra_reselection_cell, 24);
YP_TEST_SIZE(yantra_reselection_input, 32);
YP_TEST_OFF(yantra_reselection_input, neighbours, 16);
YP_TEST_OFF(yantra_reselection_input, ms_power_dbm, 24);
YP_TEST_SIZE(yantra_reselection_result, 272);
YP_TEST_OFF(yantra_reselection_result, serving_state, 8);
YP_TEST_OFF(yantra_reselection_result, reason, 16);

YP_TEST_SIZE(yantra_technology_api, 128);
YP_TEST_OFF(yantra_technology_api, channel_bandwidth_hz, 24);
YP_TEST_OFF(yantra_technology_api, band_count, 32);
YP_TEST_OFF(yantra_technology_api, bands, 40);
YP_TEST_OFF(yantra_technology_api, cell_schema, 56);
YP_TEST_OFF(yantra_technology_api, link_count, 64);
YP_TEST_OFF(yantra_technology_api, links, 72);
YP_TEST_OFF(yantra_technology_api, serving_states, 80);
YP_TEST_OFF(yantra_technology_api, frequency_to_channel, 88);
YP_TEST_OFF(yantra_technology_api, values_match, 120);

YP_TEST_SIZE(yantra_feature_api, 72);
YP_TEST_OFF(yantra_feature_api, schemas, 32);
YP_TEST_OFF(yantra_feature_api, start, 48);
YP_TEST_OFF(yantra_feature_api, on_record, 64);

YP_TEST_SIZE(yantra_export_format, 40);
YP_TEST_SIZE(yantra_export_request, 32);
YP_TEST_SIZE(yantra_exporter_api, 48);
YP_TEST_OFF(yantra_exporter_api, formats, 8);
YP_TEST_OFF(yantra_exporter_api, abort, 40);

YP_TEST_SIZE(yantra_ui_control, 56);
YP_TEST_OFF(yantra_ui_control, tooltip, 24);
YP_TEST_SIZE(yantra_ui_plot, 72);
YP_TEST_OFF(yantra_ui_plot, width, 16);
YP_TEST_OFF(yantra_ui_plot, x_min, 24);
YP_TEST_SIZE(yantra_ui_table, 40);
YP_TEST_OFF(yantra_ui_table, row_count, 8);
YP_TEST_SIZE(yantra_ui_api, 168);
YP_TEST_OFF(yantra_ui_api, tr, 24);
YP_TEST_OFF(yantra_ui_api, close, 160);
YP_TEST_SIZE(yantra_ui_window_def, 64);
YP_TEST_OFF(yantra_ui_window_def, id, 16);
YP_TEST_OFF(yantra_ui_window_def, default_width, 48);
YP_TEST_SIZE(yantra_ui_menu_item, 64);
YP_TEST_OFF(yantra_ui_menu_item, window_id, 56);
YP_TEST_SIZE(yantra_ui_menu_state, 16);
YP_TEST_SIZE(yantra_plugin_ui_api, 64);
YP_TEST_OFF(yantra_plugin_ui_api, windows, 16);
YP_TEST_OFF(yantra_plugin_ui_api, draw, 32);

YP_TEST_SIZE(yantra_plugin_descriptor, 144);
YP_TEST_OFF(yantra_plugin_descriptor, data_categories, 16);
YP_TEST_OFF(yantra_plugin_descriptor, translation_count, 20);
YP_TEST_OFF(yantra_plugin_descriptor, id, 24);
YP_TEST_OFF(yantra_plugin_descriptor, licence, 56);
YP_TEST_OFF(yantra_plugin_descriptor, translations, 80);
YP_TEST_OFF(yantra_plugin_descriptor, init, 88);
YP_TEST_OFF(yantra_plugin_descriptor, on_timer, 104);
YP_TEST_OFF(yantra_plugin_descriptor, technology, 112);
YP_TEST_OFF(yantra_plugin_descriptor, ui, 136);
#endif

#endif
