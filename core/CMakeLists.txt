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
  src/build_info.cpp
  src/calibration/antenna.cpp
  src/calibration/corrector.cpp
  src/calibration/table.cpp
  src/calibration/wizard.cpp
  src/building/geometry.cpp
  src/building/model.cpp
  src/building/model_json.cpp
  src/context/building_context.cpp
  src/context/context.cpp
  src/measure/catalog.cpp
  src/measure/measurement_pipeline.cpp
  src/measure/pipeline.cpp
  src/measure/record_file.cpp
  src/measure/sample.cpp
  src/measure/technology_map.cpp
  src/platform/dynlib.cpp
  src/platform/file_posix.cpp
  src/plan/image_meta.cpp
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
  src/radio/scene_export.cpp
  src/util/sha256.cpp)
add_library(yantra::core ALIAS yantra_core)

# SQLite 3 (spec 16.2): the project file (IND-01, docs/formats/PROJECT.md),
# a system library. 3.37 or newer (SQLITE_OPEN_EXRESCODE; VACUUM INTO,
# upserts and SQLITE_OPEN_NOFOLLOW are older).
find_package(SQLite3 3.37 REQUIRED)

target_include_directories(yantra_core
  PUBLIC include
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(yantra_core
  PUBLIC yantra::plugin_api yantra::radio_api Threads::Threads ${CMAKE_DL_LIBS}
  PRIVATE yantra::warnings yantra::json SQLite::SQLite3)

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

# PDF plans (IND-02): the pinned PDFium release (cmake/YantraPdfium.cmake),
# or a stub that refuses PDF sources with a clear reason.
if(YANTRA_PDFIUM_FOUND)
  target_sources(yantra_core PRIVATE src/plan/pdf_source.cpp)
  target_compile_definitions(yantra_core PRIVATE YANTRA_PDFIUM_VERSION="${YANTRA_PDFIUM_VERSION}")
  target_link_libraries(yantra_core PUBLIC yantra::pdfium)
else()
  target_sources(yantra_core PRIVATE src/plan/pdf_none.cpp)
endif()
