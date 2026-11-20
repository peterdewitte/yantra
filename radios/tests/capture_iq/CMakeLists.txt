# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# capture-iq of the test host (RAD-63) against the fake SoapySDR module
# (RAD-92): formats, SigMF, rolling buffer, overflow segments, decode-iq
# of the capture.
if(NOT TARGET yantra::capture_iq OR NOT TARGET yantra_fake_soapy)
  message(STATUS "test_capture_iq: left out (no capture-iq)")
  return()
endif()
add_executable(test_capture_iq test_capture_iq.cpp)
target_link_libraries(test_capture_iq PRIVATE yantra_test_main yantra_warnings
  yantra::capture_iq yantra_fake_soapy_whole)
yantra_doctest_tests(test_capture_iq GROUP test_capture_iq LABELS fast TIMEOUT 300
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
