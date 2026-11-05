# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Indoor project file (IND-01, IND-03, IND-04, NFR-05; docs/formats/PROJECT.md).
# SQLite directly too: the tests write damaged and foreign files by hand.
# One ctest per doctest case (every case has its own scratch folder).
# Copied from yantra-v2 8c7a3e1: tests/project/CMakeLists.txt, adapted.

find_package(SQLite3 3.37 REQUIRED)
list(APPEND YANTRA_PER_CASE_TESTS project)
yantra_add_test_dir(project LIBS yantra::core yantra::json SQLite::SQLite3)
target_compile_definitions(test_project PRIVATE
  YANTRA_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
# The example building of the building tests (schema 3 round trip).
target_include_directories(test_project PRIVATE ${PROJECT_SOURCE_DIR}/tests/building)
