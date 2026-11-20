# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# radio-api/: the radio API (PLG-29, RAD-02, RAD-10), the only interface
# between Yantra and a radio plugin: the C ABI header yantra/radio.h with
# its C++ helpers radio.hpp, nothing to link. The application, the radio
# plugins and the radio tools all use it through yantra::radio_api; the
# radio API keeps its own ABI version (YR_API_VERSION_MAJOR/MINOR in
# radio.h), separate from the product version.
#
#   yantra::radio_api            interface target (include folder)
#   YANTRA_RADIO_API_VERSION_MAJOR, _MINOR  parsed from radio.h
#   docs-radio-api               the API reference with Doxygen (RAD-97)
#                                in <build>/radio-api/radio-api/html
#   ctest radio_api_doxygen      fails on any Doxygen warning, so every
#                                function, struct, field, macro and
#                                callback of the header stays documented
# The header tests and the conformance kit (tools/yantra-radio-host) are
# in tests/radio (radio_api_header, radio_host_kit).

add_library(yantra_radio_api INTERFACE)
add_library(yantra::radio_api ALIAS yantra_radio_api)
target_include_directories(yantra_radio_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/include/yantra/radio.h _yr_major REGEX "^#define YR_API_VERSION_MAJOR ")
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/include/yantra/radio.h _yr_minor REGEX "^#define YR_API_VERSION_MINOR ")
string(REGEX MATCH "[0-9]+" _yr_major "${_yr_major}")
string(REGEX MATCH "[0-9]+" _yr_minor "${_yr_minor}")
set(YANTRA_RADIO_API_VERSION_MAJOR ${_yr_major} PARENT_SCOPE)
set(YANTRA_RADIO_API_VERSION_MINOR ${_yr_minor} PARENT_SCOPE)
set(YANTRA_RADIO_API_VERSION "${_yr_major}.${_yr_minor}")

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/radio-api)
  configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api @ONLY)
  add_custom_target(docs-radio-api
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/yantra/radio.h
    COMMENT "Radio API reference (Doxygen) in ${DOXYGEN_OUTPUT}/html"
    VERBATIM)
  # The test writes to its own folder, so it never races with the target.
  set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/radio-api-check)
  configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api-check @ONLY)
  if(YANTRA_RADIOS_BUILD_TESTS)
    add_test(NAME radio_api_doxygen
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api-check)
    set_tests_properties(radio_api_doxygen PROPERTIES LABELS "fast;lint;docs;radio-api" TIMEOUT 300)
    yantra_build_host_tests(radio_api_doxygen)
  endif()
elseif(YANTRA_RADIOS_BUILD_TESTS)
  add_test(NAME radio_api_doxygen
           COMMAND ${YANTRA_BUILD_HOST_TEST} sh -c "echo 'SKIPPED: Doxygen was not found (RAD-97)'; exit 77")
  set_tests_properties(radio_api_doxygen PROPERTIES
    LABELS "fast;lint;docs;radio-api" SKIP_RETURN_CODE 77)
  yantra_build_host_tests(radio_api_doxygen)
endif()
