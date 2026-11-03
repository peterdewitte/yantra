# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The shared TETRA plugin glue (plugins/tetra_common).
if(NOT TARGET yantra::tetra_plugin)
  return()
endif()
yantra_add_test_dir(tetra_plugin LIBS yantra::tetra_plugin)
