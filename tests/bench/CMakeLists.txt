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

# RAD-114: the measurement engine against real time (timing: labelled
# perf, with a factor 2 margin; see perf_engine.cpp).
if(TARGET yantra::tetra_engine)
  add_executable(perf_engine perf_engine.cpp)
  target_link_libraries(perf_engine PRIVATE yantra::tetra_engine yantra::gen yantra_warnings)
  add_test(NAME perf_engine COMMAND perf_engine)
  set_tests_properties(perf_engine PROPERTIES LABELS "slow;perf;timing" TIMEOUT 600)
endif()

# Benchmarks with CPU-calibrated limits (RAD-127, docs/BENCHMARKS.md):
# channelizer, demodulator, decoder, level meter (engine); a regression
# beyond tests/bench/limits.csv fails the slow label.
if(TARGET yantra::tetra_engine)
  add_executable(bench_limits bench_limits.cpp)
  target_link_libraries(bench_limits PRIVATE yantra::tetra_engine yantra::gen yantra_warnings)
  add_test(NAME bench_limits COMMAND bench_limits ${CMAKE_CURRENT_SOURCE_DIR}/limits.csv)
  set_tests_properties(bench_limits PROPERTIES LABELS "slow;perf" TIMEOUT 900 RUN_SERIAL TRUE)
endif()
