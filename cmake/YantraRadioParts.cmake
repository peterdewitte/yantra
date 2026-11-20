# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio parts in the one CMake project (PLG-29, RAD-141, RAD-01..03):
# radio-common/ (receiver, TETRA decoder, SoapySDR and TETRA plugin glue),
# gen/ (TETRA test-signal generator), the radio plugins in plugins/
# (hackrf, soapy, pei, simulator, example), the radio tools in tools/
# (yantra-radio-host, yantra-gen, yantra-hackrf, yantra-pei-sim,
# yantra-sens) and their tests in tests/radio. Included by the top-level
# CMakeLists.txt when YANTRA_BUILD_RADIOS is on; the application never
# compiles a source of these folders (RAD-26, HW-01, PLG-30).
#
# Before the radio folders, this file sets up what they share:
#   - the bundled libraries, built statically from third_party/ in the
#     build tree (tools/deps/deps.cmake, RAD-132, RAD-136), or taken from
#     a prefix made by tools/deps/build_deps.sh (-DYANTRA_DEPS_PREFIX=<p>,
#     the CI cache);
#   - osmo-tetra's lower layers (cmake/OsmoTetra.cmake, RAD-101), the Rust
#     protocol crate (cmake/RustProto.cmake), the radio plugin and SoapySDR
#     helpers (cmake/YantraRadioPlugin.cmake, cmake/YantraSoapy.cmake);
#   - yantra_radio_directory(): called at the top of every radio folder:
#     position-independent code (the plugins are modules), no run path
#     (RAD-133) and, on Windows,
#     the GCC runtime linked statically into every program and plugin, so
#     they import only Windows system DLLs and the UCRT (RAD-132).
#
# Results for the rest of the build:
# (YANTRA_RADIO_PLUGIN_DIR, YANTRA_RADIO_TEST_PLUGIN_DIR and
# YANTRA_RADIO_PLUGIN_SUFFIX are set by the top-level CMakeLists.txt.)
#   YANTRA_RADIO_PLUGIN_DIR       <build>/lib/yantra/plugins/radios: the
#                                 shipped radio plugins, where the
#                                 application finds them (PLG-09, PLG-25)
#   YANTRA_RADIO_TEST_PLUGIN_DIR  <build>/tests/radio-plugins: the example
#                                 beacon (RAD-64) and the fake-SDR builds
#                                 soapysdr_fake and hackrf_fake (RAD-92),
#                                 tests only, never installed
#   YANTRA_RADIO_PLUGIN_INSTALL_DIR  lib/yantra/plugins/radios, or in the
#                                 radio-parts-only build lib/yantra-radios/plugins
#   YANTRA_RADIO_PLUGINS          the shipped radio plugin targets
#   YANTRA_RADIOS_FAKE_SDR_SCENARIO, YANTRA_RADIOS_PEI_SIM,
#   YANTRA_RADIOS_PEI_SCENARIO    test fixtures for the application's tests
#   target yantra_radio_plugins   every radio plugin (shipped and test builds)

set(YANTRA_RADIOS_SOURCE_URL "https://github.com/peterdewitte/yantra-radio-plugins"
  CACHE STRING "Public source repository of the radio parts (RAD-12 descriptor, RAD-137)")

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/third_party/osmo-tetra/src)
  message(FATAL_ERROR "osmo-tetra is missing in third_party/osmo-tetra: run "
                      "'git submodule update --init' (RAD-101)")
endif()

include(GNUInstallDirs)
# Bundled C libraries built statically from third_party/ (RAD-136), then
# the osmo-tetra lower layers on them (RAD-101).
include(${PROJECT_SOURCE_DIR}/tools/deps/deps.cmake)
include(OsmoTetra)
include(RustProto)
include(YantraRadioPlugin)
include(YantraSoapy)

file(MAKE_DIRECTORY ${YANTRA_RADIO_PLUGIN_DIR} ${YANTRA_RADIO_TEST_PLUGIN_DIR})
if(YANTRA_RADIO_PARTS_ONLY)
  set(YANTRA_RADIO_PLUGIN_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/yantra-radios/plugins)
else()
  set(YANTRA_RADIO_PLUGIN_INSTALL_DIR lib/yantra/plugins/radios)
endif()
set(YANTRA_RADIO_PLUGINS hackrf simulator soapysdr)
if(YANTRA_RADIOS_PEI)
  list(APPEND YANTRA_RADIO_PLUGINS pei)
endif()

macro(yantra_radio_directory)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  # No run path: the radio plugins and programs are self-contained
  # (RAD-133); the application's $ORIGIN/../lib/yantra is for PDFium.
  set(CMAKE_INSTALL_RPATH "")
  # SIMD kernels or the plain C++ fallback; both give the same results.
  if(YANTRA_SIMD)
    add_compile_definitions(YANTRA_SIMD=1)
  else()
    add_compile_definitions(YANTRA_SIMD=0)
  endif()
  if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_link_options(-static)
  endif()
endmacro()

# Test fixtures the application's tests use (radio windows, NFR-04).
if(YANTRA_RADIO_TEST_PLUGINS)
  set(YANTRA_RADIOS_FAKE_SDR_SCENARIO ${PROJECT_SOURCE_DIR}/tests/radio/fixtures/soapy/fake_two_cells.json)
endif()
if(YANTRA_RADIOS_PEI)
  set(YANTRA_RADIOS_PEI_SIM ${PROJECT_BINARY_DIR}/bin/yantra-pei-sim${CMAKE_EXECUTABLE_SUFFIX})
  set(YANTRA_RADIOS_PEI_SCENARIO ${PROJECT_SOURCE_DIR}/tests/radio/fixtures/pei/terminal_cells.json)
endif()

# Every radio plugin, for the targets and tests that load them.
add_custom_target(yantra_radio_plugins ALL)
add_dependencies(yantra_radio_plugins ${YANTRA_RADIO_PLUGINS} beacon)
if(YANTRA_RADIO_TEST_PLUGINS)
  add_dependencies(yantra_radio_plugins soapysdr_fake hackrf_fake)
endif()

# The licence bundle of the radio plugins (RAD-137), made at build time by
# tools/ci/licence_bundle.py from tools/deps/manifest.tsv (checked against
# THIRD_PARTY.md and third_party/ first); installed to
# share/licenses/yantra-radios and packed into the radio parts' release.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(YANTRA_LICENCE_BUNDLE ${PROJECT_BINARY_DIR}/licence-bundle)
file(GLOB _bundle_inputs ${PROJECT_SOURCE_DIR}/LICENSES/*.txt)
add_custom_command(OUTPUT ${YANTRA_LICENCE_BUNDLE}/BUNDLE.md
  COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/licence_bundle.py bundle
          ${PROJECT_SOURCE_DIR} ${YANTRA_LICENCE_BUNDLE}
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/ci/licence_bundle.py ${PROJECT_SOURCE_DIR}/tools/deps/manifest.tsv
          ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md ${PROJECT_SOURCE_DIR}/LICENSE ${_bundle_inputs}
  COMMENT "Licence bundle (RAD-137) in ${YANTRA_LICENCE_BUNDLE}"
  VERBATIM)
add_custom_target(licence_bundle ALL DEPENDS ${YANTRA_LICENCE_BUNDLE}/BUNDLE.md)
unset(_bundle_inputs)

# Install (RAD-99), all relative to the prefix (never /etc; checked into a
# DESTDIR by install_destdir and, radio parts only, install_destdir_radios):
#   <plugins>                     the self-contained plugins (their CMakeLists)
#   bin/                          yantra-gen; radio parts only (and Windows,
#                                 next to yantra.exe) also yantra-radio-host
#                                 and yantra-hackrf (their CMakeLists)
#   share/yantra-radios/scenarios the scenarios with their scenes (RAD-74),
#                                 found by yantra-gen relative to itself
#   lib/udev/rules.d, lib/modprobe.d  udev rules and the RTL-SDR DVB-T
#                                 blacklist (udev/, Linux)
#   share/licenses/yantra-radios  the licence bundle (RAD-137)
#   share/doc/yantra-radios       radio parts only: their documentation
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/scenarios DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra-radios)
install(DIRECTORY ${YANTRA_LICENCE_BUNDLE}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/yantra-radios)
if(YANTRA_RADIO_PARTS_ONLY)
  set(_radio_docs CHANGELOG.md BUILD.md CONTRIBUTING.md CONTRIBUTORS.md)
  list(TRANSFORM _radio_docs PREPEND ${PROJECT_SOURCE_DIR}/)
  install(FILES ${PROJECT_SOURCE_DIR}/docs/RADIO_PARTS.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios
          RENAME README.md)
  install(FILES ${PROJECT_SOURCE_DIR}/docs/RADIO_ARCHITECTURE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios
          RENAME ARCHITECTURE.md)
  foreach(_d DEVICES REAL_WORLD_CHECKLIST RADIO_PLUGINS BENCHMARKS
             CHANNEL_MODEL FUZZING IQ-FORMATS MEASUREMENT RECEIVER_DSP RECORD_FILES SCENARIO_FORMAT
             SCENE_FORMAT SENSITIVITY TETRA_DECODER TETRA_RECORDS YANTRA_GEN YANTRA_HACKRF)
    list(APPEND _radio_docs ${PROJECT_SOURCE_DIR}/docs/${_d}.md)
  endforeach()
  install(FILES ${_radio_docs} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios)
  install(FILES ${PROJECT_SOURCE_DIR}/docs/plugins/hackrf.md ${PROJECT_SOURCE_DIR}/docs/plugins/pei.md
                ${PROJECT_SOURCE_DIR}/docs/plugins/simulator.md ${PROJECT_SOURCE_DIR}/docs/plugins/soapysdr.md
          DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios/plugins)
  # Where the pages' images look for them.
  install(FILES ${PROJECT_SOURCE_DIR}/docs/img/radio_list_light.png
                ${PROJECT_SOURCE_DIR}/docs/img/radio_hackrf_settings_light.png
                ${PROJECT_SOURCE_DIR}/docs/img/radio_simulator_settings_light.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios/img)
  unset(_radio_docs)

  # Release folder <build>/release/v<version> of the radio parts (RAD-98,
  # RAD-134, RAD-137): the plugin package with the licence bundle, the
  # corresponding source, SHA256SUMS and the release notes
  # (tools/ci/make_release.sh); the packaged artefacts are built first.
  set(_release_platform linux-x86_64)
  if(WIN32)
    set(_release_platform windows-x86_64)
  endif()
  add_custom_target(release
    COMMAND ${CMAKE_COMMAND} -E env bash ${PROJECT_SOURCE_DIR}/tools/ci/make_release.sh
            ${PROJECT_BINARY_DIR} ${PROJECT_BINARY_DIR}/release/v${YANTRA_VERSION} v${YANTRA_VERSION}
            ${_release_platform}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Release v${YANTRA_VERSION} in ${PROJECT_BINARY_DIR}/release/v${YANTRA_VERSION}"
    VERBATIM USES_TERMINAL)
  add_dependencies(release licence_bundle ${YANTRA_RADIO_PLUGINS} yantra-radio-host yantra-gen yantra-hackrf)
  cmake_language(DEFER CALL _yantra_release_manual)
  function(_yantra_release_manual)
    if(TARGET docs-radios-manual)
      add_dependencies(release docs-radios-manual)
    endif()
  endfunction()
  unset(_release_platform)
endif()
