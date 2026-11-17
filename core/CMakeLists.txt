# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra_core: the survey library without UI (OA-01, API-03).
# Technology- and radio-specific code never goes here; it lives in plugins
# (PLG-01), and all IQ processing in the radio plugins of yantra-radios
# (RAD-26, HW-01). Copied from yantra-v2 68ea20a (9540c71):
# core/CMakeLists.txt.

configure_file(src/build_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/yantra/core/build_config.h @ONLY)

add_library(yantra_core STATIC
  src/about/osm.cpp
  src/about/photo.cpp
  src/build_info.cpp
  src/calibration/antenna.cpp
  src/calibration/corrector.cpp
  src/calibration/table.cpp
  src/calibration/wizard.cpp
  src/celldb/csv.cpp
  src/celldb/database.cpp
  src/celldb/network.cpp
  src/celldb/reselection.cpp
  src/celldb/technology.cpp
  src/celldb/verify.cpp
  src/demo/demo_project.cpp
  src/control/command.cpp
  src/export/batch.cpp
  src/export/csv.cpp
  src/export/dxf.cpp
  src/export/image.cpp
  src/export/plugin_export.cpp
  src/export/raster.cpp
  src/export/stb_export.c
  src/export/table.cpp
  src/building/edit.cpp
  src/building/model.cpp
  src/building/model_json.cpp
  src/context/building_context.cpp
  src/context/context.cpp
  src/headless/runner.cpp
  src/measure/catalog.cpp
  src/measure/feature_feed.cpp
  src/measure/measurement_pipeline.cpp
  src/measure/pipeline.cpp
  src/measure/record_file.cpp
  src/measure/sample.cpp
  src/measure/self_interference.cpp
  src/measure/technology_map.cpp
  src/platform/console.cpp
  src/platform/dynlib.cpp
  src/platform/process.cpp
  src/platform/thread_name.cpp
  src/plan/image_meta.cpp
  src/plan/import.cpp
  src/plan/source.cpp
  src/plan/tiles.cpp
  src/plan/trim.cpp
  src/plugin/abi.cpp
  src/plugin/descriptor.cpp
  src/plugin/folders.cpp
  src/plugin/licence.cpp
  src/plugin/loader.cpp
  src/plugin/record.cpp
  src/plugin/schema.cpp
  src/plugin/state_store.cpp
  src/project/project.cpp
  src/project/schema.cpp
  src/project/sqlite.cpp
  src/radio/radio_host.cpp
  src/report/compare.cpp
  src/report/coverage.cpp
  src/report/document.cpp
  src/report/figure.cpp
  src/report/html.cpp
  src/report/internal.cpp
  src/report/pdf.cpp
  src/recording/codec.cpp
  src/recording/format.cpp
  src/recording/reader.cpp
  src/recording/replay.cpp
  src/recording/writer.cpp
  src/recording/wav.cpp
  src/recording/cut.cpp
  src/survey/acceptance.cpp
  src/survey/bins.cpp
  src/survey/interpolation.cpp
  src/survey/points.cpp
  src/survey/recordings.cpp
  src/survey/sample.cpp
  src/survey/stats.cpp
  src/survey/viewer.cpp
  src/survey/views.cpp
  src/survey/walk.cpp
  src/survey/walk_test.cpp
  src/survey/weak_areas.cpp
  src/util/job.cpp
  src/util/sha256.cpp)
add_library(yantra::core ALIAS yantra_core)
# Durable file operations (yantra/core/platform/file.h) per platform (HW-06).
if(WIN32)
  target_sources(yantra_core PRIVATE src/platform/file_win32.cpp)
  # platform::open_document: ShellExecuteW (shell32, a Windows system DLL).
  target_link_libraries(yantra_core PRIVATE shell32)
else()
  target_sources(yantra_core PRIVATE src/platform/file_posix.cpp)
endif()

# SQLite 3 (spec 16.2): the project file (IND-01, docs/formats/PROJECT.md),
# a system library. 3.37 or newer (SQLITE_OPEN_EXRESCODE; VACUUM INTO,
# upserts and SQLITE_OPEN_NOFOLLOW are older).
find_package(SQLite3 3.37 REQUIRED)
# zstd (spec 16.2): optional compression of recordings (REC-03,
# docs/formats/RECORDING.md), a system library found with pkg-config.
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd>=1.4)

target_include_directories(yantra_core
  PUBLIC include
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(yantra_core
  PUBLIC yantra::plugin_api yantra::radio_api yantra::json Threads::Threads ${CMAKE_DL_LIBS}
  PRIVATE yantra::warnings SQLite::SQLite3 PkgConfig::ZSTD)

# libharu (spec 16.2, zlib/libpng licence): the PDF reports (EXP-05,
# core/src/report/pdf.cpp), a system library (libhpdf-dev 2.3.0; MSYS2
# mingw-w64-ucrt-x86_64-libharu) with zlib and libpng; no pkg-config file.
# Its UTF-8 encoder and TrueType embedding (2.3.0 or newer) set the text
# in the shipped Inter and JetBrains Mono fonts.
find_path(YANTRA_HPDF_INCLUDE_DIR hpdf.h)
find_library(YANTRA_HPDF_LIBRARY NAMES hpdf libhpdf)
if(NOT YANTRA_HPDF_INCLUDE_DIR OR NOT YANTRA_HPDF_LIBRARY)
  message(FATAL_ERROR "libharu (hpdf.h, libhpdf) not found: install libhpdf-dev (PDF reports, EXP-05)")
endif()
target_include_directories(yantra_core SYSTEM PRIVATE ${YANTRA_HPDF_INCLUDE_DIR})
target_link_libraries(yantra_core PRIVATE ${YANTRA_HPDF_LIBRARY})

# Plan images (IND-02): stb_image, vendored in third_party/stb (MIT or
# public domain), compiled once in its own library without Yantra's
# warning flags. Under UBSan it needs -fno-sanitize=nonnull-attribute
# (memcpy with a null pointer and size 0 in its decoders).
# Copied from yantra-v2 68ea20a: core/CMakeLists.txt (plan part).
add_library(yantra_stb_image STATIC src/plan/stb_image_impl.c)
target_include_directories(yantra_stb_image SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/stb)
set_target_properties(yantra_stb_image PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  target_compile_options(yantra_stb_image PRIVATE -fno-sanitize=nonnull-attribute)
endif()
target_link_libraries(yantra_core PRIVATE yantra_stb_image)

# Export images (EXP-08): stb_image_write and stb_truetype (Dear ImGui's
# imstb_truetype.h) with static linkage in one C file. Third-party code:
# no Yantra warning flags.
set_property(SOURCE src/export/stb_export.c APPEND PROPERTY INCLUDE_DIRECTORIES
  ${PROJECT_SOURCE_DIR}/third_party/stb ${PROJECT_SOURCE_DIR}/third_party/imgui)
set_property(SOURCE src/export/stb_export.c APPEND PROPERTY COMPILE_OPTIONS -w)
if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  # stb_image_write: null memcpy with size 0, and its JPEG encoder shifts
  # ints past their range (third-party, not changed).
  set_property(SOURCE src/export/stb_export.c APPEND PROPERTY COMPILE_OPTIONS
    -fno-sanitize=nonnull-attribute,shift)
endif()

# PDF plans (IND-02): the pinned PDFium release (cmake/YantraPdfium.cmake),
# or a stub that refuses PDF sources with a clear reason.
if(YANTRA_PDFIUM_FOUND)
  target_sources(yantra_core PRIVATE src/plan/pdf_source.cpp)
  target_compile_definitions(yantra_core PRIVATE YANTRA_PDFIUM_VERSION="${YANTRA_PDFIUM_VERSION}")
  target_link_libraries(yantra_core PUBLIC yantra::pdfium)
else()
  target_sources(yantra_core PRIVATE src/plan/pdf_none.cpp)
endif()

# Point interpolation (IND-41): the SIMD distance kernel with YANTRA_SIMD
# (GCC/Clang vector extensions), else the plain C++ one; floating-point
# contraction is off in its files, so both paths and every thread count
# give the same map bit for bit (TST-53).
if(YANTRA_SIMD)
  target_sources(yantra_core PRIVATE src/survey/idw_kernels_simd.cpp)
  target_compile_definitions(yantra_core PRIVATE YANTRA_IDW_SIMD=1)
else()
  target_compile_definitions(yantra_core PRIVATE YANTRA_IDW_SIMD=0)
endif()
set_source_files_properties(src/survey/interpolation.cpp src/survey/idw_kernels_simd.cpp
  PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
