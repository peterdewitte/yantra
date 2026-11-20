# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# plugin-api/: Yantra's plugin API (PLG-03, PLG-30), the interface between
# the application and its technology, feature and exporter plugins
# (plugins/tetra, probe, beacon): the C ABI header yantra/plugin.h with its
# C++ helpers plugin.hpp, nothing to link. It sits next to radio-api/ (the
# interface of the radio plugins); neither includes the other. The
# reference is built by docs/ (docs-plugin-api), the header tests are in
# tests/plugin_api.
#
#   yantra::plugin_api   interface target (include folder)

add_library(yantra_plugin_api INTERFACE)
add_library(yantra::plugin_api ALIAS yantra_plugin_api)
target_include_directories(yantra_plugin_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
