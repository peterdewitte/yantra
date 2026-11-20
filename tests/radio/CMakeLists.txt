# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio parts' test suite (RAD-03, RAD-141, Yantra TST-01), part of the
# one ctest suite (added by the top-level CMakeLists.txt when
# YANTRA_RADIOS_TESTS is on): one folder per module. Every module folder
# has its own CMakeLists.txt and may call yantra_radio_add_test_dir(), which
# links every *.cpp and *.c of tests/radio/<module>/ into one doctest
# binary test_<module>. A folder may hold a LABELS file (one label per
# line: fast, slow or timing); without it the test is labelled "fast".
# fixtures/ holds the test data (YANTRA_TEST_DATA_DIR). The long binaries
# (YANTRA_PER_CASE_TESTS) get one ctest per doctest test case
# (cmake/YantraTests.cmake), so ctest -j runs their cases in parallel.
# Every test gets the label of the part it tests (_yantra_radio_areas):
# radio-api, radio-common, gen, plugin:<folder> (plugins/<folder>), docs,
# lint or packaging.
# Copied from yantra-v1 c24e8f5: tests/CMakeLists.txt, adapted (vendored
# doctest, C sources, no core library).

yantra_radio_directory()
include(YantraTests)

set(YANTRA_PER_CASE_TESTS decode_iq gen_sim receiver_tetra tetra_decoder tetra_engine yantra_gen)

# Area labels of the module folders (PLG-29).
set(_yantra_radio_areas
  radio_api_header=radio-api radio_host_kit=radio-api clean_env=radio-api
  hackrf=plugin:hackrf yantra_hackrf=plugin:hackrf
  soapysdr=plugin:soapy soapy_remote=plugin:soapy
  pei=plugin:pei simulator=plugin:simulator
  bench=radio-common capture_iq=radio-common decode_iq=radio-common deps=radio-common
  fake_soapy=radio-common fuzz=radio-common radios_common=radio-common receiver=radio-common
  receiver_tetra=radio-common robustness=radio-common rust=radio-common sens=radio-common
  soapy_deps=radio-common tetra_decoder=radio-common tetra_engine=radio-common
  tetra_plugin=radio-common
  gen=gen gen_osmo=gen gen_sim=gen yantra_gen=gen
  docs=docs completeness=packaging install=packaging release=packaging sdk=packaging)
function(_yantra_radio_area module var)
  set(_area radio-common)
  foreach(_m IN LISTS _yantra_radio_areas)
    if(_m MATCHES "^${module}=(.*)$")
      set(_area ${CMAKE_MATCH_1})
    endif()
  endforeach()
  set(${var} ${_area} PARENT_SCOPE)
endfunction()
set(YANTRA_RADIO_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(yantra_radio_test_main STATIC test_main.cpp)
target_link_libraries(yantra_radio_test_main PUBLIC yantra_doctest yantra_warnings)
# golden.hpp (the --update option of every test binary, RAD-93)
target_include_directories(yantra_radio_test_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# yantra/gen/portable_math.hpp (header only: FMA3 math paths off on Windows)
target_include_directories(yantra_radio_test_main PRIVATE ${PROJECT_SOURCE_DIR}/gen/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # doctest 2.4.12 includes <ciso646>, which libstdc++ flags with #warning
  # under C++20; clang does not silence it for system headers.
  target_compile_options(yantra_radio_test_main PUBLIC "-Wno-#warnings")
endif()
target_compile_definitions(yantra_radio_test_main PUBLIC
  YANTRA_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  YANTRA_BINARY_DIR="${PROJECT_BINARY_DIR}"
  YANTRA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  YANTRA_TEST_TIME_SCALE=${YANTRA_TEST_TIME_SCALE})

# yantra_radio_add_test_dir(<module> [LIBS ...])
function(yantra_radio_add_test_dir module)
  cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
  set(_dir ${YANTRA_RADIO_TESTS_DIR}/${module})
  file(GLOB _srcs CONFIGURE_DEPENDS ${_dir}/*.cpp ${_dir}/*.c)
  if(NOT _srcs)
    return()
  endif()
  set(_target test_${module})
  add_executable(${_target} ${_srcs})
  target_link_libraries(${_target} PRIVATE yantra_radio_test_main yantra_warnings ${ARG_LIBS})
  set(_labels fast)
  if(EXISTS ${_dir}/LABELS)
    file(STRINGS ${_dir}/LABELS _labels)
  endif()
  _yantra_radio_area(${module} _area)
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
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_child} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_child}/CMakeLists.txt)
    # The terminal plugin's tests only when it is built (YANTRA_RADIOS_PEI).
    if(_child STREQUAL "pei" AND NOT TARGET pei)
      continue()
    endif()
    _yantra_radio_area(${_child} _area)
    yantra_add_area_subdirectory(${_child} ${_area})
  endif()
endforeach()
