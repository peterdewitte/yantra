# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# common/: code shared by the plugins and tools (RAD-02): the record files
# (RAD-65), the scene JSON (RAD-36) and, later, the SoapySDR layer of the SDR plugins (RAD-54).
# Static library yantra::radios_common, headers under include/yantra/radios/.
# nlohmann/json is private: the public headers do not expose it.
add_library(yantra_radios_common STATIC
  src/record_file.cpp
  src/scene.cpp)
target_include_directories(yantra_radios_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(yantra_radios_common
  PUBLIC yantra::radio_api
  PRIVATE yantra::json yantra_warnings)
add_library(yantra::radios_common ALIAS yantra_radios_common)
