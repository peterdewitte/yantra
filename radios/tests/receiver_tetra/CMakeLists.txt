# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The receiver DSP (RAD-100) on TETRA signals from the generator (RAD-70):
# demodulation back to the exact bits, decoded with the test-only
# reference decoder of tests/gen, BER against theory, frequency and timing
# recovery ranges, channelizer with real carriers, scalar vs SIMD, and the
# burst synchroniser (DEC-01).
yantra_add_test_dir(receiver_tetra LIBS yantra::receiver yantra::gen)
target_sources(test_receiver_tetra PRIVATE ${CMAKE_SOURCE_DIR}/tests/gen/ref_decoder.cpp)
target_include_directories(test_receiver_tetra PRIVATE ${CMAKE_SOURCE_DIR}/tests/gen)
