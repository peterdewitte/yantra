# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Plan sources, tiles, trims and the plan import model (IND-02, IND-05..07,
# IND-10). The fixtures (images in every stb_image format, minimal PDFs)
# are written at test time from code; no third-party or customer plans.
# One ctest per doctest case.
# Copied from yantra-v2 68ea20a: tests/plan/CMakeLists.txt, adapted
# (per-case ctests, yantra_add_test_dir).

add_library(yantra_plan_fixtures STATIC
  fixtures/plan_fixtures.cpp
  fixtures/stb_image_write_impl.c)
target_include_directories(yantra_plan_fixtures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
target_include_directories(yantra_plan_fixtures SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
target_link_libraries(yantra_plan_fixtures PUBLIC yantra::core PRIVATE yantra_warnings)
# Third-party code: no Yantra warning flags.
set_source_files_properties(fixtures/stb_image_write_impl.c PROPERTIES COMPILE_OPTIONS "-w")
if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  # stb_image_write: null memcpy with size 0, and its JPEG encoder shifts
  # ints past their range (third-party, test-only, not changed).
  set_property(SOURCE fixtures/stb_image_write_impl.c APPEND PROPERTY COMPILE_OPTIONS
    -fno-sanitize=nonnull-attribute,shift)
endif()

find_package(SQLite3 3.37 REQUIRED)
list(APPEND YANTRA_PER_CASE_TESTS plan)
yantra_add_test_dir(plan LIBS yantra::core yantra_plan_fixtures SQLite::SQLite3)
target_compile_definitions(test_plan PRIVATE
  YANTRA_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch"
  YANTRA_TEST_PDFIUM=$<BOOL:${YANTRA_PDFIUM_FOUND}>)
