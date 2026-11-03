# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-radio-host (RAD-91): the radio test host. The host logic is the
# static library yantra::radio_host (host.hpp), so the conformance kit and
# the tests use the same code as the CLI (main.cpp).
add_library(yantra_radio_host_lib STATIC host.cpp dynlib.cpp)
target_include_directories(yantra_radio_host_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(yantra_radio_host_lib
  PUBLIC yantra::radio_api yantra::radios_common Threads::Threads ${CMAKE_DL_LIBS}
  PRIVATE yantra_warnings)
add_library(yantra::radio_host ALIAS yantra_radio_host_lib)

add_executable(yantra-radio-host main.cpp)
target_link_libraries(yantra-radio-host PRIVATE yantra::radio_host yantra_warnings)
