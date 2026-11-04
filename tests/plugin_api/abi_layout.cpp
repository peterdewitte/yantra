// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plugin API ABI layout seen from C++20 (PLG-03): the same static
// asserts as the C translation unit, plus the C++ properties that let a
// C++ plugin or host use the structs directly.
#include "abi_layout.h"

#include <type_traits>

namespace {
template <typename T>
constexpr bool plain_c = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

static_assert(plain_c<yantra_api_version> && plain_c<yantra_time> && plain_c<yantra_value> &&
                  plain_c<yantra_enum_item> && plain_c<yantra_field_def> &&
                  plain_c<yantra_schema> && plain_c<yantra_record> &&
                  plain_c<yantra_provenance> && plain_c<yantra_host_api> &&
                  plain_c<yantra_band> && plain_c<yantra_field_link> &&
                  plain_c<yantra_c1_input> && plain_c<yantra_reselection_cell> &&
                  plain_c<yantra_reselection_input> && plain_c<yantra_reselection_result> &&
                  plain_c<yantra_technology_api> && plain_c<yantra_feature_api> &&
                  plain_c<yantra_export_format> && plain_c<yantra_export_request> &&
                  plain_c<yantra_exporter_api> && plain_c<yantra_ui_control> &&
                  plain_c<yantra_ui_plot> && plain_c<yantra_ui_table> &&
                  plain_c<yantra_ui_api> && plain_c<yantra_ui_window_def> &&
                  plain_c<yantra_ui_menu_item> && plain_c<yantra_ui_menu_state> &&
                  plain_c<yantra_translation> && plain_c<yantra_plugin_ui_api> &&
                  plain_c<yantra_plugin_descriptor>,
              "plugin API structs are plain C structs");
static_assert(std::is_same_v<decltype(&yantra_plugin_entry), yantra_plugin_entry_fn>,
              "yantra_plugin_entry has the type yantra_plugin_entry_fn");
} // namespace
