# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The control commands (API-01) and the headless runner (PLG-24, API-03,
# DEMO-10) against the real pipeline fed by the simulator radio plugin.

file(GLOB _srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(test_headless ${_srcs})
target_link_libraries(test_headless PRIVATE yantra_test_main yantra_warnings yantra::core yantra::json yantra::plugin_api)
add_dependencies(test_headless yantra_radio_plugins yantra_tetra)
target_compile_definitions(test_headless PRIVATE
  RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}"
  TETRA_PLUGIN_PATH="$<TARGET_FILE:yantra_tetra>"
  HEADLESS_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
yantra_doctest_tests(test_headless GROUP test_headless LABELS fast TIMEOUT 600
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
