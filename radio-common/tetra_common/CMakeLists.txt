# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared glue of the TETRA radio plugins (simulator RAD-61, generic
# SoapySDR RAD-62, HackRF RAD-60): the descriptor schemas of the
# measurement engine, plan conversion (RAD-32), engine output to the host
# sink (RAD-20, RAD-24, RAD-25), C1 with a known calibration (RAD-22),
# settings-as-data helpers (RAD-40..46) and the GSMTAP receiver settings
# (RAD-115). Static library yantra::tetra_plugin, linked into each plugin.
if(NOT TARGET yantra::tetra_engine)
  message(STATUS "tetra_common: left out (needs the measurement engine)")
  return()
endif()
add_library(yantra_tetra_plugin STATIC src/tetra_plugin.cpp)
add_library(yantra::tetra_plugin ALIAS yantra_tetra_plugin)
target_include_directories(yantra_tetra_plugin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(yantra_tetra_plugin
  PUBLIC yantra::radio_api yantra::tetra_engine
  PRIVATE yantra_warnings)
set_target_properties(yantra_tetra_plugin PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
