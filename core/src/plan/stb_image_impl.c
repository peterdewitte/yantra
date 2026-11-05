/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The one translation unit that holds stb_image's implementation
 * (third_party/stb, IND-02). Images are decoded from memory only; the
 * error reason is thread-local (C11), so decoding is thread-safe.
 *
 * Copied from yantra-v2 68ea20a: core/src/plan/stb_image_impl.c.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"
