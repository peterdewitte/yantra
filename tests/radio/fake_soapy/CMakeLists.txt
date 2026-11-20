# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The fake SoapySDR module itself (RAD-92); its library is made by
# yantra_fake_soapy_library() (cmake/YantraSoapy.cmake) for the plugin
# test builds. This test links it with the static SoapySDR directly.
if(NOT TARGET yantra_fake_soapy)
  return()
endif()
add_executable(test_fake_soapy test_fake_soapy.cpp)
target_link_libraries(test_fake_soapy PRIVATE yantra_radio_test_main yantra_warnings
  yantra_fake_soapy_whole)
yantra_doctest_tests(test_fake_soapy GROUP test_fake_soapy FILTERS --test-suite-exclude=timing
  LABELS fast TIMEOUT 120 WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
yantra_doctest_tests(test_fake_soapy GROUP test_fake_soapy_timing FILTERS --test-suite=timing
  LABELS timing TIMEOUT 120 RUN_SERIAL WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
