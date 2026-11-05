# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Benchmarks with CPU-calibrated limits (TST-41, docs/PROPAGATION.md
# "Speed"). bench_raytrace times the ray tracer (IND-40) on the office
# floor of tests/raytrace (60 x 30 m, 120 walls, 4 antennas, 0.25 m grid,
# 2 reflections), one thread, SIMD and scalar, after a CPU calibration
# line; a time beyond limits.csv fails (label slow). Time limits: label
# timing and RUN_SERIAL (an otherwise idle machine; left out of the
# sanitizer runs, -LE timing). bench_interpolation times the fit, the
# residual field and the source search (IND-41..43) on the same floor;
# bench_pipeline the record pipeline (RF-01). bench_common.h holds the
# calibration and the limit check of all three.
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

foreach(_bench bench_interpolation bench_pipeline)
  add_executable(${_bench} ${_bench}.cpp)
  target_include_directories(${_bench} PRIVATE ${PROJECT_SOURCE_DIR}/tests/raytrace)
  target_link_libraries(${_bench} PRIVATE yantra::core yantra::plugin_api yantra_warnings)
  if(YANTRA_SANITIZER STREQUAL "")
    add_test(NAME ${_bench} COMMAND ${_bench} ${CMAKE_CURRENT_SOURCE_DIR}/limits.csv)
  else()
    # Instrumented trees: a smaller run, no limits (the checks still hold).
    add_test(NAME ${_bench} COMMAND ${_bench} ${CMAKE_CURRENT_SOURCE_DIR}/limits.csv --no-limits)
  endif()
  set_tests_properties(${_bench} PROPERTIES LABELS "slow;perf;timing" TIMEOUT 900 RUN_SERIAL TRUE)
endforeach()
