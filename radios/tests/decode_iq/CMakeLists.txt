# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# decode-iq of the radio test host (RAD-63): yantra-gen scenarios through
# the measurement engine, compared with yantra-gen's ground truth.
if(NOT TARGET yantra::radio_host_iq)
  message(STATUS "test_decode_iq: left out (needs the osmo-tetra submodule)")
  return()
endif()
yantra_add_test_dir(decode_iq LIBS yantra::radio_host_iq yantra_gen_app yantra::radios_common)
target_compile_definitions(test_decode_iq PRIVATE
  YANTRA_SCENARIO_DIR="${CMAKE_SOURCE_DIR}/data/scenarios")

# The command line: yantra-gen -> yantra-radio-host decode-iq -> read.
add_test(NAME host_cli_decode_iq_sigmf
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/decode_iq_sigmf.py
          $<TARGET_FILE:yantra-gen> $<TARGET_FILE:yantra-radio-host> ${CMAKE_BINARY_DIR}/host_cli_decode_iq_sigmf)
set_tests_properties(host_cli_decode_iq_sigmf PROPERTIES LABELS fast TIMEOUT 120
  PASS_REGULAR_EXPRESSION "synchronised 1, segments 1, clipped blocks 0, level dBm.*plugin: tetra-decode-iq.*tetra_cell: 3")
