# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared SoapySDR code of the SDR plugins (RAD-54). A plugin links
# yantra::soapy_plugin and one set of modules: yantra::soapy_modules
# (the real ones, RAD-136) or a fake module (tests, RAD-92) plus
# yantra::soapysdr.
if(NOT TARGET yantra::tetra_plugin)
  message(STATUS "soapy_common: left out (needs the measurement engine)")
  return()
endif()

add_library(yantra_soapy_plugin STATIC
  src/soapy_layer.cpp src/soapy_settings.cpp src/soapy_plugin.cpp)
add_library(yantra::soapy_plugin ALIAS yantra_soapy_plugin)
target_include_directories(yantra_soapy_plugin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# SoapySDR's headers only: the plugin links the library once, whole.
target_link_libraries(yantra_soapy_plugin
  PUBLIC yantra::tetra_plugin
  PRIVATE yantra_warnings $<COMPILE_ONLY:yantra_soapysdr_whole> $<COMPILE_ONLY:yantra_soapysdr>)
set_target_properties(yantra_soapy_plugin PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
