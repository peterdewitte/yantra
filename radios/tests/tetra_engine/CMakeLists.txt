# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The TETRA measurement engine (RAD-102, RAD-110..114, RAD-21..23,
# RAD-27): carrier search on generated wideband spans, level meter,
# quality, records against yantra-gen's ground truth, parallel carriers
# (run under ThreadSanitizer in the tsan tree). Needs the osmo-tetra
# submodule.
if(NOT YANTRA_OSMO_TETRA_FOUND)
  message(STATUS "test_tetra_engine: left out (needs the osmo-tetra submodule)")
  return()
endif()
yantra_add_test_dir(tetra_engine LIBS yantra::tetra_engine yantra::gen)
target_compile_definitions(test_tetra_engine PRIVATE
  YANTRA_SCENARIO_DIR="${CMAKE_SOURCE_DIR}/data/scenarios")
