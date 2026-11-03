# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Hook for osmo-tetra (RAD-04, RAD-101): git submodule third_party/osmo-tetra
# pinned to 59fb603. Nothing is compiled from it yet; phase 4 adds the
# static library of the parts used (PHY, coding primitives, MAC PDU
# parsers). Its crypto/ directory is never built or used: the source list
# is filtered through yantra_osmo_tetra_sources(), which refuses crypto/.

set(YANTRA_OSMO_TETRA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/osmo-tetra
  CACHE PATH "osmo-tetra sources (git submodule pinned to 59fb603)")
set(YANTRA_OSMO_TETRA_PIN 59fb603)

if(EXISTS ${YANTRA_OSMO_TETRA_DIR}/src/tetra_common.h)
  set(YANTRA_OSMO_TETRA_FOUND TRUE)
else()
  set(YANTRA_OSMO_TETRA_FOUND FALSE)
  message(STATUS "osmo-tetra: submodule not checked out (git submodule update --init); "
                 "not needed until the TETRA receiver is built")
endif()

# yantra_osmo_tetra_sources(<out-var> <relative paths under src/>...)
# Returns absolute paths; fails on anything under crypto/.
function(yantra_osmo_tetra_sources out)
  set(_list "")
  foreach(_f ${ARGN})
    if(_f MATCHES "(^|/)crypto(/|$)")
      message(FATAL_ERROR "osmo-tetra crypto/ is never built or used (scope): ${_f}")
    endif()
    list(APPEND _list ${YANTRA_OSMO_TETRA_DIR}/src/${_f})
  endforeach()
  set(${out} ${_list} PARENT_SCOPE)
endfunction()
