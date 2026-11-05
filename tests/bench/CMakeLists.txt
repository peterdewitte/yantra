# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Benchmarks with CPU-calibrated limits (TST-41, docs/PROPAGATION.md
# "Speed"). bench_raytrace times the ray tracer (IND-40) on the office
# floor of tests/raytrace (60 x 30 m, 120 walls, 4 antennas, 0.25 m grid,
# 2 reflections), one thread, SIMD and scalar, after a CPU calibration
# line; a time beyond limits.csv fails (label slow). Time limits: label
# timing and RUN_SERIAL (an otherwise idle machine; left out of the
# sanitizer runs, -LE timing). The record pipeline and interpolation
# benchmarks of TST-41 join this folder with their steps.
# Copied from yantra-v2 68ea20a: tests/raytrace_bench, adapted (CPU
# calibration and limits as yantra-radios tests/bench/bench_limits.cpp).
add_executable(bench_raytrace bench_raytrace.cpp)
target_include_directories(bench_raytrace PRIVATE ${PROJECT_SOURCE_DIR}/tests/raytrace)
target_link_libraries(bench_raytrace PRIVATE yantra::core yantra_warnings)
if(YANTRA_SANITIZER STREQUAL "")
  add_test(NAME bench_raytrace COMMAND bench_raytrace ${CMAKE_CURRENT_SOURCE_DIR}/limits.csv)
else()
  # Instrumented trees: a 1 m grid, no limits (the runs must still agree).
  add_test(NAME bench_raytrace COMMAND bench_raytrace ${CMAKE_CURRENT_SOURCE_DIR}/limits.csv --no-limits)
endif()
set_tests_properties(bench_raytrace PROPERTIES LABELS "slow;perf;timing" TIMEOUT 900 RUN_SERIAL TRUE)
