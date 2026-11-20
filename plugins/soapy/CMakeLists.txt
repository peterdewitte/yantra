# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generic SoapySDR plugin (RAD-62) with the built-in modules (RAD-136),
# and its test build soapysdr_fake with the fake module only (RAD-92),
# which runs the conformance kit with unplug/replug.
if(NOT TARGET yantra::soapy_plugin)
  message(STATUS "soapysdr plugin: left out (needs the measurement engine)")
  return()
endif()

yantra_soapy_module_table(_table)
yantra_add_radio_plugin(soapysdr
  SOURCES soapysdr_plugin.cpp
  LIBS yantra::soapy_plugin yantra::soapy_modules NO_TESTS)
target_compile_definitions(soapysdr PRIVATE
  YANTRA_SOAPY_MODULE_TABLE="${_table}")
if(YANTRA_RADIOS_BUILD_TESTS)
  # Without a device the conformance kit skips the device checks; the
  # completeness check runs on the installed artefact.
  yantra_radio_plugin_tests(soapysdr)
endif()

if(YANTRA_RADIO_TEST_PLUGINS)
  yantra_fake_soapy_library(yantra_fake_soapy DRIVER yantrafake)
  yantra_soapy_module_table(_fake_table ENTRIES "yantrafake|yantra fake module (test)|0.3.6-fake|yantra-gen")
  yantra_add_radio_plugin(soapysdr_fake
    SOURCES soapysdr_plugin.cpp
    LIBS yantra::soapy_plugin yantra_fake_soapy_whole
    OUTPUT_DIR ${YANTRA_RADIO_TEST_PLUGIN_DIR} NO_TESTS)
  target_compile_definitions(soapysdr_fake PRIVATE
      YANTRA_SOAPY_MODULE_TABLE="${_fake_table}"
    YANTRA_SOAPY_PLUGIN_NAME="soapysdr_fake"
    YANTRA_SOAPY_ONLY="yantrafake")
endif()

include(GNUInstallDirs)
install(TARGETS soapysdr
  LIBRARY DESTINATION ${YANTRA_RADIO_PLUGIN_INSTALL_DIR}
  RUNTIME DESTINATION ${YANTRA_RADIO_PLUGIN_INSTALL_DIR})
