# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests of the generator's channel model (RAD-72), indoor multi-wall model
# (RAD-73) and scenario files and engine (RAD-74), RAD-93: formulas against
# hand-calculated values, statistics of shadowing, fading and noise,
# receiver impairments, multi-wall levels on fixture scenes, scenario
# parsing and validation (data/scenarios and invalid variants), and seeded
# golden digests under golden/ (rewrite on purpose only: test_gen_sim
# --update or the build target update-golden-gen-sim).
# Windows has golden files of its own (golden/windows): the C runtime's
# math functions (MinGW-w64 libm: log10, pow, exp, sin) round some results
# differently in the last bit from glibc's, which changes digits of the
# truth levels and, rarely, a quantised sample. The UCRT's CPU-dependent
# FMA3 math paths are off in every test main and in yantra-gen
# (gen/include/yantra/gen/portable_math.hpp), so the files hold for every
# Windows machine. Same seeds, same scenarios; Windows files are written
# from a run in the Windows test VM (the failing check prints the computed
# text in LastTest.log; never from Linux).
set(_golden ${CMAKE_CURRENT_SOURCE_DIR}/golden)
if(WIN32)
  string(APPEND _golden /windows)
endif()
yantra_add_test_dir(gen_sim LIBS yantra::gen yantra::radios_common yantra::json)
target_compile_definitions(test_gen_sim PRIVATE
  YANTRA_GEN_SIM_GOLDEN_DIR="${_golden}"
  YANTRA_SCENARIO_DIR="${CMAKE_SOURCE_DIR}/data/scenarios")
add_custom_target(update-golden-gen-sim
  COMMAND $<TARGET_FILE:test_gen_sim> --update --test-suite=sim-golden
  DEPENDS test_gen_sim
  COMMENT "Rewriting the channel/scenario golden digests (tests/gen_sim/golden, golden/windows in the Windows tree)"
  VERBATIM)
