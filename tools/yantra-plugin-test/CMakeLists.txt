# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-plugin-test, the plugin test kit (PLG-13): loads a plugin through
# the real plugin loader, feeds it record files or a radio plugin's live
# records and checks it (kit.h). template/ holds the CMake template and
# the CI example for plugins built outside this repository; both are
# installed with the kit.

add_library(yantra_plugin_kit STATIC kit.cpp)
target_include_directories(yantra_plugin_kit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_plugin_kit
  PUBLIC yantra::core yantra::json
  PRIVATE yantra::warnings)

add_executable(yantra-plugin-test main.cpp)
target_link_libraries(yantra-plugin-test PRIVATE yantra_plugin_kit yantra::warnings)
yantra_windows_resources(yantra-plugin-test DESCRIPTION "Yantra plugin test kit")

include(GNUInstallDirs)
install(TARGETS yantra-plugin-test RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY template/ DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra/plugin-template)
# The plugin SDK: the public plugin headers, for the template's YANTRA_SDK_DIR.
install(FILES ${PROJECT_SOURCE_DIR}/include/yantra/plugin.h
              ${PROJECT_SOURCE_DIR}/include/yantra/plugin.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/yantra)
