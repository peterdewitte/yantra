# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio test host (RAD-91) end to end with a test-only fake plugin:
#   fake_radio      a SCENE plugin with settings, presets, sensors, an
#                   action and a self-test (built like every plugin, so the
#                   completeness check applies to it too)
#   fake_radio_v2   the same built for radio API 2.0: the host refuses it
#   test_radio_host doctest of the host library (in process)
#   test_conformance doctest of the conformance kit (RAD-91)
#   host_cli_*      the yantra-radio-host command line
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_plugins ${CMAKE_CURRENT_BINARY_DIR}/plugins)
yantra_add_radio_plugin(fake_radio SOURCES fake_plugin.cpp LIBS yantra::radios_common
                        OUTPUT_DIR ${_plugins})
yantra_add_radio_plugin(fake_radio_v2 SOURCES fake_plugin.cpp LIBS yantra::radios_common
                        OUTPUT_DIR ${_plugins}/v2 NO_TESTS)
target_compile_definitions(fake_radio_v2 PRIVATE FAKE_API_MAJOR=2u)

add_executable(test_radio_host test_host.cpp)
target_link_libraries(test_radio_host PRIVATE yantra_test_main yantra::radio_host yantra_warnings)
target_compile_definitions(test_radio_host PRIVATE
  FAKE_PLUGIN="$<TARGET_FILE:fake_radio>"
  FAKE_PLUGIN_V2="$<TARGET_FILE:fake_radio_v2>")
add_dependencies(test_radio_host fake_radio fake_radio_v2)
# The conformance kit itself (RAD-91): passes good plugins, finds faults.
add_executable(test_conformance test_conformance.cpp)
target_link_libraries(test_conformance PRIVATE yantra_test_main yantra::radio_host yantra::json
                      yantra_warnings)
target_compile_definitions(test_conformance PRIVATE
  FAKE_PLUGIN="$<TARGET_FILE:fake_radio>"
  FAKE_PLUGIN_V2="$<TARGET_FILE:fake_radio_v2>"
  BEACON_PLUGIN="$<TARGET_FILE:beacon>")
add_dependencies(test_conformance fake_radio fake_radio_v2 beacon)
# Three ctests so they run in parallel: good plugins, injected faults
# (each fault run streams for the kit's 1.5 s minimum), and the slow_stop
# fault (every stop takes over 1 s, ~16 s in all; label slow).
add_test(NAME test_conformance COMMAND test_conformance
         --test-case-exclude=*injected* --test-suite-exclude=slow)
add_test(NAME test_conformance_faults COMMAND test_conformance
         --test-case=*injected* --test-suite-exclude=slow)
add_test(NAME test_conformance_slow COMMAND test_conformance --test-suite=slow)
set_tests_properties(test_conformance test_conformance_faults PROPERTIES LABELS fast TIMEOUT 300)
set_tests_properties(test_conformance_slow PROPERTIES LABELS slow TIMEOUT 300)

add_test(NAME test_radio_host COMMAND test_radio_host)
set_tests_properties(test_radio_host PROPERTIES LABELS fast TIMEOUT 120
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# completeness_fake_radio and conformance_fake_radio come from
# yantra_add_radio_plugin(), as for every plugin.

# The command line. PASS_REGULAR_EXPRESSION checks the output; FAIL_ wins.
set(_host $<TARGET_FILE:yantra-radio-host>)
set(_fake $<TARGET_FILE:fake_radio>)
set(_scene ${PROJECT_SOURCE_DIR}/tests/fixtures/scenes/two_rooms_v1.json)
set(_records ${CMAKE_CURRENT_BINARY_DIR}/host_cli.records.jsonl)
add_test(NAME host_cli_usage COMMAND ${_host})
set_tests_properties(host_cli_usage PROPERTIES WILL_FAIL TRUE)
add_test(NAME host_cli_info COMMAND ${_host} info --plugin ${_fake})
set_tests_properties(host_cli_info PROPERTIES
  PASS_REGULAR_EXPRESSION "plugin: fake-radio 0.1.0.*capabilities: simulated scene.*fake_cell v1 measurement, 12 fields.*problems: none")
add_test(NAME host_cli_refuse COMMAND ${_host} info --plugin $<TARGET_FILE:fake_radio_v2>)
set_tests_properties(host_cli_refuse PROPERTIES WILL_FAIL TRUE)
add_test(NAME host_cli_refuse_text COMMAND ${_host} info --plugin $<TARGET_FILE:fake_radio_v2>)
set_tests_properties(host_cli_refuse_text PROPERTIES
  PASS_REGULAR_EXPRESSION "refused: the plugin was built for radio API 2\\.0")
add_test(NAME host_cli_devices COMMAND ${_host} devices --plugin ${_fake})
set_tests_properties(host_cli_devices PROPERTIES
  PASS_REGULAR_EXPRESSION "devices: 1.*fake:0 \"Fake device 0\" driver fake serial 0000 simulated")
add_test(NAME host_cli_no_device COMMAND ${_host} devices --plugin ${_fake})
set_tests_properties(host_cli_no_device PROPERTIES ENVIRONMENT YANTRA_FAKE_NO_DEVICE=1
  PASS_REGULAR_EXPRESSION "devices: none - no fake device: YANTRA_FAKE_NO_DEVICE is set")
add_test(NAME host_cli_settings COMMAND ${_host} settings --plugin ${_fake}
         --set gain_db=12.3 --set mode=outdoor --set label=hall --action reset)
set_tests_properties(host_cli_settings PROPERTIES
  PASS_REGULAR_EXPRESSION "set gain_db = 12.3: OK, reads back 12.5.*set mode = outdoor: OK, reads back outdoor \\(1\\).*action reset: OK - counters reset.*label \\(string, Receiver\\) = \"hall\".*indoor - Indoor, sensitive.*temperature_c \\(double, Device\\) = 42.5 degC.*settings_changed: 1"
  FAIL_REGULAR_EXPRESSION "problem")
add_test(NAME host_cli_settings_refused COMMAND ${_host} settings --plugin ${_fake}
         --set gain_db=99)
set_tests_properties(host_cli_settings_refused PROPERTIES
  PASS_REGULAR_EXPRESSION "set gain_db = 99: refused: INVALID_ARGUMENT: Gain must be at least 0 and at most 40 dB")
add_test(NAME host_cli_selftest COMMAND ${_host} selftest --plugin ${_fake})
set_tests_properties(host_cli_selftest PROPERTIES
  PASS_REGULAR_EXPRESSION "selftest: OK.*pass usb.*warn clock.*connect a 10 MHz reference.*skipped antenna")
add_test(NAME host_cli_stream COMMAND ${_host} stream --plugin ${_fake} --records 20
         --out ${_records} --scene ${_scene} --walk "4,3;14,3" --path-step 10 --sensors)
set_tests_properties(host_cli_stream PROPERTIES FIXTURES_SETUP host_records
  PASS_REGULAR_EXPRESSION "scene_walls \\(uint, Debug\\) = 2.*stream: records 20, events 1, invalid 0, dropped 0, late 0, stop records.*notifications: walk_position 1")
add_test(NAME host_cli_read COMMAND ${_host} read ${_records})
set_tests_properties(host_cli_read PROPERTIES FIXTURES_REQUIRED host_records
  PASS_REGULAR_EXPRESSION "plugin: fake-radio 0.1.0.*device: fake:0.*plan: serving.*records: 20.*fake_cell: 20.*events: 1.*end: records 20, events 1, dropped 0")
add_test(NAME host_cli_stream_end COMMAND ${_host} stream --plugin ${_fake} --quiet
         --set stop_after=5 --carriers 392012500,392037500)
set_tests_properties(host_cli_stream_end PROPERTIES
  PASS_REGULAR_EXPRESSION "stream: records 6, events 2, invalid 0, dropped 0, late 0, stop end_of_stream")
add_test(NAME host_cli_read_bad COMMAND ${_host} read ${_scene})
set_tests_properties(host_cli_read_bad PROPERTIES
  PASS_REGULAR_EXPRESSION "two_rooms_v1.json:1: header is not valid JSON")
add_test(NAME host_cli_decode_iq COMMAND ${_host} decode-iq --plugin ${_fake}
         --iq x.cf32 --format cf32)
if(TARGET yantra::tetra_engine)
  # Raw files need the rate and centre (RAD-63).
  set_tests_properties(host_cli_decode_iq PROPERTIES
    PASS_REGULAR_EXPRESSION "the sample rate is unknown: give --rate HZ")
else()
  set_tests_properties(host_cli_decode_iq PROPERTIES
    PASS_REGULAR_EXPRESSION "decode-iq is not available: this build has no TETRA receiver")
endif()
add_test(NAME host_cli_capture_iq COMMAND ${_host} capture-iq --plugin ${_fake}
         --out x.sigmf-data)
set_tests_properties(host_cli_capture_iq PROPERTIES
  PASS_REGULAR_EXPRESSION "capture-iq is not available yet: it needs the plugins' device layer \\(RAD-63, phase 5\\)")
add_test(NAME host_cli_conformance_usage COMMAND ${_host} conformance --plugin ${_fake}
         --unplug true)
set_tests_properties(host_cli_conformance_usage PROPERTIES
  PASS_REGULAR_EXPRESSION "--unplug and --replug go together")
set_tests_properties(completeness_fake_radio host_cli_usage host_cli_conformance_usage host_cli_info host_cli_refuse
  host_cli_refuse_text host_cli_devices host_cli_no_device host_cli_settings
  host_cli_settings_refused host_cli_selftest host_cli_stream host_cli_read host_cli_stream_end
  host_cli_read_bad host_cli_decode_iq host_cli_capture_iq
  PROPERTIES LABELS fast TIMEOUT 120)
