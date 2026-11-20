// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small, valid radio descriptor (no functions) with a MEASUREMENT schema
// that uses every value type and two optional groups, and a NEIGHBOUR
// schema, for the record file tests.
#pragma once

#include <yantra/radio.h>

namespace fixture {

inline const yr_enum_item kRoles[] = {
    {YR_ROLE_SERVING, "serving", "Serving"},
    {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
    {YR_ROLE_OTHER, "other", "Other"},
};

inline yr_field_def field(uint32_t type, uint32_t group, const char* name, const char* unit,
                          uint32_t flags = 0)
{
    yr_field_def f{};
    f.struct_size = sizeof f;
    f.type = type;
    f.group = group;
    f.flags = flags;
    f.name = name;
    f.label = name;
    f.unit = unit;
    f.help = "test field";
    if (type == YR_TYPE_ENUM) {
        f.enum_items = kRoles;
        f.enum_count = 3;
    }
    return f;
}

inline const yr_field_def kMeasFields[] = {
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_FREQUENCY_HZ, "Hz", YR_FIELD_KEY),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_CHANNEL, ""),
    field(YR_TYPE_ENUM, YR_GROUP_CORE, YR_FIELD_ROLE, ""),
    field(YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_LEVEL, "dBFS", YR_FIELD_DEFAULT),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_MCC, ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_MNC, ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_LA, ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_COLOUR_CODE, ""),
    field(YR_TYPE_DOUBLE, YR_GROUP_QUALITY, YR_FIELD_MER_PCT, "%"),
    field(YR_TYPE_STRING, YR_GROUP_SIM_POSITION, YR_FIELD_SIM_FLOOR_ID, ""),
    field(YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, YR_FIELD_SIM_X_M, "m"),
    field(YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, YR_FIELD_SIM_Y_M, "m"),
    field(YR_TYPE_BOOL, YR_GROUP_OTHER, "barred", ""),
    field(YR_TYPE_INT, YR_GROUP_OTHER, "offset_db", "dB"),
};
inline const yr_field_def* const kMeasFieldPtrs[] = {
    &kMeasFields[0], &kMeasFields[1], &kMeasFields[2],  &kMeasFields[3], &kMeasFields[4],
    &kMeasFields[5], &kMeasFields[6], &kMeasFields[7],  &kMeasFields[8], &kMeasFields[9],
    &kMeasFields[10], &kMeasFields[11], &kMeasFields[12], &kMeasFields[13]};

inline const yr_field_def kNbFields[] = {
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_FREQUENCY_HZ, "Hz", YR_FIELD_KEY),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_ENTRY_INDEX, ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_ENTRY_COUNT, ""),
    field(YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, "cell_id", ""),
};
inline const yr_field_def* const kNbFieldPtrs[] = {&kNbFields[0], &kNbFields[1], &kNbFields[2],
                                                   &kNbFields[3]};

inline const yr_schema kMeas = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "cell", "Cell", 1, 14,
                                kMeasFieldPtrs,
                                YR_GROUP_QUALITY | YR_GROUP_SIM_POSITION | YR_GROUP_OTHER, 0};
inline const yr_schema kNb = {sizeof(yr_schema), YR_SCHEMA_NEIGHBOUR, "neighbour", "Neighbour", 2,
                              4, kNbFieldPtrs, YR_GROUP_NEIGHBOURS, 0};
inline const yr_schema* const kSchemas[] = {&kMeas, &kNb};

inline yr_descriptor make_descriptor()
{
    yr_descriptor d{};
    d.struct_size = sizeof d;
    d.api_version = yr_api_version_compiled();
    d.capabilities = YR_CAP_SIMULATED | YR_CAP_SCENE;
    d.id = "fixture";
    d.name = "Fixture";
    d.version = "1.2.3";
    d.author = "The Yantra contributors";
    d.licence = "GPL-3.0-or-later";
    d.description = "Record file test fixture";
    d.source_url = "https://example.invalid/fixture";
    d.technology_id = "beacon";
    d.schema_count = 2;
    d.schemas = kSchemas;
    return d;
}

} // namespace fixture
