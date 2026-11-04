# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Yantra top-level build (OA-01, OA-02). Layout: core/ (library without UI),
# app/ (desktop application), tools/ (command-line tools), plugins/,
# tests/ (ctest, TST-01) and the public plugin header include/yantra/.
# Radio plugins and their header come from the yantra-radios submodule in
# third_party/yantra-radios (PLG-26).
# Copied from yantra-v2 68ea20a (9540c71): CMakeLists.txt, adapted.

cmake_minimum_required(VERSION 3.20)

project(Yantra
  VERSION 0.1.0
  DESCRIPTION "Indoor TETRA coverage measurement on a software-defined radio"
  HOMEPAGE_URL "https://curtat.be"
  LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported; use e.g. "
                      "cmake -S . -B build -G Ninja")
endif()

option(YANTRA_SIMD "Build the SIMD kernels (OFF: plain C++ fallback only, TST-03)" ON)
option(YANTRA_BUILD_APP "Build the desktop application (needs the GUI libraries)" ON)
option(YANTRA_BUILD_TESTS "Build the test suite and register it with ctest" ON)
option(YANTRA_WERROR "Treat compiler warnings as errors (CI, TST-02)" OFF)
set(YANTRA_SANITIZER "" CACHE STRING
    "Developer sanitizer build (TST-05): empty, asan-ubsan or tsan")
set_property(CACHE YANTRA_SANITIZER PROPERTY STRINGS "" asan-ubsan tsan)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# All executables (app, tools, test_<module>) end up in <build>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(YantraWarnings)
include(YantraSanitizers)

# Public plugin API header (PLG-01; the full C ABI follows with PLG-06).
add_library(yantra_plugin_api INTERFACE)
add_library(yantra::plugin_api ALIAS yantra_plugin_api)
target_include_directories(yantra_plugin_api INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

if(YANTRA_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(core)
if(YANTRA_BUILD_APP)
  add_subdirectory(app)
endif()
add_subdirectory(tools)
add_subdirectory(plugins)

if(YANTRA_BUILD_TESTS)
  add_subdirectory(tests)
endif()

message(STATUS "Yantra ${PROJECT_VERSION}: SIMD=${YANTRA_SIMD} "
               "APP=${YANTRA_BUILD_APP} TESTS=${YANTRA_BUILD_TESTS} "
               "SANITIZER='${YANTRA_SANITIZER}'")
