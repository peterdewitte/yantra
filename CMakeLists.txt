# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-radios top-level build (RAD-01..03, Yantra OA-01/02): the radio
# API header, the receiver and generator libraries, the radio plugins, the
# tools and the test suite. C++20, CMake >= 3.20, Ninja; gcc or clang on
# Linux, gcc in MSYS2 UCRT64 on Windows (RAD-131).
cmake_minimum_required(VERSION 3.20)
project(YantraRadios VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(YANTRA_RADIOS_BUILD_TESTS "Build the test suite" ON)
option(YANTRA_SIMD "Use SIMD kernels (OFF = plain C++ fallback with the same results, RAD-90, TST-03)" ON)
option(YANTRA_FUZZ "libFuzzer fuzz targets (clang + asan-ubsan, preset fuzz, RAD-96)" OFF)
option(YANTRA_WERROR "Treat compiler warnings as errors (ON in CI, RAD-03, RAD-90)" OFF)
set(YANTRA_SANITIZER "" CACHE STRING
  "Developer sanitizer build: '' (none), 'asan-ubsan' or 'tsan' (RAD-90, Yantra TST-05)")
set_property(CACHE YANTRA_SANITIZER PROPERTY STRINGS "" asan-ubsan tsan)
set(YANTRA_RADIOS_SOURCE_URL "https://github.com/peterdewitte/yantra-radio-plugins"
  CACHE STRING "Public source repository (RAD-12 descriptor, RAD-137)")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(YantraWarnings)
include(YantraSanitizers)
include(YantraFuzz)
# Bundled C libraries built statically from third_party/ (RAD-136), then
# the osmo-tetra lower layers on them (RAD-101).
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/deps.cmake)
include(OsmoTetra)
include(RustProto)
include(YantraRadioPlugin)
include(YantraSoapy)

# SIMD kernels or the plain C++ fallback; both give the same results.
if(YANTRA_SIMD)
  add_compile_definitions(YANTRA_SIMD=1)
else()
  add_compile_definitions(YANTRA_SIMD=0)
endif()

# Plugins in bin/plugins so that the test host finds them next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(YANTRA_RADIO_PLUGIN_DIR ${CMAKE_BINARY_DIR}/bin/plugins)
file(MAKE_DIRECTORY ${YANTRA_RADIO_PLUGIN_DIR})

# The radio API (RAD-10): one public C header, nothing to link.
add_library(yantra_radio_api INTERFACE)
target_include_directories(yantra_radio_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
add_library(yantra::radio_api ALIAS yantra_radio_api)

# nlohmann/json (RAD-04), vendored header-only release (third_party/nlohmann_json).
add_library(yantra_json INTERFACE)
target_include_directories(yantra_json SYSTEM INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include>)
add_library(yantra::json ALIAS yantra_json)

if(YANTRA_RADIOS_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(common)
add_subdirectory(receiver)
add_subdirectory(gen)
add_subdirectory(plugins)
add_subdirectory(tools)
add_subdirectory(docs)
if(YANTRA_RADIOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()

add_subdirectory(udev)

# Install (RAD-99), all relative to the prefix (never /etc; tested into a
# DESTDIR by tests/install):
#   lib/yantra-radios/plugins  the self-contained plugins (RAD-132): the
#                              plugins' CMakeLists; the example beacon is
#                              not installed (Yantra would offer it)
#   bin/                       yantra-radio-host, yantra-gen, yantra-hackrf
#   share/yantra-radios/       the scenarios with their scenes (RAD-74)
#   lib/udev/rules.d, lib/modprobe.d  udev rules and the RTL-SDR DVB-T
#                              blacklist (udev/)
#   share/licenses/yantra-radios  the licence bundle (RAD-137): LICENSE,
#                              LICENSES/, THIRD_PARTY.md and the licence
#                              texts of every bundled library
#   share/doc/yantra-radios    README, CHANGELOG and docs/
include(GNUInstallDirs)
install(DIRECTORY data/scenarios DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra-radios)
set(_licences ${CMAKE_INSTALL_DATADIR}/licenses/yantra-radios)
install(FILES LICENSE THIRD_PARTY.md DESTINATION ${_licences})
install(DIRECTORY LICENSES DESTINATION ${_licences})
file(GLOB _third_party RELATIVE ${PROJECT_SOURCE_DIR}/third_party ${PROJECT_SOURCE_DIR}/third_party/*)
foreach(_comp ${_third_party})
  if(_comp STREQUAL "doctest" OR NOT IS_DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/${_comp})
    continue() # test only, never shipped
  endif()
  file(GLOB _texts ${PROJECT_SOURCE_DIR}/third_party/${_comp}/COPYING*
                   ${PROJECT_SOURCE_DIR}/third_party/${_comp}/LICEN[CS]E*
                   ${PROJECT_SOURCE_DIR}/third_party/${_comp}/NOTICE*)
  if(NOT _texts)
    # libhackrf and libairspy: BSD-3-Clause in their source headers.
    set(_texts ${PROJECT_SOURCE_DIR}/LICENSES/BSD-3-Clause.txt)
  endif()
  install(FILES ${_texts} DESTINATION ${_licences}/${_comp})
endforeach()
file(GLOB _docs ${PROJECT_SOURCE_DIR}/docs/*.md)
install(FILES README.md CHANGELOG.md ${_docs} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios)
