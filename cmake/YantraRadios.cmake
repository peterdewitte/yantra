# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The yantra-radios submodule (PLG-26, OA-01): radio plugins and the radio
# API header yantra/radio.h come from third_party/yantra-radios, pinned to
# a tagged commit. Yantra depends on yantra-radios, never the reverse.
#
# yantra::radio_api - interface target with the include folder of the
# submodule's radio API (include/yantra/radio.h, radio.hpp). Nothing is
# compiled from the submodule here: Yantra's own code has no DSP, decoder,
# SoapySDR or IQ code (RAD-26, HW-01); the radio plugins Yantra ships are
# built from the submodule in their own build (PLG-25, PLG-26).

set(YANTRA_RADIOS_DIR ${PROJECT_SOURCE_DIR}/third_party/yantra-radios CACHE PATH
    "yantra-radios source tree (the git submodule, PLG-26)")
if(NOT EXISTS ${YANTRA_RADIOS_DIR}/include/yantra/radio.h)
  message(FATAL_ERROR "yantra-radios not found in ${YANTRA_RADIOS_DIR}: run "
                      "'git submodule update --init --recursive' (PLG-26)")
endif()

# Version of the pinned radio API, for the build summary and the tests.
file(STRINGS ${YANTRA_RADIOS_DIR}/include/yantra/radio.h _yr_major REGEX "^#define YR_API_VERSION_MAJOR ")
file(STRINGS ${YANTRA_RADIOS_DIR}/include/yantra/radio.h _yr_minor REGEX "^#define YR_API_VERSION_MINOR ")
string(REGEX MATCH "[0-9]+" YANTRA_RADIO_API_VERSION_MAJOR "${_yr_major}")
string(REGEX MATCH "[0-9]+" YANTRA_RADIO_API_VERSION_MINOR "${_yr_minor}")
unset(_yr_major)
unset(_yr_minor)

add_library(yantra_radio_api INTERFACE)
add_library(yantra::radio_api ALIAS yantra_radio_api)
# SYSTEM: the header is the submodule's code, built warning-free there.
target_include_directories(yantra_radio_api SYSTEM INTERFACE
  $<BUILD_INTERFACE:${YANTRA_RADIOS_DIR}/include>)
