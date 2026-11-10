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
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.radio-api-check)
    set_tests_properties(radio_api_doxygen PROPERTIES LABELS "fast;lint;docs" TIMEOUT 300)
    yantra_build_host_tests(radio_api_doxygen)
  endif()
elseif(YANTRA_RADIOS_BUILD_TESTS)
  add_test(NAME radio_api_doxygen
           COMMAND ${YANTRA_BUILD_HOST_TEST} sh -c "echo 'SKIPPED: Doxygen was not found (RAD-97)'; exit 77")
  set_tests_properties(radio_api_doxygen PROPERTIES
    LABELS "fast;lint;docs" SKIP_RETURN_CODE 77)
  yantra_build_host_tests(radio_api_doxygen)
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

# Links between the Markdown files (RAD-135 docs job; offline: web links
# are not fetched).
if(YANTRA_RADIOS_BUILD_TESTS AND Python3_Interpreter_FOUND)
  add_test(NAME doc_links
           COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_links.py
                   ${PROJECT_SOURCE_DIR})
  set_tests_properties(doc_links PROPERTIES LABELS "fast;lint;docs")
endif()

# The plugin pages docs/plugins/*.md (settings, presets, sensors, self-test
# items) and the record schema tables of docs/TETRA_RECORDS.md against what
# the built plugins report (RAD-97, RAD-135). `ninja update-plugin-docs`
# rewrites the generated blocks of the plugin pages. Not in the sanitizer
# trees: there the self-test stream items can time out differently.
if(NOT WIN32 AND YANTRA_RADIOS_BUILD_TESTS AND Python3_Interpreter_FOUND AND YANTRA_SANITIZER STREQUAL ""
   AND TARGET hackrf_fake AND TARGET soapysdr_fake AND TARGET pei AND TARGET yantra-pei-sim
   AND TARGET simulator)
  set(_plugin_docs ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/plugin_docs.py
                   ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
  add_test(NAME plugin_docs COMMAND ${_plugin_docs})
  set_tests_properties(plugin_docs PROPERTIES LABELS "fast;docs" TIMEOUT 300)
  add_custom_target(update-plugin-docs
    COMMAND ${_plugin_docs} --update
    DEPENDS yantra-radio-host yantra-pei-sim hackrf_fake soapysdr_fake pei simulator hackrf soapysdr
    COMMENT "Rewriting the generated blocks of docs/plugins/*.md"
    VERBATIM)
elseif(WIN32 AND YANTRA_RADIOS_BUILD_TESTS)
  include(YantraTests)
  yantra_skip_test(plugin_docs REASON "the checker drives the Linux tree's .so plugins and programs; the pages are checked in the Linux trees, the plugins are the same code")
endif()

# The radio plugins manual as one PDF (RAD-97, RAD-134): `ninja
# docs-manual` writes <build>/docs/manual/yantra-radios-manual-<version>.pdf
# (pandoc and WeasyPrint, build tools of Yantra's spec 16.2) with its
# checks (tools/docs/build_manual.py); the release ships it. ctest
# radios_manual builds and checks it into its own folder,
# radios_manual_faults checks that the checker finds faults. Both report
# Skipped without pandoc or WeasyPrint; in a cross tree they run on the
# build machine (label build_host).
if(Python3_Interpreter_FOUND OR Python3_EXECUTABLE)
  set(YANTRA_RADIOS_MANUAL ${CMAKE_CURRENT_BINARY_DIR}/manual/yantra-radios-manual-${PROJECT_VERSION}.pdf
      CACHE INTERNAL "The PDF manual (ninja docs-manual)")
  add_custom_target(docs-manual
    COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/docs/build_manual.py
            --source ${PROJECT_SOURCE_DIR} --out ${CMAKE_CURRENT_BINARY_DIR}/manual --version ${PROJECT_VERSION}
    COMMENT "PDF manual in ${YANTRA_RADIOS_MANUAL}"
    VERBATIM)
  if(YANTRA_RADIOS_BUILD_TESTS)
    add_test(NAME radios_manual
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/docs/build_manual.py
                     --source ${PROJECT_SOURCE_DIR} --out ${CMAKE_CURRENT_BINARY_DIR}/manual-check
                     --version ${PROJECT_VERSION})
    add_test(NAME radios_manual_faults
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B
                     ${PROJECT_SOURCE_DIR}/tests/docs/check_manual_faults.py ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/manual-faults)
    set_tests_properties(radios_manual radios_manual_faults PROPERTIES
      LABELS "docs" SKIP_RETURN_CODE 77 TIMEOUT 600)
    yantra_build_host_tests(radios_manual radios_manual_faults)
  endif()
endif()
