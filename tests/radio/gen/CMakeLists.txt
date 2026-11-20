# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests of the generator library (RAD-70, RAD-93): channel coding against a
# separate test-only reference decoder (ref_decoder.*), known answers, PDU
# field layouts and round trips (ref_pdu.*), the downlink schedule and the
# golden vectors under golden/ (rewrite on purpose only: test_gen --update
# or the build target update-golden-gen). The osmo-tetra cross-checks are
# in tests/gen_osmo/.
yantra_radio_add_test_dir(gen LIBS yantra::gen)
target_compile_definitions(test_gen PRIVATE
  YANTRA_GEN_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_custom_target(update-golden-gen
  COMMAND $<TARGET_FILE:test_gen> --update --test-suite=golden
  DEPENDS test_gen
  COMMENT "Rewriting the generator golden vectors (tests/gen/golden)"
  VERBATIM)
