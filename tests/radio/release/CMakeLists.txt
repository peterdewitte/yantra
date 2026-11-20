# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio parts' release scripts (RAD-134, RAD-137) run locally:
#   release_notes    CHANGELOG.md section extraction
#   release_package  (radio-parts-only build) plugin package with the licence bundle and the
#                    corresponding source (without osmo-tetra crypto/,
#                    with the upstream archives and their SHA-256); in
#                    the Windows tree the .zip of .dll and .exe
#   licence_bundle   manifest.tsv, third_party/, LICENSES/ and
#                    THIRD_PARTY.md agree (tools/ci/licence_bundle.py)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME release_notes
         COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/test_release_notes.py
                 ${PROJECT_SOURCE_DIR}/tools/ci)
set(_platform linux-x86_64)
if(WIN32)
  set(_platform windows-x86_64)
endif()
# The release package of the radio parts: their own install layout,
# radio-parts-only build (RAD-141).
if(YANTRA_RADIO_PARTS_ONLY)
add_test(NAME release_package
         COMMAND ${YANTRA_BUILD_HOST_TEST} sh ${CMAKE_CURRENT_SOURCE_DIR}/test_package.sh ${PROJECT_SOURCE_DIR}
                 ${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/package ${_platform})
set_tests_properties(release_package PROPERTIES LABELS "fast;release" RESOURCE_LOCK install_tree TIMEOUT 600)
# Packaging runs on the build machine (the Windows .zip too).
yantra_build_host_tests(release_package)
endif()
add_test(NAME licence_bundle
         COMMAND Python3::Interpreter -B ${PROJECT_SOURCE_DIR}/tools/ci/licence_bundle.py check
                 ${PROJECT_SOURCE_DIR})
set_tests_properties(release_notes licence_bundle PROPERTIES LABELS "fast;release" TIMEOUT 300)
