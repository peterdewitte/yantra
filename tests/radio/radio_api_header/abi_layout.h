/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ABI layout of the radio API on 64-bit targets (RAD-10, RAD-11): sizes,
 * offsets and alignments of every struct. Included by a C11 and a C++20
 * translation unit, so both languages and the compiler agree. A change
 * here is an ABI change: appending a field to a versioned struct is a
 * minor version step, anything else a major one (RAD-11).
 */
#ifndef YANTRA_TEST_ABI_LAYOUT_H
#define YANTRA_TEST_ABI_LAYOUT_H

#include <yantra/radio.h>

#ifdef __cplusplus
#  define YR_TEST_ASSERT(cond, msg) static_assert(cond, msg)
#  define YR_TEST_ALIGNOF(t) alignof(t)
#else
#  define YR_TEST_ASSERT(cond, msg) _Static_assert(cond, msg)
#  define YR_TEST_ALIGNOF(t) _Alignof(t)
#endif

#define YR_TEST_SIZE(t, n) YR_TEST_ASSERT(sizeof(t) == (n), "sizeof(" #t ")")
#define YR_TEST_OFF(t, f, n) YR_TEST_ASSERT(offsetof(t, f) == (n), "offsetof(" #t ", " #f ")")

/* Frozen prefix of the descriptor: the same on every target (RAD-11). */
YR_TEST_OFF(yr_descriptor, struct_size, 0);
YR_TEST_OFF(yr_descriptor, api_version, 4);
YR_TEST_OFF(yr_descriptor, capabilities, 12);
YR_TEST_SIZE(yr_api_version, 8);
YR_TEST_ASSERT(sizeof(yr_status) == 4, "yr_status is int32_t");

/* Frozen structs: the same on every target. */
YR_TEST_SIZE(yr_time, 24);
YR_TEST_OFF(yr_time, utc_ns, 8);
YR_TEST_OFF(yr_time, utc_source, 16);
YR_TEST_SIZE(yr_value, 16);
YR_TEST_OFF(yr_value, u, 8);
YR_TEST_SIZE(yr_freq_range, 16);

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
/* 64-bit targets (Linux LP64, Windows LLP64): natural layout without
 * hidden padding. */
YR_TEST_ASSERT(YR_TEST_ALIGNOF(yr_value) == 8, "alignof(yr_value)");
YR_TEST_ASSERT(YR_TEST_ALIGNOF(yr_time) == 8, "alignof(yr_time)");
YR_TEST_SIZE(yr_enum_item, 24);
YR_TEST_SIZE(yr_translation, 24);

YR_TEST_SIZE(yr_field_def, 64);
YR_TEST_OFF(yr_field_def, group, 8);
YR_TEST_OFF(yr_field_def, name, 16);
YR_TEST_OFF(yr_field_def, enum_items, 48);
YR_TEST_OFF(yr_field_def, enum_count, 56);

YR_TEST_SIZE(yr_schema, 48);
YR_TEST_OFF(yr_schema, id, 8);
YR_TEST_OFF(yr_schema, version, 24);
YR_TEST_OFF(yr_schema, fields, 32);
YR_TEST_OFF(yr_schema, groups, 40);

YR_TEST_SIZE(yr_record, 56);
YR_TEST_OFF(yr_record, time, 8);
YR_TEST_OFF(yr_record, flags, 32);
YR_TEST_OFF(yr_record, present, 36);
YR_TEST_OFF(yr_record, value_count, 40);
YR_TEST_OFF(yr_record, values, 48);

YR_TEST_SIZE(yr_floor_plan_info, 664);
YR_TEST_OFF(yr_floor_plan_info, width_m, 8);
YR_TEST_OFF(yr_floor_plan_info, plan_name, 24);
YR_TEST_OFF(yr_floor_plan_info, example_id, 280);
YR_TEST_OFF(yr_floor_plan_info, floor_id, 344);
YR_TEST_OFF(yr_floor_plan_info, floor_name, 408);

YR_TEST_SIZE(yr_plan_position, 352);
YR_TEST_OFF(yr_plan_position, x_m, 8);
YR_TEST_OFF(yr_plan_position, moved_monotonic_ns, 24);
YR_TEST_OFF(yr_plan_position, floor_id, 32);
YR_TEST_OFF(yr_plan_position, floor_name, 96);

/* 1.2 appended push_spectrum (112 -> 120); 112 stays the 1.0 minimum. */
YR_TEST_SIZE(yr_host_api, 120);
YR_TEST_OFF(yr_host_api, api_version, 4);
YR_TEST_OFF(yr_host_api, host, 16);
YR_TEST_OFF(yr_host_api, log, 24);
YR_TEST_OFF(yr_host_api, push_record, 48);
YR_TEST_OFF(yr_host_api, stream_event, 56);
YR_TEST_OFF(yr_host_api, settings_changed, 72);
YR_TEST_OFF(yr_host_api, get_floor_plan, 80);
YR_TEST_OFF(yr_host_api, get_scene, 104);
YR_TEST_OFF(yr_host_api, push_spectrum, 112);

YR_TEST_SIZE(yr_device_info, 64);
YR_TEST_OFF(yr_device_info, key, 8);
YR_TEST_OFF(yr_device_info, flags_reason, 56);

/* 1.2 appended the spectrum band (56 -> 72); 56 stays the 1.0 minimum. */
YR_TEST_SIZE(yr_plan, 72);
YR_TEST_OFF(yr_plan, serving_hz, 8);
YR_TEST_OFF(yr_plan, carriers_hz, 16);
YR_TEST_OFF(yr_plan, carrier_count, 24);
YR_TEST_OFF(yr_plan, scan_first_hz, 32);
YR_TEST_OFF(yr_plan, scan_step_hz, 48);
YR_TEST_OFF(yr_plan, spectrum_first_hz, 56);
YR_TEST_OFF(yr_plan, spectrum_last_hz, 64);

/* Since 1.2 (RAD-140). */
YR_TEST_SIZE(yr_spectrum, 72);
YR_TEST_OFF(yr_spectrum, flags, 4);
YR_TEST_OFF(yr_spectrum, time, 8);
YR_TEST_OFF(yr_spectrum, start_hz, 32);
YR_TEST_OFF(yr_spectrum, stop_hz, 40);
YR_TEST_OFF(yr_spectrum, bin_width_hz, 48);
YR_TEST_OFF(yr_spectrum, bin_count, 56);
YR_TEST_OFF(yr_spectrum, reserved, 60);
YR_TEST_OFF(yr_spectrum, power_db, 64);

YR_TEST_SIZE(yr_plan_ranges, 32);
YR_TEST_OFF(yr_plan_ranges, ranges, 16);
YR_TEST_OFF(yr_plan_ranges, max_span_hz, 24);

YR_TEST_SIZE(yr_stream_stats, 56);
YR_TEST_OFF(yr_stream_stats, records_delivered, 8);
YR_TEST_OFF(yr_stream_stats, receiver_load, 40);
YR_TEST_OFF(yr_stream_stats, sample_rate_hz, 48);

YR_TEST_SIZE(yr_stream_control, 96);
YR_TEST_OFF(yr_stream_control, enumerate, 8);
YR_TEST_OFF(yr_stream_control, state, 40);
YR_TEST_OFF(yr_stream_control, start_stream, 64);
YR_TEST_OFF(yr_stream_control, last_error, 88);

YR_TEST_SIZE(yr_setting_def, 128);
YR_TEST_OFF(yr_setting_def, key, 16);
YR_TEST_OFF(yr_setting_def, items, 72);
YR_TEST_OFF(yr_setting_def, minimum, 80);
YR_TEST_OFF(yr_setting_def, default_value, 104);
YR_TEST_OFF(yr_setting_def, sort_order, 120);

YR_TEST_SIZE(yr_preset, 32);
YR_TEST_SIZE(yr_selftest_item, 40);
YR_TEST_OFF(yr_selftest_item, remedy, 32);

YR_TEST_SIZE(yr_device_control, 80);
YR_TEST_OFF(yr_device_control, list_settings, 8);
YR_TEST_OFF(yr_device_control, run_action, 32);
YR_TEST_OFF(yr_device_control, self_test, 72);

YR_TEST_SIZE(yr_descriptor, 144);
YR_TEST_OFF(yr_descriptor, id, 16);
YR_TEST_OFF(yr_descriptor, technology_id, 72);
YR_TEST_OFF(yr_descriptor, schema_count, 80);
YR_TEST_OFF(yr_descriptor, schemas, 88);
YR_TEST_OFF(yr_descriptor, init, 104);
YR_TEST_OFF(yr_descriptor, stream, 120);
YR_TEST_OFF(yr_descriptor, on_context_event, 136);
#endif

#endif /* YANTRA_TEST_ABI_LAYOUT_H */
