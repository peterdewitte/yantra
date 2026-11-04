# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Measurement pipeline behind the radio host (OA-13, TST-23, TST-31,
# PLG-05): the fake radio plugin of tests/radio_host pushing late, burst,
# out-of-order and duplicate records; the simulator of yantra-radios end to
# end at 1 Hz; the beacon example radio with the example beacon technology
# plugin (technology schema mapping); the calibration table and antenna
# correction with the fake SDR plugin and the simulator's practice
# generator (RF-02, HW-01b, HW-09, TST-30). Radio plugin cases skip when the
# plugins are not built. One ctest per test case.
set(_dir ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB _srcs CONFIGURE_DEPENDS ${_dir}/test_*.cpp)
add_executable(test_measure_host ${_srcs})
target_link_libraries(test_measure_host PRIVATE yantra_test_main yantra_warnings yantra::core)
target_include_directories(test_measure_host PRIVATE ${PROJECT_SOURCE_DIR}/tests/radio_host)
add_dependencies(test_measure_host fake_radio_v1 yantra_radio_plugins yantra_example_beacon)
target_compile_definitions(test_measure_host PRIVATE
  FAKE_RADIO_PATH="$<TARGET_FILE:fake_radio_v1>"
  RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}"
  RADIO_TEST_PLUGIN_DIR="${YANTRA_RADIO_TEST_PLUGIN_DIR}"
  EXAMPLE_BEACON_PATH="$<TARGET_FILE:yantra_example_beacon>"
  FAKE_SDR_SCENARIO="${YANTRA_RADIOS_FAKE_SDR_SCENARIO}"
  FAKE_SDR_CONTROL_DIR="${CMAKE_CURRENT_BINARY_DIR}/fake_sdr_control")
yantra_doctest_tests(test_measure_host GROUP test_measure_host LABELS fast TIMEOUT 600
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
