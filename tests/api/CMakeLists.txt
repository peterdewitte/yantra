# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# TST-51: the socket-client test of the remote API (tests/api/
# api_client_test.py, Python standard library) against yantra-probe in
# demo mode with the probe plugin bound to loopback; and the probe
# plugin's parts (auth, routing, live view) as unit tests.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME api_client
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/api_client_test.py
          $<TARGET_FILE:yantra-probe> ${PROJECT_BINARY_DIR}/lib/yantra/plugins ${CMAKE_CURRENT_BINARY_DIR}/api_client)
set_tests_properties(api_client PROPERTIES LABELS fast TIMEOUT 300 PROCESSORS 2)

file(GLOB _srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(test_api ${_srcs})
target_link_libraries(test_api PRIVATE yantra_test_main yantra_warnings yantra_probe_api yantra::json)
add_dependencies(test_api yantra_probe yantra-probe)
add_test(NAME test_api COMMAND test_api)
set_tests_properties(test_api PROPERTIES LABELS fast TIMEOUT 300 WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
