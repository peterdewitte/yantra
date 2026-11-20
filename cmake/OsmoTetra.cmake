# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# osmo-tetra (RAD-04, RAD-101): git submodule third_party/osmo-tetra pinned
# to 59fb603, compiled as the static library yantra::osmo_tetra from the
# lower-layer sources only, never a byte of it changed:
#   phy/tetra_burst.c             burst split into blocks (tetra_burst_rx_cb)
#                                 and the training sequences
#   lower_mac/tetra_scramb.c      scrambling sequence and cell scrambling code
#   lower_mac/tetra_interleave.c  block de-interleaving
#   lower_mac/tetra_conv_enc.c    RCPC encoder and de-puncturing
#   lower_mac/viterbi.c           Viterbi wrapper of the lower MAC
#   lower_mac/viterbi_cch.c       Viterbi decoder (libosmocore conv, soft bits)
#   lower_mac/tetra_rm3014.c      RM(30,14) code of the AACH
#   lower_mac/crc_simple.c        CRC-16-CCITT
#   tetra_common.c, tetra_tdma.c, tetra_mac_pdu.c
#                                 bit helpers, TDMA time, MAC PDU parsers
# on the static libosmocore subset and talloc of tools/deps (RAD-136).
# Never built: crypto/ (scope; yantra_osmo_tetra_sources() refuses it),
# lower_mac/tetra_lower_mac.c, tetra_upper_mac.c and the LLC/MLE/CMCE/MM
# files (they pull in crypto/, keep global state and handle addresses and
# SDS), the GNU Radio front end and phy/tetra_burst_sync.c (exact training
# sequence match; the receiver's burst synchroniser replaces it, only the
# osmo-tetra cross-check tests compile it). The user of the library
# defines osmo-tetra's TP-SAP entry tp_sap_udata_ind (the decoder wrapper
# of RAD-101; the tests their own). Upstream code is compiled without the
# project's warning set, with receiver/osmo/osmo_quiet.h forced in so its
# progress output goes to a quiet sink, position independent and hidden.

set(YANTRA_OSMO_TETRA_DIR ${PROJECT_SOURCE_DIR}/third_party/osmo-tetra
  CACHE PATH "osmo-tetra sources (git submodule pinned to 59fb603)")
set(YANTRA_OSMO_TETRA_PIN 59fb603)
set(YANTRA_OSMO_QUIET_DIR ${PROJECT_SOURCE_DIR}/radio-common/receiver/osmo)

if(EXISTS ${YANTRA_OSMO_TETRA_DIR}/src/tetra_common.h)
  set(YANTRA_OSMO_TETRA_FOUND TRUE)
else()
  set(YANTRA_OSMO_TETRA_FOUND FALSE)
  message(STATUS "osmo-tetra: submodule not checked out (git submodule update --init); "
                 "yantra::osmo_tetra and its tests are left out")
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

# yantra_osmo_tetra_compile(<sources>...): the compile settings of
# upstream osmo-tetra files (no warnings, forced quiet header). Source
# properties are per directory: call it where the target is defined.
function(yantra_osmo_tetra_compile)
  set_source_files_properties(${ARGN} PROPERTIES
    COMPILE_OPTIONS "-w;-include;${YANTRA_OSMO_QUIET_DIR}/osmo_quiet.h")
endfunction()

if(YANTRA_OSMO_TETRA_FOUND)
  yantra_osmo_tetra_sources(_osmo_tetra_sources
    lower_mac/crc_simple.c
    lower_mac/tetra_conv_enc.c
    lower_mac/tetra_interleave.c
    lower_mac/tetra_rm3014.c
    lower_mac/tetra_scramb.c
    lower_mac/viterbi.c
    lower_mac/viterbi_cch.c
    phy/tetra_burst.c
    tetra_common.c
    tetra_mac_pdu.c
    tetra_tdma.c)
  add_library(yantra_osmo_tetra STATIC ${_osmo_tetra_sources} ${YANTRA_OSMO_QUIET_DIR}/osmo_quiet.c)
  yantra_osmo_tetra_compile(${_osmo_tetra_sources})
  # SYSTEM: its headers raise no warnings in the project's code.
  target_include_directories(yantra_osmo_tetra SYSTEM PUBLIC ${YANTRA_OSMO_TETRA_DIR}/src)
  target_link_libraries(yantra_osmo_tetra PUBLIC yantra::osmocore)
  set_target_properties(yantra_osmo_tetra PROPERTIES
    C_STANDARD 11
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
  add_library(yantra::osmo_tetra ALIAS yantra_osmo_tetra)
  unset(_osmo_tetra_sources)
endif()
