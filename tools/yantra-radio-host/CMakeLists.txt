# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-radio-host (RAD-91): the radio test host. The host logic is the
# static library yantra::radio_host (host.hpp), so the conformance kit and
# the tests use the same code as the CLI (main.cpp).
add_library(yantra_radio_host_lib STATIC host.cpp dynlib.cpp conformance.cpp)
target_include_directories(yantra_radio_host_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(yantra_radio_host_lib
  PUBLIC yantra::radio_api yantra::radios_common Threads::Threads ${CMAKE_DL_LIBS}
  PRIVATE yantra::json yantra_warnings)
add_library(yantra::radio_host ALIAS yantra_radio_host_lib)

add_executable(yantra-radio-host main.cpp)
target_link_libraries(yantra-radio-host PRIVATE yantra::radio_host yantra_warnings)

# decode-iq (RAD-63): IQ files through the TETRA measurement engine; only
# when the receiver is built (osmo-tetra found).
if(TARGET yantra::tetra_engine)
  add_library(yantra_radio_host_iq STATIC iq_file.cpp decode_iq.cpp)
  target_include_directories(yantra_radio_host_iq PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(yantra_radio_host_iq
    PUBLIC yantra::tetra_engine yantra::radio_host
    PRIVATE yantra::json yantra_warnings)
  add_library(yantra::radio_host_iq ALIAS yantra_radio_host_iq)
  target_link_libraries(yantra-radio-host PRIVATE yantra::radio_host_iq)
  target_compile_definitions(yantra-radio-host PRIVATE YANTRA_HOST_DECODE_IQ)
endif()
