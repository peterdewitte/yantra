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
  src/plugin/abi.cpp
  src/plugin/descriptor.cpp
  src/plugin/folders.cpp
  src/plugin/licence.cpp
  src/plugin/loader.cpp
  src/plugin/record.cpp
  src/plugin/schema.cpp
  src/plugin/state_store.cpp
  src/radio/radio_host.cpp
  src/radio/scene_export.cpp
  src/util/sha256.cpp)
add_library(yantra::core ALIAS yantra_core)

target_include_directories(yantra_core
  PUBLIC include
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(yantra_core
  PUBLIC yantra::plugin_api yantra::radio_api Threads::Threads ${CMAKE_DL_LIBS}
  PRIVATE yantra::warnings yantra::json)
