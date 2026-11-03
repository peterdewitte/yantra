# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Simulator plugin tests (RAD-94): through the test host's emulated
# context callbacks (RAD-91). test_simulator_timing (label timing) checks
# the real-time pacing.
if(NOT TARGET simulator)
  message(STATUS "test_simulator: left out (no simulator plugin)")
  return()
endif()
add_executable(test_simulator test_simulator.cpp)
target_link_libraries(test_simulator PRIVATE yantra_test_main yantra_warnings
  yantra::radio_host yantra::gen)
target_compile_definitions(test_simulator PRIVATE SIMULATOR_PLUGIN="$<TARGET_FILE:simulator>")
add_dependencies(test_simulator simulator)
add_test(NAME test_simulator COMMAND test_simulator --test-suite-exclude=timing)
set_tests_properties(test_simulator PROPERTIES LABELS fast TIMEOUT 600
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_simulator_timing COMMAND test_simulator --test-suite=timing)
set_tests_properties(test_simulator_timing PROPERTIES LABELS timing TIMEOUT 300 RUN_SERIAL TRUE
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
