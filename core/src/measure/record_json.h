// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal: the JSON forms of radio schemas, values and name tables of
// record files (record_file.cpp), shared with Yantra's recording format
// (core/src/recording, docs/formats/RECORDING.md) so both write a radio
// schema the same way. Not installed.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "yantra/core/measure/record_file.h"

namespace yantra::measure {

// A radio schema as a record file header holds it (the radio parts
// docs/RECORD_FILES.md): id, label, kind, version, groups, fields.
nlohmann::json schema_json(const RecordFileSchema& schema);
std::optional<RecordFileSchema> parse_schema_json(const nlohmann::json& j, const std::string& where,
                                                  std::string& error);
// A value of field `f`: null = absent; enums by their integer value.
nlohmann::json value_json(const plugin::Value& value);
bool parse_value_json(const nlohmann::json& v, const RecordFileField& f, plugin::Value& out,
                      std::string& error);
// Bit sets as arrays of names; unknown names are ignored.
nlohmann::json group_names(std::uint32_t groups);
std::uint32_t group_bits(const nlohmann::json& names);
nlohmann::json capability_names(std::uint32_t capabilities);
std::uint32_t capability_bits(const nlohmann::json& names);
nlohmann::json record_flag_names(std::uint32_t flags);
std::uint32_t record_flag_bits(const nlohmann::json& names);
// YR_EVENT_* names ("sync_lost"); nullptr / nullopt when unknown.
const char* event_name(std::uint32_t event);
std::optional<std::uint32_t> event_value(std::string_view name);

}  // namespace yantra::measure
