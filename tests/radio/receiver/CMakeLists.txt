# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The receiver DSP building blocks (RAD-100): kernels scalar vs SIMD, FFT
# against a direct DFT, window functions and spectrum, FIR design, RRC,
# NCO, resampler and channelizer selectivity and aliasing. The generator's
# random numbers and AWGN (yantra::gen) are the noise source.
yantra_radio_add_test_dir(receiver LIBS yantra::receiver yantra::gen)
target_compile_definitions(test_receiver PRIVATE YANTRA_TEST_EXPECT_SIMD=$<BOOL:${YANTRA_SIMD}>)
