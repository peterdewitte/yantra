# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Static builds of the bundled C libraries (RAD-132, RAD-136) from the
# pinned, vendored sources under third_party/ (versions and checksums in
# tools/deps/manifest.tsv), with only the features used:
#   yantra::talloc    talloc 2.4.3 (LGPL-3.0-or-later): talloc.c only
#   yantra::osmocore  libosmocore 1.11.0 (GPL-2.0-or-later): the core subset
#                     osmo-tetra's lower layers need (bits, bit vectors,
#                     convolutional/Viterbi decoder with the portable
#                     kernels, utilities, panic) and talloc
# Neither upstream build system (waf, autotools) is on the tool lists, so
# the sources are compiled here with hand-written config.h files
# (tools/deps/talloc, tools/deps/libosmocore). Static, position
# independent, hidden visibility; upstream code is compiled without the
# project's warning set and never changed.
#
# Used in two ways:
#   - included by the top-level CMakeLists.txt: the libraries are built in
#     the build tree (an ignored folder), or, with -DYANTRA_DEPS_PREFIX=<p>,
#     taken from a prefix made by tools/deps/build_deps.sh (CI cache, Yantra
#     PLG-26 / RAD-138);
#   - by tools/deps/CMakeLists.txt, the standalone project build_deps.sh
#     configures, builds and installs into its prefix.
# Both run the same on Linux and in MSYS2 UCRT64 (Windows DEFERRED).

get_filename_component(YANTRA_DEPS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
set(YANTRA_DEPS_DIR ${YANTRA_DEPS_ROOT}/tools/deps)

# The manifest as a CMake list of "name|version|licence" (RAD-136): the
# bundled libraries, shown by the plugins' build info.
function(yantra_deps_manifest out)
  file(STRINGS ${YANTRA_DEPS_DIR}/manifest.tsv _lines REGEX "^[^#]")
  set(_list "")
  foreach(_line IN LISTS _lines)
    string(REPLACE "\t" ";" _fields "${_line}")
    list(GET _fields 0 _name)
    list(GET _fields 1 _version)
    list(GET _fields 2 _licence)
    list(APPEND _list "${_name}|${_version}|${_licence}")
  endforeach()
  set(${out} ${_list} PARENT_SCOPE)
endfunction()

set(YANTRA_DEPS_PREFIX "" CACHE PATH
  "Prefix made by tools/deps/build_deps.sh; empty: build the bundled libraries in the build tree")

if(YANTRA_DEPS_PREFIX)
  foreach(_lib talloc osmocore)
    set(_file ${YANTRA_DEPS_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}yantra_${_lib}${CMAKE_STATIC_LIBRARY_SUFFIX})
    if(NOT EXISTS ${_file})
      message(FATAL_ERROR "YANTRA_DEPS_PREFIX: ${_file} is missing (run tools/deps/build_deps.sh ${YANTRA_DEPS_PREFIX})")
    endif()
    add_library(yantra_${_lib} STATIC IMPORTED GLOBAL)
    set_target_properties(yantra_${_lib} PROPERTIES IMPORTED_LOCATION ${_file})
  endforeach()
  set_target_properties(yantra_talloc PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${YANTRA_DEPS_PREFIX}/include
    INTERFACE_COMPILE_DEFINITIONS "_PUBLIC_=")
  set_target_properties(yantra_osmocore PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${YANTRA_DEPS_PREFIX}/include
    INTERFACE_LINK_LIBRARIES yantra_talloc)
  message(STATUS "Bundled libraries from ${YANTRA_DEPS_PREFIX}")
else()
  # talloc: talloc.c with lib/replace/replace.h; nothing of lib/replace's
  # C sources is needed with the config.h results.
  set(_talloc ${YANTRA_DEPS_ROOT}/third_party/talloc)
  add_library(yantra_talloc STATIC ${_talloc}/talloc.c)
  target_include_directories(yantra_talloc SYSTEM PUBLIC $<BUILD_INTERFACE:${_talloc}>
    $<INSTALL_INTERFACE:include>)
  target_include_directories(yantra_talloc PRIVATE ${YANTRA_DEPS_DIR}/talloc ${_talloc}/lib/replace)
  # _PUBLIC_ empty: the symbols follow the hidden visibility preset (each
  # plugin keeps its own copy, RAD-133).
  target_compile_definitions(yantra_talloc PUBLIC "_PUBLIC_=" PRIVATE __STDC_WANT_LIB_EXT1__=1)

  # libosmocore: the subset osmo-tetra's lower layers use. backtrace.c is
  # replaced (tools/deps/libosmocore/backtrace.c): the upstream file needs
  # the logging framework, which is left out.
  set(_osmo ${YANTRA_DEPS_ROOT}/third_party/libosmocore)
  add_library(yantra_osmocore STATIC
    ${_osmo}/src/core/bits.c
    ${_osmo}/src/core/bitvec.c
    ${_osmo}/src/core/conv.c
    ${_osmo}/src/core/conv_acc.c
    ${_osmo}/src/core/conv_acc_generic.c
    ${_osmo}/src/core/panic.c
    ${_osmo}/src/core/utils.c
    ${YANTRA_DEPS_DIR}/libosmocore/backtrace.c)
  target_include_directories(yantra_osmocore SYSTEM PUBLIC $<BUILD_INTERFACE:${_osmo}/include>
    $<INSTALL_INTERFACE:include>)
  target_include_directories(yantra_osmocore PRIVATE ${YANTRA_DEPS_DIR}/libosmocore)
  target_link_libraries(yantra_osmocore PUBLIC yantra_talloc)

  foreach(_t yantra_talloc yantra_osmocore)
    target_compile_options(${_t} PRIVATE -w)
    set_target_properties(${_t} PROPERTIES
      C_STANDARD 11
      C_EXTENSIONS ON
      POSITION_INDEPENDENT_CODE ON
      C_VISIBILITY_PRESET hidden)
  endforeach()
  unset(_talloc)
  unset(_osmo)
endif()
add_library(yantra::talloc ALIAS yantra_talloc)
add_library(yantra::osmocore ALIAS yantra_osmocore)

# SoapySDR, its modules, the vendor libraries and libusb (phase 5).
include(${YANTRA_DEPS_DIR}/sdr.cmake)
