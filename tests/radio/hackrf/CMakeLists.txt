# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The HackRF plugin against the fake module registered as driver
# "hackrf" (RAD-60, RAD-92): per-case doctests, the conformance kit with
# unplug/replug, and the completeness check of the test build.
if(NOT TARGET hackrf_fake)
  message(STATUS "test_hackrf: left out (no hackrf plugin)")
  return()
endif()
add_executable(test_hackrf test_hackrf.cpp)
target_link_libraries(test_hackrf PRIVATE yantra_radio_test_main yantra_warnings
  yantra::radio_host yantra::gen)
target_compile_definitions(test_hackrf PRIVATE HACKRF_FAKE_PLUGIN="$<TARGET_FILE:hackrf_fake>")
add_dependencies(test_hackrf hackrf_fake)
# Real-time checks are TEST_SUITE("timing"): label timing, left out of the
# sanitizer trees. A streaming case keeps two threads busy (the plugin's
# stream thread, which also runs the fake module's generator, and its
# engine worker; threads = 2): PROCESSORS 2 keeps `ctest -j$(nproc)` from
# oversubscribing the machine, which made
# the records case reach its 600 s hang guard once in the asan-ubsan tree
# (166 s alone).
yantra_doctest_tests(test_hackrf GROUP test_hackrf FILTERS --test-suite-exclude=timing
  LABELS fast TIMEOUT 300 PROCESSORS 2 WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
yantra_doctest_tests(test_hackrf GROUP test_hackrf_timing FILTERS --test-suite=timing
  LABELS timing TIMEOUT 120 RUN_SERIAL WORKING_DIRECTORY ${PROJECT_BINARY_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_allow "")
if(NOT YANTRA_SANITIZER STREQUAL "")
  set(_allow --allow "lib(asan|ubsan|tsan|lsan)\\.so\\.[0-9]+" --allow "libstdc\\+\\+\\.so\\.6"
             --allow "libgcc_s\\.so\\.1")
endif()
if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  list(APPEND _allow --allow-build-paths) # gcc's ASan keeps the source paths
endif()
add_test(NAME completeness_hackrf_fake
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/ci/check_completeness.py
                 ${YANTRA_COMPLETENESS_PLATFORM} ${_allow} $<TARGET_FILE:hackrf_fake>)
set_tests_properties(completeness_hackrf_fake PROPERTIES LABELS fast)

# Conformance with a fake HackRF One, unplugged and replugged by a
# control file (RAD-35).
set(_ctl ${PROJECT_BINARY_DIR}/tests/hackrf_control/conformance)
set(_serial 0000000000000000a1b2c3d4e5f60001)
set(_fake "{\"control\":\"${_ctl}\",\"devices\":[{\"serial\":\"${_serial}\",\"scenario\":\"${PROJECT_SOURCE_DIR}/tests/radio/fixtures/soapy/fake_two_cells.json\"}]}")
add_test(NAME conformance_hackrf_fake
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/radio/test_command.py --mkdir ${_ctl} --remove ${_ctl}/${_serial}.unplugged --
                 $<TARGET_FILE:yantra-radio-host> conformance --plugin $<TARGET_FILE:hackrf_fake>
                 --report ${PROJECT_BINARY_DIR}/conformance/hackrf_fake.json
                 --unplug "\"${CMAKE_COMMAND}\" -E touch \"${_ctl}/${_serial}.unplugged\""
                 --replug "\"${CMAKE_COMMAND}\" -E rm -f \"${_ctl}/${_serial}.unplugged\"")
set_tests_properties(conformance_hackrf_fake PROPERTIES LABELS "fast;timing" TIMEOUT 300
  ENVIRONMENT "YANTRA_FAKE_SOAPY=${_fake}")
