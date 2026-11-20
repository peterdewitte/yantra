// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio API ABI layout seen from C++20 (RAD-10): the same static
// asserts as the C translation unit, plus the C++ properties that let a
// C++ plugin or host use the structs directly.
#include "abi_layout.h"

#include <type_traits>

namespace {
template <typename T>
constexpr bool plain_c = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

static_assert(plain_c<yr_api_version> && plain_c<yr_time> && plain_c<yr_value> &&
              plain_c<yr_enum_item> && plain_c<yr_field_def> && plain_c<yr_schema> &&
              plain_c<yr_record> && plain_c<yr_floor_plan_info> && plain_c<yr_plan_position> &&
              plain_c<yr_host_api> && plain_c<yr_device_info> && plain_c<yr_plan> &&
              plain_c<yr_freq_range> && plain_c<yr_plan_ranges> && plain_c<yr_stream_stats> &&
              plain_c<yr_stream_control> && plain_c<yr_setting_def> && plain_c<yr_preset> &&
              plain_c<yr_selftest_item> && plain_c<yr_device_control> &&
              plain_c<yr_translation> && plain_c<yr_descriptor> && plain_c<yr_spectrum>,
              "radio API structs are plain C structs");
static_assert(std::is_same_v<decltype(&yantra_radio_entry), yr_entry_fn>,
              "yantra_radio_entry has the type yr_entry_fn");
} // namespace
