# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Documentation targets and checks (RAD-97). docs-radio-api builds the
# radio API reference with Doxygen from include/yantra/radio.h into
# <build>/docs/radio-api/html; the radio_api_doxygen test fails on any
# Doxygen warning, so every function, struct, field, macro and callback of
# the header stays documented. Without Doxygen the test reports Skipped.
# doc_snippets checks that the code quoted in docs/RADIO_PLUGINS.md is the
# compiled code of the beacon example plugin (RAD-64).
# Copied from yantra 68ea20a: docs/CMakeLists.txt, adapted for RAD-97.

set(YANTRA_RADIO_API_VERSION "")
file(STRINGS ${PROJECT_SOURCE_DIR}/include/yantra/radio.h _api_lines
     REGEX "#define YR_API_VERSION_(MAJOR|MINOR) ")
foreach(_line IN LISTS _api_lines)
  string(REGEX MATCH "#define YR_API_VERSION_[A-Z]+ ([0-9]+)u" _ ${_line})
  if(YANTRA_RADIO_API_VERSION)
    set(YANTRA_RADIO_API_VERSION "${YANTRA_RADIO_API_VERSION}.${CMAKE_MATCH_1}")
  else()
    set(YANTRA_RADIO_API_VERSION "${CMAKE_MATCH_1}")
  endif()
endforeach()

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/radio-api)
  configure_file(api/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api @ONLY)
  add_custom_target(docs-radio-api
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api
    DEPENDS ${PROJECT_SOURCE_DIR}/include/yantra/radio.h
    COMMENT "Radio API reference (Doxygen) in ${DOXYGEN_OUTPUT}/html"
    VERBATIM)
  # The test writes to its own folder, so it never races with the target.
  set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/radio-api-check)
  configure_file(api/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api-check @ONLY)
  if(YANTRA_RADIOS_BUILD_TESTS)
    add_test(NAME radio_api_doxygen
             COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api-check)
    set_tests_properties(radio_api_doxygen PROPERTIES LABELS "fast;lint;docs" TIMEOUT 300)
  endif()
elseif(YANTRA_RADIOS_BUILD_TESTS)
  add_test(NAME radio_api_doxygen
           COMMAND sh -c "echo 'SKIPPED: Doxygen was not found (RAD-97)'; exit 77")
  set_tests_properties(radio_api_doxygen PROPERTIES
    LABELS "fast;lint;docs" SKIP_RETURN_CODE 77)
endif()

# Code quoted in the tutorial must be the real, compiled code (RAD-97).
if(YANTRA_RADIOS_BUILD_TESTS)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME doc_snippets
             COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_snippets.py
                     ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/docs/RADIO_PLUGINS.md)
    set_tests_properties(doc_snippets PROPERTIES LABELS "fast;lint;docs")
  else()
    add_test(NAME doc_snippets
             COMMAND sh -c "echo 'SKIPPED: Python 3 was not found'; exit 77")
    set_tests_properties(doc_snippets PROPERTIES LABELS "fast;lint;docs" SKIP_RETURN_CODE 77)
  endif()
endif()
