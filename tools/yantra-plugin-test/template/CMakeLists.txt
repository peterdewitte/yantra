# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CMake template for a Yantra plugin built outside the Yantra repository
# (PLG-13; tutorial in docs/PLUGINS.md). Copy this file into an empty
# folder, put the plugin's C sources (*.c, and the *.h / *.inc they
# include) in src/ and its test inputs in tests/, then
#
#   cmake -S . -B build -DYANTRA_SDK_DIR=<Yantra sources or install prefix> \
#         -DYANTRA_PLUGIN_TEST=<path of yantra-plugin-test>
#   cmake --build build && ctest --test-dir build
#
# Tests (the plugin test kit, yantra-plugin-test check):
#   - conformance_<plugin>: descriptor, licence, schemas, UI, threading;
#   - conformance_<case> for every tests/<case>.records.jsonl (a record file
#     of the technology's radio, RAD-65): the records are fed to the plugin
#     and, when tests/<case>.golden.json exists, its output must equal it.
# `cmake --build build --target update-golden` rewrites the golden files
# (only on purpose, after checking the change). Reports and the actual
# output land in build/plugin-kit/.
#
# The plugin must be under a GPL-3.0-compatible licence and declare it as
# an SPDX expression in its descriptor (PLG-10).

cmake_minimum_required(VERSION 3.20)

get_filename_component(_folder ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(PLUGIN_NAME ${_folder} CACHE STRING "File name of the plugin library (without .so)")
string(MAKE_C_IDENTIFIER "${PLUGIN_NAME}" _project)
project(yantra_plugin_${_project} LANGUAGES C)

set(YANTRA_SDK_DIR "" CACHE PATH "Yantra source folder (plugin-api/include/yantra/plugin.h) or install prefix (include/yantra/plugin.h)")
set(YANTRA_PLUGIN_TEST "" CACHE FILEPATH "The yantra-plugin-test program")
if(EXISTS "${YANTRA_SDK_DIR}/plugin-api/include/yantra/plugin.h")
  set(_yantra_include ${YANTRA_SDK_DIR}/plugin-api/include)
elseif(EXISTS "${YANTRA_SDK_DIR}/include/yantra/plugin.h")
  set(_yantra_include ${YANTRA_SDK_DIR}/include)
else()
  message(FATAL_ERROR "Set YANTRA_SDK_DIR to the Yantra source folder or install prefix "
                      "(yantra/plugin.h not found in '${YANTRA_SDK_DIR}')")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
if(NOT PLUGIN_SOURCES)
  message(FATAL_ERROR "no C sources in ${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

# A plugin is a shared library that exports only yantra_plugin_entry.
add_library(${PLUGIN_NAME} MODULE ${PLUGIN_SOURCES})
target_include_directories(${PLUGIN_NAME} PRIVATE ${_yantra_include})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(${PLUGIN_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)

enable_testing()
if(NOT YANTRA_PLUGIN_TEST)
  message(WARNING "YANTRA_PLUGIN_TEST is not set: no plugin test kit tests")
  return()
endif()

set(_out ${CMAKE_CURRENT_BINARY_DIR}/plugin-kit)
file(MAKE_DIRECTORY ${_out})
add_test(NAME conformance_${PLUGIN_NAME}
  COMMAND ${YANTRA_PLUGIN_TEST} check --plugin $<TARGET_FILE:${PLUGIN_NAME}>
          --strict-translations --json ${_out}/${PLUGIN_NAME}.report.json)

file(GLOB _cases CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.records.jsonl)
set(_updates)
foreach(_records IN LISTS _cases)
  get_filename_component(_name ${_records} NAME)
  string(REGEX REPLACE "\\.records\\.jsonl$" "" _case ${_name})
  set(_golden ${CMAKE_CURRENT_SOURCE_DIR}/tests/${_case}.golden.json)
  set(_args check --plugin $<TARGET_FILE:${PLUGIN_NAME}> --records ${_records}
            --json ${_out}/${_case}.report.json --output ${_out}/${_case}.out.json)
  if(EXISTS ${_golden})
    add_test(NAME conformance_${_case} COMMAND ${YANTRA_PLUGIN_TEST} ${_args} --golden ${_golden})
  else()
    add_test(NAME conformance_${_case} COMMAND ${YANTRA_PLUGIN_TEST} ${_args})
  endif()
  list(APPEND _updates COMMAND ${YANTRA_PLUGIN_TEST} ${_args} --golden ${_golden} --update)
endforeach()
if(_updates)
  add_custom_target(update-golden ${_updates} DEPENDS ${PLUGIN_NAME}
    COMMENT "Rewriting the golden files in tests/")
endif()
