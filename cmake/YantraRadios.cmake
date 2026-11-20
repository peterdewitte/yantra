# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio parts (PLG-29, RAD-141, OA-01): radio plugins and the radio
# API header yantra/radio.h come from the radios/ folder of this
# repository (yantra-radios, merged with its history). The application
# depends on the radio API, never the reverse (PLG-30).
#
# yantra::radio_api - interface target with the include folder of the
# radio API (radios/include/yantra/radio.h, radio.hpp). Nothing is
# compiled from radios/ here: the application's code has no DSP, decoder,
# SoapySDR or IQ code (RAD-26, HW-01); the radio plugins Yantra ships are
# built from radios/ in their own build (PLG-25, PLG-29).

# Earlier trees cached the submodule path third_party/yantra-radios, which
# no longer exists: such a value is replaced.
if(DEFINED CACHE{YANTRA_RADIOS_DIR} AND YANTRA_RADIOS_DIR MATCHES "/third_party/yantra-radios/?$")
  unset(YANTRA_RADIOS_DIR CACHE)
endif()
set(YANTRA_RADIOS_DIR ${PROJECT_SOURCE_DIR}/radios CACHE PATH
    "Source folder of the radio parts (radios/, PLG-29)")
if(NOT EXISTS ${YANTRA_RADIOS_DIR}/include/yantra/radio.h)
  message(FATAL_ERROR "The radio parts were not found in ${YANTRA_RADIOS_DIR} (PLG-29)")
endif()
if(NOT EXISTS ${YANTRA_RADIOS_DIR}/third_party/osmo-tetra/src)
  message(FATAL_ERROR "osmo-tetra is missing in ${YANTRA_RADIOS_DIR}/third_party/osmo-tetra: run "
                      "'git submodule update --init' (RAD-101)")
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
# SYSTEM: the header is built warning-free by the radios build.
target_include_directories(yantra_radio_api SYSTEM INTERFACE
  $<BUILD_INTERFACE:${YANTRA_RADIOS_DIR}/include>)
