/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hand-written config.h for the libosmocore 1.11.0 subset of RAD-136
 * (bits, bit vectors, convolutional decoder, utilities, panic). Its
 * autotools build is not on the tool lists, so tools/deps/deps.cmake
 * compiles the subset osmo-tetra needs directly. Only the portable
 * Viterbi kernels are built (no SSE/AVX/NEON variants, no run-time CPU
 * dispatch): the same results on every target.
 */
#ifndef YANTRA_OSMOCORE_CONFIG_H
#define YANTRA_OSMOCORE_CONFIG_H

#ifndef _WIN32
#define HAVE_ALLOCA_H 1
#endif
#define HAVE_CTYPE_H 1

#endif
