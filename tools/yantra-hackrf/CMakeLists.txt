# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-hackrf (RAD-66): the HackRF One test tool. hackrf_app is a library
# so tests/yantra_hackrf runs every command in-process against the fake
# module (RAD-92); the tool links the built-in SoapySDR modules for capture
# (RAD-63) and loads the HackRF plugin for everything else. Usage:
# docs/YANTRA_HACKRF.md.
if(NOT TARGET yantra::capture_iq OR NOT TARGET hackrf)
  message(STATUS "yantra-hackrf: left out (needs the HackRF plugin and capture-iq)")
  return()
endif()
include(GNUInstallDirs)

add_library(yantra_hackrf_app STATIC hackrf_app.cpp)
target_include_directories(yantra_hackrf_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_hackrf_app
  PUBLIC yantra::capture_iq yantra::radio_host
  PRIVATE yantra::json yantra_warnings)
target_compile_definitions(yantra_hackrf_app PRIVATE
  YANTRA_HACKRF_VERSION="${PROJECT_VERSION}"
  YANTRA_HACKRF_PLUGIN_FILE="$<TARGET_FILE_NAME:hackrf>"
  YANTRA_HACKRF_LIBDIR="${CMAKE_INSTALL_LIBDIR}")

add_executable(yantra-hackrf main.cpp)
target_link_libraries(yantra-hackrf PRIVATE yantra_hackrf_app yantra::soapy_modules yantra_warnings)
install(TARGETS yantra-hackrf RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
