# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The TETRA terminal plugin "pei" (RAD-80, TRM-01, TRM-04): plain C11,
# POSIX termios and threads, no other library (RAD-04). Its tests run it
# against the simulated terminal tools/yantra-pei-sim (tests/pei), so the
# kit never opens a real serial port: the plugin's own completeness and
# conformance ctests are added there (NO_TESTS here).
find_package(Threads REQUIRED)

yantra_add_radio_plugin(pei
  SOURCES pei_plugin.c pei_session.c pei_at.c pei_port.c
  LIBS Threads::Threads m
  NO_TESTS)
target_include_directories(pei PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pei PRIVATE
  YANTRA_RADIOS_SOURCE_URL="${YANTRA_RADIOS_SOURCE_URL}")

# The AT parser on its own, for the unit tests and the fuzz target (RAD-96).
add_library(yantra_pei_at STATIC pei_at.c)
target_include_directories(yantra_pei_at PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_pei_at PRIVATE yantra_warnings)
set_target_properties(yantra_pei_at PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
install(TARGETS pei
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/yantra-radios/plugins
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/yantra-radios/plugins)
