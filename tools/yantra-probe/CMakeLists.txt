# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-probe: the headless runner (PLG-24, API-01, API-03, DEMO-10;
# docs/PROBE.md). Built with or without the desktop app (the scalar tree
# has no app); it finds the plugins in <bin>/../lib/yantra/plugins like
# the app.

include(GNUInstallDirs)
add_executable(yantra-probe main.cpp)
target_link_libraries(yantra-probe PRIVATE yantra::core yantra::json yantra::warnings Threads::Threads)
yantra_windows_resources(yantra-probe DESCRIPTION "Yantra probe (headless measurement, remote API)")
install(TARGETS yantra-probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
