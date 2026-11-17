# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Benchmarks with CPU-calibrated limits (TST-41, docs/INTERPOLATION.md
# "Speed and SIMD"): bench_interpolation times the point interpolation
# (IDW, IND-41), one thread, SIMD and scalar; bench_pipeline the record
# pipeline (RF-01). Each runs after a CPU calibration line; a time beyond
# limits.csv fails (label slow). Time limits: label timing and RUN_SERIAL
# (an otherwise idle machine; left out of the sanitizer runs, -LE
# timing). bench_common.h holds the calibration and the limit check.
# Copied from yantra-v2 68ea20a: tests/raytrace_bench, adapted (CPU
# calibration and limits as yantra-radios tests/bench/bench_limits.cpp).
foreach(_bench bench_interpolation bench_pipeline)
  add_executable(${_bench} ${_bench}.cpp)
  target_link_libraries(${_bench} PRIVATE yantra::core yantra::plugin_api yantra_warnings)
  if(YANTRA_SANITIZER STREQUAL "")
    add_test(NAME ${_bench} COMMAND ${_bench} ${CMAKE_CURRENT_SOURCE_DIR}/limits.csv)
  else()
    # Instrumented trees: a smaller run, no limits (the checks still hold).
    add_test(NAME ${_bench} COMMAND ${_bench} ${CMAKE_CURRENT_SOURCE_DIR}/limits.csv --no-limits)
  endif()
  set_tests_properties(${_bench} PROPERTIES LABELS "slow;perf;timing" TIMEOUT 900 RUN_SERIAL TRUE)
endforeach()
