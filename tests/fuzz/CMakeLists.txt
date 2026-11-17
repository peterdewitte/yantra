# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fuzz targets (TST-12, TST-23): one LLVMFuzzerTestOneInput per parser of
# untrusted input: project files, recordings, record files through the
# record sink into the measurement pipeline, cell database CSV import,
# calibration tables, buildings with their scene export, plan sources
# (images and PDF), the export CSV/DXF readers, command lines of the
# command-line and remote control, acceptance profiles. Normal builds
# link each with replay_main.cpp: ctest (labels fast, fuzz) replays the
# seed corpus that yantra-fuzz-seeds writes at test time, the committed
# crash regressions (regressions/<target>/, when there are any) and
# seeded mutations of each (400; 48 for project files); the sanitizer trees (asan-ubsan, tsan) run the
# same tests. With YANTRA_FUZZ=ON (clang only) they are libFuzzer
# binaries instead (-runs=0 replays the corpus; run them longer by hand).
# Adapted from yantra-radios 5394318: tests/fuzz/CMakeLists.txt.

set(_fuzz_corpus ${PROJECT_BINARY_DIR}/fuzz-corpus)
set(_fuzz_regress ${CMAKE_CURRENT_SOURCE_DIR}/regressions)
set(YANTRA_FUZZ_TARGETS project recording record_file celldb_csv calibration building plan_source
    export_readers command acceptance)

add_executable(yantra-fuzz-seeds seeds.cpp)
target_link_libraries(yantra-fuzz-seeds PRIVATE yantra::core yantra::radio_api yantra::json yantra_plan_fixtures yantra_warnings)
target_include_directories(yantra-fuzz-seeds PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME fuzz_seeds COMMAND yantra-fuzz-seeds ${_fuzz_corpus} ${PROJECT_SOURCE_DIR})
set_tests_properties(fuzz_seeds PROPERTIES
  LABELS "fast;fuzz" TIMEOUT 300 FIXTURES_SETUP yantra_fuzz_corpus WORKING_DIRECTORY ${PROJECT_BINARY_DIR})

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
    set(_args -runs=0 ${_fuzz_corpus}/${_t})
  else()
    add_executable(${_exe} target_${_t}.cpp $<TARGET_OBJECTS:yantra_fuzz_replay>)
    # Cheap parsers get many mutations; a project opens an SQLite file
    # per input.
    set(_mutations 400)
    if(_t STREQUAL "project")
      set(_mutations 48)
    endif()
    set(_args --mutations ${_mutations} ${_fuzz_corpus}/${_t})
  endif()
  if(IS_DIRECTORY ${_fuzz_regress}/${_t})
    list(APPEND _args ${_fuzz_regress}/${_t})
  endif()
  target_link_libraries(${_exe} PRIVATE yantra::core yantra::radio_api yantra::json yantra_warnings)
  target_include_directories(${_exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(${_exe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/fuzz)
  add_dependencies(fuzz-all ${_exe})
  add_test(NAME fuzz_${_t} COMMAND ${_exe} ${_args})
  set_tests_properties(fuzz_${_t} PROPERTIES
    LABELS "fast;fuzz" TIMEOUT 600 FIXTURES_REQUIRED yantra_fuzz_corpus
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
endforeach()
