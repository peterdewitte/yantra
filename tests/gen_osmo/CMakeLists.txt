# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# osmo-tetra cross-checks of the generator (RAD-93, RAD-101): the
# generator's coded blocks and PDUs are decoded with osmo-tetra's own lower
# MAC primitives and MAC PDU parsers, its bursts are compared with
# osmo-tetra's burst builders, demodulated samples go through osmo-tetra's
# burst synchroniser, and osmo-tetra's test vectors (src/testpdu.c) are
# compared with the generator's encoders. The
# generator never links osmo-tetra; only this test does, through a
# test-only static library of the coding primitives (never crypto/, see
# yantra_osmo_tetra_sources()). Needs the osmo-tetra submodule and
# libosmocore (pkg-config); without them the test is left out.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(YANTRA_OSMOCORE QUIET IMPORTED_TARGET libosmocore)
endif()
if(NOT YANTRA_OSMO_TETRA_FOUND OR NOT YANTRA_OSMOCORE_FOUND)
  message(STATUS "test_gen_osmo: left out (needs the osmo-tetra submodule and libosmocore)")
  return()
endif()

yantra_osmo_tetra_sources(_osmo_sources
  lower_mac/crc_simple.c
  lower_mac/tetra_conv_enc.c
  lower_mac/tetra_interleave.c
  lower_mac/tetra_rm3014.c
  lower_mac/tetra_scramb.c
  lower_mac/viterbi.c
  lower_mac/viterbi_cch.c
  phy/tetra_burst.c
  phy/tetra_burst_sync.c
  tetra_common.c
  tetra_mac_pdu.c
  tetra_tdma.c
  testpdu.c)
add_library(yantra_test_osmo_coding STATIC ${_osmo_sources})
target_include_directories(yantra_test_osmo_coding SYSTEM PUBLIC ${YANTRA_OSMO_TETRA_DIR}/src)
target_link_libraries(yantra_test_osmo_coding PUBLIC PkgConfig::YANTRA_OSMOCORE)
# Upstream code: no warnings from it; its printf output goes to the quiet
# sink of osmo_quiet.c (linked into the test).
set_source_files_properties(${_osmo_sources} PROPERTIES
  COMPILE_OPTIONS "-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/osmo_quiet.h")

yantra_add_test_dir(gen_osmo LIBS yantra::gen yantra_test_osmo_coding)
# The modulated-sample round trip demodulates with the test-only
# demodulator of tests/gen.
target_sources(test_gen_osmo PRIVATE ${CMAKE_SOURCE_DIR}/tests/gen/ref_demod.cpp)
target_include_directories(test_gen_osmo PRIVATE ${CMAKE_SOURCE_DIR}/tests/gen)
