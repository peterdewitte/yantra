# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The plugin test kit (PLG-13) on the example technology plugin (PLG-15)
# and on the C test plugin, its negative cases, and the CMake template.
#   conformance_example_beacon       golden record file -> golden output
#   conformance_example_beacon_live  live records of the simulated radio
#                                    (the yantra-radios beacon plugin, PLG-11)
#   conformance_ctest                C test plugin, hand-written record file
#   plugin_kit_refuses_*             broken plugins and inputs: exit 1 and
#                                    the reason in the output
#   plugin_kit_usage                 exit 2 on a usage error
#   plugin_template                  the template builds the example out of tree
#   test_plugin_kit                  the kit library in process (doctest)

set(_kit $<TARGET_FILE:yantra-plugin-test>)
set(_out ${CMAKE_CURRENT_BINARY_DIR}/out)
file(MAKE_DIRECTORY ${_out})
set(_example $<TARGET_FILE:yantra_example_beacon>)
set(_example_data ${PROJECT_SOURCE_DIR}/plugins/beacon/testdata)
set(_ctest ${PROJECT_BINARY_DIR}/tests/plugin_loader/modules)
set(_data ${CMAKE_CURRENT_SOURCE_DIR}/data)

# Broken variants of the example (kit_fixture.c).
set(_broken licence LICENCE ui UI impure IMPURE slow SLOW settings SETTINGS)
while(_broken)
  list(POP_FRONT _broken _name _def)
  add_library(kit_broken_${_name} MODULE fixture/kit_fixture.c)
  target_include_directories(kit_broken_${_name} PRIVATE ${PROJECT_SOURCE_DIR}/plugins/beacon)
  target_link_libraries(kit_broken_${_name} PRIVATE yantra::plugin_api yantra::warnings)
  target_compile_definitions(kit_broken_${_name} PRIVATE KIT_BROKEN_${_def}=1)
  set_target_properties(kit_broken_${_name} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules)
endwhile()

# yantra_kit_test(<name> EXIT <code> [MATCH <regex>] [REQUIRE_FILE <file>]
#                 [PROCESSORS <n>] [LABELS ...] ARGS ...); without
#                 REQUIRE_FILE it is skipped. The ctest TIMEOUT is a hang
#                 guard and scales with YANTRA_TEST_TIME_SCALE (sanitizer
#                 trees); so does the radio wait of a live run (_kit_radio_wait).
function(yantra_kit_test name)
  cmake_parse_arguments(T "" "EXIT;MATCH;REQUIRE_FILE;PROCESSORS" "ARGS;LABELS" ${ARGN})
  list(JOIN T_ARGS "|" _args)
  if(NOT T_LABELS)
    set(T_LABELS fast)
  endif()
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} -DKIT=${_kit} "-DARGS=${_args}" -DEXIT=${T_EXIT}
            "-DMATCH=${T_MATCH}" "-DREQUIRE_FILE=${T_REQUIRE_FILE}" -DREPORT=${_out}/${name}.report.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect.cmake)
  math(EXPR _timeout "300 * ${YANTRA_TEST_TIME_SCALE}")
  set_tests_properties(${name} PROPERTIES LABELS "${T_LABELS}" TIMEOUT ${_timeout}
    SKIP_REGULAR_EXPRESSION "SKIPPED: ")
  if(T_PROCESSORS)
    set_tests_properties(${name} PROPERTIES PROCESSORS ${T_PROCESSORS})
  endif()
endfunction()

# The kit's wait for a live radio's records (--timeout, default 30 s): the
# sanitizer trees run the radio plugins 5-13x slower than real time, so the
# wait is a hang guard scaled like the others; the record count it waits
# for (the check) stays the same.
math(EXPR _kit_radio_wait "30 * ${YANTRA_TEST_TIME_SCALE}")

yantra_kit_test(conformance_example_beacon EXIT 0 MATCH "PASS .*example_beacon"
  ARGS check --plugin ${_example} --records ${_example_data}/beacon.records.jsonl
       --golden ${_example_data}/beacon.golden.json --strict-translations
       --output ${_out}/example_beacon.out.json)
yantra_kit_test(conformance_example_beacon_live EXIT 0 MATCH "pass  radio_schema: radio beacon"
  LABELS slow REQUIRE_FILE ${YANTRA_RADIO_TEST_PLUGIN_DIR}/beacon${YANTRA_RADIO_PLUGIN_SUFFIX}
  ARGS check --plugin ${_example} --radio ${YANTRA_RADIO_TEST_PLUGIN_DIR}/beacon${YANTRA_RADIO_PLUGIN_SUFFIX} --count 8
       --timeout ${_kit_radio_wait} --strict-translations)
yantra_kit_test(conformance_ctest EXIT 0 MATCH "PASS .*ctest"
  ARGS check --plugin ${_ctest}/ctest_ok/ctest${CMAKE_SHARED_MODULE_SUFFIX} --records ${_data}/ctest.records.jsonl
       --golden ${_data}/ctest.golden.json --output ${_out}/ctest.out.json)

# Negative cases: each must fail with its reason in the output.
set(_beacon_records --records ${_example_data}/beacon.records.jsonl)
yantra_kit_test(plugin_kit_refuses_licence EXIT 1 MATCH "fail  load: .*LicenseRef-Proprietary"
  ARGS check --plugin $<TARGET_FILE:kit_broken_licence> ${_beacon_records})
yantra_kit_test(plugin_kit_refuses_ui EXIT 1 MATCH "fail  ui: window 'beacon_channels': button 'refresh': no tooltip"
  ARGS check --plugin $<TARGET_FILE:kit_broken_ui> ${_beacon_records})
yantra_kit_test(plugin_kit_refuses_impure EXIT 1 MATCH "fail  threading: the technology rules are not pure"
  ARGS check --plugin $<TARGET_FILE:kit_broken_impure> ${_beacon_records})
yantra_kit_test(plugin_kit_refuses_slow EXIT 1 MATCH "fail  threading: .*compute_c1 took [0-9.]+ ms \\(limit 100.0 ms\\): it must not block"
  LABELS timing
  ARGS check --plugin $<TARGET_FILE:kit_broken_slow> ${_beacon_records})
yantra_kit_test(plugin_kit_refuses_settings EXIT 1 MATCH "fail  threading: .*settings_set refused on a measurement thread \\(PLG-07\\)"
  ARGS check --plugin $<TARGET_FILE:kit_broken_settings> ${_beacon_records})
yantra_kit_test(plugin_kit_refuses_api_major EXIT 1 MATCH "fail  load: the plugin was"
  ARGS check --plugin ${_ctest}/ctest_v2/ctest${CMAKE_SHARED_MODULE_SUFFIX})
yantra_kit_test(plugin_kit_refuses_no_entry EXIT 1 MATCH "fail  load: .*yantra_plugin_entry"
  ARGS check --plugin ${_ctest}/ctest_noentry/ctest${CMAKE_SHARED_MODULE_SUFFIX})
yantra_kit_test(plugin_kit_refuses_init_failure EXIT 1 MATCH "fail  load: the plugin was"
  ARGS check --plugin ${_ctest}/ctest_initfail/ctest${CMAKE_SHARED_MODULE_SUFFIX})
yantra_kit_test(plugin_kit_refuses_wrong_technology EXIT 1
  MATCH "fail  radio_schema: wrong_technology.records.jsonl: technology 'other', the plugin's is 'ctest'"
  ARGS check --plugin ${_ctest}/ctest_ok/ctest${CMAKE_SHARED_MODULE_SUFFIX} --records ${_data}/wrong_technology.records.jsonl)
yantra_kit_test(plugin_kit_refuses_rogue_field EXIT 1
  MATCH "fail  radio_schema: .*the radio delivers 'rogue', which the technology schema lacks"
  ARGS check --plugin ${_ctest}/ctest_ok/ctest${CMAKE_SHARED_MODULE_SUFFIX} --records ${_data}/rogue_field.records.jsonl)
yantra_kit_test(plugin_kit_refuses_golden_difference EXIT 1 MATCH "fail  golden: [0-9]+ differences"
  ARGS check --plugin ${_ctest}/ctest_ok/ctest${CMAKE_SHARED_MODULE_SUFFIX} --records ${_data}/ctest.records.jsonl
       --golden ${_example_data}/beacon.golden.json)
yantra_kit_test(plugin_kit_usage EXIT 2 MATCH "error: unknown command"
  ARGS frobnicate --plugin ${_example})

if(CMAKE_CROSSCOMPILING)
  # The template is built with the tree's compiler and checked with its kit
  # program: on the build machine the kit cannot run, on Windows there is
  # no compiler. The Linux trees run it.
  yantra_skip_test(plugin_template REASON "builds the plugin template with the cross compiler and runs the kit: needs both machines (the Linux trees run it)" LABELS slow)
else()
  add_test(NAME plugin_template
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/template -DKIT=${_kit}
            -DGENERATOR=${CMAKE_GENERATOR} -DC_COMPILER=${CMAKE_C_COMPILER}
            -DCTEST=${CMAKE_CTEST_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/run_template.cmake)
  set_tests_properties(plugin_template PROPERTIES LABELS slow TIMEOUT 600)
endif()

yantra_add_test_dir(plugin_kit LIBS yantra_plugin_kit)
target_compile_definitions(test_plugin_kit PRIVATE
  KIT_EXAMPLE_PATH="$<TARGET_FILE:yantra_example_beacon>"
  KIT_CTEST_PATH="${_ctest}/ctest_ok/ctest${CMAKE_SHARED_MODULE_SUFFIX}"
  KIT_IMPURE_PATH="$<TARGET_FILE:kit_broken_impure>")
add_dependencies(test_plugin_kit yantra_example_beacon kit_broken_impure ctest_ok)

# The TETRA technology plugin (PLG-11): golden record files (yantra-gen
# scenario fraunces, dBm levels; the simulator radio through
# yantra-radio-host, dBFS levels with its C1) and the live simulator.
set(_tetra $<TARGET_FILE:yantra_tetra>)
set(_tetra_data ${PROJECT_SOURCE_DIR}/plugins/tetra/testdata)
yantra_kit_test(conformance_tetra EXIT 0 MATCH "pass  neighbours: [0-9]+ entries, [1-9][0-9]* resolved.*PASS .*tetra"
  ARGS check --plugin ${_tetra} --records ${_tetra_data}/tetra_gen.records.jsonl
       --records ${_tetra_data}/tetra_sim.records.jsonl
       --golden ${_tetra_data}/tetra.golden.json --strict-translations
       --output ${_out}/tetra.out.json)
yantra_kit_test(conformance_tetra_live EXIT 0 MATCH "pass  radio_schema: radio simulator.*PASS .*tetra"
  LABELS slow REQUIRE_FILE ${YANTRA_RADIO_PLUGIN_DIR}/simulator${YANTRA_RADIO_PLUGIN_SUFFIX} PROCESSORS 4
  ARGS check --plugin ${_tetra} --radio ${YANTRA_RADIO_PLUGIN_DIR}/simulator${YANTRA_RADIO_PLUGIN_SUFFIX} --count 40
       --timeout ${_kit_radio_wait} --strict-translations)

