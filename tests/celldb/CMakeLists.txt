# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The cell database (DB-01..05, DB-08, TST-33; docs/CELL_DATABASE.md): the
# model, CSV import/export, verification and the difference log, auto-learn,
# the neighbour cross-check and the reselection simulator, with a technology
# made up for the test and with the TETRA plugin (its values_match, raster
# and reselection rule). One ctest per doctest case (every case has its own
# scratch folder).

list(APPEND YANTRA_PER_CASE_TESTS celldb)
yantra_add_test_dir(celldb LIBS yantra::core yantra::plugin_api)
add_dependencies(test_celldb yantra_tetra)
target_compile_definitions(test_celldb PRIVATE
  YANTRA_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch"
  TETRA_PLUGIN_PATH="$<TARGET_FILE:yantra_tetra>")
