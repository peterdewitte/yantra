# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Test suite (TST-01): one folder per core module, picked up by a glob.
# Every *.cpp and *.c in tests/<module>/ is linked into one doctest binary
# test_<module>. A folder may hold a LABELS file (one label per line:
# fast, slow, timing); without it the test is labelled "fast". A folder
# with its own CMakeLists.txt is added as a subdirectory instead and may
# call yantra_add_test_dir() itself. fixtures/ holds test data. The long
# binaries (YANTRA_PER_CASE_TESTS) get one ctest per doctest test case
# (cmake/YantraTests.cmake), so ctest -j runs their cases in parallel.
# The radio parts' tests are in tests/radio (their own harness, added by
# the top-level CMakeLists.txt). Every test gets the label of its area
# (_yantra_test_area below): app, core, docs, lint, packaging or
# plugin:<name> (PLG-29).
# Copied from yantra-radios fb194f5: tests/CMakeLists.txt, adapted (core
# library, SIMD expectation, app smoke test).

include(YantraTests)

set(YANTRA_PER_CASE_TESTS calibration measure recording)

# Plugin file names in the tests: "<name>" YANTRA_PLUGIN_SUFFIX (.so on
# Linux, .dll on Windows).
add_compile_definitions(YANTRA_PLUGIN_SUFFIX="${CMAKE_SHARED_MODULE_SUFFIX}")

add_library(yantra_test_main STATIC test_main.cpp)
target_link_libraries(yantra_test_main PUBLIC yantra_doctest yantra_warnings)
# golden.hpp (the --update option of every test binary)
target_include_directories(yantra_test_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # doctest 2.4.12 includes <ciso646>, which libstdc++ flags with #warning
  # under C++20; clang does not silence it for system headers.
  target_compile_options(yantra_test_main PUBLIC "-Wno-#warnings")
endif()
if(YANTRA_SIMD)
  set(_yantra_expect_simd 1)
else()
  set(_yantra_expect_simd 0)
endif()
# The radio plugins are built (tests/radio_plugins.hpp; PLG-30: without
# them the cases that load one are left out).
if(TARGET yantra_radio_plugins)
  set(_yantra_radio_plugins 1)
else()
  set(_yantra_radio_plugins 0)
endif()
target_compile_definitions(yantra_test_main PUBLIC
  YANTRA_TEST_RADIO_PLUGINS=${_yantra_radio_plugins}
  YANTRA_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  YANTRA_BINARY_DIR="${PROJECT_BINARY_DIR}"
  YANTRA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  YANTRA_TEST_TIME_SCALE=${YANTRA_TEST_TIME_SCALE}
  YANTRA_TEST_EXPECT_SIMD=${_yantra_expect_simd})

# The area label of a test folder (default core).
set(_yantra_test_areas
  about=app app=app headless=app
  api=plugin:probe tetra=plugin:tetra
  doc_examples=docs lint=lint install=packaging plugins_off=packaging
  radio_api=radio-api plugin_api=plugin-api)
function(_yantra_test_area module var)
  set(_area core)
  foreach(_m IN LISTS _yantra_test_areas)
    if(_m MATCHES "^${module}=(.*)$")
      set(_area ${CMAKE_MATCH_1})
    endif()
  endforeach()
  set(${var} ${_area} PARENT_SCOPE)
endfunction()

# yantra_add_test_dir(<module> [LIBS ...])
function(yantra_add_test_dir module)
  cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
  set(_dir ${PROJECT_SOURCE_DIR}/tests/${module})
  file(GLOB _srcs CONFIGURE_DEPENDS ${_dir}/*.cpp ${_dir}/*.c)
  if(NOT _srcs)
    return()
  endif()
  set(_target test_${module})
  add_executable(${_target} ${_srcs})
  target_link_libraries(${_target} PRIVATE yantra_test_main yantra_warnings ${ARG_LIBS})
  set(_labels fast)
  if(EXISTS ${_dir}/LABELS)
    file(STRINGS ${_dir}/LABELS _labels)
  endif()
  _yantra_test_area(${module} _area)
  list(APPEND _labels ${_area})
  if(module IN_LIST YANTRA_PER_CASE_TESTS)
    yantra_doctest_tests(${_target} GROUP ${_target} LABELS ${_labels} TIMEOUT 600
      WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
  else()
    add_test(NAME ${_target} COMMAND ${_target})
    set_tests_properties(${_target} PROPERTIES LABELS "${_labels}" TIMEOUT 600
      WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
  endif()
endfunction()

file(GLOB _children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(_child ${_children})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_child} AND NOT _child STREQUAL "fixtures"
     AND NOT _child STREQUAL "radio")
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_child}/CMakeLists.txt)
      _yantra_test_area(${_child} _area)
      yantra_add_area_subdirectory(${_child} ${_area})
    else()
      yantra_add_test_dir(${_child} LIBS yantra::core yantra::plugin_api)
    endif()
  endif()
endforeach()

# Smoke test of the application binary (only when it is built).
if(TARGET yantra)
  add_test(NAME app_version COMMAND yantra --version)
  set_tests_properties(app_version PROPERTIES
    LABELS "fast;app"
    PASS_REGULAR_EXPRESSION "^yantra ${YANTRA_VERSION}\n$")
endif()
