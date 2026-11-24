# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Documentation targets and checks (DOC-17). docs-plugin-api builds the
# plugin API reference with Doxygen from plugin-api/include/yantra/plugin.h and
# plugin.hpp into <build>/docs/plugin-api/html; the plugin_api_doxygen test
# fails on any Doxygen warning, so every function, struct, field, macro
# and callback of the headers stays documented. Without Doxygen the test
# reports Skipped.
# Copied from yantra-radios fb194f5: docs/CMakeLists.txt, adapted for
# DOC-17.
set(YANTRA_PLUGIN_API_VERSION "")
file(STRINGS ${PROJECT_SOURCE_DIR}/plugin-api/include/yantra/plugin.h _api_lines
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
  # Doxygen never removes pages it no longer writes: the html folder is
  # made afresh (a stale page could still name the build machine's folders).
  add_custom_target(docs-plugin-api
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${DOXYGEN_OUTPUT}/html
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.plugin-api
    DEPENDS ${PROJECT_SOURCE_DIR}/plugin-api/include/yantra/plugin.h
            ${PROJECT_SOURCE_DIR}/plugin-api/include/yantra/plugin.hpp
    COMMENT "Plugin API reference (Doxygen) in ${DOXYGEN_OUTPUT}/html"
    VERBATIM)
  # The test writes to its own folder, so it never races with the target.
  set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/plugin-api-check)
  configure_file(api/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.plugin-api-check @ONLY)
  if(YANTRA_BUILD_TESTS)
    add_test(NAME plugin_api_doxygen
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.plugin-api-check)
    set_tests_properties(plugin_api_doxygen PROPERTIES LABELS "fast;lint;docs" TIMEOUT 300)
  endif()
elseif(YANTRA_BUILD_TESTS)
  add_test(NAME plugin_api_doxygen
           COMMAND ${YANTRA_BUILD_HOST_TEST} sh -c "echo 'SKIPPED: Doxygen was not found (DOC-17)'; exit 77")
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
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_snippets.py
                     ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/docs/PLUGINS.md)
    add_test(NAME doc_links
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_links.py
                     ${PROJECT_SOURCE_DIR})
    # SCR-07: the manual and README use images from docs/img only, all exist,
    # and every image there is used (the screenshot set, SCR-03).
    add_test(NAME doc_images
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_images.py
                     ${PROJECT_SOURCE_DIR})
    set_tests_properties(doc_snippets doc_links doc_images PROPERTIES LABELS "fast;lint;docs")
  else()
    foreach(_t doc_snippets doc_links doc_images)
      add_test(NAME ${_t} COMMAND ${YANTRA_BUILD_HOST_TEST} sh -c "echo 'SKIPPED: Python 3 was not found'; exit 77")
      set_tests_properties(${_t} PROPERTIES LABELS "fast;lint;docs" SKIP_RETURN_CODE 77)
    endforeach()
  endif()
endif()

# DOC-20, DOC-02, DOC-03, DOC-06, DOC-17: `ninja docs` builds the user
# manual (one page, HTML and PDF), the quick starts (EN, NL, FR, DE), the
# method and legal notes and every other Markdown page to HTML with pandoc,
# the PDFs with WeasyPrint (spec 16.2 build tools), adds the plugin API
# reference (Doxygen, docs-plugin-api) and checks every link and picture
# offline (tools/docs/build_docs.sh) into <build>/docs/site. The install
# rules ship that site when it was built (packaging/deb/build_deb.sh
# builds it first). Without pandoc or WeasyPrint the target fails; the
# ctest docs_build then reports Skipped (no pandoc) or builds HTML only.
set(YANTRA_DOCS_SITE ${CMAKE_CURRENT_BINARY_DIR}/site CACHE INTERNAL "Built documentation (ninja docs)")
set(_docs_api)
set(_docs_depends)
if(TARGET docs-plugin-api)
  set(_docs_api --plugin-api ${CMAKE_CURRENT_BINARY_DIR}/plugin-api/html)
  set(_docs_depends docs-plugin-api)
endif()
add_custom_target(docs
  COMMAND bash ${PROJECT_SOURCE_DIR}/tools/docs/build_docs.sh ${YANTRA_DOCS_SITE} --root ${PROJECT_SOURCE_DIR}
          --require-pdf ${_docs_api}
  COMMENT "Documentation (HTML and PDF) in ${YANTRA_DOCS_SITE}"
  VERBATIM)
if(_docs_depends)
  add_dependencies(docs ${_docs_depends})
endif()
if(YANTRA_BUILD_TESTS)
  # The same build into its own folder (never racing with the target),
  # PDFs included when WeasyPrint is there; offline.
  add_test(NAME docs_build
           COMMAND ${YANTRA_BUILD_HOST_TEST} bash ${PROJECT_SOURCE_DIR}/tools/docs/build_docs.sh ${CMAKE_CURRENT_BINARY_DIR}/site-check
                   --root ${PROJECT_SOURCE_DIR})
  set_tests_properties(docs_build PROPERTIES LABELS "docs" SKIP_RETURN_CODE 77 TIMEOUT 600
                       FIXTURES_SETUP yantra_docs_site)
endif()

# DOC-02a: `ninja docs-manual` builds the PDF user manual alone,
# <build>/docs/manual/yantra-manual-<version>.pdf (tools/docs/build_manual.py:
# pandoc and WeasyPrint, title page, contents, bookmarks, index, Inter
# embedded) with its DOC-20 checks; `ninja docs` builds the same PDF into
# the docs site (docs_build checks it). The packages ship it in
# share/doc/yantra (the install rule below; build_deb.sh, package_windows.py),
# where Help > User manual (PDF) finds it (UI-14). docs_manual_faults: the
# checker finds a chapter's faults. Skipped without pandoc or WeasyPrint.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(YANTRA_MANUAL_PDF ${CMAKE_CURRENT_BINARY_DIR}/manual/yantra-manual-${YANTRA_VERSION}.pdf
    CACHE INTERNAL "The PDF user manual (ninja docs-manual)")
add_custom_target(docs-manual
  COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/docs/build_manual.py
          --source ${PROJECT_SOURCE_DIR} --out ${CMAKE_CURRENT_BINARY_DIR}/manual --version ${YANTRA_VERSION}
  COMMENT "PDF user manual in ${YANTRA_MANUAL_PDF} (DOC-02a)"
  VERBATIM)
if(YANTRA_BUILD_TESTS)
  add_test(NAME docs_manual_faults
           COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/docs/check_manual_faults.py
                   ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/manual-faults)
  set_tests_properties(docs_manual_faults PROPERTIES LABELS "docs" SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()

# Installed documentation: the manual PDF, the method and legal notes next
# to the manual and quick starts (share/yantra, the manual links
# ../METHOD.md and ../LEGAL.md), the changelog, and the built site with the
# manual PDF (DOC-02) in share/doc/yantra/html when `ninja docs` ran. Not
# in the radio-parts-only build (RAD-141).
if(NOT YANTRA_RADIO_PARTS_ONLY)
  install(FILES ${YANTRA_MANUAL_PDF} DESTINATION share/doc/yantra OPTIONAL)
  install(FILES ${PROJECT_SOURCE_DIR}/docs/METHOD.md ${PROJECT_SOURCE_DIR}/docs/LEGAL.md
          DESTINATION share/yantra)
  install(FILES ${PROJECT_SOURCE_DIR}/CHANGELOG.md DESTINATION share/doc/yantra)
  install(DIRECTORY ${YANTRA_DOCS_SITE}/ DESTINATION share/doc/yantra/html OPTIONAL
          PATTERN "build.log" EXCLUDE PATTERN "external-links.txt" EXCLUDE)
endif()

# SCR-06: `ninja captures` makes the animated capture of the indoor
# workflow (docs/img/indoor_workflow.gif and .webm, under 10 s) from the
# app's screenshot-mode frames with ffmpeg (tools/screenshots/captures.py;
# Xvfb, ffmpeg). An intended change commits the new files.
if(TARGET yantra)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  add_custom_target(captures
    COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/screenshots/captures.py --build ${PROJECT_BINARY_DIR}
    DEPENDS yantra
    COMMENT "Animated capture of the indoor workflow (SCR-06) in docs/img"
    VERBATIM)
  if(TARGET demo_fraunces)
    add_dependencies(captures demo_fraunces)
  endif()
endif()

# The radio parts' documentation (RAD-97, RAD-135; PLG-29), when they are
# built:
#   doc_snippets_radio  the code quoted in docs/RADIO_PLUGINS.md is the
#                       compiled code of the beacon example radio plugin
#                       (plugins/example, RAD-64)
#   plugin_docs         the radio plugin pages docs/plugins/{hackrf,pei,
#                       simulator,soapysdr}.md (settings, presets,
#                       sensors, self-test items) and the record schema
#                       tables of docs/TETRA_RECORDS.md against what the
#                       built plugins report; `ninja update-plugin-docs`
#                       rewrites their generated blocks. Not in the
#                       sanitizer trees (the self-test stream items time
#                       out differently there).
#   docs-radios-manual  the radio plugins manual as one PDF,
#                       <build>/docs/radios-manual/yantra-radios-manual-<version>.pdf
#                       (tools/docs/build_radio_manual.py: pandoc and
#                       WeasyPrint); ctest radios_manual builds and checks
#                       it into its own folder, radios_manual_faults checks
#                       that the checker finds faults. Both report Skipped
#                       without pandoc or WeasyPrint.
# The radio API reference (docs-radio-api) is built in radio-api/.
if(YANTRA_BUILD_RADIOS)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(YANTRA_RADIOS_BUILD_TESTS)
    add_test(NAME doc_snippets_radio
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/check_doc_snippets.py
                     ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/docs/RADIO_PLUGINS.md)
    set_tests_properties(doc_snippets_radio PROPERTIES LABELS "fast;lint;docs")
  endif()
  if(NOT WIN32 AND YANTRA_RADIOS_BUILD_TESTS AND YANTRA_SANITIZER STREQUAL ""
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
    yantra_skip_test(plugin_docs REASON "the checker drives the Linux tree's .so plugins and programs; the pages are checked in the Linux trees, the plugins are the same code")
  endif()
  set(YANTRA_RADIOS_MANUAL ${CMAKE_CURRENT_BINARY_DIR}/radios-manual/yantra-radios-manual-${YANTRA_VERSION}.pdf
      CACHE INTERNAL "The radio plugins' PDF manual (ninja docs-radios-manual)")
  add_custom_target(docs-radios-manual
    COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/docs/build_radio_manual.py
            --source ${PROJECT_SOURCE_DIR} --out ${CMAKE_CURRENT_BINARY_DIR}/radios-manual --version ${YANTRA_VERSION}
    COMMENT "PDF manual of the radio plugins in ${YANTRA_RADIOS_MANUAL}"
    VERBATIM)
  if(YANTRA_RADIOS_BUILD_TESTS)
    add_test(NAME radios_manual
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/docs/build_radio_manual.py
                     --source ${PROJECT_SOURCE_DIR} --out ${CMAKE_CURRENT_BINARY_DIR}/radios-manual-check
                     --version ${YANTRA_VERSION})
    add_test(NAME radios_manual_faults
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B
                     ${PROJECT_SOURCE_DIR}/tests/radio/docs/check_manual_faults.py ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/radios-manual-faults)
    set_tests_properties(radios_manual radios_manual_faults PROPERTIES
      LABELS "docs" SKIP_RETURN_CODE 77 TIMEOUT 600)
  endif()
endif()

# Checks of the sources and the build machine's tools: build_host in a
# cross-compiled tree (top-level CMakeLists.txt).
foreach(_t plugin_api_doxygen doc_snippets doc_links doc_images docs_build docs_manual_faults
           doc_snippets_radio radios_manual radios_manual_faults)
  if(TEST ${_t})
    yantra_build_host_tests(${_t})
  endif()
endforeach()
