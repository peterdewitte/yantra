# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Export (EXP-01, EXP-03, EXP-08, PLG-02 exporter plugins, TST-33;
# docs/EXPORT.md): CSV with its columns and -999, DXF and CSV write/read
# round trips, floor images, and the C test exporter plugin
# (plugin/rows_exporter.c, public C ABI only) run on the rows. The batch
# export of real recordings (EXP-04) is in tests/measure_host
# (test_measure_export.cpp: the Fraunces demo with the TETRA plugin).
# One ctest per doctest case.
add_library(test_rows_exporter MODULE plugin/rows_exporter.c)
target_link_libraries(test_rows_exporter PRIVATE yantra::plugin_api yantra::warnings)
set_target_properties(test_rows_exporter PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules)

list(APPEND YANTRA_PER_CASE_TESTS export)
yantra_add_test_dir(export LIBS yantra::core yantra::json)
target_include_directories(test_export PRIVATE ${PROJECT_SOURCE_DIR}/tests/survey)
add_dependencies(test_export test_rows_exporter)
target_compile_definitions(test_export PRIVATE
  YANTRA_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch"
  ROWS_EXPORTER_PATH="$<TARGET_FILE:test_rows_exporter>")
