// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kernel tables of the two paths (kernels.cpp, kernels_simd.cpp).
#pragma once

#include "yantra/receiver/kernels.hpp"

namespace yantra::receiver::detail {

extern const Kernels kScalarKernels;
#if YANTRA_RECEIVER_SIMD
extern const Kernels kSimdKernels;
#endif

}  // namespace yantra::receiver::detail
