# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HackRF plugin (RAD-60) on the shared SoapySDR layer with the built-in
# modules (RAD-136; it takes only the driver "hackrf"), and its test
# build hackrf_fake with the fake module registered as driver "hackrf"
# (RAD-92), which runs the conformance kit with unplug/replug.
if(NOT TARGET yantra::soapy_plugin)
  message(STATUS "hackrf plugin: left out (needs the measurement engine)")
  return()
endif()

yantra_soapy_module_table(_table)
yantra_add_radio_plugin(hackrf
  SOURCES hackrf_plugin.cpp
  LIBS yantra::soapy_plugin yantra::soapy_modules NO_TESTS)
target_compile_definitions(hackrf PRIVATE
  YANTRA_SOAPY_MODULE_TABLE="${_table}")
if(YANTRA_RADIOS_BUILD_TESTS)
  # Without a HackRF the kit skips the device checks.
  yantra_radio_plugin_tests(hackrf)
endif()

if(YANTRA_RADIO_TEST_PLUGINS)
  yantra_fake_soapy_library(yantra_fake_soapy_hackrf DRIVER hackrf)
  yantra_soapy_module_table(_fake_table
    ENTRIES "hackrf|SoapyHackRF (yantra fake module for tests)|0.3.6-fake|yantra-gen")
  yantra_add_radio_plugin(hackrf_fake
    SOURCES hackrf_plugin.cpp
    LIBS yantra::soapy_plugin yantra_fake_soapy_hackrf_whole
    OUTPUT_DIR ${YANTRA_RADIO_TEST_PLUGIN_DIR} NO_TESTS)
  target_compile_definitions(hackrf_fake PRIVATE
      YANTRA_SOAPY_MODULE_TABLE="${_fake_table}"
    YANTRA_HACKRF_PLUGIN_NAME="hackrf_fake")
endif()

include(GNUInstallDirs)
install(TARGETS hackrf
  LIBRARY DESTINATION ${YANTRA_RADIO_PLUGIN_INSTALL_DIR}
  RUNTIME DESTINATION ${YANTRA_RADIO_PLUGIN_INSTALL_DIR})
