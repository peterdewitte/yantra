# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone project of the static dependency builds (RAD-136), configured
# by build_deps.sh: builds the bundled libraries of deps.cmake and installs
# them with their headers, licence texts and the dependency manifest into
# CMAKE_INSTALL_PREFIX. The main project uses the same deps.cmake.
cmake_minimum_required(VERSION 3.20)
project(YantraRadiosDeps LANGUAGES C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(YANTRA_DEPS_PREFIX "" CACHE PATH "unused here" FORCE)
include(${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake)

install(TARGETS yantra_talloc yantra_osmocore ${_sdr_c_libs} ${_sdr_cxx_libs} ARCHIVE DESTINATION lib)
install(FILES ${YANTRA_DEPS_ROOT}/third_party/talloc/talloc.h DESTINATION include)
install(DIRECTORY ${YANTRA_DEPS_ROOT}/third_party/libosmocore/include/osmocom DESTINATION include)
set(_tp ${YANTRA_DEPS_ROOT}/third_party)
install(FILES ${_tp}/libusb/libusb/libusb.h DESTINATION include/libusb-1.0)
install(DIRECTORY ${_tp}/soapysdr/include/SoapySDR DESTINATION include)
if(WIN32)
  # The static library's Config.h (sdr.cmake) replaces upstream's.
  install(FILES ${YANTRA_SOAPY_WIN_CONFIG} DESTINATION include/SoapySDR)
endif()
install(FILES ${_tp}/hackrf/host/libhackrf/src/hackrf.h DESTINATION include)
file(GLOB _rtl_headers ${_tp}/rtl-sdr/include/*.h)
install(FILES ${_rtl_headers} DESTINATION include)
install(FILES ${_tp}/airspy/libairspy/src/airspy.h ${_tp}/airspy/libairspy/src/airspy_commands.h
        DESTINATION include/libairspy)
file(GLOB _remote_headers ${_tp}/soapyremote/common/*.hpp ${_tp}/soapyremote/server/*.hpp)
install(FILES ${_remote_headers} ${CMAKE_BINARY_DIR}/yantra_deps/soapyremote/SoapySocketDefs.hpp
        DESTINATION include/SoapyRemote)
install(FILES ${YANTRA_DEPS_ROOT}/third_party/talloc/LICENSE DESTINATION share/licenses/talloc)
install(FILES ${_tp}/libusb/COPYING DESTINATION share/licenses/libusb)
install(FILES ${_tp}/soapysdr/LICENSE_1_0.txt DESTINATION share/licenses/SoapySDR)
install(FILES ${YANTRA_DEPS_ROOT}/LICENSES/BSD-3-Clause.txt DESTINATION share/licenses/libhackrf)
install(FILES ${_tp}/soapyhackrf/LICENSE DESTINATION share/licenses/SoapyHackRF)
install(FILES ${_tp}/rtl-sdr/COPYING DESTINATION share/licenses/librtlsdr)
install(FILES ${_tp}/soapyrtlsdr/LICENSE.txt DESTINATION share/licenses/SoapyRTLSDR)
install(FILES ${YANTRA_DEPS_ROOT}/LICENSES/BSD-3-Clause.txt DESTINATION share/licenses/libairspy)
install(FILES ${_tp}/soapyairspy/LICENSE.txt DESTINATION share/licenses/SoapyAirspy)
install(FILES ${_tp}/soapyremote/LICENSE_1_0.txt DESTINATION share/licenses/SoapyRemote)
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
