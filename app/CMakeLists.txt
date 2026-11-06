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

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

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

set(_imgui ${PROJECT_SOURCE_DIR}/third_party/imgui)
add_library(yantra_ui_probe OBJECT gui/ui_probe.cpp)
target_include_directories(yantra_ui_probe SYSTEM PRIVATE ${_imgui})
target_compile_definitions(yantra_ui_probe PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_ENABLE_TEST_ENGINE)
target_link_libraries(yantra_ui_probe PRIVATE yantra::warnings)
add_library(yantra_imgui STATIC
  ${_imgui}/imgui.cpp ${_imgui}/imgui_draw.cpp ${_imgui}/imgui_tables.cpp ${_imgui}/imgui_widgets.cpp
  $<TARGET_OBJECTS:yantra_ui_probe>)
target_include_directories(yantra_imgui SYSTEM PUBLIC ${_imgui})
target_include_directories(yantra_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gui)
# The same definitions for every user: IMGUI_ENABLE_TEST_ENGINE changes
# imgui_internal.h enums (ui_probe.h).
target_compile_definitions(yantra_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_ENABLE_TEST_ENGINE)

add_library(yantra_app_gui STATIC gui/gui.cpp)
add_library(yantra::app_gui ALIAS yantra_app_gui)
target_include_directories(yantra_app_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gui)
target_link_libraries(yantra_app_gui PUBLIC yantra::app_model yantra_imgui PRIVATE yantra::warnings)

add_library(yantra_imgui_backends STATIC
  ${_imgui}/backends/imgui_impl_glfw.cpp ${_imgui}/backends/imgui_impl_opengl3.cpp)
target_include_directories(yantra_imgui_backends SYSTEM PUBLIC ${_imgui}/backends)
target_link_libraries(yantra_imgui_backends PUBLIC yantra_imgui glfw OpenGL::GL ${CMAKE_DL_LIBS})

add_executable(yantra src/main.cpp src/stb_image_write_impl.c)
target_include_directories(yantra SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
target_link_libraries(yantra PRIVATE yantra::app_gui yantra_imgui_backends yantra::core yantra::warnings)
if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  # stb_image_write passes NULL with size 0 to memcpy (as stb_image, core/).
  set_source_files_properties(src/stb_image_write_impl.c PROPERTIES COMPILE_OPTIONS -fno-sanitize=nonnull-attribute)
endif()

include(GNUInstallDirs)
install(TARGETS yantra RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
