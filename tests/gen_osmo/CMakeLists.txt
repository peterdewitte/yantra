# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# osmo-tetra cross-checks of the generator (RAD-93, RAD-101): the
# generator's coded blocks and PDUs are decoded with osmo-tetra's own lower
# MAC primitives and MAC PDU parsers, its bursts are compared with
# osmo-tetra's burst builders, demodulated samples go through osmo-tetra's
# burst synchroniser, and osmo-tetra's test vectors (src/testpdu.c) are
# compared with the generator's encoders. The
# generator never links osmo-tetra; only this test does: the project's
# static yantra::osmo_tetra (cmake/OsmoTetra.cmake, on the static
# libosmocore subset of tools/deps, RAD-136) plus a test-only library of
# osmo-tetra's exact-match burst synchroniser and its test PDUs (never
# crypto/, see yantra_osmo_tetra_sources()). Needs the osmo-tetra
# submodule; without it the test is left out.
if(NOT YANTRA_OSMO_TETRA_FOUND)
  message(STATUS "test_gen_osmo: left out (needs the osmo-tetra submodule)")
  return()
endif()

yantra_osmo_tetra_sources(_osmo_test_sources
  phy/tetra_burst_sync.c
  testpdu.c)
add_library(yantra_test_osmo_extra STATIC ${_osmo_test_sources})
yantra_osmo_tetra_compile(${_osmo_test_sources})
target_link_libraries(yantra_test_osmo_extra PUBLIC yantra::osmo_tetra)
set_target_properties(yantra_test_osmo_extra PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

yantra_add_test_dir(gen_osmo LIBS yantra::gen yantra_test_osmo_extra yantra::json)
# The modulated-sample round trip demodulates with the test-only
# demodulator of tests/gen.
target_sources(test_gen_osmo PRIVATE ${CMAKE_SOURCE_DIR}/tests/gen/ref_demod.cpp)
target_include_directories(test_gen_osmo PRIVATE ${CMAKE_SOURCE_DIR}/tests/gen)
# The yantra-gen round trip (test_osmo_yantra_gen.cpp) runs the yantra-gen
# program and decodes its IQ file.
target_compile_definitions(test_gen_osmo PRIVATE YANTRA_GEN_EXE="$<TARGET_FILE:yantra-gen>")
add_dependencies(test_gen_osmo yantra-gen)
