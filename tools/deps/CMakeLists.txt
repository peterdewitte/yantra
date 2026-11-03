# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone project of the static dependency builds (RAD-136), configured
# by build_deps.sh: builds the bundled libraries of deps.cmake and installs
# them with their headers, licence texts and the dependency manifest into
# CMAKE_INSTALL_PREFIX. The main project uses the same deps.cmake.
cmake_minimum_required(VERSION 3.20)
project(YantraRadiosDeps LANGUAGES C)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(YANTRA_DEPS_PREFIX "" CACHE PATH "unused here" FORCE)
include(${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake)

install(TARGETS yantra_talloc yantra_osmocore ARCHIVE DESTINATION lib)
install(FILES ${YANTRA_DEPS_ROOT}/third_party/talloc/talloc.h DESTINATION include)
install(DIRECTORY ${YANTRA_DEPS_ROOT}/third_party/libosmocore/include/osmocom DESTINATION include)
install(FILES ${YANTRA_DEPS_ROOT}/third_party/talloc/LICENSE DESTINATION share/licenses/talloc)
install(FILES ${YANTRA_DEPS_ROOT}/third_party/libosmocore/COPYING DESTINATION share/licenses/libosmocore)

# deps-manifest.tsv: name, version, licence per bundled library.
yantra_deps_manifest(_manifest)
set(_text "")
foreach(_entry IN LISTS _manifest)
  string(REPLACE "|" "\t" _entry "${_entry}")
  string(APPEND _text "${_entry}\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/deps-manifest.tsv "${_text}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/deps-manifest.tsv DESTINATION .)
