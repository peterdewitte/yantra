# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Decoder sensitivity sweep (RAD-126, docs/SENSITIVITY.md). test_sens
# (fast): the sweep library. sens_quick_<scenario> (label sens, the
# sens-quick workflow: ctest -L sens): the quick tier against
# baseline/quick.csv, one test per scenario, verdicts in build/sens/.
# sens_full (labels slow, release, opt_in): the full tier against
# baseline/full.csv, before a release.
if(NOT TARGET yantra-sens)
  return()
endif()
yantra_add_test_dir(sens LIBS yantra::sens yantra::gen)
target_include_directories(test_sens PRIVATE ${CMAKE_SOURCE_DIR}/tools/yantra-sens/src)

set(_sens_dir ${CMAKE_BINARY_DIR}/sens)
file(MAKE_DIRECTORY ${_sens_dir})
foreach(_s awgn cfo2 cfo20 cfo50 offcentre clock50 tu50 ht200 phasenoise combined)
  add_test(NAME sens_quick_${_s}
    COMMAND yantra-sens --tier quick --scenario ${_s} --threads 4
            --baseline ${CMAKE_CURRENT_SOURCE_DIR}/baseline/quick.csv --csv ${_sens_dir}/quick_${_s}.csv)
  set_tests_properties(sens_quick_${_s} PROPERTIES LABELS sens TIMEOUT 600)
endforeach()
# Opt-in (cmake/YantraTests.cmake): YANTRA_TESTS_OPT_IN=1 ctest -L opt_in.
yantra_add_opt_in_test(sens_full
  COMMAND yantra-sens --tier full --baseline ${CMAKE_CURRENT_SOURCE_DIR}/baseline/full.csv
          --csv ${_sens_dir}/full.csv
  LABELS slow release TIMEOUT 3600 RUN_SERIAL)
