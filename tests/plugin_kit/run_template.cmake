# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Script of the plugin_template test: the example technology plugin built
# out of tree with the CMake template of the plugin test kit (PLG-13),
# exactly as a plugin author would, then its ctests run.
# Inputs: SOURCE_DIR, WORK_DIR, KIT, GENERATOR, C_COMPILER, CTEST.
# Copied from yantra-v2 68ea20a: tests/plugin_template/run_template.cmake,
# adapted (example_beacon, record-file test cases).

set(_plugin ${WORK_DIR}/example_beacon)
file(REMOVE_RECURSE ${WORK_DIR})
file(MAKE_DIRECTORY ${_plugin}/src ${_plugin}/tests)
file(COPY ${SOURCE_DIR}/tools/yantra-plugin-test/template/CMakeLists.txt DESTINATION ${_plugin})
file(GLOB _sources ${SOURCE_DIR}/plugins/beacon/*.c
                   ${SOURCE_DIR}/plugins/beacon/*.inc)
file(COPY ${_sources} DESTINATION ${_plugin}/src)
file(GLOB _tests ${SOURCE_DIR}/plugins/beacon/testdata/*)
file(COPY ${_tests} DESTINATION ${_plugin}/tests)

function(step)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "failed (${_rc}): ${ARGN}\n${_out}")
  endif()
  message(STATUS "ok: ${ARGN}")
  set(_last "${_out}" PARENT_SCOPE)
endfunction()

step(${CMAKE_COMMAND} -S ${_plugin} -B ${_plugin}/build -G ${GENERATOR}
     -DCMAKE_C_COMPILER=${C_COMPILER} -DYANTRA_SDK_DIR=${SOURCE_DIR} -DYANTRA_PLUGIN_TEST=${KIT})
step(${CMAKE_COMMAND} --build ${_plugin}/build)
step(${CTEST} --test-dir ${_plugin}/build --output-on-failure)
if(NOT _last MATCHES "100% tests passed, 0 tests failed out of 2")
  message(FATAL_ERROR "expected the template's 2 tests to pass:\n${_last}")
endif()
