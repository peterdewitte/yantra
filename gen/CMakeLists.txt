# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# gen/: TETRA test-signal generator library (RAD-02, RAD-70), own C++20
# code independent of osmo-tetra (so decoding its output with osmo-tetra
# is a real cross-check): bit helpers, channel coding
# (EN 300 392-2 clause 8), broadcast PDUs, TDMA time, the downlink
# logical-channel schedule, continuous downlink bursts (clause 9), the
# pi/4-DQPSK modulator with RRC shaping (clause 5), the cell transmitter
# and multi-carrier span, SHA-256 for golden digests (RAD-93), the channel
# model (RAD-72), the indoor multi-wall model (RAD-73) and the scenario
# files and engine (RAD-74) and the ground truth as radio records
# (RAD-65, RAD-71).
# Static library yantra::gen, headers under include/yantra/gen/; position
# independent so it can go into plugin modules. Tests: tests/radio/gen/ and
# the osmo-tetra cross-check tests/radio/gen_osmo/.
yantra_radio_directory()
add_library(yantra_gen STATIC
  src/bits.cpp
  src/burst.cpp
  src/cell_tx.cpp
  src/channel.cpp
  src/coding.cpp
  src/digest.cpp
  src/downlink.cpp
  src/modulator.cpp
  src/multiwall.cpp
  src/pdu.cpp
  src/scenario.cpp
  src/scenario_engine.cpp
  src/timing.cpp
  src/truth_records.cpp)
target_include_directories(yantra_gen PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# The multi-wall model and the scenario engine work on the scene JSON of
# RAD-36 (yantra::radios_common).
target_link_libraries(yantra_gen
  PUBLIC yantra::radios_common
  PRIVATE yantra::json yantra_warnings)
target_compile_features(yantra_gen PUBLIC cxx_std_20)
# No fused multiply-add contraction: the samples (and the IQ golden digest
# of tests/gen) are the same on every target and in every build tree.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(yantra_gen PRIVATE -ffp-contract=off)
endif()
set_target_properties(yantra_gen PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(yantra::gen ALIAS yantra_gen)
