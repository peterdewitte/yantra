# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The desktop application (OA-17, UI-13). Only built with YANTRA_BUILD_APP=ON;
# the GUI libraries (Dear ImGui, GLFW, OpenGL) are found here and nowhere
# else, so a build with YANTRA_BUILD_APP=OFF needs none of them.
# docs/APP.md describes the structure:
#
#   yantra_app_model      state, actions, settings, command line, window-system
#                         choice, data folders, icons: no GUI library, tested by
#                         tests/app (TST-50)
#   yantra_imgui          Dear ImGui core (third_party/imgui) with Yantra's item
#                         probe (app/gui/ui_probe.cpp, IMGUI_ENABLE_TEST_ENGINE
#                         hooks): no window system or OpenGL
#   yantra_app_gui        the ImGui layer (app/gui): needs an ImGui context only,
#                         so the headless UI tests draw it as the app does
#   yantra_imgui_backends Dear ImGui's GLFW and OpenGL 3 backends
#   yantra                the executable (app/src/main.cpp): window, GL, loop
# Copied from yantra-v2 68ea20a: app/CMakeLists.txt, adapted (headless GUI
# library and probe).

set(YANTRA_SOURCE_URL "https://github.com/peterdewitte/yantra" CACHE STRING
    "Source code link shown in the About dialog (NFR-01, NFR-09, UI-15)")

add_library(yantra_app_model STATIC
  model/src/app_icon.cpp
  model/src/app_state.cpp
  model/src/cli.cpp
  model/src/data_paths.cpp
  model/src/file_browser.cpp
  model/src/settings.cpp
  model/src/window_system.cpp)
add_library(yantra::app_model ALIAS yantra_app_model)
target_include_directories(yantra_app_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/model/include)
target_compile_definitions(yantra_app_model PRIVATE YANTRA_SOURCE_URL="${YANTRA_SOURCE_URL}")
target_link_libraries(yantra_app_model
  PUBLIC yantra::core
  PRIVATE yantra::json yantra::warnings)

add_executable(yantra src/main.cpp)
target_link_libraries(yantra PRIVATE yantra::core yantra::warnings)
install(TARGETS yantra RUNTIME DESTINATION bin)
