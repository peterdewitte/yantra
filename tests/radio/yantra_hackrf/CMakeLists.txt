# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-hackrf (RAD-66): every command in-process against the HackRF
# plugin's test build hackrf_fake and, for capture, the fake module
# linked in as driver "hackrf" (RAD-92); plus the tool's usage exit code.
if(NOT TARGET yantra_hackrf_app OR NOT TARGET hackrf_fake)
  message(STATUS "test_yantra_hackrf: left out (no yantra-hackrf)")
  return()
endif()
add_executable(test_yantra_hackrf test_yantra_hackrf.cpp)
target_link_libraries(test_yantra_hackrf PRIVATE yantra_radio_test_main yantra_warnings
  yantra_hackrf_app yantra_fake_soapy_hackrf_whole yantra::json)
target_compile_definitions(test_yantra_hackrf PRIVATE
  HACKRF_FAKE_PLUGIN="$<TARGET_FILE:hackrf_fake>")
add_dependencies(test_yantra_hackrf hackrf_fake)
yantra_doctest_tests(test_yantra_hackrf GROUP test_yantra_hackrf LABELS fast TIMEOUT 300
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR})

add_test(NAME yantra_hackrf_usage COMMAND yantra-hackrf)
set_tests_properties(yantra_hackrf_usage PROPERTIES LABELS fast
  PASS_REGULAR_EXPRESSION "exit codes: 0 pass, 1 fail, 2 no device, 3 usage error")
add_test(NAME yantra_hackrf_usage_code
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/radio/test_command.py --exit-code 3 --quiet-stderr -- $<TARGET_FILE:yantra-hackrf>)
set_tests_properties(yantra_hackrf_usage_code PROPERTIES LABELS fast)
