# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Benchmarks (tests/bench; RAD-127 groundwork). bench_receiver times the
# spectrum, channelizer per channel and demodulator per carrier on
# generated TETRA captures, scalar and SIMD (RAD-100). Informative for now:
# it prints the numbers (docs/RECEIVER_DSP.md quotes them) and fails only
# when the demodulated bits are wrong, never on time; the CPU calibration
# line and the limits of RAD-127 come with the decoder benchmarks.
# Copied from yantra 68ea20a: tests/dsp_bench, adapted for yantra::receiver.
add_executable(bench_receiver bench_receiver.cpp)
target_link_libraries(bench_receiver PRIVATE yantra::receiver yantra::gen yantra_warnings)
add_test(NAME bench_receiver COMMAND bench_receiver)
set_tests_properties(bench_receiver PROPERTIES LABELS "slow;perf" TIMEOUT 600)
