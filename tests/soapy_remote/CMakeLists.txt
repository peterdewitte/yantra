# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# SoapyRemote (RAD-50 network radios) in the generic plugin: a loopback
# SoapySDRServer (SoapyRemote's server side) with the fake module, the
# per-case doctests and a conformance run of the installed soapysdr
# plugin over the network.
if(NOT TARGET soapysdr OR NOT TARGET yantra_soapy_remote_server)
  message(STATUS "test_soapy_remote: left out (no soapysdr plugin)")
  return()
endif()
yantra_fake_soapy_library(yantra_fake_soapy_remote_server DRIVER yantrafake)
add_library(yantra_test_remote_server STATIC remote_server.cpp)
target_link_libraries(yantra_test_remote_server PUBLIC yantra_soapy_remote_server
  PRIVATE yantra_warnings $<COMPILE_ONLY:yantra_soapysdr_whole>)
add_executable(test_soapy_remote test_soapy_remote.cpp)
target_link_libraries(test_soapy_remote PRIVATE yantra_test_main yantra_warnings
  yantra::radio_host yantra::gen yantra_test_remote_server yantra_fake_soapy_remote_server_whole)
target_compile_definitions(test_soapy_remote PRIVATE SOAPYSDR_PLUGIN="$<TARGET_FILE:soapysdr>")
add_dependencies(test_soapy_remote soapysdr)
yantra_doctest_tests(test_soapy_remote GROUP test_soapy_remote LABELS fast TIMEOUT 300
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(yantra-soapy-remote-test-server remote_server_main.cpp)
target_link_libraries(yantra-soapy-remote-test-server PRIVATE yantra_warnings
  yantra_test_remote_server yantra_fake_soapy_remote_server_whole)
set_target_properties(yantra-soapy-remote-test-server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/bin)

# The conformance kit on the installed plugin with a device behind the
# loopback server (the server runs for the length of the kit).
set(_ctl ${CMAKE_BINARY_DIR}/tests/soapy_remote_control/conformance)
set(_serial 0000000000000000a1b2c3d4e5f60001)
set(_fake "{\"control\":\"${_ctl}\",\"devices\":[{\"serial\":\"${_serial}\",\"scenario\":\"${PROJECT_SOURCE_DIR}/tests/fixtures/soapy/fake_two_cells.json\"}]}")
add_test(NAME conformance_soapysdr_remote
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/with_remote_server.sh ${_ctl}
                 $<TARGET_FILE:yantra-soapy-remote-test-server>
                 $<TARGET_FILE:yantra-radio-host> conformance --plugin $<TARGET_FILE:soapysdr>
                 --device remote:${_serial}
                 --report ${CMAKE_BINARY_DIR}/conformance/soapysdr_remote.json)
set_tests_properties(conformance_soapysdr_remote PROPERTIES LABELS "fast;timing" TIMEOUT 300
  ENVIRONMENT "YANTRA_FAKE_SOAPY=${_fake}")
