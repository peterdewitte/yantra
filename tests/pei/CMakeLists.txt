# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The TETRA terminal plugin (RAD-80, TRM-01, TRM-04): the AT parser and the
# serial-port layer on their own, and the plugin end to end against the
# simulated terminal (tools/yantra-pei-sim) on a pseudo-terminal (Linux)
# or a named pipe (Windows, through the Win32 serial-port code): records against the
# simulator's truth, sensors, the AT log, ERROR replies, timeouts, garbage,
# unplug/replug and the command whitelist (RAD-17). Also the plugin's
# completeness check and the conformance kit through the simulator, with
# --unplug/--replug (the plugin was added with NO_TESTS so the kit never
# opens a real serial port).
if(NOT TARGET pei)
  message(STATUS "test_pei: left out (no terminal plugin)")
  return()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_allow "")
if(NOT YANTRA_SANITIZER STREQUAL "")
  set(_allow --allow "lib(asan|ubsan|tsan|lsan)\\.so\\.[0-9]+" --allow "libstdc\\+\\+\\.so\\.6"
             --allow "libgcc_s\\.so\\.1")
endif()
if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  list(APPEND _allow --allow-build-paths) # gcc's ASan keeps the source paths
endif()
add_test(NAME completeness_pei
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/ci/check_completeness.py
                 ${YANTRA_COMPLETENESS_PLATFORM} ${_allow} $<TARGET_FILE:pei>)
set_tests_properties(completeness_pei PROPERTIES LABELS fast)

add_executable(test_pei test_pei.cpp test_pei_at.cpp test_pei_port.cpp)
target_link_libraries(test_pei PRIVATE yantra_test_main yantra_warnings
  yantra::radio_host yantra_pei_sim yantra_pei_at yantra_pei_port)
target_compile_definitions(test_pei PRIVATE PEI_PLUGIN="$<TARGET_FILE:pei>")
add_dependencies(test_pei pei)
yantra_doctest_tests(test_pei GROUP test_pei LABELS fast TIMEOUT 300
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# The terminal's port: a symlink to a pseudo-terminal, or on Windows a
# named pipe (\\.\pipe\..., one per tree); the control file unplugs it.
set(_ctl ${CMAKE_BINARY_DIR}/tests/pei_links/conformance.unplugged)
if(WIN32)
  string(MD5 _tree "${CMAKE_BINARY_DIR}")
  string(SUBSTRING "${_tree}" 0 8 _tree)
  set(_link "\\\\.\\pipe\\yantra-pei-conformance-${_tree}")
else()
  set(_link ${CMAKE_BINARY_DIR}/tests/pei_links/conformance)
endif()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests/pei_links ${CMAKE_BINARY_DIR}/conformance)
# The kit's records check wants a record per carrier every 1.25 s of wall
# time (RAD-21): label timing, like the other real-time conformance runs.
add_test(NAME conformance_pei
         COMMAND $<TARGET_FILE:yantra-pei-sim> --link ${_link} --control ${_ctl}
                 --scenario ${PROJECT_SOURCE_DIR}/tests/fixtures/pei/terminal_cells.json --
                 $<TARGET_FILE:yantra-radio-host> conformance --plugin $<TARGET_FILE:pei>
                 --device pei:${_link} --report ${CMAKE_BINARY_DIR}/conformance/pei.json
                 --unplug "\"${CMAKE_COMMAND}\" -E touch \"${_ctl}\""
                 --replug "\"${CMAKE_COMMAND}\" -E rm -f \"${_ctl}\"")
set_tests_properties(conformance_pei PROPERTIES LABELS "fast;timing" TIMEOUT 300)
