# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# osmo-tetra wrapper tests (RAD-123; RAD-101, DEC-01..03, DEC-05, DEC-10,
# DEC-11): the lower MAC with bit errors, half-slot bursts, AACH and
# encryption flags; osmo-tetra's own test PDUs (src/testpdu.c) through the
# wrapper's parsers and a full encode/decode round trip; the carrier
# decoder on generator signals with noise, carrier and timing offsets,
# CRC failure counting, frame numbering; a bundled scenario through the
# channelizer; several carriers decoded concurrently (run under
# ThreadSanitizer in the tsan tree). Needs the osmo-tetra submodule.
if(NOT YANTRA_OSMO_TETRA_FOUND)
  message(STATUS "test_tetra_decoder: left out (needs the osmo-tetra submodule)")
  return()
endif()

# osmo-tetra's test PDUs (test-only; never crypto/).
yantra_osmo_tetra_sources(_osmo_testpdu testpdu.c)
add_library(yantra_test_osmo_testpdu STATIC ${_osmo_testpdu})
yantra_osmo_tetra_compile(${_osmo_testpdu})
target_link_libraries(yantra_test_osmo_testpdu PUBLIC yantra::osmo_tetra)
set_target_properties(yantra_test_osmo_testpdu PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

yantra_radio_add_test_dir(tetra_decoder LIBS yantra::tetra_decoder yantra::gen yantra_test_osmo_testpdu)
target_compile_definitions(test_tetra_decoder PRIVATE
  YANTRA_SCENARIO_DIR="${PROJECT_SOURCE_DIR}/data/scenarios")
