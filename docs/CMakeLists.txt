# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Documentation targets and checks (DOC-17). docs-plugin-api builds the
# plugin API reference with Doxygen from include/yantra/plugin.h and
# plugin.hpp into <build>/docs/plugin-api/html; the plugin_api_doxygen test
# fails on any Doxygen warning, so every function, struct, field, macro
# and callback of the headers stays documented. Without Doxygen the test
# reports Skipped.
# Copied from yantra-radios fb194f5: docs/CMakeLists.txt, adapted for
# DOC-17.
set(YANTRA_PLUGIN_API_VERSION "")
file(STRINGS ${PROJECT_SOURCE_DIR}/include/yantra/plugin.h _api_lines
     REGEX "#define YANTRA_PLUGIN_API_VERSION_(MAJOR|MINOR) ")
foreach(_line IN LISTS _api_lines)
  string(REGEX MATCH "#define YANTRA_PLUGIN_API_VERSION_[A-Z]+ ([0-9]+)u" _ ${_line})
  if(YANTRA_PLUGIN_API_VERSION)
    set(YANTRA_PLUGIN_API_VERSION "${YANTRA_PLUGIN_API_VERSION}.${CMAKE_MATCH_1}")
  else()
    set(YANTRA_PLUGIN_API_VERSION "${CMAKE_MATCH_1}")
  endif()
endforeach()

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/plugin-api)
  configure_file(api/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.plugin-api @ONLY)
  add_custom_target(docs-plugin-api
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.plugin-api
    DEPENDS ${PROJECT_SOURCE_DIR}/include/yantra/plugin.h
            ${PROJECT_SOURCE_DIR}/include/yantra/plugin.hpp
    COMMENT "Plugin API reference (Doxygen) in ${DOXYGEN_OUTPUT}/html"
    VERBATIM)
  # The test writes to its own folder, so it never races with the target.
  set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/plugin-api-check)
  configure_file(api/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.plugin-api-check @ONLY)
  if(YANTRA_BUILD_TESTS)
    add_test(NAME plugin_api_doxygen
             COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.plugin-api-check)
    set_tests_properties(plugin_api_doxygen PROPERTIES LABELS "fast;lint;docs" TIMEOUT 300)
  endif()
elseif(YANTRA_BUILD_TESTS)
  add_test(NAME plugin_api_doxygen
           COMMAND sh -c "echo 'SKIPPED: Doxygen was not found (DOC-17)'; exit 77")
  set_tests_properties(plugin_api_doxygen PROPERTIES
    LABELS "fast;lint;docs" SKIP_RETURN_CODE 77)
endif()

# Code quoted in docs/PLUGINS.md must be the real, compiled code, and the
# relative links of the Markdown files must resolve (DOC-17; offline: web
# links are not fetched). Scripts copied from yantra-radios fb194f5.
if(YANTRA_BUILD_TESTS)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME doc_snippets
             COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_snippets.py
                     ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/docs/PLUGINS.md)
    add_test(NAME doc_links
             COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_links.py
                     ${PROJECT_SOURCE_DIR})
    set_tests_properties(doc_snippets doc_links PROPERTIES LABELS "fast;lint;docs")
  else()
    foreach(_t doc_snippets doc_links)
      add_test(NAME ${_t} COMMAND sh -c "echo 'SKIPPED: Python 3 was not found'; exit 77")
      set_tests_properties(${_t} PROPERTIES LABELS "fast;lint;docs" SKIP_RETURN_CODE 77)
    endforeach()
  endif()
endif()
