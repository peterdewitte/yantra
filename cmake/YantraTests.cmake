# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ctest helpers (RAD-03, RAD-90, Yantra TST-01): parallel doctest cases and
# opt-in suites. CONTRIBUTING.md "Building and testing" has the commands.
#
# yantra_doctest_tests(<target> GROUP <name> [FILTERS <doctest args>...]
#                      [LABELS <l>...] [TIMEOUT <s>] [RUN_SERIAL] [OPT_IN]
#                      [PROCESSORS <n>] [WORKING_DIRECTORY <dir>])
#   One ctest "<name>/<case>" per doctest test case of <target> that passes
#   FILTERS (e.g. --test-suite=slow), so ctest -j runs the cases of a long
#   binary in parallel. The cases are listed after every link
#   (cmake/doctest_discover.py); every ctest runs the binary with FILTERS
#   and --test-case=<case>, so the group checks exactly what one ctest
#   "<target> FILTERS" did. COST comes from tests/ctest_costs.txt (the
#   longest start first in a fresh tree; afterwards ctest's own cost data).
#   PROCESSORS: the ctest PROCESSORS property of every case, for cases that
#   keep several threads busy (a plugin's stream thread, its engine worker
#   pool and the fake device), so `ctest -j N` does not start more of them
#   than the machine runs and the hang guards stay far away.
#
# yantra_add_opt_in_test(<name> COMMAND <cmd>... [LABELS <l>...]
#                        [TIMEOUT <s>] [RUN_SERIAL] [COST <c>])
#   A plain ctest that is OPT_IN.
#
# OPT_IN: the test gets the label opt_in and exists only when the
# environment variable YANTRA_TESTS_OPT_IN is set to a true value when
# ctest runs (the long robustness and sensitivity suites): the default
# `ctest` leaves them out, `YANTRA_TESTS_OPT_IN=1 ctest -L opt_in` runs them.

# YANTRA_TEST_TIME_SCALE: the sanitizer trees (Debug, ASan/TSan) run 5-60x
# slower; their hang guards scale with it: the TIMEOUT of the per-case
# ctests and the tests' own stream limits (C++ YANTRA_TEST_TIME_SCALE).
# 5 for ASan/UBSan, 10 for TSan (the DSP runs ~2x slower again under TSan:
# the HackRF records case took 298 s alone and over 600 s under ctest -j).
# Only guards: no check or tolerance changes, the real-time checks are
# label timing and left out of those trees.
if(YANTRA_SANITIZER STREQUAL "")
  set(_yantra_scale 1)
elseif(YANTRA_SANITIZER STREQUAL "tsan")
  set(_yantra_scale 10)
else()
  set(_yantra_scale 5)
endif()
set(YANTRA_TEST_TIME_SCALE ${_yantra_scale} CACHE STRING
    "Factor on the tests' hang guards (5 in ASan, 10 in TSan trees)")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(YANTRA_DOCTEST_DISCOVER ${CMAKE_CURRENT_LIST_DIR}/doctest_discover.py)
set(YANTRA_TEST_COSTS ${PROJECT_SOURCE_DIR}/tests/ctest_costs.txt)
set(_yantra_opt_in_if [=[set(_yantra_opt_in "$ENV{YANTRA_TESTS_OPT_IN}")
if(_yantra_opt_in)
]=])

function(yantra_doctest_tests target)
  cmake_parse_arguments(ARG "RUN_SERIAL;OPT_IN" "GROUP;TIMEOUT;PROCESSORS;WORKING_DIRECTORY" "FILTERS;LABELS" ${ARGN})
  if(NOT ARG_GROUP)
    message(FATAL_ERROR "yantra_doctest_tests(${target}): GROUP is required")
  endif()
  set(_labels ${ARG_LABELS})
  if(ARG_OPT_IN)
    list(APPEND _labels opt_in)
  endif()
  list(JOIN _labels "," _labels_arg)
  set(_gen ${CMAKE_CURRENT_BINARY_DIR}/${ARG_GROUP}_tests.cmake)
  set(_opts)
  if(ARG_TIMEOUT)
    math(EXPR _timeout "${ARG_TIMEOUT} * ${YANTRA_TEST_TIME_SCALE}")
    list(APPEND _opts --timeout ${_timeout})
  endif()
  if(ARG_RUN_SERIAL)
    list(APPEND _opts --run-serial)
  endif()
  if(ARG_PROCESSORS)
    list(APPEND _opts --processors ${ARG_PROCESSORS})
  endif()
  if(ARG_WORKING_DIRECTORY)
    list(APPEND _opts --workdir ${ARG_WORKING_DIRECTORY})
  endif()
  # A tree with CMAKE_CROSSCOMPILING_EMULATOR (YANTRA_WINDOWS_TEST_RUNNER
  # wsl) lists and runs its .exe through it.
  foreach(_e ${CMAKE_CROSSCOMPILING_EMULATOR})
    list(APPEND _opts --emulator=${_e})
  endforeach()
  # A cross-compiled tree without one (the Windows tree, run in the test
  # VM) cannot run its binaries here: the build writes one ctest for the
  # group (<group>/all), and ctest on the target lists the cases when it
  # reads the tests (--at-test-time, <group>_cases.cmake), so there every
  # case is its own ctest as on Linux.
  set(_at_test_time OFF)
  if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set(_at_test_time ON)
    list(APPEND _opts --group-only)
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${YANTRA_DOCTEST_DISCOVER} --exe $<TARGET_FILE:${target}>
            --group ${ARG_GROUP} --out ${_gen} --labels ${_labels_arg} --costs ${YANTRA_TEST_COSTS}
            ${_opts} -- ${ARG_FILTERS}
    COMMENT "Listing the doctest cases of ${ARG_GROUP}"
    VERBATIM)
  # A new cost table or discovery script relinks, so the list is rewritten.
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${YANTRA_TEST_COSTS} ${YANTRA_DOCTEST_DISCOVER})
  # Before the first build the binary and its list do not exist: one
  # failing placeholder says so instead of silently running nothing.
  set(_body "if(EXISTS \"${_gen}\")\n  include(\"${_gen}\")\nelse()\n  add_test(${ARG_GROUP}_NOT_BUILT ${ARG_GROUP}_NOT_BUILT)\nendif()\n")
  if(_at_test_time)
    set(_cases ${CMAKE_CURRENT_BINARY_DIR}/${ARG_GROUP}_cases.cmake)
    set(_list_args --exe $<TARGET_FILE:${target}> --group ${ARG_GROUP} --out ${_cases}
                   --labels ${_labels_arg} --costs ${YANTRA_TEST_COSTS} ${_opts} --at-test-time -- ${ARG_FILTERS})
    list(REMOVE_ITEM _list_args --group-only)
    set(_list_cmd "")
    foreach(_a ${Python3_EXECUTABLE} ${YANTRA_DOCTEST_DISCOVER} ${_list_args})
      string(APPEND _list_cmd " [==[${_a}]==]")
    endforeach()
    # Listed again when the binary is newer than the list; the script
    # writes nothing where it cannot run the binary (Linux), and a failed
    # listing leaves the group test, which then fails with the reason.
    set(_body "if(EXISTS [==[$<TARGET_FILE:${target}>]==] AND (NOT EXISTS [==[${_cases}]==] OR [==[$<TARGET_FILE:${target}>]==] IS_NEWER_THAN [==[${_cases}]==]))
  execute_process(COMMAND${_list_cmd} RESULT_VARIABLE _yantra_rc OUTPUT_VARIABLE _yantra_out ERROR_VARIABLE _yantra_out)
  if(NOT _yantra_rc EQUAL 0 AND NOT _yantra_rc EQUAL 77)
    message(WARNING \"${ARG_GROUP}: listing the doctest cases failed (\${_yantra_rc}): \${_yantra_out}\")
  endif()
endif()
if(EXISTS [==[${_cases}]==])
  include([==[${_cases}]==])
else()
${_body}endif()
")
  endif()
  if(ARG_OPT_IN)
    set(_body "${_yantra_opt_in_if}${_body}endif()\n")
  endif()
  set(_inc ${CMAKE_CURRENT_BINARY_DIR}/${ARG_GROUP}_include.cmake)
  if(_at_test_time)
    file(GENERATE OUTPUT ${_inc} CONTENT "${_body}")
  else()
    file(WRITE ${_inc} "${_body}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${_inc})
endfunction()

function(yantra_add_opt_in_test name)
  cmake_parse_arguments(ARG "RUN_SERIAL" "TIMEOUT;COST" "COMMAND;LABELS" ${ARGN})
  set(_cmd)
  list(GET ARG_COMMAND 0 _first)
  if(TARGET ${_first})
    get_target_property(_type ${_first} TYPE)
    if(_type STREQUAL "EXECUTABLE")
      foreach(_e ${CMAKE_CROSSCOMPILING_EMULATOR})
        string(APPEND _cmd " [==[${_e}]==]")
      endforeach()
    endif()
  endif()
  foreach(_a ${ARG_COMMAND})
    if(TARGET ${_a})
      set(_a $<TARGET_FILE:${_a}>)
    endif()
    string(APPEND _cmd " [==[${_a}]==]")
  endforeach()
  set(_labels ${ARG_LABELS} opt_in)
  set(_props "LABELS [==[${_labels}]==]")
  if(ARG_TIMEOUT)
    string(APPEND _props " TIMEOUT ${ARG_TIMEOUT}")
  endif()
  if(ARG_RUN_SERIAL)
    string(APPEND _props " RUN_SERIAL TRUE")
  endif()
  if(ARG_COST)
    string(APPEND _props " COST ${ARG_COST}")
  endif()
  set(_inc ${CMAKE_CURRENT_BINARY_DIR}/${name}_include.cmake)
  file(GENERATE OUTPUT ${_inc} CONTENT
    "${_yantra_opt_in_if}  add_test([==[${name}]==]${_cmd})\n  set_tests_properties([==[${name}]==] PROPERTIES ${_props})\nendif()\n")
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${_inc})
endfunction()

# yantra_skip_test(<name> REASON <text> [LABELS <l>...])
#
# A test that does not apply on this platform (e.g. a Linux-only check in
# the Windows tree): it stays in the list and ctest reports it as skipped
# (exit code 77, SKIP_RETURN_CODE) with the reason in its output, so a
# platform's skipped tests are visible instead of silently missing.
function(yantra_skip_test name)
  cmake_parse_arguments(ARG "" "REASON" "LABELS" ${ARGN})
  if(NOT ARG_REASON)
    message(FATAL_ERROR "yantra_skip_test(${name}): REASON is required")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME ${name}
           COMMAND ${Python3_EXECUTABLE} -c "import sys; print(sys.argv[1]); sys.exit(77)"
                   "skipped on ${CMAKE_SYSTEM_NAME}: ${ARG_REASON}")
  set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 LABELS "skipped;${ARG_LABELS}")
endfunction()
