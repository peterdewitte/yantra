# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# libFuzzer builds (RAD-96), YANTRA_FUZZ=ON (preset "fuzz"): clang only,
# together with the asan-ubsan sanitizers. All C and C++ code gets
# libFuzzer's coverage instrumentation; the fuzz targets in tests/fuzz link
# libFuzzer's main. The Rust protocol crate is instrumented the same way
# with stable rustc codegen options (YANTRA_FUZZ_RUSTFLAGS, used by
# cmake/RustProto.cmake), so the C++ targets that call its C ABI fuzz the
# Rust parsers with coverage feedback too. The default GCC build is not
# affected.
# Copied from yantra 68ea20a: cmake/YantraFuzz.cmake.

set(YANTRA_FUZZ_RUSTFLAGS "")
if(NOT YANTRA_FUZZ)
  return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "YANTRA_FUZZ needs clang (CC=clang CXX=clang++, or the fuzz preset)")
endif()
if(NOT YANTRA_SANITIZER STREQUAL "asan-ubsan")
  message(FATAL_ERROR "YANTRA_FUZZ needs YANTRA_SANITIZER=asan-ubsan")
endif()

add_compile_options(-fsanitize=fuzzer-no-link)
add_link_options(-fsanitize=fuzzer-no-link)
set(YANTRA_FUZZ_RUSTFLAGS
  "-Cpasses=sancov-module -Cllvm-args=-sanitizer-coverage-level=4 -Cllvm-args=-sanitizer-coverage-inline-8bit-counters -Cllvm-args=-sanitizer-coverage-pc-table -Cllvm-args=-sanitizer-coverage-trace-compares -Cdebug-assertions -Coverflow-checks")
