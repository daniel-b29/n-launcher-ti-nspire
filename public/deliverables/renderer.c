/****************************************************************************
 * N-Launcher — renderer.c
 * 320x240 RGB565 Console UI Renderer:
 *  - Rounded clipped covers (zero corner overflow)
 *  - Dynamic layouts (1x1, 2x2, 3x2 default, 3x3, 4x4) + optional game names
 *  - Scale-only selection animation + subtle navigation luminance
 *  - Top-bar gear button + complete minimalist Settings screens (§1–§12)
 *  - Smooth animated ON/OFF switches, organic ink transition, launch zoom
 ****************************************************************************/

#include "nlauncher.h"
#include "cursors_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Precomputed quarter-circle row insets (zero float overhead) */
static int round_corner_inset(int dy, int h, int radius) {
    if (radius <= 0) return 0;
    if (radius == 3 && h == 6) {
        static const int k_inset6[6] = {2, 1, 0, 0, 1, 2};
        return (dy >= 0 && dy < 6) ? k_inset6[dy] : 0;
    }
    if (radius == 2 && h == 4) {
        static const int k_inset4[4] = {1, 0, 0, 1};
        return (dy >= 0 && dy < 4) ? k_inset4[dy] : 0;
    }
    int dist_y = -1;
    if (dy < radius) {
        dist_y = radius - 1 - dy;
    } else if (dy >= h - radius) {
        dist_y = dy - (h - radius);
    } else {
        return 0;
    }
    int r2 = radius * radius - radius;
    int keep_w = radius;
    while (keep_w > 0 && (keep_w * keep_w + dist_y * dist_y) > r2) {
        keep_w--;
    }
    return radius - keep_w;
}

uint16_t nl_rgb565_lerp(uint16_t c0, uint16_t c1, unsigned alpha_0_to_32) {
    if (alpha_0_to_32 == 0) return c0;
    if (alpha_0_to_32 >= 32) return c1;
    unsigned r0 = (c0 >> 11) & 0x1FU, g0 = (c0 >> 5) & 0x3FU, b0 = c0 & 0x1FU;
    unsigned r1 = (c1 >> 11) & 0x1FU, g1 = (c1 >> 5) & 0x3FU, b1 = c1 & 0x1FU;
    unsigned r = (r0 * (32U - alpha_0_to_32) + r1 * alpha_0_to_32) >> 5;
    unsigned g = (g0 * (32U - alpha_0_to_32) + g1 * alpha_0_to_32) >> 5;
    unsigned b = (b0 * (32U - alpha_0_to_32) + b1 * alpha_0_to_32) >> 5;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline void fb_put_pixel(uint16_t *fb, int x, int y, uint16_t color) {
    if ((unsigned)x < NL_SCREEN_W && (unsigned)y < NL_SCREEN_H) {
        fb[y * NL_SCREEN_W + x] = color;
    }
}

static void fb_fill_rect_clipped(uint16_t *fb, int x, int y, int w, int h,
                                 uint16_t color, int clip_y0, int clip_y1) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = (x + w > NL_SCREEN_W) ? NL_SCREEN_W : (x + w);
    int y1 = (y + h > clip_y1) ? clip_y1 : (y + h);
    if (x0 >= x1 || y0 >= y1) return;

    for (int py = y0; py < y1; py++) {
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = x0; px < x1; px++) {
            row[px] = color;
        }
    }
}

static void fb_fill_rounded_rect_clipped(uint16_t *fb, int x, int y, int w, int h,
                                         int radius, uint16_t color,
                                         int clip_y0, int clip_y1) {
    if (w <= 0 || h <= 0) return;
    int y0 = y < clip_y0 ? clip_y0 : y;
    int y1 = (y + h > clip_y1) ? clip_y1 : (y + h);
    if (y0 >= y1) return;

    for (int py = y0; py < y1; py++) {
        int dy = py - y;
        int inset = round_corner_inset(dy, h, radius);
        int x0 = x + inset;
        int x1 = x + w - inset;
        if (x0 < 0) x0 = 0;
        if (x1 > NL_SCREEN_W) x1 = NL_SCREEN_W;
        if (x0 >= x1) continue;
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = x0; px < x1; px++) {
            row[px] = color;
        }
    }
}

/* Subpixel 4x4 anti-aliased Euclidean stadium/capsule (renders true circles when w == h) */
static void fb_fill_aa_capsule_clipped(uint16_t *fb, int x, int y, int w, int h,
                                       uint16_t color, int clip_y0, int clip_y1) {
    if (w <= 0 || h <= 0) return;
    int y0 = y < clip_y0 ? clip_y0 : y;
    int y1 = (y + h > clip_y1) ? clip_y1 : (y + h);
    if (y0 >= y1) return;

    const int r8 = h * 4;
    const int r2_8 = r8 * r8;
    const int lx8 = r8;
    const int rx8 = w * 8 - r8;
    const int cy8 = r8;
    const int body_x0 = x + (h / 2);
    const int body_x1 = x + w - (h / 2);

    for (int py = y0; py < y1; py++) {
        int iy = py - y;
        uint16_t *row = fb + py * NL_SCREEN_W;
        int x0 = x < 0 ? 0 : x;
        int x1 = (x + w > NL_SCREEN_W) ? NL_SCREEN_W : (x + w);
        for (int px = x0; px < x1; px++) {
            if (px >= body_x0 && px < body_x1) {
                row[px] = color;
                continue;
            }
            int ix = px - x;
            unsigned hits = 0U;
            for (int sy = 0; sy < 4; sy++) {
                int dy = (iy * 8 + sy * 2 + 1) - cy8;
                int dy2 = dy * dy;
                for (int sx = 0; sx < 4; sx++) {
                    int px8 = ix * 8 + sx * 2 + 1;
                    int cx8 = (px8 < lx8) ? lx8 : ((px8 > rx8) ? rx8 : px8);
                    int dx = px8 - cx8;
                    if (dx * dx + dy2 <= r2_8) {
                        hits++;
                    }
                }
            }
            unsigned cov = hits * 2U;
            if (cov >= 32U) {
                row[px] = color;
            } else if (cov > 0U) {
                row[px] = nl_rgb565_lerp(row[px], color, cov);
            }
        }
    }
}

/* Soft rounded drop shadow behind elevated/scaled card */
static void fb_rounded_shadow_clipped(uint16_t *fb, int x, int y, int w, int h,
                                      int radius, bool is_light, int clip_y0, int clip_y1) {
    int y0 = y < clip_y0 ? clip_y0 : y;
    int y1 = (y + h > clip_y1) ? clip_y1 : (y + h);
    const uint16_t light_shadow = RGB565(136, 148, 166);
    for (int py = y0; py < y1; py++) {
        int inset = round_corner_inset(py - y, h, radius);
        int x0 = x + inset;
        int x1 = x + w - inset;
        if (x0 < 0) x0 = 0;
        if (x1 > NL_SCREEN_W) x1 = NL_SCREEN_W;
        uint16_t *row = fb + py * NL_SCREEN_W;
        if (is_light) {
            for (int px = x0; px < x1; px++) {
                row[px] = nl_rgb565_lerp(row[px], light_shadow, 7);
            }
        } else {
            for (int px = x0; px < x1; px++) {
                row[px] = (uint16_t)((row[px] >> 1) & 0x7BEFU);
            }
        }
    }
}

/* Very soft neutral ambient luminance diffusion behind navigating card */
static void fb_rounded_soft_luminance_clipped(uint16_t *fb, int x, int y, int w, int h,
                                              int radius, int hl_0_to_32, bool is_light,
                                              int clip_y0, int clip_y1) {
    if (hl_0_to_32 <= 0) return;
    static const int pads[2] = {2, 1};
    const uint16_t lum_tint = is_light ? RGB565(168, 180, 198) : RGB565(255, 255, 255);
    for (int layer = 0; layer < 2; layer++) {
        int p = pads[layer];
        unsigned alpha = (unsigned)(((layer == 0 ? 1 : 2) * hl_0_to_32 + 16) >> 5);
        if (alpha == 0) alpha = (hl_0_to_32 >= 12 && layer == 1) ? 1U : 0U;
        if (alpha == 0) continue;

        int rx = x - p;
        int ry = y - p;
        int rw = w + p * 2;
        int rh = h + p * 2;
        int rr = radius + p;
        int y0 = ry < clip_y0 ? clip_y0 : ry;
        int y1 = (ry + rh > clip_y1) ? clip_y1 : (ry + rh);
        for (int py = y0; py < y1; py++) {
            int inset = round_corner_inset(py - ry, rh, rr);
            int x0 = rx + inset;
            int x1 = rx + rw - inset;
            if (x0 < 0) x0 = 0;
            if (x1 > NL_SCREEN_W) x1 = NL_SCREEN_W;
            uint16_t *row = fb + py * NL_SCREEN_W;
            for (int px = x0; px < x1; px++) {
                row[px] = nl_rgb565_lerp(row[px], lum_tint, alpha);
            }
        }
    }
}

/* Minimal 5x7 font */
static const uint8_t G_FONT_5X7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},{0x1C,0x3E,0x7F,0x3E,0x1C}
};

static void fb_draw_char(uint16_t *fb, int x, int y, char c, uint16_t color, int clip_y0, int clip_y1) {
    uint8_t uc = (uint8_t)c;
    if (uc < 0x20 || uc > 0x7F) uc = '?';
    const uint8_t *glyph = G_FONT_5X7[uc - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if ((bits & (1U << row)) && (y + row >= clip_y0) && (y + row < clip_y1)) {
                fb_put_pixel(fb, x + col, y + row, color);
            }
        }
    }
}

static void fb_draw_char_2x(uint16_t *fb, int x, int y, char c, uint16_t color, int clip_y0, int clip_y1) {
    uint8_t uc = (uint8_t)c;
    if (uc < 0x20 || uc > 0x7F) uc = '?';
    const uint8_t *glyph = G_FONT_5X7[uc - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1U << row)) {
                fb_fill_rect_clipped(fb, x + col * 2, y + row * 2, 2, 2, color, clip_y0, clip_y1);
            }
        }
    }
}

static void fb_draw_text_clipped(uint16_t *fb, int x, int y, const char *str,
                                 uint16_t color, int max_chars, int clip_y0, int clip_y1) {
    if (!str) return;
    int count = 0;
    while (*str && (max_chars <= 0 || count < max_chars)) {
        fb_draw_char(fb, x, y, *str, color, clip_y0, clip_y1);
        x += 6;
        str++;
        count++;
    }
}

static void fb_draw_text(uint16_t *fb, int x, int y, const char *str, uint16_t color, int max_chars) {
    fb_draw_text_clipped(fb, x, y, str, color, max_chars, 0, NL_SCREEN_H);
}

static void fb_draw_text_ellipsis(uint16_t *fb, int x, int y, const char *str,
                                  uint16_t color, int max_chars, int clip_y0, int clip_y1) {
    if (!str || max_chars <= 0) return;
    int len = (int)strlen(str);
    if (len <= max_chars) {
        fb_draw_text_clipped(fb, x, y, str, color, max_chars, clip_y0, clip_y1);
        return;
    }
    if (max_chars <= 2) {
        fb_draw_text_clipped(fb, x, y, str, color, max_chars, clip_y0, clip_y1);
        return;
    }
    fb_draw_text_clipped(fb, x, y, str, color, max_chars - 2, clip_y0, clip_y1);
    fb_draw_text_clipped(fb, x + (max_chars - 2) * 6, y, "..", color, 2, clip_y0, clip_y1);
}

/* Integer LUT & helpers (shared by rotating gear, organic ink & Settings liquid-drop) */
static int8_t   G_INK_SIN256[256];
static bool     G_INK_SIN_READY = false;
static uint8_t  G_INK_FIELD[NL_FB_PIXELS];
static uint32_t G_INK_CACHED_SEED = 0xFFFFFFFFU;
static int      G_INK_MAX_VAL = 220;

static void init_ink_sin_lut(void) {
    if (G_INK_SIN_READY) return;
    for (int i = 0; i < 128; i++) {
        int p = i * (128 - i);
        int v = (16 * 127 * p) / (5 * 128 * 128 - 4 * p);
        G_INK_SIN256[i] = (int8_t)v;
        G_INK_SIN256[128 + i] = (int8_t)(-v);
    }
    G_INK_SIN_READY = true;
}

static int ink_isqrt32(int val) {
    if (val <= 0) return 0;
    uint32_t x = (uint32_t)val;
    uint32_t res = 0U;
    uint32_t bit = 1U << 30;
    while (bit > x) bit >>= 2;
    while (bit != 0U) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (int)res;
}

static int ink_octant_64(int ux, int uy) {
    if (ux <= 0 && uy <= 0) return 0;
    int big = (ux >= uy) ? ux : uy;
    int sml = (ux >= uy) ? uy : ux;
    int ratio = (sml * 64) / big;
    int oct = (ratio * 32 + ((64 - ratio) * ratio * 9) / 64) >> 6;
    return (ux >= uy) ? oct : (64 - oct);
}

static int ink_atan2_256(int dx, int dy) {
    if (dx == 0 && dy == 0) return 0;
    int ux = (dx < 0) ? -dx : dx;
    int uy = (dy < 0) ? -dy : dy;
    int a0 = ink_octant_64(ux, uy);
    if (dx >= 0 && dy >= 0) return a0 & 255;
    if (dx < 0 && dy >= 0)  return (128 - a0) & 255;
    if (dx < 0 && dy < 0)   return (128 + a0) & 255;
    return (256 - a0) & 255;
}

/* Crisp 9x9 gear bitmap with optional smooth clockwise rotation (angle_256 in 0..255) */
static void draw_gear_button_ex(uint16_t *fb, int bx, int by, int bd, const nl_theme_t *th, bool active, int angle_256) {
    uint16_t bg = active ? th->text_primary : th->accent_primary;
    uint16_t fg = active ? th->bg_top_bar : th->text_on_accent;
    fb_fill_aa_capsule_clipped(fb, bx, by, bd, bd, bg, 0, NL_SCREEN_H);

    int ang = angle_256 & 255;
    if (ang == 0) {
        static const uint16_t k_gear9[9] = {
            0x054, /* . . # . # . # . . */
            0x0BA, /* . # . # # # . # . */
            0x1FF, /* # # # # # # # # # */
            0x1C7, /* # # # . . . # # # */
            0x0EE, /* . # # # . # # # . */
            0x1C7, /* # # # . . . # # # */
            0x1FF, /* # # # # # # # # # */
            0x0BA, /* . # . # # # . # . */
            0x054  /* . . # . # . # . . */
        };
        int gx = bx + (bd - 9) / 2;
        int gy = by + (bd - 9) / 2;
        for (int r = 0; r < 9; r++) {
            uint16_t row_bits = k_gear9[r];
            for (int c = 0; c < 9; c++) {
                if (row_bits & (1U << (8 - c))) {
                    fb_put_pixel(fb, gx + c, gy + r, fg);
                }
            }
        }
        return;
    }

    init_ink_sin_lut();
    int cx2 = bx * 2 + bd;
    int cy2 = by * 2 + bd;
    for (int py = by + 1; py < by + bd - 1; py++) {
        int dy2 = py * 2 + 1 - cy2;
        for (int px = bx + 1; px < bx + bd - 1; px++) {
            int dx2 = px * 2 + 1 - cx2;
            int r2 = dx2 * dx2 + dy2 * dy2;
            if (r2 < 12 || r2 > 110) continue;
            if (r2 <= 58) {
                fb_put_pixel(fb, px, py, fg);
                continue;
            }
            int th256 = ink_atan2_256(dx2, dy2);
            int rel = (th256 - ang) & 31;
            if (rel < 15) {
                fb_put_pixel(fb, px, py, fg);
            }
        }
    }
}

static void draw_gear_button(uint16_t *fb, int bx, int by, int bd, const nl_theme_t *th, bool active) {
    draw_gear_button_ex(fb, bx, by, bd, th, active, 0);
}


/* Smooth animated ON/OFF capsule toggle switch (anim_fp in 0..256) with subtle mid-travel knob elongation */
static void draw_toggle_switch(uint16_t *fb, int x, int y, int anim_fp, const nl_theme_t *th) {
    if (anim_fp < 0) anim_fp = 0;
    if (anim_fp > 256) anim_fp = 256;
    const int sw = 32;
    const int sh = 14;
    const int knob_d = 10;

    /* Smooth cubic ease-in-out curve for knob travel */
    int ease_fp;
    if (anim_fp <= 128) {
        ease_fp = (anim_fp * anim_fp) >> 7;
    } else {
        int inv = 256 - anim_fp;
        ease_fp = 256 - ((inv * inv) >> 7);
    }

    unsigned alpha = (unsigned)((ease_fp + 4) >> 3); /* 0..32 */
    uint16_t track_col = nl_rgb565_lerp(th->dot_inactive, th->accent_primary, alpha);
    fb_fill_aa_capsule_clipped(fb, x, y, sw, sh, track_col, 0, NL_SCREEN_H);

    /* Subtle capsule elongation (+2 px at midpoint, 0 px at endpoints 0 and 256) */
    int dist_mid = (anim_fp >= 128) ? (anim_fp - 128) : (128 - anim_fp);
    int stretch = ((128 - dist_mid) * 2 + 64) >> 7;
    int kw = knob_d + stretch;
    int kx = x + 2 + (((sw - 4 - kw) * ease_fp + 128) >> 8);
    int ky = y + 2;
    fb_fill_aa_capsule_clipped(fb, kx, ky, kw, knob_d, RGB565(255, 255, 255), 0, NL_SCREEN_H);
}

/* Fast integer bilinear RGB565 sampler (u256 = x * 256, v256 = y * 256 in source texels) */
static uint16_t sample_rgb565_bilinear(const uint16_t *pix, int w, int h, int u256, int v256) {
    if (w <= 1 && h <= 1) return pix[0];
    int max_u = (w - 1) << 8;
    int max_v = (h - 1) << 8;
    if (u256 < 0) u256 = 0;
    if (u256 > max_u) u256 = max_u;
    if (v256 < 0) v256 = 0;
    if (v256 > max_v) v256 = max_v;

    int x0 = u256 >> 8;
    int y0 = v256 >> 8;
    int fx = u256 & 255;
    int fy = v256 & 255;
    int x1 = (x0 + 1 < w) ? (x0 + 1) : x0;
    int y1 = (y0 + 1 < h) ? (y0 + 1) : y0;

    if (fx == 0 && fy == 0) {
        return pix[y0 * w + x0];
    }

    uint16_t p00 = pix[y0 * w + x0];
    uint16_t p10 = pix[y0 * w + x1];
    uint16_t p01 = pix[y1 * w + x0];
    uint16_t p11 = pix[y1 * w + x1];

    unsigned w00 = (unsigned)((256 - fx) * (256 - fy));
    unsigned w10 = (unsigned)(fx * (256 - fy));
    unsigned w01 = (unsigned)((256 - fx) * fy);
    unsigned w11 = (unsigned)(fx * fy);

    unsigned r = ((((p00 >> 11) & 0x1FU) * w00) + (((p10 >> 11) & 0x1FU) * w10) +
                  (((p01 >> 11) & 0x1FU) * w01) + (((p11 >> 11) & 0x1FU) * w11) + 32768U) >> 16;
    unsigned g = ((((p00 >> 5) & 0x3FU) * w00) + (((p10 >> 5) & 0x3FU) * w10) +
                  (((p01 >> 5) & 0x3FU) * w01) + (((p11 >> 5) & 0x3FU) * w11) + 32768U) >> 16;
    unsigned b = (((p00 & 0x1FU) * w00) + ((p10 & 0x1FU) * w10) +
                  ((p01 & 0x1FU) * w01) + ((p11 & 0x1FU) * w11) + 32768U) >> 16;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static unsigned sample_alpha_bilinear(const uint8_t *alpha, int w, int h, int u256, int v256) {
    if (!alpha) return 255U;
    int max_u = (w - 1) << 8;
    int max_v = (h - 1) << 8;
    if (u256 < 0) u256 = 0;
    if (u256 > max_u) u256 = max_u;
    if (v256 < 0) v256 = 0;
    if (v256 > max_v) v256 = max_v;

    int x0 = u256 >> 8;
    int y0 = v256 >> 8;
    int fx = u256 & 255;
    int fy = v256 & 255;
    int x1 = (x0 + 1 < w) ? (x0 + 1) : x0;
    int y1 = (y0 + 1 < h) ? (y0 + 1) : y0;

    unsigned w00 = (unsigned)((256 - fx) * (256 - fy));
    unsigned w10 = (unsigned)(fx * (256 - fy));
    unsigned w01 = (unsigned)((256 - fx) * fy);
    unsigned w11 = (unsigned)(fx * fy);

    unsigned a = ((unsigned)alpha[y0 * w + x0] * w00 + (unsigned)alpha[y0 * w + x1] * w10 +
                  (unsigned)alpha[y1 * w + x0] * w01 + (unsigned)alpha[y1 * w + x1] * w11 + 32768U) >> 16;
    return a;
}

/* Crisp premultiplied integer area-box downsampling when dst_w < src_w or dst_h < src_h */
static void sample_icon_area_rgba565(
    const uint16_t *pix, const uint8_t *alpha,
    int src_w, int src_h,
    int dst_x, int dst_y, int dst_w, int dst_h,
    uint16_t *out_rgb565, unsigned *out_a255
) {
    int sx0 = (dst_x * src_w) / dst_w;
    int sx1 = ((dst_x + 1) * src_w + dst_w - 1) / dst_w;
    if (sx1 <= sx0) sx1 = sx0 + 1;
    if (sx1 > src_w) sx1 = src_w;

    int sy0 = (dst_y * src_h) / dst_h;
    int sy1 = ((dst_y + 1) * src_h + dst_h - 1) / dst_h;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sy1 > src_h) sy1 = src_h;

    uint32_t acc_r = 0U, acc_g = 0U, acc_b = 0U, acc_a = 0U, acc_n = 0U;
    for (int sy = sy0; sy < sy1; sy++) {
        int row_off = sy * src_w;
        for (int sx = sx0; sx < sx1; sx++) {
            uint16_t c = pix[row_off + sx];
            unsigned r5 = (c >> 11) & 0x1FU;
            unsigned g6 = (c >> 5)  & 0x3FU;
            unsigned b5 =  c        & 0x1FU;
            unsigned r8 = (r5 << 3) | (r5 >> 2);
            unsigned g8 = (g6 << 2) | (g6 >> 4);
            unsigned b8 = (b5 << 3) | (b5 >> 2);
            if (alpha) {
                unsigned a = (unsigned)alpha[row_off + sx];
                acc_r += r8 * a;
                acc_g += g8 * a;
                acc_b += b8 * a;
                acc_a += a;
            } else {
                acc_r += r8;
                acc_g += g8;
                acc_b += b8;
            }
            acc_n++;
        }
    }
    if (acc_n == 0U) acc_n = 1U;
    if (alpha) {
        unsigned a_avg = (acc_a + acc_n / 2U) / acc_n;
        *out_a255 = (a_avg > 255U) ? 255U : a_avg;
        if (acc_a > 0U) {
            unsigned r = (acc_r + acc_a / 2U) / acc_a;
            unsigned g = (acc_g + acc_a / 2U) / acc_a;
            unsigned b = (acc_b + acc_a / 2U) / acc_a;
            if (r > 255U) r = 255U;
            if (g > 255U) g = 255U;
            if (b > 255U) b = 255U;
            *out_rgb565 = RGB565(r, g, b);
        } else {
            *out_rgb565 = 0U;
        }
    } else {
        unsigned r = (acc_r + acc_n / 2U) / acc_n;
        unsigned g = (acc_g + acc_n / 2U) / acc_n;
        unsigned b = (acc_b + acc_n / 2U) / acc_n;
        if (r > 255U) r = 255U;
        if (g > 255U) g = 255U;
        if (b > 255U) b = 255U;
        *out_rgb565 = RGB565(r, g, b);
        *out_a255 = 255U;
    }
}

/* 4x4 subpixel anti-aliased rounded corner coverage (0..32) + discreet 1px perimeter border detection */
static unsigned aa_rounded_card_pixel(int ix, int iy, int w, int h, int rad, bool *out_border) {
    if (rad <= 0) {
        *out_border = (ix == 0 || ix == w - 1 || iy == 0 || iy == h - 1);
        return 32U;
    }
    int r = rad;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    int cx = -1, cy = -1;
    if (ix < r) cx = r;
    else if (ix >= w - r) cx = w - r;

    if (iy < r) cy = r;
    else if (iy >= h - r) cy = h - r;

    if (cx < 0 || cy < 0) {
        *out_border = (ix == 0 || ix == w - 1 || iy == 0 || iy == h - 1);
        return 32U;
    }

    int r8 = r * 8;
    int r2_8 = r8 * r8;
    int rin8 = (r > 1) ? ((r - 1) * 8) : 0;
    int rin2_8 = rin8 * rin8;
    int cx8 = cx * 8;
    int cy8 = cy * 8;

    unsigned hits = 0U;
    for (int sy = 0; sy < 4; sy++) {
        int dy = (iy * 8 + sy * 2 + 1) - cy8;
        int dy2 = dy * dy;
        for (int sx = 0; sx < 4; sx++) {
            int dx = (ix * 8 + sx * 2 + 1) - cx8;
            if (dx * dx + dy2 <= r2_8) hits++;
        }
    }
    int mdx = (ix * 8 + 4) - cx8;
    int mdy = (iy * 8 + 4) - cy8;
    *out_border = (hits > 0U && (mdx * mdx + mdy * mdy >= rin2_8));
    return hits * 2U;
}

/* Compute aspect-preserving "contain" placement inside (cw, ch) with zero cropping/stretching */
static void compute_icon_contain_box(int iw, int ih, int cw, int ch,
                                     int *out_ox, int *out_oy, int *out_fw, int *out_fh) {
    if (iw <= 0 || ih <= 0 || cw <= 0 || ch <= 0) {
        *out_ox = 0; *out_oy = 0; *out_fw = cw; *out_fh = ch;
        return;
    }
    int64_t lhs = (int64_t)iw * (int64_t)ch;
    int64_t rhs = (int64_t)cw * (int64_t)ih;
    /* Exact match or within 1px ratio difference -> full bleed */
    if (lhs == rhs) {
        *out_ox = 0; *out_oy = 0; *out_fw = cw; *out_fh = ch;
        return;
    }
    if (lhs > rhs) {
        int fh = (int)(((int64_t)cw * ih + iw / 2) / iw);
        if (fh < 1) fh = 1;
        if (fh > ch) fh = ch;
        *out_fw = cw;
        *out_fh = fh;
        *out_ox = 0;
        *out_oy = (ch - fh) / 2;
    } else {
        int fw = (int)(((int64_t)ch * iw + ih / 2) / ih);
        if (fw < 1) fw = 1;
        if (fw > cw) fw = cw;
        *out_fw = fw;
        *out_fh = ch;
        *out_ox = (cw - fw) / 2;
        *out_oy = 0;
    }
}

/* Draw a cover card strictly clipped to AA rounded mask & viewport band with discreet outer border */
static void draw_game_cover_card(
    uint16_t *fb,
    int cx, int cy, int cw, int ch,
    int radius,
    int hl_0_to_32,
    const nl_game_t *g,
    const nl_theme_t *th,
    int clip_y0, int clip_y1
) {
    int y0 = cy < clip_y0 ? clip_y0 : cy;
    int y1 = (cy + ch > clip_y1) ? clip_y1 : (cy + ch);
    if (y0 >= y1 || cw <= 0 || ch <= 0) return;

    int x0 = cx < 0 ? 0 : cx;
    int x1 = (cx + cw > NL_SCREEN_W) ? NL_SCREEN_W : (cx + cw);
    if (x0 >= x1) return;

    unsigned surface_lift = (hl_0_to_32 > 0) ? (unsigned)((hl_0_to_32 + 10) >> 4) : 0U;
    if (surface_lift > 2U) surface_lift = 2U;

    if (g->icon_source != NL_ICON_NONE && g->icon_pixels != NULL) {
        int iw = (g->icon_w > 0) ? (int)g->icon_w : NL_COVER_W;
        int ih = (g->icon_h > 0) ? (int)g->icon_h : NL_COVER_H;
        int ox = 0, oy = 0, fw = cw, fh = ch;
        compute_icon_contain_box(iw, ih, cw, ch, &ox, &oy, &fw, &fh);
        int bx = cx + ox;
        int by = cy + oy;
        int by0 = by < clip_y0 ? clip_y0 : by;
        int by1 = (by + fh > clip_y1) ? clip_y1 : (by + fh);
        int bx0 = bx < 0 ? 0 : bx;
        int bx1 = (bx + fw > NL_SCREEN_W) ? NL_SCREEN_W : (bx + fw);
        bool use_area = (fw < iw || fh < ih);
        uint16_t icon_backdrop = nl_rgb565_lerp(th->bg_main, th->card_bg, 20U);

        for (int py = by0; py < by1; py++) {
            int iy_fit = py - by;
            uint16_t *dst_row = fb + py * NL_SCREEN_W;
            bool edge_fade = (clip_y1 == NL_VIEW_BOTTOM && py >= NL_VIEW_BOTTOM - 5);
            unsigned fade_a = edge_fade ? (unsigned)(py - (NL_VIEW_BOTTOM - 5)) * 4U : 0U;
            int v256 = (fh > 1 && ih > 1) ? ((iy_fit * (ih - 1) * 256) / (fh - 1)) : 0;

            for (int px = bx0; px < bx1; px++) {
                int ix_fit = px - bx;
                bool is_border = false;
                unsigned cov = aa_rounded_card_pixel(ix_fit, iy_fit, fw, fh, radius, &is_border);
                if (cov == 0U) continue;

                uint16_t icon_c;
                unsigned a255 = 255U;
                if (use_area) {
                    sample_icon_area_rgba565(g->icon_pixels, g->icon_alpha,
                                             iw, ih, ix_fit, iy_fit, fw, fh,
                                             &icon_c, &a255);
                } else {
                    int u256 = (fw > 1 && iw > 1) ? ((ix_fit * (iw - 1) * 256) / (fw - 1)) : 0;
                    icon_c = sample_rgb565_bilinear(g->icon_pixels, iw, ih, u256, v256);
                    if (g->icon_alpha != NULL) {
                        a255 = sample_alpha_bilinear(g->icon_alpha, iw, ih, u256, v256);
                    }
                }
                uint16_t px_col = icon_c;
                if (g->icon_alpha != NULL && a255 < 255U) {
                    unsigned a32 = (a255 * 32U + 127U) / 255U;
                    px_col = nl_rgb565_lerp(icon_backdrop, icon_c, a32);
                }

                if (surface_lift > 0U) {
                    px_col = nl_rgb565_lerp(px_col, RGB565(250, 252, 255), surface_lift);
                }
                if (is_border) {
                    px_col = nl_rgb565_lerp(px_col, th->border_subtle, 10U);
                }
                if (edge_fade) {
                    px_col = nl_rgb565_lerp(px_col, th->bg_main, fade_a);
                }
                dst_row[px] = (cov >= 32U) ? px_col : nl_rgb565_lerp(dst_row[px], px_col, cov);
            }
        }
    } else {
        uint16_t bg_top = nl_rgb565_lerp(th->card_bg, g->accent_color, th->is_light ? 4U : 5U);
        uint16_t bg_bot = nl_rgb565_lerp(th->is_light ? th->bg_top_bar : th->bg_main, g->accent_color, 2U);

        for (int py = y0; py < y1; py++) {
            int dy = py - cy;
            uint16_t row_base = nl_rgb565_lerp(bg_top, bg_bot, (unsigned)((dy * 32) / ch));
            if (surface_lift > 0U) {
                row_base = nl_rgb565_lerp(row_base, RGB565(250, 252, 255), surface_lift);
            }
            bool edge_fade = (clip_y1 == NL_VIEW_BOTTOM && py >= NL_VIEW_BOTTOM - 5);
            unsigned fade_a = edge_fade ? (unsigned)(py - (NL_VIEW_BOTTOM - 5)) * 4U : 0U;

            uint16_t *dst_row = fb + py * NL_SCREEN_W;
            for (int px = x0; px < x1; px++) {
                int dx = px - cx;
                bool is_border = false;
                unsigned cov = aa_rounded_card_pixel(dx, dy, cw, ch, radius, &is_border);
                if (cov == 0U) continue;
                uint16_t px_col = is_border ? nl_rgb565_lerp(row_base, th->border_subtle, 24U) : row_base;
                if (edge_fade) {
                    px_col = nl_rgb565_lerp(px_col, th->bg_main, fade_a);
                }
                dst_row[px] = (cov >= 32U) ? px_col : nl_rgb565_lerp(dst_row[px], px_col, cov);
            }
        }

        uint16_t mono_accent = th->is_light
            ? nl_rgb565_lerp(g->accent_color, th->text_primary, 8U)
            : g->accent_color;

        int bar_pad = (cw >= 70) ? 12 : (cw >= 46 ? 8 : 5);
        int bar_w = cw - bar_pad * 2;
        if (bar_w >= 8) {
            fb_fill_rounded_rect_clipped(fb, cx + bar_pad, cy + ch - 7, bar_w, 3, 1,
                                         nl_rgb565_lerp(th->card_bg, mono_accent, 18), y0, y1);
        }

        char mono[3];
        nl_compute_monogram(g->title, mono);
        if (cw >= 58 && ch >= 42) {
            int mx = cx + (cw - 22) / 2;
            int my = cy + (ch - 14) / 2 - 2;
            fb_draw_char_2x(fb, mx, my, mono[0], th->text_primary, y0, y1);
            fb_draw_char_2x(fb, mx + 12, my, mono[1], mono_accent, y0, y1);
        } else {
            int mx = cx + (cw - 11) / 2;
            int my = cy + (ch - 7) / 2 - 1;
            fb_draw_char(fb, mx, my, mono[0], th->text_primary, y0, y1);
            fb_draw_char(fb, mx + 6, my, mono[1], mono_accent, y0, y1);
        }
    }

    /* Discrete favorite badge (when card is large enough) */
    if (g->is_favorite && cw >= 42 && ch >= 32 && y0 <= cy + 4 && y1 >= cy + 14) {
        fb_fill_rounded_rect_clipped(fb, cx + cw - 14, cy + 3, 11, 11, 3,
                                     RGB565(18, 20, 28), y0, y1);
        fb_draw_char(fb, cx + cw - 11, cy + 5, '\x7F', th->badge_fav, y0, y1);
    }

    /* Discrete custom shortcut key badge in top-left if assigned */
    if (g->shortcut_key != 0 && cw >= 46 && ch >= 32 && y0 <= cy + 4 && y1 >= cy + 14) {
        fb_fill_rounded_rect_clipped(fb, cx + 3, cy + 3, 11, 11, 3,
                                     th->accent_primary, y0, y1);
        fb_draw_char(fb, cx + 6, cy + 5, g->shortcut_key, th->text_on_accent, y0, y1);
    }
}

/* Draw N-Launcher rounded logo emblem at (x, y) */
static void draw_launcher_emblem(uint16_t *fb, int x, int y, int size, const nl_theme_t *th) {
    int rad = (size <= 16) ? 4 : ((size * 26 + 50) / 100);
    fb_fill_rounded_rect_clipped(fb, x, y, size, size, rad, th->accent_primary, 0, NL_SCREEN_H);
    if (size >= 14) {
        int pad = (size <= 16) ? (size / 4) : ((size * 25 + 50) / 100);
        int stroke = (size <= 20) ? 2 : ((size * 13 + 50) / 100);
        if (stroke < 2) stroke = 2;
        int h = size - pad * 2;
        int span = size - pad * 2 - stroke;
        if (span < 1) span = 1;
        uint16_t glyph_col = th->text_on_accent;
        fb_fill_rect_clipped(fb, x + pad, y + pad, stroke, h, glyph_col, 0, NL_SCREEN_H);
        fb_fill_rect_clipped(fb, x + size - pad - stroke, y + pad, stroke, h, glyph_col, 0, NL_SCREEN_H);
        for (int i = 0; i < h; i++) {
            int dx = pad + (i * span) / (h > 1 ? h - 1 : 1);
            fb_fill_rect_clipped(fb, x + dx, y + pad + i, stroke, 1, glyph_col, 0, NL_SCREEN_H);
        }
    }
}

/* Circular profile avatar renderer using verbatim 40x40 RGB565 asset with inset sampling (zero white corners) & 4x4 AA edge */
static void draw_circular_avatar_box(
    uint16_t *fb,
    int x,
    int y,
    int diam,
    int avatar_id,
    int y_min,
    int y_max,
    bool dim,
    const nl_theme_t *th
) {
    if (diam < 4) return;
    const uint16_t *pix = nl_avatar_get_pixels(avatar_id);
    if (!pix) return;

    int py0 = y < y_min ? y_min : y;
    int py1 = (y + diam > y_max) ? y_max : (y + diam);
    if (py0 < 0) py0 = 0;
    if (py1 > NL_SCREEN_H) py1 = NL_SCREEN_H;

    int px0 = x < 0 ? 0 : x;
    int px1 = (x + diam > NL_SCREEN_W) ? NL_SCREEN_W : (x + diam);
    if (py0 >= py1 || px0 >= px1) return;

    const int r8 = diam * 4;
    const int r2_8 = r8 * r8;
    const int src_mid_256 = (NL_AVATAR_SRC_DIM - 1) * 128; /* 19.5 * 256 */
    /* Map dest radius strictly inside ~17.9 texel source radius so white padding never enters bilinear kernel */
    const int src_rad_scale = 4560; /* 17.81 * 256 */
    const int src_clamp2 = 4560 * 4560;

    for (int py = py0; py < py1; py++) {
        int iy = py - y;
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = px0; px < px1; px++) {
            int ix = px - x;
            unsigned hits = 0U;
            int min_dy = (iy * 8 + 1) - r8;
            int max_dy = (iy * 8 + 7) - r8;
            int min_dx = (ix * 8 + 1) - r8;
            int max_dx = (ix * 8 + 7) - r8;
            int far_dx = (abs(min_dx) > abs(max_dx)) ? min_dx : max_dx;
            int far_dy = (abs(min_dy) > abs(max_dy)) ? min_dy : max_dy;
            if (far_dx * far_dx + far_dy * far_dy <= r2_8) {
                hits = 16U;
            } else {
                for (int sy = 0; sy < 4; sy++) {
                    int dy = (iy * 8 + sy * 2 + 1) - r8;
                    int dy2 = dy * dy;
                    for (int sx = 0; sx < 4; sx++) {
                        int dx = (ix * 8 + sx * 2 + 1) - r8;
                        if (dx * dx + dy2 <= r2_8) hits++;
                    }
                }
            }
            if (hits == 0U) continue;

            int rel_x = (ix * 2 + 1) - diam;
            int rel_y = (iy * 2 + 1) - diam;
            int du = (rel_x * src_rad_scale) / diam;
            int dv = (rel_y * src_rad_scale) / diam;
            int d2 = du * du + dv * dv;
            if (d2 > src_clamp2) {
                int d = ink_isqrt32(d2);
                if (d > 0) {
                    du = (du * src_rad_scale) / d;
                    dv = (dv * src_rad_scale) / d;
                }
            }
            uint16_t col = sample_rgb565_bilinear(pix, NL_AVATAR_SRC_DIM, NL_AVATAR_SRC_DIM,
                                                  src_mid_256 + du, src_mid_256 + dv);
            if (dim && th) {
                col = nl_rgb565_lerp(col, th->bg_main, 20U);
            }
            unsigned cov = hits * 2U;
            row[px] = (cov >= 32U) ? col : nl_rgb565_lerp(row[px], col, cov);
        }
    }
}

/* Translucent circular avatar renderer for sequential ghost silhouettes & smooth fade transitions (zero heap allocation) */
static void draw_circular_avatar_box_alpha(
    uint16_t *fb,
    int x,
    int y,
    int diam,
    int avatar_id,
    int y_min,
    int y_max,
    unsigned alpha_32,
    uint16_t tint_col,
    unsigned tint_mix_32
) {
    if (diam < 6 || alpha_32 == 0U) return;
    if (alpha_32 > 32U) alpha_32 = 32U;
    const uint16_t *pix = nl_avatar_get_pixels(avatar_id);
    if (!pix) return;

    int py0 = y < y_min ? y_min : y;
    int py1 = (y + diam > y_max) ? y_max : (y + diam);
    if (py0 < 0) py0 = 0;
    if (py1 > NL_SCREEN_H) py1 = NL_SCREEN_H;

    int px0 = x < 0 ? 0 : x;
    int px1 = (x + diam > NL_SCREEN_W) ? NL_SCREEN_W : (x + diam);
    if (py0 >= py1 || px0 >= px1) return;

    const int r2 = diam; /* radius in 2x subpixel units */
    const int r_in = (r2 > 3) ? (r2 - 2) : r2;
    const int r2_sq = r2 * r2;
    const int r_in_sq = r_in * r_in;
    const int ring_span = (r2_sq > r_in_sq) ? (r2_sq - r_in_sq) : 1;
    const int src_mid_256 = (NL_AVATAR_SRC_DIM - 1) * 128;
    const int src_rad_scale = 4560;
    const int src_clamp2 = 4560 * 4560;

    for (int py = py0; py < py1; py++) {
        int rel_y = ((py - y) * 2 + 1) - diam;
        int dy2 = rel_y * rel_y;
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = px0; px < px1; px++) {
            int rel_x = ((px - x) * 2 + 1) - diam;
            int d2_pix = rel_x * rel_x + dy2;
            if (d2_pix > r2_sq) continue;

            unsigned edge_a = alpha_32;
            if (d2_pix > r_in_sq) {
                edge_a = (alpha_32 * (unsigned)(r2_sq - d2_pix)) / (unsigned)ring_span;
                if (edge_a == 0U) continue;
            }

            int du = (rel_x * src_rad_scale) / diam;
            int dv = (rel_y * src_rad_scale) / diam;
            int d2 = du * du + dv * dv;
            if (d2 > src_clamp2) {
                int d = ink_isqrt32(d2);
                if (d > 0) {
                    du = (du * src_rad_scale) / d;
                    dv = (dv * src_rad_scale) / d;
                }
            }
            uint16_t col = sample_rgb565_bilinear(pix, NL_AVATAR_SRC_DIM, NL_AVATAR_SRC_DIM,
                                                  src_mid_256 + du, src_mid_256 + dv);
            if (tint_mix_32 > 0U) {
                col = nl_rgb565_lerp(col, tint_col, tint_mix_32);
            }
            row[px] = (edge_a >= 32U) ? col : nl_rgb565_lerp(row[px], col, edge_a);
        }
    }
}

/* Soft diffused water-drop luminous waves (multi-pixel feathered thickness, theme + white crest) */
static void draw_profile_water_drop_waves(
    uint16_t *fb,
    int cx,
    int cy,
    int progress_fp,
    int max_radius,
    const nl_theme_t *th,
    int y_min,
    int y_max
) {
    if (progress_fp <= 0 || progress_fp >= 256 || max_radius <= 8) return;

    uint16_t crest_col = th->is_light ? th->accent_primary : RGB565(255, 255, 255);
    uint16_t glow_col  = th->is_light ? th->accent_secondary : th->accent_primary;

    for (int w = 0; w < 3; w++) {
        int wp = progress_fp - w * 38;
        if (wp <= 0 || wp >= 220) continue;
        int rad = 10 + ((max_radius - 10) * wp) / 220;
        int thick = 8 + (w * 2);
        unsigned base_a = (unsigned)(((220 - wp) * (20 - w * 4)) / 220);
        if (base_a == 0U) continue;
        if (base_a > 24U) base_a = 24U;

        int r_out = rad + thick / 2;
        int r_in  = (rad > thick / 2) ? (rad - thick / 2) : 1;
        int r_out2 = r_out * r_out;
        int r_in2  = r_in * r_in;
        int r_mid2 = rad * rad;

        int x0 = cx - r_out - 1;
        int x1 = cx + r_out + 1;
        int y0 = cy - r_out - 1;
        int y1 = cy + r_out + 1;
        if (x0 < 0) x0 = 0;
        if (x1 > NL_SCREEN_W) x1 = NL_SCREEN_W;
        if (y0 < y_min) y0 = y_min;
        if (y1 > y_max) y1 = y_max;
        if (y0 < 0) y0 = 0;
        if (y1 > NL_SCREEN_H) y1 = NL_SCREEN_H;

        for (int py = y0; py < y1; py++) {
            int dy = py - cy;
            int dy2 = dy * dy;
            uint16_t *row = fb + py * NL_SCREEN_W;
            for (int px = x0; px < x1; px++) {
                int dx = px - cx;
                int d2 = dx * dx + dy2;
                if (d2 < r_in2 || d2 > r_out2) continue;

                unsigned a = 0U;
                uint16_t c = glow_col;
                if (d2 >= r_mid2) {
                    int span = r_out2 - r_mid2;
                    a = (span > 0) ? (unsigned)((base_a * (unsigned)(r_out2 - d2)) / (unsigned)span) : base_a;
                    c = nl_rgb565_lerp(glow_col, crest_col, 14U);
                } else {
                    int span = r_mid2 - r_in2;
                    a = (span > 0) ? (unsigned)((base_a * (unsigned)(d2 - r_in2)) / (unsigned)span) : base_a;
                    c = glow_col;
                }
                if (a > 0U) {
                    row[px] = nl_rgb565_lerp(row[px], c, a);
                }
            }
        }
    }
}

/* Circular '+' badge renderer with smooth 0..90 deg cross rotation & alpha blending for ghost trails */
static void draw_plus_badge_alpha(
    uint16_t *fb,
    int cx,
    int cy,
    int diam,
    int rot_fp,
    unsigned alpha_32,
    bool full,
    bool focused,
    const nl_theme_t *th,
    int y_min,
    int y_max
) {
    if (diam < 8 || alpha_32 == 0U) return;
    if (alpha_32 > 32U) alpha_32 = 32U;

    /* Quarter-circle trig LUT for 0..90 deg rotation in 17 steps (scaled by 256) */
    static const int16_t k_cos90[17] = {
        256, 255, 251, 245, 237, 226, 213, 198, 181, 162, 142, 121, 98, 75, 50, 25, 0
    };
    int idx = (rot_fp < 0) ? 0 : (rot_fp > 256 ? 16 : ((rot_fp + 8) >> 4));
    int cos_v = k_cos90[idx];
    int sin_v = k_cos90[16 - idx];

    uint16_t bg_col = th->is_light ? th->pill_bg : RGB565(36, 42, 54);
    if (focused) {
        bg_col = nl_rgb565_lerp(bg_col, th->accent_primary, 7U);
    }
    uint16_t ring_col = focused ? th->accent_secondary : th->border_subtle;
    uint16_t sym_col = full
        ? th->text_secondary
        : (focused ? (th->is_light ? th->accent_primary : RGB565(255, 255, 255)) : th->text_primary);

    int r2 = diam;
    int r_in = (r2 > 3) ? (r2 - 3) : r2;
    int r2_sq = r2 * r2;
    int r_in_sq = r_in * r_in;
    int arm_len_2 = (diam * 42) / 100; /* arm half-length in 2x space */
    int arm_thk_2 = (diam >= 46) ? 5 : 3;

    int x0 = cx - diam / 2 - 1;
    int x1 = cx + diam / 2 + 2;
    int y0 = cy - diam / 2 - 1;
    int y1 = cy + diam / 2 + 2;
    if (x0 < 0) x0 = 0;
    if (x1 > NL_SCREEN_W) x1 = NL_SCREEN_W;
    if (y0 < y_min) y0 = y_min;
    if (y1 > y_max) y1 = y_max;
    if (y0 < 0) y0 = 0;
    if (y1 > NL_SCREEN_H) y1 = NL_SCREEN_H;

    for (int py = y0; py < y1; py++) {
        int dy2 = (py - cy) * 2;
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = x0; px < x1; px++) {
            int dx2 = (px - cx) * 2;
            int d2 = dx2 * dx2 + dy2 * dy2;
            if (d2 > r2_sq) continue;

            uint16_t c = (d2 >= r_in_sq) ? ring_col : bg_col;
            /* Rotated '+' cross arms in 2x coordinates */
            int rx = (dx2 * cos_v + dy2 * sin_v) >> 8;
            int ry = (-dx2 * sin_v + dy2 * cos_v) >> 8;
            if ((abs(rx) <= arm_thk_2 && abs(ry) <= arm_len_2) ||
                (abs(ry) <= arm_thk_2 && abs(rx) <= arm_len_2)) {
                c = sym_col;
            }

            row[px] = (alpha_32 >= 32U) ? c : nl_rgb565_lerp(row[px], c, alpha_32);
        }
    }
}

/* Continuous animated multicolor focus ring/halo, adapting cleanly to Dark/Gray vs Light themes (independent of accent_color) */
static void draw_multicolor_focus_ring(
    uint16_t *fb,
    int cx,
    int cy,
    int inner_d,
    int ring_thick,
    uint16_t phase,
    bool is_light,
    unsigned master_alpha_32,
    int y_min,
    int y_max
) {
    if (master_alpha_32 == 0U || inner_d <= 0) return;

    static const uint16_t k_dark_stops[6] = {
        RGB565(56, 232, 255),  /* Neon cyan */
        RGB565(59, 130, 246),  /* Electric sky */
        RGB565(168, 85, 247),  /* Luminous violet */
        RGB565(244, 63, 168),  /* Hot magenta */
        RGB565(250, 204, 21),  /* Vivid gold */
        RGB565(52, 211, 153)   /* Bright mint */
    };
    static const uint16_t k_light_stops[6] = {
        RGB565(29, 78, 216),   /* Deep cobalt */
        RGB565(126, 34, 206),  /* Royal purple */
        RGB565(225, 29, 72),   /* Vivid crimson */
        RGB565(217, 119, 6),   /* Burnt amber */
        RGB565(5, 150, 105),   /* Deep emerald */
        RGB565(14, 116, 144)   /* Rich teal */
    };
    const uint16_t *stops = is_light ? k_light_stops : k_dark_stops;

    int pad = ring_thick + 3;
    int r_in_2 = inner_d;                   /* diameter in 2x space = inner radius * 2 */
    int r_core_2 = inner_d + ring_thick * 2;
    int r_halo_2 = r_core_2 + 6;            /* +3px soft outer aura */

    int x0 = cx - (inner_d / 2) - pad;
    int y0 = cy - (inner_d / 2) - pad;
    int x1 = cx + (inner_d / 2) + pad + 1;
    int y1 = cy + (inner_d / 2) + pad + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > NL_SCREEN_W) x1 = NL_SCREEN_W;
    if (y0 < y_min) y0 = y_min;
    if (y1 > y_max) y1 = y_max;
    if (y0 < 0) y0 = 0;
    if (y1 > NL_SCREEN_H) y1 = NL_SCREEN_H;

    int cx2 = cx * 2;
    int cy2 = cy * 2;

    for (int py = y0; py < y1; py++) {
        int dy2 = (py * 2 + 1) - cy2;
        int dy2_sq = dy2 * dy2;
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = x0; px < x1; px++) {
            int dx2 = (px * 2 + 1) - cx2;
            int r2 = ink_isqrt32(dx2 * dx2 + dy2_sq);
            if (r2 < r_in_2 - 1 || r2 > r_halo_2) continue;

            unsigned rad_alpha = 0U;
            if (r2 < r_in_2) {
                rad_alpha = 18U;
            } else if (r2 <= r_core_2) {
                rad_alpha = 32U;
            } else {
                int over = r2 - r_core_2;
                int span = r_halo_2 - r_core_2;
                rad_alpha = (unsigned)(((span - over) * 18) / (span > 0 ? span : 1));
            }
            unsigned a = (rad_alpha * master_alpha_32) >> 5;
            if (a == 0U) continue;

            int ang = (ink_atan2_256(dx2, dy2) + (int)phase) & 255;
            int scaled = ang * 6;
            int s0 = scaled >> 8;
            int s1 = (s0 + 1) % 6;
            unsigned f = (unsigned)((scaled & 255) >> 3);
            uint16_t ring_col = nl_rgb565_lerp(stops[s0], stops[s1], f);
            row[px] = (a >= 32U) ? ring_col : nl_rgb565_lerp(row[px], ring_col, a);
        }
    }
}

static void draw_centered_shortcuts_list_at_y(uint16_t *fb, int bar_y, const char *const *labels, int n_items, const nl_theme_t *th) {
    if (bar_y >= NL_SCREEN_H) return;
    const int gap = 12;
    int total_w = 0;
    int item_w[6];
    if (n_items > 6) n_items = 6;
    for (int i = 0; i < n_items; i++) {
        item_w[i] = (int)strlen(labels[i]) * 6;
        total_w += item_w[i];
    }
    if (n_items > 1) total_w += gap * (n_items - 1);

    int x = (NL_SCREEN_W - total_w) / 2;
    int y = bar_y + 6;
    for (int i = 0; i < n_items; i++) {
        const char *sp = strchr(labels[i], ' ');
        int klen = sp ? (int)(sp - labels[i]) : (int)strlen(labels[i]);
        fb_draw_text(fb, x, y, labels[i], th->accent_secondary, klen);
        if (sp) {
            fb_draw_text(fb, x + klen * 6, y, sp, th->text_secondary, 20);
        }
        x += item_w[i] + gap;
    }
}

static void draw_centered_shortcuts_list(uint16_t *fb, const char *const *labels, int n_items, const nl_theme_t *th) {
    draw_centered_shortcuts_list_at_y(fb, NL_BOTTOM_BAR_Y, labels, n_items, th);
}

static void draw_centered_shortcuts_at_y(uint16_t *fb, int bar_y, const nl_theme_t *th) {
    static const char *const labels[4] = {
        "EXE/5 Jouer",
        "TAB Cat.",
        "VAR Fav.",
        "ESC Quitter"
    };
    draw_centered_shortcuts_list_at_y(fb, bar_y, labels, 4, th);
}

static void lerp_layout_metrics_pair(const nl_layout_metrics_t *a, const nl_layout_metrics_t *b, int fp, nl_layout_metrics_t *out) {
    if (fp <= 0) {
        *out = *a;
        return;
    }
    if (fp >= 256) {
        *out = *b;
        return;
    }
    *out = *b;
    out->card_w       = a->card_w       + (((b->card_w       - a->card_w)       * fp + 128) >> 8);
    out->card_h       = a->card_h       + (((b->card_h       - a->card_h)       * fp + 128) >> 8);
    out->radius       = a->radius       + (((b->radius       - a->radius)       * fp + 128) >> 8);
    out->gap_x        = a->gap_x        + (((b->gap_x        - a->gap_x)        * fp + 128) >> 8);
    out->gap_y        = a->gap_y        + (((b->gap_y        - a->gap_y)        * fp + 128) >> 8);
    out->margin_x     = a->margin_x     + (((b->margin_x     - a->margin_x)     * fp + 128) >> 8);
    out->top_y        = a->top_y        + (((b->top_y        - a->top_y)        * fp + 128) >> 8);
    out->label_h      = a->label_h      + (((b->label_h      - a->label_h)      * fp + 128) >> 8);
    out->row_stride_y = a->row_stride_y + (((b->row_stride_y - a->row_stride_y) * fp + 128) >> 8);
    out->page_stride_y= a->page_stride_y+ (((b->page_stride_y- a->page_stride_y)* fp + 128) >> 8);
    out->view_bottom  = a->view_bottom  + (((b->view_bottom  - a->view_bottom)  * fp + 128) >> 8);
    out->pagination_y = a->pagination_y + (((b->pagination_y - a->pagination_y) * fp + 128) >> 8);
}

static void get_layout_metrics_2d(nl_layout_t layout, int names_fp, int sc_fp, int item_count, nl_layout_metrics_t *out) {
    nl_layout_metrics_t m_n0_s0, m_n0_s1, m_n1_s0, m_n1_s1;
    nl_get_layout_metrics_for_count(layout, false, false, item_count, &m_n0_s0);
    nl_get_layout_metrics_for_count(layout, false, true,  item_count, &m_n0_s1);
    nl_get_layout_metrics_for_count(layout, true,  false, item_count, &m_n1_s0);
    nl_get_layout_metrics_for_count(layout, true,  true,  item_count, &m_n1_s1);

    nl_layout_metrics_t m_n0, m_n1;
    lerp_layout_metrics_pair(&m_n0_s0, &m_n0_s1, sc_fp, &m_n0);
    lerp_layout_metrics_pair(&m_n1_s0, &m_n1_s1, sc_fp, &m_n1);
    lerp_layout_metrics_pair(&m_n0, &m_n1, names_fp, out);
}

static void get_interpolated_layout_metrics(const nl_state_t *state, nl_layout_metrics_t *out) {
    if (!state || !out) return;
    int sc_fp = state->anim.switch_shortcuts_fp;
    int nm_fp = state->anim.switch_shownames_fp;
    int lt_fp = state->anim.layout_trans_fp;

    /* If show_names was set directly in a test while switch_shownames_fp was not stepped, honor settings.show_names */
    if (state->sui.screen == NL_UI_LIBRARY && ((state->settings.show_names && nm_fp == 0) || (!state->settings.show_names && nm_fp == 256))) {
        nm_fp = state->settings.show_names ? 256 : 0;
    }

    nl_layout_metrics_t m_cur;
    get_layout_metrics_2d(state->settings.layout, nm_fp, sc_fp, state->filtered_count, &m_cur);
    if (lt_fp >= 256 || state->anim.layout_from == state->settings.layout) {
        *out = m_cur;
        return;
    }
    nl_layout_metrics_t m_prev;
    get_layout_metrics_2d(state->anim.layout_from, nm_fp, sc_fp, state->filtered_count, &m_prev);
    lerp_layout_metrics_pair(&m_prev, &m_cur, lt_fp, out);
}

static uint16_t G_TOAST_SCRATCH[NL_SCREEN_W * 24];

static void draw_toast_banner(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    if (state->anim.toast_ticks > 0 && state->anim.toast_text[0] != '\0') {
        int ticks = state->anim.toast_ticks;
        int max_t = state->anim.toast_max_ticks > 0 ? state->anim.toast_max_ticks : 75;
        int age = max_t - ticks;

        int slide_y = 29;
        unsigned alpha = 32U;

        if (age < 5 && max_t > 12) {
            slide_y -= (5 - age) * 2;
            alpha = (unsigned)(12 + age * 4);
        } else if (ticks < 8) {
            slide_y -= (8 - ticks) * 2;
            alpha = (unsigned)(ticks * 4);
        }
        if (alpha > 32U) alpha = 32U;

        int msg_len = (int)strlen(state->anim.toast_text);
        int bw = msg_len * 6 + 20;
        if (bw > 292) bw = 292;
        int bx = (NL_SCREEN_W - bw) / 2;

        if (alpha >= 32U) {
            fb_fill_rounded_rect_clipped(fb, bx - 1, slide_y - 1, bw + 2, 19, 6, th->border_subtle, 0, NL_SCREEN_H);
            fb_fill_rounded_rect_clipped(fb, bx, slide_y, bw, 17, 5, th->bg_top_bar, 0, NL_SCREEN_H);
            fb_draw_text_ellipsis(fb, bx + 10, slide_y + 5, state->anim.toast_text, th->accent_secondary, 44, 0, NL_SCREEN_H);
        } else if (alpha > 0U) {
            int y0 = (slide_y - 1 < 0) ? 0 : (slide_y - 1);
            int y1 = (slide_y + 18 > NL_SCREEN_H) ? NL_SCREEN_H : (slide_y + 18);
            int h = y1 - y0;
            if (h > 0 && h <= 24) {
                memcpy(G_TOAST_SCRATCH, fb + y0 * NL_SCREEN_W, (size_t)(h * NL_SCREEN_W) * sizeof(uint16_t));
                fb_fill_rounded_rect_clipped(fb, bx - 1, slide_y - 1, bw + 2, 19, 6, th->border_subtle, y0, y1);
                fb_fill_rounded_rect_clipped(fb, bx, slide_y, bw, 17, 5, th->bg_top_bar, y0, y1);
                fb_draw_text_ellipsis(fb, bx + 10, slide_y + 5, state->anim.toast_text, th->accent_secondary, 44, y0, y1);
                for (int r = 0; r < h; r++) {
                    uint16_t *dst = fb + (y0 + r) * NL_SCREEN_W;
                    const uint16_t *old_r = G_TOAST_SCRATCH + r * NL_SCREEN_W;
                    for (int x = bx - 1; x < bx + bw + 1; x++) {
                        if ((unsigned)x < NL_SCREEN_W && dst[x] != old_r[x]) {
                            dst[x] = nl_rgb565_lerp(old_r[x], dst[x], alpha);
                        }
                    }
                }
            }
        }
    }
}

/* =========================================================================
 * SETTINGS SCREENS RENDERING (§1 – §12) WITH FLUID ANIMATIONS (A – G)
 * ========================================================================= */

static uint16_t G_SET_BUF_A[NL_FB_PIXELS];
static uint16_t G_SET_BUF_B[NL_FB_PIXELS];

typedef enum {
    NL_ICO_POWER = 0,
    NL_ICO_SCREEN,
    NL_ICO_TOUCHPAD,
    NL_ICO_LAUNCH_KEY,
    NL_ICO_KEYBOARD,
    NL_ICO_SORT,
    NL_ICO_STATS,
    NL_ICO_SEARCH,
    NL_ICO_RESET,
    NL_ICO_INFO,
    NL_ICO_CREDITS,
    NL_ICO_GRID,
    NL_ICO_TAG,
    NL_ICO_BOTTOMBAR,
    NL_ICO_BATTERY,
    NL_ICO_REFRESH,
    NL_ICO_CAPTURE,
    NL_ICO_ICONS,
    NL_ICO_HOTKEY,
    NL_ICO_PROFILES,
    NL_ICO_COUNT
} nl_set_icon_id_t;

static const uint16_t k_set_icons_11x11[NL_ICO_COUNT][11] = {
    [NL_ICO_POWER] = {
        0x020, 0x020, 0x1AC, 0x326, 0x222, 0x222, 0x202, 0x306, 0x18C, 0x0F8, 0x000
    },
    [NL_ICO_SCREEN] = {
        0x000, 0x3FE, 0x202, 0x2E2, 0x2A2, 0x202, 0x202, 0x3FE, 0x070, 0x1FC, 0x000
    },
    [NL_ICO_TOUCHPAD] = {
        0x1FC, 0x202, 0x222, 0x272, 0x222, 0x2F2, 0x222, 0x3FE, 0x222, 0x222, 0x1FC
    },
    [NL_ICO_LAUNCH_KEY] = {
        0x1FC, 0x202, 0x262, 0x272, 0x27A, 0x272, 0x262, 0x202, 0x3FE, 0x3FE, 0x000
    },
    [NL_ICO_KEYBOARD] = {
        0x000, 0x3FE, 0x202, 0x2AA, 0x202, 0x2AA, 0x202, 0x272, 0x202, 0x3FE, 0x000
    },
    [NL_ICO_SORT] = {
        0x000, 0x080, 0x09E, 0x080, 0x09C, 0x080, 0x298, 0x1C0, 0x090, 0x000, 0x000
    },
    [NL_ICO_STATS] = {
        0x000, 0x070, 0x070, 0x076, 0x076, 0x376, 0x376, 0x376, 0x376, 0x7FF, 0x000
    },
    [NL_ICO_SEARCH] = {
        0x000, 0x1E0, 0x210, 0x210, 0x210, 0x210, 0x1E8, 0x00C, 0x006, 0x002, 0x000
    },
    [NL_ICO_RESET] = {
        0x000, 0x178, 0x384, 0x1E2, 0x002, 0x002, 0x202, 0x104, 0x0F8, 0x000, 0x000
    },
    [NL_ICO_INFO] = {
        0x0F8, 0x104, 0x222, 0x202, 0x262, 0x222, 0x222, 0x272, 0x104, 0x0F8, 0x000
    },
    [NL_ICO_CREDITS] = {
        0x020, 0x070, 0x070, 0x1FC, 0x3FE, 0x1FC, 0x0F8, 0x1DC, 0x104, 0x000, 0x000
    },
    [NL_ICO_GRID] = {
        0x000, 0x3DE, 0x3DE, 0x3DE, 0x3DE, 0x000, 0x3DE, 0x3DE, 0x3DE, 0x3DE, 0x000
    },
    [NL_ICO_TAG] = {
        0x000, 0x1FC, 0x104, 0x104, 0x104, 0x1FC, 0x000, 0x3FE, 0x000, 0x0F8, 0x000
    },
    [NL_ICO_BOTTOMBAR] = {
        0x000, 0x3FE, 0x202, 0x202, 0x202, 0x202, 0x3FE, 0x2AA, 0x3FE, 0x000, 0x000
    },
    [NL_ICO_BATTERY] = {
        0x000, 0x000, 0x3FC, 0x204, 0x2D7, 0x2D7, 0x2D7, 0x204, 0x3FC, 0x000, 0x000
    },
    [NL_ICO_REFRESH] = {
        0x000, 0x0F8, 0x106, 0x20E, 0x200, 0x000, 0x002, 0x382, 0x304, 0x0F8, 0x000
    },
    [NL_ICO_CAPTURE] = {
        0x000, 0x0F8, 0x3FE, 0x202, 0x272, 0x2FA, 0x2FA, 0x272, 0x202, 0x3FE, 0x000
    },
    [NL_ICO_ICONS] = {
        0x000, 0x3FE, 0x202, 0x2C2, 0x2C2, 0x212, 0x23A, 0x2FE, 0x3FE, 0x000, 0x000
    },
    [NL_ICO_HOTKEY] = {
        0x018, 0x030, 0x060, 0x0FE, 0x1FC, 0x030, 0x060, 0x0C0, 0x180, 0x000, 0x000
    },
    [NL_ICO_PROFILES] = {
        0x000, 0x0E0, 0x1F0, 0x1F0, 0x0E0, 0x000, 0x1F0, 0x3F8, 0x3F8, 0x3F8, 0x000
    }
};

static void draw_settings_icon_11x11(
    uint16_t *fb,
    int x,
    int y,
    nl_set_icon_id_t icon_id,
    uint16_t fg_col,
    int y_min,
    int y_max
) {
    if ((unsigned)icon_id >= (unsigned)NL_ICO_COUNT) return;
    const uint16_t *rows = k_set_icons_11x11[icon_id];
    for (int r = 0; r < 11; r++) {
        int py = y + r;
        if (py < y_min || py >= y_max || (unsigned)py >= NL_SCREEN_H) continue;
        uint16_t bits = rows[r];
        for (int c = 0; c < 11; c++) {
            if ((bits >> (10 - c)) & 1U) {
                int px = x + c;
                if ((unsigned)px < NL_SCREEN_W) {
                    fb[py * NL_SCREEN_W + px] = fg_col;
                }
            }
        }
    }
}

static void draw_settings_icon_badge(
    uint16_t *fb,
    int x,
    int y,
    int size,
    nl_set_icon_id_t icon_id,
    unsigned row_alpha_32,
    bool is_danger,
    const nl_theme_t *th,
    int y_min,
    int y_max
) {
    uint16_t target_bg = is_danger ? RGB565(220, 38, 38) : th->accent_primary;
    uint16_t target_fg = is_danger ? RGB565(255, 255, 255) : th->text_on_accent;
    uint16_t base_bg = th->is_light ? th->bg_main : th->pill_bg;
    uint16_t base_fg = is_danger ? RGB565(248, 113, 113) : th->accent_secondary;

    uint16_t badge_bg = nl_rgb565_lerp(base_bg, target_bg, row_alpha_32);
    uint16_t icon_fg = nl_rgb565_lerp(base_fg, target_fg, row_alpha_32);

    fb_fill_rounded_rect_clipped(fb, x, y, size, size, 5, badge_bg, y_min, y_max);
    int gx = x + (size - 11) / 2;
    int gy = y + (size - 11) / 2;
    draw_settings_icon_11x11(fb, gx, gy, icon_id, icon_fg, y_min, y_max);
}

static void draw_settings_chrome(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    const char *title,
    int header_icon_id,
    const char *const *footer_hints,
    int footer_count
) {
    (void)state;
    fb_fill_rect_clipped(fb, 0, 0, NL_SCREEN_W, NL_SCREEN_H, th->bg_main, 0, NL_SCREEN_H);

    /* Top Header Bar */
    fb_fill_rect_clipped(fb, 0, 0, NL_SCREEN_W, NL_TOP_BAR_H, th->bg_top_bar, 0, NL_SCREEN_H);
    fb_fill_rect_clipped(fb, 0, NL_TOP_BAR_H, NL_SCREEN_W, 1, th->border_subtle, 0, NL_SCREEN_H);

    if (header_icon_id >= 0 && header_icon_id < NL_ICO_COUNT) {
        draw_settings_icon_badge(fb, 10, 4, 17, (nl_set_icon_id_t)header_icon_id, 24U,
                                 header_icon_id == (int)NL_ICO_RESET, th, 0, NL_TOP_BAR_H);
    } else {
        draw_gear_button(fb, 10, 5, 15, th, false);
    }
    fb_draw_text_ellipsis(fb, 32, 9, title, th->text_primary, 26, 0, NL_TOP_BAR_H);

    /* Discreet Power button right next to the Settings title (triggered by DEL or click) */
    {
        int t_len = (int)strlen(title);
        if (t_len > 26) t_len = 26;
        int pwr_x = 32 + t_len * 6 + 6;
        if (pwr_x + 17 <= 246) {
            draw_settings_icon_badge(fb, pwr_x, 4, 17, NL_ICO_POWER, 20U, false, th, 0, NL_TOP_BAR_H);
            if (pwr_x + 42 <= 246) {
                fb_fill_rounded_rect_clipped(fb, pwr_x + 19, 6, 22, 13, 3, th->pill_bg, 0, NL_TOP_BAR_H);
                fb_draw_text_clipped(fb, pwr_x + 21, 9, "DEL", th->text_secondary, 3, 0, NL_TOP_BAR_H);
            }
        }
    }

    /* Right ESC pill */
    fb_fill_rounded_rect_clipped(fb, 250, 5, 60, 15, 4, th->pill_bg, 0, NL_SCREEN_H);
    fb_draw_text(fb, 255, 9, "ESC", th->accent_secondary, 3);
    fb_draw_text(fb, 275, 9, "Ret.", th->text_secondary, 4);

    /* Bottom Footer Bar */
    fb_fill_rect_clipped(fb, 0, NL_BOTTOM_BAR_Y - 1, NL_SCREEN_W, 1, th->border_subtle, 0, NL_SCREEN_H);
    fb_fill_rect_clipped(fb, 0, NL_BOTTOM_BAR_Y, NL_SCREEN_W, NL_BOTTOM_BAR_H, th->bg_bottom_bar, 0, NL_SCREEN_H);
    draw_centered_shortcuts_list(fb, footer_hints, footer_count, th);
}

static void draw_scrollbar_v_smooth(uint16_t *fb, int y_top, int h, int scroll_px, int stride_px, int visible, int total, const nl_theme_t *th) {
    if (total <= visible || visible <= 0 || h <= 6 || stride_px <= 0) return;
    int track_x = NL_SCREEN_W - 6;
    fb_fill_rounded_rect_clipped(fb, track_x, y_top, 3, h, 1, th->pill_bg, y_top, y_top + h);
    int thumb_h = (h * visible) / total;
    if (thumb_h < 14) thumb_h = 14;
    int max_scroll_px = (total - visible) * stride_px;
    if (scroll_px < 0) scroll_px = 0;
    if (scroll_px > max_scroll_px) scroll_px = max_scroll_px;
    int thumb_y = y_top + ((h - thumb_h) * scroll_px) / (max_scroll_px > 0 ? max_scroll_px : 1);
    fb_fill_rounded_rect_clipped(fb, track_x, thumb_y, 3, thumb_h, 1, th->accent_primary, y_top, y_top + h);
}

static void draw_mini_grid_preview_single(uint16_t *fb, int px, int py, int pw, int ph,
                                          nl_layout_t layout, int names_fp, int sc_fp,
                                          unsigned grid_alpha_32, const nl_theme_t *th) {
    if (grid_alpha_32 == 0U) return;
    nl_layout_metrics_t m;
    nl_get_layout_metrics_ex(layout, names_fp >= 128, sc_fp >= 128, &m);

    int footer_reserve = 3 + ((7 * sc_fp + 128) >> 8);
    int inner_x = px + 10;
    int inner_y = py + 5;
    int inner_w = pw - 20;
    int inner_h = ph - 9 - footer_reserve;

    int gap_x = (m.cols > 1) ? 5 : 0;
    int gap_y = (m.rows > 1) ? 4 : 0;
    int max_cw = (inner_w - gap_x * (m.cols - 1)) / m.cols;
    int max_ch = (inner_h - gap_y * (m.rows - 1)) / m.rows;
    int name_deduct = (3 * names_fp + 128) >> 8;
    int side = max_ch - name_deduct;
    if (max_cw < side) side = max_cw;
    if (side < 4) side = 4;

    int grid_w = m.cols * side + (m.cols - 1) * gap_x;
    int grid_h = m.rows * (side + name_deduct) + (m.rows - 1) * gap_y;
    int start_x = inner_x + (inner_w - grid_w) / 2;
    int start_y = inner_y + (inner_h - grid_h) / 2;

    for (int r = 0; r < m.rows; r++) {
        for (int c = 0; c < m.cols; c++) {
            int cx = start_x + c * (side + gap_x);
            int cy = start_y + r * (side + name_deduct + gap_y);
            bool first = (r == 0 && c == 0);
            uint16_t c_col = first ? th->accent_primary : th->card_bg_sel;
            if (grid_alpha_32 < 32U) {
                c_col = nl_rgb565_lerp(th->bg_top_bar, c_col, grid_alpha_32);
            }
            fb_fill_rounded_rect_clipped(fb, cx, cy, side, side, (side >= 14) ? 3 : 2, c_col, py, py + ph);

            if (names_fp > 16 && side >= 6) {
                unsigned n_alpha = (unsigned)(((names_fp + 4) >> 3) * grid_alpha_32) >> 5;
                uint16_t l_col = nl_rgb565_lerp(th->bg_top_bar, first ? th->text_primary : th->text_secondary, n_alpha);
                int lw = (side > 4) ? (side - 4) : side;
                fb_fill_rect_clipped(fb, cx + (side - lw) / 2, cy + side + 1, lw, 2, l_col, py, py + ph);
            }
        }
    }
}

static void draw_mini_grid_preview(const nl_state_t *state, uint16_t *fb, int px, int py, int pw, int ph, const nl_theme_t *th) {
    fb_fill_rounded_rect_clipped(fb, px - 1, py - 1, pw + 2, ph + 2, 7, th->border_subtle, 0, NL_SCREEN_H);
    fb_fill_rounded_rect_clipped(fb, px, py, pw, ph, 6, th->bg_top_bar, 0, NL_SCREEN_H);

    int names_fp = state->anim.switch_shownames_fp;
    int sc_fp = state->anim.switch_shortcuts_fp;
    int lt_fp = state->anim.layout_trans_fp;

    if (lt_fp < 256 && state->anim.layout_from != state->settings.layout) {
        unsigned a_new = (unsigned)((lt_fp + 4) >> 3);
        unsigned a_old = 32U - a_new;
        draw_mini_grid_preview_single(fb, px, py, pw, ph, state->anim.layout_from, names_fp, sc_fp, a_old, th);
        draw_mini_grid_preview_single(fb, px, py, pw, ph, state->settings.layout, names_fp, sc_fp, a_new, th);
    } else {
        draw_mini_grid_preview_single(fb, px, py, pw, ph, state->settings.layout, names_fp, sc_fp, 32U, th);
    }

    if (sc_fp > 8) {
        unsigned sc_alpha = (unsigned)((sc_fp + 4) >> 3);
        int slide_off = ((256 - sc_fp) * 6 + 128) >> 8;
        int bar_y = py + ph - 7 + slide_off;
        if (bar_y < py + ph - 1) {
            uint16_t sep_col = nl_rgb565_lerp(th->bg_top_bar, th->border_subtle, sc_alpha);
            uint16_t k_col = nl_rgb565_lerp(th->bg_top_bar, th->accent_secondary, sc_alpha);
            uint16_t t_col = nl_rgb565_lerp(th->bg_top_bar, th->text_secondary, sc_alpha);
            fb_fill_rect_clipped(fb, px + 4, bar_y - 1, pw - 8, 1, sep_col, py, py + ph - 1);
            int pill_w = 24;
            int pill_gap = 8;
            int start_x = px + (pw - (4 * pill_w + 3 * pill_gap)) / 2;
            for (int i = 0; i < 4; i++) {
                int sx = start_x + i * (pill_w + pill_gap);
                fb_fill_rect_clipped(fb, sx, bar_y + 2, 8, 2, k_col, py, py + ph - 1);
                fb_fill_rect_clipped(fb, sx + 10, bar_y + 2, 12, 2, t_col, py, py + ph - 1);
            }
        }
    }
}

static void resolve_list_anim_metrics(
    const nl_state_t *state,
    bool is_active_scr,
    int row_y0,
    int stride,
    int cursor,
    int scroll,
    int *out_scroll_px,
    int *out_sel_y,
    unsigned *out_glow_alpha,
    int *out_prev_cursor
) {
    int expected_target_y_fp = (row_y0 + cursor * stride) << 4;
    int expected_scroll_fp = (scroll * stride) << 4;
    if (is_active_scr &&
        state->anim.set_target_sel_y_fp == expected_target_y_fp &&
        state->anim.set_target_scroll_fp == expected_scroll_fp) {
        *out_scroll_px = state->anim.set_list_scroll_fp >> 4;
        *out_sel_y = (state->anim.set_sel_y_fp - state->anim.set_list_scroll_fp) >> 4;
        unsigned ga = (unsigned)((state->anim.set_sel_glow_fp + 4) >> 3);
        if (ga < 6U) ga = 6U;
        if (ga > 32U) ga = 32U;
        *out_glow_alpha = ga;
        *out_prev_cursor = state->anim.set_prev_cursor;
    } else {
        *out_scroll_px = scroll * stride;
        *out_sel_y = row_y0 + (cursor - scroll) * stride;
        *out_glow_alpha = 32U;
        *out_prev_cursor = -1;
    }
}

/* Render the authentic PNG cursor sprite (Arrow icons8-select-cursor-24.png or Circle icons8-select-48.png)
 * tinted with the active theme accent color + crisp high-contrast edge halo for Nuit/Jour/Gris legibility. */
static void draw_cursor_alpha_sprite(
    uint16_t *fb,
    nl_cursor_style_t style,
    int hx,
    int hy,
    unsigned opacity_256,
    int clip_top,
    int clip_bot,
    const nl_theme_t *th
) {
    if (!fb || !th || opacity_256 == 0U) return;
    if (opacity_256 > 256U) opacity_256 = 256U;

    const uint8_t *mask = NULL;
    int hot_x = 0;
    int hot_y = 0;
    if (style == NL_CURSOR_STYLE_ARROW) {
        mask = G_CURSOR_ARROW_28;
        hot_x = NL_CURSOR_ARROW_28_HX;
        hot_y = NL_CURSOR_ARROW_28_HY;
    } else if (style == NL_CURSOR_STYLE_CIRCLE) {
        mask = G_CURSOR_CIRCLE_28;
        hot_x = NL_CURSOR_CIRCLE_28_HX;
        hot_y = NL_CURSOR_CIRCLE_28_HY;
    } else {
        return;
    }

    const int sw = NL_CURSOR_ARROW_28_W;
    const int sh = NL_CURSOR_ARROW_28_H;
    const int sx = hx - hot_x;
    const int sy = hy - hot_y;

    /* High-contrast outer halo color: white on Dark/Gray themes, deep slate on Light theme */
    const uint16_t halo_col = th->is_light ? RGB565(15, 23, 42) : RGB565(255, 255, 255);
    const uint16_t core_hi  = nl_rgb565_lerp(th->accent_secondary, RGB565(255, 255, 255), th->is_light ? 6U : 11U);
    const uint16_t core_lo  = th->accent_primary;

    for (int dy = -1; dy <= sh; dy++) {
        int py = sy + dy;
        if (py < clip_top || py >= clip_bot || py < 0 || py >= NL_SCREEN_H) continue;
        uint16_t *row = fb + py * NL_SCREEN_W;

        for (int dx = -1; dx <= sw; dx++) {
            int px = sx + dx;
            if (px < 0 || px >= NL_SCREEN_W) continue;

            uint8_t a8 = (dx >= 0 && dx < sw && dy >= 0 && dy < sh)
                ? mask[dy * sw + dx]
                : 0U;

            /* 1px anti-aliased contrast halo around the silhouette perimeter */
            if (a8 < 180U) {
                uint8_t max_nb = 0U;
                for (int ny = dy - 1; ny <= dy + 1; ny++) {
                    if (ny < 0 || ny >= sh) continue;
                    for (int nx = dx - 1; nx <= dx + 1; nx++) {
                        if (nx < 0 || nx >= sw) continue;
                        uint8_t na = mask[ny * sw + nx];
                        if (na > max_nb) max_nb = na;
                    }
                }
                if (max_nb > 48U) {
                    unsigned halo_a = ((unsigned)(max_nb - (a8 >> 1)) * opacity_256 * 18U) >> 16;
                    if (halo_a > 20U) halo_a = 20U;
                    if (halo_a > 0U) {
                        row[px] = nl_rgb565_lerp(row[px], halo_col, halo_a);
                    }
                }
            }

            if (a8 == 0U) continue;

            /* Smooth accent gradient from hotspot (core_hi) toward outer body (core_lo) */
            int dist_hot = abs(dx - hot_x) + abs(dy - hot_y);
            unsigned grad_t = (unsigned)((dist_hot * 32) / 28);
            if (grad_t > 32U) grad_t = 32U;
            uint16_t fill_col = nl_rgb565_lerp(core_hi, core_lo, grad_t);

            unsigned a32 = ((unsigned)a8 * opacity_256 * 32U + 32768U) >> 16;
            if (a32 > 32U) a32 = 32U;
            if (a32 > 0U) {
                row[px] = nl_rgb565_lerp(row[px], fill_col, a32);
            }
        }
    }
}

/* Render the optional touchpad graphical cursor + short 5-node luminous trail */
static void draw_touchpad_graphical_cursor(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th
) {
    if (!state || !fb || !th) return;
    if (state->anim.launch_anim_active) return;

    bool vis = nl_cursor_is_visible(state);
    int op_fp = state->anim.cur_opacity_fp;
    if (!vis && op_fp <= 0) return;
    if (vis && op_fp <= 0) op_fp = 256;
    if (op_fp > 256) op_fp = 256;

    nl_cursor_style_t style = state->settings.touchpad_cursor_style;
    if (style != NL_CURSOR_STYLE_ARROW && style != NL_CURSOR_STYLE_CIRCLE) {
        return;
    }

    int hx = 160;
    int hy = 120;
    if (state->anim.cur_initialized && (state->anim.cur_x_fp > 0 || state->anim.cur_y_fp > 0)) {
        hx = (state->anim.cur_x_fp + 8) >> 4;
        hy = (state->anim.cur_y_fp + 8) >> 4;
    } else {
        nl_cursor_compute_target(state, &hx, &hy);
    }

    /* 1. Short, discrete 5-node luminous trail behind the moving cursor */
    uint16_t trail_core = nl_rgb565_lerp(th->accent_secondary, RGB565(255, 255, 255), th->is_light ? 4U : 10U);
    uint16_t trail_glow = th->accent_primary;
    for (int i = NL_CURSOR_TRAIL_LEN - 1; i >= 0; i--) {
        uint8_t ta8 = state->anim.cur_trail_alpha[i];
        if (ta8 == 0U) continue;
        int tx = (state->anim.cur_trail_x_fp[i] + 8) >> 4;
        int ty = (state->anim.cur_trail_y_fp[i] + 8) >> 4;
        if (abs(tx - hx) <= 1 && abs(ty - hy) <= 1) continue;

        /* Subtle ghost silhouette for the closest two wake nodes */
        if (i <= 1) {
            unsigned ghost_op = ((unsigned)ta8 * (unsigned)op_fp) >> (i == 0 ? 10 : 11);
            if (ghost_op >= 12U) {
                draw_cursor_alpha_sprite(fb, style, tx, ty, ghost_op, 0, NL_SCREEN_H, th);
            }
        }

        /* Soft luminous particle wake at the hotspot trajectory */
        int rad = (i == 0) ? 4 : ((i <= 2) ? 3 : 2);
        unsigned pa32 = ((unsigned)ta8 * (unsigned)op_fp * 18U) >> 16;
        if (pa32 > 20U) pa32 = 20U;
        if (pa32 == 0U) continue;

        uint16_t pcol = (i <= 1) ? trail_core : trail_glow;
        for (int dy = -rad; dy <= rad; dy++) {
            int py = ty + dy;
            if (py < 0 || py >= NL_SCREEN_H) continue;
            for (int dx = -rad; dx <= rad; dx++) {
                int px = tx + dx;
                if (px < 0 || px >= NL_SCREEN_W) continue;
                int d2 = dx * dx + dy * dy;
                if (d2 > rad * rad) continue;
                unsigned falloff = (unsigned)((rad * rad + 1 - d2) * (int)pa32) / (unsigned)(rad * rad + 1);
                if (falloff > 0U) {
                    fb[py * NL_SCREEN_W + px] = nl_rgb565_lerp(fb[py * NL_SCREEN_W + px], pcol, falloff);
                }
            }
        }
    }

    /* 2. Active graphical cursor sprite at (hx, hy) */
    draw_cursor_alpha_sprite(fb, style, hx, hy, (unsigned)op_fp, 0, NL_SCREEN_H, th);
}

static void render_settings_single_screen(
    const nl_state_t *state,
    nl_ui_screen_t scr,
    bool is_active_scr,
    uint16_t *fb,
    const nl_theme_t *th
) {
    const nl_settings_ui_t *sui = &state->sui;

    switch (scr) {
        case NL_UI_SETTINGS_MAIN: {
            static const char *const hints[3] = {"FLECHES Nav.", "EXE Ouvrir", "ESC Fermer"};
            draw_settings_chrome(state, fb, th, "PARAMETRES", -1, hints, 3);

            static const char *const titles[NL_SET_MENU_COUNT] = {
                "Affichage",
                "Navigation tactile",
                "Touches de lancement",
                "Commandes du launcher",
                "Tri de la bibliotheque",
                "Statistiques de jeu",
                "Generer captures d'ecran",
                "Gestion des icones",
                "Actualiser la bibliotheque",
                "Raccourci global TI-OS",
                "Profils utilisateurs",
                "Reinitialiser les reglages",
                "Informations systeme",
                "A propos et credits"
            };

            static const char *const subtitles[NL_SET_MENU_COUNT] = {
                "Grille, titres et barre d'aide",
                "Deplacer le curseur au doigt",
                "Touche directe vers un jeu",
                "Touches et actions systeme",
                "Ordre d'affichage des jeux",
                "Temps de jeu et sessions",
                "Capturer les jeux automatiquement",
                "Voir ou supprimer une icone",
                "Rescanner jeux et reconstruire cache",
                "Ouvrir N Launcher via Ctrl+Menu",
                "Comptes, avatars et donnees",
                "Valeurs par defaut sans perte",
                "Version, TI-OS et Ndless",
                "Createur et projet N Launcher"
            };

            static const nl_set_icon_id_t menu_icons[NL_SET_MENU_COUNT] = {
                NL_ICO_SCREEN,
                NL_ICO_TOUCHPAD,
                NL_ICO_LAUNCH_KEY,
                NL_ICO_KEYBOARD,
                NL_ICO_SORT,
                NL_ICO_STATS,
                NL_ICO_CAPTURE,
                NL_ICO_ICONS,
                NL_ICO_REFRESH,
                NL_ICO_HOTKEY,
                NL_ICO_PROFILES,
                NL_ICO_RESET,
                NL_ICO_INFO,
                NL_ICO_CREDITS
            };

            const int vis = 6;
            const int row_y0 = 32;
            const int row_h = 28;
            const int row_gap = 2;
            const int stride = row_h + row_gap;

            int scroll_px, sel_y, prev_cur;
            unsigned glow_alpha;
            resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                      sui->menu_cursor, sui->menu_scroll,
                                      &scroll_px, &sel_y, &glow_alpha, &prev_cur);

            /* Gliding selection highlight border behind active option */
            uint16_t border_col = nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha);
            fb_fill_rounded_rect_clipped(fb, 11, sel_y - 1, 294, row_h + 2, 7, border_col, 28, 218);

            for (int idx = 0; idx < NL_SET_MENU_COUNT; idx++) {
                int ry = row_y0 + idx * stride - scroll_px;
                if (ry + row_h <= 28 || ry >= 218) continue;

                bool sel = (idx == sui->menu_cursor);
                bool is_prev = (!sel && idx == prev_cur && glow_alpha < 32U);
                unsigned row_alpha = sel ? glow_alpha : (is_prev ? (32U - glow_alpha) : 0U);
                bool is_danger = (idx == NL_SET_MENU_RESET);

                uint16_t bg_col = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, row_alpha);
                fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6, bg_col, 28, 218);

                /* Left cohesive category icon badge (18x18 rounded square with 11x11 glyph) */
                draw_settings_icon_badge(fb, 17, ry + 5, 18, menu_icons[idx],
                                         row_alpha, is_danger, th, 28, 218);

                /* Compact right-side value pill */
                char val_buf[32] = "";
                if (idx == NL_SET_MENU_DISPLAY) {
                    snprintf(val_buf, sizeof(val_buf), "%s", nl_layout_label(state->settings.layout));
                } else if (idx == NL_SET_MENU_TOUCHPAD) {
                    snprintf(val_buf, sizeof(val_buf), "%s", state->settings.touchpad_nav ? "ON" : "OFF");
                } else if (idx == NL_SET_MENU_SHORTCUTS) {
                    int assigned = 0;
                    for (int g = 0; g < state->total_games; g++) {
                        if (state->games[g].shortcut_key != 0) assigned++;
                    }
                    if (assigned > 0) {
                        snprintf(val_buf, sizeof(val_buf), "%d", assigned);
                    }
                } else if (idx == NL_SET_MENU_SORT) {
                    snprintf(val_buf, sizeof(val_buf), "%s",
                             (state->settings.sort_order == NL_SORT_ALPHA_ASC) ? "A-Z" :
                             (state->settings.sort_order == NL_SORT_ALPHA_DESC) ? "Z-A" :
                             (state->settings.sort_order == NL_SORT_RECENT_ADD) ? "RECENT" :
                             (state->settings.sort_order == NL_SORT_OLDEST_ADD) ? "ANCIEN" :
                             (state->settings.sort_order == NL_SORT_LAST_USED) ? "JOUE" :
                             (state->settings.sort_order == NL_SORT_MOST_LAUNCHED) ? "TOP" : "TEMPS");
                } else if (idx == NL_SET_MENU_CAPTURE_BATCH) {
                    int scr_cnt = 0;
                    for (int g = 0; g < state->total_games; g++) {
                        if (state->games[g].has_screenshot) scr_cnt++;
                    }
                    snprintf(val_buf, sizeof(val_buf), "%d/%d", scr_cnt, state->total_games);
                } else if (idx == NL_SET_MENU_ICONS) {
                    int ico_cnt = 0;
                    for (int g = 0; g < state->total_games; g++) {
                        if (state->games[g].icon_source != NL_ICON_NONE) ico_cnt++;
                    }
                    snprintf(val_buf, sizeof(val_buf), "%d", ico_cnt);
                } else if (idx == NL_SET_MENU_REFRESH_LIB) {
                    snprintf(val_buf, sizeof(val_buf), "%d", state->total_games);
                } else if (idx == NL_SET_MENU_HOTKEY) {
                    snprintf(val_buf, sizeof(val_buf), "%s", state->settings.global_hotkey ? "ON" : "OFF");
                } else if (idx == NL_SET_MENU_PROFILES) {
                    const nl_profile_t *ap = nl_profile_get_active(state);
                    snprintf(val_buf, sizeof(val_buf), "%.*s", 9, ap ? ap->name : "Profil 1");
                } else if (idx == NL_SET_MENU_INFO) {
                    snprintf(val_buf, sizeof(val_buf), "v%s", NL_LAUNCHER_VERSION_STR);
                }

                int max_txt_chars = (val_buf[0] != '\0') ? 31 : 38;
                uint16_t txt_col = (is_danger && sel)
                    ? nl_rgb565_lerp(th->text_primary, RGB565(248, 113, 113), glow_alpha)
                    : th->text_primary;
                uint16_t sub_col = nl_rgb565_lerp(th->text_secondary,
                                                   is_danger ? RGB565(248, 113, 113) : th->accent_secondary,
                                                   row_alpha >> 1);

                fb_draw_text_clipped(fb, 41, ry + 5, titles[idx], txt_col, max_txt_chars, 28, 218);
                fb_draw_text_clipped(fb, 41, ry + 16, subtitles[idx], sub_col, max_txt_chars, 28, 218);

                if (val_buf[0] != '\0') {
                    int vw = (int)strlen(val_buf) * 6;
                    int pill_w = vw + 8;
                    int pill_x = 286 - pill_w;
                    uint16_t pill_bg = nl_rgb565_lerp(th->pill_bg, th->bg_top_bar, row_alpha >> 1);
                    uint16_t val_col = nl_rgb565_lerp(th->text_secondary, th->accent_secondary, row_alpha);
                    fb_fill_rounded_rect_clipped(fb, pill_x, ry + 7, pill_w, 14, 4, pill_bg, 28, 218);
                    fb_draw_text_clipped(fb, pill_x + 4, ry + 11, val_buf, val_col, 10, 28, 218);
                }
                uint16_t chev_col = nl_rgb565_lerp(th->text_secondary, th->accent_primary, row_alpha);
                fb_draw_text_clipped(fb, 291, ry + 11, ">", chev_col, 1, 28, 218);
            }
            draw_scrollbar_v_smooth(fb, row_y0, vis * stride - row_gap,
                                    scroll_px, stride, vis, NL_SET_MENU_COUNT, th);
            break;
        }

        case NL_UI_SETTINGS_AUTOSTART: {
            static const char *const hints[3] = {"EXE Activer/Des.", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "DEMARRAGE AUTOMATIQUE", NL_ICO_POWER, hints, 2);

            unsigned sw_alpha = (unsigned)((state->anim.switch_autostart_fp + 4) >> 3);
            uint16_t top_border = nl_rgb565_lerp(th->card_bg_sel, th->accent_primary, sw_alpha);
            fb_fill_rounded_rect_clipped(fb, 14, 34, 292, 54, 7, top_border, 26, 220);
            fb_fill_rounded_rect_clipped(fb, 15, 35, 290, 52, 6, th->card_bg, 26, 220);

            draw_settings_icon_badge(fb, 22, 49, 22, NL_ICO_POWER, sw_alpha, false, th, 26, 220);
            fb_draw_text(fb, 50, 45, "Ouvrir N Launcher a l'allumage", th->text_primary, 32);
            fb_draw_text(fb, 50, 59,
                         state->settings.autostart ? "Actif via ndless/startup" : "Demarrage manuel uniquement",
                         nl_rgb565_lerp(th->text_secondary, th->accent_secondary, sw_alpha), 32);
            draw_toggle_switch(fb, 262, 52, state->anim.switch_autostart_fp, th);

            /* System pre-flight diagnostics card */
            fb_fill_rounded_rect_clipped(fb, 14, 96, 292, 114, 7, th->card_bg, 26, 220);

            fb_draw_text(fb, 24, 105, "Ndless :", th->text_secondary, 10);
            fb_draw_text(fb, 104, 105,
                         state->sys.has_ndless ? (state->sys.ndless_persistent ? "Persistant (OK)" : "Non persistant") : "Absent",
                         state->sys.ndless_persistent ? RGB565(74, 222, 128) : RGB565(248, 113, 113), 26);

            fb_draw_text(fb, 24, 120, "Bootloader :", th->text_secondary, 14);
            fb_draw_text(fb, 104, 120,
                         state->sys.autostart_active ? "nl_boot.tns pret" : "Aucun hook actif",
                         state->sys.autostart_active ? th->accent_secondary : th->text_secondary, 26);

            if (state->sys.boot_recovery_triggered && !state->settings.autostart) {
                const char *r_label = "Anomalie au demarrage";
                if (strcmp(state->sys.boot_recovery_reason, "crash_detected") == 0) {
                    r_label = "Crash au demarrage";
                } else if (strcmp(state->sys.boot_recovery_reason, "manual_key") == 0) {
                    r_label = "Touche ESC/ON maintenue";
                } else if (strcmp(state->sys.boot_recovery_reason, "missing_target") == 0) {
                    r_label = "N-Launcher.tns introuvable";
                } else if (strcmp(state->sys.boot_recovery_reason, "exec_failed") == 0) {
                    r_label = "Echec execution .tns";
                }
                fb_draw_text(fb, 24, 138, "Mode securise :", RGB565(251, 191, 36), 16);
                fb_draw_text(fb, 122, 138, r_label, th->text_primary, 28);
                fb_draw_text(fb, 24, 154, "Demarrage auto coupe automatiquement.", th->text_secondary, 40);
                fb_draw_text(fb, 24, 168, "Jeux, favoris et reglages intacts.", RGB565(74, 222, 128), 40);
                fb_draw_text(fb, 24, 184, "Appuyez sur EXE pour reactiver.", th->accent_secondary, 38);
            } else {
                fb_draw_text(fb, 24, 140, "Securite demarrage :", th->text_primary, 24);
                fb_draw_text(fb, 24, 155, "- Maintenir ESC ou ON au boot", th->text_secondary, 36);
                fb_draw_text(fb, 24, 168, "  pour demarrer l'OS normalement.", th->text_secondary, 36);
                fb_draw_text(fb, 24, 182, "- Garde anti-crash incluse.", th->text_secondary, 36);

                if (state->sys.autostart_diag[0] != '\0') {
                    fb_draw_text_ellipsis(fb, 24, 196, state->sys.autostart_diag, th->accent_secondary, 44, 26, 218);
                }
            }
            break;
        }

        case NL_UI_SETTINGS_HOTKEY: {
            static const char *const hints[2] = {"EXE Activer/Des.", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "RACCOURCI GLOBAL TI-OS", NL_ICO_HOTKEY, hints, 2);

            int hk_fp = state->anim.switch_hotkey_fp;
            if ((state->settings.global_hotkey && hk_fp == 0) || (!state->settings.global_hotkey && hk_fp == 256)) {
                hk_fp = state->settings.global_hotkey ? 256 : 0;
            }
            unsigned sw_alpha = (unsigned)((hk_fp + 4) >> 3);
            uint16_t top_border = nl_rgb565_lerp(th->card_bg_sel, th->accent_primary, sw_alpha);
            fb_fill_rounded_rect_clipped(fb, 14, 34, 292, 54, 7, top_border, 26, 220);
            fb_fill_rounded_rect_clipped(fb, 15, 35, 290, 52, 6, th->card_bg, 26, 220);

            draw_settings_icon_badge(fb, 22, 49, 22, NL_ICO_HOTKEY, sw_alpha, false, th, 26, 220);
            fb_draw_text(fb, 50, 45, "Ouvrir depuis TI-OS (Ctrl + Menu)", th->text_primary, 34);
            fb_draw_text(fb, 50, 59,
                         state->settings.global_hotkey
                             ? "Actif via ndless/startup/nl_hotkey.tns"
                             : "Desactive par defaut (securise)",
                         nl_rgb565_lerp(th->text_secondary, th->accent_secondary, sw_alpha), 38);
            draw_toggle_switch(fb, 262, 52, hk_fp, th);

            fb_fill_rounded_rect_clipped(fb, 14, 96, 292, 114, 7, th->card_bg, 26, 220);

            fb_draw_text(fb, 24, 105, "Combinaison :", th->text_secondary, 14);
            fb_draw_text(fb, 110, 105, "Ctrl + Menu (menus TI-OS)", th->accent_secondary, 28);

            fb_draw_text(fb, 24, 120, "Module :", th->text_secondary, 10);
            fb_draw_text(fb, 110, 120,
                         state->sys.hotkey_active ? "nl_hotkey.tns installe" : "Aucun hook installe",
                         state->sys.hotkey_active ? RGB565(74, 222, 128) : th->text_secondary, 28);

            fb_draw_text(fb, 24, 138, "Protections materielles incluses :", th->text_primary, 36);
            fb_draw_text(fb, 24, 152, "- Bloque automatiquement en plein jeu", th->text_secondary, 40);
            fb_draw_text(fb, 24, 165, "  et quand N Launcher est deja ouvert.", th->text_secondary, 40);
            fb_draw_text(fb, 24, 179, "- Maintenir ESC ou ON au demarrage", th->text_secondary, 40);
            fb_draw_text(fb, 24, 192, "  desactive aussi ce module.", RGB565(74, 222, 128), 38);
            break;
        }

        case NL_UI_SETTINGS_DISPLAY: {
            static const char *const hints[3] = {"UP/DN Option", "L/R/EXE Modif.", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "AFFICHAGE", NL_ICO_SCREEN, hints, 3);

            static const nl_set_icon_id_t disp_icons[NL_DISPLAY_OPT_COUNT] = {
                NL_ICO_GRID,
                NL_ICO_TAG,
                NL_ICO_BOTTOMBAR,
                NL_ICO_POWER
            };

            const int vis = 3;
            const int row_y0 = 30;
            const int row_h = 28;
            const int row_gap = 3;
            const int stride = row_h + row_gap;
            const int clip_top = 28;
            const int clip_bot = 122;

            int scroll_px, sel_y, prev_cur;
            unsigned glow_alpha;
            resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                      sui->display_cursor, sui->display_scroll,
                                      &scroll_px, &sel_y, &glow_alpha, &prev_cur);

            uint16_t border_col = nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha);
            fb_fill_rounded_rect_clipped(fb, 11, sel_y - 1, 294, row_h + 2, 7, border_col, clip_top, clip_bot);

            for (int r = 0; r < NL_DISPLAY_OPT_COUNT; r++) {
                int ry = row_y0 + r * stride - scroll_px;
                if (ry + row_h <= clip_top || ry >= clip_bot) continue;

                bool sel = (sui->display_cursor == r);
                bool is_prev = (!sel && r == prev_cur && glow_alpha < 32U);
                unsigned r_alpha = sel ? glow_alpha : (is_prev ? (32U - glow_alpha) : 0U);
                uint16_t r_bg = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha);
                fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6, r_bg, clip_top, clip_bot);

                draw_settings_icon_badge(fb, 17, ry + 5, 18, disp_icons[r], r_alpha, false, th, clip_top, clip_bot);

                if (r == 0) {
                    /* Row 0: Grid Layout + animated selector pill */
                    fb_draw_text_clipped(fb, 41, ry + 11, "Grille des jaquettes", th->text_primary, 24, clip_top, clip_bot);
                    char lay_buf[16];
                    snprintf(lay_buf, sizeof(lay_buf), "< %s >", nl_layout_label(state->settings.layout));
                    int lw = (int)strlen(lay_buf) * 6;
                    int pulse_dx = (sui->display_cursor == 0) ? ((state->anim.val_pulse_dir * state->anim.val_pulse_fp * 2) >> 8) : 0;
                    unsigned pulse_a = (sui->display_cursor == 0) ? (unsigned)((state->anim.val_pulse_fp * 10) >> 8) : 0U;
                    uint16_t pill_col = nl_rgb565_lerp(th->pill_bg, th->accent_primary, pulse_a);
                    fb_fill_rounded_rect_clipped(fb, 284 - lw - 10 + pulse_dx, ry + 6, lw + 12, 16, 4, pill_col, clip_top, clip_bot);
                    fb_draw_text_clipped(fb, 284 - lw - 4 + pulse_dx, ry + 11, lay_buf, th->accent_secondary, 14, clip_top, clip_bot);
                } else if (r == 1) {
                    /* Row 1: Show Game Names */
                    fb_draw_text_clipped(fb, 41, ry + 11, "Titres sous les jaquettes", th->text_primary, 28, clip_top, clip_bot);
                    if (ry >= clip_top - 2 && ry + row_h <= clip_bot + 2) {
                        draw_toggle_switch(fb, 260, ry + 7, state->anim.switch_shownames_fp, th);
                    }
                } else if (r == 2) {
                    /* Row 2: Show Bottom Shortcuts Bar */
                    fb_draw_text_clipped(fb, 41, ry + 11, "Barre d'aide en bas", th->text_primary, 28, clip_top, clip_bot);
                    if (ry >= clip_top - 2 && ry + row_h <= clip_bot + 2) {
                        draw_toggle_switch(fb, 260, ry + 7, state->anim.switch_shortcuts_fp, th);
                    }
                } else if (r == 3) {
                    /* Row 3: Power Off Calculator */
                    fb_draw_text_clipped(fb, 41, ry + 11, "Eteindre la calculatrice", th->text_primary, 26, clip_top, clip_bot);
                    uint16_t btn_col = sel ? th->accent_primary : th->pill_bg;
                    uint16_t txt_col = sel ? th->text_on_accent : th->accent_secondary;
                    fb_fill_rounded_rect_clipped(fb, 238, ry + 6, 54, 16, 4, btn_col, clip_top, clip_bot);
                    fb_draw_text_clipped(fb, 244, ry + 11, "DEL/EXE", txt_col, 7, clip_top, clip_bot);
                }
            }

            draw_scrollbar_v_smooth(fb, row_y0, vis * stride - row_gap,
                                    scroll_px, stride, vis, NL_DISPLAY_OPT_COUNT, th);

            /* Live Proportional Animated Mini Grid Preview */
            fb_draw_text(fb, 16, 125, "Apercu en direct :", th->text_secondary, 22);
            draw_mini_grid_preview(state, fb, 56, 136, 208, 78, th);
            break;
        }

        case NL_UI_SETTINGS_TOUCHPAD: {
            static const char *const hints_tab0[3] = {"UP/DN Option", "TAB Curseur", "ESC Retour"};
            static const char *const hints_tab1[3] = {"L/R Style", "EXE Choisir", "ESC/TAB Retour"};
            static const char *const sens_labels[NL_TP_SENS_COUNT] = {
                "1/5 Lente",
                "2/5 Douce",
                "3/5 Normale",
                "4/5 Sensible",
                "5/5 Rapide"
            };
            bool is_tab1 = (sui->tp_tab == 1);
            draw_settings_chrome(state, fb, th,
                                 is_tab1 ? "PERSONNALISER LE CURSEUR" : "NAVIGATION TACTILE",
                                 NL_ICO_TOUCHPAD,
                                 is_tab1 ? hints_tab1 : hints_tab0, 3);

            /* Top 2-Tab Bar (y = 28..44) */
            fb_fill_rounded_rect_clipped(fb, 14, 28, 292, 16, 5, th->pill_bg, 26, 220);
            uint16_t t0_bg = !is_tab1 ? th->accent_primary : th->pill_bg;
            uint16_t t0_fg = !is_tab1 ? th->text_on_accent : th->text_secondary;
            uint16_t t1_bg = is_tab1  ? th->accent_primary : th->pill_bg;
            uint16_t t1_fg = is_tab1  ? th->text_on_accent : th->accent_secondary;
            fb_fill_rounded_rect_clipped(fb, 15, 29, 138, 14, 4, t0_bg, 26, 220);
            fb_draw_text(fb, 27, 33, "1. Navigation tactile", t0_fg, 22);
            fb_fill_rounded_rect_clipped(fb, 155, 29, 150, 14, 4, t1_bg, 26, 220);
            fb_draw_text(fb, 163, 33, "2. Personnaliser curseur", t1_fg, 24);

            unsigned glow_alpha = 32U;
            if (is_active_scr && state->anim.set_sel_glow_fp < 256) {
                glow_alpha = (unsigned)((state->anim.set_sel_glow_fp + 4) >> 3);
                if (glow_alpha > 32U) glow_alpha = 32U;
            }

            if (!is_tab1) {
                int cur_row = sui->tp_cursor;
                if (cur_row < 0) cur_row = 0;
                if (cur_row > 3) cur_row = 3;

                /* Row 0 (y=47..85): Touchpad Slide Toggle Card */
                int sw_fp = state->anim.switch_touchpad_fp;
                if ((state->settings.touchpad_nav && sw_fp == 0) || (!state->settings.touchpad_nav && sw_fp == 256)) {
                    sw_fp = state->settings.touchpad_nav ? 256 : 0;
                }
                unsigned sw_alpha = (unsigned)((sw_fp + 4) >> 3);
                uint16_t row0_border = (cur_row == 0)
                    ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha)
                    : th->border_subtle;
                uint16_t row0_bg = (cur_row == 0)
                    ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, glow_alpha >> 1)
                    : th->card_bg;
                fb_fill_rounded_rect_clipped(fb, 14, 47, 292, 38, 6, row0_border, 26, 220);
                fb_fill_rounded_rect_clipped(fb, 15, 48, 290, 36, 5, row0_bg, 26, 220);

                draw_settings_icon_badge(fb, 21, 56, 20, NL_ICO_TOUCHPAD, sw_alpha, false, th, 26, 220);
                fb_draw_text(fb, 47, 53, "Navigation au pave tactile", th->text_primary, 28);
                fb_draw_text(fb, 47, 66,
                             state->settings.touchpad_nav ? "Glissement actif (style souris)" : "Desactive (touches flechees)",
                             nl_rgb565_lerp(th->text_secondary, th->accent_secondary, sw_alpha), 34);
                draw_toggle_switch(fb, 262, 59, sw_fp, th);

                /* Row 1 (y=88..141): 5-Detent Mechanical Sensitivity Slider Card */
                int sens = state->settings.touchpad_sensitivity;
                if (sens < 0) sens = 0;
                if (sens >= NL_TP_SENS_COUNT) sens = NL_TP_SENS_COUNT - 1;

                uint16_t row1_border = (cur_row == 1)
                    ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha)
                    : th->border_subtle;
                uint16_t row1_bg = (cur_row == 1)
                    ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, glow_alpha >> 1)
                    : th->card_bg;
                fb_fill_rounded_rect_clipped(fb, 14, 88, 292, 53, 6, row1_border, 26, 220);
                fb_fill_rounded_rect_clipped(fb, 15, 89, 290, 51, 5, row1_bg, 26, 220);

                fb_draw_text(fb, 22, 95, "Sensibilite (5 crans)", th->text_primary, 24);

                unsigned pulse_alpha = (unsigned)((state->anim.tp_detent_pulse_fp + 4) >> 3);
                if (pulse_alpha > 32U) pulse_alpha = 32U;
                const char *s_lbl = sens_labels[sens];
                int lbl_w = (int)strlen(s_lbl) * 6 + 10;
                int lbl_x = 294 - lbl_w;
                uint16_t badge_bg = (cur_row == 1)
                    ? nl_rgb565_lerp(th->pill_bg, th->accent_primary, 12U + (pulse_alpha >> 1))
                    : th->pill_bg;
                uint16_t badge_fg = (cur_row == 1)
                    ? nl_rgb565_lerp(th->accent_secondary, th->text_on_accent, 14U)
                    : th->accent_secondary;
                fb_fill_rounded_rect_clipped(fb, lbl_x, 92, lbl_w, 13, 4, badge_bg, 26, 220);
                fb_draw_text(fb, lbl_x + 5, 96, s_lbl, badge_fg, 16);

                const int track_x = 34;
                const int track_w = 252;
                const int track_y = 119;

                int pos_fp = state->anim.tp_slider_pos_fp;
                if (pos_fp < 0 && pos_fp != 0 && !state->anim.initialized) pos_fp = sens * 256;
                int handle_x = track_x + ((pos_fp * track_w + 512) >> 10);
                if (handle_x < track_x - 3) handle_x = track_x - 3;
                if (handle_x > track_x + track_w + 3) handle_x = track_x + track_w + 3;

                int fill_w = handle_x - track_x;
                if (fill_w < 0) fill_w = 0;
                if (fill_w > track_w) fill_w = track_w;

                uint16_t rail_col = nl_rgb565_lerp(th->dot_inactive, th->border_subtle, 10U);
                uint16_t active_rail_col = state->settings.touchpad_nav
                    ? th->accent_primary
                    : nl_rgb565_lerp(th->dot_inactive, th->accent_primary, 20U);
                fb_fill_aa_capsule_clipped(fb, track_x, track_y - 2, track_w, 5, rail_col, 26, 220);
                if (fill_w >= 5) {
                    fb_fill_aa_capsule_clipped(fb, track_x, track_y - 2, fill_w, 5, active_rail_col, 26, 220);
                }

                for (int n = 0; n < NL_TP_SENS_COUNT; n++) {
                    int nx = track_x + n * 63;
                    bool passed = (n <= sens);
                    bool is_cur = (n == sens);
                    if (is_cur && pulse_alpha > 1U) {
                        uint16_t halo_c = nl_rgb565_lerp(row1_bg, th->accent_secondary, pulse_alpha);
                        fb_fill_aa_capsule_clipped(fb, nx - 6, track_y - 6, 13, 13, halo_c, 26, 220);
                    }
                    uint16_t tick_col = is_cur
                        ? th->accent_secondary
                        : (passed ? active_rail_col : nl_rgb565_lerp(th->text_secondary, th->dot_inactive, 16U));
                    fb_fill_rect_clipped(fb, nx - 1, track_y - 5, 3, 11, tick_col, 26, 220);
                    fb_fill_aa_capsule_clipped(fb, nx - 4, track_y - 4, 9, 9, tick_col, 26, 220);
                    fb_fill_aa_capsule_clipped(fb, nx - 2, track_y - 2, 5, 5, row1_bg, 26, 220);

                    char num_buf[2] = { (char)('1' + n), '\0' };
                    uint16_t num_col = is_cur ? th->accent_secondary : th->text_secondary;
                    fb_draw_text(fb, nx - 2, 130, num_buf, num_col, 1);
                }

                int knob_expand = (pulse_alpha > 14U) ? 1 : 0;
                int kw = 12 + knob_expand * 2;
                int kh = 14 + knob_expand * 2;
                int kx = handle_x - (kw / 2);
                int ky = track_y - (kh / 2);
                uint16_t knob_rim = (cur_row == 1 || pulse_alpha > 0U)
                    ? nl_rgb565_lerp(th->accent_primary, th->accent_secondary, 16U + (pulse_alpha >> 1))
                    : th->border_subtle;
                fb_fill_rounded_rect_clipped(fb, kx - 1, ky - 1, kw + 2, kh + 2, 4, knob_rim, 26, 220);
                fb_fill_rounded_rect_clipped(fb, kx, ky, kw, kh, 3, RGB565(248, 250, 252), 26, 220);
                fb_fill_rect_clipped(fb, handle_x - 1, ky + 3, 2, kh - 6, active_rail_col, 26, 220);

                /* Row 2 (y=144..180): Independent Cursor Visibility Toggle (Afficher le curseur : OUI / NON) */
                int cur_sw_fp = state->anim.switch_tp_cursor_fp;
                if ((state->settings.touchpad_cursor_show && cur_sw_fp == 0) ||
                    (!state->settings.touchpad_cursor_show && cur_sw_fp == 256)) {
                    cur_sw_fp = state->settings.touchpad_cursor_show ? 256 : 0;
                }
                unsigned cur_sw_alpha = (unsigned)((cur_sw_fp + 4) >> 3);
                uint16_t row2_border = (cur_row == 2)
                    ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha)
                    : th->border_subtle;
                uint16_t row2_bg = (cur_row == 2)
                    ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, glow_alpha >> 1)
                    : th->card_bg;
                fb_fill_rounded_rect_clipped(fb, 14, 144, 292, 36, 6, row2_border, 26, 220);
                fb_fill_rounded_rect_clipped(fb, 15, 145, 290, 34, 5, row2_bg, 26, 220);

                fb_draw_text(fb, 22, 150,
                             state->settings.touchpad_cursor_show
                                 ? "Afficher le curseur : OUI"
                                 : "Afficher le curseur : NON",
                             th->text_primary, 28);
                fb_draw_text(fb, 22, 163,
                             state->settings.touchpad_cursor_show
                                 ? "Curseur graphique actif"
                                 : "Navigation classique sans curseur",
                             nl_rgb565_lerp(th->text_secondary, th->accent_secondary, cur_sw_alpha), 34);
                draw_toggle_switch(fb, 262, 155, cur_sw_fp, th);

                /* Row 3 (y=183..216): Open "Personnaliser le curseur" Tab Card */
                uint16_t row3_border = (cur_row == 3)
                    ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha)
                    : th->border_subtle;
                uint16_t row3_bg = (cur_row == 3)
                    ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, glow_alpha >> 1)
                    : th->card_bg;
                fb_fill_rounded_rect_clipped(fb, 14, 183, 292, 33, 6, row3_border, 26, 220);
                fb_fill_rounded_rect_clipped(fb, 15, 184, 290, 31, 5, row3_bg, 26, 220);

                fb_draw_text(fb, 22, 189, "Personnaliser le curseur", th->text_primary, 26);
                char style_sub[44];
                snprintf(style_sub, sizeof(style_sub), "Apparence : %s",
                         nl_cursor_style_label(state->settings.touchpad_cursor_style));
                fb_draw_text(fb, 22, 201, style_sub, th->accent_secondary, 30);

                uint16_t btn_bg = (cur_row == 3) ? th->accent_primary : th->pill_bg;
                uint16_t btn_fg = (cur_row == 3) ? th->text_on_accent : th->accent_secondary;
                fb_fill_rounded_rect_clipped(fb, 232, 192, 64, 15, 4, btn_bg, 26, 220);
                fb_draw_text(fb, 238, 196, "TAB / EXE", btn_fg, 9);
            } else {
                /* TAB 1: "Personnaliser le curseur" — 3 Visual Vignette Cards with Animated Halo */
                static const char *const card_titles[NL_CURSOR_STYLE_COUNT] = {
                    "Aucun",
                    "Fleche",
                    "Cercle"
                };
                static const char *const card_subs[NL_CURSOR_STYLE_COUNT] = {
                    "Sans curseur",
                    "Pointe exacte",
                    "Centre exact"
                };

                int sel_style = sui->tp_style_cursor;
                if (sel_style < 0) sel_style = 0;
                if (sel_style >= NL_CURSOR_STYLE_COUNT) sel_style = NL_CURSOR_STYLE_COUNT - 1;

                unsigned style_glow = (unsigned)((state->anim.tp_style_glow_fp + 4) >> 3);
                if (style_glow > 32U) style_glow = 32U;

                const int card_w = 90;
                const int card_h = 116;
                const int gap_x  = 11;
                const int start_x = 14;
                const int card_y  = 50;

                for (int c = 0; c < NL_CURSOR_STYLE_COUNT; c++) {
                    int cx = start_x + c * (card_w + gap_x);
                    bool is_focused = (c == sel_style);
                    bool is_saved   = (c == (int)state->settings.touchpad_cursor_style);

                    /* Animated contour / halo around focused vignette card */
                    if (is_focused) {
                        int pulse_phase = (int)(state->anim.tp_style_halo_phase & 63U);
                        int tri = (pulse_phase < 32) ? pulse_phase : (64 - pulse_phase);
                        unsigned halo_a = (unsigned)(12 + ((tri * 18) >> 5));
                        uint16_t outer_halo = nl_rgb565_lerp(th->bg_main, th->accent_secondary, halo_a);
                        fb_fill_rounded_rect_clipped(fb, cx - 2, card_y - 2, card_w + 4, card_h + 4, 9, outer_halo, 26, 220);
                    }

                    uint16_t c_border = is_focused
                        ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, style_glow)
                        : (is_saved ? th->accent_primary : th->border_subtle);
                    uint16_t c_bg = is_focused
                        ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, style_glow >> 1)
                        : th->card_bg;
                    fb_fill_rounded_rect_clipped(fb, cx, card_y, card_w, card_h, 7, c_border, 26, 220);
                    fb_fill_rounded_rect_clipped(fb, cx + 1, card_y + 1, card_w - 2, card_h - 2, 6, c_bg, 26, 220);

                    /* Active style badge at top-right of vignette */
                    if (is_saved) {
                        fb_fill_rounded_rect_clipped(fb, cx + card_w - 38, card_y + 5, 33, 11, 3, th->accent_primary, 26, 220);
                        fb_draw_text(fb, cx + card_w - 35, card_y + 7, "ACTIF", th->text_on_accent, 5);
                    }

                    /* Inner visual preview stage (56x54 rounded box) */
                    int stage_x = cx + (card_w - 62) / 2;
                    int stage_y = card_y + 20;
                    uint16_t stage_bg = th->is_light ? RGB565(226, 232, 240) : RGB565(12, 17, 28);
                    fb_fill_rounded_rect_clipped(fb, stage_x, stage_y, 62, 52, 6, th->border_subtle, 26, 220);
                    fb_fill_rounded_rect_clipped(fb, stage_x + 1, stage_y + 1, 60, 50, 5, stage_bg, 26, 220);

                    int mid_x = stage_x + 31;
                    int mid_y = stage_y + 26;

                    if (c == NL_CURSOR_STYLE_NONE) {
                        /* Standard cursorless navigation frame vignette */
                        fb_fill_rounded_rect_clipped(fb, mid_x - 14, mid_y - 11, 28, 22, 5, th->accent_primary, 26, 220);
                        fb_fill_rounded_rect_clipped(fb, mid_x - 12, mid_y - 9, 24, 18, 4, c_bg, 26, 220);
                        fb_draw_text(fb, mid_x - 9, mid_y - 3, "STD", th->text_secondary, 3);
                    } else if (c == NL_CURSOR_STYLE_ARROW) {
                        /* Faint 3-dot trail preview + authentic Arrow PNG cursor (hotspot at tip) */
                        uint16_t tr_c1 = nl_rgb565_lerp(stage_bg, th->accent_primary, 10U);
                        uint16_t tr_c2 = nl_rgb565_lerp(stage_bg, th->accent_secondary, 18U);
                        fb_fill_aa_capsule_clipped(fb, mid_x - 14, mid_y + 8, 5, 5, tr_c1, 26, 220);
                        fb_fill_aa_capsule_clipped(fb, mid_x - 10, mid_y + 3, 6, 6, tr_c2, 26, 220);
                        draw_cursor_alpha_sprite(fb, NL_CURSOR_STYLE_ARROW, mid_x - 8, mid_y - 11, 256U, 26, 220, th);
                    } else if (c == NL_CURSOR_STYLE_CIRCLE) {
                        /* Faint 3-dot trail preview + authentic Circle PNG cursor (hotspot at center) */
                        uint16_t tr_c1 = nl_rgb565_lerp(stage_bg, th->accent_primary, 10U);
                        uint16_t tr_c2 = nl_rgb565_lerp(stage_bg, th->accent_secondary, 18U);
                        fb_fill_aa_capsule_clipped(fb, mid_x - 14, mid_y + 6, 5, 5, tr_c1, 26, 220);
                        fb_fill_aa_capsule_clipped(fb, mid_x - 8, mid_y + 3, 6, 6, tr_c2, 26, 220);
                        draw_cursor_alpha_sprite(fb, NL_CURSOR_STYLE_CIRCLE, mid_x + 1, mid_y, 256U, 26, 220, th);
                    }

                    /* Title & subtitle centered at bottom of vignette */
                    int tw = (int)strlen(card_titles[c]) * 6;
                    fb_draw_text(fb, cx + (card_w - tw) / 2, card_y + 80, card_titles[c],
                                 is_focused ? th->text_primary : th->text_secondary, 12);
                    int sw = (int)strlen(card_subs[c]) * 6;
                    fb_draw_text(fb, cx + (card_w - sw) / 2, card_y + 95, card_subs[c],
                                 is_focused ? th->accent_secondary : th->text_secondary, 14);
                }

                /* Bottom status card reminding independent visibility state */
                fb_fill_rounded_rect_clipped(fb, 14, 172, 292, 42, 6, th->card_bg, 26, 220);
                char vis_buf[52];
                snprintf(vis_buf, sizeof(vis_buf), "Afficher le curseur : %s   |   Style : %s",
                         state->settings.touchpad_cursor_show ? "OUI" : "NON",
                         nl_cursor_style_label(state->settings.touchpad_cursor_style));
                fb_draw_text(fb, 22, 180, vis_buf,
                             state->settings.touchpad_cursor_show ? RGB565(74, 222, 128) : th->accent_secondary, 44);
                fb_draw_text(fb, 22, 195, "EXE : Appliquer ce style   |   TAB : Retour options", th->text_secondary, 45);
            }
            break;
        }

        case NL_UI_SETTINGS_SHORTCUTS: {
            static const char *const hints[3] = {"EXE Choisir", "DEL Effacer", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "TOUCHES DE LANCEMENT", NL_ICO_LAUNCH_KEY, hints, 3);

            if (state->total_games <= 0) {
                fb_fill_rounded_rect_clipped(fb, 40, 88, 240, 44, 7, th->card_bg, 26, 220);
                fb_draw_text(fb, 84, 106, "Aucun jeu detecte", th->text_secondary, 24);
            } else {
                const int vis = 6;
                const int row_y0 = 32;
                const int row_h = 28;
                const int row_gap = 2;
                const int stride = row_h + row_gap;

                int scroll_px, sel_y, prev_cur;
                unsigned glow_alpha;
                resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                          sui->sc_game_cursor, sui->sc_game_scroll,
                                          &scroll_px, &sel_y, &glow_alpha, &prev_cur);

                uint16_t border_col = nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha);
                fb_fill_rounded_rect_clipped(fb, 11, sel_y - 1, 294, row_h + 2, 7, border_col, 28, 218);

                for (int idx = 0; idx < state->total_games; idx++) {
                    int ry = row_y0 + idx * stride - scroll_px;
                    if (ry + row_h <= 28 || ry >= 218) continue;

                    bool sel = (idx == sui->sc_game_cursor);
                    bool is_prev = (!sel && idx == prev_cur && glow_alpha < 32U);
                    unsigned r_alpha = sel ? glow_alpha : (is_prev ? (32U - glow_alpha) : 0U);
                    const nl_game_t *g = &state->games[idx];

                    fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6,
                                                 nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha), 28, 218);
                    draw_settings_icon_badge(fb, 17, ry + 5, 18, NL_ICO_LAUNCH_KEY, r_alpha, false, th, 28, 218);
                    fb_draw_text_ellipsis(fb, 41, ry + 11, g->title, th->text_primary, 30, 28, 218);

                    if (g->shortcut_key != 0) {
                        char kb[6];
                        snprintf(kb, sizeof(kb), "[%c]", g->shortcut_key);
                        fb_fill_rounded_rect_clipped(fb, 266, ry + 6, 28, 16, 4, th->accent_primary, 28, 218);
                        fb_draw_text_clipped(fb, 271, ry + 11, kb, th->text_on_accent, 3, 28, 218);
                    } else {
                        fb_fill_rounded_rect_clipped(fb, 266, ry + 6, 28, 16, 4, th->pill_bg, 28, 218);
                        fb_draw_text_clipped(fb, 271, ry + 11, "---", th->text_secondary, 3, 28, 218);
                    }
                }
                draw_scrollbar_v_smooth(fb, row_y0, vis * stride - row_gap,
                                        scroll_px, stride, vis, state->total_games, th);
            }

            /* Overlay Key Picker with smooth modal settle */
            if (sui->sc_picking_key && state->total_games > 0) {
                int m_fp = (state->anim.modal_fp > 0) ? state->anim.modal_fp : 256;
                int m_dy = ((256 - m_fp) * 6 + 128) >> 8;
                unsigned dim_a = (unsigned)((m_fp * 12) >> 8);
                uint16_t dim_col = th->is_light ? RGB565(180, 190, 205) : RGB565(6, 8, 14);
                for (int py = 26; py < 220; py++) {
                    uint16_t *row = fb + py * NL_SCREEN_W;
                    for (int px = 0; px < NL_SCREEN_W; px++) {
                        row[px] = nl_rgb565_lerp(row[px], dim_col, dim_a);
                    }
                }

                fb_fill_rounded_rect_clipped(fb, 22, 42 + m_dy, 276, 164, 8, th->border_subtle, 26, 220);
                fb_fill_rounded_rect_clipped(fb, 24, 44 + m_dy, 272, 160, 7, th->bg_top_bar, 26, 220);

                const nl_game_t *sg = &state->games[sui->sc_game_cursor];
                fb_draw_text(fb, 34, 52 + m_dy, "Touche pour :", th->text_secondary, 16);
                fb_draw_text_ellipsis(fb, 116, 52 + m_dy, sg->title, th->accent_secondary, 26, 26, 220);

                const char *keys_str = NULL;
                int n_keys = nl_get_assignable_keys(&keys_str);
                int cols = 8;
                for (int k = 0; k < n_keys + 1; k++) {
                    int r = k / cols;
                    int c = k % cols;
                    int kx = 34 + c * 31;
                    int ky = 72 + r * 26 + m_dy;
                    int kw = (k == 0) ? 28 : 26;
                    char ch = (k == 0) ? '-' : keys_str[k - 1];
                    int owner = (k == 0) ? -1 : nl_shortcut_find_owner(state, ch, sui->sc_game_cursor);
                    uint16_t box_col = (owner >= 0) ? th->border_subtle : th->card_bg;
                    fb_fill_rounded_rect_clipped(fb, kx, ky, kw, 20, 4, box_col, 44 + m_dy, 204 + m_dy);
                }

                /* Smooth gliding key-picker highlight pill */
                {
                    int cur_k = sui->sc_key_cursor;
                    int cur_r = cur_k / cols;
                    int cur_c = cur_k % cols;
                    int hk_x = (state->anim.sc_key_w_fp > 0) ? (state->anim.sc_key_x_fp >> 4) : (34 + cur_c * 31);
                    int hk_y = ((state->anim.sc_key_w_fp > 0) ? (state->anim.sc_key_y_fp >> 4) : (72 + cur_r * 26)) + m_dy;
                    int hk_w = (state->anim.sc_key_w_fp > 0) ? (state->anim.sc_key_w_fp >> 4) : ((cur_k == 0) ? 28 : 26);
                    fb_fill_rounded_rect_clipped(fb, hk_x - 1, hk_y - 1, hk_w + 2, 22, 5, th->accent_secondary, 44 + m_dy, 204 + m_dy);
                    fb_fill_rounded_rect_clipped(fb, hk_x, hk_y, hk_w, 20, 4, th->accent_primary, 44 + m_dy, 204 + m_dy);
                }

                for (int k = 0; k < n_keys + 1; k++) {
                    int r = k / cols;
                    int c = k % cols;
                    int kx = 34 + c * 31;
                    int ky = 72 + r * 26 + m_dy;
                    bool ksel = (k == sui->sc_key_cursor);
                    char ch = (k == 0) ? '-' : keys_str[k - 1];
                    int owner = (k == 0) ? -1 : nl_shortcut_find_owner(state, ch, sui->sc_game_cursor);
                    if (k == 0) {
                        fb_draw_text_clipped(fb, kx + 2, ky + 6, "OFF", ksel ? th->text_on_accent : th->text_secondary, 3, 44 + m_dy, 204 + m_dy);
                    } else {
                        char cs[2] = {ch, '\0'};
                        fb_draw_text_clipped(fb, kx + 10, ky + 6, cs,
                                             ksel ? th->text_on_accent : (owner >= 0 ? th->accent_secondary : th->text_primary),
                                             1, 44 + m_dy, 204 + m_dy);
                    }
                }
                fb_draw_text(fb, 34, 184 + m_dy, "Clavier direct ou FLECHES + EXE", th->text_secondary, 40);
            }

            /* Overlay Conflict Confirmation with smooth modal settle & animated button highlight */
            if (sui->sc_conflict_prompt) {
                int m_fp = (state->anim.modal_fp > 0) ? state->anim.modal_fp : 256;
                int m_dy = ((256 - m_fp) * 6 + 128) >> 8;
                fb_fill_rounded_rect_clipped(fb, 32, 62 + m_dy, 256, 120, 8, th->accent_primary, 26, 220);
                fb_fill_rounded_rect_clipped(fb, 34, 64 + m_dy, 252, 116, 7, th->bg_top_bar, 26, 220);

                char msg1[40];
                snprintf(msg1, sizeof(msg1), "Touche [%c] deja assignee a :", sui->sc_conflict_key);
                fb_draw_text(fb, 46, 76 + m_dy, msg1, th->text_primary, 36);
                if (sui->sc_conflict_owner_idx >= 0 && sui->sc_conflict_owner_idx < state->total_games) {
                    fb_draw_text_ellipsis(fb, 46, 92 + m_dy, state->games[sui->sc_conflict_owner_idx].title,
                                          th->accent_secondary, 36, 64 + m_dy, 180 + m_dy);
                }
                fb_draw_text(fb, 46, 112 + m_dy, "Remplacer le raccourci ?", th->text_secondary, 30);

                int btn_fp = state->anim.modal_btn_fp;
                if ((sui->sc_conflict_confirm_yes && btn_fp == 0) || (!sui->sc_conflict_confirm_yes && btn_fp == 256)) {
                    btn_fp = sui->sc_conflict_confirm_yes ? 256 : 0;
                }
                unsigned a_yes = (unsigned)((btn_fp + 4) >> 3);
                unsigned a_no = 32U - a_yes;

                fb_fill_rounded_rect_clipped(fb, 54, 138 + m_dy, 92, 24, 5,
                                             nl_rgb565_lerp(th->pill_bg, th->accent_primary, a_no), 64 + m_dy, 180 + m_dy);
                fb_draw_text(fb, 76, 146 + m_dy, "Annuler",
                             nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_no), 10);

                fb_fill_rounded_rect_clipped(fb, 174, 138 + m_dy, 92, 24, 5,
                                             nl_rgb565_lerp(th->pill_bg, th->accent_primary, a_yes), 64 + m_dy, 180 + m_dy);
                fb_draw_text(fb, 192, 146 + m_dy, "Remplacer",
                             nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_yes), 10);
            }
            break;
        }

        case NL_UI_SETTINGS_KEY_HELP: {
            static const char *const hints[2] = {"UP/DN Defiler", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "COMMANDES DU LAUNCHER", NL_ICO_KEYBOARD, hints, 2);

            nl_shortcut_def_t defs[40];
            int total = nl_get_shortcut_defs(state, defs, 40);
            const int vis = 7;
            const int row_y0 = 33;
            const int row_h = 24;
            const int row_gap = 2;
            const int stride = row_h + row_gap;

            int scroll_px, sel_y, prev_cur;
            unsigned glow_alpha;
            resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                      sui->sub_scroll, sui->sub_scroll,
                                      &scroll_px, &sel_y, &glow_alpha, &prev_cur);
            (void)sel_y;
            (void)glow_alpha;
            (void)prev_cur;

            for (int idx = 0; idx < total; idx++) {
                int ry = row_y0 + idx * stride - scroll_px;
                if (ry + row_h <= 28 || ry >= 218) continue;
                fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 5, th->card_bg, 28, 218);

                fb_fill_rounded_rect_clipped(fb, 18, ry + 4, 72, 16, 4, th->pill_bg, 28, 218);
                fb_draw_text_ellipsis(fb, 22, ry + 8, defs[idx].key_label, th->accent_secondary, 11, 28, 218);
                fb_draw_text_ellipsis(fb, 98, ry + 8, defs[idx].action_label, th->text_primary, 32, 28, 218);
            }
            draw_scrollbar_v_smooth(fb, row_y0, vis * stride - row_gap,
                                    scroll_px, stride, vis, total, th);
            break;
        }

        case NL_UI_SETTINGS_SORT: {
            static const char *const hints[3] = {"UP/DN Choisir", "EXE Appliquer", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "TRI DE LA BIBLIOTHEQUE", NL_ICO_SORT, hints, 3);

            const int vis = 6;
            const int row_y0 = 32;
            const int row_h = 28;
            const int row_gap = 2;
            const int stride = row_h + row_gap;

            int scroll_px, sel_y, prev_cur;
            unsigned glow_alpha;
            resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                      sui->sub_cursor, sui->sub_scroll,
                                      &scroll_px, &sel_y, &glow_alpha, &prev_cur);

            uint16_t border_col = nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha);
            fb_fill_rounded_rect_clipped(fb, 11, sel_y - 1, 294, row_h + 2, 7, border_col, 28, 218);

            for (int idx = 0; idx < NL_SORT_COUNT; idx++) {
                int ry = row_y0 + idx * stride - scroll_px;
                if (ry + row_h <= 28 || ry >= 218) continue;

                bool sel = (idx == sui->sub_cursor);
                bool is_prev = (!sel && idx == prev_cur && glow_alpha < 32U);
                unsigned r_alpha = sel ? glow_alpha : (is_prev ? (32U - glow_alpha) : 0U);
                bool cur = (idx == (int)state->settings.sort_order);

                fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6,
                                             nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha), 28, 218);

                fb_fill_aa_capsule_clipped(fb, 20, ry + 9, 10, 10,
                                           cur ? th->accent_primary : th->dot_inactive, 28, 218);
                if (cur) {
                    fb_fill_aa_capsule_clipped(fb, 23, ry + 12, 4, 4, th->text_on_accent, 28, 218);
                }

                fb_draw_text_clipped(fb, 36, ry + 11, nl_sort_order_label((nl_sort_order_t)idx), th->text_primary, 35, 28, 218);
                if (cur) {
                    fb_fill_rounded_rect_clipped(fb, 254, ry + 7, 38, 14, 4, th->pill_bg, 28, 218);
                    fb_draw_text_clipped(fb, 258, ry + 11, "ACTIF", th->accent_secondary, 5, 28, 218);
                }
            }
            draw_scrollbar_v_smooth(fb, row_y0, vis * stride - row_gap,
                                    scroll_px, stride, vis, NL_SORT_COUNT, th);
            break;
        }

        case NL_UI_SETTINGS_STATS: {
            static const char *const hints[2] = {"UP/DN Parcourir", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "STATISTIQUES DE JEU", NL_ICO_STATS, hints, 2);

            if (state->total_games <= 0) {
                fb_fill_rounded_rect_clipped(fb, 40, 88, 240, 44, 7, th->card_bg, 26, 220);
                fb_draw_text(fb, 84, 106, "Aucune statistique", th->text_secondary, 24);
            } else {
                const int vis = 5;
                const int row_y0 = 33;
                const int row_h = 33;
                const int row_gap = 3;
                const int stride = row_h + row_gap;

                int scroll_px, sel_y, prev_cur;
                unsigned glow_alpha;
                resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                          sui->sub_cursor, sui->sub_scroll,
                                          &scroll_px, &sel_y, &glow_alpha, &prev_cur);

                uint16_t border_col = nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha);
                fb_fill_rounded_rect_clipped(fb, 11, sel_y - 1, 294, row_h + 2, 7, border_col, 28, 218);

                for (int idx = 0; idx < state->total_games; idx++) {
                    int ry = row_y0 + idx * stride - scroll_px;
                    if (ry + row_h <= 28 || ry >= 218) continue;

                    bool sel = (idx == sui->sub_cursor);
                    bool is_prev = (!sel && idx == prev_cur && glow_alpha < 32U);
                    unsigned r_alpha = sel ? glow_alpha : (is_prev ? (32U - glow_alpha) : 0U);
                    const nl_game_t *g = &state->games[idx];

                    fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6,
                                                 nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha), 28, 218);

                    draw_settings_icon_badge(fb, 17, ry + 7, 18, NL_ICO_STATS, r_alpha, false, th, 28, 218);
                    fb_draw_text_ellipsis(fb, 41, ry + 6, g->title, th->text_primary, 27, 28, 218);

                    char l_buf[24];
                    snprintf(l_buf, sizeof(l_buf), "%lu lanc.", (unsigned long)g->launch_count);
                    int lw = (int)strlen(l_buf) * 6;
                    fb_draw_text_clipped(fb, 294 - lw, ry + 6, l_buf, th->accent_secondary, 14, 28, 218);

                    char d_buf[20];
                    char t_buf[24];
                    nl_format_duration(g->play_time_sec, d_buf, sizeof(d_buf));
                    nl_format_timestamp(g->last_played_ts, t_buf, sizeof(t_buf));

                    char line2[64];
                    snprintf(line2, sizeof(line2), "Temps: %-8s Dernier: %s", d_buf, t_buf);
                    fb_draw_text_ellipsis(fb, 41, ry + 20, line2, th->text_secondary, 42, 28, 218);
                }
                draw_scrollbar_v_smooth(fb, row_y0, vis * stride - row_gap,
                                        scroll_px, stride, vis, state->total_games, th);
            }
            break;
        }

        case NL_UI_SETTINGS_CAPTURE_BATCH: {
            static const char *const hints_active[2] = {"DROITE Passer", "ESC Annuler"};
            static const char *const hints_semi[3] = {"EXE Relancer", "DROITE Passer", "ESC Annuler"};
            static const char *const hints_done[2] = {"EXE Relancer/OK", "ESC Retour"};
            const char *const *hints = sui->cap_active
                ? (sui->cap_semi_auto ? hints_semi : hints_active)
                : hints_done;
            int hint_cnt = sui->cap_active ? (sui->cap_semi_auto ? 3 : 2) : 2;
            draw_settings_chrome(state, fb, th, "GENERER CAPTURES D'ECRAN", NL_ICO_CAPTURE, hints, hint_cnt);

            fb_fill_rounded_rect_clipped(fb, 14, 34, 292, 178, 8, th->border_subtle, 26, 220);
            fb_fill_rounded_rect_clipped(fb, 15, 35, 290, 176, 7, th->card_bg, 26, 220);

            if (sui->cap_total <= 0) {
                draw_settings_icon_badge(fb, 26, 48, 22, NL_ICO_CAPTURE, 24U, false, th, 26, 220);
                fb_draw_text(fb, 56, 50, "Toutes les captures sont pretes !", th->text_primary, 36);
                fb_draw_text(fb, 56, 64, "Chaque jeu possede deja sa capture.", th->accent_secondary, 36);
                fb_draw_text(fb, 26, 94, "- Aucune capture existante (.scr.tns)", th->text_secondary, 42);
                fb_draw_text(fb, 26, 108, "  ne sera ecrasee.", th->text_secondary, 42);
                fb_draw_text(fb, 26, 126, "- Les icones personnalisees (.bmp/.ico)", th->text_secondary, 42);
                fb_draw_text(fb, 26, 140, "  restent intactes.", th->text_secondary, 42);

                fb_fill_rounded_rect_clipped(fb, 96, 168, 128, 26, 6, th->accent_primary, 26, 220);
                fb_draw_text(fb, 124, 177, "Retour (EXE)", th->text_on_accent, 14);
            } else if (sui->cap_active) {
                int cur_game_idx = (sui->cap_pos >= 0 && sui->cap_pos < sui->cap_total)
                    ? sui->cap_queue[sui->cap_pos]
                    : 0;
                const char *g_title = (cur_game_idx >= 0 && cur_game_idx < state->total_games)
                    ? state->games[cur_game_idx].title
                    : "---";
                int remaining = sui->cap_total - sui->cap_pos;
                if (remaining < 0) remaining = 0;

                draw_settings_icon_badge(fb, 24, 43, 20, NL_ICO_CAPTURE, 28U, false, th, 26, 220);
                fb_draw_text(fb, 50, 45, "Jeu en cours de capture :", th->text_secondary, 28);
                fb_draw_text_ellipsis(fb, 50, 57, g_title, th->text_primary, 38, 26, 220);

                /* Progress Bar */
                int bar_x = 24, bar_y = 74, bar_w = 272, bar_h = 10;
                fb_fill_rounded_rect_clipped(fb, bar_x, bar_y, bar_w, bar_h, 4, th->pill_bg, 26, 220);
                int fill_w = (sui->cap_total > 0) ? ((sui->cap_pos * bar_w) / sui->cap_total) : 0;
                if (fill_w > 0 && fill_w < 8) fill_w = 8;
                if (fill_w > bar_w) fill_w = bar_w;
                if (fill_w > 0) {
                    fb_fill_rounded_rect_clipped(fb, bar_x, bar_y, fill_w, bar_h, 4, th->accent_primary, 26, 220);
                }

                /* Stats Counters */
                char c1[40], c2[40], c3[40];
                snprintf(c1, sizeof(c1), "Captures realisees : %d", sui->cap_captured);
                snprintf(c2, sizeof(c2), "Jeux restants      : %d / %d", remaining, sui->cap_total);
                snprintf(c3, sizeof(c3), "Jeux passes        : %d", sui->cap_skipped);
                fb_draw_text(fb, 26, 92, c1, th->accent_secondary, 36);
                fb_draw_text(fb, 26, 105, c2, th->text_primary, 36);
                fb_draw_text(fb, 26, 118, c3, th->text_secondary, 36);

                if (sui->cap_semi_auto) {
                    fb_fill_rounded_rect_clipped(fb, 24, 132, 272, 34, 5, th->bg_top_bar, 26, 220);
                    fb_draw_text(fb, 30, 137, "Mode semi-auto : lancez avec EXE,", th->accent_secondary, 42);
                    fb_draw_text(fb, 30, 149, "quittez le jeu avec ESC, ou DROITE Passer.", th->text_secondary, 43);
                } else {
                    fb_fill_rounded_rect_clipped(fb, 24, 132, 272, 34, 5, th->bg_top_bar, 26, 220);
                    fb_draw_text(fb, 30, 137, "Capture automatique du jeu au lancement.", th->text_secondary, 42);
                    fb_draw_text(fb, 30, 149, "Quittez le jeu avec ESC apres le chargement.", th->text_secondary, 44);
                }

                /* Action Buttons: Passer & Annuler */
                fb_fill_rounded_rect_clipped(fb, 30, 174, 124, 24, 5, th->accent_primary, 26, 220);
                fb_draw_text(fb, 42, 182, "DROITE Passer", th->text_on_accent, 16);

                fb_fill_rounded_rect_clipped(fb, 166, 174, 124, 24, 5, th->pill_bg, 26, 220);
                fb_draw_text(fb, 186, 182, "ESC Annuler", th->text_primary, 14);
            } else {
                draw_settings_icon_badge(fb, 24, 45, 20, NL_ICO_CAPTURE, 24U, false, th, 26, 220);
                fb_draw_text(fb, 50, 48,
                             sui->cap_cancelled ? "Generation interrompue" : "Generation terminee",
                             th->text_primary, 32);
                fb_draw_text(fb, 50, 62,
                             "Les captures deja realisees sont conservees.",
                             th->accent_secondary, 40);

                char s1[44], s2[44];
                snprintf(s1, sizeof(s1), "Captures enregistrees : %d", sui->cap_captured);
                snprintf(s2, sizeof(s2), "Jeux passes           : %d", sui->cap_skipped);
                fb_draw_text(fb, 28, 94, s1, th->text_primary, 38);
                fb_draw_text(fb, 28, 110, s2, th->text_secondary, 38);
                fb_draw_text(fb, 28, 134, "Chaque capture (.scr.tns) apparaitra en", th->text_secondary, 42);
                fb_draw_text(fb, 28, 146, "fondu lors de l'animation de lancement.", th->text_secondary, 42);

                fb_fill_rounded_rect_clipped(fb, 96, 172, 128, 24, 6, th->accent_primary, 26, 220);
                fb_draw_text(fb, 124, 180, "Valider (EXE)", th->text_on_accent, 14);
            }
            break;
        }

        case NL_UI_SETTINGS_ICONS: {
            static const char *const hints[3] = {"FLECHES Choisir", "EXE Suppr. icone", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "GESTION DES ICONES", NL_ICO_ICONS, hints, 3);

            if (state->total_games <= 0) {
                fb_fill_rounded_rect_clipped(fb, 40, 88, 240, 44, 7, th->card_bg, 26, 220);
                fb_draw_text(fb, 84, 106, "Aucun jeu detecte", th->text_secondary, 24);
            } else {
                const int cols = 3;
                const int vis_rows = 2;
                const int cw = 68;
                const int ch = 68;
                const int gap_x = 22;
                const int margin_x = 32;
                const int top_y = 33;
                const int row_stride = 90;
                const int clip_top = 28;
                const int clip_bot = 216;

                int total_rows = (state->total_games + cols - 1) / cols;
                int scroll_row = sui->icon_scroll;
                if (scroll_row < 0) scroll_row = 0;

                for (int idx = 0; idx < state->total_games; idx++) {
                    int r = idx / cols;
                    int c = idx % cols;
                    int cx = margin_x + c * (cw + gap_x);
                    int cy = top_y + (r - scroll_row) * row_stride;
                    if (cy + ch + 18 <= clip_top || cy >= clip_bot) continue;

                    bool sel = (idx == sui->icon_cursor);
                    const nl_game_t *g = &state->games[idx];

                    if (sel) {
                        fb_fill_rounded_rect_clipped(fb, cx - 2, cy - 2, cw + 4, ch + 4, 11,
                                                     th->accent_primary, clip_top, clip_bot);
                    }

                    draw_game_cover_card(fb, cx, cy, cw, ch, 9, sel ? 18 : 0, g, th, clip_top, clip_bot);

                    /* Custom icon vs Default emblem badge in top-right */
                    bool has_custom = (g->icon_source != NL_ICON_NONE);
                    uint16_t b_bg = has_custom ? th->accent_primary : th->pill_bg;
                    uint16_t b_fg = has_custom ? th->text_on_accent : th->text_secondary;
                    const char *b_txt = (g->icon_source == NL_ICON_BMP) ? "BMP" : (has_custom ? "ICO" : "DEF");
                    fb_fill_rounded_rect_clipped(fb, cx + cw - 24, cy + 3, 21, 11, 3, b_bg, clip_top, clip_bot);
                    fb_draw_text_clipped(fb, cx + cw - 22, cy + 5, b_txt, b_fg, 3, clip_top, clip_bot);

                    /* Centered Game Title below icon */
                    int max_ch = 13;
                    int tlen = (int)strlen(g->title);
                    int shown = (tlen > max_ch) ? max_ch : tlen;
                    int tx = cx + (cw - (shown * 6 - 1)) / 2;
                    fb_draw_text_ellipsis(fb, tx, cy + ch + 4, g->title,
                                          sel ? th->text_primary : th->text_secondary,
                                          max_ch, clip_top, clip_bot);
                }

                draw_scrollbar_v_smooth(fb, top_y, vis_rows * row_stride - 10,
                                        scroll_row * row_stride, row_stride,
                                        vis_rows, total_rows, th);
            }

            /* Confirmation Modal for Custom Icon Deletion */
            if (sui->icon_confirm_modal && sui->icon_cursor >= 0 && sui->icon_cursor < state->total_games) {
                int m_fp = (state->anim.modal_fp > 0) ? state->anim.modal_fp : 256;
                int m_dy = ((256 - m_fp) * 6 + 128) >> 8;
                unsigned dim_a = (unsigned)((m_fp * 14) >> 8);
                uint16_t dim_col = th->is_light ? RGB565(180, 190, 205) : RGB565(6, 8, 14);
                for (int py = 26; py < 220; py++) {
                    uint16_t *row = fb + py * NL_SCREEN_W;
                    for (int px = 0; px < NL_SCREEN_W; px++) {
                        row[px] = nl_rgb565_lerp(row[px], dim_col, dim_a);
                    }
                }

                fb_fill_rounded_rect_clipped(fb, 24, 48 + m_dy, 272, 138, 8, RGB565(220, 38, 38), 26, 220);
                fb_fill_rounded_rect_clipped(fb, 26, 50 + m_dy, 268, 134, 7, th->bg_top_bar, 26, 220);

                const nl_game_t *target_g = &state->games[sui->icon_cursor];
                fb_draw_text(fb, 38, 60 + m_dy, "Supprimer l'icone personnalisee ?", RGB565(248, 113, 113), 36);
                fb_draw_text_ellipsis(fb, 38, 76 + m_dy, target_g->title, th->text_primary, 38, 26, 220);
                fb_draw_text(fb, 38, 94 + m_dy, "Seul le fichier d'icone (.bmp/.ico)", th->text_secondary, 38);
                fb_draw_text(fb, 38, 106 + m_dy, "sera supprime. Le jeu (.tns) et sa", th->text_secondary, 38);
                fb_draw_text(fb, 38, 118 + m_dy, "capture (.scr.tns) sont conserves.", th->accent_secondary, 38);

                int btn_fp = state->anim.modal_btn_fp;
                if ((sui->icon_confirm_yes && btn_fp == 0) || (!sui->icon_confirm_yes && btn_fp == 256)) {
                    btn_fp = sui->icon_confirm_yes ? 256 : 0;
                }
                unsigned a_yes = (unsigned)((btn_fp + 4) >> 3);
                unsigned a_no = 32U - a_yes;

                fb_fill_rounded_rect_clipped(fb, 46, 144 + m_dy, 102, 24, 5,
                                             nl_rgb565_lerp(th->pill_bg, th->accent_primary, a_no), 26, 220);
                fb_draw_text(fb, 74, 152 + m_dy, "Annuler",
                             nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_no), 10);

                fb_fill_rounded_rect_clipped(fb, 172, 144 + m_dy, 102, 24, 5,
                                             nl_rgb565_lerp(th->pill_bg, RGB565(220, 38, 38), a_yes), 26, 220);
                fb_draw_text(fb, 196, 152 + m_dy, "Supprimer",
                             nl_rgb565_lerp(th->text_primary, RGB565(255, 255, 255), a_yes), 10);
            }
            break;
        }

        case NL_UI_SETTINGS_RESET: {
            static const char *const hints[3] = {"L/R Choisir", "EXE Valider", "ESC Annuler"};
            draw_settings_chrome(state, fb, th, "REINITIALISER LES REGLAGES", NL_ICO_RESET, hints, 3);

            int m_fp = (is_active_scr && state->anim.modal_fp > 0) ? state->anim.modal_fp : 256;
            int m_dy = ((256 - m_fp) * 6 + 128) >> 8;

            fb_fill_rounded_rect_clipped(fb, 16, 36 + m_dy, 288, 172, 8, th->border_subtle, 26, 220);
            fb_fill_rounded_rect_clipped(fb, 18, 38 + m_dy, 284, 168, 7, th->card_bg, 26, 220);

            draw_settings_icon_badge(fb, 28, 43 + m_dy, 18, NL_ICO_RESET, 28U, true, th, 26, 220);
            fb_draw_text(fb, 52, 48 + m_dy, "Reinitialiser les reglages ?", RGB565(248, 113, 113), 34);

            fb_draw_text(fb, 30, 68 + m_dy, "Cette operation reinitialisera uniquement", th->text_primary, 44);
            fb_draw_text(fb, 30, 80 + m_dy, "les parametres de N Launcher.", th->text_primary, 44);

            fb_draw_text(fb, 30, 98 + m_dy, "Aucun jeu, fichier, sauvegarde ou donnee", th->accent_secondary, 44);
            fb_draw_text(fb, 30, 110 + m_dy, "personnelle ne sera supprime.", th->accent_secondary, 44);

            fb_draw_text(fb, 30, 128 + m_dy, "Seules les valeurs de configuration", th->text_secondary, 44);
            fb_draw_text(fb, 30, 140 + m_dy, "seront restaurees par defaut.", th->text_secondary, 44);

            int btn_fp = state->anim.modal_btn_fp;
            if ((sui->reset_confirm_yes && btn_fp == 0) || (!sui->reset_confirm_yes && btn_fp == 256)) {
                btn_fp = sui->reset_confirm_yes ? 256 : 0;
            }
            unsigned a_yes = (unsigned)((btn_fp + 4) >> 3);
            unsigned a_no = 32U - a_yes;

            fb_fill_rounded_rect_clipped(fb, 42, 166 + m_dy, 108, 26, 6,
                                         nl_rgb565_lerp(th->pill_bg, th->accent_primary, a_no), 26, 220);
            fb_draw_text(fb, 74, 175 + m_dy, "Annuler",
                         nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_no), 8);

            fb_fill_rounded_rect_clipped(fb, 170, 166 + m_dy, 108, 26, 6,
                                         nl_rgb565_lerp(th->pill_bg, RGB565(220, 38, 38), a_yes), 26, 220);
            fb_draw_text(fb, 196, 175 + m_dy, "Confirmer",
                         nl_rgb565_lerp(th->text_primary, RGB565(255, 255, 255), a_yes), 10);
            break;
        }

        case NL_UI_SETTINGS_INFO: {
            static const char *const hints[2] = {"EXE / ESC Retour"};
            draw_settings_chrome(state, fb, th, "INFORMATIONS SYSTEME", NL_ICO_INFO, hints, 1);

            int fav_count = 0;
            for (int i = 0; i < state->total_games; i++) {
                if (state->games[i].is_favorite) fav_count++;
            }

            char ndless_buf[36];
            if (state->sys.has_ndless) {
                snprintf(ndless_buf, sizeof(ndless_buf), "r%lu (%s)",
                         (unsigned long)state->sys.ndless_rev,
                         state->sys.ndless_persistent ? "Persistant" : "Temporaire");
            } else {
                snprintf(ndless_buf, sizeof(ndless_buf), "Non detecte");
            }

            char games_buf[36];
            char fav_buf[24];
            snprintf(games_buf, sizeof(games_buf), "%d jeux (%s)",
                     state->total_games,
                     state->sys.cache_valid ? "Cache CRC OK" : "Scan direct");
            snprintf(fav_buf, sizeof(fav_buf), "%d favoris", fav_count);

            const char *labels[6] = {
                "Logiciel",
                "Version",
                "Systeme OS",
                "Ndless",
                "Bibliotheque",
                "Favoris"
            };
            const char *vals[6] = {
                "N Launcher",
                NL_LAUNCHER_VERSION_STR " (" NL_BUILD_STAMP_STR ")",
                state->sys.os_version[0] ? state->sys.os_version : "TI-Nspire OS",
                ndless_buf,
                games_buf,
                fav_buf
            };
            static const nl_set_icon_id_t info_icons[6] = {
                NL_ICO_CREDITS,
                NL_ICO_INFO,
                NL_ICO_SCREEN,
                NL_ICO_POWER,
                NL_ICO_GRID,
                NL_ICO_STATS
            };

            for (int i = 0; i < 6; i++) {
                int ry = 34 + i * 29;
                fb_fill_rounded_rect_clipped(fb, 14, ry, 292, 25, 6, th->card_bg, 26, 220);
                draw_settings_icon_badge(fb, 19, ry + 4, 17, info_icons[i], 10U, false, th, 26, 220);
                fb_draw_text(fb, 42, ry + 9, labels[i], th->text_secondary, 16);
                fb_draw_text_ellipsis(fb, 134, ry + 9, vals[i],
                                      (i == 0 || i == 1) ? th->accent_secondary : th->text_primary,
                                      27, 26, 220);
            }
            break;
        }

        case NL_UI_SETTINGS_CREDITS: {
            static const char *const hints[1] = {"EXE / ESC Retour"};
            draw_settings_chrome(state, fb, th, "A PROPOS ET CREDITS", NL_ICO_CREDITS, hints, 1);

            fb_fill_rounded_rect_clipped(fb, 28, 42, 264, 158, 9, th->border_subtle, 26, 220);
            fb_fill_rounded_rect_clipped(fb, 30, 44, 260, 154, 8, th->card_bg, 26, 220);

            draw_launcher_emblem(fb, 143, 56, 34, th);

            const char *t1 = "N Launcher";
            fb_draw_text(fb, (NL_SCREEN_W - (int)strlen(t1) * 6) / 2, 99, t1, th->text_primary, 16);

            const char *t2 = "Cree et developpe par";
            fb_draw_text(fb, (NL_SCREEN_W - (int)strlen(t2) * 6) / 2, 118, t2, th->text_secondary, 28);

            const char *t3 = "daniel.b";
            fb_draw_text(fb, (NL_SCREEN_W - (int)strlen(t3) * 6) / 2, 134, t3, th->accent_primary, 16);

            const char *t4 = "(c) 2026 - v" NL_LAUNCHER_VERSION_STR " (" NL_BUILD_STAMP_STR ")";
            fb_draw_text(fb, (NL_SCREEN_W - (int)strlen(t4) * 6) / 2, 156, t4, th->text_secondary, 40);

            const char *t5 = "TI-Nspire CX II-T  -  Ndless";
            fb_draw_text(fb, (NL_SCREEN_W - (int)strlen(t5) * 6) / 2, 174, t5, th->accent_secondary, 32);
            break;
        }

        case NL_UI_SETTINGS_PROFILES: {
            int pcount = state->profiles.count;
            switch (sui->prof_mode) {
                case NL_PROF_MODE_LIST: {
                    static const char *const hints[3] = {"UP/DN Nav.", "EXE Ouvrir", "ESC Retour"};
                    draw_settings_chrome(state, fb, th, "PROFILS UTILISATEURS", NL_ICO_PROFILES, hints, 3);

                    int total_rows = pcount + 1;
                    const int vis = 6;
                    const int row_y0 = 32;
                    const int row_h = 28;
                    const int row_gap = 2;
                    const int stride = row_h + row_gap;

                    int scroll_px, sel_y, prev_cur;
                    unsigned glow_alpha;
                    resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                              sui->prof_cursor, sui->prof_scroll,
                                              &scroll_px, &sel_y, &glow_alpha, &prev_cur);

                    uint16_t border_col = nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha);
                    fb_fill_rounded_rect_clipped(fb, 11, sel_y - 1, 294, row_h + 2, 7, border_col, 28, 218);

                    for (int row = 0; row < total_rows; row++) {
                        int ry = row_y0 + row * stride - scroll_px;
                        if (ry + row_h <= 28 || ry >= 218) continue;

                        bool sel = (row == sui->prof_cursor);
                        bool is_prev = (!sel && row == prev_cur && glow_alpha < 32U);
                        unsigned r_alpha = sel ? glow_alpha : (is_prev ? (32U - glow_alpha) : 0U);
                        fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6,
                                                     nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha), 28, 218);

                        if (row == 0) {
                            fb_fill_aa_capsule_clipped(fb, 18, ry + 4, 20, 20,
                                                       nl_rgb565_lerp(th->pill_bg, th->accent_primary, r_alpha), 28, 218);
                            uint16_t plus_c = sel ? th->text_on_accent : th->accent_secondary;
                            fb_fill_rect_clipped(fb, 27, ry + 9, 2, 10, plus_c, 28, 218);
                            fb_fill_rect_clipped(fb, 23, ry + 13, 10, 2, plus_c, 28, 218);
                            fb_draw_text_clipped(fb, 44, ry + 6, "Creer un profil", th->text_primary, 24, 28, 218);
                            fb_draw_text_clipped(fb, 44, ry + 16, "Choisir un nom et une icone", th->text_secondary, 30, 28, 218);

                            char cnt_buf[16];
                            snprintf(cnt_buf, sizeof(cnt_buf), "%d/%d", pcount, NL_MAX_PROFILES);
                            int cw = (int)strlen(cnt_buf) * 6 + 8;
                            fb_fill_rounded_rect_clipped(fb, 286 - cw, ry + 7, cw, 14, 4, th->pill_bg, 28, 218);
                            fb_draw_text_clipped(fb, 290 - cw, ry + 11, cnt_buf, th->accent_secondary, 6, 28, 218);
                        } else {
                            int pi = row - 1;
                            const nl_profile_t *p = &state->profiles.items[pi];
                            bool is_act = (pi == state->profiles.active_index);
                            if (is_act) {
                                fb_fill_aa_capsule_clipped(fb, 17, ry + 3, 22, 22, th->accent_primary, 28, 218);
                            }
                            draw_circular_avatar_box(fb, 18, ry + 4, 20, p->avatar_id, 28, 218, false, th);

                            fb_draw_text_ellipsis(fb, 44, ry + 6, p->name, th->text_primary, 22, 28, 218);
                            char sub[40];
                            if (p->is_primary) {
                                snprintf(sub, sizeof(sub), "Profil principal");
                            } else {
                                snprintf(sub, sizeof(sub), "%s", nl_avatar_category_label(nl_avatar_category_of(p->avatar_id)));
                            }
                            fb_draw_text_ellipsis(fb, 44, ry + 16, sub, th->text_secondary, 26, 28, 218);

                            if (is_act) {
                                fb_fill_rounded_rect_clipped(fb, 246, ry + 7, 38, 14, 4, th->accent_primary, 28, 218);
                                fb_draw_text_clipped(fb, 250, ry + 11, "ACTIF", th->text_on_accent, 5, 28, 218);
                            }
                            fb_draw_text_clipped(fb, 291, ry + 11, ">",
                                                 nl_rgb565_lerp(th->text_secondary, th->accent_primary, r_alpha), 1, 28, 218);
                        }
                    }
                    draw_scrollbar_v_smooth(fb, row_y0, vis * stride - row_gap,
                                            scroll_px, stride, vis, total_rows, th);
                    break;
                }

                case NL_PROF_MODE_DETAIL:
                case NL_PROF_MODE_CONFIRM_DELETE: {
                    static const char *const hints[3] = {"UP/DN Nav.", "EXE Valider", "ESC Retour"};
                    draw_settings_chrome(state, fb, th, "GESTION DU PROFIL", NL_ICO_PROFILES, hints, 3);

                    int di = sui->prof_detail_idx;
                    if (di < 0 || di >= pcount) di = 0;
                    const nl_profile_t *dp = &state->profiles.items[di];
                    bool is_act = (di == state->profiles.active_index);
                    bool can_del = (!dp->is_primary && pcount > 1);

                    /* Top Summary Card */
                    fb_fill_rounded_rect_clipped(fb, 12, 31, 296, 46, 7, th->border_subtle, 26, 220);
                    fb_fill_rounded_rect_clipped(fb, 13, 32, 294, 44, 6, th->card_bg, 26, 220);
                    if (is_act) {
                        fb_fill_aa_capsule_clipped(fb, 19, 36, 36, 36, th->accent_primary, 26, 220);
                    }
                    draw_circular_avatar_box(fb, 20, 37, 34, dp->avatar_id, 26, 220, false, th);

                    fb_draw_text_ellipsis(fb, 62, 41, dp->name, th->text_primary, 22, 26, 220);
                    char info_line[48];
                    snprintf(info_line, sizeof(info_line), "%s  |  %s",
                             dp->is_primary ? "Principal" : "Profil libre",
                             nl_avatar_category_label(nl_avatar_category_of(dp->avatar_id)));
                    fb_draw_text_ellipsis(fb, 62, 55, info_line, th->text_secondary, 34, 26, 220);
                    if (is_act) {
                        fb_fill_rounded_rect_clipped(fb, 260, 40, 38, 14, 4, th->accent_primary, 26, 220);
                        fb_draw_text_clipped(fb, 264, 44, "ACTIF", th->text_on_accent, 5, 26, 220);
                    }

                    /* 4 Management Actions */
                    static const char *const act_labels[4] = {
                        "Modifier le nom",
                        "Changer l'icone",
                        "Activer ce profil",
                        "Supprimer le profil"
                    };
                    const int row_y0 = 83;
                    const int row_h = 28;
                    const int row_gap = 4;
                    const int stride = row_h + row_gap;
                    for (int r = 0; r < 4; r++) {
                        int ry = row_y0 + r * stride;
                        bool sel = (r == sui->prof_detail_cursor);
                        bool danger = (r == 3 && can_del);
                        bool locked = (r == 3 && !can_del);
                        uint16_t b_col = sel ? (danger ? RGB565(220, 38, 38) : th->accent_primary) : th->border_subtle;
                        fb_fill_rounded_rect_clipped(fb, 12, ry - 1, 296, row_h + 2, 7, b_col, 26, 220);
                        fb_fill_rounded_rect_clipped(fb, 13, ry, 294, row_h, 6, sel ? th->card_bg_sel : th->card_bg, 26, 220);

                        uint16_t t_col = danger
                            ? RGB565(248, 113, 113)
                            : (locked ? th->text_secondary : th->text_primary);
                        fb_draw_text_clipped(fb, 24, ry + 11, act_labels[r], t_col, 24, 26, 220);

                        char rpill[20] = "";
                        if (r == 0) snprintf(rpill, sizeof(rpill), "%s", dp->name);
                        else if (r == 1) snprintf(rpill, sizeof(rpill), "#%d", dp->avatar_id + 1);
                        else if (r == 2) snprintf(rpill, sizeof(rpill), "%s", is_act ? "Actif" : "Basculer");
                        else if (r == 3) snprintf(rpill, sizeof(rpill), "%s", can_del ? "Suppr." : "Protege");

                        if (rpill[0] != '\0') {
                            int pw = (int)strlen(rpill) * 6 + 10;
                            if (pw > 106) pw = 106;
                            fb_fill_rounded_rect_clipped(fb, 284 - pw, ry + 7, pw, 14, 4,
                                                         danger ? RGB565(127, 29, 29) : th->pill_bg, 26, 220);
                            fb_draw_text_ellipsis(fb, 289 - pw, ry + 11, rpill,
                                                  danger ? RGB565(254, 202, 202) : th->accent_secondary, 15, 26, 220);
                        }
                        fb_draw_text_clipped(fb, 291, ry + 11, ">",
                                             sel ? th->accent_primary : th->text_secondary, 1, 26, 220);
                    }

                    /* Confirmation modal for safe profile deletion */
                    if (sui->prof_mode == NL_PROF_MODE_CONFIRM_DELETE) {
                        int m_fp = (state->anim.modal_fp > 0) ? state->anim.modal_fp : 256;
                        int m_dy = ((256 - m_fp) * 6 + 128) >> 8;
                        unsigned dim_a = (unsigned)((m_fp * 14) >> 8);
                        uint16_t dim_col = th->is_light ? RGB565(180, 190, 205) : RGB565(6, 8, 14);
                        for (int py = 26; py < 220; py++) {
                            uint16_t *row = fb + py * NL_SCREEN_W;
                            for (int px = 0; px < NL_SCREEN_W; px++) {
                                row[px] = nl_rgb565_lerp(row[px], dim_col, dim_a);
                            }
                        }

                        fb_fill_rounded_rect_clipped(fb, 20, 44 + m_dy, 280, 146, 8, RGB565(220, 38, 38), 26, 220);
                        fb_fill_rounded_rect_clipped(fb, 22, 46 + m_dy, 276, 142, 7, th->bg_top_bar, 26, 220);

                        fb_draw_text(fb, 34, 56 + m_dy, "Supprimer ce profil ?", RGB565(248, 113, 113), 30);
                        fb_draw_text_ellipsis(fb, 34, 72 + m_dy, dp->name, th->text_primary, 36, 26, 220);
                        fb_draw_text(fb, 34, 92 + m_dy, "Favoris, stats et raccourcis du profil", th->text_secondary, 40);
                        fb_draw_text(fb, 34, 104 + m_dy, "seront effaces et son icone liberee.", th->text_secondary, 40);
                        fb_draw_text(fb, 34, 120 + m_dy, "Les jeux (.tns) et sauvegardes restent.", th->accent_secondary, 40);

                        int btn_fp = state->anim.modal_btn_fp;
                        if ((sui->prof_delete_yes && btn_fp == 0) || (!sui->prof_delete_yes && btn_fp == 256)) {
                            btn_fp = sui->prof_delete_yes ? 256 : 0;
                        }
                        unsigned a_yes = (unsigned)((btn_fp + 4) >> 3);
                        unsigned a_no = 32U - a_yes;

                        fb_fill_rounded_rect_clipped(fb, 42, 148 + m_dy, 104, 24, 5,
                                                     nl_rgb565_lerp(th->pill_bg, th->accent_primary, a_no), 26, 220);
                        fb_draw_text(fb, 72, 156 + m_dy, "Annuler",
                                     nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_no), 10);

                        fb_fill_rounded_rect_clipped(fb, 174, 148 + m_dy, 104, 24, 5,
                                                     nl_rgb565_lerp(th->pill_bg, RGB565(220, 38, 38), a_yes), 26, 220);
                        fb_draw_text(fb, 198, 156 + m_dy, "Supprimer",
                                     nl_rgb565_lerp(th->text_primary, RGB565(255, 255, 255), a_yes), 10);
                    }
                    break;
                }

                case NL_PROF_MODE_EDIT_NAME:
                case NL_PROF_MODE_CREATE_NAME: {
                    bool is_create = (sui->prof_mode == NL_PROF_MODE_CREATE_NAME);
                    static const char *const hints[3] = {"CLAVIER Taper", "EXE Valider", "ESC Retour"};
                    draw_settings_chrome(state, fb, th,
                                         is_create ? "NOUVEAU PROFIL (1/2)" : "MODIFIER LE NOM",
                                         NL_ICO_PROFILES, hints, 3);

                    fb_fill_rounded_rect_clipped(fb, 18, 42, 284, 148, 8, th->border_subtle, 26, 220);
                    fb_fill_rounded_rect_clipped(fb, 20, 44, 280, 144, 7, th->card_bg, 26, 220);

                    fb_draw_text(fb, 34, 58, "Nom du profil (max 15 car.) :", th->text_primary, 36);

                    /* Input Field */
                    fb_fill_rounded_rect_clipped(fb, 34, 80, 252, 32, 6, th->accent_primary, 26, 220);
                    fb_fill_rounded_rect_clipped(fb, 36, 82, 248, 28, 5, th->bg_main, 26, 220);

                    char disp[NL_PROFILE_NAME_LEN + 2];
                    snprintf(disp, sizeof(disp), "%s_", sui->prof_edit_name);
                    fb_draw_text_clipped(fb, 46, 92, disp, th->text_primary, 20, 26, 220);

                    char clen_buf[16];
                    snprintf(clen_buf, sizeof(clen_buf), "%d/%d", sui->prof_edit_len, NL_PROFILE_NAME_LEN - 1);
                    fb_draw_text(fb, 252, 118, clen_buf, th->text_secondary, 8);

                    fb_draw_text(fb, 34, 136, "DEL Effacer   |   ESPACE Espace", th->text_secondary, 38);
                    fb_draw_text(fb, 34, 152,
                                 is_create ? "Etape suivante : choisir une icone" : "Appuyez sur EXE pour enregistrer",
                                 th->accent_secondary, 38);
                    break;
                }

                case NL_PROF_MODE_EDIT_AVATAR:
                case NL_PROF_MODE_CREATE_AVATAR: {
                    bool is_create = (sui->prof_mode == NL_PROF_MODE_CREATE_AVATAR);
                    static const char *const hints[3] = {"TAB Categ.", "EXE Valider", "ESC Retour"};
                    draw_settings_chrome(state, fb, th,
                                         is_create ? "NOUVEAU PROFIL (2/2)" : "CHOISIR UNE ICONE",
                                         NL_ICO_PROFILES, hints, 3);

                    int cat = sui->prof_avatar_cat;
                    if (cat < 0 || cat >= NL_AVATAR_CAT_COUNT) cat = 0;

                    /* 4 Category Tabs */
                    static const int tab_w = 72;
                    for (int c = 0; c < NL_AVATAR_CAT_COUNT; c++) {
                        int tx = 11 + c * (tab_w + 3);
                        bool csel = (c == cat);
                        fb_fill_rounded_rect_clipped(fb, tx, 30, tab_w, 16, 4,
                                                     csel ? th->accent_primary : th->pill_bg, 26, 220);
                        const char *lbl = nl_avatar_category_label(c);
                        int lx = tx + (tab_w - (int)strlen(lbl) * 6) / 2;
                        fb_draw_text_clipped(fb, lx, 35, lbl,
                                             csel ? th->text_on_accent : th->text_secondary, 11, 26, 220);
                    }

                    int cstart = 0, ccnt = 0;
                    nl_avatar_category_range(cat, &cstart, &ccnt);
                    int cols = (cat == 0) ? 8 : 5;
                    int diam = (cat == 0) ? 31 : 36;
                    int dx_step = (cat == 0) ? 37 : 52;
                    int dy_step = (cat == 0) ? 54 : 58;
                    int grid_w = (cols - 1) * dx_step + diam;
                    int start_x = (NL_SCREEN_W - grid_w) / 2;
                    int start_y = 58;

                    int excl = is_create ? -1 : sui->prof_detail_idx;

                    for (int i = 0; i < ccnt; i++) {
                        int av_id = cstart + i;
                        int r = i / cols;
                        int col_idx = i % cols;
                        int ax = start_x + col_idx * dx_step;
                        int ay = start_y + r * dy_step;
                        int acx = ax + diam / 2;
                        int acy = ay + diam / 2;
                        int owner = nl_avatar_find_owner(state, av_id, excl);
                        bool taken = (owner >= 0);
                        bool cur = (i == sui->prof_avatar_cursor);

                        if (cur) {
                            draw_multicolor_focus_ring(fb, acx, acy, diam + 2, 2,
                                                       state->anim.qp_glow_phase, th->is_light, 32U, 48, 198);
                        }
                        draw_circular_avatar_box(fb, ax, ay, diam, av_id, 48, 198, taken, th);

                        if (taken) {
                            fb_fill_rounded_rect_clipped(fb, acx - 13, ay + diam - 9, 26, 10, 3,
                                                         RGB565(180, 30, 30), 48, 198);
                            fb_draw_text_clipped(fb, acx - 11, ay + diam - 7, "PRIS",
                                                 RGB565(255, 255, 255), 4, 48, 198);
                        }
                    }

                    /* Bottom Status Strip for Hovered Avatar */
                    int hov_id = cstart + sui->prof_avatar_cursor;
                    int hov_owner = nl_avatar_find_owner(state, hov_id, excl);
                    fb_fill_rounded_rect_clipped(fb, 14, 192, 292, 20, 5, th->card_bg, 26, 220);
                    char av_info[48];
                    if (hov_owner >= 0) {
                        snprintf(av_info, sizeof(av_info), "Icone deja utilisee par : %s",
                                 state->profiles.items[hov_owner].name);
                        fb_draw_text_ellipsis(fb, 22, 198, av_info, RGB565(248, 113, 113), 44, 26, 220);
                    } else {
                        snprintf(av_info, sizeof(av_info), "Categorie %s  -  Icone disponible",
                                 nl_avatar_category_label(cat));
                        fb_draw_text_ellipsis(fb, 22, 198, av_info, th->accent_secondary, 44, 26, 220);
                    }
                    break;
                }
            }
            break;
        }

        default:
            break;
    }
}

/* Render Settings screen with smooth directional section-to-section crossfade & displacement (B) */
static void render_settings_screen_with_transitions(
    const nl_state_t *state,
    nl_ui_screen_t target_scr,
    uint16_t *fb,
    const nl_theme_t *th
) {
    bool is_managed = (state->sui.screen == state->anim.set_last_screen);
    if (is_managed &&
        state->anim.set_sec_fp > 0 &&
        state->anim.set_sec_fp < 256 &&
        state->anim.set_prev_screen != NL_UI_LIBRARY &&
        state->anim.set_prev_screen != target_scr) {

        render_settings_single_screen(state, state->anim.set_prev_screen, false, G_SET_BUF_A, th);
        render_settings_single_screen(state, target_scr, true, G_SET_BUF_B, th);

        int t = state->anim.set_sec_fp;
        int inv = 256 - t;
        int ease_fp = 256 - ((inv * inv * inv) >> 16);
        unsigned alpha = (unsigned)((ease_fp + 4) >> 3);
        if (alpha > 32U) alpha = 32U;
        unsigned inv_alpha = 32U - alpha;

        int dir = (state->anim.set_sec_dir >= 0) ? 1 : -1;
        int dx_old = -(dir * ((14 * ease_fp + 128) >> 8));
        int dx_new = dir * ((14 * (256 - ease_fp) + 128) >> 8);

        /* Top bar (0..25) and bottom bar (220..239) crossfade cleanly in place */
        for (int py = 0; py < NL_SCREEN_H; py++) {
            uint16_t *dst = fb + py * NL_SCREEN_W;
            const uint16_t *row_a = G_SET_BUF_A + py * NL_SCREEN_W;
            const uint16_t *row_b = G_SET_BUF_B + py * NL_SCREEN_W;
            if (py <= NL_TOP_BAR_H || py >= NL_BOTTOM_BAR_Y - 1) {
                for (int px = 0; px < NL_SCREEN_W; px++) {
                    dst[px] = nl_rgb565_lerp(row_a[px], row_b[px], alpha);
                }
            } else {
                for (int px = 0; px < NL_SCREEN_W; px++) {
                    int sx_a = px - dx_old;
                    int sx_b = px - dx_new;
                    uint16_t ca = ((unsigned)sx_a < NL_SCREEN_W) ? row_a[sx_a] : th->bg_main;
                    uint16_t cb = ((unsigned)sx_b < NL_SCREEN_W) ? row_b[sx_b] : th->bg_main;
                    uint16_t mix_a = nl_rgb565_lerp(th->bg_main, ca, inv_alpha);
                    dst[px] = nl_rgb565_lerp(mix_a, cb, alpha);
                }
            }
        }
    } else {
        render_settings_single_screen(state, target_scr, true, fb, th);
    }
}

static int settings_open_ease256(int open_fp) {
    if (open_fp <= 0) return 0;
    if (open_fp >= 256) return 256;
    int inv = 256 - open_fp;
    int cubic = 256 - ((inv * inv * inv) >> 16);
    return (open_fp * 77 + cubic * 179 + 128) >> 8;
}

/* Composite Settings buffer over Library buffer as an organic asymmetric liquid drop
 * (zero static 150 KB .bss field: computes distance on the fly using integer LUT) */
static void composite_settings_open_close(
    uint16_t *fb_lib,
    const uint16_t *fb_set,
    int open_fp,
    const nl_theme_t *th
) {
    if (open_fp <= 0) return;
    if (open_fp >= 256) {
        memcpy(fb_lib, fb_set, NL_FB_BYTES);
        return;
    }

    init_ink_sin_lut();

    const int cx = NL_GEAR_BTN_X + NL_GEAR_BTN_D / 2; /* 302 */
    const int cy = NL_GEAR_BTN_Y + NL_GEAR_BTN_D / 2; /* 12 */
    int dir_scale[256];
    for (int th_i = 0; th_i < 256; th_i++) {
        int m1 = G_INK_SIN256[(th_i + 29) & 255];
        int m2 = G_INK_SIN256[((th_i * 2) + 87) & 255];
        int m3 = G_INK_SIN256[((th_i * 3) + 149) & 255];
        int m5 = G_INK_SIN256[((th_i * 5) + 43) & 255];
        int bias = (m1 * 18 + m2 * 22 + m3 * 16 + m5 * 9) >> 7;
        dir_scale[th_i] = 256 + bias;
    }

    const int max_blob_val = 408;
    const int edge_w = 24;
    int ease256 = settings_open_ease256(open_fp);
    int front = ((max_blob_val + edge_w + 4) * ease256) >> 8;
    unsigned bg_dim = (unsigned)((ease256 * 8) >> 8);
    uint16_t dim_col = th->bg_main;
    uint16_t rim_col = th->accent_secondary;

    for (int py = 0; py < NL_SCREEN_H; py++) {
        int dy = py - cy;
        int dy2 = dy * dy;
        int row_off = py * NL_SCREEN_W;
        for (int px = 0; px < NL_SCREEN_W; px++) {
            int i = row_off + px;
            int dx = px - cx;
            int r = ink_isqrt32(dx * dx + dy2);
            int th256 = ink_atan2_256(dx, dy);
            int wave = (G_INK_SIN256[((px * 3 + py * 2) + 17) & 255] * 4 +
                        G_INK_SIN256[((px * 2 - py * 4) + 91) & 255] * 3) >> 7;
            int ramp = (r < 36) ? ((r * 256) / 36) : 256;
            int d = ((r * dir_scale[th256]) >> 8) + ((ramp * wave) >> 8);
            if (d < 0) d = 0;

            int delta = front - d;
            if (delta >= edge_w) {
                fb_lib[i] = fb_set[i];
            } else if (delta <= 0) {
                if (bg_dim > 0U) {
                    fb_lib[i] = nl_rgb565_lerp(fb_lib[i], dim_col, bg_dim);
                }
            } else {
                uint16_t base_px = (bg_dim > 0U) ? nl_rgb565_lerp(fb_lib[i], dim_col, bg_dim) : fb_lib[i];
                unsigned alpha = (unsigned)((delta * 32 + (edge_w >> 1)) / edge_w);
                if (alpha > 32U) alpha = 32U;
                uint16_t blended = nl_rgb565_lerp(base_px, fb_set[i], alpha);

                /* Luminous asymmetric drop frontier highlight peaking at delta == edge_w / 2 */
                int mid = edge_w / 2;
                int dist_mid = (delta >= mid) ? (delta - mid) : (mid - delta);
                int glow_a = ((mid - dist_mid) * 10) / mid;
                if (glow_a > 0) {
                    blended = nl_rgb565_lerp(blended, rim_col, (unsigned)glow_a);
                }
                fb_lib[i] = blended;
            }
        }
    }
}

static void render_library_frame_ex(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    int skip_game_idx,
    unsigned other_card_alpha_32
) {
    if (other_card_alpha_32 > 32U) other_card_alpha_32 = 32U;

    /* 1. Clear 320x240 screen */
    fb_fill_rect_clipped(fb, 0, 0, NL_SCREEN_W, NL_SCREEN_H, th->bg_main, 0, NL_SCREEN_H);

    /* 2. Top Header Bar (y = 0..25): Active Profile Avatar + 3 categories with sliding pill + gear */
    fb_fill_rect_clipped(fb, 0, 0, NL_SCREEN_W, NL_TOP_BAR_H, th->bg_top_bar, 0, NL_SCREEN_H);
    fb_fill_rect_clipped(fb, 0, NL_TOP_BAR_H, NL_SCREEN_W, 1, th->border_subtle, 0, NL_SCREEN_H);

    {
        const nl_profile_t *ap = nl_profile_get_active(state);
        int av_id = ap ? ap->avatar_id : 0;
        fb_fill_aa_capsule_clipped(fb, 9, 4, 17, 17, th->accent_primary, 0, NL_TOP_BAR_H);
        draw_circular_avatar_box(fb, 10, 5, 15, av_id, 0, NL_TOP_BAR_H, false, th);
    }

    static const int k_tab_x[NL_CAT_COUNT] = { 34, 76, 112 };
    static const int k_tab_w[NL_CAT_COUNT] = { 36, 30, 30 };

    for (int c = 0; c < NL_CAT_COUNT; c++) {
        fb_fill_rounded_rect_clipped(fb, k_tab_x[c], 5, k_tab_w[c], 15, 4, th->pill_bg, 0, NL_SCREEN_H);
    }

    int ind_x = (state->anim.cat_ind_w_fp > 0) ? (state->anim.cat_ind_x_fp >> 4) : k_tab_x[state->active_category];
    int ind_w = (state->anim.cat_ind_w_fp > 0) ? (state->anim.cat_ind_w_fp >> 4) : k_tab_w[state->active_category];
    fb_fill_rounded_rect_clipped(fb, ind_x, 5, ind_w, 15, 4, th->accent_primary, 0, NL_SCREEN_H);

    for (int c = 0; c < NL_CAT_COUNT; c++) {
        nl_category_t cat_id = (nl_category_t)c;
        const char *label = nl_category_label(cat_id);
        int tx0 = k_tab_x[c];
        int tx1 = tx0 + k_tab_w[c];
        int ix0 = ind_x;
        int ix1 = ind_x + ind_w;
        int ov = (tx1 < ix1 ? tx1 : ix1) - (tx0 > ix0 ? tx0 : ix0);
        if (ov < 0) ov = 0;
        unsigned txt_a = (unsigned)((ov * 32) / k_tab_w[c]);
        if (txt_a > 32U) txt_a = 32U;
        uint16_t tcol = nl_rgb565_lerp(th->text_secondary, th->text_on_accent, txt_a);
        fb_draw_text(fb, tx0 + 6, 9, label, tcol, 6);
    }

    /* Top-right round gear icon button */
    draw_gear_button(fb, NL_GEAR_BTN_X, NL_GEAR_BTN_Y, NL_GEAR_BTN_D, th, false);

    /* 3. Inline Library Search Bar + Vertically-Scrolled Clipped Cover Library */
    nl_layout_metrics_t m;
    get_interpolated_layout_metrics(state, &m);
    const int view_bottom = m.view_bottom;
    int names_fp = state->anim.switch_shownames_fp;
    if (state->sui.screen == NL_UI_LIBRARY && ((state->settings.show_names && names_fp == 0) || (!state->settings.show_names && names_fp == 256))) {
        names_fp = state->settings.show_names ? 256 : 0;
    }

    int search_fp = state->anim.search_bar_fp;
    if (state->sui.search_active && search_fp == 0) {
        search_fp = 256;
    } else if (!state->sui.search_active && search_fp == 256 && state->sui.screen == NL_UI_LIBRARY) {
        search_fp = 0;
    }
    int search_h = (28 * search_fp + 128) >> 8;

    int cat_dx = 0;
    if (state->anim.cat_trans_fp > 0 && state->anim.cat_trans_fp < 256) {
        int inv = 256 - state->anim.cat_trans_fp;
        int ease = 256 - ((inv * inv * inv) >> 16);
        int dir = (state->anim.cat_trans_dir >= 0) ? 1 : -1;
        cat_dx = dir * (((256 - ease) * 18 + 128) >> 8);
    }

    if (search_h > 4) {
        int sb_y = NL_VIEW_TOP + 2 + search_h - 28;
        fb_fill_rounded_rect_clipped(fb, 10, sb_y, 300, 22, 6, th->accent_primary, NL_VIEW_TOP, view_bottom);
        fb_fill_rounded_rect_clipped(fb, 11, sb_y + 1, 298, 20, 5, th->bg_top_bar, NL_VIEW_TOP, view_bottom);
        draw_settings_icon_11x11(fb, 17, sb_y + 5, NL_ICO_SEARCH, th->accent_secondary, NL_VIEW_TOP, view_bottom);

        char q_disp[NL_MAX_SEARCH_LEN + 4];
        if (state->sui.search_len > 0) {
            snprintf(q_disp, sizeof(q_disp), "%s_", state->sui.search_query);
            fb_draw_text_ellipsis(fb, 33, sb_y + 7, q_disp, th->text_primary, 32, NL_VIEW_TOP, view_bottom);
        } else {
            fb_draw_text_clipped(fb, 33, sb_y + 7, "Recherche...", th->text_secondary, 20, NL_VIEW_TOP, view_bottom);
        }

        char cnt_buf[12];
        snprintf(cnt_buf, sizeof(cnt_buf), "%d", state->filtered_count);
        int cw = (int)strlen(cnt_buf) * 6;
        fb_fill_rounded_rect_clipped(fb, 278 - cw - 8, sb_y + 4, cw + 8, 14, 4, th->pill_bg, NL_VIEW_TOP, view_bottom);
        fb_draw_text_clipped(fb, 278 - cw - 4, sb_y + 7, cnt_buf, th->accent_secondary, 5, NL_VIEW_TOP, view_bottom);
        fb_draw_text_clipped(fb, 284, sb_y + 7, "ESC", th->text_secondary, 3, NL_VIEW_TOP, view_bottom);
    }

    int grid_clip_top = NL_VIEW_TOP + search_h;
    if (grid_clip_top > view_bottom) grid_clip_top = view_bottom;

    if (state->filtered_count > 0) {
        int scroll_y = state->anim.scroll_y_fp >> 4;
        int s_diff = state->anim.target_scroll_y_fp - state->anim.scroll_y_fp;
        if (s_diff < 0) s_diff = -s_diff;
        bool v_settled = (s_diff <= 16);
        int sel_game_idx = state->filtered_indices[state->selected_pos];

        for (int pass = 0; pass < 2; pass++) {
            for (int f_idx = 0; f_idx < state->filtered_count; f_idx++) {
                int game_idx = state->filtered_indices[f_idx];
                if (skip_game_idx >= 0 && game_idx == skip_game_idx) continue;
                bool is_sel_pass = (pass == 1);
                if ((game_idx == sel_game_idx) != is_sel_pass) continue;
                if (!is_sel_pass && other_card_alpha_32 == 0U) continue;

                const nl_game_t *g = &state->games[game_idx];
                int row = f_idx / m.cols;

                int glide_dx = (skip_game_idx < 0 && !state->anim.launch_anim_active)
                    ? (state->anim.card_dx_fp[game_idx] >> 4)
                    : 0;
                int glide_dy = (skip_game_idx < 0 && !state->anim.launch_anim_active)
                    ? (state->anim.card_dy_fp[game_idx] >> 4)
                    : 0;
                int slot_x = 0, slot_y = 0;
                nl_compute_grid_slot_pos(&m, state->filtered_count, f_idx, &slot_x, &slot_y);
                int base_x = slot_x + cat_dx + glide_dx;
                int base_y = slot_y + search_h - scroll_y + glide_dy;

                if (base_y + m.card_h + m.label_h + 12 < grid_clip_top || base_y - 12 >= view_bottom) {
                    continue;
                }

                int scale_fp = state->anim.card_scale_fp[game_idx];
                if (scale_fp < NL_SCALE_NORMAL) scale_fp = NL_SCALE_NORMAL;
                if (is_sel_pass && state->anim.launch_holding && state->anim.launch_shrink_fp > 0) {
                    scale_fp = state->anim.launch_shrink_fp;
                }

                int draw_w = (m.card_w * scale_fp + 128) >> 8;
                int draw_h = (m.card_h * scale_fp + 128) >> 8;
                int draw_x = base_x - (draw_w - m.card_w) / 2;
                int draw_y = base_y - (draw_h - m.card_h) / 2;

                if (v_settled && search_h == 0 && draw_y < NL_VIEW_TOP && row == state->current_page * m.rows) {
                    draw_y = NL_VIEW_TOP + 1;
                }

                int hl_fp = 0;
                if (!state->anim.launch_anim_active && !state->anim.launch_holding && scale_fp > NL_SCALE_NORMAL) {
                    hl_fp = ((scale_fp - NL_SCALE_NORMAL) * 32) / (NL_SCALE_SELECTED - NL_SCALE_NORMAL);
                    if (hl_fp > 32) hl_fp = 32;
                }

                int box_x = draw_x, box_y = draw_y, box_w = draw_w, box_h = draw_h;
                if (g->icon_source != NL_ICON_NONE && g->icon_pixels != NULL) {
                    int iw = (g->icon_w > 0) ? (int)g->icon_w : NL_COVER_W;
                    int ih = (g->icon_h > 0) ? (int)g->icon_h : NL_COVER_H;
                    int ox = 0, oy = 0;
                    compute_icon_contain_box(iw, ih, draw_w, draw_h, &ox, &oy, &box_w, &box_h);
                    box_x = draw_x + ox;
                    box_y = draw_y + oy;
                }

                if (hl_fp > 0 && other_card_alpha_32 >= 32U) {
                    fb_rounded_soft_luminance_clipped(fb, box_x, box_y, box_w, box_h,
                                                      m.radius, hl_fp, th->is_light,
                                                      grid_clip_top, view_bottom);
                }

                if (is_sel_pass && scale_fp > NL_SCALE_NORMAL && other_card_alpha_32 >= 32U) {
                    fb_rounded_shadow_clipped(fb, box_x + 2, box_y + 3, box_w, box_h,
                                              m.radius, th->is_light, grid_clip_top, view_bottom);
                }

                draw_game_cover_card(
                    fb, draw_x, draw_y, draw_w, draw_h, m.radius, hl_fp,
                    g, th, grid_clip_top, view_bottom
                );

                /* Progressive fade-out of non-selected cards into bg_main during game launch */
                if (!is_sel_pass && other_card_alpha_32 < 32U) {
                    int fy0 = (draw_y < grid_clip_top) ? grid_clip_top : draw_y;
                    int fy1 = (draw_y + draw_h > view_bottom) ? view_bottom : (draw_y + draw_h);
                    int fx0 = (draw_x < 0) ? 0 : draw_x;
                    int fx1 = (draw_x + draw_w > NL_SCREEN_W) ? NL_SCREEN_W : (draw_x + draw_w);
                    for (int py = fy0; py < fy1; py++) {
                        uint16_t *row_px = fb + py * NL_SCREEN_W;
                        for (int px = fx0; px < fx1; px++) {
                            if (row_px[px] != th->bg_main) {
                                row_px[px] = nl_rgb565_lerp(th->bg_main, row_px[px], other_card_alpha_32);
                            }
                        }
                    }
                }

                /* Optional compact centered game title below icon (§3) with progressive fade */
                if (names_fp > 0 && m.label_h > 0) {
                    unsigned name_a = (unsigned)((names_fp + 4) >> 3);
                    if (name_a > 32U) name_a = 32U;
                    if (!is_sel_pass && other_card_alpha_32 < 32U) {
                        name_a = (name_a * other_card_alpha_32 + 16U) >> 5;
                    }
                    if (name_a > 0U) {
                        uint16_t raw_tcol = is_sel_pass ? th->text_primary : th->text_secondary;
                        uint16_t tcol = nl_rgb565_lerp(th->bg_main, raw_tcol, name_a);
                        int label_max_w = (m.cols == 1)
                            ? (m.card_w + 48)
                            : (m.card_w + (m.gap_x > 8 ? (m.gap_x - 6) : 2));
                        int max_ch = label_max_w / 6;
                        if (max_ch < 4) max_ch = 4;
                        int tlen = (int)strlen(g->title);
                        int shown_len = (tlen > max_ch) ? max_ch : tlen;
                        int text_px_w = (shown_len > 0) ? (shown_len * 6 - 1) : 0;
                        int tx = draw_x + (draw_w - text_px_w) / 2;
                        int ty = draw_y + draw_h + 1;
                        if (tx < 2) tx = 2;
                        fb_draw_text_ellipsis(
                            fb, tx, ty, g->title, tcol,
                            max_ch, grid_clip_top, view_bottom
                        );
                    }
                }
            }
        }
    } else {
        int empty_y = (grid_clip_top + view_bottom - 48) / 2;
        fb_fill_rounded_rect_clipped(fb, 48 + cat_dx, empty_y, 224, 48, 7, th->card_bg, grid_clip_top, view_bottom);
        const char *msg = state->sui.search_active
            ? "Aucun jeu trouve (ESC)"
            : ((state->active_category == NL_CAT_FAVORITES)
                ? "AUCUN FAVORI  (VAR)"
                : "AUCUN JEU RECENT");
        int mw = (int)strlen(msg) * 6;
        fb_draw_text_clipped(fb, (NL_SCREEN_W - mw) / 2 + cat_dx, empty_y + 20, msg, th->text_secondary, 32, grid_clip_top, view_bottom);
    }

    /* 4. Smart Adaptive Pagination (Circular dots while all fit <= 19, compact dashes when > 19) */
    {
        int total_pages = (state->total_pages >= 1) ? state->total_pages : 1;
        int cur_page = state->current_page;
        if (cur_page < 0) cur_page = 0;
        if (cur_page >= total_pages) cur_page = total_pages - 1;

        int anim_fp = state->anim.dot_pos_fp;
        int max_fp = (total_pages - 1) << 4;
        if (anim_fp < 0) anim_fp = 0;
        if (anim_fp > max_fp) anim_fp = max_fp;
        if (total_pages <= 1) anim_fp = 0;

        int base_idx = anim_fp >> 4;
        int rem_fp = anim_fp & 15;

        if (total_pages <= NL_PAGINATION_MAX_DOTS) {
            /* Mode 1: Circular Gray Dots & Rounded Active Capsule */
            const int dot_d = 8;
            const int cap_w = 18;
            const int gap = 6;
            const int extra_total = cap_w - dot_d;
            int extra_right = (extra_total * rem_fp + 8) >> 4;
            int extra_left = extra_total - extra_right;

            int total_w = (total_pages - 1) * (dot_d + gap) + cap_w;
            int cur_x = (NL_SCREEN_W - total_w) / 2;

            for (int i = 0; i < total_pages; i++) {
                int pw = dot_d;
                unsigned alpha_col = 0U;
                if (i == base_idx) {
                    pw += extra_left;
                    alpha_col = (unsigned)((16 - rem_fp) * 2);
                } else if (rem_fp > 0 && i == base_idx + 1) {
                    pw += extra_right;
                    alpha_col = (unsigned)(rem_fp * 2);
                }
                uint16_t dcol = nl_rgb565_lerp(th->dot_inactive, th->accent_primary, alpha_col);
                fb_fill_aa_capsule_clipped(fb, cur_x, m.pagination_y, pw, dot_d, dcol, 0, NL_SCREEN_H);
                cur_x += pw + gap;
            }
        } else {
            /* Mode 2: Compact, evenly spaced horizontal dashes inside safe [20..300] margins */
            const int max_row_w = 280;
            int active_extra = (total_pages <= 36) ? 6 : ((total_pages <= 64) ? 4 : 2);
            int gap = (total_pages <= 32) ? 4 : ((total_pages <= 56) ? 3 : ((total_pages <= 84) ? 2 : 1));
            int avail = max_row_w - (total_pages - 1) * gap - active_extra;
            int dash_w = (total_pages > 0) ? (avail / total_pages) : 4;
            if (dash_w < 1) dash_w = 1;
            if (dash_w > 10) dash_w = 10;

            int total_w = total_pages * dash_w + (total_pages - 1) * gap + active_extra;
            if (total_w > max_row_w && gap > 1) {
                gap--;
                total_w = total_pages * dash_w + (total_pages - 1) * gap + active_extra;
            }
            int cur_x = (NL_SCREEN_W - total_w) / 2;
            int extra_right = (active_extra * rem_fp + 8) >> 4;
            int extra_left = active_extra - extra_right;

            for (int i = 0; i < total_pages; i++) {
                int pw = dash_w;
                unsigned alpha_col = 0U;
                if (i == base_idx) {
                    pw += extra_left;
                    alpha_col = (unsigned)((16 - rem_fp) * 2);
                } else if (rem_fp > 0 && i == base_idx + 1) {
                    pw += extra_right;
                    alpha_col = (unsigned)(rem_fp * 2);
                }
                int dh = (alpha_col >= 16U) ? 5 : ((alpha_col >= 6U) ? 4 : 3);
                int dy = m.pagination_y + (8 - dh) / 2;
                uint16_t dcol = nl_rgb565_lerp(th->dot_inactive, th->accent_primary, alpha_col);
                if (pw <= 2) {
                    fb_fill_rect_clipped(fb, cur_x, dy, pw, dh, dcol, 0, NL_SCREEN_H);
                } else {
                    fb_fill_aa_capsule_clipped(fb, cur_x, dy, pw, dh, dcol, 0, NL_SCREEN_H);
                }
                cur_x += pw + gap;
            }
        }
    }

    /* 5. Bottom Centered Shortcut Bar (smoothly hidden when show_shortcuts == false) */
    if (state->anim.switch_shortcuts_fp > 0) {
        int bar_y = NL_SCREEN_H - ((NL_BOTTOM_BAR_H * state->anim.switch_shortcuts_fp + 128) >> 8);
        if (bar_y < NL_BOTTOM_BAR_Y) bar_y = NL_BOTTOM_BAR_Y;
        if (bar_y < NL_SCREEN_H) {
            fb_fill_rect_clipped(fb, 0, bar_y - 1, NL_SCREEN_W, 1, th->border_subtle, 0, NL_SCREEN_H);
            fb_fill_rect_clipped(fb, 0, bar_y, NL_SCREEN_W, NL_SCREEN_H - bar_y, th->bg_bottom_bar, 0, NL_SCREEN_H);
            draw_centered_shortcuts_at_y(fb, bar_y, th);
        }
    }
}

static void render_library_frame(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    render_library_frame_ex(state, fb, th, -1, 32U);
}

/* Spring rebound curve helper: maps progress p (0..256) to scale factor (0..288, peaking at 288 (~1.125x) around p=152 and settling to 256 at p>=196) */
static int profile_spring_rebound_fp(int p) {
    if (p <= 0) return 0;
    if (p < 128) {
        /* Smooth ease-out acceleration 0 -> 256 */
        int inv = 128 - p;
        return 256 - ((inv * inv) >> 6);
    }
    if (p < 160) {
        /* Overshoot 256 -> 288 */
        return 256 + (((p - 128) * 32) >> 5);
    }
    if (p < 196) {
        /* Settle 288 -> 256 */
        return 288 - (((p - 160) * 32) / 36);
    }
    return 256;
}

/* 1. 4-Stage Profile Switch Transition (Water-Drop + 4 Ghost Silhouettes + Spring Rebound + Smooth Fade) */
static void render_profile_switch_transition(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    if (!state->anim.prof_switch_active || state->anim.prof_switch_max <= 0) return;

    int tick = state->anim.prof_switch_tick;
    int max_t = state->anim.prof_switch_max;
    int p = (tick * 256) / max_t;
    if (p < 0) p = 0;
    if (p > 256) p = 256;

    /* Soft backdrop veil during zoom & rebound, fading out cleanly in Stage 4 */
    unsigned veil_a = (p < 180)
        ? (unsigned)((p * 14) / 180)
        : (unsigned)(((256 - p) * 14) / 76);
    if (veil_a > 14U) veil_a = 14U;
    if (veil_a > 0U) {
        uint16_t veil_col = th->is_light ? RGB565(185, 195, 210) : RGB565(6, 9, 15);
        for (int i = NL_TOP_BAR_H * NL_SCREEN_W; i < NL_FB_PIXELS; i++) {
            fb[i] = nl_rgb565_lerp(fb[i], veil_col, veil_a);
        }
    }

    int orig_x = state->anim.prof_switch_origin_x;
    int orig_y = state->anim.prof_switch_origin_y;
    int orig_d = (state->anim.prof_switch_origin_d >= 14) ? state->anim.prof_switch_origin_d : 36;
    const int tgt_x = 160;
    const int tgt_y = 112;
    const int tgt_d = 84;
    int av_id = state->anim.prof_switch_avatar_id;

    /* Stage 1: Luminous water-drop waves radiating from the icon's origin and following its motion */
    int move_t = (p < 140) ? ((p * 256) / 140) : 256;
    int cur_cx = orig_x + (((tgt_x - orig_x) * move_t) >> 8);
    int cur_cy = orig_y + (((tgt_y - orig_y) * move_t) >> 8);
    draw_profile_water_drop_waves(fb, orig_x, orig_y, p, 118, th, NL_TOP_BAR_H, NL_SCREEN_H);
    if (p > 40) {
        draw_profile_water_drop_waves(fb, cur_cx, cur_cy, p - 32, 96, th, NL_TOP_BAR_H, NL_SCREEN_H);
    }

    /* Master fade for Stage 4 (p in 192..256) */
    unsigned main_alpha = (p <= 192) ? 32U : (unsigned)(((256 - p) * 32) / 64);
    if (main_alpha == 0U) return;

    /* Stage 2: 4 Sequential Ghost Silhouettes trailing behind the advancing & zooming avatar */
    static const uint8_t k_ghost_lag[4]   = { 18, 36, 54, 72 };
    static const uint8_t k_ghost_alpha[4] = { 19, 13,  9,  5 };
    if (p < 188) {
        unsigned ghost_fade = (p <= 132) ? 32U : (unsigned)(((188 - p) * 32) / 56);
        for (int g = 3; g >= 0; g--) {
            int gp = p - (int)k_ghost_lag[g];
            if (gp <= 0) continue;
            int g_move = (gp < 140) ? ((gp * 256) / 140) : 256;
            int gx = orig_x + (((tgt_x - orig_x) * g_move) >> 8);
            int gy = orig_y + (((tgt_y - orig_y) * g_move) >> 8) + (g + 1) * 3;
            int g_spring = profile_spring_rebound_fp(gp);
            int gd = orig_d + (((tgt_d - orig_d) * g_spring) >> 8) - (g + 1) * 5;
            if (gd < 14) gd = 14;
            unsigned ga = ((unsigned)k_ghost_alpha[g] * ghost_fade) >> 5;
            if (ga > 0U) {
                draw_circular_avatar_box_alpha(fb, gx - gd / 2, gy - gd / 2, gd, av_id,
                                               NL_TOP_BAR_H, NL_SCREEN_H, ga,
                                               th->accent_primary, (unsigned)(4 + g * 2));
            }
        }
    }

    /* Stage 3 & 4: Main Avatar with Natural Spring Rebound + Luminous Halo + Fade Out */
    int spring_fp = profile_spring_rebound_fp(p);
    int main_d = orig_d + (((tgt_d - orig_d) * spring_fp) >> 8);
    if (p > 192) {
        main_d += ((p - 192) * 8) / 64;
    }

    draw_multicolor_focus_ring(fb, cur_cx, cur_cy, main_d + 2, 3,
                               state->anim.qp_glow_phase, th->is_light, main_alpha,
                               NL_TOP_BAR_H, NL_SCREEN_H);
    draw_circular_avatar_box_alpha(fb, cur_cx - main_d / 2, cur_cy - main_d / 2, main_d, av_id,
                                   NL_TOP_BAR_H, NL_SCREEN_H, main_alpha, 0, 0U);

    /* Profile name pill beneath the rebounding avatar */
    const nl_profile_t *ap = nl_profile_get_active(state);
    if (ap && p >= 64 && main_alpha >= 8U) {
        int lw = (int)strlen(ap->name) * 6 + 16;
        int px = cur_cx - lw / 2;
        int py = cur_cy + main_d / 2 + 8;
        if (py + 16 < NL_SCREEN_H) {
            fb_fill_aa_capsule_clipped(fb, px - 1, py - 1, lw + 2, 16, th->accent_primary, NL_TOP_BAR_H, NL_SCREEN_H);
            fb_fill_aa_capsule_clipped(fb, px, py, lw, 14, th->card_bg_sel, NL_TOP_BAR_H, NL_SCREEN_H);
            fb_draw_text_clipped(fb, cur_cx - ((int)strlen(ap->name) * 6) / 2, py + 4,
                                 ap->name, th->text_primary, 16, NL_TOP_BAR_H, NL_SCREEN_H);
        }
    }
}

/* 2. '+' Button Sequential Ghost Zoom & Rebound Transition (played when opening Avatar Selection) */
static void render_profile_plus_transition(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    if (!state->anim.prof_plus_active || state->anim.prof_plus_max <= 0) return;

    int tick = state->anim.prof_plus_tick;
    int max_t = state->anim.prof_plus_max;
    int p = (tick * 256) / max_t;
    if (p < 0 || p > 256) return;

    int orig_x = state->anim.prof_plus_origin_x;
    int orig_y = state->anim.prof_plus_origin_y;
    const int tgt_x = 160;
    const int tgt_y = 114;
    const int orig_d = 36;
    const int tgt_d = 76;

    draw_profile_water_drop_waves(fb, orig_x, orig_y, p, 110, th, NL_TOP_BAR_H, NL_SCREEN_H);

    unsigned main_alpha = (p <= 176) ? 32U : (unsigned)(((256 - p) * 32) / 80);
    if (main_alpha == 0U) return;

    int move_t = (p < 136) ? ((p * 256) / 136) : 256;
    int cur_cx = orig_x + (((tgt_x - orig_x) * move_t) >> 8);
    int cur_cy = orig_y + (((tgt_y - orig_y) * move_t) >> 8);

    /* 4 Translucent Ghost Silhouettes of the expanding & rotating '+' badge */
    static const uint8_t k_lag[4]   = { 18, 36, 54, 72 };
    static const uint8_t k_alpha[4] = { 18, 13,  8,  4 };
    for (int g = 3; g >= 0; g--) {
        int gp = p - (int)k_lag[g];
        if (gp <= 0) continue;
        int gm = (gp < 136) ? ((gp * 256) / 136) : 256;
        int gx = orig_x + (((tgt_x - orig_x) * gm) >> 8);
        int gy = orig_y + (((tgt_y - orig_y) * gm) >> 8) + (g + 1) * 3;
        int gs = profile_spring_rebound_fp(gp);
        int gd = orig_d + (((tgt_d - orig_d) * gs) >> 8) - (g + 1) * 4;
        unsigned ga = ((unsigned)k_alpha[g] * main_alpha) >> 5;
        draw_plus_badge_alpha(fb, gx, gy, gd, gp, ga, false, true, th, NL_TOP_BAR_H, NL_SCREEN_H);
    }

    int spring_fp = profile_spring_rebound_fp(p);
    int main_d = orig_d + (((tgt_d - orig_d) * spring_fp) >> 8);
    draw_multicolor_focus_ring(fb, cur_cx, cur_cy, main_d + 2, 2,
                               state->anim.qp_glow_phase, th->is_light, main_alpha,
                               NL_TOP_BAR_H, NL_SCREEN_H);
    draw_plus_badge_alpha(fb, cur_cx, cur_cy, main_d, p, main_alpha, false, true, th, NL_TOP_BAR_H, NL_SCREEN_H);
}

/* 3. New User Avatar Validation & Creation Confirmation Animation */
static void render_profile_birth_transition(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    int dest_cx,
    int dest_cy
) {
    if (!state->anim.prof_birth_active || state->anim.prof_birth_max <= 0) return;

    int tick = state->anim.prof_birth_tick;
    int max_t = state->anim.prof_birth_max;
    int p = (tick * 256) / max_t;
    if (p < 0 || p > 256) return;

    int av_id = state->anim.prof_birth_avatar_id;
    draw_profile_water_drop_waves(fb, dest_cx, dest_cy, p, 92, th, NL_TOP_BAR_H, NL_SCREEN_H);

    if (p < 176) {
        int move_t = (p * 256) / 176;
        int cur_cx = 160 + (((dest_cx - 160) * move_t) >> 8);
        int cur_cy = 96  + (((dest_cy - 96)  * move_t) >> 8);
        int cur_d  = 66  - (((66 - 40) * move_t) >> 8);
        unsigned a = (p <= 128) ? 28U : (unsigned)(((176 - p) * 28) / 48);

        /* 3 Ghost Silhouettes trailing into the new user's slot */
        for (int g = 2; g >= 0; g--) {
            int gp = move_t - (g + 1) * 32;
            if (gp <= 0) continue;
            int gx = 160 + (((dest_cx - 160) * gp) >> 8);
            int gy = 96  + (((dest_cy - 96)  * gp) >> 8);
            int gd = 66  - (((66 - 40) * gp) >> 8);
            unsigned ga = (a * (unsigned)(14 - g * 4)) >> 5;
            draw_circular_avatar_box_alpha(fb, gx - gd / 2, gy - gd / 2, gd, av_id,
                                           NL_TOP_BAR_H, NL_SCREEN_H, ga, th->accent_primary, 6U);
        }

        draw_multicolor_focus_ring(fb, cur_cx, cur_cy, cur_d + 2, 2,
                                   state->anim.qp_glow_phase, th->is_light, a,
                                   NL_TOP_BAR_H, NL_SCREEN_H);
        draw_circular_avatar_box_alpha(fb, cur_cx - cur_d / 2, cur_cy - cur_d / 2, cur_d, av_id,
                                       NL_TOP_BAR_H, NL_SCREEN_H, a, 0, 0U);
    }
}

/* Quick Profile Selector Overlay (Library only — STRICTLY browse/switch/'+' only, ZERO edit/delete) */
static void render_quick_profile_overlay(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    int m_fp = state->anim.qp_open_fp;
    if (m_fp <= 0 && !state->sui.qp_open) return;
    if (m_fp <= 0) m_fp = 256;
    if (m_fp > 256) m_fp = 256;

    unsigned alpha_32 = (unsigned)((m_fp + 4) >> 3);
    if (alpha_32 > 32U) alpha_32 = 32U;

    unsigned dim_a = (unsigned)((m_fp * 17) >> 8);
    uint16_t dim_col = th->is_light ? RGB565(175, 185, 200) : RGB565(5, 7, 12);
    for (int i = 0; i < NL_FB_PIXELS; i++) {
        fb[i] = nl_rgb565_lerp(fb[i], dim_col, dim_a);
    }

    /* Subtle panel zoom + vertical slide on open/close */
    int dy = ((256 - m_fp) * 12 + 128) >> 8;
    int zoom_inset_x = ((256 - m_fp) * 14 + 128) >> 8;
    int zoom_inset_y = ((256 - m_fp) * 6 + 128) >> 8;
    int mx = 10 + zoom_inset_x;
    int my = 52 + dy + zoom_inset_y;
    int mw = 300 - zoom_inset_x * 2;
    int mh = 136 - zoom_inset_y * 2;

    uint16_t box_bg = th->is_light ? RGB565(250, 252, 255) : th->card_bg;
    uint16_t border_col = (m_fp < 240)
        ? nl_rgb565_lerp(th->accent_primary, th->border_subtle, (unsigned)((m_fp * 32) >> 8))
        : th->border_subtle;
    fb_fill_rounded_rect_clipped(fb, mx - 1, my - 1, mw + 2, mh + 2, 11, border_col, 0, NL_SCREEN_H);
    fb_fill_rounded_rect_clipped(fb, mx, my, mw, mh, 10, box_bg, 0, NL_SCREEN_H);

    /* Luminous top accent sweep during panel opening */
    if (m_fp < 252) {
        int sweep_w = (mw * m_fp) >> 8;
        int sweep_x = 160 - sweep_w / 2;
        fb_fill_rect_clipped(fb, sweep_x, my, sweep_w, 2, th->accent_primary, 0, NL_SCREEN_H);
    }

    fb_draw_text_clipped(fb, mx + 14, my + 12, "PROFILS", th->text_primary, 12, 0, NL_SCREEN_H);
    char cnt_buf[14];
    snprintf(cnt_buf, sizeof(cnt_buf), "%d/%d", state->profiles.count, NL_MAX_PROFILES);
    int cw = (int)strlen(cnt_buf) * 6 + 10;
    fb_fill_rounded_rect_clipped(fb, mx + mw - 12 - cw, my + 10, cw, 14, 4, th->pill_bg, 0, NL_SCREEN_H);
    fb_draw_text_clipped(fb, mx + mw - 7 - cw, my + 14, cnt_buf, th->accent_secondary, 8, 0, NL_SCREEN_H);

    int pcount = (state->profiles.count > 0) ? state->profiles.count : 1;
    int total_slots = pcount + 1;
    const int vis_max = 6;
    const int diam = 34;
    const int stride = 44;
    int cur = state->sui.qp_cursor;
    if (cur < 0) cur = 0;
    if (cur >= total_slots) cur = total_slots - 1;

    int shown = (total_slots < vis_max) ? total_slots : vis_max;
    int win_start = 0;
    if (total_slots > vis_max) {
        win_start = cur - (vis_max / 2);
        if (win_start < 0) win_start = 0;
        if (win_start > total_slots - vis_max) win_start = total_slots - vis_max;
    }
    int strip_w = shown * diam + (shown - 1) * (stride - diam);
    int start_x = 160 - strip_w / 2;
    int cy = 114 + dy;

    if (win_start > 0) {
        fb_draw_text(fb, mx + 6, cy - 3, "<", th->text_secondary, 1);
    }
    if (win_start + shown < total_slots) {
        fb_draw_text(fb, mx + mw - 12, cy - 3, ">", th->text_secondary, 1);
    }

    int rel = cur - win_start;
    int target_gx = start_x + rel * stride + diam / 2;
    int glow_cx = (state->anim.qp_glow_x_fp > 0) ? (state->anim.qp_glow_x_fp >> 4) : target_gx;
    int trail_cx = (state->anim.qp_trail_x_fp > 0) ? (state->anim.qp_trail_x_fp >> 4) : glow_cx;
    int glow_cy = ((state->anim.qp_glow_y_fp > 0) ? (state->anim.qp_glow_y_fp >> 4) : 114) + dy;

    /* Diffused ghost glow wake when navigating between profiles */
    int wake_dx = glow_cx - trail_cx;
    if (abs(wake_dx) >= 3) {
        int mid_wx = (glow_cx + trail_cx) / 2;
        draw_multicolor_focus_ring(fb, trail_cx, glow_cy, 38, 2,
                                   (uint16_t)(state->anim.qp_glow_phase - 20U), th->is_light,
                                   (alpha_32 * 10U) >> 5, 0, NL_SCREEN_H);
        draw_multicolor_focus_ring(fb, mid_wx, glow_cy, 40, 2,
                                   (uint16_t)(state->anim.qp_glow_phase - 10U), th->is_light,
                                   (alpha_32 * 16U) >> 5, 0, NL_SCREEN_H);
    }

    int bounce_px = (state->anim.qp_sel_bounce_fp * 4 + 128) >> 8;
    int focus_ring_d = 36 + bounce_px;
    draw_multicolor_focus_ring(fb, glow_cx, glow_cy, focus_ring_d, 3,
                               state->anim.qp_glow_phase, th->is_light, alpha_32, 0, NL_SCREEN_H);

    int birth_slot_cx = 160;
    for (int v = 0; v < shown; v++) {
        int slot = win_start + v;
        int scx = start_x + v * stride + diam / 2;
        if (slot == state->anim.prof_birth_slot) birth_slot_cx = scx;
        bool focused = (slot == cur);

        /* Staggered sequential cascade reveal on selector open */
        int stag_delay = v * 22;
        int slot_open_fp = (m_fp <= stag_delay)
            ? 32
            : (((m_fp - stag_delay) * 256) / (256 - stag_delay));
        if (slot_open_fp > 256) slot_open_fp = 256;
        int stag_dy = ((256 - slot_open_fp) * 10 + 128) >> 8;
        unsigned slot_alpha = (unsigned)((slot_open_fp * (int)alpha_32) >> 8);
        if (slot_alpha < 6U) slot_alpha = 6U;

        /* Per-slot smooth scale (30px unselected -> 36px selected, +20% zoom) + spring rebound */
        int sc_fp = (slot >= 0 && slot < 9 && state->anim.qp_slot_scale_fp[slot] > 0)
            ? state->anim.qp_slot_scale_fp[slot]
            : (focused ? 272 : 226);
        int cur_diam = (diam * sc_fp + 128) >> 8;
        if (focused) {
            cur_diam += bounce_px;
        }
        cur_diam = (cur_diam * (160 + ((96 * slot_open_fp) >> 8))) >> 8;
        if (cur_diam < 16) cur_diam = 16;

        int lift_y = (focused && state->anim.qp_sel_bounce_fp > 0) ? -1 : 0;
        int scy = cy + stag_dy + lift_y;
        int sx = scx - cur_diam / 2;
        int sy = scy - cur_diam / 2;

        /* Discrete depth shadow under elevated selected avatar */
        if (focused) {
            uint16_t sh_col = th->is_light ? RGB565(195, 205, 220) : RGB565(4, 6, 10);
            fb_fill_aa_capsule_clipped(fb, sx + 2, sy + cur_diam - 1, cur_diam - 4, 4, sh_col, 0, NL_SCREEN_H);
        }

        if (slot < pcount) {
            const nl_profile_t *p = &state->profiles.items[slot];
            bool is_act = (slot == state->profiles.active_index);
            if (!focused) {
                uint16_t ring_c = is_act ? th->accent_primary : th->border_subtle;
                fb_fill_aa_capsule_clipped(fb, sx - 1, sy - 1, cur_diam + 2, cur_diam + 2, ring_c, 0, NL_SCREEN_H);
            }
            if (slot_alpha >= 30U) {
                draw_circular_avatar_box(fb, sx, sy, cur_diam, p->avatar_id, 0, NL_SCREEN_H, false, th);
            } else {
                draw_circular_avatar_box_alpha(fb, sx, sy, cur_diam, p->avatar_id, 0, NL_SCREEN_H, slot_alpha, 0, 0U);
            }

            /* Centered profile label below */
            char short_lbl[10];
            snprintf(short_lbl, sizeof(short_lbl), "%.7s", p->name);
            int lw = (int)strlen(short_lbl) * 6;
            fb_draw_text_clipped(fb, scx - lw / 2, cy + diam / 2 + 8, short_lbl,
                                 focused ? th->text_primary : th->text_secondary, 7, 0, NL_SCREEN_H);

            if (is_act) {
                int act_y = sy - 10;
                fb_fill_aa_capsule_clipped(fb, scx - 14, act_y, 28, 8, th->accent_primary, 0, NL_SCREEN_H);
                fb_draw_text_clipped(fb, scx - 12, act_y + 1, "ACT.", th->text_on_accent, 4, 0, NL_SCREEN_H);
            }
        } else {
            bool full = (pcount >= NL_MAX_PROFILES || nl_avatar_first_free(state) < 0);
            int rot_fp = focused ? state->anim.qp_plus_focus_fp : 0;
            draw_plus_badge_alpha(fb, scx, scy, cur_diam, rot_fp, slot_alpha, full, focused, th, 0, NL_SCREEN_H);

            const char *add_lbl = full ? "Complet" : "Ajouter";
            int lw = (int)strlen(add_lbl) * 6;
            fb_draw_text_clipped(fb, scx - lw / 2, cy + diam / 2 + 8, add_lbl,
                                 focused ? th->text_primary : th->text_secondary, 7, 0, NL_SCREEN_H);
        }
    }

    fb_fill_rect_clipped(fb, mx + 12, my + mh - 24, mw - 24, 1, th->border_subtle, 0, NL_SCREEN_H);
    const char *hint = "< > Choisir   EXE Valider   ESC Fermer";
    int hw = (int)strlen(hint) * 6;
    fb_draw_text_clipped(fb, mx + (mw - hw) / 2, my + mh - 16, hint, th->text_secondary, 44, 0, NL_SCREEN_H);

    if (state->anim.prof_birth_active) {
        render_profile_birth_transition(state, fb, th, birth_slot_cx, cy - 2);
    }
}

void nl_render_frame(const nl_state_t *state, uint16_t *fb) {
    if (!state || !fb) return;
    const nl_theme_t *th = nl_get_theme(state->display_mode, state->accent_color);

    bool is_managed = (state->sui.screen == state->anim.set_last_screen);

    if (state->sui.screen != NL_UI_LIBRARY) {
        int eff_open_fp = (is_managed && state->anim.set_open_fp > 0) ? state->anim.set_open_fp : 256;
        if (eff_open_fp < 256) {
            render_library_frame(state, fb, th);
            render_settings_single_screen(state, state->sui.screen, true, G_SET_BUF_A, th);
            composite_settings_open_close(fb, G_SET_BUF_A, eff_open_fp, th);
            int rot256 = (settings_open_ease256(eff_open_fp) * 96) >> 8;
            draw_gear_button_ex(fb, NL_GEAR_BTN_X, NL_GEAR_BTN_Y, NL_GEAR_BTN_D, th, true, rot256);
        } else {
            render_settings_screen_with_transitions(state, state->sui.screen, fb, th);
        }
        if (state->anim.prof_plus_active) {
            render_profile_plus_transition(state, fb, th);
        }
        if (state->anim.prof_switch_active) {
            render_profile_switch_transition(state, fb, th);
        }
        draw_touchpad_graphical_cursor(state, fb, th);
        draw_toast_banner(state, fb, th);
        return;
    }

    /* Render Library */
    render_library_frame(state, fb, th);

    /* Close Settings: reverse organic liquid-drop contraction + reverse gear rotation */
    if (is_managed && state->anim.set_open_fp > 0 && state->anim.set_prev_screen != NL_UI_LIBRARY) {
        render_settings_single_screen(state, state->anim.set_prev_screen, true, G_SET_BUF_A, th);
        composite_settings_open_close(fb, G_SET_BUF_A, state->anim.set_open_fp, th);
        int rot256 = (settings_open_ease256(state->anim.set_open_fp) * 96) >> 8;
        draw_gear_button_ex(fb, NL_GEAR_BTN_X, NL_GEAR_BTN_Y, NL_GEAR_BTN_D, th, state->anim.set_open_fp > 32, rot256);
    }

    /* Quick Profile Selector modal overlay (Library) */
    if (state->sui.qp_open || state->anim.qp_open_fp > 0) {
        render_quick_profile_overlay(state, fb, th);
    }

    /* 4-Stage Profile Switch Transition over the newly loaded Library */
    if (state->anim.prof_switch_active) {
        render_profile_switch_transition(state, fb, th);
    }

    /* Optional Touchpad Graphical Cursor + Luminous Trail */
    draw_touchpad_graphical_cursor(state, fb, th);

    /* 6. Toast Banner */
    draw_toast_banner(state, fb, th);
}

/* Integer LUT for raised-cosine ease curve: 0.5 - 0.5*cos(pi * i / 32) scaled 0..256 */
static const uint16_t G_EASE_COS_LUT[33] = {
    0, 1, 2, 5, 10, 15, 21, 29, 37, 47, 57, 68, 79, 91, 103, 115, 128,
    141, 153, 165, 177, 188, 199, 209, 219, 227, 235, 241, 246, 251, 254, 255, 256
};

void nl_render_splash_anim(const nl_state_t *state, uint16_t *fb, uint32_t elapsed_ms, bool show_loader) {
    if (!fb) return;
    const nl_theme_t *th = state
        ? nl_get_theme(state->display_mode, state->accent_color)
        : nl_get_theme(NL_DISPLAY_DARK, NL_ACCENT_BLUE);

    fb_fill_rect_clipped(fb, 0, 0, NL_SCREEN_W, NL_SCREEN_H, th->bg_main, 0, NL_SCREEN_H);

    int size;
    if (elapsed_ms < 240U) {
        uint32_t t = (elapsed_ms * 256U) / 240U;
        uint32_t inv = 256U - t;
        uint32_t ease = 256U - ((inv * inv * inv) >> 16);
        size = 24 + (int)((12U * ease + 128U) >> 8);
    } else {
        uint32_t phase = (elapsed_ms - 240U) % 960U;
        uint32_t idx = (phase < 480U)
            ? ((phase * 32U) / 480U)
            : (((960U - phase) * 32U) / 480U);
        if (idx > 32U) idx = 32U;
        uint32_t pulse = G_EASE_COS_LUT[idx];
        size = 36 + (int)((8U * pulse + 128U) >> 8);
    }

    const int logo_cx = NL_SCREEN_W / 2;
    const int logo_cy = 96;
    draw_launcher_emblem(fb, logo_cx - size / 2, logo_cy - size / 2, size, th);

    {
        const char *title = "N-LAUNCHER";
        int tlen = (int)strlen(title);
        int tx = (NL_SCREEN_W - (tlen * 7 - 2)) / 2;
        int ty = 128;
        for (int i = 0; i < tlen; i++) {
            fb_draw_char(fb, tx + i * 7, ty, title[i], th->text_primary, 0, NL_SCREEN_H);
        }
    }

    if (show_loader) {
        static const int8_t dot_offsets[8][2] = {
            {  0, -10 }, {  7, -7 }, { 10,  0 }, {  7,  7 },
            {  0,  10 }, { -7,  7 }, {-10,  0 }, { -7, -7 }
        };
        const int spin_cx = NL_SCREEN_W / 2;
        const int spin_cy = 158;
        const uint16_t dot_quiet = nl_rgb565_lerp(th->bg_main, th->dot_inactive, th->is_light ? 22U : 18U);
        const uint32_t step_ms = 85U;
        unsigned head = (unsigned)((elapsed_ms / step_ms) % 8U);
        unsigned sub = (unsigned)(((elapsed_ms % step_ms) * 32U) / step_ms);

        for (unsigned i = 0; i < 8U; i++) {
            unsigned dist = (head + 8U - i) & 7U;
            uint16_t dcol = dot_quiet;
            if (dist == 0U) {
                dcol = nl_rgb565_lerp(th->accent_secondary, th->accent_primary, sub);
            } else if (dist == 1U) {
                dcol = nl_rgb565_lerp(dot_quiet, th->accent_primary, 32U - (sub >> 1));
            } else if (dist == 2U) {
                unsigned fade = (sub < 32U) ? ((32U - sub) >> 1) : 0U;
                dcol = nl_rgb565_lerp(dot_quiet, th->accent_secondary, fade);
            } else if (dist == 7U) {
                dcol = nl_rgb565_lerp(dot_quiet, th->accent_primary, sub >> 1);
            }
            int dx = spin_cx + dot_offsets[i][0] - 2;
            int dy = spin_cy + dot_offsets[i][1] - 2;
            fb_fill_rounded_rect_clipped(fb, dx, dy, 4, 4, 2, dcol, 0, NL_SCREEN_H);
        }
    }
}

void nl_render_boot_transition(
    const nl_state_t *state,
    uint16_t *fb,
    const uint16_t *last_splash_fb,
    const uint16_t *lib_fb,
    int step,
    int max_steps
) {
    (void)state;
    if (!fb || !lib_fb || max_steps <= 0) return;
    if (!last_splash_fb || step >= max_steps) {
        memcpy(fb, lib_fb, NL_FB_BYTES);
        return;
    }
    if (step <= 0) {
        memcpy(fb, last_splash_fb, NL_FB_BYTES);
        return;
    }

    int t = (step * 32) / max_steps;
    int inv = 32 - t;
    unsigned alpha = (unsigned)(32 - ((inv * inv) >> 5));
    if (alpha > 32U) alpha = 32U;

    for (int i = 0; i < NL_FB_PIXELS; i++) {
        uint16_t s_px = last_splash_fb[i];
        uint16_t l_px = lib_fb[i];
        fb[i] = (s_px == l_px) ? l_px : nl_rgb565_lerp(s_px, l_px, alpha);
    }
}

/* build_organic_ink_field uses shared LUT & integer geometry defined near top of renderer.c */

static void build_organic_ink_field(uint32_t seed) {
    init_ink_sin_lut();

    uint8_t s0 = (uint8_t)((seed * 37U + 13U) & 255U);
    uint8_t s1 = (uint8_t)((seed * 73U + 41U) & 255U);
    uint8_t s2 = (uint8_t)((seed * 113U + 97U) & 255U);
    uint8_t s3 = (uint8_t)((seed * 157U + 19U) & 255U);

    int dir_scale[256];
    for (int th = 0; th < 256; th++) {
        int m1 = G_INK_SIN256[(th + s0) & 255];
        int m2 = G_INK_SIN256[((th * 2) + s1) & 255];
        int m3 = G_INK_SIN256[((th * 3) + s2) & 255];
        int m5 = G_INK_SIN256[((th * 5) + s3) & 255];
        int bias = (m1 * 10 + m2 * 13 + m3 * 12 + m5 * 7) >> 7;
        dir_scale[th] = 256 + bias;
    }

    int8_t wx[NL_SCREEN_W];
    int8_t wy[NL_SCREEN_H];
    for (int x = 0; x < NL_SCREEN_W; x++) {
        wx[x] = (int8_t)((G_INK_SIN256[((x * 2) + s1) & 255] * 4) >> 7);
    }
    for (int y = 0; y < NL_SCREEN_H; y++) {
        wy[y] = (int8_t)((G_INK_SIN256[((y * 2) + 64 + s2) & 255] * 4) >> 7);
    }

    int max_v = 1;
    for (int uy = 0; uy < 120; uy++) {
        int y_lo = 120 + uy;
        int y_hi = 119 - uy;
        uint8_t *row_lo = G_INK_FIELD + y_lo * NL_SCREEN_W;
        uint8_t *row_hi = G_INK_FIELD + y_hi * NL_SCREEN_W;
        int uy2 = uy * uy;

        for (int ux = 0; ux < 160; ux++) {
            int r = ink_isqrt32(ux * ux + uy2);
            int a0 = ink_octant_64(ux, uy);
            int ramp = (r < 56) ? ((r * 256) / 56) : 256;

            int x_r = 160 + ux;
            int x_l = 159 - ux;

            {
                int th = a0 & 255;
                int wd = (G_INK_SIN256[(((x_r + y_lo) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx[x_r] + wy[y_lo] + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_lo[x_r] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
            {
                int th = (128 - a0) & 255;
                int wd = (G_INK_SIN256[(((x_l + y_lo) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx[x_l] + wy[y_lo] + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_lo[x_l] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
            {
                int th = (128 + a0) & 255;
                int wd = (G_INK_SIN256[(((x_l + y_hi) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx[x_l] + wy[y_hi] + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_hi[x_l] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
            {
                int th = (256 - a0) & 255;
                int wd = (G_INK_SIN256[(((x_r + y_hi) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx[x_r] + wy[y_hi] + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_hi[x_r] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
        }
    }

    G_INK_CACHED_SEED = seed;
    G_INK_MAX_VAL = max_v;
}

void nl_render_ink_transition(
    uint16_t *fb,
    const uint16_t *old_fb,
    const uint16_t *new_fb,
    uint32_t seed,
    int step,
    int max_steps
) {
    if (!fb || !new_fb || max_steps <= 0) return;
    if (!old_fb || step >= max_steps) {
        memcpy(fb, new_fb, NL_FB_BYTES);
        return;
    }
    if (step <= 0) {
        memcpy(fb, old_fb, NL_FB_BYTES);
        return;
    }

    if (G_INK_CACHED_SEED != seed) {
        build_organic_ink_field(seed);
    }

    const int edge_w = 14;
    int t256 = (step * 256) / max_steps;
    if (t256 < 0) t256 = 0;
    if (t256 > 256) t256 = 256;
    int inv = 256 - t256;
    int cubic = 256 - ((inv * inv * inv) >> 16);
    int ease256 = (t256 * 77 + cubic * 179 + 128) >> 8;

    int front = ((G_INK_MAX_VAL + edge_w + 2) * ease256) >> 8;

    for (int i = 0; i < NL_FB_PIXELS; i++) {
        int delta = front - (int)G_INK_FIELD[i];
        if (delta >= edge_w) {
            fb[i] = new_fb[i];
        } else if (delta <= 0) {
            fb[i] = old_fb[i];
        } else {
            uint16_t o = old_fb[i];
            uint16_t n = new_fb[i];
            if (o == n) {
                fb[i] = n;
            } else {
                unsigned alpha = (unsigned)((delta * 32 + (edge_w >> 1)) / edge_w);
                fb[i] = nl_rgb565_lerp(o, n, alpha);
            }
        }
    }
}

void nl_render_splash(const nl_state_t *state, uint16_t *fb, int step, int max_steps) {
    if (!fb || max_steps <= 0) return;
    uint32_t elapsed = (step > 0) ? (uint32_t)((step * 900) / max_steps) : 0U;
    nl_render_splash_anim(state, fb, elapsed, true);
}

/* Render a single soft diffused concentric water-drop ripple centered at (cx, cy).
 * Guaranteed never to touch the 320x240 screen borders (soft margin fade near edges). */
static void draw_launch_concentric_ripple(
    uint16_t *fb,
    int cx,
    int cy,
    int r_16,         /* Ring crest radius in 1/16 px */
    int half_w_16,    /* Soft halo half-width in 1/16 px */
    unsigned alpha_32,/* Peak crest opacity (0..32) */
    uint16_t halo_col,
    uint16_t crest_col
) {
    if (!fb || alpha_32 == 0U || half_w_16 <= 0) return;
    if (alpha_32 > 32U) alpha_32 = 32U;

    int r_outer_px = (r_16 + half_w_16 + 15) >> 4;
    int r_inner_px = (r_16 > half_w_16) ? ((r_16 - half_w_16) >> 4) : 0;
    if (r_outer_px <= 0) return;

    int r_outer2 = r_outer_px * r_outer_px;
    int r_inner2 = r_inner_px * r_inner_px;

    /* Strict 4px safety margin from all 4 screen borders so ripples never touch screen edges */
    const int min_margin = 4;
    int y0 = (cy - r_outer_px < min_margin) ? min_margin : (cy - r_outer_px);
    int y1 = (cy + r_outer_px + 1 > NL_SCREEN_H - min_margin) ? (NL_SCREEN_H - min_margin) : (cy + r_outer_px + 1);
    int x0 = (cx - r_outer_px < min_margin) ? min_margin : (cx - r_outer_px);
    int x1 = (cx + r_outer_px + 1 > NL_SCREEN_W - min_margin) ? (NL_SCREEN_W - min_margin) : (cx + r_outer_px + 1);

    const uint16_t white_col = RGB565(255, 255, 255);
    const uint16_t crest_pearl = nl_rgb565_lerp(crest_col, white_col, 15U);

    for (int py = y0; py < y1; py++) {
        int dy = py - cy;
        int dy2 = dy * dy;
        int edge_y = (py < 12) ? (py - min_margin) : ((py >= NL_SCREEN_H - 12) ? (NL_SCREEN_H - min_margin - 1 - py) : 8);
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = x0; px < x1; px++) {
            int dx = px - cx;
            int d2 = dx * dx + dy2;
            if (d2 >= r_outer2 || d2 < r_inner2) continue;

            /* Subpixel Euclidean distance in 1/16 px */
            int dist16 = ink_isqrt32(d2 << 8);
            int delta = dist16 - r_16;
            if (delta < 0) delta = -delta;
            if (delta >= half_w_16) continue;

            /* Smooth raised-cosine / quadratic bell profile for a genuine soft halo */
            int u = ((half_w_16 - delta) * 256) / half_w_16; /* 0..256 */
            int bell = (u * u) >> 8; /* 0..256 */
            unsigned a = (unsigned)((bell * (int)alpha_32 + 128) >> 8);
            if (a == 0U) continue;

            int edge_x = (px < 12) ? (px - min_margin) : ((px >= NL_SCREEN_W - 12) ? (NL_SCREEN_W - min_margin - 1 - px) : 8);
            int edge_m = (edge_x < edge_y) ? edge_x : edge_y;
            if (edge_m < 8) {
                a = (a * (unsigned)edge_m) >> 3;
                if (a == 0U) continue;
            }
            if (a > 32U) a = 32U;

            uint16_t ring_col = halo_col;
            if (u > 136) {
                unsigned t_crest = (unsigned)(((u - 136) * 32) / 120);
                if (t_crest > 32U) t_crest = 32U;
                ring_col = nl_rgb565_lerp(halo_col, crest_pearl, t_crest);
            }
            row[px] = nl_rgb565_lerp(row[px], ring_col, a);
        }
    }
}

/* 4-Stage Launch Transition with 3 Concentric Water-Drop Ripples (Switch-inspired):
 * 1. Upon releasing EXE, the selected custom icon shrinks smoothly (t in 0..36: scale 280/256 -> 232/256),
 *    while all other library icons progressively fade out toward full transparency (t in 0..180).
 * 2. As the selected icon begins its slight rebound (t >= 36), 3 concentric luminous ripples emerge
 *    from the exact center (card_cx, card_cy) BEHIND the selected custom icon.
 * 3. During t_exp in 0..140, the selected custom icon stays in the foreground at (card_cx, card_cy)
 *    so the 3 concentric ripples propagate clearly around its perimeter before any screenshot crossfade.
 * 4. During t_exp in 140..256, a smooth crossfade replaces the custom icon with the real screenshot (.scr.tns)
 *    while the card completes its smooth expansion to fullscreen (320x240). */
void nl_render_launch_zoom(const nl_state_t *state, uint16_t *fb, int step, int max_steps) {
    if (!state || !fb || state->total_games <= 0 || max_steps <= 0) return;

    const nl_theme_t *th = nl_get_theme(state->display_mode, state->accent_color);
    uint16_t dim_target = th->is_light ? RGB565(216, 222, 232) : RGB565(6, 8, 12);

    int game_idx = (state->launch_game_idx >= 0 && state->launch_game_idx < state->total_games)
        ? state->launch_game_idx
        : (state->filtered_count > 0 ? state->filtered_indices[state->selected_pos] : 0);
    const nl_game_t *sel = &state->games[game_idx];

    int t = (step * 256) / max_steps;
    if (t < 0) t = 0;
    if (t > 256) t = 256;

    /* Progressive, slow, smooth fade-out of all non-selected library icons (32 -> 0 over t in 0..180) */
    unsigned other_alpha_32 = 0U;
    if (t < 180) {
        int fade_idx = (t * 32) / 180;
        if (fade_idx < 0) fade_idx = 0;
        if (fade_idx > 32) fade_idx = 32;
        unsigned rem256 = (unsigned)(256 - G_EASE_COS_LUT[fade_idx]);
        other_alpha_32 = (rem256 * 32U + 128U) >> 8;
        if (t > 0 && other_alpha_32 > 31U) other_alpha_32 = 31U;
    }

    nl_layout_metrics_t m;
    get_interpolated_layout_metrics(state, &m);

    int search_fp = state->anim.search_bar_fp;
    if (state->sui.search_active && search_fp == 0) search_fp = 256;
    else if (!state->sui.search_active && search_fp == 256 && state->sui.screen == NL_UI_LIBRARY) search_fp = 0;
    int search_h = (28 * search_fp + 128) >> 8;

    int sx = (NL_SCREEN_W - m.card_w) / 2;
    int sy = (NL_SCREEN_H - m.card_h) / 2;

    if (state->sui.screen == NL_UI_LIBRARY && state->filtered_count > 0) {
        int slot_pos = state->selected_pos;
        if (slot_pos < 0 || slot_pos >= state->filtered_count || state->filtered_indices[slot_pos] != game_idx) {
            for (int fi = 0; fi < state->filtered_count; fi++) {
                if (state->filtered_indices[fi] == game_idx) {
                    slot_pos = fi;
                    break;
                }
            }
        }
        render_library_frame_ex(state, fb, th, game_idx, other_alpha_32);
        if (slot_pos >= 0 && slot_pos < state->filtered_count && state->filtered_indices[slot_pos] == game_idx) {
            int row = slot_pos / m.cols;
            int scroll_y = state->anim.scroll_y_fp >> 4;
            int slot_x = 0, slot_y = 0;
            nl_compute_grid_slot_pos(&m, state->filtered_count, slot_pos, &slot_x, &slot_y);
            sx = slot_x;
            sy = slot_y + search_h - scroll_y;
            if (search_h == 0 && sy < NL_VIEW_TOP && row == state->current_page * m.rows) {
                sy = NL_VIEW_TOP + 1;
            }
        }
    } else {
        nl_render_frame(state, fb);
    }

    /* Dynamic center of the selected custom icon */
    int card_cx = sx + m.card_w / 2;
    int card_cy = sy + m.card_h / 2;

    /* Layer 1: Gentle background dimming (ramps up primarily during final expansion so fading icons stay clean) */
    unsigned dim = 0U;
    if (t <= 140) {
        dim = (unsigned)((t * 6) / 140);
    } else {
        dim = (unsigned)(6 + (((t - 140) * 16) / (256 - 140)));
    }
    if (dim > 22U) dim = 22U;
    if (dim > 0U) {
        for (int i = 0; i < NL_FB_PIXELS; i++) {
            fb[i] = nl_rgb565_lerp(fb[i], dim_target, dim);
        }
    }

    const int shrink_end_t = 36;
    int rw = 0, rh = 0, rx = 0, ry = 0, rad = m.radius;
    int t_exp = 0;
    int ease = 0;

    int shrink_w = (m.card_w * 232 + 128) >> 8;
    int shrink_h = (m.card_h * 232 + 128) >> 8;
    int shrink_x = sx + (m.card_w - shrink_w) / 2;
    int shrink_y = sy + (m.card_h - shrink_h) / 2;

    if (t < shrink_end_t) {
        int shrink_prog = (t * 256) / shrink_end_t; /* 0..256 */
        int s_ease = (shrink_prog * (512 - shrink_prog) + 128) >> 8;
        int cur_scale = NL_SCALE_SELECTED - (((NL_SCALE_SELECTED - 232) * s_ease + 128) >> 8);
        rw = (m.card_w * cur_scale + 128) >> 8;
        rh = (m.card_h * cur_scale + 128) >> 8;
        rx = sx + (m.card_w - rw) / 2;
        ry = sy + (m.card_h - rh) / 2;
        rad = m.radius;
    } else {
        t_exp = ((t - shrink_end_t) * 256) / (256 - shrink_end_t);
        if (t_exp < 0) t_exp = 0;
        if (t_exp > 256) t_exp = 256;

        /* Stage 2 & 3 (t_exp in 0..144): Slight rebound (232/256 -> ~256/256, ease 0..20)
         * keeping the custom icon localized at (card_cx, card_cy) while the 3 concentric ripples
         * propagate clearly behind and around it.
         * Stage 4 (t_exp in 144..256): Smooth cubic expansion to fullscreen 320x240 (ease 20..256). */
        if (t_exp < 144) {
            int p = (t_exp * 256) / 144; /* 0..256 */
            int p_ease = (p * (512 - p) + 128) >> 8;
            ease = (20 * p_ease + 128) >> 8; /* 0..20 */
        } else {
            int p = ((t_exp - 144) * 256) / (256 - 144); /* 0..256 */
            int inv = 256 - p;
            int p_cubic = 256 - ((inv * inv * inv) >> 16);
            ease = 20 + (((256 - 20) * p_cubic + 128) >> 8); /* 20..256 */
        }

        rw = shrink_w + (((NL_SCREEN_W - shrink_w) * ease) >> 8);
        rh = shrink_h + (((NL_SCREEN_H - shrink_h) * ease) >> 8);
        rx = shrink_x + (((0 - shrink_x) * ease) >> 8);
        ry = shrink_y + (((0 - shrink_y) * ease) >> 8);
        rad = (m.radius * (256 - (ease > 256 ? 256 : ease))) >> 8;
    }

    /* Layer 2 (Middle Layer — BEHIND the selected custom icon):
     * Exactly 3 concentric luminous water-drop ripples centered dynamically at (card_cx, card_cy).
     * Drawn BEFORE draw_game_cover_card so the selected icon masks their interior and the ripples
     * emerge cleanly from behind the icon's edges and propagate around it. */
    if (t >= shrink_end_t && t_exp < 228) {
        static const uint8_t k_wave_delay[3]   = {   0,  22,  44 };
        static const uint8_t k_wave_life[3]    = { 156, 148, 140 };
        static const uint8_t k_wave_span_px[3] = {  25,  19,  13 }; /* Outward travel beyond icon half-width */
        static const uint8_t k_wave_peak_a[3]  = {  19,  21,  23 };
        static const uint8_t k_wave_half_w[3]  = {  88,  84,  80 }; /* ~5.0..5.5 px soft diffused half-width */

        int min_dim = (m.card_w < m.card_h) ? m.card_w : m.card_h;
        int half_card = (min_dim * 236) >> 9; /* Half-size of the icon during rebound (~35 px in 3x2) */
        if (half_card < 14) half_card = 14;
        int r_start_px = (half_card > 8) ? (half_card - 7) : 4;
        const int r_start_16 = r_start_px << 4;

        for (int w = 0; w < 3; w++) {
            int delay = (int)k_wave_delay[w];
            int life = (int)k_wave_life[w];
            if (t_exp <= delay || t_exp >= delay + life) continue;

            int st = ((t_exp - delay) * 256) / life; /* 0..256 */
            if (st < 0 || st > 256) continue;

            /* Smooth decelerating radial expansion like a water ripple */
            int inv_st = 256 - st;
            int expand_ease = 256 - ((inv_st * inv_st * inv_st) >> 16);
            int r_end_16 = (half_card + (int)k_wave_span_px[w]) << 4;
            int r_16 = r_start_16 + (((r_end_16 - r_start_16) * expand_ease + 128) >> 8);

            /* Raised-cosine fade-in (0..40) then continuous outward luminosity decay (40..256) */
            unsigned env_256 = 0U;
            if (st < 40) {
                int idx = (st * 32) / 40;
                if (idx > 32) idx = 32;
                env_256 = (unsigned)G_EASE_COS_LUT[idx];
            } else {
                int rem = 256 - st;
                int norm = (rem * 256) / (256 - 40); /* 256..0 */
                env_256 = (unsigned)((norm * norm) >> 8);
            }

            unsigned alpha_32 = (env_256 * (unsigned)k_wave_peak_a[w] + 128U) >> 8;
            if (alpha_32 == 0U) continue;

            /* Launch animation glows are strictly pure white across all themes (Blue, Red, Green, Yellow) */
            const uint16_t halo_col  = RGB565(255, 255, 255);
            const uint16_t crest_col = RGB565(255, 255, 255);
            draw_launch_concentric_ripple(
                fb,
                card_cx,
                card_cy,
                r_16,
                (int)k_wave_half_w[w],
                alpha_32,
                halo_col,
                crest_col
            );
        }
    }

    /* Layer 3 (Foreground Layer — IN FRONT of the concentric ripples):
     * Selected game's custom icon + soft white border luminance + Stage 4 smooth crossfade to screenshot (.scr.tns) */
    int card_glow = 0;
    if (t >= shrink_end_t && t_exp < 236) {
        int g_in = (t_exp < 44) ? ((t_exp * 28) / 44) : 28;
        card_glow = (g_in * (236 - t_exp)) / 236;
    }
    if (card_glow > 0 && rad > 0) {
        fb_rounded_soft_luminance_clipped(fb, rx, ry, rw, rh, rad, card_glow, false, 0, NL_SCREEN_H);
    }

    draw_game_cover_card(fb, rx, ry, rw, rh, rad, card_glow, sel, th, 0, NL_SCREEN_H);

    if (t >= shrink_end_t && sel->has_screenshot && sel->scr_pixels != NULL && rw > 0 && rh > 0) {
        /* Stage 4: Only start the crossfade to the .scr.tns screenshot at t_exp >= 136,
         * AFTER the 3 concentric ripples have clearly propagated around the custom icon! */
        unsigned xfade_alpha = 0U;
        if (t_exp <= 136) {
            xfade_alpha = 0U;
        } else if (t_exp >= 212) {
            xfade_alpha = 32U;
        } else {
            int u256 = ((t_exp - 136) * 256) / (212 - 136);
            int smooth = (u256 * u256 * (768 - 2 * u256)) >> 16; /* smoothstep 0..256 */
            xfade_alpha = (unsigned)((smooth * 32 + 128) >> 8);
        }
        if (xfade_alpha > 32U) xfade_alpha = 32U;

        if (xfade_alpha > 0U) {
            int y0 = (ry < 0) ? 0 : ry;
            int y1 = (ry + rh > NL_SCREEN_H) ? NL_SCREEN_H : (ry + rh);
            int x0_clip = (rx < 0) ? 0 : rx;
            int x1_clip = (rx + rw > NL_SCREEN_W) ? NL_SCREEN_W : (rx + rw);
            for (int py = y0; py < y1; py++) {
                int dy = py - ry;
                int v256 = (rh > 1) ? ((dy * (NL_COVER_H - 1) * 256) / (rh - 1)) : 0;
                uint16_t *dst_row = fb + py * NL_SCREEN_W;

                for (int px = x0_clip; px < x1_clip; px++) {
                    int dx = px - rx;
                    bool is_border = false;
                    unsigned cov = aa_rounded_card_pixel(dx, dy, rw, rh, rad, &is_border);
                    if (cov == 0U) continue;
                    (void)is_border;
                    int u256 = (rw > 1) ? ((dx * (NL_COVER_W - 1) * 256) / (rw - 1)) : 0;
                    uint16_t scr_px = sample_rgb565_bilinear(sel->scr_pixels, NL_COVER_W, NL_COVER_H, u256, v256);
                    unsigned eff_a = (cov >= 32U) ? xfade_alpha : ((xfade_alpha * cov + 16U) >> 5);
                    if (eff_a > 0U) {
                        dst_row[px] = nl_rgb565_lerp(dst_row[px], scr_px, eff_a);
                    }
                }
            }
        }
    }
}


