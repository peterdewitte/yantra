# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The TETRA terminal plugin "pei" (RAD-80, TRM-01, TRM-04): plain C11 and
# threads, serial ports on the native APIs behind one interface (RAD-04,
# pei_port.h): POSIX termios, or the Win32 communications API with the
# SetupAPI on Windows (system DLLs only, RAD-132). Its tests run it against
# the simulated terminal tools/yantra-pei-sim (tests/pei: a
# pseudo-terminal on Linux, a named pipe on Windows), so the kit never
# opens a real serial port: the plugin's own completeness and conformance
# ctests are added there (NO_TESTS here).
find_package(Threads REQUIRED)
if(WIN32)
  set(_pei_port_sources pei_port.c pei_port_win32.c)
else()
  set(_pei_port_sources pei_port.c pei_port_posix.c)
endif()

yantra_add_radio_plugin(pei
  SOURCES pei_plugin.c pei_session.c pei_at.c ${_pei_port_sources}
  LIBS Threads::Threads m $<$<BOOL:${WIN32}>:setupapi> $<$<BOOL:${WIN32}>:cfgmgr32>
       $<$<BOOL:${WIN32}>:advapi32>
  NO_TESTS)
target_include_directories(pei PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# The AT parser on its own, for the unit tests and the fuzz target (RAD-96).
add_library(yantra_pei_at STATIC pei_at.c)
target_include_directories(yantra_pei_at PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_pei_at PRIVATE yantra_warnings)
set_target_properties(yantra_pei_at PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The serial-port layer on its own, for its unit tests on both platforms.
add_library(yantra_pei_port STATIC ${_pei_port_sources})
target_include_directories(yantra_pei_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_pei_port PRIVATE yantra_warnings)
if(WIN32)
  target_link_libraries(yantra_pei_port PUBLIC setupapi cfgmgr32 advapi32)
endif()
set_target_properties(yantra_pei_port PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
install(TARGETS pei
  LIBRARY DESTINATION ${YANTRA_RADIO_PLUGIN_INSTALL_DIR}
  RUNTIME DESTINATION ${YANTRA_RADIO_PLUGIN_INSTALL_DIR})
