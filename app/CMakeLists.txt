# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The desktop application (OA-01, OA-17). Only built with
# YANTRA_BUILD_APP=ON; the GUI libraries (Dear ImGui, GLFW, OpenGL) are
# found here and nowhere else, so a build with YANTRA_BUILD_APP=OFF needs
# none of them. For now this is a console stub; the UI arrives in later
# phases. Copied from yantra-v2 68ea20a (9540c71): app/.

add_executable(yantra src/main.cpp)
target_link_libraries(yantra PRIVATE yantra::core yantra::warnings)
