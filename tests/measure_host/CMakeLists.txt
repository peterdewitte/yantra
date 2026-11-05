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
# plugins are not built. TST-10 end to end (test_measure_e2e.cpp): the
# simulator on the radios' scenarios through RadioHost + pipeline + TETRA
# plugin, and record files of the same scenarios (yantra-gen ground truth
# and decode-iq dBFS) replayed, against the ground truth. A walk test of
# the simulator on a drawn floor, recorded and replayed
# (test_measure_survey.cpp, IND-20, IND-30). One ctest per test case.
set(_dir ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB _srcs CONFIGURE_DEPENDS ${_dir}/test_*.cpp)
add_executable(test_measure_host ${_srcs})
target_link_libraries(test_measure_host PRIVATE yantra_test_main yantra_warnings yantra::core yantra::json)
target_include_directories(test_measure_host PRIVATE ${PROJECT_SOURCE_DIR}/tests/radio_host)
add_dependencies(test_measure_host fake_radio_v1 yantra_radio_plugins yantra_example_beacon yantra_tetra)
target_compile_definitions(test_measure_host PRIVATE
  FAKE_RADIO_PATH="$<TARGET_FILE:fake_radio_v1>"
  RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}"
  RADIO_TEST_PLUGIN_DIR="${YANTRA_RADIO_TEST_PLUGIN_DIR}"
  EXAMPLE_BEACON_PATH="$<TARGET_FILE:yantra_example_beacon>"
  TETRA_PLUGIN_PATH="$<TARGET_FILE:yantra_tetra>"
  FAKE_SDR_SCENARIO="${YANTRA_RADIOS_FAKE_SDR_SCENARIO}"
  FAKE_SDR_CONTROL_DIR="${CMAKE_CURRENT_BINARY_DIR}/fake_sdr_control"
  MEASURE_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")

# TST-10 record files of the radios' scenarios, written into the build
# tree by the submodule's yantra-gen and yantra-radio-host when the radio
# plugins are built (or a prebuilt package has the tools); the cases skip
# without them. Regenerated when a scenario or the script changes.
set(_e2e_records ${CMAKE_CURRENT_BINARY_DIR}/records)
set(_e2e_gen ${YANTRA_RADIOS_STAGE}/bin/yantra-gen)
set(_e2e_host ${YANTRA_RADIOS_STAGE}/bin/yantra-radio-host)
if(YANTRA_RADIOS_KIND STREQUAL "build" OR (YANTRA_RADIOS_KIND STREQUAL "prebuilt" AND EXISTS ${_e2e_gen}))
  set(_e2e_scenarios fraunces:3 failed_site:10 floor_change:12 indoor_good_das:10 indoor_weak_das:10)
  set(_e2e_outputs ${_e2e_records}/fraunces_dbfs.records.jsonl)
  foreach(_s IN LISTS _e2e_scenarios)
    string(REGEX REPLACE ":.*" "" _id ${_s})
    list(APPEND _e2e_outputs ${_e2e_records}/${_id}.records.jsonl ${_e2e_records}/${_id}.cells.csv
         ${_e2e_records}/${_id}.truth.csv)
  endforeach()
  file(GLOB _e2e_scenario_files ${YANTRA_RADIOS_DIR}/data/scenarios/*.json)
  add_custom_command(OUTPUT ${_e2e_outputs}
    COMMAND ${CMAKE_COMMAND} -DGEN=${_e2e_gen} -DHOST=${_e2e_host} -DOUT=${_e2e_records}
            "-DSCENARIOS=${_e2e_scenarios}" -DDBFS=fraunces -P ${_dir}/gen_records.cmake
    DEPENDS ${_dir}/gen_records.cmake ${_e2e_scenario_files}
    COMMENT "Record files of the radios' scenarios (yantra-gen, decode-iq) for TST-10"
    VERBATIM)
  add_custom_target(measure_host_records DEPENDS ${_e2e_outputs})
  add_dependencies(measure_host_records yantra_radio_plugins)
  add_dependencies(test_measure_host measure_host_records)
  target_compile_definitions(test_measure_host PRIVATE E2E_RECORDS_DIR="${_e2e_records}")
else()
  target_compile_definitions(test_measure_host PRIVATE E2E_RECORDS_DIR="")
endif()
target_compile_definitions(test_measure_host PRIVATE
  RADIOS_BIN_DIR="${YANTRA_RADIOS_STAGE}/bin"
  RADIOS_SCENARIO_DIR="${YANTRA_RADIOS_DIR}/data/scenarios")
# The demo project (tools/yantra-demo, DEMO-05/06; test_measure_demo.cpp).
if(TARGET demo_fraunces)
  add_dependencies(test_measure_host demo_fraunces)
endif()
target_compile_definitions(test_measure_host PRIVATE
  DEMO_DATA_DIR="${PROJECT_SOURCE_DIR}/data/demo"
  DEMO_FRAUNCES_DIR="$<$<TARGET_EXISTS:demo_fraunces>:${YANTRA_DEMO_FRAUNCES_DIR}>"
  YANTRA_DEMO_PATH="$<TARGET_FILE:yantra-demo>"
  DEMO_GEN="${YANTRA_DEMO_GEN}"
  DEMO_SCENARIO_DIR="${YANTRA_DEMO_SCENARIO_DIR}")

# PROCESSORS: the simulator and SDR plugin cases keep a stream thread,
# the plugin's engine workers and the pipeline busy and check real-time
# cadence; ctest -j must not start more of them than the machine runs.
# The test suite "long" (a long replayed walk test, TST-31) is opt-in.
yantra_doctest_tests(test_measure_host GROUP test_measure_host LABELS fast TIMEOUT 600 PROCESSORS 4
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR} FILTERS --test-suite-exclude=long)
yantra_doctest_tests(test_measure_host GROUP test_measure_host_long LABELS slow TIMEOUT 1200 OPT_IN
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR} FILTERS --test-suite=long)
