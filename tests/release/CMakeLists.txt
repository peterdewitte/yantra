# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The release scripts of release.yml (RAD-134, RAD-137) run locally:
#   release_notes    CHANGELOG.md section extraction
#   release_package  plugin package with the licence bundle and the
#                    corresponding source (without osmo-tetra crypto/)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME release_notes
         COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/test_release_notes.py
                 ${PROJECT_SOURCE_DIR}/tools/ci)
add_test(NAME release_package
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_package.sh ${PROJECT_SOURCE_DIR}
                 ${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/package)
set_tests_properties(release_notes release_package PROPERTIES LABELS "fast;release" TIMEOUT 300)
