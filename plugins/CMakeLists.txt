# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Yantra plugins (OA-01, PLG-01): the TETRA technology plugin, the example
# technology plugin (PLG-15) and the feature plugins (e.g. the probe,
# PLG-24), one sub-folder each with its own CMakeLists.txt, picked up
# automatically. Plugins build against the public header
# include/yantra/plugin.h only. Radio plugins are not here: they come from
# the yantra-radios submodule (PLG-26).
# Copied from yantra-v2 68ea20a (9540c71): plugins/CMakeLists.txt, adapted.

file(GLOB _yantra_plugin_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(_dir IN LISTS _yantra_plugin_dirs)
  if(IS_DIRECTORY ${_dir} AND EXISTS ${_dir}/CMakeLists.txt)
    add_subdirectory(${_dir})
  endif()
endforeach()
