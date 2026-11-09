# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The TETRA technology plugin (PLG-11, RF-06, DB-04, DB-08): its raster and
# rule library against the standard's values, C1 against hand-calculated
# values, yantra-radios' formula and the C1 of radio records, and its
# schemas against every shipped radio plugin (check_radio_schema).

add_executable(test_tetra test_tetra.cpp)
target_link_libraries(test_tetra PRIVATE yantra_test_main yantra_warnings yantra::core yantra::tetra_rules)
target_include_directories(test_tetra PRIVATE ${PROJECT_SOURCE_DIR}/third_party/yantra-radios/common/include)
add_dependencies(test_tetra yantra_tetra yantra_radio_plugins)
target_compile_definitions(test_tetra PRIVATE
  TETRA_PLUGIN_PATH="$<TARGET_FILE:yantra_tetra>"
  TETRA_TESTDATA="${PROJECT_SOURCE_DIR}/plugins/tetra/testdata"
  RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}")
# The terminal plugin is shipped unless YANTRA_RADIOS_PEI=OFF (PLG-01 check).
if("pei" IN_LIST _yr_shipped)
  target_compile_definitions(test_tetra PRIVATE TETRA_TEST_PEI=1)
endif()
yantra_doctest_tests(test_tetra GROUP test_tetra LABELS fast)
