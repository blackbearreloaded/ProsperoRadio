// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only exporter for the completed LVGL app's compiled multilingual fonts.

#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const lv_font_t radio_font_20;
extern const lv_font_t radio_font_24;
extern const lv_font_t radio_font_28;
extern const lv_font_t radio_font_32;

static int write_u32(FILE * file, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)
    };
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static int write_i32(FILE * file, int32_t value)
{
    return write_u32(file, (uint32_t)value);
}

static int glyph_is_owned(const lv_font_t * font, uint32_t codepoint,
                          lv_font_glyph_dsc_t * descriptor)
{
    if(!lv_font_get_glyph_dsc(font, descriptor, codepoint, 0)) return 0;
    return descriptor->resolved_font == font && !descriptor->is_placeholder;
}

static int export_font(const char * output_directory, unsigned size,
                       const lv_font_t * font)
{
    char path[1024];
    if(snprintf(path, sizeof(path), "%s/radio-font-%u.rbf", output_directory,
                size) >= (int)sizeof(path)) return 0;

    uint32_t count = 0;
    for(uint32_t codepoint = 0x80; codepoint <= 0xffff; ++codepoint) {
        lv_font_glyph_dsc_t descriptor;
        if(glyph_is_owned(font, codepoint, &descriptor)) ++count;
    }

    FILE * file = fopen(path, "wb");
    if(!file) return 0;
    int ok = fwrite("RBF1", 1, 4, file) == 4 &&
             write_u32(file, size) &&
             write_u32(file, (uint32_t)font->line_height) &&
             write_u32(file, (uint32_t)font->base_line) &&
             write_u32(file, count);

    for(uint32_t codepoint = 0x80; ok && codepoint <= 0xffff; ++codepoint) {
        lv_font_glyph_dsc_t descriptor;
        if(!glyph_is_owned(font, codepoint, &descriptor)) continue;

        const uint32_t pixel_count =
            (uint32_t)descriptor.box_w * (uint32_t)descriptor.box_h;
        ok = write_u32(file, codepoint) &&
             write_i32(file, descriptor.adv_w) &&
             write_i32(file, descriptor.box_w) &&
             write_i32(file, descriptor.box_h) &&
             write_i32(file, descriptor.ofs_x) &&
             write_i32(file, descriptor.ofs_y) &&
             write_u32(file, pixel_count);
        if(!ok || pixel_count == 0) continue;

        lv_draw_buf_t * buffer = lv_draw_buf_create(
            descriptor.box_w, descriptor.box_h, LV_COLOR_FORMAT_A8,
            LV_STRIDE_AUTO);
        const lv_draw_buf_t * rendered = buffer
            ? (const lv_draw_buf_t *)lv_font_get_glyph_bitmap(&descriptor, buffer)
            : NULL;
        if(!rendered) {
            if(buffer) lv_draw_buf_destroy(buffer);
            ok = 0;
            break;
        }

        for(uint32_t row = 0; ok && row < descriptor.box_h; ++row) {
            const uint8_t * source = rendered->data +
                row * rendered->header.stride;
            ok = fwrite(source, 1, descriptor.box_w, file) == descriptor.box_w;
        }
        lv_font_glyph_release_draw_data(&descriptor);
        lv_draw_buf_destroy(buffer);
    }

    if(fclose(file) != 0) ok = 0;
    if(ok) printf("%u px: exported %u glyphs to %s\n", size, count, path);
    return ok;
}

int main(int argc, char ** argv)
{
    if(argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }

    const struct {
        unsigned size;
        const lv_font_t * font;
    } fonts[] = {
        {20, &radio_font_20}, {24, &radio_font_24},
        {28, &radio_font_28}, {32, &radio_font_32},
    };

    lv_init();
    int ok = 1;
    for(size_t index = 0; ok && index < sizeof(fonts) / sizeof(fonts[0]); ++index)
        ok = export_font(argv[1], fonts[index].size, fonts[index].font);
    lv_deinit();
    return ok ? 0 : 1;
}
