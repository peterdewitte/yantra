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
# One ctest per test case (cmake/YantraTests.cmake); the timing cases
# run one at a time (RUN_SERIAL), so other tests do not take their CPU.
yantra_doctest_tests(test_simulator GROUP test_simulator FILTERS --test-suite-exclude=timing
  LABELS fast TIMEOUT 600 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
yantra_doctest_tests(test_simulator GROUP test_simulator_timing FILTERS --test-suite=timing
  LABELS timing TIMEOUT 300 RUN_SERIAL WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# The conformance kit on the simulator with a scene 1.1 (RAD-36, RAD-91):
# storeys, polylines and per-band walls are read and simulated. Real-time
# stream checks: label timing, like the other conformance runs.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/conformance)
add_test(NAME conformance_simulator_scene_v1_1
  COMMAND $<TARGET_FILE:yantra-radio-host> conformance --plugin $<TARGET_FILE:simulator>
          --scene ${PROJECT_SOURCE_DIR}/tests/fixtures/scenes/multiwall_v1_1.json
          --report ${CMAKE_BINARY_DIR}/conformance/simulator_scene_v1_1.json)
set_tests_properties(conformance_simulator_scene_v1_1 PROPERTIES LABELS "fast;timing" TIMEOUT 300
  PASS_REGULAR_EXPRESSION "result: PASS")
