/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The stb functions the export images need (EXP-08), behind a few plain
 * C wrappers: stb_image_write's PNG/JPEG encoders and stb_truetype's
 * glyph rendering are compiled with static linkage in stb_export.c only,
 * so they never clash with the other copies of stb in the app and tests.
 */
#ifndef YANTRA_EXPORT_STB_EXPORT_H
#define YANTRA_EXPORT_STB_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void yx_write_fn(void* context, void* data, int size);

int yx_write_png(yx_write_fn* fn, void* context, int w, int h, int comp, const void* data, int stride);
int yx_write_jpg(yx_write_fn* fn, void* context, int w, int h, int comp, const void* data, int quality);

/* An opaque stbtt_fontinfo over `data` (which must outlive it); NULL when
 * the bytes are not a TrueType font. */
void* yx_font_new(const unsigned char* data);
void yx_font_free(void* font);
float yx_font_scale(const void* font, float pixels);
void yx_font_vmetrics(const void* font, int* ascent, int* descent, int* gap);
void yx_font_hmetrics(const void* font, int codepoint, int* advance, int* lsb);
int yx_font_kern(const void* font, int cp1, int cp2);
void yx_font_box(const void* font, int codepoint, float scale, float shift_x, float shift_y, int* x0, int* y0,
                 int* x1, int* y1);
void yx_font_render(const void* font, unsigned char* out, int w, int h, int stride, float scale, float shift_x,
                    float shift_y, int codepoint);

#ifdef __cplusplus
}
#endif

#endif
