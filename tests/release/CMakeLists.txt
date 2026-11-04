# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The release scripts of release.yml (RAD-134, RAD-137) run locally:
#   release_notes    CHANGELOG.md section extraction
#   release_package  plugin package with the licence bundle and the
#                    corresponding source (without osmo-tetra crypto/,
#                    with the upstream archives and their SHA-256)
#   licence_bundle   manifest.tsv, third_party/, LICENSES/ and
#                    THIRD_PARTY.md agree (tools/ci/licence_bundle.py)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME release_notes
         COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/test_release_notes.py
                 ${PROJECT_SOURCE_DIR}/tools/ci)
add_test(NAME release_package
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_package.sh ${PROJECT_SOURCE_DIR}
                 ${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/package)
add_test(NAME licence_bundle
         COMMAND Python3::Interpreter -B ${PROJECT_SOURCE_DIR}/tools/ci/licence_bundle.py check
                 ${PROJECT_SOURCE_DIR})
set_tests_properties(release_notes release_package licence_bundle PROPERTIES LABELS "fast;release" TIMEOUT 300)
# The package installs the build tree; the install test does too.
set_tests_properties(release_package PROPERTIES RESOURCE_LOCK install_tree)
set_tests_properties(release_package PROPERTIES TIMEOUT 600)
