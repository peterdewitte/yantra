/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * stb_image_write (third_party/stb) and stb_truetype (Dear ImGui's
 * imstb_truetype.h, third_party/imgui) for the export images (EXP-08):
 * static implementations behind the wrappers of stb_export.h. Only the
 * fonts shipped with Yantra are loaded (stb_truetype does no range
 * checking on hostile files).
 */
#include <stdlib.h>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include "stb_export.h"

int yx_write_png(yx_write_fn* fn, void* context, int w, int h, int comp, const void* data, int stride)
{
    return stbi_write_png_to_func(fn, context, w, h, comp, data, stride);
}

int yx_write_jpg(yx_write_fn* fn, void* context, int w, int h, int comp, const void* data, int quality)
{
    return stbi_write_jpg_to_func(fn, context, w, h, comp, data, quality);
}

void* yx_font_new(const unsigned char* data)
{
    stbtt_fontinfo* info = (stbtt_fontinfo*)calloc(1, sizeof(stbtt_fontinfo));
    if (info == NULL)
        return NULL;
    if (!stbtt_InitFont(info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        free(info);
        return NULL;
    }
    return info;
}

void yx_font_free(void* font)
{
    free(font);
}

float yx_font_scale(const void* font, float pixels)
{
    return stbtt_ScaleForPixelHeight((const stbtt_fontinfo*)font, pixels);
}

void yx_font_vmetrics(const void* font, int* ascent, int* descent, int* gap)
{
    stbtt_GetFontVMetrics((const stbtt_fontinfo*)font, ascent, descent, gap);
}

void yx_font_hmetrics(const void* font, int codepoint, int* advance, int* lsb)
{
    stbtt_GetCodepointHMetrics((const stbtt_fontinfo*)font, codepoint, advance, lsb);
}

int yx_font_kern(const void* font, int cp1, int cp2)
{
    return stbtt_GetCodepointKernAdvance((const stbtt_fontinfo*)font, cp1, cp2);
}

void yx_font_box(const void* font, int codepoint, float scale, float shift_x, float shift_y, int* x0, int* y0,
                 int* x1, int* y1)
{
    stbtt_GetCodepointBitmapBoxSubpixel((const stbtt_fontinfo*)font, codepoint, scale, scale, shift_x, shift_y, x0,
                                        y0, x1, y1);
}

void yx_font_render(const void* font, unsigned char* out, int w, int h, int stride, float scale, float shift_x,
                    float shift_y, int codepoint)
{
    stbtt_MakeCodepointBitmapSubpixel((const stbtt_fontinfo*)font, out, w, h, stride, scale, scale, shift_x, shift_y,
                                      codepoint);
}
