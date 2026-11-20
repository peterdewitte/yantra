# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The example technology plugin (PLG-15): one C file against the public
# header yantra/plugin.h only, built as a plugin module (no "lib" prefix,
# only the entry point exported). It is an example, not a shipped plugin:
# it is not installed; the tutorial (docs/PLUGINS.md) shows how to put it
# into a plugin folder. Its conformance test (PLG-13) is in
# tests/plugin_kit.

add_library(yantra_example_beacon MODULE beacon_technology.c)
target_link_libraries(yantra_example_beacon PRIVATE yantra::plugin_api yantra::warnings)
set_target_properties(yantra_example_beacon PROPERTIES
  PREFIX ""
  OUTPUT_NAME example_beacon
  C_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib/yantra/examples)
