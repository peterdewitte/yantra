# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fuzz targets (RAD-96): one LLVMFuzzerTestOneInput per parser that takes
# untrusted input: TETRA PDUs (and the Rust crate through its C ABI),
# soft-bit bursts through the lower MAC, reassembly and GSMTAP filter, IQ
# file content through the whole measurement engine, SigMF metadata,
# scenario, scene and record files. Normal builds (any compiler) link each
# with replay_main.cpp: ctest (labels fast, fuzz) replays the seed corpus
# that yantra-fuzz-seeds writes from the generator at test time, the
# committed crash regressions (regressions/<target>/) and seeded mutations
# of both; the sanitizer trees run the same tests under ASan/UBSan/TSan.
# With YANTRA_FUZZ=ON (preset "fuzz", clang only) they are libFuzzer
# binaries instead: tools/ci/fuzz.sh runs each for a bounded time. pei
# takes the PEI responses of a terminal into the terminal plugin's AT
# parser (RAD-80).
# See docs/FUZZING.md.
# Copied from yantra 68ea20a: tests/fuzz/CMakeLists.txt, adapted.
if(NOT TARGET yantra::radio_host_iq)
  message(STATUS "fuzz targets: left out (need the osmo-tetra submodule)")
  return()
endif()

set(_fuzz_corpus ${PROJECT_BINARY_DIR}/fuzz-corpus)
set(_fuzz_regress ${CMAKE_CURRENT_SOURCE_DIR}/regressions)

# target -> libraries
set(_libs_tetra_pdu yantra::tetra_decoder yantra::proto)
set(_libs_tetra_bursts yantra::tetra_engine)
set(_libs_iq yantra::radio_host_iq)
set(_libs_sigmf yantra::radio_host_iq)
set(_libs_scenario yantra::gen)
set(_libs_scene yantra::gen yantra::radios_common)
set(_libs_record_file yantra::radios_common)
set(_libs_pei yantra_pei_at)
set(YANTRA_FUZZ_TARGETS tetra_pdu tetra_bursts iq sigmf scenario scene record_file)
if(TARGET yantra_pei_at)
  list(APPEND YANTRA_FUZZ_TARGETS pei)
endif()

add_executable(yantra-fuzz-seeds seeds.cpp)
target_link_libraries(yantra-fuzz-seeds PRIVATE yantra_gen_app yantra::gen yantra_warnings)
target_include_directories(yantra-fuzz-seeds PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME fuzz_seeds COMMAND yantra-fuzz-seeds ${_fuzz_corpus} ${PROJECT_SOURCE_DIR})
set_tests_properties(fuzz_seeds PROPERTIES
  LABELS "fast;fuzz" TIMEOUT 300 FIXTURES_SETUP yantra_fuzz_corpus)

if(NOT YANTRA_FUZZ)
  add_library(yantra_fuzz_replay OBJECT replay_main.cpp)
  target_link_libraries(yantra_fuzz_replay PRIVATE yantra_warnings)
  target_compile_features(yantra_fuzz_replay PRIVATE cxx_std_20)
endif()

add_custom_target(fuzz-all)
add_dependencies(fuzz-all yantra-fuzz-seeds)
foreach(_t IN LISTS YANTRA_FUZZ_TARGETS)
  set(_exe fuzz_${_t})
  if(YANTRA_FUZZ)
    add_executable(${_exe} target_${_t}.cpp)
    target_compile_options(${_exe} PRIVATE -fsanitize=fuzzer)
    target_link_options(${_exe} PRIVATE -fsanitize=fuzzer)
    # libFuzzer: -runs=0 only executes the given inputs.
    set(_args -runs=0 ${_fuzz_corpus}/${_t})
  else()
    add_executable(${_exe} target_${_t}.cpp $<TARGET_OBJECTS:yantra_fuzz_replay>)
    set(_args --mutations 24 ${_fuzz_corpus}/${_t})
  endif()
  if(IS_DIRECTORY ${_fuzz_regress}/${_t})
    list(APPEND _args ${_fuzz_regress}/${_t})
  endif()
  target_link_libraries(${_exe} PRIVATE ${_libs_${_t}} yantra_warnings)
  target_include_directories(${_exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(${_exe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/fuzz)
  add_dependencies(fuzz-all ${_exe})
  add_test(NAME fuzz_${_t} COMMAND ${_exe} ${_args})
  set_tests_properties(fuzz_${_t} PROPERTIES
    LABELS "fast;fuzz" TIMEOUT 600 FIXTURES_REQUIRED yantra_fuzz_corpus
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
endforeach()

# The Rust crate's fuzz entry points on stable Rust (receiver/rust/tests/
# fuzz_smoke.rs) over the generated tetra_pdu corpus.
if(YANTRA_CARGO_EXECUTABLE)
  add_test(NAME fuzz_proto_rust
           COMMAND ${YANTRA_CARGO_EXECUTABLE} test --offline --locked --quiet
                   --manifest-path ${PROJECT_SOURCE_DIR}/receiver/rust/Cargo.toml
                   --target-dir ${PROJECT_BINARY_DIR}/cargo --test fuzz_smoke)
  set_tests_properties(fuzz_proto_rust PROPERTIES
    LABELS "fast;fuzz;rust" TIMEOUT 600 FIXTURES_REQUIRED yantra_fuzz_corpus RESOURCE_LOCK cargo_target
    ENVIRONMENT "YANTRA_FUZZ_CORPUS=${_fuzz_corpus}/tetra_pdu;YANTRA_FUZZ_TM_SDU_CORPUS=${_fuzz_corpus}/tm_sdu"
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
endif()
