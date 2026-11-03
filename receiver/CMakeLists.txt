# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# receiver/: the receiver library shared by the SDR plugins and the
# simulator (RAD-02, RAD-100..115). So far the DSP of RAD-100: kernels
# with a SIMD and a plain C++ path, FFT (pocketfft), window functions,
# spectrum, FIR design (Kaiser low-pass, RRC matched filter), NCO,
# polyphase resampler, channelizer (25 kHz channels from a wideband
# capture), the pi/4-DQPSK demodulator with frequency and symbol timing
# recovery, the TETRA parameter set, and the TETRA training sequences and
# bit-error-tolerant burst synchroniser that feed osmo-tetra (RAD-101,
# DEC-01). Static library yantra::receiver, headers under
# include/yantra/receiver/, position independent so it goes into plugin
# modules (RAD-132). docs/RECEIVER_DSP.md describes it; tests in
# tests/receiver and tests/receiver_tetra. The osmo-tetra lower layers are
# the static library yantra::osmo_tetra (cmake/OsmoTetra.cmake).
# Copied from yantra 68ea20a: core/dsp, plugins/tetra/rx and
# plugins/tetra/decoder burst_sync (namespace yantra::receiver).

add_library(yantra_receiver STATIC
  src/burst_sync.cpp
  src/channelizer.cpp
  src/fft.cpp
  src/fir.cpp
  src/kernels.cpp
  src/nco.cpp
  src/pi4dqpsk.cpp
  src/resampler.cpp
  src/spectrum.cpp
  src/window.cpp)
if(YANTRA_SIMD)
  # GCC/Clang vector extensions: SSE2 on x86-64, NEON on aarch64.
  target_sources(yantra_receiver PRIVATE src/kernels_simd.cpp)
  target_compile_definitions(yantra_receiver PRIVATE YANTRA_RECEIVER_SIMD=1)
else()
  target_compile_definitions(yantra_receiver PRIVATE YANTRA_RECEIVER_SIMD=0)
endif()
add_library(yantra::receiver ALIAS yantra_receiver)
target_include_directories(yantra_receiver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# pocketfft (third_party/pocketfft, BSD-3-Clause, RAD-04): header only,
# used in fft.cpp only; SYSTEM so its warnings stay out of the build.
target_include_directories(yantra_receiver SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/pocketfft)
target_compile_definitions(yantra_receiver PRIVATE POCKETFFT_NO_MULTITHREADING)
target_link_libraries(yantra_receiver PRIVATE yantra_warnings)
target_compile_features(yantra_receiver PUBLIC cxx_std_20)
set_target_properties(yantra_receiver PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
