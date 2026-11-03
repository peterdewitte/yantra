# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Test suite (RAD-03, Yantra TST-01): one folder per module, picked up by a
# glob. Every *.cpp and *.c in tests/<module>/ is linked into one doctest
# binary test_<module>. A folder may hold a LABELS file (one label per
# line: fast, slow or timing); without it the test is labelled "fast". A
# folder with its own CMakeLists.txt is added as a subdirectory instead and
# may call yantra_add_test_dir() itself. fixtures/ holds test data.
# Copied from yantra-v1 c24e8f5: tests/CMakeLists.txt, adapted (vendored
# doctest, C sources, no core library).

add_library(yantra_doctest INTERFACE)
target_include_directories(yantra_doctest SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/third_party/doctest)

add_library(yantra_test_main STATIC test_main.cpp)
target_link_libraries(yantra_test_main PUBLIC yantra_doctest yantra_warnings)
# golden.hpp (the --update option of every test binary, RAD-93)
target_include_directories(yantra_test_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # doctest 2.4.12 includes <ciso646>, which libstdc++ flags with #warning
  # under C++20; clang does not silence it for system headers.
  target_compile_options(yantra_test_main PUBLIC "-Wno-#warnings")
endif()
target_compile_definitions(yantra_test_main PUBLIC
  YANTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  YANTRA_BINARY_DIR="${CMAKE_BINARY_DIR}"
  YANTRA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# yantra_add_test_dir(<module> [LIBS ...])
function(yantra_add_test_dir module)
  cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
  set(_dir ${CMAKE_SOURCE_DIR}/tests/${module})
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
  add_test(NAME ${_target} COMMAND ${_target})
  set_tests_properties(${_target} PROPERTIES LABELS "${_labels}" TIMEOUT 600
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

file(GLOB _children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(_child ${_children})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_child} AND NOT _child STREQUAL "fixtures")
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_child}/CMakeLists.txt)
      add_subdirectory(${_child})
    else()
      yantra_add_test_dir(${_child} LIBS yantra::radio_api)
    endif()
  endif()
endforeach()
