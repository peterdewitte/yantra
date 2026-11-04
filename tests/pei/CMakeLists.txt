# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The TETRA terminal plugin (RAD-80, TRM-01, TRM-04): the AT parser on its
# own, and the plugin end to end against the simulated terminal
# (tools/yantra-pei-sim) on a pseudo-terminal: records against the
# simulator's truth, sensors, the AT log, ERROR replies, timeouts, garbage,
# unplug/replug and the command whitelist (RAD-17). Also the plugin's
# completeness check and the conformance kit through the simulator, with
# --unplug/--replug (the plugin was added with NO_TESTS so the kit never
# opens a real serial port).
if(NOT TARGET pei OR NOT TARGET yantra_pei_sim)
  message(STATUS "test_pei: left out (no terminal plugin or simulator)")
  return()
endif()
add_executable(test_pei test_pei.cpp test_pei_at.cpp)
target_link_libraries(test_pei PRIVATE yantra_test_main yantra_warnings
  yantra::radio_host yantra_pei_sim yantra_pei_at)
target_compile_definitions(test_pei PRIVATE PEI_PLUGIN="$<TARGET_FILE:pei>")
add_dependencies(test_pei pei)
yantra_doctest_tests(test_pei GROUP test_pei LABELS fast TIMEOUT 300
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_allow "")
if(NOT YANTRA_SANITIZER STREQUAL "")
  set(_allow --allow "lib(asan|ubsan|tsan|lsan)\\.so\\.[0-9]+" --allow "libstdc\\+\\+\\.so\\.6"
             --allow "libgcc_s\\.so\\.1")
endif()
add_test(NAME completeness_pei
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/ci/check_completeness.py
                 ${_allow} $<TARGET_FILE:pei>)
set_tests_properties(completeness_pei PROPERTIES LABELS fast)

# The kit's records check wants a record per carrier every 1.25 s of wall
# time (RAD-21): label timing, like the other real-time conformance runs.
set(_link ${CMAKE_BINARY_DIR}/tests/pei_links/conformance)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests/pei_links ${CMAKE_BINARY_DIR}/conformance)
add_test(NAME conformance_pei
         COMMAND $<TARGET_FILE:yantra-pei-sim> --link ${_link}
                 --scenario ${PROJECT_SOURCE_DIR}/tests/fixtures/pei/terminal_cells.json --
                 $<TARGET_FILE:yantra-radio-host> conformance --plugin $<TARGET_FILE:pei>
                 --device pei:${_link} --report ${CMAKE_BINARY_DIR}/conformance/pei.json
                 --unplug "touch ${_link}.unplugged" --replug "rm -f ${_link}.unplugged")
set_tests_properties(conformance_pei PROPERTIES LABELS "fast;timing" TIMEOUT 300)
