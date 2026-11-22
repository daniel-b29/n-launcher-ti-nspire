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
#include "boot_guard_core.h"
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
    return nl_rgb565_lerp_fast(c0, c1, alpha_0_to_32);
}

/* R15: Inline SWAR RGB565 lerp for all internal renderer hot loops (eliminates BL overhead on ARMv5TE) */
#define nl_rgb565_lerp nl_rgb565_lerp_fast

static inline void fb_put_pixel(uint16_t *fb, int x, int y, uint16_t color) {
    if ((unsigned)x < NL_SCREEN_W && (unsigned)y < NL_SCREEN_H) {
        fb[y * NL_SCREEN_W + x] = color;
    }
}

/* Crisp pixel padlock icon (9x11) centered at (cx, cy) for Profile Protection UI */
static void draw_padlock_icon(uint16_t *fb, int cx, int cy, uint16_t col,
                              uint16_t hole_col, bool locked, int clip_y0, int clip_y1) {
    int x0 = cx - 4;
    int y0 = cy - 5;
    /* Shackle arch */
    int sh_top = locked ? y0 : (y0 - 1);
    for (int dx = 2; dx <= 6; dx++) {
        int py = sh_top;
        if (py >= clip_y0 && py < clip_y1) fb_put_pixel(fb, x0 + dx, py, col);
    }
    for (int dy = 1; dy <= 4; dy++) {
        int py = sh_top + dy;
        if (py >= clip_y0 && py < clip_y1) {
            fb_put_pixel(fb, x0 + 2, py, col);
            if (locked || dy <= 2) {
                fb_put_pixel(fb, x0 + 6, py, col);
            }
        }
    }
    /* Solid padlock body (9x6, slightly rounded corners) */
    for (int dy = 0; dy < 6; dy++) {
        int py = y0 + 5 + dy;
        if (py < clip_y0 || py >= clip_y1) continue;
        int inset = (dy == 0 || dy == 5) ? 1 : 0;
        for (int dx = inset; dx < 9 - inset; dx++) {
            fb_put_pixel(fb, x0 + dx, py, col);
        }
    }
    /* Keyhole cutout */
    if (y0 + 7 >= clip_y0 && y0 + 7 < clip_y1) fb_put_pixel(fb, cx, y0 + 7, hole_col);
    if (y0 + 8 >= clip_y0 && y0 + 8 < clip_y1) fb_put_pixel(fb, cx, y0 + 8, hole_col);
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

static void fb_draw_rounded_outline_clipped(uint16_t *fb, int x, int y, int w, int h,
                                            int radius, uint16_t color,
                                            int clip_y0, int clip_y1) {
    if (w <= 2 || h <= 2) return;
    int y0 = y < clip_y0 ? clip_y0 : y;
    int y1 = (y + h > clip_y1) ? clip_y1 : (y + h);
    if (y0 >= y1) return;

    for (int py = y0; py < y1; py++) {
        int dy = py - y;
        int inset = round_corner_inset(dy, h, radius);
        int x0 = x + inset;
        int x1 = x + w - inset - 1;
        uint16_t *row = fb + py * NL_SCREEN_W;
        if (dy == 0 || dy == h - 1) {
            int cx0 = x0 < 0 ? 0 : x0;
            int cx1 = (x1 + 1 > NL_SCREEN_W) ? NL_SCREEN_W : (x1 + 1);
            for (int px = cx0; px < cx1; px++) row[px] = color;
        } else {
            int prev_in = round_corner_inset(dy - 1, h, radius);
            int next_in = round_corner_inset(dy + 1, h, radius);
            int span = (prev_in > next_in ? prev_in : next_in) - inset;
            if (span < 1) span = 1;
            for (int s = 0; s < span; s++) {
                int lx = x0 + s;
                int rx = x1 - s;
                if ((unsigned)lx < NL_SCREEN_W) row[lx] = color;
                if ((unsigned)rx < NL_SCREEN_W) row[rx] = color;
            }
        }
    }
}

/* Fast 2x2 subpixel anti-aliased Euclidean stadium/capsule (fast interior span, zero inner branch) */
static void fb_fill_aa_capsule_clipped(uint16_t *fb, int x, int y, int w, int h,
                                       uint16_t color, int clip_y0, int clip_y1) {
    if (w <= 0 || h <= 0) return;
    int y0 = y < clip_y0 ? clip_y0 : y;
    int y1 = (y + h > clip_y1) ? clip_y1 : (y + h);
    if (y0 >= y1) return;

    const int r4 = h * 2;
    const int r2_4 = r4 * r4;
    const int lx4 = r4;
    const int rx4 = w * 4 - r4;
    const int cy4 = r4;
    const int x0 = x < 0 ? 0 : x;
    const int x1 = (x + w > NL_SCREEN_W) ? NL_SCREEN_W : (x + w);
    if (x0 >= x1) return;

    int body_x0 = x + (h / 2);
    int body_x1 = x + w - (h / 2);
    if (body_x0 < x0) body_x0 = x0;
    if (body_x1 > x1) body_x1 = x1;

    for (int py = y0; py < y1; py++) {
        int iy = py - y;
        uint16_t *row = fb + py * NL_SCREEN_W;
        int dy0 = (iy * 4 + 1) - cy4;
        int dy1 = (iy * 4 + 3) - cy4;
        int dy0_2 = dy0 * dy0;
        int dy1_2 = dy1 * dy1;

        int left_end = (body_x0 < body_x1) ? body_x0 : x1;
        for (int px = x0; px < left_end; px++) {
            int ix = px - x;
            int px4_0 = ix * 4 + 1;
            int px4_1 = ix * 4 + 3;
            int dx0 = px4_0 - ((px4_0 < lx4) ? lx4 : ((px4_0 > rx4) ? rx4 : px4_0));
            int dx1 = px4_1 - ((px4_1 < lx4) ? lx4 : ((px4_1 > rx4) ? rx4 : px4_1));
            int dx0_2 = dx0 * dx0;
            int dx1_2 = dx1 * dx1;
            unsigned hits = (unsigned)(dx0_2 + dy0_2 <= r2_4) +
                            (unsigned)(dx1_2 + dy0_2 <= r2_4) +
                            (unsigned)(dx0_2 + dy1_2 <= r2_4) +
                            (unsigned)(dx1_2 + dy1_2 <= r2_4);
            if (hits == 4U) row[px] = color;
            else if (hits > 0U) row[px] = nl_rgb565_lerp(row[px], color, hits << 3);
        }
        if (body_x0 < body_x1) {
            for (int px = body_x0; px < body_x1; px++) {
                row[px] = color;
            }
            for (int px = body_x1; px < x1; px++) {
                int ix = px - x;
                int px4_0 = ix * 4 + 1;
                int px4_1 = ix * 4 + 3;
                int dx0 = px4_0 - ((px4_0 < lx4) ? lx4 : ((px4_0 > rx4) ? rx4 : px4_0));
                int dx1 = px4_1 - ((px4_1 < lx4) ? lx4 : ((px4_1 > rx4) ? rx4 : px4_1));
                int dx0_2 = dx0 * dx0;
                int dx1_2 = dx1 * dx1;
                unsigned hits = (unsigned)(dx0_2 + dy0_2 <= r2_4) +
                                (unsigned)(dx1_2 + dy0_2 <= r2_4) +
                                (unsigned)(dx0_2 + dy1_2 <= r2_4) +
                                (unsigned)(dx1_2 + dy1_2 <= r2_4);
                if (hits == 4U) row[px] = color;
                else if (hits > 0U) row[px] = nl_rgb565_lerp(row[px], color, hits << 3);
            }
        }
    }
}

static void fb_fill_aa_circle_clipped(uint16_t *fb, int cx, int cy, int r,
                                      uint16_t color, int clip_y0, int clip_y1) {
    if (r <= 0) return;
    int d = r * 2 + 1;
    fb_fill_aa_capsule_clipped(fb, cx - r, cy - r, d, d, color, clip_y0, clip_y1);
}

/* Soft rounded drop shadow behind elevated/scaled card (occl_x/occl_y = opaque card origin that overwrites interior) */
static void fb_rounded_shadow_clipped(uint16_t *fb, int x, int y, int w, int h,
                                      int radius, bool is_light, int clip_y0, int clip_y1,
                                      int occl_x, int occl_y) {
    int y0 = y < clip_y0 ? clip_y0 : y;
    int y1 = (y + h > clip_y1) ? clip_y1 : (y + h);
    const uint16_t light_shadow = RGB565(136, 148, 166);
    for (int py = y0; py < y1; py++) {
        int inset = round_corner_inset(py - y, h, radius);
        int x0 = x + inset;
        int x1 = x + w - inset;
        if (x0 < 0) x0 = 0;
        if (x1 > NL_SCREEN_W) x1 = NL_SCREEN_W;
        if (x0 >= x1) continue;
        uint16_t *row = fb + py * NL_SCREEN_W;

        /* Skip opaque card interior strictly covered by draw_game_cover_card immediately after (bit-for-bit identical) */
        if (py >= occl_y + 1 && py < occl_y + h - 1 && w > 4) {
            int in_inset = round_corner_inset(py - occl_y, h, radius) + 1;
            int ix0 = occl_x + in_inset;
            int ix1 = occl_x + w - in_inset;
            if (ix0 < ix1) {
                int left_end = (ix0 < x1) ? ix0 : x1;
                int right_start = (ix1 > x0) ? ix1 : x0;
                if (is_light) {
                    for (int px = x0; px < left_end; px++) {
                        row[px] = nl_rgb565_lerp(row[px], light_shadow, 7);
                    }
                    for (int px = right_start; px < x1; px++) {
                        row[px] = nl_rgb565_lerp(row[px], light_shadow, 7);
                    }
                } else {
                    for (int px = x0; px < left_end; px++) {
                        row[px] = (uint16_t)((row[px] >> 1) & 0x7BEFU);
                    }
                    for (int px = right_start; px < x1; px++) {
                        row[px] = (uint16_t)((row[px] >> 1) & 0x7BEFU);
                    }
                }
                continue;
            }
        }

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
            if (x0 >= x1) continue;
            uint16_t *row = fb + py * NL_SCREEN_W;

            /* Skip the opaque card interior (strictly covered by draw_game_cover_card immediately after):
             * Cuts halo pixel blends by ~96% on both library cards and expanding launch cards with identical visuals. */
            if (py >= y + 1 && py < y + h - 1 && w > 4) {
                int in_inset = round_corner_inset(py - y, h, radius) + 1;
                int ix0 = x + in_inset;
                int ix1 = x + w - in_inset;
                if (ix0 < ix1) {
                    int left_end = (ix0 < x1) ? ix0 : x1;
                    for (int px = x0; px < left_end; px++) {
                        row[px] = nl_rgb565_lerp(row[px], lum_tint, alpha);
                    }
                    int right_start = (ix1 > x0) ? ix1 : x0;
                    for (int px = right_start; px < x1; px++) {
                        row[px] = nl_rgb565_lerp(row[px], lum_tint, alpha);
                    }
                    continue;
                }
            }
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

/* Integer LUT, cached transition fields & reusable scratch framebuffers (zero per-frame malloc) */
static int8_t    G_INK_SIN256[256];
static bool      G_INK_SIN_READY = false;
static uint8_t  *G_INK_FIELD = NULL;
static uint32_t  G_INK_CACHED_SEED = 0xFFFFFFFFU;
static int       G_INK_MAX_VAL = 220;
static uint16_t *G_SCRATCH_FB_A = NULL;
static uint16_t *G_SCRATCH_FB_B = NULL;
static uint16_t *G_SET_DROP_FIELD = NULL;
static bool      G_LAUNCH_BG_VALID = false;
static int       G_LAUNCH_BG_GAME_IDX = -1;
static uint32_t  G_LAUNCH_BG_SIG = 0U;
static int16_t   G_LAUNCH_SPAN_X0[NL_SCREEN_H];
static int16_t   G_LAUNCH_SPAN_X1[NL_SCREEN_H];

/* #R19 Zero-Quality-Loss Rendering Caches (all strictly bounded & freed in nl_renderer_cleanup):
 *  1. G_CARD_CACHE   : 64-slot, 512 KB cap LRU pre-scaled + theme-baked RGB565 card sprites
 *  2. G_BAKED_SRC    : 2-slot, max 28 KB native-res theme-baked source icons for live zoom/pop frames
 *  3. G_LIB_STATIC_FB: 153.6 KB cached library frame when library state is unchanged (mouse/overlays)
 *  4. G_DIM_BG_FB    : 153.6 KB cached dimmed library backdrop for Quick Profile / Avatar Picker 60 FPS halos */
#define NL_CARD_CACHE_SLOTS      64
#define NL_CARD_CACHE_MAX_BYTES  (512U * 1024U)
#define NL_BAKED_SRC_SLOTS       2

typedef struct {
    const uint16_t *src_pix;
    const uint8_t  *src_alp;
    uint16_t       *pixels;
    uint8_t        *alpha;
    size_t          bytes;
    uint32_t        data_hash;
    uint32_t        last_used;
    int16_t         iw;
    int16_t         ih;
    int16_t         fw;
    int16_t         fh;
    bool            valid;
} nl_card_cache_entry_t;

static nl_card_cache_entry_t G_CARD_CACHE[NL_CARD_CACHE_SLOTS];
static size_t    G_CARD_CACHE_BYTES = 0U;
static uint32_t  G_CARD_CACHE_TICK  = 0U;

static uint16_t *G_LIB_STATIC_FB    = NULL;
static bool      G_LIB_STATIC_VALID = false;
static uint64_t  G_LIB_STATIC_SIG   = 0ULL;

static uint16_t *G_DIM_BG_FB        = NULL;
static bool      G_DIM_BG_VALID     = false;
static uint64_t  G_DIM_BG_SIG       = 0ULL;
static uint16_t  G_DIM_BG_COL       = 0U;
static uint8_t   G_DIM_BG_ALPHA     = 0U;

static uint32_t icon_data_fingerprint(const uint16_t *pix, const uint8_t *alp, int iw, int ih) {
    uint32_t h = 2166136261U;
    h = (h ^ (uint32_t)iw) * 16777619U;
    h = (h ^ (uint32_t)ih) * 16777619U;
    int n = iw * ih;
    if (pix && n > 0) {
        int step = (n > 16) ? (n / 16) : 1;
        for (int i = 0; i < n; i += step) {
            h = (h ^ (uint32_t)pix[i]) * 16777619U;
        }
        h = (h ^ (uint32_t)pix[n - 1]) * 16777619U;
    }
    if (alp && n > 0) {
        int step = (n > 8) ? (n / 8) : 1;
        for (int i = 0; i < n; i += step) {
            h = (h ^ (uint32_t)alp[i]) * 16777619U;
        }
        h = (h ^ (uint32_t)alp[n - 1]) * 16777619U;
    }
    return h;
}

static uint16_t *renderer_get_scratch_a(void) {
    if (!G_SCRATCH_FB_A) {
        G_SCRATCH_FB_A = (uint16_t *)malloc(NL_FB_BYTES);
    }
    return G_SCRATCH_FB_A;
}

static uint16_t *renderer_get_scratch_b(void) {
    if (!G_SCRATCH_FB_B) {
        G_SCRATCH_FB_B = (uint16_t *)malloc(NL_FB_BYTES);
    }
    return G_SCRATCH_FB_B;
}

void nl_renderer_cleanup(void) {
    free(G_INK_FIELD);
    G_INK_FIELD = NULL;
    G_INK_CACHED_SEED = 0xFFFFFFFFU;
    free(G_SCRATCH_FB_A);
    G_SCRATCH_FB_A = NULL;
    free(G_SCRATCH_FB_B);
    G_SCRATCH_FB_B = NULL;
    free(G_SET_DROP_FIELD);
    G_SET_DROP_FIELD = NULL;
    G_LAUNCH_BG_VALID = false;
    G_LAUNCH_BG_GAME_IDX = -1;
    G_LAUNCH_BG_SIG = 0U;

    for (int i = 0; i < NL_CARD_CACHE_SLOTS; i++) {
        free(G_CARD_CACHE[i].pixels);
        free(G_CARD_CACHE[i].alpha);
        memset(&G_CARD_CACHE[i], 0, sizeof(G_CARD_CACHE[i]));
    }
    G_CARD_CACHE_BYTES = 0U;
    G_CARD_CACHE_TICK  = 0U;

    free(G_LIB_STATIC_FB);
    G_LIB_STATIC_FB = NULL;
    G_LIB_STATIC_VALID = false;
    G_LIB_STATIC_SIG = 0ULL;

    free(G_DIM_BG_FB);
    G_DIM_BG_FB = NULL;
    G_DIM_BG_VALID = false;
    G_DIM_BG_SIG = 0ULL;
    G_DIM_BG_COL = 0U;
    G_DIM_BG_ALPHA = 0U;
}

/* #R23 Fast-return exec prep:
 * - Free heavy transient scratch buffers (ink/scratch/drop ~530 KB) for the child game.
 * - On TI-Nspire CX II-T (64 MB RAM, hw_is_cx2 == true), keep G_LIB_STATIC_FB (153 KB) intact
 *   so returning from a game whose sort order did not change blits Frame 1 in < 2 ms without
 *   redrawing the entire grid! (On CX I 32 MB RAM, free G_LIB_STATIC_FB as before.)
 * - Preserve pre-scaled sprites for BOTH the currently visible page AND adjacent peek edges
 *   (page - 1 and page + 1, <= 240 KB cap) so Frame 1 never downscales 144x144 PNG icons! */
void nl_renderer_prepare_for_exec(const nl_state_t *state) {
    free(G_INK_FIELD);
    G_INK_FIELD = NULL;
    G_INK_CACHED_SEED = 0xFFFFFFFFU;
    free(G_SCRATCH_FB_A);
    G_SCRATCH_FB_A = NULL;
    free(G_SCRATCH_FB_B);
    G_SCRATCH_FB_B = NULL;
    free(G_SET_DROP_FIELD);
    G_SET_DROP_FIELD = NULL;
    G_LAUNCH_BG_VALID = false;
    G_LAUNCH_BG_GAME_IDX = -1;
    G_LAUNCH_BG_SIG = 0U;

    bool keep_static = (state != NULL && state->sys.hw_is_cx2);
    if (!keep_static) {
        free(G_LIB_STATIC_FB);
        G_LIB_STATIC_FB = NULL;
        G_LIB_STATIC_VALID = false;
        G_LIB_STATIC_SIG = 0ULL;
    }

    free(G_DIM_BG_FB);
    G_DIM_BG_FB = NULL;
    G_DIM_BG_VALID = false;
    G_DIM_BG_SIG = 0ULL;
    G_DIM_BG_COL = 0U;
    G_DIM_BG_ALPHA = 0U;

    if (!state || state->filtered_count <= 0) {
        for (int i = 0; i < NL_CARD_CACHE_SLOTS; i++) {
            free(G_CARD_CACHE[i].pixels);
            free(G_CARD_CACHE[i].alpha);
            memset(&G_CARD_CACHE[i], 0, sizeof(G_CARD_CACHE[i]));
        }
        G_CARD_CACHE_BYTES = 0U;
        G_CARD_CACHE_TICK  = 0U;
        return;
    }

    nl_layout_metrics_t m;
    nl_get_layout_metrics_full(
        state->settings.layout,
        state->settings.scroll_dir,
        state->settings.show_names,
        state->settings.show_shortcuts,
        state->filtered_count,
        &m
    );
    int per_page = (m.per_page > 0) ? m.per_page : NL_GAMES_PER_PAGE;
    int sel = state->selected_pos;
    if (sel < 0) sel = 0;
    if (sel >= state->filtered_count) sel = state->filtered_count - 1;
    int page = sel / per_page;

    /* Preserve current page FIRST, then adjacent peek edges (page-1 & page+1) */
    const uint16_t *keep_ptrs[36];
    int keep_count = 0;
    int p_start = page * per_page;
    int p_end = p_start + per_page;
    if (p_end > state->filtered_count) p_end = state->filtered_count;
    for (int fi = p_start; fi < p_end && keep_count < 36; fi++) {
        int gi = state->filtered_indices[fi];
        if (gi >= 0 && gi < state->total_games && state->games[gi].icon_pixels != NULL) {
            keep_ptrs[keep_count++] = state->games[gi].icon_pixels;
        }
    }
    /* Neighboring peek page icons (prev page + next page) so peek carousel renders with 0 downscale */
    if (page > 0) {
        int prev_start = (page - 1) * per_page;
        int prev_end = prev_start + per_page;
        if (prev_end > state->filtered_count) prev_end = state->filtered_count;
        for (int fi = prev_start; fi < prev_end && keep_count < 36; fi++) {
            int gi = state->filtered_indices[fi];
            if (gi >= 0 && gi < state->total_games && state->games[gi].icon_pixels != NULL) {
                keep_ptrs[keep_count++] = state->games[gi].icon_pixels;
            }
        }
    }
    if ((page + 1) * per_page < state->filtered_count) {
        int next_start = (page + 1) * per_page;
        int next_end = next_start + per_page;
        if (next_end > state->filtered_count) next_end = state->filtered_count;
        for (int fi = next_start; fi < next_end && keep_count < 36; fi++) {
            int gi = state->filtered_indices[fi];
            if (gi >= 0 && gi < state->total_games && state->games[gi].icon_pixels != NULL) {
                keep_ptrs[keep_count++] = state->games[gi].icon_pixels;
            }
        }
    }

    const size_t exec_keep_cap = (state->sys.hw_is_cx2) ? (240U * 1024U) : (160U * 1024U);
    size_t kept_bytes = 0U;
    for (int i = 0; i < NL_CARD_CACHE_SLOTS; i++) {
        if (!G_CARD_CACHE[i].valid) continue;
        bool on_keep = false;
        for (int k = 0; k < keep_count; k++) {
            if (G_CARD_CACHE[i].src_pix == keep_ptrs[k]) {
                on_keep = true;
                break;
            }
        }
        if (on_keep && (kept_bytes + G_CARD_CACHE[i].bytes) <= exec_keep_cap) {
            kept_bytes += G_CARD_CACHE[i].bytes;
        } else {
            free(G_CARD_CACHE[i].pixels);
            free(G_CARD_CACHE[i].alpha);
            memset(&G_CARD_CACHE[i], 0, sizeof(G_CARD_CACHE[i]));
        }
    }
    G_CARD_CACHE_BYTES = kept_bytes;
}

bool nl_renderer_has_static_fb_cached(void) {
    return (G_LIB_STATIC_FB != NULL && G_LIB_STATIC_VALID);
}

int nl_renderer_cached_sprite_count(void) {
    int cnt = 0;
    for (int i = 0; i < NL_CARD_CACHE_SLOTS; i++) {
        if (G_CARD_CACHE[i].valid) cnt++;
    }
    return cnt;
}

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


/* Fast SWAR bilinear RGB565 sampler (zero channel-by-channel branching, ~3x faster on ARM926EJ-S) */
static inline uint16_t sample_rgb565_bilinear_fast(
    const uint16_t *pix, int w, int h, int u256, int v256
) {
    int x0 = u256 >> 8;
    int y0 = v256 >> 8;
    if (x0 < 0) x0 = 0; else if (x0 >= w) x0 = w - 1;
    if (y0 < 0) y0 = 0; else if (y0 >= h) y0 = h - 1;
    int x1 = (x0 + 1 < w) ? (x0 + 1) : x0;
    int y1 = (y0 + 1 < h) ? (y0 + 1) : y0;
    unsigned fx32 = (unsigned)((u256 & 255) >> 3); /* 0..31 */
    unsigned fy32 = (unsigned)((v256 & 255) >> 3); /* 0..31 */
    const uint16_t *r0 = pix + y0 * w;
    uint16_t top = (fx32 == 0U) ? r0[x0] : nl_rgb565_lerp(r0[x0], r0[x1], fx32);
    if (fy32 == 0U || y1 == y0) return top;
    const uint16_t *r1 = pix + y1 * w;
    uint16_t bot = (fx32 == 0U) ? r1[x0] : nl_rgb565_lerp(r1[x0], r1[x1], fx32);
    return nl_rgb565_lerp(top, bot, fy32);
}

static inline unsigned sample_alpha_bilinear_fast(
    const uint8_t *alpha, int w, int h, int u256, int v256
) {
    int x0 = u256 >> 8;
    int y0 = v256 >> 8;
    if (x0 < 0) x0 = 0; else if (x0 >= w) x0 = w - 1;
    if (y0 < 0) y0 = 0; else if (y0 >= h) y0 = h - 1;
    int x1 = (x0 + 1 < w) ? (x0 + 1) : x0;
    int y1 = (y0 + 1 < h) ? (y0 + 1) : y0;
    unsigned fx = (unsigned)(u256 & 255);
    unsigned fy = (unsigned)(v256 & 255);
    const uint8_t *r0 = alpha + y0 * w;
    const uint8_t *r1 = alpha + y1 * w;
    unsigned top = ((unsigned)r0[x0] * (256U - fx) + (unsigned)r0[x1] * fx) >> 8;
    unsigned bot = ((unsigned)r1[x0] * (256U - fx) + (unsigned)r1[x1] * fx) >> 8;
    return (top * (256U - fy) + bot * fy + 128U) >> 8;
}

/* 2x2 subpixel anti-aliased rounded corner coverage (0..32) + discreet 1px perimeter border detection */
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

    int r4 = r * 4;
    int r2_4 = r4 * r4;
    int rin4 = (r > 1) ? ((r - 1) * 4) : 0;
    int rin2_4 = rin4 * rin4;
    int cx4 = cx * 4;
    int cy4 = cy * 4;

    int dx0 = (ix * 4 + 1) - cx4;
    int dx1 = (ix * 4 + 3) - cx4;
    int dy0 = (iy * 4 + 1) - cy4;
    int dy1 = (iy * 4 + 3) - cy4;
    int dx0_2 = dx0 * dx0, dx1_2 = dx1 * dx1;
    int dy0_2 = dy0 * dy0, dy1_2 = dy1 * dy1;

    unsigned hits = (unsigned)(dx0_2 + dy0_2 <= r2_4) +
                    (unsigned)(dx1_2 + dy0_2 <= r2_4) +
                    (unsigned)(dx0_2 + dy1_2 <= r2_4) +
                    (unsigned)(dx1_2 + dy1_2 <= r2_4);
    int mdx = (ix * 4 + 2) - cx4;
    int mdy = (iy * 4 + 2) - cy4;
    *out_border = (hits > 0U && (mdx * mdx + mdy * mdy >= rin2_4));
    return hits << 3;
}

/* Static 6-slot symmetric corner AA LUT (zero malloc, 3.4 KB BSS, bit-for-bit identical to aa_rounded_card_pixel).
 * Each byte encodes: (hits << 3) | (is_border ? 1 : 0) for top-left quadrant (dx_rel, dy_rel) in [0..r-1]. */
#define NL_AA_CORNER_LUT_SLOTS  6
#define NL_AA_CORNER_LUT_MAX_R  24
static uint8_t G_AA_CORNER_LUT[NL_AA_CORNER_LUT_SLOTS][NL_AA_CORNER_LUT_MAX_R * NL_AA_CORNER_LUT_MAX_R];
static int     G_AA_CORNER_LUT_R[NL_AA_CORNER_LUT_SLOTS] = {0, 0, 0, 0, 0, 0};
static int     G_AA_CORNER_LUT_NEXT = 0;

static const uint8_t *aa_corner_get_lut(int r) {
    if (r <= 0 || r > NL_AA_CORNER_LUT_MAX_R) return NULL;
    for (int s = 0; s < NL_AA_CORNER_LUT_SLOTS; s++) {
        if (G_AA_CORNER_LUT_R[s] == r) return G_AA_CORNER_LUT[s];
    }
    int slot = G_AA_CORNER_LUT_NEXT;
    G_AA_CORNER_LUT_NEXT = (G_AA_CORNER_LUT_NEXT + 1) % NL_AA_CORNER_LUT_SLOTS;
    uint8_t *tbl = G_AA_CORNER_LUT[slot];
    int r4 = r * 4;
    int r2_4 = r4 * r4;
    int rin4 = (r > 1) ? ((r - 1) * 4) : 0;
    int rin2_4 = rin4 * rin4;
    for (int iy = 0; iy < r; iy++) {
        int dy0 = (iy * 4 + 1) - r4;
        int dy1 = (iy * 4 + 3) - r4;
        int mdy = (iy * 4 + 2) - r4;
        int dy0_2 = dy0 * dy0;
        int dy1_2 = dy1 * dy1;
        int mdy2 = mdy * mdy;
        uint8_t *row = tbl + iy * r;
        for (int ix = 0; ix < r; ix++) {
            int dx0 = (ix * 4 + 1) - r4;
            int dx1 = (ix * 4 + 3) - r4;
            int mdx = (ix * 4 + 2) - r4;
            int dx0_2 = dx0 * dx0;
            int dx1_2 = dx1 * dx1;
            unsigned hits = (unsigned)(dx0_2 + dy0_2 <= r2_4) +
                            (unsigned)(dx1_2 + dy0_2 <= r2_4) +
                            (unsigned)(dx0_2 + dy1_2 <= r2_4) +
                            (unsigned)(dx1_2 + dy1_2 <= r2_4);
            unsigned border = (hits > 0U && (mdx * mdx + mdy2 >= rin2_4)) ? 1U : 0U;
            row[ix] = (uint8_t)((hits << 3) | border);
        }
    }
    G_AA_CORNER_LUT_R[slot] = r;
    return tbl;
}

/* Pre-scale an icon (and optional alpha channel) into the bounded 512 KB LRU card sprite cache.
 * #R20: NEVER bakes a gray backdrop behind transparent PNG icons — keeps alpha crisp & clean. */
static const uint16_t *card_cache_lookup_or_build(
    const uint16_t *src_pix,
    const uint8_t  *src_alp,
    int iw, int ih,
    int fw, int fh,
    const uint8_t **out_alp
) {
    if (out_alp) *out_alp = NULL;
    if (!src_pix || iw <= 0 || ih <= 0 || fw <= 0 || fh <= 0) return NULL;
    size_t pix_bytes = (size_t)fw * (size_t)fh * sizeof(uint16_t);
    size_t alp_bytes = src_alp ? ((size_t)fw * (size_t)fh) : 0U;
    size_t need_bytes = pix_bytes + alp_bytes;
    if (need_bytes == 0U || need_bytes > NL_CARD_CACHE_MAX_BYTES) return NULL;

    uint32_t dhash = icon_data_fingerprint(src_pix, src_alp, iw, ih);
    G_CARD_CACHE_TICK++;

    int free_slot = -1;
    for (int i = 0; i < NL_CARD_CACHE_SLOTS; i++) {
        nl_card_cache_entry_t *e = &G_CARD_CACHE[i];
        if (e->valid) {
            if (e->src_pix == src_pix && e->src_alp == src_alp &&
                e->iw == iw && e->ih == ih && e->fw == fw && e->fh == fh &&
                e->data_hash == dhash) {
                e->last_used = G_CARD_CACHE_TICK;
                if (out_alp) *out_alp = e->alpha;
                return e->pixels;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }

    while (free_slot < 0 || (G_CARD_CACHE_BYTES + need_bytes > NL_CARD_CACHE_MAX_BYTES)) {
        int victim = -1;
        uint32_t oldest = 0xFFFFFFFFU;
        for (int i = 0; i < NL_CARD_CACHE_SLOTS; i++) {
            if (G_CARD_CACHE[i].valid && (victim < 0 || G_CARD_CACHE[i].last_used < oldest)) {
                oldest = G_CARD_CACHE[i].last_used;
                victim = i;
            }
        }
        if (victim < 0) break;
        free(G_CARD_CACHE[victim].pixels);
        free(G_CARD_CACHE[victim].alpha);
        if (G_CARD_CACHE_BYTES >= G_CARD_CACHE[victim].bytes) {
            G_CARD_CACHE_BYTES -= G_CARD_CACHE[victim].bytes;
        } else {
            G_CARD_CACHE_BYTES = 0U;
        }
        memset(&G_CARD_CACHE[victim], 0, sizeof(G_CARD_CACHE[victim]));
        if (free_slot < 0) free_slot = victim;
    }
    if (free_slot < 0) return NULL;

    uint16_t *buf = (uint16_t *)malloc(pix_bytes);
    if (!buf) return NULL;
    uint8_t *alp_buf = NULL;
    if (src_alp) {
        alp_buf = (uint8_t *)malloc(alp_bytes);
        if (!alp_buf) {
            free(buf);
            return NULL;
        }
    }

    bool exact = (fw == iw && fh == ih);
    if (exact) {
        memcpy(buf, src_pix, pix_bytes);
        if (alp_buf) memcpy(alp_buf, src_alp, alp_bytes);
    } else {
        int du_fp = (fw > 1 && iw > 1) ? (((iw - 1) << 16) / (fw - 1)) : 0;
        int dv_fp = (fh > 1 && ih > 1) ? (((ih - 1) << 16) / (fh - 1)) : 0;
        bool use_sep = (iw <= NL_ICON_MAX_DIM && ih <= NL_ICON_MAX_DIM && fw <= NL_SCREEN_W);
        if (use_sep) {
            uint8_t ux0[NL_SCREEN_W], ux1[NL_SCREEN_W], ufx[NL_SCREEN_W];
            int u_fp = 0;
            for (int x = 0; x < fw; x++, u_fp += du_fp) {
                int u256 = u_fp >> 8;
                int sx0 = u256 >> 8;
                int sx1 = sx0 + 1;
                if (sx0 < 0) sx0 = 0;
                if (sx0 >= iw) sx0 = iw - 1;
                if (sx1 >= iw) sx1 = iw - 1;
                ux0[x] = (uint8_t)sx0;
                ux1[x] = (uint8_t)sx1;
                ufx[x] = (uint8_t)((u256 & 0xFF) >> 3);
            }
            uint16_t vpix[NL_ICON_MAX_DIM];
            uint8_t  valp[NL_ICON_MAX_DIM];
            for (int y = 0; y < fh; y++) {
                int v256 = (y * dv_fp) >> 8;
                int sy0 = v256 >> 8;
                int sy1 = sy0 + 1;
                if (sy0 < 0) sy0 = 0;
                if (sy0 >= ih) sy0 = ih - 1;
                if (sy1 >= ih) sy1 = ih - 1;
                unsigned fy32 = (unsigned)((v256 & 0xFF) >> 3);
                const uint16_t *sr0 = src_pix + sy0 * iw;
                const uint16_t *sr1 = src_pix + sy1 * iw;
                if (fy32 == 0U || sy0 == sy1) {
                    memcpy(vpix, sr0, (size_t)iw * sizeof(uint16_t));
                    if (src_alp) memcpy(valp, src_alp + sy0 * iw, (size_t)iw);
                } else {
                    for (int sx = 0; sx < iw; sx++) {
                        vpix[sx] = nl_rgb565_lerp(sr0[sx], sr1[sx], fy32);
                    }
                    if (src_alp) {
                        const uint8_t *ar0 = src_alp + sy0 * iw;
                        const uint8_t *ar1 = src_alp + sy1 * iw;
                        unsigned inv_fy = 32U - fy32;
                        for (int sx = 0; sx < iw; sx++) {
                            valp[sx] = (uint8_t)(((unsigned)ar0[sx] * inv_fy + (unsigned)ar1[sx] * fy32 + 16U) >> 5);
                        }
                    }
                }
                uint16_t *drow = buf + y * fw;
                if (!alp_buf) {
                    for (int x = 0; x < fw; x++) {
                        drow[x] = nl_rgb565_lerp(vpix[ux0[x]], vpix[ux1[x]], ufx[x]);
                    }
                } else {
                    uint8_t *arow = alp_buf + y * fw;
                    for (int x = 0; x < fw; x++) {
                        int x0s = ux0[x], x1s = ux1[x];
                        unsigned fx = ufx[x];
                        drow[x] = nl_rgb565_lerp(vpix[x0s], vpix[x1s], fx);
                        arow[x] = (uint8_t)(((unsigned)valp[x0s] * (32U - fx) + (unsigned)valp[x1s] * fx + 16U) >> 5);
                    }
                }
            }
        } else {
            for (int y = 0; y < fh; y++) {
                int v256 = (y * dv_fp) >> 8;
                int u_fp = 0;
                uint16_t *drow = buf + y * fw;
                uint8_t *arow = alp_buf ? (alp_buf + y * fw) : NULL;
                for (int x = 0; x < fw; x++, u_fp += du_fp) {
                    int u256 = u_fp >> 8;
                    drow[x] = sample_rgb565_bilinear_fast(src_pix, iw, ih, u256, v256);
                    if (arow) arow[x] = (uint8_t)sample_alpha_bilinear_fast(src_alp, iw, ih, u256, v256);
                }
            }
        }
    }

    nl_card_cache_entry_t *e = &G_CARD_CACHE[free_slot];
    e->src_pix   = src_pix;
    e->src_alp   = src_alp;
    e->pixels    = buf;
    e->alpha     = alp_buf;
    e->bytes     = need_bytes;
    e->data_hash = dhash;
    e->last_used = G_CARD_CACHE_TICK;
    e->iw        = (int16_t)iw;
    e->ih        = (int16_t)ih;
    e->fw        = (int16_t)fw;
    e->fh        = (int16_t)fh;
    e->valid     = true;
    G_CARD_CACHE_BYTES += need_bytes;
    if (out_alp) *out_alp = alp_buf;
    return buf;
}

/* #R20 Alpha-silhouette-aware soft shadow for transparent PNG icons (zero rectangular border overflow) */
static void draw_alpha_icon_shadow_clipped(
    uint16_t *fb,
    int bx, int by, int fw, int fh,
    const uint8_t *src_alp, int iw, int ih,
    int dx_off, int dy_off,
    bool is_light,
    int clip_y0, int clip_y1
) {
    if (!src_alp || fw <= 0 || fh <= 0 || iw <= 0 || ih <= 0) return;
    int sx0 = bx + dx_off;
    int sy0 = by + dy_off;
    int y0 = sy0 < clip_y0 ? clip_y0 : sy0;
    int y1 = (sy0 + fh > clip_y1) ? clip_y1 : (sy0 + fh);
    int x0 = sx0 < 0 ? 0 : sx0;
    int x1 = (sx0 + fw > NL_SCREEN_W) ? NL_SCREEN_W : (sx0 + fw);
    if (y0 >= y1 || x0 >= x1) return;

    const uint16_t light_shadow = RGB565(178, 186, 198);
    int du_fp = (fw > 1 && iw > 1) ? (((iw - 1) << 16) / (fw - 1)) : 0;
    int dv_fp = (fh > 1 && ih > 1) ? (((ih - 1) << 16) / (fh - 1)) : 0;
    uint8_t sx_lut[NL_SCREEN_W];
    int u_fp = (x0 - sx0) * du_fp;
    for (int px = x0; px < x1; px++, u_fp += du_fp) {
        int sxi = (u_fp + 32768) >> 16;
        if (sxi < 0) sxi = 0;
        if (sxi >= iw) sxi = iw - 1;
        sx_lut[px] = (uint8_t)sxi;
    }

    for (int py = y0; py < y1; py++) {
        int syi = (((py - sy0) * dv_fp) + 32768) >> 16;
        if (syi < 0) syi = 0;
        if (syi >= ih) syi = ih - 1;
        const uint8_t *arow = src_alp + syi * iw;
        uint16_t *drow = fb + py * NL_SCREEN_W;
        if (is_light) {
            for (int px = x0; px < x1; px++) {
                unsigned a = arow[sx_lut[px]];
                if (a >= 96U) {
                    unsigned sa = (a >= 220U) ? 7U : ((a * 7U) >> 8);
                    if (sa > 0U) drow[px] = nl_rgb565_lerp(drow[px], light_shadow, sa);
                }
            }
        } else {
            for (int px = x0; px < x1; px++) {
                unsigned a = arow[sx_lut[px]];
                if (a >= 96U) {
                    drow[px] = (a >= 200U)
                        ? (uint16_t)((drow[px] >> 1) & 0x7BEFU)
                        : nl_rgb565_lerp(drow[px], RGB565(0, 0, 0), (a * 14U) >> 8);
                }
            }
        }
    }
}

/* #R20 Alpha-silhouette-aware soft ambient glow behind selected transparent PNG icon (only 1 card/frame) */
static void draw_alpha_icon_glow_clipped(
    uint16_t *fb,
    int bx, int by, int fw, int fh,
    const uint8_t *src_alp, int iw, int ih,
    int hl_0_to_32, bool is_light,
    int clip_y0, int clip_y1
) {
    if (!src_alp || hl_0_to_32 <= 0 || fw <= 0 || fh <= 0 || iw <= 0 || ih <= 0) return;
    unsigned alpha = (unsigned)((2 * hl_0_to_32 + 16) >> 5);
    if (alpha == 0U && hl_0_to_32 >= 12) alpha = 1U;
    if (alpha == 0U) return;

    const uint16_t lum_tint = is_light ? RGB565(168, 180, 198) : RGB565(255, 255, 255);
    int y0 = (by - 2) < clip_y0 ? clip_y0 : (by - 2);
    int y1 = (by + fh + 2 > clip_y1) ? clip_y1 : (by + fh + 2);
    int x0 = (bx - 2) < 0 ? 0 : (bx - 2);
    int x1 = (bx + fw + 2 > NL_SCREEN_W) ? NL_SCREEN_W : (bx + fw + 2);
    if (y0 >= y1 || x0 >= x1) return;

    int du_fp = (fw > 1 && iw > 1) ? (((iw - 1) << 16) / (fw - 1)) : 0;
    int dv_fp = (fh > 1 && ih > 1) ? (((ih - 1) << 16) / (fh - 1)) : 0;

    for (int py = y0; py < y1; py++) {
        uint16_t *drow = fb + py * NL_SCREEN_W;
        int ly = py - by;
        int sy_c = (ly >= 0 && ly < fh) ? (((ly * dv_fp) + 32768) >> 16) : -1;
        int sy_u = (ly - 2 >= 0 && ly - 2 < fh) ? ((((ly - 2) * dv_fp) + 32768) >> 16) : -1;
        int sy_d = (ly + 2 >= 0 && ly + 2 < fh) ? ((((ly + 2) * dv_fp) + 32768) >> 16) : -1;
        if (sy_c >= ih) sy_c = ih - 1;
        if (sy_u >= ih) sy_u = ih - 1;
        if (sy_d >= ih) sy_d = ih - 1;
        const uint8_t *rc = (sy_c >= 0) ? (src_alp + sy_c * iw) : NULL;
        const uint8_t *ru = (sy_u >= 0) ? (src_alp + sy_u * iw) : NULL;
        const uint8_t *rd = (sy_d >= 0) ? (src_alp + sy_d * iw) : NULL;

        for (int px = x0; px < x1; px++) {
            int lx = px - bx;
            int sx_c = (lx >= 0 && lx < fw) ? (((lx * du_fp) + 32768) >> 16) : -1;
            if (sx_c >= iw) sx_c = iw - 1;
            if (rc && sx_c >= 0 && rc[sx_c] >= 220U) continue; /* Strictly covered by opaque icon */

            bool near = false;
            if (rc) {
                int sx_l = (lx - 2 >= 0 && lx - 2 < fw) ? ((((lx - 2) * du_fp) + 32768) >> 16) : -1;
                int sx_r = (lx + 2 >= 0 && lx + 2 < fw) ? ((((lx + 2) * du_fp) + 32768) >> 16) : -1;
                if (sx_l >= iw) sx_l = iw - 1;
                if (sx_r >= iw) sx_r = iw - 1;
                if ((sx_l >= 0 && rc[sx_l] >= 128U) || (sx_r >= 0 && rc[sx_r] >= 128U)) near = true;
            }
            if (!near && sx_c >= 0) {
                if ((ru && ru[sx_c] >= 128U) || (rd && rd[sx_c] >= 128U)) near = true;
            }
            if (near) {
                drow[px] = nl_rgb565_lerp(drow[px], lum_tint, alpha);
            }
        }
    }
}

/* #R20 Ultra-discreet 3x5 proportional micro-signature "daniel.b" (25x5 px) */
static void draw_micro_signature_daniel_b(uint16_t *fb, int x, int y, uint16_t col) {
    /* Bitmaps: 5 rows per glyph, MSB at bit (w-1) */
    static const struct { uint8_t w; uint8_t rows[5]; } k_sig[8] = {
        { 3, { 0x1, 0x1, 0x7, 0x5, 0x7 } }, /* d */
        { 3, { 0x0, 0x7, 0x3, 0x5, 0x7 } }, /* a */
        { 3, { 0x0, 0x6, 0x5, 0x5, 0x5 } }, /* n */
        { 1, { 0x1, 0x0, 0x1, 0x1, 0x1 } }, /* i */
        { 3, { 0x0, 0x7, 0x7, 0x4, 0x7 } }, /* e */
        { 1, { 0x1, 0x1, 0x1, 0x1, 0x1 } }, /* l */
        { 1, { 0x0, 0x0, 0x0, 0x0, 0x1 } }, /* . */
        { 3, { 0x4, 0x4, 0x7, 0x5, 0x7 } }  /* b */
    };
    int cx = x;
    for (int i = 0; i < 8; i++) {
        int w = (int)k_sig[i].w;
        for (int r = 0; r < 5; r++) {
            int py = y + r;
            if ((unsigned)py >= NL_SCREEN_H) continue;
            uint8_t bits = k_sig[i].rows[r];
            for (int c = 0; c < w; c++) {
                if ((bits >> (w - 1 - c)) & 1U) {
                    int px = cx + c;
                    if ((unsigned)px < NL_SCREEN_W) {
                        fb[py * NL_SCREEN_W + px] = col;
                    }
                }
            }
        }
        cx += w + 1;
    }
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

/* Draw a cover card strictly clipped to AA rounded mask & viewport band (ZERO divisions in inner loop) */
static void draw_game_cover_card_ex(
    uint16_t *fb,
    int cx, int cy, int cw, int ch,
    int radius,
    int hl_0_to_32,
    const nl_game_t *g,
    const nl_theme_t *th,
    int clip_y0, int clip_y1,
    bool allow_cache
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
        int iw = (g->icon_w > 0) ? (int)g->icon_w : NL_ICON_V1_W;
        int ih = (g->icon_h > 0) ? (int)g->icon_h : NL_ICON_V1_H;
        int ox = 0, oy = 0, fw = cw, fh = ch;
        compute_icon_contain_box(iw, ih, cw, ch, &ox, &oy, &fw, &fh);
        int bx = cx + ox;
        int by = cy + oy;
        int by0 = by < clip_y0 ? clip_y0 : by;
        int by1 = (by + fh > clip_y1) ? clip_y1 : (by + fh);
        int bx0 = bx < 0 ? 0 : bx;
        int bx1 = (bx + fw > NL_SCREEN_W) ? NL_SCREEN_W : (bx + fw);
        if (by0 >= by1 || bx0 >= bx1) return;

        int r_eff = radius;
        if (r_eff * 2 > fw) r_eff = fw / 2;
        if (r_eff * 2 > fh) r_eff = fh / 2;
        if (r_eff < 0) r_eff = 0;

        const uint16_t *src_pix = g->icon_pixels;
        const uint8_t  *src_alp = g->icon_alpha;
        bool exact_1to1 = (fw == iw && fh == ih);

        /* #R20 Fast Path: Pre-scaled LRU card sprite cache with true alpha channel (ZERO gray backdrop slab!) */
        if (allow_cache && fw > 0 && fh > 0 && fw <= 184 && fh <= 184 && !exact_1to1) {
            const uint8_t *cached_alp = NULL;
            const uint16_t *cached = card_cache_lookup_or_build(src_pix, src_alp, iw, ih, fw, fh, &cached_alp);
            if (cached) {
                src_pix = cached;
                src_alp = cached_alp;
                iw = fw;
                ih = fh;
                exact_1to1 = true;
            }
        }

        int du_fp = (fw > 1 && iw > 1) ? (((iw - 1) << 16) / (fw - 1)) : 0;
        int dv_fp = (fh > 1 && ih > 1) ? (((ih - 1) << 16) / (fh - 1)) : 0;

        /* Separable 1D vertical pre-blend fast path when live-scaling (clamped strictly to visible sx_min..sx_max) */
        bool use_sep = (!exact_1to1 && iw > 0 && iw <= NL_ICON_MAX_DIM && ih > 0 && ih <= NL_ICON_MAX_DIM);
        uint8_t u_x0_lut[NL_SCREEN_W];
        uint8_t u_x1_lut[NL_SCREEN_W];
        uint8_t u_fx_lut[NL_SCREEN_W];
        int sx_min = 0, sx_max = iw - 1;
        if (use_sep) {
            int u_fp_lut = (bx0 - bx) * du_fp;
            for (int px = bx0; px < bx1; px++, u_fp_lut += du_fp) {
                int u256 = u_fp_lut >> 8;
                int sx0 = u256 >> 8;
                int sx1 = sx0 + 1;
                if (sx0 < 0) sx0 = 0;
                if (sx0 >= iw) sx0 = iw - 1;
                if (sx1 >= iw) sx1 = iw - 1;
                u_x0_lut[px] = (uint8_t)sx0;
                u_x1_lut[px] = (uint8_t)sx1;
                u_fx_lut[px] = (uint8_t)((u256 & 0xFF) >> 3);
            }
            sx_min = (int)u_x0_lut[bx0];
            sx_max = (int)u_x1_lut[bx1 - 1];
            if (sx_min < 0) sx_min = 0;
            if (sx_max >= iw) sx_max = iw - 1;
            if (sx_max < sx_min) sx_max = sx_min;
        }
        uint16_t vrow_pix[NL_ICON_MAX_DIM];
        uint8_t  vrow_alp[NL_ICON_MAX_DIM];
        const uint8_t *corner_lut = (r_eff >= 1 && r_eff <= NL_AA_CORNER_LUT_MAX_R)
            ? aa_corner_get_lut(r_eff)
            : NULL;

        for (int py = by0; py < by1; py++) {
            int iy_fit = py - by;
            uint16_t *dst_row = fb + py * NL_SCREEN_W;
            bool edge_fade = (clip_y1 == NL_VIEW_BOTTOM && py >= NL_VIEW_BOTTOM - 5);
            unsigned fade_a = edge_fade ? (unsigned)(py - (NL_VIEW_BOTTOM - 5)) * 4U : 0U;
            bool in_corner_y = (iy_fit < r_eff || iy_fit >= fh - r_eff);
            int v256 = (iy_fit * dv_fp) >> 8;
            const uint8_t *clut_row = (corner_lut && in_corner_y)
                ? (corner_lut + ((iy_fit < r_eff) ? iy_fit : (fh - 1 - iy_fit)) * r_eff)
                : NULL;

            if (use_sep) {
                int sy0 = v256 >> 8;
                int sy1 = sy0 + 1;
                if (sy0 < 0) sy0 = 0;
                if (sy0 >= ih) sy0 = ih - 1;
                if (sy1 >= ih) sy1 = ih - 1;
                unsigned fy32 = (unsigned)((v256 & 0xFF) >> 3);
                const uint16_t *sr0 = src_pix + sy0 * iw;
                const uint16_t *sr1 = src_pix + sy1 * iw;
                int sx_cnt = sx_max - sx_min + 1;
                if (fy32 == 0U || sy0 == sy1) {
                    memcpy(vrow_pix + sx_min, sr0 + sx_min, (size_t)sx_cnt * sizeof(uint16_t));
                    if (src_alp) memcpy(vrow_alp + sx_min, src_alp + sy0 * iw + sx_min, (size_t)sx_cnt);
                } else {
                    for (int sx = sx_min; sx <= sx_max; sx++) {
                        vrow_pix[sx] = nl_rgb565_lerp(sr0[sx], sr1[sx], fy32);
                    }
                    if (src_alp) {
                        const uint8_t *ar0 = src_alp + sy0 * iw;
                        const uint8_t *ar1 = src_alp + sy1 * iw;
                        unsigned inv_fy = 32U - fy32;
                        for (int sx = sx_min; sx <= sx_max; sx++) {
                            vrow_alp[sx] = (uint8_t)(((unsigned)ar0[sx] * inv_fy + (unsigned)ar1[sx] * fy32 + 16U) >> 5);
                        }
                    }
                }
            }

            int left_end = in_corner_y ? (bx + r_eff) : bx0;
            int right_start = in_corner_y ? (bx + fw - r_eff) : bx1;
            if (left_end < bx0) left_end = bx0;
            if (left_end > bx1) left_end = bx1;
            if (right_start < left_end) right_start = left_end;
            if (right_start > bx1) right_start = bx1;

            int u_fp = (bx0 - bx) * du_fp;

            /* Left rounded corner span (ZERO synthetic gray border on custom icons) */
            for (int px = bx0; px < left_end; px++, u_fp += du_fp) {
                int ix_fit = px - bx;
                bool is_border = false;
                unsigned cov;
                if (!in_corner_y) {
                    cov = 32U;
                } else if (clut_row && ix_fit < r_eff) {
                    cov = (unsigned)(clut_row[ix_fit] & 0xFEU);
                } else {
                    cov = aa_rounded_card_pixel(ix_fit, iy_fit, fw, fh, r_eff, &is_border);
                    (void)is_border;
                }
                if (cov == 0U) continue;

                uint16_t px_col;
                unsigned a255 = 255U;
                if (exact_1to1) {
                    int idx = iy_fit * iw + ix_fit;
                    px_col = src_pix[idx];
                    if (src_alp) a255 = src_alp[idx];
                } else if (use_sep) {
                    int x0s = u_x0_lut[px], x1s = u_x1_lut[px];
                    unsigned fx = u_fx_lut[px];
                    px_col = nl_rgb565_lerp(vrow_pix[x0s], vrow_pix[x1s], fx);
                    if (src_alp) a255 = ((unsigned)vrow_alp[x0s] * (32U - fx) + (unsigned)vrow_alp[x1s] * fx + 16U) >> 5;
                } else {
                    int u256 = u_fp >> 8;
                    px_col = sample_rgb565_bilinear_fast(src_pix, iw, ih, u256, v256);
                    if (src_alp) a255 = sample_alpha_bilinear_fast(src_alp, iw, ih, u256, v256);
                }
                if (a255 == 0U) continue;
                if (surface_lift > 0U) px_col = nl_rgb565_lerp(px_col, RGB565(250, 252, 255), surface_lift);
                if (edge_fade) px_col = nl_rgb565_lerp(px_col, th->bg_main, fade_a);
                if (a255 >= 255U && cov >= 32U) {
                    dst_row[px] = px_col;
                } else {
                    unsigned a32 = (a255 >= 255U) ? 32U : ((a255 + 4U) >> 3);
                    unsigned eff = (cov >= 32U) ? a32 : ((a32 * cov + 16U) >> 5);
                    if (eff > 0U) dst_row[px] = (eff >= 32U) ? px_col : nl_rgb565_lerp(dst_row[px], px_col, eff);
                }
            }

            /* Fast solid interior span (cov == 32 guaranteed, zero aa_rounded_card_pixel calls!) */
            if (exact_1to1 && !src_alp && !edge_fade) {
                const uint16_t *srow = src_pix + iy_fit * iw + (left_end - bx);
                int cnt = right_start - left_end;
                if (cnt > 0) {
                    if (surface_lift == 0U) {
                        memcpy(dst_row + left_end, srow, (size_t)cnt * sizeof(uint16_t));
                    } else {
                        uint16_t *d = dst_row + left_end;
                        for (int k = 0; k < cnt; k++) {
                            d[k] = nl_rgb565_lerp(srow[k], RGB565(250, 252, 255), surface_lift);
                        }
                    }
                }
                u_fp += cnt * du_fp;
            } else if (use_sep && !src_alp && surface_lift == 0U && !edge_fade) {
                for (int px = left_end; px < right_start; px++) {
                    dst_row[px] = nl_rgb565_lerp(vrow_pix[u_x0_lut[px]], vrow_pix[u_x1_lut[px]], u_fx_lut[px]);
                }
                u_fp += (right_start - left_end) * du_fp;
            } else if (exact_1to1 && src_alp != NULL && !edge_fade) {
                const uint16_t *srow = src_pix + iy_fit * iw + (left_end - bx);
                const uint8_t  *arow = src_alp + iy_fit * iw + (left_end - bx);
                int cnt = right_start - left_end;
                uint16_t *d = dst_row + left_end;
                for (int k = 0; k < cnt; k++) {
                    unsigned a255 = arow[k];
                    if (a255 == 0U) continue;
                    uint16_t px_col = srow[k];
                    if (surface_lift > 0U) px_col = nl_rgb565_lerp(px_col, RGB565(250, 252, 255), surface_lift);
                    if (a255 >= 255U) {
                        d[k] = px_col;
                    } else {
                        unsigned a32 = (a255 + 4U) >> 3;
                        if (a32 > 0U) d[k] = (a32 >= 32U) ? px_col : nl_rgb565_lerp(d[k], px_col, a32);
                    }
                }
                u_fp += cnt * du_fp;
            } else {
                for (int px = left_end; px < right_start; px++, u_fp += du_fp) {
                    uint16_t px_col;
                    unsigned a255 = 255U;
                    if (exact_1to1) {
                        int idx = iy_fit * iw + (px - bx);
                        px_col = src_pix[idx];
                        if (src_alp) a255 = src_alp[idx];
                    } else if (use_sep) {
                        int x0s = u_x0_lut[px], x1s = u_x1_lut[px];
                        unsigned fx = u_fx_lut[px];
                        px_col = nl_rgb565_lerp(vrow_pix[x0s], vrow_pix[x1s], fx);
                        if (src_alp) a255 = ((unsigned)vrow_alp[x0s] * (32U - fx) + (unsigned)vrow_alp[x1s] * fx + 16U) >> 5;
                    } else {
                        int u256 = u_fp >> 8;
                        px_col = sample_rgb565_bilinear_fast(src_pix, iw, ih, u256, v256);
                        if (src_alp) a255 = sample_alpha_bilinear_fast(src_alp, iw, ih, u256, v256);
                    }
                    if (a255 == 0U) continue;
                    if (surface_lift > 0U) px_col = nl_rgb565_lerp(px_col, RGB565(250, 252, 255), surface_lift);
                    if (edge_fade) px_col = nl_rgb565_lerp(px_col, th->bg_main, fade_a);
                    if (a255 >= 255U) {
                        dst_row[px] = px_col;
                    } else {
                        unsigned a32 = (a255 + 4U) >> 3;
                        if (a32 > 0U) dst_row[px] = (a32 >= 32U) ? px_col : nl_rgb565_lerp(dst_row[px], px_col, a32);
                    }
                }
            }

            /* Right rounded corner span */
            for (int px = right_start; px < bx1; px++, u_fp += du_fp) {
                int ix_fit = px - bx;
                bool is_border = false;
                unsigned cov;
                if (!in_corner_y) {
                    cov = 32U;
                } else if (clut_row && ix_fit >= fw - r_eff) {
                    cov = (unsigned)(clut_row[fw - 1 - ix_fit] & 0xFEU);
                } else {
                    cov = aa_rounded_card_pixel(ix_fit, iy_fit, fw, fh, r_eff, &is_border);
                    (void)is_border;
                }
                if (cov == 0U) continue;

                uint16_t px_col;
                unsigned a255 = 255U;
                if (exact_1to1) {
                    int idx = iy_fit * iw + ix_fit;
                    px_col = src_pix[idx];
                    if (src_alp) a255 = src_alp[idx];
                } else if (use_sep) {
                    int x0s = u_x0_lut[px], x1s = u_x1_lut[px];
                    unsigned fx = u_fx_lut[px];
                    px_col = nl_rgb565_lerp(vrow_pix[x0s], vrow_pix[x1s], fx);
                    if (src_alp) a255 = ((unsigned)vrow_alp[x0s] * (32U - fx) + (unsigned)vrow_alp[x1s] * fx + 16U) >> 5;
                } else {
                    int u256 = u_fp >> 8;
                    px_col = sample_rgb565_bilinear_fast(src_pix, iw, ih, u256, v256);
                    if (src_alp) a255 = sample_alpha_bilinear_fast(src_alp, iw, ih, u256, v256);
                }
                if (a255 == 0U) continue;
                if (surface_lift > 0U) px_col = nl_rgb565_lerp(px_col, RGB565(250, 252, 255), surface_lift);
                if (edge_fade) px_col = nl_rgb565_lerp(px_col, th->bg_main, fade_a);
                if (a255 >= 255U && cov >= 32U) {
                    dst_row[px] = px_col;
                } else {
                    unsigned a32 = (a255 >= 255U) ? 32U : ((a255 + 4U) >> 3);
                    unsigned eff = (cov >= 32U) ? a32 : ((a32 * cov + 16U) >> 5);
                    if (eff > 0U) dst_row[px] = (eff >= 32U) ? px_col : nl_rgb565_lerp(dst_row[px], px_col, eff);
                }
            }
        }
    } else {
        uint16_t bg_top = nl_rgb565_lerp(th->card_bg, g->accent_color, th->is_light ? 4U : 5U);
        uint16_t bg_bot = nl_rgb565_lerp(th->is_light ? th->bg_top_bar : th->bg_main, g->accent_color, 2U);
        int r_eff = radius;
        if (r_eff * 2 > cw) r_eff = cw / 2;
        if (r_eff * 2 > ch) r_eff = ch / 2;
        if (r_eff < 0) r_eff = 0;
        const uint8_t *corner_lut = (r_eff >= 1 && r_eff <= NL_AA_CORNER_LUT_MAX_R)
            ? aa_corner_get_lut(r_eff)
            : NULL;

        for (int py = y0; py < y1; py++) {
            int dy = py - cy;
            uint16_t row_base = nl_rgb565_lerp(bg_top, bg_bot, (unsigned)((dy * 32) / ch));
            if (surface_lift > 0U) {
                row_base = nl_rgb565_lerp(row_base, RGB565(250, 252, 255), surface_lift);
            }
            bool edge_fade = (clip_y1 == NL_VIEW_BOTTOM && py >= NL_VIEW_BOTTOM - 5);
            unsigned fade_a = edge_fade ? (unsigned)(py - (NL_VIEW_BOTTOM - 5)) * 4U : 0U;
            if (edge_fade) {
                row_base = nl_rgb565_lerp(row_base, th->bg_main, fade_a);
            }
            uint16_t row_border_col = nl_rgb565_lerp(row_base, th->border_subtle, 24U);
            bool in_corner_y = (dy < r_eff || dy >= ch - r_eff);
            bool border_row = (dy == 0 || dy == ch - 1);
            const uint8_t *clut_row = (corner_lut && in_corner_y)
                ? (corner_lut + ((dy < r_eff) ? dy : (ch - 1 - dy)) * r_eff)
                : NULL;

            int left_end = in_corner_y ? (cx + r_eff) : (cx + 1);
            int right_start = in_corner_y ? (cx + cw - r_eff) : (cx + cw - 1);
            if (left_end < x0) left_end = x0;
            if (left_end > x1) left_end = x1;
            if (right_start < left_end) right_start = left_end;
            if (right_start > x1) right_start = x1;

            uint16_t *dst_row = fb + py * NL_SCREEN_W;
            for (int px = x0; px < left_end; px++) {
                int ix = px - cx;
                bool is_border = false;
                unsigned cov;
                if (!in_corner_y) {
                    cov = 32U;
                    is_border = (ix == 0 || ix == cw - 1 || border_row);
                } else if (clut_row && ix < r_eff) {
                    uint8_t le = clut_row[ix];
                    cov = (unsigned)(le & 0xFEU);
                    is_border = (le & 1U) != 0U;
                } else {
                    cov = aa_rounded_card_pixel(ix, dy, cw, ch, r_eff, &is_border);
                }
                if (cov == 0U) continue;
                uint16_t px_col = is_border ? row_border_col : row_base;
                dst_row[px] = (cov >= 32U) ? px_col : nl_rgb565_lerp(dst_row[px], px_col, cov);
            }
            uint16_t fill_col = border_row ? row_border_col : row_base;
            for (int px = left_end; px < right_start; px++) {
                dst_row[px] = fill_col;
            }
            for (int px = right_start; px < x1; px++) {
                int ix = px - cx;
                bool is_border = false;
                unsigned cov;
                if (!in_corner_y) {
                    cov = 32U;
                    is_border = (ix == 0 || ix == cw - 1 || border_row);
                } else if (clut_row && ix >= cw - r_eff) {
                    uint8_t le = clut_row[cw - 1 - ix];
                    cov = (unsigned)(le & 0xFEU);
                    is_border = (le & 1U) != 0U;
                } else {
                    cov = aa_rounded_card_pixel(ix, dy, cw, ch, r_eff, &is_border);
                }
                if (cov == 0U) continue;
                uint16_t px_col = is_border ? row_border_col : row_base;
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

static void draw_game_cover_card(
    uint16_t *fb,
    int cx, int cy, int cw, int ch,
    int radius,
    int hl_0_to_32,
    const nl_game_t *g,
    const nl_theme_t *th,
    int clip_y0, int clip_y1
) {
    draw_game_cover_card_ex(fb, cx, cy, cw, ch, radius, hl_0_to_32, g, th, clip_y0, clip_y1, true);
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

/* R16: Crisp Orbital Prism Pulse Rings & 4 Glinting Micro-Sparks (refined, bounded, fast on ARMv5TE) */
static void draw_prism_pulse_rings(
    uint16_t *fb,
    int cx,
    int cy,
    int progress_fp,
    int min_radius,
    int max_radius,
    int ring_count,
    uint16_t phase,
    bool draw_sparks,
    const nl_theme_t *th,
    int y_min,
    int y_max
) {
    if (progress_fp <= 0 || progress_fp >= 256 || max_radius <= min_radius) return;
    if (ring_count < 1) ring_count = 1;
    if (ring_count > 3) ring_count = 3;

    static const uint16_t k_prism_dark[4] = {
        RGB565(96, 195, 255),
        RGB565(192, 132, 252),
        RGB565(251, 146, 196),
        RGB565(125, 232, 210)
    };
    static const uint16_t k_prism_light[4] = {
        RGB565(37, 99, 235),
        RGB565(147, 51, 234),
        RGB565(219, 39, 119),
        RGB565(13, 148, 136)
    };
    const uint16_t *pal = th->is_light ? k_prism_light : k_prism_dark;

    for (int w = 0; w < ring_count; w++) {
        int wp = progress_fp - w * 40;
        if (wp <= 0 || wp >= 200) continue;
        int inv_w = 200 - wp;
        int ease_w = 200 - ((inv_w * inv_w) / 200);
        int rad = min_radius + ((max_radius - min_radius) * ease_w) / 200;
        unsigned base_a = (unsigned)(((200 - wp) * (24 - w * 4)) / 200);
        if (base_a == 0U) continue;
        if (base_a > 24U) base_a = 24U;

        uint16_t col = nl_rgb565_lerp(th->accent_primary, pal[(w + (phase >> 6)) & 3], 18U);
        int r_out = rad + 1;
        int r_in  = (rad > 1) ? (rad - 1) : 1;
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
                int dist = (d2 >= r_mid2) ? (d2 - r_mid2) : (r_mid2 - d2);
                int span = (d2 >= r_mid2) ? (r_out2 - r_mid2) : (r_mid2 - r_in2);
                unsigned a = (span > 0) ? (unsigned)((base_a * (unsigned)(span - dist)) / (unsigned)span) : base_a;
                if (a > 0U) {
                    row[px] = nl_rgb565_lerp(row[px], col, a);
                }
            }
        }
    }

    /* Optional 4 delicate glinting orbital micro-sparks around the perimeter */
    if (draw_sparks && progress_fp > 16 && progress_fp < 224) {
        static const int16_t k_cos32[32] = {
            256, 251, 237, 213, 181, 142, 98, 50,
              0, -50, -98,-142,-181,-213,-237,-251,
           -256,-251,-237,-213,-181,-142,-98,-50,
              0,  50,  98, 142, 181, 213, 237, 251
        };
        int spark_r = min_radius + 4 + ((max_radius - min_radius) * progress_fp) / 384;
        unsigned sp_a = (progress_fp < 120)
            ? (unsigned)(((progress_fp - 16) * 26) / 104)
            : (unsigned)(((224 - progress_fp) * 26) / 104);
        if (sp_a > 26U) sp_a = 26U;
        if (sp_a > 2U) {
            for (int s = 0; s < 4; s++) {
                int ang = ((int)((phase >> 3) + (uint16_t)(s * 8))) & 31;
                int sx = cx + ((spark_r * (int)k_cos32[ang]) >> 8);
                int sy = cy + ((spark_r * (int)k_cos32[(ang + 24) & 31]) >> 8);
                uint16_t scol = pal[s & 3];
                for (int oy = -1; oy <= 1; oy++) {
                    int py = sy + oy;
                    if (py < y_min || py >= y_max || py < 0 || py >= NL_SCREEN_H) continue;
                    for (int ox = -1; ox <= 1; ox++) {
                        int px = sx + ox;
                        if (px < 0 || px >= NL_SCREEN_W) continue;
                        if (ox != 0 && oy != 0) continue;
                        unsigned pa = (ox == 0 && oy == 0) ? sp_a : (sp_a >> 1);
                        fb[py * NL_SCREEN_W + px] = nl_rgb565_lerp(fb[py * NL_SCREEN_W + px], scol, pa);
                    }
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
    out->page_stride_x= a->page_stride_x+ (((b->page_stride_x- a->page_stride_x)* fp + 128) >> 8);
    out->view_bottom  = a->view_bottom  + (((b->view_bottom  - a->view_bottom)  * fp + 128) >> 8);
    out->pagination_y = a->pagination_y + (((b->pagination_y - a->pagination_y) * fp + 128) >> 8);
}

static void get_layout_metrics_2d(nl_layout_t layout, nl_scroll_dir_t scroll_dir,
                                  int names_fp, int sc_fp, int item_count, nl_layout_metrics_t *out) {
    nl_layout_metrics_t m_n0_s0, m_n0_s1, m_n1_s0, m_n1_s1;
    nl_get_layout_metrics_full(layout, scroll_dir, false, false, item_count, &m_n0_s0);
    nl_get_layout_metrics_full(layout, scroll_dir, false, true,  item_count, &m_n0_s1);
    nl_get_layout_metrics_full(layout, scroll_dir, true,  false, item_count, &m_n1_s0);
    nl_get_layout_metrics_full(layout, scroll_dir, true,  true,  item_count, &m_n1_s1);

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
    get_layout_metrics_2d(state->settings.layout, state->settings.scroll_dir, nm_fp, sc_fp, state->filtered_count, &m_cur);
    if (lt_fp >= 256 || state->anim.layout_from == state->settings.layout) {
        *out = m_cur;
        return;
    }
    nl_layout_metrics_t m_prev;
    get_layout_metrics_2d(state->anim.layout_from, state->settings.scroll_dir, nm_fp, sc_fp, state->filtered_count, &m_prev);
    lerp_layout_metrics_pair(&m_prev, &m_cur, lt_fp, out);
}

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
                uint16_t *scratch = renderer_get_scratch_b();
                if (scratch) {
                    memcpy(scratch, fb + y0 * NL_SCREEN_W, (size_t)(h * NL_SCREEN_W) * sizeof(uint16_t));
                    fb_fill_rounded_rect_clipped(fb, bx - 1, slide_y - 1, bw + 2, 19, 6, th->border_subtle, y0, y1);
                    fb_fill_rounded_rect_clipped(fb, bx, slide_y, bw, 17, 5, th->bg_top_bar, y0, y1);
                    fb_draw_text_ellipsis(fb, bx + 10, slide_y + 5, state->anim.toast_text, th->accent_secondary, 44, y0, y1);
                    for (int r = 0; r < h; r++) {
                        uint16_t *dst = fb + (y0 + r) * NL_SCREEN_W;
                        const uint16_t *old_r = scratch + r * NL_SCREEN_W;
                        for (int x = bx - 1; x < bx + bw + 1; x++) {
                            if ((unsigned)x < NL_SCREEN_W && dst[x] != old_r[x]) {
                                dst[x] = nl_rgb565_lerp(old_r[x], dst[x], alpha);
                            }
                        }
                    }
                } else {
                    fb_fill_rounded_rect_clipped(fb, bx - 1, slide_y - 1, bw + 2, 19, 6, th->border_subtle, y0, y1);
                    fb_fill_rounded_rect_clipped(fb, bx, slide_y, bw, 17, 5, th->bg_top_bar, y0, y1);
                    fb_draw_text_ellipsis(fb, bx + 10, slide_y + 5, state->anim.toast_text, th->accent_secondary, 44, y0, y1);
                }
            }
        }
    }
}

/* =========================================================================
 * SETTINGS SCREENS RENDERING (§1 – §12) WITH FLUID ANIMATIONS (A – G)
 * ========================================================================= */

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
    NL_ICO_DIAG,
    NL_ICO_REFRESH,
    NL_ICO_CAPTURE,
    NL_ICO_ICONS,
    NL_ICO_HOTKEY,
    NL_ICO_PROFILES,
    NL_ICO_ANIM,
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
    [NL_ICO_DIAG] = {
        0x020, 0x070, 0x0D8, 0x1A4, 0x1A4, 0x326, 0x306, 0x222, 0x3FE, 0x1FC, 0x000
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
    },
    [NL_ICO_ANIM] = {
        0x000, 0x080, 0x1C0, 0x084, 0x00C, 0x09C, 0x1FC, 0x09C, 0x00C, 0x004, 0x000
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

    bool m_act = (state && nl_mouse_is_active(state) && state->sui.hover_valid);
    bool hov_pwr = (m_act && state->sui.hover_kind == NL_HOVER_SET_POWER);
    bool hov_esc = (m_act && state->sui.hover_kind == NL_HOVER_SET_ESC);

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
            draw_settings_icon_badge(fb, pwr_x, 4, 17, NL_ICO_POWER, hov_pwr ? 32U : 20U, false, th, 0, NL_TOP_BAR_H);
            if (pwr_x + 42 <= 246) {
                fb_fill_rounded_rect_clipped(fb, pwr_x + 19, 6, 22, 13, 3,
                                             hov_pwr ? th->accent_primary : th->pill_bg, 0, NL_TOP_BAR_H);
                fb_draw_text_clipped(fb, pwr_x + 21, 9, "DEL",
                                     hov_pwr ? th->text_on_accent : th->text_secondary, 3, 0, NL_TOP_BAR_H);
            }
            if (hov_pwr) {
                int pw = (pwr_x + 42 <= 246) ? 43 : 19;
                fb_draw_rounded_outline_clipped(fb, pwr_x - 1, 3, pw, 19, 4, th->accent_secondary, 0, NL_TOP_BAR_H);
            }
        }
    }

    /* Right ESC pill */
    fb_fill_rounded_rect_clipped(fb, 250, 5, 60, 15, 4,
                                 hov_esc ? th->accent_primary : th->pill_bg, 0, NL_SCREEN_H);
    fb_draw_text(fb, 255, 9, "ESC", hov_esc ? th->text_on_accent : th->accent_secondary, 3);
    fb_draw_text(fb, 275, 9, "Ret.", hov_esc ? th->text_on_accent : th->text_secondary, 4);
    if (hov_esc) {
        fb_draw_rounded_outline_clipped(fb, 249, 4, 62, 17, 5, th->accent_secondary, 0, NL_TOP_BAR_H);
    }

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

    /* Visual cue for Horizontal carousel mode: subtle adjacent page peeks on left & right */
    if (state->settings.scroll_dir == NL_SCROLL_HORIZONTAL) {
        uint16_t peek_c = nl_rgb565_lerp(th->bg_top_bar, th->card_bg_sel, 16U);
        int p_h = (ph - 22) / 2;
        if (p_h < 8) p_h = 8;
        fb_fill_rounded_rect_clipped(fb, px + 1, py + 7, 5, p_h, 2, peek_c, py + 2, py + ph - 9);
        fb_fill_rounded_rect_clipped(fb, px + 1, py + 11 + p_h, 5, p_h, 2, peek_c, py + 2, py + ph - 9);
        fb_fill_rounded_rect_clipped(fb, px + pw - 6, py + 7, 5, p_h, 2, peek_c, py + 2, py + ph - 9);
        fb_fill_rounded_rect_clipped(fb, px + pw - 6, py + 11 + p_h, 5, p_h, 2, peek_c, py + 2, py + ph - 9);
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

/* Pre-baked 3-model x 3-size cursor sprite + 1px halo cache (static .bss, ZERO heap alloc, ZERO per-frame 3x3 scan) */
typedef struct {
    bool    ready;
    uint8_t w;       /* Sprite width  (<= 28) */
    uint8_t h;       /* Sprite height (<= 28) */
    uint8_t pw;      /* Padded width  = w + 2 (<= 30) */
    uint8_t ph;      /* Padded height = h + 2 (<= 30) */
    uint8_t hx;      /* Hotspot X inside sprite */
    uint8_t hy;      /* Hotspot Y inside sprite */
    uint8_t alpha[30 * 30]; /* Padded silhouette alpha (0..255) */
    uint8_t halo[30 * 30];  /* Pre-baked 1px outer contrast halo alpha (0..255) */
    uint8_t aux[30 * 30];   /* Pointer fill mask (0..255) OR Arrow/Circle gradient t32 (0..32) */
} nl_baked_cursor_t;

static nl_baked_cursor_t G_CUR_CACHE[3][NL_CURSOR_SIZE_COUNT];

static uint8_t sample_mask_area_u8(const uint8_t *src, int sw, int sh, int dx, int dy, int dw, int dh) {
    if (dw == sw && dh == sh) return src[dy * sw + dx];
    int sx0 = (dx * sw) / dw;
    int sx1 = ((dx + 1) * sw + dw - 1) / dw;
    if (sx1 <= sx0) sx1 = sx0 + 1;
    if (sx1 > sw) sx1 = sw;
    int sy0 = (dy * sh) / dh;
    int sy1 = ((dy + 1) * sh + dh - 1) / dh;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sy1 > sh) sy1 = sh;
    uint32_t sum = 0U, cnt = 0U;
    for (int sy = sy0; sy < sy1; sy++) {
        const uint8_t *srow = src + sy * sw;
        for (int sx = sx0; sx < sx1; sx++) {
            sum += srow[sx];
            cnt++;
        }
    }
    if (cnt == 0U) return 0U;
    uint32_t v = (sum + cnt / 2U) / cnt;
    return (v > 255U) ? 255U : (uint8_t)v;
}

static const nl_baked_cursor_t *get_baked_cursor(nl_cursor_style_t style, nl_cursor_size_t size) {
    int m_idx = (style == NL_CURSOR_STYLE_CIRCLE) ? 1 : ((style == NL_CURSOR_STYLE_POINTER) ? 2 : 0);
    int s_idx = (int)size;
    if (s_idx < 0) s_idx = 0;
    if (s_idx >= (int)NL_CURSOR_SIZE_COUNT) s_idx = (int)NL_CURSOR_SIZE_LARGE;

    nl_baked_cursor_t *c = &G_CUR_CACHE[m_idx][s_idx];
    if (c->ready) return c;

    const uint8_t *src_alpha = G_CURSOR_ARROW_28;
    const uint8_t *src_fill = NULL;
    int src_w = NL_CURSOR_ARROW_28_W;
    int src_h = NL_CURSOR_ARROW_28_H;
    if (m_idx == 1) {
        src_alpha = G_CURSOR_CIRCLE_28;
        src_w = NL_CURSOR_CIRCLE_28_W;
        src_h = NL_CURSOR_CIRCLE_28_H;
    } else if (m_idx == 2) {
        src_alpha = G_CURSOR_POINTER_ALPHA_28;
        src_fill = G_CURSOR_POINTER_FILL_28;
        src_w = NL_CURSOR_POINTER_W;
        src_h = NL_CURSOR_POINTER_H;
    }

    int dw = 21, dh = 21, dhx = 5, dhy = 2;
    nl_cursor_get_size_metrics(style, (nl_cursor_size_t)s_idx, &dw, &dh, &dhx, &dhy);
    if (dw > 28) dw = 28;
    if (dh > 28) dh = 28;

    memset(c, 0, sizeof(*c));
    c->w = (uint8_t)dw;
    c->h = (uint8_t)dh;
    c->pw = (uint8_t)(dw + 2);
    c->ph = (uint8_t)(dh + 2);
    c->hx = (uint8_t)dhx;
    c->hy = (uint8_t)dhy;

    const int pw = c->pw;
    const int ph = c->ph;
    const int grad_den = (dw > dh) ? dw : dh;

    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            int p_off = (dy + 1) * pw + (dx + 1);
            c->alpha[p_off] = sample_mask_area_u8(src_alpha, src_w, src_h, dx, dy, dw, dh);
            if (src_fill != NULL) {
                c->aux[p_off] = sample_mask_area_u8(src_fill, src_w, src_h, dx, dy, dw, dh);
            } else {
                int dist_hot = abs(dx - dhx) + abs(dy - dhy);
                int gt = (grad_den > 0) ? ((dist_hot * 32) / grad_den) : 0;
                if (gt > 32) gt = 32;
                c->aux[p_off] = (uint8_t)gt;
            }
        }
    }

    /* Pre-bake 1px contrast halo once so runtime rendering needs ZERO 3x3 loops */
    for (int py = 0; py < ph; py++) {
        for (int px = 0; px < pw; px++) {
            int p_off = py * pw + px;
            uint8_t a8 = c->alpha[p_off];
            if (a8 >= 180U) {
                c->halo[p_off] = 0U;
                continue;
            }
            uint8_t max_nb = 0U;
            int y0 = (py > 0) ? (py - 1) : 0;
            int y1 = (py + 1 < ph) ? (py + 1) : (ph - 1);
            int x0 = (px > 0) ? (px - 1) : 0;
            int x1 = (px + 1 < pw) ? (px + 1) : (pw - 1);
            for (int ny = y0; ny <= y1; ny++) {
                const uint8_t *arow = c->alpha + ny * pw;
                for (int nx = x0; nx <= x1; nx++) {
                    if (arow[nx] > max_nb) max_nb = arow[nx];
                }
            }
            if (max_nb > 48U) {
                int hval = (int)max_nb - (int)(a8 >> 1);
                c->halo[p_off] = (hval > 0) ? (uint8_t)hval : 0U;
            }
        }
    }

    c->ready = true;
    return c;
}

/* Fast pre-baked cursor blitter supporting all 3 models x 3 proportional sizes (ZERO divisions, ZERO 3x3 scan) */
static void draw_cursor_alpha_sprite_sized(
    uint16_t *fb,
    nl_cursor_style_t style,
    nl_cursor_size_t size,
    int hx,
    int hy,
    unsigned opacity_256,
    bool draw_halo,
    int clip_top,
    int clip_bot,
    uint16_t anim_phase,
    const nl_theme_t *th
) {
    if (!fb || !th || opacity_256 == 0U) return;
    if (style != NL_CURSOR_STYLE_ARROW &&
        style != NL_CURSOR_STYLE_CIRCLE &&
        style != NL_CURSOR_STYLE_POINTER) {
        return;
    }
    if (opacity_256 > 256U) opacity_256 = 256U;
    if (style == NL_CURSOR_STYLE_POINTER) init_ink_sin_lut();

    const nl_baked_cursor_t *bc = get_baked_cursor(style, size);
    const int pw = (int)bc->pw;
    const int ph = (int)bc->ph;
    const int sx = hx - (int)bc->hx - 1;
    const int sy = hy - (int)bc->hy - 1;

    const uint16_t halo_col = th->is_light ? RGB565(15, 23, 42) : RGB565(255, 255, 255);
    const uint16_t core_hi  = nl_rgb565_lerp(th->accent_secondary, RGB565(255, 255, 255), th->is_light ? 6U : 11U);
    const uint16_t core_lo  = th->accent_primary;
    const uint16_t ptr_edge = RGB565(20, 24, 33);
    const uint16_t ptr_top  = nl_rgb565_lerp(RGB565(8, 198, 224), th->accent_secondary, 10U);
    const uint16_t ptr_bot  = nl_rgb565_lerp(RGB565(20, 42, 86), th->accent_primary, 10U);
    const unsigned halo_str = (style == NL_CURSOR_STYLE_POINTER) ? (th->is_light ? 6U : 10U) : 18U;
    const unsigned halo_max = (style == NL_CURSOR_STYLE_POINTER) ? 11U : 20U;
    const unsigned halo_mul = (opacity_256 * halo_str);
    const unsigned ptr_y_step = (bc->h > 0U) ? (8192U / (unsigned)bc->h) : 256U; /* 32*256 / h */

    int by0 = 0;
    if (sy < clip_top) by0 = clip_top - sy;
    if (sy + by0 < 0) by0 = -sy;
    int by1 = ph;
    if (sy + by1 > clip_bot) by1 = clip_bot - sy;
    if (sy + by1 > NL_SCREEN_H) by1 = NL_SCREEN_H - sy;
    if (by0 >= by1) return;

    int bx0 = (sx < 0) ? -sx : 0;
    int bx1 = (sx + pw > NL_SCREEN_W) ? (NL_SCREEN_W - sx) : pw;
    if (bx0 >= bx1) return;

    for (int by = by0; by < by1; by++) {
        uint16_t *row = fb + (sy + by) * NL_SCREEN_W;
        const uint8_t *arow = bc->alpha + by * pw;
        const uint8_t *hrow = bc->halo + by * pw;
        const uint8_t *xrow = bc->aux + by * pw;
        int dy_grad = (by > 0) ? (int)(((unsigned)(by - 1) * ptr_y_step) >> 8) : 0;

        for (int bx = bx0; bx < bx1; bx++) {
            uint8_t a8 = arow[bx];
            uint8_t h8 = draw_halo ? hrow[bx] : 0U;
            if (a8 == 0U && h8 == 0U) continue;
            int px = sx + bx;

            if (h8 > 0U) {
                unsigned halo_a = ((unsigned)h8 * halo_mul) >> 16;
                if (halo_a > halo_max) halo_a = halo_max;
                if (halo_a > 0U) {
                    row[px] = nl_rgb565_lerp(row[px], halo_col, halo_a);
                }
            }
            if (a8 == 0U) continue;

            uint16_t fill_col;
            if (style == NL_CURSOR_STYLE_POINTER) {
                uint8_t f8 = xrow[bx];
                if (f8 == 0U) {
                    fill_col = ptr_edge;
                } else {
                    int wave = (int)G_INK_SIN256[((by * 9 + bx * 5 + (int)anim_phase) & 255)];
                    int t32 = dy_grad + (wave >> 4);
                    if (t32 < 0) t32 = 0; else if (t32 > 32) t32 = 32;
                    uint16_t grad_c = nl_rgb565_lerp(ptr_top, ptr_bot, (unsigned)t32);
                    fill_col = (f8 >= 248U) ? grad_c : nl_rgb565_lerp(ptr_edge, grad_c, ((unsigned)f8 + 4U) >> 3);
                }
            } else {
                fill_col = nl_rgb565_lerp(core_hi, core_lo, (unsigned)xrow[bx]);
            }

            unsigned a32 = ((unsigned)a8 * opacity_256 + 1024U) >> 11; /* * 32 / 65536 */
            if (a32 > 32U) a32 = 32U;
            if (a32 > 0U) {
                row[px] = nl_rgb565_lerp(row[px], fill_col, a32);
            }
        }
    }
}

/* Render the Mouse Mode graphical cursor + proportional 5-node trail + subtle click pulse ring.
 * Mode 1 (Pave tactile) WITHOUT CTRL strictly renders ZERO trail and ZERO cursor. */
static void draw_touchpad_graphical_cursor(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th
) {
    if (!state || !fb || !th) return;
    if (state->anim.launch_anim_active) return;

    if (!nl_cursor_is_visible(state)) return;

    int op_fp = state->anim.cur_opacity_fp;
    if (op_fp <= 0) op_fp = 256;
    if (op_fp > 256) op_fp = 256;

    nl_cursor_style_t style = state->settings.touchpad_cursor_style;
    if (style != NL_CURSOR_STYLE_ARROW &&
        style != NL_CURSOR_STYLE_CIRCLE &&
        style != NL_CURSOR_STYLE_POINTER) {
        style = NL_CURSOR_STYLE_ARROW;
    }
    nl_cursor_size_t csize = state->settings.cursor_size;
    if ((unsigned)csize >= (unsigned)NL_CURSOR_SIZE_COUNT) {
        csize = NL_CURSOR_SIZE_DEFAULT;
    }

    int hx = (state->anim.cur_initialized) ? ((state->anim.cur_x_fp + 8) >> 4) : 160;
    int hy = (state->anim.cur_initialized) ? ((state->anim.cur_y_fp + 8) >> 4) : 116;
    if (hx < 0) hx = 0;
    if (hx >= NL_SCREEN_W) hx = NL_SCREEN_W - 1;
    if (hy < 0) hy = 0;
    if (hy >= NL_SCREEN_H) hy = NL_SCREEN_H - 1;

    /* 1. Proportional short luminous trail behind the moving cursor (scales cleanly with cursor size) */
    static const uint8_t k_trail_rad[NL_CURSOR_SIZE_COUNT][3] = {
        { 3, 2, 1 }, /* Petite  */
        { 4, 3, 2 }, /* Moyenne */
        { 5, 4, 3 }  /* Grande  */
    };
    uint16_t trail_core = nl_rgb565_lerp(th->accent_secondary, RGB565(255, 255, 255), th->is_light ? 4U : 10U);
    uint16_t trail_glow = th->accent_primary;
    for (int i = NL_CURSOR_TRAIL_LEN - 1; i >= 0; i--) {
        uint8_t ta8 = state->anim.cur_trail_alpha[i];
        if (ta8 == 0U) continue;
        int tx = (state->anim.cur_trail_x_fp[i] + 8) >> 4;
        int ty = (state->anim.cur_trail_y_fp[i] + 8) >> 4;
        if (abs(tx - hx) <= 1 && abs(ty - hy) <= 1) continue;

        /* Single lightweight no-halo ghost silhouette for closest wake node (fast on ARM926EJ-S) */
        if (i == 0) {
            unsigned ghost_op = ((unsigned)ta8 * (unsigned)op_fp) >> 10;
            if (ghost_op >= 14U) {
                draw_cursor_alpha_sprite_sized(fb, style, csize, tx, ty, ghost_op, false,
                                               0, NL_SCREEN_H, state->anim.cur_style_phase, th);
            }
        }

        int rad = (i == 0) ? (int)k_trail_rad[csize][0]
                : ((i <= 2) ? (int)k_trail_rad[csize][1] : (int)k_trail_rad[csize][2]);
        unsigned pa32 = ((unsigned)ta8 * (unsigned)op_fp * 18U) >> 16;
        if (pa32 > 20U) pa32 = 20U;
        if (pa32 == 0U) continue;

        uint16_t pcol = (i <= 1) ? trail_core : trail_glow;
        int r2_p1 = rad * rad + 1;
        unsigned inv_r2 = 256U / (unsigned)r2_p1;
        for (int dy = -rad; dy <= rad; dy++) {
            int py = ty + dy;
            if ((unsigned)py >= NL_SCREEN_H) continue;
            uint16_t *row = fb + py * NL_SCREEN_W;
            int dy2 = dy * dy;
            for (int dx = -rad; dx <= rad; dx++) {
                int px = tx + dx;
                if ((unsigned)px >= NL_SCREEN_W) continue;
                int d2 = dx * dx + dy2;
                if (d2 > rad * rad) continue;
                unsigned falloff = ((unsigned)(r2_p1 - d2) * pa32 * inv_r2) >> 8;
                if (falloff > 0U) {
                    row[px] = nl_rgb565_lerp(row[px], pcol, falloff);
                }
            }
        }
    }

    /* 2. Subtle click / tap-to-click confirmation ring expanding from (hx, hy) */
    if (state->anim.cur_click_pulse_fp > 8) {
        int p = 256 - state->anim.cur_click_pulse_fp; /* 0..248 */
        int base_r = (csize == NL_CURSOR_SIZE_SMALL) ? 5 : ((csize == NL_CURSOR_SIZE_LARGE) ? 9 : 7);
        int ring_r = base_r + ((p * 7) >> 8);
        unsigned ring_a = (unsigned)((state->anim.cur_click_pulse_fp * 22) >> 8);
        if (ring_a > 0U) {
            int r_out2 = (ring_r + 1) * (ring_r + 1);
            int r_in2 = (ring_r > 1) ? ((ring_r - 1) * (ring_r - 1)) : 0;
            for (int dy = -ring_r - 1; dy <= ring_r + 1; dy++) {
                int py = hy + dy;
                if ((unsigned)py >= NL_SCREEN_H) continue;
                uint16_t *row = fb + py * NL_SCREEN_W;
                int dy2 = dy * dy;
                for (int dx = -ring_r - 1; dx <= ring_r + 1; dx++) {
                    int px = hx + dx;
                    if ((unsigned)px >= NL_SCREEN_W) continue;
                    int d2 = dx * dx + dy2;
                    if (d2 >= r_in2 && d2 <= r_out2) {
                        row[px] = nl_rgb565_lerp(row[px], th->accent_secondary, ring_a);
                    }
                }
            }
        }
    }

    /* 3. Active graphical cursor sprite at (hx, hy) */
    draw_cursor_alpha_sprite_sized(fb, style, csize, hx, hy, (unsigned)op_fp, true,
                                   0, NL_SCREEN_H, state->anim.cur_style_phase, th);
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
                "Animations",
                "Navigation tactile",
                "Touches de lancement",
                "Commandes du launcher",
                "Tri de la bibliotheque",
                "Statistiques de jeu",
                "Generer captures d'ecran",
                "Gestion des icones",
                "Rescanner les jeux",
                "Raccourci global TI-OS",
                "Profils utilisateurs",
                "Diagnostics & Journaux",
                "Reinitialiser les reglages",
                "Informations systeme",
                "A propos et credits"
            };

            static const char *const subtitles[NL_SET_MENU_COUNT] = {
                "Grille, titres et barre d'aide",
                "Animation de lancement des jeux",
                "Mode pave tactile / souris",
                "Touche directe vers un jeu",
                "Touches et actions systeme",
                "Ordre d'affichage des jeux",
                "Temps de jeu et sessions",
                "Capturer les jeux automatiquement",
                "Voir ou supprimer une icone",
                "Verifier dossiers et actualiser",
                "Ouvrir depuis menus TI-OS (manuel)",
                "Comptes, avatars et donnees",
                "Erreurs, boot et journaux",
                "Valeurs par defaut sans perte",
                "Version, TI-OS et Ndless",
                "Createur et projet N Launcher"
            };

            static const nl_set_icon_id_t menu_icons[NL_SET_MENU_COUNT] = {
                NL_ICO_SCREEN,
                NL_ICO_ANIM,
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
                NL_ICO_DIAG,
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
                bool hovered = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == idx);

                uint16_t bg_col = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, row_alpha);
                fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6, bg_col, 28, 218);
                if (hovered && !sel) {
                    fb_draw_rounded_outline_clipped(fb, 12, ry, 292, row_h, 6, th->accent_secondary, 28, 218);
                }

                /* Left cohesive category icon badge (18x18 rounded square with 11x11 glyph) */
                draw_settings_icon_badge(fb, 17, ry + 5, 18, menu_icons[idx],
                                         row_alpha, is_danger, th, 28, 218);

                /* Compact right-side value pill */
                char val_buf[32] = "";
                if (idx == NL_SET_MENU_DISPLAY) {
                    snprintf(val_buf, sizeof(val_buf), "%s", nl_layout_label(state->settings.layout));
                } else if (idx == NL_SET_MENU_ANIMATIONS) {
                    snprintf(val_buf, sizeof(val_buf), "%s", nl_launch_anim_short_label(state->settings.launch_anim_mode));
                } else if (idx == NL_SET_MENU_TOUCHPAD) {
                    snprintf(val_buf, sizeof(val_buf), "%s",
                             (state->settings.touchpad_mode == NL_TP_MODE_MOUSE)
                                 ? "Souris"
                                 : (state->settings.touchpad_nav ? "ON" : "OFF"));
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
                } else if (idx == NL_SET_MENU_DIAG) {
                    if (state->diag.count > 0) {
                        snprintf(val_buf, sizeof(val_buf), "! %d", state->diag.count);
                    } else {
                        snprintf(val_buf, sizeof(val_buf), "OK");
                    }
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
                    uint16_t val_col = (idx == NL_SET_MENU_DIAG && state->diag.count > 0)
                        ? RGB565(251, 191, 36)
                        : nl_rgb565_lerp(th->text_secondary, th->accent_secondary, row_alpha);
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

        case NL_UI_SETTINGS_DIAG: {
            static const char *const hints_t0[3] = {"TAB Onglet", "EXE Details", "SUPPR Effacer"};
            static const char *const hints_t1[3] = {"TAB Onglet", "U/D Etapes", "ESC Retour"};
            static const char *const hints_det[2] = {"EXE/ESC Fermer", "TAB Onglets"};
            static const char *const hints_mod[3] = {"L/R Choisir", "EXE Valider", "ESC Annuler"};
            const char *const *hptr = sui->diag_confirm_modal ? hints_mod
                : (sui->diag_detail_open ? hints_det : ((sui->diag_tab == 0) ? hints_t0 : hints_t1));
            int hcnt = (sui->diag_detail_open && !sui->diag_confirm_modal) ? 2 : 3;
            draw_settings_chrome(state, fb, th, "DIAGNOSTICS & JOURNAUX", NL_ICO_DIAG, hptr, hcnt);

            bool m_hov = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid);

            /* Sub-tabs at y = 28..44 */
            char t0_lbl[24];
            snprintf(t0_lbl, sizeof(t0_lbl), "1. Erreurs (%d)", state->diag.count);
            for (int t = 0; t < 2; t++) {
                int tx = (t == 0) ? 14 : 160;
                int tw = (t == 0) ? 142 : 146;
                bool active_t = (sui->diag_tab == t);
                bool hov_t = (m_hov && sui->hover_kind == NL_HOVER_DIAG_TAB && sui->hover_index == t);
                fb_fill_rounded_rect_clipped(fb, tx, 28, tw, 16, 4,
                                             active_t ? th->accent_primary : th->card_bg, 26, 48);
                if (hov_t && !active_t) {
                    fb_draw_rounded_outline_clipped(fb, tx, 28, tw, 16, 4, th->accent_secondary, 26, 48);
                }
                fb_draw_text_clipped(fb, tx + 8, 32, (t == 0) ? t0_lbl : "2. Boot & Systeme",
                                     active_t ? th->text_on_accent : th->text_secondary, 22, 26, 48);
            }

            if (sui->diag_tab == 0) {
                /* Summary banner (y = 48..64) */
                char sum_buf[72];
                if (state->diag.count > 0) {
                    snprintf(sum_buf, sizeof(sum_buf), "%d probleme(s) (%lu evts) - Bib. protegee",
                             state->diag.count, (unsigned long)state->diag.total_events);
                } else {
                    snprintf(sum_buf, sizeof(sum_buf), "Aucun probleme detecte - Systeme sain");
                }
                uint16_t sum_bg = (state->diag.count > 0) ? th->card_bg_sel : th->card_bg;
                uint16_t sum_fg = (state->diag.count > 0) ? RGB565(251, 191, 36) : RGB565(74, 222, 128);
                fb_fill_rounded_rect_clipped(fb, 14, 48, 292, 16, 4, sum_bg, 46, 66);
                fb_draw_text_ellipsis(fb, 20, 52, sum_buf, sum_fg, 45, 46, 66);

                if (state->diag.count <= 0) {
                    fb_fill_rounded_rect_clipped(fb, 14, 72, 292, 112, 6, th->card_bg, 66, 190);
                    fb_draw_text(fb, 46, 108, "Aucune anomalie active enregistree.", th->text_primary, 38);
                    fb_draw_text(fb, 46, 124, "Journal permanent: nlauncher.diag.log.tns", th->text_secondary, 42);
                } else {
                    for (int i = 0; i < state->diag.count; i++) {
                        int ry = 67 + (i - sui->diag_scroll) * 30;
                        if (ry + 28 <= 66 || ry >= 190) continue;
                        const nl_diag_entry_t *e = &state->diag.entries[i];
                        bool sel = (sui->diag_cursor == i);
                        bool hov_r = (m_hov && sui->hover_kind == NL_HOVER_DIAG_ROW && sui->hover_index == i);

                        if (sel) {
                            fb_fill_rounded_rect_clipped(fb, 13, ry - 1, 294, 30, 6, th->accent_primary, 66, 190);
                        }
                        fb_fill_rounded_rect_clipped(fb, 14, ry, 292, 28, 5,
                                                     sel ? th->card_bg_sel : th->card_bg, 66, 190);
                        if (hov_r && !sel) {
                            fb_draw_rounded_outline_clipped(fb, 14, ry, 292, 28, 5, th->accent_secondary, 66, 190);
                        }

                        bool is_crit = (e->code == NL_DIAG_E801_LOW_MEMORY || e->code == NL_DIAG_E901_PREV_CRASH || e->code == NL_DIAG_E601_PROFILE_CORRUPT);
                        uint16_t code_col = is_crit ? RGB565(248, 113, 113) : RGB565(251, 191, 36);
                        fb_fill_rounded_rect_clipped(fb, 18, ry + 5, 32, 18, 4, th->pill_bg, 66, 190);
                        fb_draw_text_clipped(fb, 22, ry + 10, nl_diag_code_str(e->code), code_col, 4, 66, 190);

                        char line1[64];
                        if (e->target_name[0] != '\0') {
                            snprintf(line1, sizeof(line1), "%s: %s", e->title, e->target_name);
                        } else {
                            snprintf(line1, sizeof(line1), "%s", e->title);
                        }
                        int max_l1 = (e->count > 1U) ? 31 : 38;
                        fb_draw_text_ellipsis(fb, 56, ry + 5, line1, th->text_primary, max_l1, 66, 190);
                        fb_draw_text_ellipsis(fb, 56, ry + 16, e->recovery, th->accent_secondary, 37, 66, 190);

                        if (e->count > 1U) {
                            char cnt_s[10];
                            snprintf(cnt_s, sizeof(cnt_s), "x%u", (unsigned)e->count);
                            int cw = (int)strlen(cnt_s) * 6 + 6;
                            fb_fill_rounded_rect_clipped(fb, 298 - cw, ry + 7, cw, 14, 4, th->pill_bg, 66, 190);
                            fb_draw_text_clipped(fb, 301 - cw, ry + 10, cnt_s, code_col, 5, 66, 190);
                        }
                    }
                    if (state->diag.count > 4) {
                        draw_scrollbar_v_smooth(fb, 67, 118, sui->diag_scroll * 30, 30, 4, state->diag.count, th);
                    }
                }

                /* Clear Logs action button at bottom (y = 193..215) */
                bool clr_sel = (sui->diag_cursor == state->diag.count);
                bool clr_hov = (m_hov && sui->hover_kind == NL_HOVER_DIAG_CLEAR_BTN);
                if (clr_sel || clr_hov) {
                    fb_fill_rounded_rect_clipped(fb, 13, 192, 294, 24, 6,
                                                 clr_sel ? RGB565(220, 60, 60) : th->accent_secondary, 190, 218);
                }
                fb_fill_rounded_rect_clipped(fb, 14, 193, 292, 22, 5,
                                             clr_sel ? RGB565(90, 26, 30) : th->card_bg, 190, 218);
                fb_draw_text_clipped(fb, 68, 200, "Effacer les anciens journaux",
                                     clr_sel ? RGB565(255, 220, 220) : th->text_primary, 32, 190, 218);

                /* Error Detail Overlay */
                if (sui->diag_detail_open && sui->diag_cursor >= 0 && sui->diag_cursor < state->diag.count) {
                    const nl_diag_entry_t *e = &state->diag.entries[sui->diag_cursor];
                    fb_fill_rounded_rect_clipped(fb, 12, 30, 296, 184, 7, th->accent_primary, 28, 218);
                    fb_fill_rounded_rect_clipped(fb, 13, 31, 294, 182, 6, th->bg_top_bar, 28, 218);

                    char hdr[56];
                    snprintf(hdr, sizeof(hdr), "%s - %s", nl_diag_code_str(e->code), e->title);
                    fb_draw_text_ellipsis(fb, 20, 37, hdr, RGB565(251, 191, 36), 40, 28, 218);
                    fb_fill_rounded_rect_clipped(fb, 284, 34, 18, 14, 4, th->pill_bg, 28, 218);
                    fb_draw_text_clipped(fb, 290, 37, "X", th->text_primary, 1, 28, 218);

                    char buf_d[160];
                    snprintf(buf_d, sizeof(buf_d), "Cat: %s (x%u) | Etape %d", e->category, (unsigned)e->count, (int)e->stage);
                    fb_draw_text_ellipsis(fb, 20, 56, buf_d, th->accent_secondary, 44, 28, 218);

                    snprintf(buf_d, sizeof(buf_d), "Element: %s", e->target_name[0] ? e->target_name : "-");
                    fb_draw_text_ellipsis(fb, 20, 74, buf_d, th->text_primary, 44, 28, 218);

                    snprintf(buf_d, sizeof(buf_d), "Chemin: %s", e->path[0] ? e->path : "-");
                    fb_draw_text_ellipsis(fb, 20, 92, buf_d, th->text_secondary, 44, 28, 218);

                    snprintf(buf_d, sizeof(buf_d), "Op: %s", e->operation[0] ? e->operation : "-");
                    fb_draw_text_ellipsis(fb, 20, 110, buf_d, th->text_primary, 44, 28, 218);

                    snprintf(buf_d, sizeof(buf_d), "Raison: %s", e->reason);
                    fb_draw_text_ellipsis(fb, 20, 128, buf_d, RGB565(248, 113, 113), 44, 28, 218);

                    snprintf(buf_d, sizeof(buf_d), "Impact: %s", e->impact);
                    fb_draw_text_ellipsis(fb, 20, 146, buf_d, th->text_secondary, 44, 28, 218);

                    snprintf(buf_d, sizeof(buf_d), "Action: %s", e->recovery);
                    fb_draw_text_ellipsis(fb, 20, 164, buf_d, RGB565(74, 222, 128), 44, 28, 218);

                    fb_draw_text_clipped(fb, 84, 194, "EXE ou ESC pour fermer", th->text_secondary, 26, 28, 218);
                }

                /* Confirmation Modal (defaults safely to Annuler) */
                if (sui->diag_confirm_modal) {
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

                    fb_fill_rounded_rect_clipped(fb, 24, 52 + m_dy, 272, 132, 8, RGB565(248, 113, 113), 26, 218);
                    fb_fill_rounded_rect_clipped(fb, 25, 53 + m_dy, 270, 130, 7, th->bg_top_bar, 26, 218);
                    fb_draw_text(fb, 40, 64 + m_dy, "Effacer les anciens journaux ?", th->text_primary, 34);
                    fb_draw_text(fb, 40, 84 + m_dy, "Supprime uniquement les fichiers log.", th->text_secondary, 38);
                    fb_draw_text(fb, 40, 100 + m_dy, "Jeux, profils et reglages preserves.", RGB565(74, 222, 128), 38);

                    int btn_fp = state->anim.modal_btn_fp;
                    if ((sui->diag_confirm_yes && btn_fp == 0) || (!sui->diag_confirm_yes && btn_fp == 256)) {
                        btn_fp = sui->diag_confirm_yes ? 256 : 0;
                    }
                    unsigned a_yes = (unsigned)((btn_fp + 4) >> 3);
                    unsigned a_no = 32U - a_yes;
                    bool hov_no = (m_hov && sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 0);
                    bool hov_yes = (m_hov && sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 1);

                    fb_fill_rounded_rect_clipped(fb, 42, 152 + m_dy, 108, 24, 5,
                                                 nl_rgb565_lerp(th->card_bg, th->accent_primary, a_no), 26, 218);
                    if (hov_no) {
                        fb_draw_rounded_outline_clipped(fb, 42, 152 + m_dy, 108, 24, 5, th->accent_secondary, 26, 218);
                    }
                    fb_draw_text(fb, 74, 160 + m_dy, "Annuler",
                                 nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_no), 10);

                    fb_fill_rounded_rect_clipped(fb, 170, 152 + m_dy, 108, 24, 5,
                                                 nl_rgb565_lerp(th->card_bg, RGB565(220, 52, 52), a_yes), 26, 218);
                    if (hov_yes) {
                        fb_draw_rounded_outline_clipped(fb, 170, 152 + m_dy, 108, 24, 5, th->accent_secondary, 26, 218);
                    }
                    fb_draw_text(fb, 202, 160 + m_dy, "Effacer",
                                 nl_rgb565_lerp(th->text_secondary, RGB565(255, 255, 255), a_yes), 10);
                }
            } else {
                /* Tab 1: Boot Forensics (12 Stages) & CX II-T / CX System Info */
                fb_fill_rounded_rect_clipped(fb, 14, 48, 292, 44, 5, th->card_bg, 46, 94);
                char sys_l1[64], sys_l2[64], sys_l3[80];
                const char *nd_mode = state->sys.has_ndless
                    ? (state->sys.ndless_persistent ? "Persistant" : "Classique")
                    : "Inactif";
                snprintf(sys_l1, sizeof(sys_l1), "OS: %s | Ndless: %s",
                         state->sys.os_version[0] ? state->sys.os_version : "TI-OS", nd_mode);
                fb_draw_text_ellipsis(fb, 20, 53, sys_l1, th->text_primary, 45, 46, 94);

                if (state->sys.prior_incomplete_boot) {
                    snprintf(sys_l2, sizeof(sys_l2), "Crash precedent: etape %d (%s)",
                             (int)state->sys.last_crash_stage,
                             state->sys.last_crash_mode[0] ? state->sys.last_crash_mode : "?");
                    fb_draw_text_ellipsis(fb, 20, 65, sys_l2, RGB565(248, 113, 113), 45, 46, 94);
                } else {
                    snprintf(sys_l2, sizeof(sys_l2), "Dernier boot sain | Etape act.: %d/12",
                             (int)state->diag.current_stage);
                    fb_draw_text_ellipsis(fb, 20, 65, sys_l2, RGB565(74, 222, 128), 45, 46, 94);
                }

                if (state->perf.frame_count > 0U) {
                    uint32_t avg_dt = state->perf.total_dt_us / state->perf.frame_count;
                    if (avg_dt == 0U) avg_dt = 16667U;
                    uint32_t fps_x10 = (10000000U + avg_dt / 2U) / avg_dt;
                    uint32_t avg_ren = state->perf.total_render_us / state->perf.frame_count;
                    uint32_t avg_jit = state->perf.total_jitter_us / state->perf.frame_count;
                    snprintf(sys_l3, sizeof(sys_l3), "%lu.%luFPS %luus Jit:%luus C:%lu/%lu",
                             (unsigned long)(fps_x10 / 10U), (unsigned long)(fps_x10 % 10U),
                             (unsigned long)avg_ren, (unsigned long)avg_jit,
                             (unsigned long)state->perf.physical_clicks,
                             (unsigned long)state->perf.tap_clicks);
                } else {
                    snprintf(sys_l3, sizeof(sys_l3), "OOM recup: %lu | Rev r%lu | Cache: %s",
                             (unsigned long)state->sys.oom_recoveries,
                             (unsigned long)state->sys.ndless_rev,
                             state->sys.cache_loaded_at_boot ? "ON" : "OFF");
                }
                fb_draw_text_ellipsis(fb, 20, 77, sys_l3, th->accent_secondary, 45, 46, 94);

                /* 12-stage checklist (6 visible rows, scrollable 0..6) */
                for (int s = 1; s <= 12; s++) {
                    int idx = s - 1;
                    int ry = 96 + (idx - sui->diag_scroll) * 20;
                    if (ry + 18 <= 94 || ry >= 216) continue;

                    bool reached = state->diag.stage_reached[s];
                    bool is_crash_st = (state->sys.prior_incomplete_boot && (int)state->sys.last_crash_stage == s);
                    uint16_t st_col = is_crash_st ? RGB565(248, 113, 113)
                        : (reached ? RGB565(74, 222, 128) : th->text_secondary);
                    const char *st_tag = is_crash_st ? "!!" : (reached ? "OK" : "--");

                    fb_fill_rounded_rect_clipped(fb, 14, ry, 292, 18, 4, th->card_bg, 94, 216);
                    fb_fill_rounded_rect_clipped(fb, 18, ry + 2, 22, 14, 3, th->pill_bg, 94, 216);
                    fb_draw_text_clipped(fb, 23, ry + 5, st_tag, st_col, 2, 94, 216);
                    fb_draw_text_ellipsis(fb, 46, ry + 5, nl_diag_stage_label((nl_boot_stage_t)s),
                                          reached ? th->text_primary : th->text_secondary, 40, 94, 216);
                }
                draw_scrollbar_v_smooth(fb, 96, 118, sui->diag_scroll * 20, 20, 6, 12, th);
            }
            break;
        }

        case NL_UI_SETTINGS_HOTKEY: {
            static const char *const hints[3] = {"UP/DN Choix", "EXE/L/R Modif.", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "RACCOURCI GLOBAL TI-OS", NL_ICO_HOTKEY, hints, 3);

            int hk_fp = state->anim.switch_hotkey_fp;
            if ((state->settings.global_hotkey && hk_fp == 0) || (!state->settings.global_hotkey && hk_fp == 256)) {
                hk_fp = state->settings.global_hotkey ? 256 : 0;
            }
            unsigned sw_alpha = (unsigned)((hk_fp + 4) >> 3);

            /* Row 0 (y=32, h=44): Enable/Disable TI-OS Hotkey */
            bool sel0 = (sui->hotkey_cursor == 0);
            bool hov0 = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                         sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == 0);
            uint16_t b0 = sel0 ? th->accent_primary : (hov0 ? th->accent_secondary : th->border_subtle);
            uint16_t bg0 = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, sel0 ? 28U : 0U);
            fb_fill_rounded_rect_clipped(fb, 14, 32, 292, 44, 7, b0, 26, 220);
            fb_fill_rounded_rect_clipped(fb, 15, 33, 290, 42, 6, bg0, 26, 220);

            draw_settings_icon_badge(fb, 22, 43, 22, NL_ICO_HOTKEY, sel0 ? 32U : sw_alpha, false, th, 26, 220);
            fb_draw_text(fb, 50, 41, "Ouvrir depuis les menus TI-OS", th->text_primary, 31);
            const char *r0_sub = state->settings.global_hotkey
                ? (state->sys.hotkey_ram_active
                    ? "Actif en RAM + ndless/startup"
                    : "Active (hook RAM en attente)")
                : "Desactive (aucun lancement auto)";
            fb_draw_text(fb, 50, 55, r0_sub,
                         state->settings.global_hotkey
                             ? (state->sys.hotkey_ram_active ? RGB565(74, 222, 128) : th->accent_secondary)
                             : th->text_secondary, 34);
            draw_toggle_switch(fb, 262, 46, hk_fp, th);

            /* Row 1 (y=80, h=32): Hotkey Combo Selector */
            bool sel1 = (sui->hotkey_cursor == 1);
            bool hov1 = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                         sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == 1);
            uint16_t b1 = sel1 ? th->accent_primary : (hov1 ? th->accent_secondary : th->border_subtle);
            uint16_t bg1 = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, sel1 ? 28U : 0U);
            fb_fill_rounded_rect_clipped(fb, 14, 80, 292, 32, 6, b1, 26, 220);
            fb_fill_rounded_rect_clipped(fb, 15, 81, 290, 30, 5, bg1, 26, 220);

            fb_draw_text(fb, 22, 92, "Combinaison", th->text_primary, 14);
            char combo_pill[32];
            snprintf(combo_pill, sizeof(combo_pill), "< %s >",
                     nl_hk_combo_label((nl_hk_combo_t)state->settings.hotkey_combo));
            int pill_w = (int)strlen(combo_pill) * 6 + 12;
            if (pill_w < 110) pill_w = 110;
            if (pill_w > 170) pill_w = 170;
            int pill_x = 298 - pill_w;
            fb_fill_rounded_rect_clipped(fb, pill_x, 86, pill_w, 20, 4, th->pill_bg, 26, 220);
            fb_draw_text_clipped(fb, pill_x + 6, 92, combo_pill,
                                 sel1 ? th->accent_primary : th->accent_secondary, 26, 26, 220);

            /* RAM Hook, Ndless Mode & Safety Diagnostics Box (y=116, h=100) */
            fb_fill_rounded_rect_clipped(fb, 14, 116, 292, 100, 6, th->card_bg, 26, 220);

            fb_draw_text(fb, 22, 122, "Etat RAM :", th->text_secondary, 11);
            fb_draw_text(fb, 94, 122,
                         state->sys.hotkey_ram_active
                             ? "Hook get_event actif (0 SWI)"
                             : "Aucun hook actif en RAM",
                         state->sys.hotkey_ram_active ? RGB565(74, 222, 128) : th->text_secondary, 33);

            fb_draw_text(fb, 22, 135, "Ndless :", th->text_secondary, 9);
            fb_draw_text(fb, 94, 135,
                         state->sys.ndless_persistent
                             ? "Persistant (survit reboot)"
                             : "Classique (RAM session)",
                         th->accent_secondary, 33);

            fb_draw_text(fb, 22, 150, "Protections & compatibilite :", th->text_primary, 32);
            fb_draw_text(fb, 22, 163, "- Manuel uniquement (pas d'autostart)", th->text_secondary, 42);
            fb_draw_text(fb, 22, 176, "- Evite le menu contextuel TI-OS", th->text_secondary, 42);
            fb_draw_text(fb, 22, 189, "- Bloque en jeu + anti double-lancement", th->text_secondary, 42);
            fb_draw_text(fb, 22, 202,
                         state->sys.ndless_persistent
                             ? "- Maint. ESC ou ON au boot = desactive"
                             : "- Classique: relancer Ndless apres reboot",
                         state->sys.ndless_persistent ? RGB565(74, 222, 128) : RGB565(251, 191, 36), 42);
            break;
        }

        case NL_UI_SETTINGS_DISPLAY: {
            static const char *const hints[3] = {"UP/DN Option", "L/R/EXE Modif.", "ESC Retour"};
            draw_settings_chrome(state, fb, th, "AFFICHAGE", NL_ICO_SCREEN, hints, 3);

            static const nl_set_icon_id_t disp_icons[NL_DISPLAY_OPT_COUNT] = {
                NL_ICO_GRID,
                NL_ICO_SORT,
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
                bool hovered = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == r);
                uint16_t r_bg = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha);
                fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6, r_bg, clip_top, clip_bot);
                if (hovered && !sel) {
                    fb_draw_rounded_outline_clipped(fb, 12, ry, 292, row_h, 6, th->accent_secondary, clip_top, clip_bot);
                }

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
                    /* Row 1: Scroll Direction (Vertical vs Horizontal + Peek) */
                    fb_draw_text_clipped(fb, 41, ry + 11, "Sens de defilement", th->text_primary, 22, clip_top, clip_bot);
                    char dir_buf[20];
                    snprintf(dir_buf, sizeof(dir_buf), "< %s >", nl_scroll_dir_label(state->settings.scroll_dir));
                    int dw = (int)strlen(dir_buf) * 6;
                    int pulse_dx = (sui->display_cursor == 1) ? ((state->anim.val_pulse_dir * state->anim.val_pulse_fp * 2) >> 8) : 0;
                    unsigned pulse_a = (sui->display_cursor == 1) ? (unsigned)((state->anim.val_pulse_fp * 10) >> 8) : 0U;
                    uint16_t pill_col = nl_rgb565_lerp(th->pill_bg, th->accent_primary, pulse_a);
                    fb_fill_rounded_rect_clipped(fb, 284 - dw - 10 + pulse_dx, ry + 6, dw + 12, 16, 4, pill_col, clip_top, clip_bot);
                    fb_draw_text_clipped(fb, 284 - dw - 4 + pulse_dx, ry + 11, dir_buf, th->accent_secondary, 16, clip_top, clip_bot);
                } else if (r == 2) {
                    /* Row 2: Show Game Names */
                    fb_draw_text_clipped(fb, 41, ry + 11, "Titres sous les jaquettes", th->text_primary, 28, clip_top, clip_bot);
                    if (ry >= clip_top - 2 && ry + row_h <= clip_bot + 2) {
                        draw_toggle_switch(fb, 260, ry + 7, state->anim.switch_shownames_fp, th);
                    }
                } else if (r == 3) {
                    /* Row 3: Show Bottom Shortcuts Bar */
                    fb_draw_text_clipped(fb, 41, ry + 11, "Barre d'aide en bas", th->text_primary, 28, clip_top, clip_bot);
                    if (ry >= clip_top - 2 && ry + row_h <= clip_bot + 2) {
                        draw_toggle_switch(fb, 260, ry + 7, state->anim.switch_shortcuts_fp, th);
                    }
                } else if (r == 4) {
                    /* Row 4: Power Off Calculator */
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

        case NL_UI_SETTINGS_ANIMATIONS: {
            static const char *const hints[3] = {"UP/DN Categorie", "L/R+EXE Choix", "ESC Retour"};
            static const char *const cat_titles[NL_ANIM_CAT_COUNT] = {
                "Ouverture de l'app",
                "Fermeture de l'app",
                "Lancement des jeux",
                "Retour des jeux"
            };
            draw_settings_chrome(state, fb, th, "ANIMATIONS", NL_ICO_ANIM, hints, 3);

            fb_draw_text_clipped(fb, 16, 31, "4 categories independantes", th->text_secondary, 32, 26, 44);

            const int row_y0 = 44;
            const int row_h = 29;
            const int stride = 32;
            const int clip_top = 42;
            const int clip_bot = 176;

            int scroll_px, sel_y, prev_cur;
            unsigned glow_alpha;
            resolve_list_anim_metrics(state, is_active_scr, row_y0, stride,
                                      sui->anim_cursor, 0,
                                      &scroll_px, &sel_y, &glow_alpha, &prev_cur);

            uint16_t border_col = nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha);
            fb_fill_rounded_rect_clipped(fb, 13, sel_y - 1, 294, row_h + 2, 7, border_col, clip_top, clip_bot);

            for (int r = 0; r < NL_ANIM_CAT_COUNT; r++) {
                int ry = row_y0 + r * stride;
                bool sel = (sui->anim_cursor == r);
                bool is_prev = (!sel && r == prev_cur && glow_alpha < 32U);
                unsigned r_alpha = sel ? glow_alpha : (is_prev ? (32U - glow_alpha) : 0U);
                bool hovered = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == r);

                uint16_t r_bg = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha);
                fb_fill_rounded_rect_clipped(fb, 14, ry, 292, row_h, 6, r_bg, clip_top, clip_bot);
                if (hovered && !sel) {
                    fb_draw_rounded_outline_clipped(fb, 14, ry, 292, row_h, 6, th->accent_secondary, clip_top, clip_bot);
                }

                const char *val_short = "";
                const char *val_full  = "";
                if (r == 0) {
                    val_short = nl_app_open_anim_short_label(state->settings.app_open_anim);
                    val_full  = nl_app_open_anim_label(state->settings.app_open_anim);
                } else if (r == 1) {
                    val_short = nl_app_close_anim_short_label(state->settings.app_close_anim);
                    val_full  = nl_app_close_anim_label(state->settings.app_close_anim);
                } else if (r == 2) {
                    val_short = nl_launch_anim_short_label(state->settings.launch_anim_mode);
                    val_full  = nl_launch_anim_label(state->settings.launch_anim_mode);
                } else {
                    val_short = nl_return_anim_short_label(state->settings.return_anim_mode);
                    val_full  = nl_return_anim_label(state->settings.return_anim_mode);
                }

                /* Left category title + active mode subtitle */
                fb_draw_text_clipped(fb, 22, ry + 4, cat_titles[r],
                                     th->text_primary, 24, clip_top, clip_bot);
                uint16_t sub_col = nl_rgb565_lerp(th->text_secondary, th->accent_secondary, r_alpha >> 1);
                fb_draw_text_clipped(fb, 22, ry + 16, val_full,
                                     sub_col, 24, clip_top, clip_bot);

                /* Right independent value selector badge (< Val >) */
                int bx = 212;
                int bw = 88;
                int nudge = (sel && state->anim.val_pulse_fp > 0)
                    ? ((state->anim.val_pulse_dir * state->anim.val_pulse_fp) >> 7) : 0;
                uint16_t badge_bg = sel ? th->accent_primary : th->pill_bg;
                uint16_t badge_fg = sel ? th->text_on_accent : th->text_primary;
                fb_fill_rounded_rect_clipped(fb, bx + nudge, ry + 6, bw, 17, 5, badge_bg, clip_top, clip_bot);
                char badge_txt[20];
                snprintf(badge_txt, sizeof(badge_txt), "< %s >", val_short);
                int blen = (int)strlen(badge_txt);
                int btx = bx + nudge + (bw - blen * 6) / 2;
                if (btx < bx + nudge + 3) btx = bx + nudge + 3;
                fb_draw_text_clipped(fb, btx, ry + 11, badge_txt, badge_fg, 14, clip_top, clip_bot);
            }

            /* Compact theme-adaptive visual cue box at bottom (y=176..212) for highlighted category */
            fb_fill_rounded_rect_clipped(fb, 14, 176, 292, 36, 6, th->card_bg, 174, 216);
            int cur_cat = sui->anim_cursor;
            if (cur_cat < 0 || cur_cat >= NL_ANIM_CAT_COUNT) cur_cat = 0;
            int pv_cx = 44, pv_cy = 194;
            bool is_liq = false, is_zoom = false, is_inst = false, is_spin = false, is_bar = false, is_fade = false;
            const char *pv_title = "";
            const char *pv_desc  = "";

            if (cur_cat == 0) {
                nl_app_open_anim_t m = state->settings.app_open_anim;
                pv_title = nl_app_open_anim_label(m);
                pv_desc  = nl_app_open_anim_desc(m);
                is_liq   = (m == NL_OPEN_ANIM_LIQUID);
                is_zoom  = (m == NL_OPEN_ANIM_ZOOM);
                is_inst  = (m == NL_OPEN_ANIM_INSTANT);
            } else if (cur_cat == 1) {
                nl_app_close_anim_t m = state->settings.app_close_anim;
                pv_title = nl_app_close_anim_label(m);
                pv_desc  = nl_app_close_anim_desc(m);
                is_liq   = (m == NL_CLOSE_ANIM_LIQUID);
                is_fade  = (m == NL_CLOSE_ANIM_FADE);
                is_zoom  = (m == NL_CLOSE_ANIM_ZOOM);
                is_inst  = (m == NL_CLOSE_ANIM_INSTANT);
            } else if (cur_cat == 2) {
                nl_launch_anim_t m = state->settings.launch_anim_mode;
                pv_title = nl_launch_anim_label(m);
                pv_desc  = nl_launch_anim_desc(m);
                is_liq   = (m == NL_LAUNCH_ANIM_THEME_DROP);
                is_spin  = (m == NL_LAUNCH_ANIM_SPINNER);
                is_bar   = (m == NL_LAUNCH_ANIM_LOADING_BAR);
            } else {
                nl_return_anim_t m = state->settings.return_anim_mode;
                pv_title = nl_return_anim_label(m);
                pv_desc  = nl_return_anim_desc(m);
                is_liq   = (m == NL_RETURN_ANIM_LIQUID);
                is_zoom  = (m == NL_RETURN_ANIM_ZOOM);
                is_fade  = (m == NL_RETURN_ANIM_FADE);
                is_inst  = (m == NL_RETURN_ANIM_INSTANT);
            }

            if (is_liq) {
                for (int d = 0; d < 4; d++) {
                    uint16_t dc = nl_liquid_theme_color_rgb565(state->display_mode, state->accent_color, d);
                    int r_pv = 12 - d * 2;
                    int px_pv = 38 + (d - 1) * 4;
                    int py_pv = 194 + ((d & 1) ? 2 : -1);
                    fb_fill_aa_circle_clipped(fb, px_pv - 1, py_pv, r_pv, dc, 176, 212);
                    fb_fill_aa_circle_clipped(fb, px_pv + 2, py_pv - 1, (r_pv > 3) ? (r_pv - 2) : 2, dc, 176, 212);
                }
            } else if (is_spin) {
                fb_fill_rounded_rect_clipped(fb, pv_cx - 10, pv_cy - 10, 20, 20, 5, th->pill_bg, 176, 212);
                static const int8_t sp_o[8][2] = { {0,-6},{4,-4},{6,0},{4,4},{0,6},{-4,4},{-6,0},{-4,-4} };
                for (int d = 0; d < 8; d++) {
                    uint16_t dc = (d < 3) ? th->accent_primary : th->dot_inactive;
                    fb_fill_rect_clipped(fb, pv_cx + sp_o[d][0] - 1, pv_cy + sp_o[d][1] - 1, 2, 2, dc, 176, 212);
                }
            } else if (is_bar) {
                fb_fill_rounded_rect_clipped(fb, pv_cx - 8, pv_cy - 11, 16, 16, 4, th->accent_primary, 176, 212);
                fb_fill_rounded_rect_clipped(fb, pv_cx - 16, pv_cy + 8, 32, 4, 2, th->dot_inactive, 176, 212);
                fb_fill_rounded_rect_clipped(fb, pv_cx - 16, pv_cy + 8, 22, 4, 2, th->accent_secondary, 176, 212);
            } else if (is_fade) {
                fb_fill_rounded_rect_clipped(fb, pv_cx - 13, pv_cy - 10, 26, 20, 5,
                                             nl_rgb565_lerp(th->bg_main, th->accent_secondary, 10U), 176, 212);
                fb_fill_rounded_rect_clipped(fb, pv_cx - 8, pv_cy - 6, 16, 12, 4,
                                             nl_rgb565_lerp(th->bg_main, th->accent_primary, 22U), 176, 212);
            } else if (is_inst) {
                fb_fill_rounded_rect_clipped(fb, pv_cx - 11, pv_cy - 10, 22, 20, 5, th->pill_bg, 176, 212);
                fb_draw_text_clipped(fb, pv_cx - 5, pv_cy - 3, ">>", th->accent_primary, 2, 176, 212);
            } else if (is_zoom) {
                fb_draw_rounded_outline_clipped(fb, pv_cx - 13, pv_cy - 11, 26, 22, 6, th->accent_secondary, 176, 212);
                fb_fill_rounded_rect_clipped(fb, pv_cx - 7, pv_cy - 6, 14, 12, 4, th->accent_primary, 176, 212);
            } else {
                fb_draw_rounded_outline_clipped(fb, pv_cx - 12, pv_cy - 12, 24, 24, 10, th->accent_secondary, 176, 212);
                fb_fill_rounded_rect_clipped(fb, pv_cx - 8, pv_cy - 8, 16, 16, 4, th->accent_primary, 176, 212);
            }

            fb_draw_text_clipped(fb, 74, 183, pv_title, th->text_primary, 36, 176, 212);
            fb_draw_text_clipped(fb, 74, 196, pv_desc, th->text_secondary, 38, 176, 212);
            break;
        }

        case NL_UI_SETTINGS_TOUCHPAD: {
            static const char *const hints_tab0[3] = {"UP/DN Option", "TAB Curseur", "ESC Retour"};
            static const char *const hints_tab1[3] = {"U/D Ligne", "L/R+EXE Choix", "ESC/TAB Retour"};
            static const char *const sens_labels[NL_TP_SENS_COUNT] = {
                "1/5 Lente",
                "2/5 Douce",
                "3/5 Normale",
                "4/5 Sensible",
                "5/5 Rapide"
            };
            static const int k_row_h[7] = { 26, 25, 37, 37, 26, 26, 28 };
            bool is_tab1 = (sui->tp_tab == 1);
            bool mouse_on = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid);
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
            if (mouse_on && sui->hover_kind == NL_HOVER_SET_TAB && sui->hover_index == 0) {
                fb_draw_rounded_outline_clipped(fb, 15, 29, 138, 14, 4, th->accent_secondary, 26, 220);
            }
            fb_fill_rounded_rect_clipped(fb, 155, 29, 150, 14, 4, t1_bg, 26, 220);
            fb_draw_text(fb, 163, 33, "2. Curseur & Taille", t1_fg, 22);
            if (mouse_on && sui->hover_kind == NL_HOVER_SET_TAB && sui->hover_index == 1) {
                fb_draw_rounded_outline_clipped(fb, 155, 29, 150, 14, 4, th->accent_secondary, 26, 220);
            }

            unsigned glow_alpha = 32U;
            if (is_active_scr && state->anim.set_sel_glow_fp < 256) {
                glow_alpha = (unsigned)((state->anim.set_sel_glow_fp + 4) >> 3);
                if (glow_alpha > 32U) glow_alpha = 32U;
            }

            if (!is_tab1) {
                int cur_row = sui->tp_cursor;
                if (cur_row < 0) cur_row = 0;
                if (cur_row > 6) cur_row = 6;

                int scroll = sui->tp_scroll;
                if (scroll < 0) scroll = 0;
                if (scroll > 2) scroll = 2;

                bool is_mouse_mode = (state->settings.touchpad_mode == NL_TP_MODE_MOUSE);
                uint16_t rail_col = nl_rgb565_lerp(th->dot_inactive, th->border_subtle, 10U);
                const int track_x = 34;
                const int track_w = 252;

                int ry = 47;
                for (int r = scroll; r < scroll + 5 && r < 7; r++) {
                    int rh = k_row_h[r];
                    bool is_cur_row = (cur_row == r);
                    bool hov_r = (mouse_on &&
                        (sui->hover_kind == NL_HOVER_SET_ROW ||
                         ((r == 2 || r == 3) && sui->hover_kind == NL_HOVER_SET_SLIDER_NOTCH)) &&
                        sui->hover_index == r);

                    uint16_t r_border = is_cur_row
                        ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, glow_alpha)
                        : (hov_r ? th->accent_secondary : th->border_subtle);
                    uint16_t r_bg = is_cur_row
                        ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, glow_alpha >> 1)
                        : th->card_bg;
                    fb_fill_rounded_rect_clipped(fb, 14, ry, 290, rh, 6, r_border, 45, 218);
                    fb_fill_rounded_rect_clipped(fb, 15, ry + 1, 288, rh - 2, 5, r_bg, 45, 218);

                    if (r == 0) {
                        /* Row 0: Navigation Mode Selector */
                        fb_draw_text_clipped(fb, 22, ry + 9, "Mode de navigation", th->text_primary, 20, 45, 218);
                        const char *mode_txt = is_mouse_mode ? "< Souris >" : "< Pave tactile >";
                        int mw = (int)strlen(mode_txt) * 6 + 12;
                        int mx_pill = 294 - mw;
                        uint16_t m_bg = is_mouse_mode ? th->accent_primary : th->pill_bg;
                        uint16_t m_fg = is_mouse_mode ? th->text_on_accent : th->accent_secondary;
                        fb_fill_rounded_rect_clipped(fb, mx_pill, ry + 5, mw, 16, 4, m_bg, 45, 218);
                        fb_draw_text_clipped(fb, mx_pill + 6, ry + 10, mode_txt, m_fg, 18, 45, 218);
                    } else if (r == 1) {
                        /* Row 1: Touchpad Slide Nav ON/OFF (Mode 1) */
                        int sw_fp = state->anim.switch_touchpad_fp;
                        if ((state->settings.touchpad_nav && sw_fp == 0) || (!state->settings.touchpad_nav && sw_fp == 256)) {
                            sw_fp = state->settings.touchpad_nav ? 256 : 0;
                        }
                        fb_draw_text_clipped(fb, 22, ry + 9, "Glissement au pave (Mode 1)",
                                             is_mouse_mode ? th->text_secondary : th->text_primary, 28, 45, 218);
                        draw_toggle_switch(fb, 260, ry + 5, sw_fp, th);
                    } else if (r == 2) {
                        /* Row 2: 5-Detent Slide Sensitivity (Mode 1) */
                        int sens = state->settings.touchpad_sensitivity;
                        if (sens < 0) sens = 0;
                        if (sens >= NL_TP_SENS_COUNT) sens = NL_TP_SENS_COUNT - 1;

                        fb_draw_text_clipped(fb, 22, ry + 4, "Sensibilite pave (5 crans)", th->text_primary, 28, 45, 218);

                        unsigned pulse_alpha = (unsigned)((state->anim.tp_detent_pulse_fp + 4) >> 3);
                        if (pulse_alpha > 32U) pulse_alpha = 32U;
                        const char *s_lbl = sens_labels[sens];
                        int lbl_w = (int)strlen(s_lbl) * 6 + 10;
                        int lbl_x = 294 - lbl_w;
                        uint16_t badge_bg = is_cur_row
                            ? nl_rgb565_lerp(th->pill_bg, th->accent_primary, 12U + (pulse_alpha >> 1))
                            : th->pill_bg;
                        uint16_t badge_fg = is_cur_row
                            ? nl_rgb565_lerp(th->accent_secondary, th->text_on_accent, 14U)
                            : th->accent_secondary;
                        fb_fill_rounded_rect_clipped(fb, lbl_x, ry + 3, lbl_w, 12, 4, badge_bg, 45, 218);
                        fb_draw_text_clipped(fb, lbl_x + 5, ry + 6, s_lbl, badge_fg, 16, 45, 218);

                        int track_y = ry + 22;
                        int pos_fp = state->anim.tp_slider_pos_fp;
                        if (pos_fp < 0 && !state->anim.initialized) pos_fp = sens * 256;
                        int handle_x = track_x + ((pos_fp * track_w + 512) >> 10);
                        if (handle_x < track_x - 3) handle_x = track_x - 3;
                        if (handle_x > track_x + track_w + 3) handle_x = track_x + track_w + 3;
                        int fill_w = handle_x - track_x;
                        if (fill_w < 0) fill_w = 0;
                        if (fill_w > track_w) fill_w = track_w;

                        uint16_t active_rail_col = (!is_mouse_mode && state->settings.touchpad_nav)
                            ? th->accent_primary
                            : nl_rgb565_lerp(th->dot_inactive, th->accent_primary, 20U);
                        fb_fill_aa_capsule_clipped(fb, track_x, track_y - 2, track_w, 4, rail_col, 45, 218);
                        if (fill_w >= 4) {
                            fb_fill_aa_capsule_clipped(fb, track_x, track_y - 2, fill_w, 4, active_rail_col, 45, 218);
                        }

                        for (int n = 0; n < NL_TP_SENS_COUNT; n++) {
                            int nx = track_x + n * 63;
                            bool passed = (n <= sens);
                            bool is_cur = (n == sens);
                            bool hov_n = (mouse_on && sui->hover_kind == NL_HOVER_SET_SLIDER_NOTCH &&
                                          sui->hover_index == 2 && sui->hover_sub == n);
                            if ((is_cur && pulse_alpha > 1U) || hov_n) {
                                uint16_t halo_c = hov_n
                                    ? th->accent_secondary
                                    : nl_rgb565_lerp(r_bg, th->accent_secondary, pulse_alpha);
                                fb_fill_aa_capsule_clipped(fb, nx - 5, track_y - 5, 11, 11, halo_c, 45, 218);
                            }
                            uint16_t tick_col = is_cur
                                ? th->accent_secondary
                                : (passed ? active_rail_col : nl_rgb565_lerp(th->text_secondary, th->dot_inactive, 16U));
                            fb_fill_aa_capsule_clipped(fb, nx - 3, track_y - 3, 7, 7, tick_col, 45, 218);
                            fb_fill_aa_capsule_clipped(fb, nx - 1, track_y - 1, 3, 3, r_bg, 45, 218);
                            char num_buf[2] = { (char)('1' + n), '\0' };
                            uint16_t num_col = (is_cur || hov_n) ? th->accent_secondary : th->text_secondary;
                            fb_draw_text_clipped(fb, nx - 2, ry + 28, num_buf, num_col, 1, 45, 218);
                        }

                        int knob_expand = (pulse_alpha > 14U) ? 1 : 0;
                        int kw = 10 + knob_expand * 2;
                        int kh = 11 + knob_expand * 2;
                        int kx = handle_x - (kw / 2);
                        int ky = track_y - (kh / 2);
                        uint16_t knob_rim = (is_cur_row || pulse_alpha > 0U)
                            ? nl_rgb565_lerp(th->accent_primary, th->accent_secondary, 16U + (pulse_alpha >> 1))
                            : th->border_subtle;
                        fb_fill_rounded_rect_clipped(fb, kx - 1, ky - 1, kw + 2, kh + 2, 3, knob_rim, 45, 218);
                        fb_fill_rounded_rect_clipped(fb, kx, ky, kw, kh, 3, RGB565(248, 250, 252), 45, 218);
                        fb_fill_rect_clipped(fb, handle_x - 1, ky + 2, 2, kh - 4, active_rail_col, 45, 218);
                    } else if (r == 3) {
                        /* Row 3: 5-Detent Mouse Pointer Speed Slider */
                        int cspd = state->settings.cursor_speed;
                        if (cspd < 0) cspd = 0;
                        if (cspd >= NL_CURSOR_SPEED_COUNT) cspd = NL_CURSOR_SPEED_COUNT - 1;

                        fb_draw_text_clipped(fb, 22, ry + 4, "Vitesse du curseur", th->text_primary, 24, 45, 218);

                        unsigned spd_pulse = (unsigned)((state->anim.tp_speed_detent_pulse_fp + 4) >> 3);
                        if (spd_pulse > 32U) spd_pulse = 32U;
                        const char *spd_lbl = nl_cursor_speed_label(cspd);
                        int slbl_w = (int)strlen(spd_lbl) * 6 + 10;
                        int slbl_x = 294 - slbl_w;
                        uint16_t sbadge_bg = is_cur_row
                            ? nl_rgb565_lerp(th->pill_bg, th->accent_primary, 12U + (spd_pulse >> 1))
                            : th->pill_bg;
                        uint16_t sbadge_fg = is_cur_row
                            ? nl_rgb565_lerp(th->accent_secondary, th->text_on_accent, 14U)
                            : th->accent_secondary;
                        fb_fill_rounded_rect_clipped(fb, slbl_x, ry + 3, slbl_w, 12, 4, sbadge_bg, 45, 218);
                        fb_draw_text_clipped(fb, slbl_x + 5, ry + 6, spd_lbl, sbadge_fg, 16, 45, 218);

                        int strack_y = ry + 22;
                        int spos_fp = state->anim.tp_speed_slider_pos_fp;
                        int shandle_x = track_x + ((spos_fp * track_w + 512) >> 10);
                        if (shandle_x < track_x - 3) shandle_x = track_x - 3;
                        if (shandle_x > track_x + track_w + 3) shandle_x = track_x + track_w + 3;
                        int sfill_w = shandle_x - track_x;
                        if (sfill_w < 0) sfill_w = 0;
                        if (sfill_w > track_w) sfill_w = track_w;

                        uint16_t sactive_rail = is_mouse_mode
                            ? th->accent_primary
                            : nl_rgb565_lerp(th->dot_inactive, th->accent_primary, 20U);
                        fb_fill_aa_capsule_clipped(fb, track_x, strack_y - 2, track_w, 4, rail_col, 45, 218);
                        if (sfill_w >= 4) {
                            fb_fill_aa_capsule_clipped(fb, track_x, strack_y - 2, sfill_w, 4, sactive_rail, 45, 218);
                        }

                        for (int n = 0; n < NL_CURSOR_SPEED_COUNT; n++) {
                            int nx = track_x + n * 63;
                            bool passed = (n <= cspd);
                            bool is_cur = (n == cspd);
                            bool hov_n = (mouse_on && sui->hover_kind == NL_HOVER_SET_SLIDER_NOTCH &&
                                          sui->hover_index == 3 && sui->hover_sub == n);
                            if ((is_cur && spd_pulse > 1U) || hov_n) {
                                uint16_t halo_c = hov_n
                                    ? th->accent_secondary
                                    : nl_rgb565_lerp(r_bg, th->accent_secondary, spd_pulse);
                                fb_fill_aa_capsule_clipped(fb, nx - 5, strack_y - 5, 11, 11, halo_c, 45, 218);
                            }
                            uint16_t tick_col = is_cur
                                ? th->accent_secondary
                                : (passed ? sactive_rail : nl_rgb565_lerp(th->text_secondary, th->dot_inactive, 16U));
                            fb_fill_aa_capsule_clipped(fb, nx - 3, strack_y - 3, 7, 7, tick_col, 45, 218);
                            fb_fill_aa_capsule_clipped(fb, nx - 1, strack_y - 1, 3, 3, r_bg, 45, 218);
                            char num_buf[2] = { (char)('1' + n), '\0' };
                            uint16_t num_col = (is_cur || hov_n) ? th->accent_secondary : th->text_secondary;
                            fb_draw_text_clipped(fb, nx - 2, ry + 28, num_buf, num_col, 1, 45, 218);
                        }

                        int sknob_exp = (spd_pulse > 14U) ? 1 : 0;
                        int skw = 10 + sknob_exp * 2;
                        int skh = 11 + sknob_exp * 2;
                        int skx = shandle_x - (skw / 2);
                        int sky = strack_y - (skh / 2);
                        uint16_t sknob_rim = (is_cur_row || spd_pulse > 0U)
                            ? nl_rgb565_lerp(th->accent_primary, th->accent_secondary, 16U + (spd_pulse >> 1))
                            : th->border_subtle;
                        fb_fill_rounded_rect_clipped(fb, skx - 1, sky - 1, skw + 2, skh + 2, 3, sknob_rim, 45, 218);
                        fb_fill_rounded_rect_clipped(fb, skx, sky, skw, skh, 3, RGB565(248, 250, 252), 45, 218);
                        fb_fill_rect_clipped(fb, shandle_x - 1, sky + 2, 2, skh - 4, sactive_rail, 45, 218);
                    } else if (r == 4) {
                        /* Row 4: Independent Tap-to-Click Toggle (OUI / NON) */
                        int tap_fp = state->anim.switch_tap_click_fp;
                        if ((state->settings.tap_to_click && tap_fp == 0) || (!state->settings.tap_to_click && tap_fp == 256)) {
                            tap_fp = state->settings.tap_to_click ? 256 : 0;
                        }
                        fb_draw_text_clipped(fb, 22, ry + 9, "Tapotement pour cliquer", th->text_primary, 24, 45, 218);
                        const char *tap_lbl = state->settings.tap_to_click ? "OUI" : "NON";
                        fb_fill_rounded_rect_clipped(fb, 224, ry + 6, 30, 14, 4, th->pill_bg, 45, 218);
                        fb_draw_text_clipped(fb, 230, ry + 10, tap_lbl,
                                             state->settings.tap_to_click ? RGB565(74, 222, 128) : th->text_secondary,
                                             3, 45, 218);
                        draw_toggle_switch(fb, 260, ry + 5, tap_fp, th);
                    } else if (r == 5) {
                        /* Row 5: Independent Cursor Size Selector (Petite / Moyenne / Grande) */
                        fb_draw_text_clipped(fb, 22, ry + 9, "Taille du curseur", th->text_primary, 20, 45, 218);
                        char sz_txt[20];
                        snprintf(sz_txt, sizeof(sz_txt), "< %s >", nl_cursor_size_label(state->settings.cursor_size));
                        int sw = (int)strlen(sz_txt) * 6 + 12;
                        int sx_pill = 294 - sw;
                        uint16_t sz_bg = is_cur_row ? th->accent_primary : th->pill_bg;
                        uint16_t sz_fg = is_cur_row ? th->text_on_accent : th->accent_secondary;
                        fb_fill_rounded_rect_clipped(fb, sx_pill, ry + 5, sw, 16, 4, sz_bg, 45, 218);
                        fb_draw_text_clipped(fb, sx_pill + 6, ry + 10, sz_txt, sz_fg, 16, 45, 218);
                    } else {
                        /* Row 6: Open "Personnaliser le curseur" Tab 1 */
                        fb_draw_text_clipped(fb, 22, ry + 4, "Personnaliser le curseur", th->text_primary, 26, 45, 218);
                        char style_sub[44];
                        snprintf(style_sub, sizeof(style_sub), "%s | %s",
                                 nl_cursor_style_label(state->settings.touchpad_cursor_style),
                                 nl_cursor_size_label(state->settings.cursor_size));
                        fb_draw_text_clipped(fb, 22, ry + 16, style_sub, th->accent_secondary, 30, 45, 218);

                        uint16_t btn_bg = (is_cur_row || hov_r) ? th->accent_primary : th->pill_bg;
                        uint16_t btn_fg = (is_cur_row || hov_r) ? th->text_on_accent : th->accent_secondary;
                        fb_fill_rounded_rect_clipped(fb, 230, ry + 6, 64, 15, 4, btn_bg, 45, 218);
                        fb_draw_text_clipped(fb, 236, ry + 10, "TAB / EXE", btn_fg, 9, 45, 218);
                    }

                    ry += rh + 2;
                }

                /* Scrollbar indicator for 7 rows (5 visible) */
                draw_scrollbar_v_smooth(fb, 47, 162, scroll * 32, 32, 5, 7, th);
            } else {
                /* TAB 1: "Personnaliser le curseur"
                 * Row 0 (y=50..134, h=84): 3 Cursor Model Cards (Fleche / Cercle / Pointeur) at active size
                 * Row 1 (y=142..214, h=72): 3 Independent Size Cards (Petite / Moyenne / Grande) for active model */
                static const nl_cursor_style_t card_styles[3] = {
                    NL_CURSOR_STYLE_ARROW,
                    NL_CURSOR_STYLE_CIRCLE,
                    NL_CURSOR_STYLE_POINTER
                };
                static const char *const card_titles[3] = { "Fleche", "Cercle", "Pointeur" };
                static const nl_cursor_size_t card_sizes[3] = {
                    NL_CURSOR_SIZE_SMALL,
                    NL_CURSOR_SIZE_MEDIUM,
                    NL_CURSOR_SIZE_LARGE
                };
                static const char *const size_titles[3] = { "Petite", "Moyenne", "Grande" };

                int sel_card = sui->tp_style_cursor;
                if (sel_card < 0) sel_card = 0;
                if (sel_card > 2) sel_card = 2;
                int sel_size = sui->tp_size_cursor;
                if (sel_size < 0) sel_size = 0;
                if (sel_size > 2) sel_size = 2;
                int active_row = (sui->tp_custom_row == 1) ? 1 : 0;

                unsigned style_glow = (unsigned)((state->anim.tp_style_glow_fp + 4) >> 3);
                if (style_glow > 32U) style_glow = 32U;

                nl_cursor_size_t active_sz = state->settings.cursor_size;
                if (active_sz >= NL_CURSOR_SIZE_COUNT) active_sz = NL_CURSOR_SIZE_MEDIUM;
                nl_cursor_style_t active_st = state->settings.touchpad_cursor_style;
                if (active_st != NL_CURSOR_STYLE_ARROW &&
                    active_st != NL_CURSOR_STYLE_CIRCLE &&
                    active_st != NL_CURSOR_STYLE_POINTER) {
                    active_st = NL_CURSOR_STYLE_ARROW;
                }

                const int card_w = 90;
                const int gap_x  = 11;
                const int start_x = 14;
                uint16_t stage_bg = th->is_light ? RGB565(226, 232, 240) : RGB565(12, 17, 28);

                /* --- ROW 0: 3 MODEL CARDS (y = 50..134, h = 84) --- */
                const int mod_y = 50;
                const int mod_h = 84;
                for (int c = 0; c < 3; c++) {
                    nl_cursor_style_t cstyle = card_styles[c];
                    int cx = start_x + c * (card_w + gap_x);
                    bool is_focused = (active_row == 0 && c == sel_card);
                    bool is_saved   = (cstyle == active_st);
                    bool is_hovered = (mouse_on && sui->hover_kind == NL_HOVER_SET_STYLE_CARD &&
                                       sui->hover_index == c);

                    if (is_focused || is_hovered) {
                        int pulse_phase = (int)(state->anim.tp_style_halo_phase & 63U);
                        int tri = (pulse_phase < 32) ? pulse_phase : (64 - pulse_phase);
                        unsigned halo_a = (unsigned)(12 + ((tri * 18) >> 5));
                        uint16_t outer_halo = nl_rgb565_lerp(th->bg_main, th->accent_secondary, halo_a);
                        fb_fill_rounded_rect_clipped(fb, cx - 2, mod_y - 2, card_w + 4, mod_h + 4, 8, outer_halo, 46, 218);
                    }

                    uint16_t c_border = is_focused
                        ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, style_glow)
                        : (is_hovered ? th->accent_secondary : (is_saved ? th->accent_primary : th->border_subtle));
                    uint16_t c_bg = (is_focused || is_hovered)
                        ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, style_glow >> 1)
                        : th->card_bg;
                    fb_fill_rounded_rect_clipped(fb, cx, mod_y, card_w, mod_h, 6, c_border, 46, 218);
                    fb_fill_rounded_rect_clipped(fb, cx + 1, mod_y + 1, card_w - 2, mod_h - 2, 5, c_bg, 46, 218);

                    if (is_saved) {
                        fb_fill_rounded_rect_clipped(fb, cx + card_w - 36, mod_y + 4, 32, 10, 3, th->accent_primary, 46, 218);
                        fb_draw_text_clipped(fb, cx + card_w - 33, mod_y + 6, "ACTIF", th->text_on_accent, 5, 46, 218);
                    }

                    int stage_x = cx + 13;
                    int stage_y = mod_y + 17;
                    fb_fill_rounded_rect_clipped(fb, stage_x, stage_y, 64, 44, 5, th->border_subtle, 46, 218);
                    fb_fill_rounded_rect_clipped(fb, stage_x + 1, stage_y + 1, 62, 42, 4, stage_bg, 46, 218);

                    int cw = 21, ch = 21, chx = 0, chy = 0;
                    nl_cursor_get_size_metrics(cstyle, active_sz, &cw, &ch, &chx, &chy);
                    int draw_hx = stage_x + 32 - (cw / 2) + chx;
                    int draw_hy = stage_y + 22 - (ch / 2) + chy;
                    draw_cursor_alpha_sprite_sized(fb, cstyle, active_sz, draw_hx, draw_hy,
                                                   256U, true, stage_y + 1, stage_y + 43,
                                                   state->anim.cur_style_phase, th);

                    int tw = (int)strlen(card_titles[c]) * 6;
                    fb_draw_text_clipped(fb, cx + (card_w - tw) / 2, mod_y + 67, card_titles[c],
                                         (is_focused || is_saved) ? th->text_primary : th->text_secondary, 12, 46, 218);
                }

                /* --- ROW 1: 3 INDEPENDENT SIZE CARDS (y = 142..214, h = 72) --- */
                const int sz_y = 142;
                const int sz_h = 72;
                for (int s = 0; s < 3; s++) {
                    nl_cursor_size_t csz = card_sizes[s];
                    int cx = start_x + s * (card_w + gap_x);
                    bool is_focused = (active_row == 1 && s == sel_size);
                    bool is_saved   = (csz == active_sz);
                    bool is_hovered = (mouse_on && sui->hover_kind == NL_HOVER_SET_SIZE_CARD &&
                                       sui->hover_index == s);

                    if (is_focused || is_hovered) {
                        int pulse_phase = (int)(state->anim.tp_style_halo_phase & 63U);
                        int tri = (pulse_phase < 32) ? pulse_phase : (64 - pulse_phase);
                        unsigned halo_a = (unsigned)(12 + ((tri * 18) >> 5));
                        uint16_t outer_halo = nl_rgb565_lerp(th->bg_main, th->accent_secondary, halo_a);
                        fb_fill_rounded_rect_clipped(fb, cx - 2, sz_y - 2, card_w + 4, sz_h + 4, 8, outer_halo, 138, 218);
                    }

                    uint16_t s_border = is_focused
                        ? nl_rgb565_lerp(th->border_subtle, th->accent_primary, style_glow)
                        : (is_hovered ? th->accent_secondary : (is_saved ? th->accent_primary : th->border_subtle));
                    uint16_t s_bg = (is_focused || is_hovered)
                        ? nl_rgb565_lerp(th->card_bg, th->card_bg_sel, style_glow >> 1)
                        : th->card_bg;
                    fb_fill_rounded_rect_clipped(fb, cx, sz_y, card_w, sz_h, 6, s_border, 138, 218);
                    fb_fill_rounded_rect_clipped(fb, cx + 1, sz_y + 1, card_w - 2, sz_h - 2, 5, s_bg, 138, 218);

                    int cw = 21, ch = 21, chx = 0, chy = 0;
                    nl_cursor_get_size_metrics(active_st, csz, &cw, &ch, &chx, &chy);

                    int stage_x = cx + 13;
                    int stage_y = sz_y + 6;
                    fb_fill_rounded_rect_clipped(fb, stage_x, stage_y, 64, 38, 5, th->border_subtle, 138, 218);
                    fb_fill_rounded_rect_clipped(fb, stage_x + 1, stage_y + 1, 62, 36, 4, stage_bg, 138, 218);

                    int draw_hx = stage_x + 32 - (cw / 2) + chx;
                    int draw_hy = stage_y + 19 - (ch / 2) + chy;
                    draw_cursor_alpha_sprite_sized(fb, active_st, csz, draw_hx, draw_hy,
                                                   256U, true, stage_y + 1, stage_y + 37,
                                                   state->anim.cur_style_phase, th);

                    int tw = (int)strlen(size_titles[s]) * 6;
                    fb_draw_text_clipped(fb, cx + (card_w - tw) / 2, sz_y + 47, size_titles[s],
                                         (is_focused || is_saved) ? th->text_primary : th->text_secondary, 10, 138, 218);

                    char dim_buf[14];
                    snprintf(dim_buf, sizeof(dim_buf), "%dx%d%s", cw, ch, is_saved ? "*" : "");
                    int dw = (int)strlen(dim_buf) * 6;
                    fb_draw_text_clipped(fb, cx + (card_w - dw) / 2, sz_y + 59, dim_buf,
                                         is_saved ? th->accent_secondary : th->text_secondary, 10, 138, 218);
                }
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
                    bool hov = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == idx);
                    const nl_game_t *g = &state->games[idx];

                    fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6,
                                                 nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha), 28, 218);
                    if (hov && !sel) {
                        fb_draw_rounded_outline_clipped(fb, 12, ry, 292, row_h, 6, th->accent_secondary, 28, 218);
                    }
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
                    bool hov_k = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                  sui->hover_kind == NL_HOVER_SC_KEY_CELL && sui->hover_index == k);
                    uint16_t box_col = (owner >= 0) ? th->border_subtle : th->card_bg;
                    fb_fill_rounded_rect_clipped(fb, kx, ky, kw, 20, 4, box_col, 44 + m_dy, 204 + m_dy);
                    if (hov_k) {
                        fb_draw_rounded_outline_clipped(fb, kx, ky, kw, 20, 4, th->accent_secondary, 44 + m_dy, 204 + m_dy);
                    }
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
                bool hov = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                            sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == idx);

                fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6,
                                             nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha), 28, 218);
                if (hov && !sel) {
                    fb_draw_rounded_outline_clipped(fb, 12, ry, 292, row_h, 6, th->accent_secondary, 28, 218);
                }

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

                bool hov_ret = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == 0);
                fb_fill_rounded_rect_clipped(fb, 96, 168, 128, 26, 6, th->accent_primary, 26, 220);
                if (hov_ret) {
                    fb_draw_rounded_outline_clipped(fb, 96, 168, 128, 26, 6, th->accent_secondary, 26, 220);
                }
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
                bool hov_skip = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                 sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == 0);
                bool hov_cancel = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                   sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == 1);
                fb_fill_rounded_rect_clipped(fb, 30, 174, 124, 24, 5, th->accent_primary, 26, 220);
                if (hov_skip) {
                    fb_draw_rounded_outline_clipped(fb, 30, 174, 124, 24, 5, th->accent_secondary, 26, 220);
                }
                fb_draw_text(fb, 42, 182, "DROITE Passer", th->text_on_accent, 16);

                fb_fill_rounded_rect_clipped(fb, 166, 174, 124, 24, 5, th->pill_bg, 26, 220);
                if (hov_cancel) {
                    fb_draw_rounded_outline_clipped(fb, 166, 174, 124, 24, 5, th->accent_secondary, 26, 220);
                }
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

                bool hov_val = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_SET_ROW && sui->hover_index == 0);
                fb_fill_rounded_rect_clipped(fb, 96, 172, 128, 24, 6, th->accent_primary, 26, 220);
                if (hov_val) {
                    fb_draw_rounded_outline_clipped(fb, 96, 172, 128, 24, 6, th->accent_secondary, 26, 220);
                }
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
                    bool hov = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_ICON_CARD && sui->hover_index == idx);
                    const nl_game_t *g = &state->games[idx];

                    if (sel || hov) {
                        fb_fill_rounded_rect_clipped(fb, cx - 2, cy - 2, cw + 4, ch + 4, 11,
                                                     sel ? th->accent_primary : th->accent_secondary, clip_top, clip_bot);
                    }

                    draw_game_cover_card(fb, cx, cy, cw, ch, 9, sel ? 18 : 0, g, th, clip_top, clip_bot);

                    /* Custom icon vs Default emblem badge in top-right */
                    bool has_custom = (g->icon_source != NL_ICON_NONE);
                    uint16_t b_bg = has_custom ? th->accent_primary : th->pill_bg;
                    uint16_t b_fg = has_custom ? th->text_on_accent : th->text_secondary;
                    const char *b_txt = (g->icon_source == NL_ICON_PNG) ? "PNG"
                                      : (g->icon_source == NL_ICON_BMP) ? "BMP"
                                      : (has_custom ? "ICO" : "DEF");
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

                bool hov_no = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                               sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 0);
                bool hov_yes = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 1);

                fb_fill_rounded_rect_clipped(fb, 46, 144 + m_dy, 102, 24, 5,
                                             nl_rgb565_lerp(th->pill_bg, th->accent_primary, a_no), 26, 220);
                if (hov_no) {
                    fb_draw_rounded_outline_clipped(fb, 46, 144 + m_dy, 102, 24, 5, th->accent_secondary, 26, 220);
                }
                fb_draw_text(fb, 74, 152 + m_dy, "Annuler",
                             nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_no), 10);

                fb_fill_rounded_rect_clipped(fb, 172, 144 + m_dy, 102, 24, 5,
                                             nl_rgb565_lerp(th->pill_bg, RGB565(220, 38, 38), a_yes), 26, 220);
                if (hov_yes) {
                    fb_draw_rounded_outline_clipped(fb, 172, 144 + m_dy, 102, 24, 5, th->accent_secondary, 26, 220);
                }
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
            bool hov_no = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                           sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 0);
            bool hov_yes = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                            sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 1);

            fb_fill_rounded_rect_clipped(fb, 42, 166 + m_dy, 108, 26, 6,
                                         nl_rgb565_lerp(th->pill_bg, th->accent_primary, a_no), 26, 220);
            if (hov_no) {
                fb_draw_rounded_outline_clipped(fb, 42, 166 + m_dy, 108, 26, 6, th->accent_secondary, 26, 220);
            }
            fb_draw_text(fb, 74, 175 + m_dy, "Annuler",
                         nl_rgb565_lerp(th->text_primary, th->text_on_accent, a_no), 8);

            fb_fill_rounded_rect_clipped(fb, 170, 166 + m_dy, 108, 26, 6,
                                         nl_rgb565_lerp(th->pill_bg, RGB565(220, 38, 38), a_yes), 26, 220);
            if (hov_yes) {
                fb_draw_rounded_outline_clipped(fb, 170, 166 + m_dy, 108, 26, 6, th->accent_secondary, 26, 220);
            }
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

                    int total_rows = pcount + 2;
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
                        bool hov = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                    sui->hover_kind == NL_HOVER_PROF_ROW && sui->hover_index == row);
                        uint16_t row_bg = nl_rgb565_lerp(th->card_bg, th->card_bg_sel, r_alpha);
                        fb_fill_rounded_rect_clipped(fb, 12, ry, 292, row_h, 6, row_bg, 28, 218);
                        if (hov && !sel) {
                            fb_draw_rounded_outline_clipped(fb, 12, ry, 292, row_h, 6, th->accent_secondary, 28, 218);
                        }

                        if (row == 0) {
                            /* Row 0: Global Padlock Protection Toggle */
                            uint16_t badge_bg = nl_rgb565_lerp(th->pill_bg, th->accent_primary, r_alpha);
                            fb_fill_aa_capsule_clipped(fb, 18, ry + 4, 20, 20, badge_bg, 28, 218);
                            draw_padlock_icon(fb, 28, ry + 14,
                                              sel ? th->text_on_accent : th->accent_secondary,
                                              badge_bg, state->settings.profile_lock_enabled, 28, 218);
                            fb_draw_text_clipped(fb, 44, ry + 6, "Protection par cadenas", th->text_primary, 26, 28, 218);
                            fb_draw_text_clipped(fb, 44, ry + 16, "Verrouiller certains profils", th->text_secondary, 30, 28, 218);
                            if (ry >= 26 && ry + row_h <= 220) {
                                int lock_fp = state->anim.switch_prof_lock_fp;
                                if ((state->settings.profile_lock_enabled && lock_fp == 0) ||
                                    (!state->settings.profile_lock_enabled && lock_fp == 256)) {
                                    lock_fp = state->settings.profile_lock_enabled ? 256 : 0;
                                }
                                draw_toggle_switch(fb, 260, ry + 7, lock_fp, th);
                            }
                        } else if (row == 1) {
                            /* Row 1: Create new profile */
                            fb_fill_aa_capsule_clipped(fb, 18, ry + 4, 20, 20,
                                                       nl_rgb565_lerp(th->pill_bg, th->accent_primary, r_alpha), 28, 218);
                            uint16_t plus_c = sel ? th->text_on_accent : th->accent_secondary;
                            fb_fill_rect_clipped(fb, 27, ry + 9, 2, 10, plus_c, 28, 218);
                            fb_fill_rect_clipped(fb, 23, ry + 13, 10, 2, plus_c, 28, 218);
                            fb_draw_text_clipped(fb, 44, ry + 6, "Creer un profil", th->text_primary, 24, 28, 218);
                            fb_draw_text_clipped(fb, 44, ry + 16, "Choisir une icone disponible", th->text_secondary, 30, 28, 218);

                            char cnt_buf[16];
                            snprintf(cnt_buf, sizeof(cnt_buf), "%d/%d", pcount, NL_MAX_PROFILES);
                            int cw = (int)strlen(cnt_buf) * 6 + 8;
                            fb_fill_rounded_rect_clipped(fb, 286 - cw, ry + 7, cw, 14, 4, th->pill_bg, 28, 218);
                            fb_draw_text_clipped(fb, 290 - cw, ry + 11, cnt_buf, th->accent_secondary, 6, 28, 218);
                        } else {
                            int pi = row - 2;
                            const nl_profile_t *p = &state->profiles.items[pi];
                            bool is_act = (pi == state->profiles.active_index);
                            bool is_prot = nl_profile_is_protected(state, pi);
                            if (is_act) {
                                fb_fill_aa_capsule_clipped(fb, 17, ry + 3, 22, 22, th->accent_primary, 28, 218);
                            }
                            draw_circular_avatar_box(fb, 18, ry + 4, 20, p->avatar_id, 28, 218, false, th);

                            fb_draw_text_ellipsis(fb, 44, ry + 6, p->name, th->text_primary, 20, 28, 218);
                            char sub[40];
                            if (is_prot) {
                                snprintf(sub, sizeof(sub), "Profil verrouille");
                            } else if (p->is_primary) {
                                snprintf(sub, sizeof(sub), "Profil principal");
                            } else {
                                snprintf(sub, sizeof(sub), "%s", nl_avatar_category_label(nl_avatar_category_of(p->avatar_id)));
                            }
                            fb_draw_text_ellipsis(fb, 44, ry + 16, sub, th->text_secondary, 26, 28, 218);

                            if (is_prot) {
                                int lx = is_act ? 236 : 276;
                                draw_padlock_icon(fb, lx, ry + 14, RGB565(251, 191, 36), row_bg, true, 28, 218);
                            }
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
                    bool is_prot = nl_profile_is_protected(state, di);
                    bool can_del = (!dp->is_primary && pcount > 1 && !is_prot);

                    /* Top Summary Card */
                    fb_fill_rounded_rect_clipped(fb, 12, 30, 296, 44, 7, th->border_subtle, 26, 220);
                    fb_fill_rounded_rect_clipped(fb, 13, 31, 294, 42, 6, th->card_bg, 26, 220);
                    if (is_act) {
                        fb_fill_aa_capsule_clipped(fb, 19, 34, 36, 36, th->accent_primary, 26, 220);
                    }
                    draw_circular_avatar_box(fb, 20, 35, 34, dp->avatar_id, 26, 220, false, th);

                    fb_draw_text_ellipsis(fb, 62, 39, dp->name, th->text_primary, 20, 26, 220);
                    char info_line[48];
                    snprintf(info_line, sizeof(info_line), "%s  |  %s",
                             is_prot ? "Verrouille" : (dp->is_primary ? "Principal" : "Profil libre"),
                             nl_avatar_category_label(nl_avatar_category_of(dp->avatar_id)));
                    fb_draw_text_ellipsis(fb, 62, 53, info_line, th->text_secondary, 34, 26, 220);
                    if (is_prot) {
                        int lx = is_act ? 248 : 284;
                        draw_padlock_icon(fb, lx, 51, RGB565(251, 191, 36), th->card_bg, true, 26, 220);
                    }
                    if (is_act) {
                        fb_fill_rounded_rect_clipped(fb, 260, 38, 38, 14, 4, th->accent_primary, 26, 220);
                        fb_draw_text_clipped(fb, 264, 42, "ACTIF", th->text_on_accent, 5, 26, 220);
                    }

                    /* 5 Management Actions (compact 24px rows to fit cleanly above bottom bar) */
                    const char *act_labels[5] = {
                        "Modifier le nom",
                        "Changer l'icone",
                        "Activer ce profil",
                        dp->is_locked ? "Deverrouiller le profil" : "Verrouiller le profil",
                        "Supprimer le profil"
                    };
                    const int row_y0 = 80;
                    const int row_h = 24;
                    const int row_gap = 2;
                    const int stride = row_h + row_gap;
                    for (int r = 0; r < 5; r++) {
                        int ry = row_y0 + r * stride;
                        bool sel = (r == sui->prof_detail_cursor);
                        bool hov_r = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid &&
                                      sui->hover_kind == NL_HOVER_PROF_DETAIL_ROW && sui->hover_index == r);
                        bool danger = (r == 4 && can_del);
                        bool disabled = ((r == 0 || r == 1) && is_prot) ||
                                        (r == 3 && !state->settings.profile_lock_enabled) ||
                                        (r == 4 && !can_del);
                        uint16_t b_col = sel
                            ? (danger ? RGB565(220, 38, 38) : th->accent_primary)
                            : (hov_r ? th->accent_secondary : th->border_subtle);
                        uint16_t r_bg = sel ? th->card_bg_sel : th->card_bg;
                        fb_fill_rounded_rect_clipped(fb, 12, ry - 1, 296, row_h + 2, 6, b_col, 26, 220);
                        fb_fill_rounded_rect_clipped(fb, 13, ry, 294, row_h, 5, r_bg, 26, 220);

                        uint16_t t_col = danger
                            ? RGB565(248, 113, 113)
                            : (disabled ? th->text_secondary : th->text_primary);
                        int tx_off = 24;
                        if (r == 3) {
                            draw_padlock_icon(fb, 26, ry + 12,
                                              disabled ? th->text_secondary : (dp->is_locked ? RGB565(251, 191, 36) : th->accent_secondary),
                                              r_bg, dp->is_locked, 26, 220);
                            tx_off = 38;
                        }
                        fb_draw_text_clipped(fb, tx_off, ry + 9, act_labels[r], t_col, 24, 26, 220);

                        char rpill[20] = "";
                        if (r == 0) snprintf(rpill, sizeof(rpill), "%s", is_prot ? "Verrouille" : dp->name);
                        else if (r == 1) snprintf(rpill, sizeof(rpill), "%s", is_prot ? "Verrouille" : "");
                        else if (r == 2) snprintf(rpill, sizeof(rpill), "%s", is_act ? "Actif" : "Basculer");
                        else if (r == 3) {
                            if (!state->settings.profile_lock_enabled) snprintf(rpill, sizeof(rpill), "Inactif");
                            else snprintf(rpill, sizeof(rpill), "%s", dp->is_locked ? "Verrouille" : "Libre");
                        } else if (r == 4) {
                            snprintf(rpill, sizeof(rpill), "%s", is_prot ? "Verrouille" : (can_del ? "Suppr." : "Protege"));
                        }
                        if (r == 1 && !is_prot) {
                            snprintf(rpill, sizeof(rpill), "#%d", dp->avatar_id + 1);
                        }

                        if (rpill[0] != '\0') {
                            int pw = (int)strlen(rpill) * 6 + 10;
                            if (pw > 100) pw = 100;
                            fb_fill_rounded_rect_clipped(fb, 284 - pw, ry + 5, pw, 14, 4,
                                                         danger ? RGB565(127, 29, 29) : th->pill_bg, 26, 220);
                            fb_draw_text_ellipsis(fb, 289 - pw, ry + 9, rpill,
                                                  danger ? RGB565(254, 202, 202) : (disabled ? th->text_secondary : th->accent_secondary),
                                                  14, 26, 220);
                        }
                        fb_draw_text_clipped(fb, 291, ry + 9, ">",
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

        case NL_UI_SETTINGS_RESCAN: {
            const nl_scan_stats_t *sc = &state->scan;
            bool m_hov = (is_active_scr && nl_mouse_is_active(state) && sui->hover_valid);
            if (sc->active) {
                static const char *const hints_run[2] = {"ESC / EXE Annuler", "CTRL Mode Souris"};
                draw_settings_chrome(state, fb, th, "RESCANNER LES JEUX", NL_ICO_REFRESH, hints_run, 2);

                fb_fill_rounded_rect_clipped(fb, 14, 34, 292, 60, 6, th->card_bg, 26, 220);
                const char *phase_str = sc->phase_label[0] ? sc->phase_label : "Recherche des jeux...";
                fb_draw_text_ellipsis(fb, 24, 41, phase_str, th->text_primary, 36, 26, 220);

                int pct = sc->progress_pct;
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                char pct_buf[8];
                snprintf(pct_buf, sizeof(pct_buf), "%3d%%", pct);
                fb_draw_text(fb, 266, 41, pct_buf, th->accent_secondary, 5);

                char dir_line[64];
                snprintf(dir_line, sizeof(dir_line), "Dossier : %.50s",
                         sc->current_dir[0] ? sc->current_dir : "/documents");
                fb_draw_text_ellipsis(fb, 24, 55, dir_line, th->accent_secondary, 44, 26, 220);

                /* Real determinate progress bar with subtle moving sheen */
                fb_fill_rounded_rect_clipped(fb, 24, 71, 272, 13, 4, th->pill_bg, 26, 220);
                int fill_w = (272 * pct) / 100;
                if (fill_w < 8 && pct > 0) fill_w = 8;
                if (fill_w > 272) fill_w = 272;
                if (fill_w > 0) {
                    fb_fill_rounded_rect_clipped(fb, 24, 72, fill_w, 11, 4, th->accent_primary, 26, 220);
                    if (fill_w >= 24) {
                        int sheen_x = 24 + ((sc->files_examined * 7 + sc->dirs_scanned * 13) % (fill_w - 14));
                        uint16_t sheen_col = nl_rgb565_lerp(th->accent_primary, RGB565(255, 255, 255), 10U);
                        fb_fill_rounded_rect_clipped(fb, sheen_x, 73, 14, 9, 3, sheen_col, 26, 220);
                    }
                }

                /* Live counters card */
                fb_fill_rounded_rect_clipped(fb, 14, 100, 292, 82, 6, th->card_bg, 26, 220);
                char l1[56], l2[56], l3[56], l4[56];
                snprintf(l1, sizeof(l1), "Dossiers analyses  : %d", sc->dirs_scanned);
                snprintf(l2, sizeof(l2), "Fichiers examines  : %d", sc->files_examined);
                snprintf(l3, sizeof(l3), "Jeux detectes      : %d", sc->games_found);
                snprintf(l4, sizeof(l4), "Ignores : %d   |   Erreurs : %d", sc->files_ignored, sc->errors_count);
                fb_draw_text(fb, 24, 108, l1, th->text_primary, 44);
                fb_draw_text(fb, 24, 126, l2, th->text_secondary, 44);
                fb_draw_text(fb, 24, 144, l3, RGB565(74, 222, 128), 44);
                fb_draw_text(fb, 24, 162, l4,
                             (sc->errors_count > 0) ? RGB565(251, 191, 36) : th->text_secondary, 44);

                /* Prominent interactive Cancel button (clickable in Mode Souris or via ESC/EXE) */
                bool cancel_hov = (m_hov && sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 0);
                uint16_t btn_bg = sc->cancel_requested
                    ? RGB565(180, 83, 9)
                    : (cancel_hov ? RGB565(220, 38, 38) : th->pill_bg);
                uint16_t btn_border = (cancel_hov || sc->cancel_requested)
                    ? RGB565(248, 113, 113)
                    : th->border_subtle;
                uint16_t btn_fg = (cancel_hov || sc->cancel_requested)
                    ? RGB565(255, 255, 255)
                    : th->text_primary;
                fb_fill_rounded_rect_clipped(fb, 96, 189, 128, 22, 6, btn_border, 26, 220);
                fb_fill_rounded_rect_clipped(fb, 97, 190, 126, 20, 5, btn_bg, 26, 220);
                const char *clbl = sc->cancel_requested ? "Annulation..." : "Annuler (ESC)";
                int cw = (int)strlen(clbl) * 6;
                fb_draw_text(fb, 96 + (128 - cw) / 2, 196, clbl, btn_fg, 20);
            } else if (sc->empty_confirm_pending) {
                static const char *const hints_emp[3] = {"L/R Choisir", "EXE Valider", "ESC Conserver"};
                draw_settings_chrome(state, fb, th, "RESCANNER LES JEUX", NL_ICO_REFRESH, hints_emp, 3);

                fb_fill_rounded_rect_clipped(fb, 14, 34, 292, 130, 7, RGB565(251, 191, 36), 26, 220);
                fb_fill_rounded_rect_clipped(fb, 15, 35, 290, 128, 6, th->bg_top_bar, 26, 220);

                fb_draw_text(fb, 26, 46, "AUCUN JEU TROUVE PENDANT LE SCAN !", RGB565(251, 191, 36), 40);
                char w1[56], w2[56];
                snprintf(w1, sizeof(w1), "Votre bibliotheque contient %d jeu%s.",
                         sc->prev_games_count, (sc->prev_games_count > 1) ? "x" : "");
                snprintf(w2, sizeof(w2), "Dossiers : %d | Fichiers : %d | Err : %d",
                         sc->dirs_scanned, sc->files_examined, sc->errors_count);
                fb_draw_text(fb, 26, 68, w1, th->text_primary, 44);
                fb_draw_text(fb, 26, 86, w2, th->text_secondary, 44);
                fb_draw_text(fb, 26, 108, "Par securite, vos jeux n'ont pas ete effaces.", RGB565(74, 222, 128), 46);
                fb_draw_text(fb, 26, 126, "Voulez-vous conserver la liste actuelle", th->text_secondary, 44);
                fb_draw_text(fb, 26, 140, "ou la remplacer par une liste vide ?", th->text_secondary, 44);

                bool sel_rep = sc->empty_confirm_replace;
                bool hov_keep = (m_hov && sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 0);
                bool hov_rep  = (m_hov && sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 1);

                fb_fill_rounded_rect_clipped(fb, 34, 174, 118, 24, 5,
                                             !sel_rep ? th->accent_primary : th->card_bg, 26, 220);
                if (hov_keep) {
                    fb_draw_rounded_outline_clipped(fb, 34, 174, 118, 24, 5, th->accent_secondary, 26, 220);
                }
                fb_draw_text(fb, 66, 182, "Conserver",
                             !sel_rep ? th->text_on_accent : th->text_primary, 12);

                fb_fill_rounded_rect_clipped(fb, 168, 174, 118, 24, 5,
                                             sel_rep ? RGB565(220, 52, 52) : th->card_bg, 26, 220);
                if (hov_rep) {
                    fb_draw_rounded_outline_clipped(fb, 168, 174, 118, 24, 5, th->accent_secondary, 26, 220);
                }
                fb_draw_text(fb, 200, 182, "Remplacer",
                             sel_rep ? RGB565(255, 255, 255) : th->text_secondary, 12);
            } else {
                static const char *const hints_done[3] = {"EXE/ESC Fermer", "DEL Relancer", "Dossiers sauves"};
                draw_settings_chrome(state, fb, th, "RESCANNER LES JEUX", NL_ICO_REFRESH, hints_done, 3);

                fb_fill_rounded_rect_clipped(fb, 14, 32, 292, 144, 6, th->card_bg, 26, 220);
                fb_draw_text(fb, 24, 40,
                             sc->finished ? "RESUME DE LA RECHERCHE" : "PREPARATION DU RESCAN...",
                             th->accent_secondary, 36);

                char s1[56], s2[56], s3[56], s4[56], s5[56];
                snprintf(s1, sizeof(s1), "Jeux trouves : %d  (+%d aj. / %d cons. / -%d ret.)",
                         state->total_games, sc->games_added, sc->games_kept, sc->games_removed);
                snprintf(s2, sizeof(s2), "Icones : %d chargees | %d generiques",
                         sc->icons_loaded, sc->icons_fallback);
                snprintf(s3, sizeof(s3), "Dossiers memorises : %d  (analyses : %d)",
                         state->folders.count, sc->dirs_scanned);
                snprintf(s4, sizeof(s4), "Fichiers : %d exam. | %d ign. | %d err.",
                         sc->files_examined, sc->files_ignored, sc->errors_count);
                if (sc->last_error[0] != '\0') {
                    snprintf(s5, sizeof(s5), "Note : %.46s", sc->last_error);
                } else {
                    snprintf(s5, sizeof(s5), "Index et dossiers sauvegardes sans perte.");
                }

                fb_draw_text_ellipsis(fb, 24, 58, s1, th->text_primary, 46, 26, 220);
                fb_draw_text_ellipsis(fb, 24, 78, s2, th->accent_secondary, 46, 26, 220);
                fb_draw_text_ellipsis(fb, 24, 98, s3, th->text_secondary, 46, 26, 220);
                fb_draw_text_ellipsis(fb, 24, 118, s4, th->text_secondary, 46, 26, 220);
                fb_draw_text_ellipsis(fb, 24, 142, s5,
                                      sc->last_error[0] ? RGB565(251, 191, 36) : RGB565(74, 222, 128),
                                      46, 26, 220);
                fb_draw_text_ellipsis(fb, 24, 158, "Les icones restent 100% independantes des jeux.",
                                      th->text_secondary, 47, 26, 220);

                bool hov_close = (m_hov && sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 0);
                bool hov_again = (m_hov && sui->hover_kind == NL_HOVER_MODAL_BTN && sui->hover_index == 1);

                fb_fill_rounded_rect_clipped(fb, 34, 184, 118, 22, 5, th->accent_primary, 26, 220);
                if (hov_close) {
                    fb_draw_rounded_outline_clipped(fb, 34, 184, 118, 22, 5, th->accent_secondary, 26, 220);
                }
                fb_draw_text(fb, 54, 191, "Fermer (EXE)", th->text_on_accent, 14);

                fb_fill_rounded_rect_clipped(fb, 168, 184, 118, 22, 5, th->card_bg_sel, 26, 220);
                if (hov_again) {
                    fb_draw_rounded_outline_clipped(fb, 168, 184, 118, 22, 5, th->accent_secondary, 26, 220);
                }
                fb_draw_text(fb, 184, 191, "Relancer (DEL)", th->accent_secondary, 16);
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

        uint16_t *buf_a = renderer_get_scratch_a();
        uint16_t *buf_b = buf_a ? renderer_get_scratch_b() : NULL;
        if (!buf_a || !buf_b) {
            render_settings_single_screen(state, target_scr, true, fb, th);
            return;
        }

        render_settings_single_screen(state, state->anim.set_prev_screen, false, buf_a, th);
        render_settings_single_screen(state, target_scr, true, buf_b, th);

        int t = state->anim.set_sec_fp;
        int inv = 256 - t;
        int ease_fp = 256 - ((inv * inv * inv) >> 16);
        unsigned alpha = (unsigned)((ease_fp + 4) >> 3);
        if (alpha > 32U) alpha = 32U;

        int dir = (state->anim.set_sec_dir >= 0) ? 1 : -1;
        int dx_old = -(dir * ((14 * ease_fp + 128) >> 8));
        int dx_new = dir * ((14 * (256 - ease_fp) + 128) >> 8);
        uint16_t bg_c = th->bg_main;

        /* Top bar (0..25) and bottom bar (220..239) crossfade cleanly in place */
        for (int py = 0; py < NL_SCREEN_H; py++) {
            uint16_t *dst = fb + py * NL_SCREEN_W;
            const uint16_t *row_a = buf_a + py * NL_SCREEN_W;
            const uint16_t *row_b = buf_b + py * NL_SCREEN_W;
            if (py <= NL_TOP_BAR_H || py >= NL_BOTTOM_BAR_Y - 1) {
                for (int px = 0; px < NL_SCREEN_W; px++) {
                    uint16_t a_px = row_a[px];
                    uint16_t b_px = row_b[px];
                    dst[px] = (a_px == b_px) ? b_px : nl_rgb565_lerp(a_px, b_px, alpha);
                }
            } else {
                for (int px = 0; px < NL_SCREEN_W; px++) {
                    int sx_a = px - dx_old;
                    int sx_b = px - dx_new;
                    uint16_t ca = ((unsigned)sx_a < NL_SCREEN_W) ? row_a[sx_a] : bg_c;
                    uint16_t cb = ((unsigned)sx_b < NL_SCREEN_W) ? row_b[sx_b] : bg_c;
                    dst[px] = (ca == cb) ? cb : nl_rgb565_lerp(ca, cb, alpha);
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

static bool ensure_settings_drop_field(void) {
    if (G_SET_DROP_FIELD) return true;
    G_SET_DROP_FIELD = (uint16_t *)malloc((size_t)NL_FB_PIXELS * sizeof(uint16_t));
    if (!G_SET_DROP_FIELD) return false;

    init_ink_sin_lut();
    const int cx = NL_GEAR_BTN_X + NL_GEAR_BTN_D / 2; /* 302 */
    const int cy = NL_GEAR_BTN_Y + NL_GEAR_BTN_D / 2; /* 12 */
    int16_t dir_scale[256];
    for (int th_i = 0; th_i < 256; th_i++) {
        int m1 = G_INK_SIN256[(th_i + 29) & 255];
        int m2 = G_INK_SIN256[((th_i * 2) + 87) & 255];
        int m3 = G_INK_SIN256[((th_i * 3) + 149) & 255];
        int m5 = G_INK_SIN256[((th_i * 5) + 43) & 255];
        int bias = (m1 * 18 + m2 * 22 + m3 * 16 + m5 * 9) >> 7;
        dir_scale[th_i] = (int16_t)(256 + bias);
    }

    for (int py = 0; py < NL_SCREEN_H; py++) {
        int dy = py - cy;
        int dy2 = dy * dy;
        int row_off = py * NL_SCREEN_W;
        for (int px = 0; px < NL_SCREEN_W; px++) {
            int dx = px - cx;
            int r = ink_isqrt32(dx * dx + dy2);
            int th256 = ink_atan2_256(dx, dy);
            int wave = (G_INK_SIN256[((px * 3 + py * 2) + 17) & 255] * 4 +
                        G_INK_SIN256[((px * 2 - py * 4) + 91) & 255] * 3) >> 7;
            int ramp = (r < 36) ? ((r * 256) / 36) : 256;
            int d = ((r * dir_scale[th256]) >> 8) + ((ramp * wave) >> 8);
            if (d < 0) d = 0;
            if (d > 65535) d = 65535;
            G_SET_DROP_FIELD[row_off + px] = (uint16_t)d;
        }
    }
    return true;
}

/* Composite Settings buffer over Library buffer as an organic asymmetric liquid drop.
 * Pre-baked distance field + 25-entry alpha/glow LUTs => ZERO isqrt, ZERO atan2, ZERO divisions per frame! */
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

    if (!ensure_settings_drop_field()) {
        memcpy(fb_lib, fb_set, NL_FB_BYTES);
        return;
    }

    static const uint8_t k_alpha_lut[25] = {
         0,  1,  3,  4,  5,  7,  8,  9, 11, 12, 13, 15, 16,
        17, 19, 20, 21, 23, 24, 25, 27, 28, 29, 31, 32
    };
    static const uint8_t k_glow_lut[25] = {
         0,  0,  1,  2,  3,  4,  5,  5,  6,  7,  8,  9, 10,
         9,  8,  7,  6,  5,  5,  4,  3,  2,  1,  0,  0
    };

    const int max_blob_val = 408;
    const int edge_w = 24;
    int ease256 = settings_open_ease256(open_fp);
    int front = ((max_blob_val + edge_w + 4) * ease256) >> 8;
    unsigned bg_dim = (unsigned)((ease256 * 8) >> 8);
    uint16_t dim_col = th->bg_main;
    uint16_t rim_col = th->accent_secondary;
    const uint16_t *field = G_SET_DROP_FIELD;

    for (int i = 0; i < NL_FB_PIXELS; i++) {
        int delta = front - (int)field[i];
        if (delta >= edge_w) {
            fb_lib[i] = fb_set[i];
        } else if (delta <= 0) {
            if (bg_dim > 0U) {
                fb_lib[i] = nl_rgb565_lerp(fb_lib[i], dim_col, bg_dim);
            }
        } else {
            uint16_t base_px = (bg_dim > 0U) ? nl_rgb565_lerp(fb_lib[i], dim_col, bg_dim) : fb_lib[i];
            uint16_t blended = nl_rgb565_lerp(base_px, fb_set[i], (unsigned)k_alpha_lut[delta]);
            unsigned glow_a = (unsigned)k_glow_lut[delta];
            if (glow_a > 0U) {
                blended = nl_rgb565_lerp(blended, rim_col, glow_a);
            }
            fb_lib[i] = blended;
        }
    }
}

/* Smart Adaptive Pagination (Circular dots while all fit <= 19, compact dashes when > 19) */
static void render_library_pagination(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    const nl_layout_metrics_t *m,
    bool m_act
) {
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

    int p_sx = 0;
    int p_ex = 0;

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
        p_sx = cur_x;

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
            bool hov_dot = (m_act && state->sui.hover_kind == NL_HOVER_PAGE_DOT &&
                            state->sui.hover_index == i);
            uint16_t dcol = hov_dot
                ? th->accent_secondary
                : nl_rgb565_lerp(th->dot_inactive, th->accent_primary, alpha_col);
            fb_fill_aa_capsule_clipped(fb, cur_x, m->pagination_y, pw, dot_d, dcol, 0, NL_SCREEN_H);
            p_ex = cur_x + pw;
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
        p_sx = cur_x;
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
            int dy = m->pagination_y + (8 - dh) / 2;
            bool hov_dot = (m_act && state->sui.hover_kind == NL_HOVER_PAGE_DOT &&
                            state->sui.hover_index == i);
            uint16_t dcol = hov_dot
                ? th->accent_secondary
                : nl_rgb565_lerp(th->dot_inactive, th->accent_primary, alpha_col);
            if (pw <= 2) {
                fb_fill_rect_clipped(fb, cur_x, dy, pw, dh, dcol, 0, NL_SCREEN_H);
            } else {
                fb_fill_aa_capsule_clipped(fb, cur_x, dy, pw, dh, dcol, 0, NL_SCREEN_H);
            }
            p_ex = cur_x + pw;
            cur_x += pw + gap;
        }
    }

    /* Mouse Mode ONLY: clickable '<' and '>' pagination arrows flanking dots/dashes */
    if (nl_mouse_is_active(state) && total_pages > 1) {
        int ax_l = (p_sx >= 13) ? (p_sx - 12) : 2;
        int ax_r = (p_ex + 13 <= NL_SCREEN_W) ? (p_ex + 7) : (NL_SCREEN_W - 7);
        uint16_t lc = (m_act && state->sui.hover_kind == NL_HOVER_PAGE_PREV)
            ? th->accent_primary : th->text_secondary;
        uint16_t rc = (m_act && state->sui.hover_kind == NL_HOVER_PAGE_NEXT)
            ? th->accent_primary : th->text_secondary;
        fb_draw_text(fb, ax_l, m->pagination_y, "<", lc, 1);
        fb_draw_text(fb, ax_r, m->pagination_y, ">", rc, 1);
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

    bool m_act = (nl_mouse_is_active(state) && state->sui.hover_valid);
    {
        const nl_profile_t *ap = nl_profile_get_active(state);
        int av_id = ap ? ap->avatar_id : 0;
        bool hov_p = (m_act && state->sui.hover_kind == NL_HOVER_PROFILE_BADGE);
        fb_fill_aa_capsule_clipped(fb, 9, 4, 17, 17,
                                   hov_p ? th->accent_secondary : th->accent_primary, 0, NL_TOP_BAR_H);
        draw_circular_avatar_box(fb, 10, 5, 15, av_id, 0, NL_TOP_BAR_H, false, th);
    }

    static const int k_tab_x[NL_CAT_COUNT] = { 34, 76, 112 };
    static const int k_tab_w[NL_CAT_COUNT] = { 36, 30, 30 };

    for (int c = 0; c < NL_CAT_COUNT; c++) {
        fb_fill_rounded_rect_clipped(fb, k_tab_x[c], 5, k_tab_w[c], 15, 4, th->pill_bg, 0, NL_SCREEN_H);
        if (m_act && state->sui.hover_kind == NL_HOVER_CATEGORY && state->sui.hover_index == c &&
            c != (int)state->active_category) {
            fb_draw_rounded_outline_clipped(fb, k_tab_x[c], 5, k_tab_w[c], 15, 4, th->accent_secondary, 0, NL_TOP_BAR_H);
        }
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

    /* Top-Bar Favorite Star Toggle Button (x=148..164, y=5..20 — keeps x=250..290 clean bg_top_bar) */
    {
        bool sel_is_fav = (state->filtered_count > 0 && state->selected_pos >= 0 &&
                           state->selected_pos < state->filtered_count &&
                           state->games[state->filtered_indices[state->selected_pos]].is_favorite);
        bool hov_fav = (m_act && state->sui.hover_kind == NL_HOVER_FAV_BTN);
        fb_fill_rounded_rect_clipped(fb, 148, 5, 16, 15, 4,
                                     sel_is_fav ? th->card_bg_sel : th->pill_bg, 0, NL_TOP_BAR_H);
        if (hov_fav) {
            fb_draw_rounded_outline_clipped(fb, 148, 5, 16, 15, 4, th->accent_secondary, 0, NL_TOP_BAR_H);
        }
        fb_draw_char(fb, 153, 9, '\x7F', sel_is_fav ? th->badge_fav : th->text_secondary, 0, NL_TOP_BAR_H);
    }

    /* Subtle compact non-blocking diagnostic pill at x=170..208 (keeps x=250..290 pure bg_top_bar) */
    if (state->diag.count > 0) {
        char d_pill[16];
        snprintf(d_pill, sizeof(d_pill), "! %d", state->diag.count);
        int dpw = (int)strlen(d_pill) * 6 + 8;
        if (dpw > 40) dpw = 40;
        fb_fill_rounded_rect_clipped(fb, 170, 5, dpw, 15, 4, th->pill_bg, 0, NL_TOP_BAR_H);
        fb_draw_text_clipped(fb, 174, 9, d_pill, RGB565(251, 191, 36), 5, 0, NL_TOP_BAR_H);
    }

    /* Top-right round gear icon button */
    {
        bool hov_gear = (m_act && state->sui.hover_kind == NL_HOVER_SETTINGS_BTN);
        if (hov_gear) {
            fb_fill_aa_capsule_clipped(fb, NL_GEAR_BTN_X - 2, NL_GEAR_BTN_Y - 2,
                                       NL_GEAR_BTN_D + 4, NL_GEAR_BTN_D + 4, th->accent_secondary, 0, NL_TOP_BAR_H);
        }
        draw_gear_button(fb, NL_GEAR_BTN_X, NL_GEAR_BTN_Y, NL_GEAR_BTN_D, th, false);
    }

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
    int grid_clip_bot = view_bottom;
    /* #R18 Horizontal Mode ONLY: draw pagination in background layer BEFORE library cards
     * and allow cards/halos/labels to extend cleanly over the pagination band up to the bottom bar.
     * Vertical Mode remains 100% untouched. */
    if (m.is_horizontal) {
        render_library_pagination(state, fb, th, &m, m_act);
        int h_clip_bot = NL_SCREEN_H;
        if (state->anim.switch_shortcuts_fp > 0) {
            int bar_y = NL_SCREEN_H - ((NL_BOTTOM_BAR_H * state->anim.switch_shortcuts_fp + 128) >> 8);
            if (bar_y < NL_BOTTOM_BAR_Y) bar_y = NL_BOTTOM_BAR_Y;
            if (bar_y - 1 < h_clip_bot) h_clip_bot = bar_y - 1;
        }
        if (h_clip_bot < view_bottom) h_clip_bot = view_bottom;
        grid_clip_bot = h_clip_bot;
    }
    if (grid_clip_top > grid_clip_bot) grid_clip_top = grid_clip_bot;

    if (state->filtered_count > 0) {
        int scroll_x = state->anim.scroll_x_fp >> 4;
        int scroll_y = state->anim.scroll_y_fp >> 4;
        int sel_game_idx = state->filtered_indices[state->selected_pos];

        /* Fast visible-range culling for large libraries (e.g., 78 games) when no post-return glide is active */
        bool any_glide = false;
        if (skip_game_idx < 0 && !state->anim.launch_anim_active) {
            for (int gi = 0; gi < state->total_games; gi++) {
                if (state->anim.card_dx_fp[gi] != 0 || state->anim.card_dy_fp[gi] != 0) {
                    any_glide = true;
                    break;
                }
            }
        }
        int f_start = 0;
        int f_end = state->filtered_count;
        if (!any_glide && m.per_page > 0) {
            if (m.is_horizontal && m.page_stride_x > 0) {
                int p_min = (scroll_x - m.page_stride_x) / m.page_stride_x;
                if (p_min < 0) p_min = 0;
                int p_max = (scroll_x + NL_SCREEN_W + m.page_stride_x) / m.page_stride_x;
                f_start = p_min * m.per_page;
                f_end = (p_max + 1) * m.per_page;
            } else if (m.page_stride_y > 0) {
                int p_min = (scroll_y - m.page_stride_y) / m.page_stride_y;
                if (p_min < 0) p_min = 0;
                int p_max = (scroll_y + view_bottom + m.page_stride_y) / m.page_stride_y;
                f_start = p_min * m.per_page;
                f_end = (p_max + 1) * m.per_page;
            }
            if (f_start < 0) f_start = 0;
            if (f_end > state->filtered_count) f_end = state->filtered_count;
        }

        for (int pass = 0; pass < 2; pass++) {
            int loop_lo = (pass == 1) ? state->selected_pos : f_start;
            int loop_hi = (pass == 1) ? (state->selected_pos + 1) : f_end;
            if (loop_lo < 0) loop_lo = 0;
            if (loop_hi > state->filtered_count) loop_hi = state->filtered_count;
            for (int f_idx = loop_lo; f_idx < loop_hi; f_idx++) {
                int game_idx = state->filtered_indices[f_idx];
                if (skip_game_idx >= 0 && game_idx == skip_game_idx) continue;
                bool is_sel_pass = (pass == 1);
                if ((game_idx == sel_game_idx) != is_sel_pass) continue;
                if (!is_sel_pass && other_card_alpha_32 == 0U) continue;

                const nl_game_t *g = &state->games[game_idx];
                int row_in_page = (m.per_page > 0) ? ((f_idx % m.per_page) / m.cols) : ((f_idx / m.cols) % m.rows);

                int glide_dx = (skip_game_idx < 0 && !state->anim.launch_anim_active)
                    ? (state->anim.card_dx_fp[game_idx] >> 4)
                    : 0;
                int glide_dy = (skip_game_idx < 0 && !state->anim.launch_anim_active)
                    ? (state->anim.card_dy_fp[game_idx] >> 4)
                    : 0;
                int slot_x = 0, slot_y = 0;
                nl_compute_grid_slot_pos(&m, state->filtered_count, f_idx, &slot_x, &slot_y);
                int base_x = slot_x + cat_dx - scroll_x + glide_dx;
                int base_y = slot_y + search_h - scroll_y + glide_dy;

                if (base_x + m.card_w + 12 <= 0 || base_x - 12 >= NL_SCREEN_W) {
                    continue;
                }
                if (base_y + m.card_h + m.label_h + 12 < grid_clip_top || base_y - 12 >= grid_clip_bot) {
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

                /* C1-continuous top-row clearance: depends ONLY on scale_fp */
                if (search_h == 0 && row_in_page == 0 && scale_fp > NL_SCALE_NORMAL) {
                    int exp_top = (draw_h - m.card_h) / 2;
                    int top_edge = m.top_y - exp_top;
                    if (top_edge < NL_VIEW_TOP + 1) {
                        draw_y += (NL_VIEW_TOP + 1) - top_edge;
                    }
                }

                int hl_fp = 0;
                if (!state->anim.launch_anim_active && !state->anim.launch_holding && scale_fp > NL_SCALE_NORMAL) {
                    hl_fp = ((scale_fp - NL_SCALE_NORMAL) * 32) / (NL_SCALE_SELECTED - NL_SCALE_NORMAL);
                    if (hl_fp > 32) hl_fp = 32;
                }

                int draw_rad = (m.radius * scale_fp + 128) >> 8;
                if (draw_rad < 1 && m.radius > 0) draw_rad = 1;
                bool has_alpha_png = (g->icon_source != NL_ICON_NONE && g->icon_pixels != NULL && g->icon_alpha != NULL);

                int box_x = draw_x, box_y = draw_y, box_w = draw_w, box_h = draw_h;
                int icon_src_w = NL_COVER_W, icon_src_h = NL_COVER_H;
                if (g->icon_source != NL_ICON_NONE && g->icon_pixels != NULL) {
                    icon_src_w = (g->icon_w > 0) ? (int)g->icon_w : NL_COVER_W;
                    icon_src_h = (g->icon_h > 0) ? (int)g->icon_h : NL_COVER_H;
                    int ox = 0, oy = 0;
                    compute_icon_contain_box(icon_src_w, icon_src_h, draw_w, draw_h, &ox, &oy, &box_w, &box_h);
                    box_x = draw_x + ox;
                    box_y = draw_y + oy;
                }

                if (hl_fp > 0 && other_card_alpha_32 >= 32U) {
                    if (has_alpha_png) {
                        draw_alpha_icon_glow_clipped(fb, box_x, box_y, box_w, box_h,
                                                     g->icon_alpha, icon_src_w, icon_src_h,
                                                     hl_fp, th->is_light, grid_clip_top, grid_clip_bot);
                    } else {
                        fb_rounded_soft_luminance_clipped(fb, box_x, box_y, box_w, box_h,
                                                          draw_rad, hl_fp, th->is_light,
                                                          grid_clip_top, grid_clip_bot);
                    }
                }

                if (is_sel_pass && scale_fp > NL_SCALE_NORMAL && other_card_alpha_32 >= 32U) {
                    if (has_alpha_png) {
                        draw_alpha_icon_shadow_clipped(fb, box_x, box_y, box_w, box_h,
                                                       g->icon_alpha, icon_src_w, icon_src_h,
                                                       2, 3, th->is_light, grid_clip_top, grid_clip_bot);
                    } else {
                        fb_rounded_shadow_clipped(fb, box_x + 2, box_y + 3, box_w, box_h,
                                                  draw_rad, th->is_light, grid_clip_top, grid_clip_bot,
                                                  box_x, box_y);
                    }
                }

                bool allow_card_cache = (scale_fp == NL_SCALE_NORMAL || scale_fp == NL_SCALE_SELECTED);
                draw_game_cover_card_ex(
                    fb, draw_x, draw_y, draw_w, draw_h, draw_rad, hl_fp,
                    g, th, grid_clip_top, grid_clip_bot, allow_card_cache
                );

                /* Mode Souris hover outline (never changes layout or triggers launch on hover) */
                if (m_act && state->sui.hover_kind == NL_HOVER_GAME_CARD &&
                    state->sui.hover_index == f_idx && other_card_alpha_32 >= 32U) {
                    fb_draw_rounded_outline_clipped(fb, box_x - 1, box_y - 1, box_w + 2, box_h + 2,
                                                    draw_rad + 1, th->accent_secondary,
                                                    grid_clip_top, grid_clip_bot);
                }

                /* Progressive fade-out of non-selected cards into bg_main during game launch */
                if (!is_sel_pass && other_card_alpha_32 < 32U) {
                    int fy0 = (draw_y < grid_clip_top) ? grid_clip_top : draw_y;
                    int fy1 = (draw_y + draw_h > grid_clip_bot) ? grid_clip_bot : (draw_y + draw_h);
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
                            max_ch, grid_clip_top, grid_clip_bot
                        );
                    }
                }
            }
        }

        /* R15 Horizontal / R17 Vertical: Subtle edge fade so natural peeking icons from neighboring pages blend cleanly */
        if (m.is_horizontal && state->total_pages > 1 && other_card_alpha_32 >= 32U) {
            static const uint8_t k_edge_a[10] = { 20, 18, 15, 13, 10, 8, 6, 4, 2, 1 };
            for (int py = NL_VIEW_TOP; py < grid_clip_bot; py++) {
                uint16_t *row_px = fb + py * NL_SCREEN_W;
                for (int ex = 0; ex < 10; ex++) {
                    unsigned a = (unsigned)k_edge_a[ex];
                    if (row_px[ex] != th->bg_main) {
                        row_px[ex] = nl_rgb565_lerp(row_px[ex], th->bg_main, a);
                    }
                    int rx_px = NL_SCREEN_W - 1 - ex;
                    if (row_px[rx_px] != th->bg_main) {
                        row_px[rx_px] = nl_rgb565_lerp(row_px[rx_px], th->bg_main, a);
                    }
                }
            }
        } else if (!m.is_horizontal && state->total_pages > 1 && search_h == 0 && other_card_alpha_32 >= 32U) {
            static const uint8_t k_vedge_a[8] = { 20, 17, 14, 11, 8, 6, 3, 1 };
            for (int ey = 0; ey < 8; ey++) {
                unsigned a = (unsigned)k_vedge_a[ey];
                int ty = NL_VIEW_TOP + ey;
                if (ty >= NL_VIEW_TOP && ty < view_bottom) {
                    uint16_t *trow = fb + ty * NL_SCREEN_W;
                    for (int px = 0; px < NL_SCREEN_W; px++) {
                        if (trow[px] != th->bg_main) {
                            trow[px] = nl_rgb565_lerp(trow[px], th->bg_main, a);
                        }
                    }
                }
                int by = view_bottom - 1 - ey;
                if (by >= NL_VIEW_TOP && by < view_bottom) {
                    uint16_t *brow = fb + by * NL_SCREEN_W;
                    for (int px = 0; px < NL_SCREEN_W; px++) {
                        if (brow[px] != th->bg_main) {
                            brow[px] = nl_rgb565_lerp(brow[px], th->bg_main, a);
                        }
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

    /* 4. Smart Adaptive Pagination — Vertical Mode keeps exact post-grid z-order (Horizontal Mode already rendered behind library) */
    if (!m.is_horizontal) {
        render_library_pagination(state, fb, th, &m, m_act);
    }

    /* 5. Bottom Centered Shortcut Bar (smoothly hidden when show_shortcuts == false) */
    bool bar_visible = false;
    if (state->anim.switch_shortcuts_fp > 0) {
        int bar_y = NL_SCREEN_H - ((NL_BOTTOM_BAR_H * state->anim.switch_shortcuts_fp + 128) >> 8);
        if (bar_y < NL_BOTTOM_BAR_Y) bar_y = NL_BOTTOM_BAR_Y;
        if (bar_y < NL_SCREEN_H) {
            fb_fill_rect_clipped(fb, 0, bar_y - 1, NL_SCREEN_W, 1, th->border_subtle, 0, NL_SCREEN_H);
            fb_fill_rect_clipped(fb, 0, bar_y, NL_SCREEN_W, NL_SCREEN_H - bar_y, th->bg_bottom_bar, 0, NL_SCREEN_H);
            draw_centered_shortcuts_at_y(fb, bar_y, th);
            if (bar_y <= NL_SCREEN_H - 6) bar_visible = true;
        }
    }

    /* 6. #R20 Ultra-discreet bottom-left micro-signature "daniel.b" (x=3..27, y=234..238, zero overlap) */
    if (other_card_alpha_32 >= 32U) {
        uint16_t sig_bg = bar_visible ? th->bg_bottom_bar : th->bg_main;
        uint16_t sig_col = nl_rgb565_lerp(sig_bg, th->text_secondary, 13U);
        draw_micro_signature_daniel_b(fb, 3, NL_SCREEN_H - 6, sig_col);
    }
}

static inline void fnv64_mix_u32(uint64_t *h, uint32_t v) {
    *h = (*h ^ (uint64_t)v) * 1099511628211ULL;
}

static inline void fnv64_mix_u64(uint64_t *h, uint64_t v) {
    *h = (*h ^ (v & 0xFFFFFFFFULL)) * 1099511628211ULL;
    *h = (*h ^ (v >> 32)) * 1099511628211ULL;
}

static inline void fnv64_mix_str(uint64_t *h, const char *s, int max_len) {
    if (!s) return;
    for (int i = 0; i < max_len && s[i] != '\0'; i++) {
        *h = (*h ^ (uint64_t)(uint8_t)s[i]) * 1099511628211ULL;
    }
    *h = (*h ^ 0xFFULL) * 1099511628211ULL;
}

/* Exhaustive 64-bit signature of every state input consumed by render_library_frame_ex(state, fb, th, -1, 32U) */
static uint64_t compute_library_frame_sig(const nl_state_t *state) {
    uint64_t h = 14695981039346656037ULL;
    fnv64_mix_u32(&h, (uint32_t)state->display_mode);
    fnv64_mix_u32(&h, (uint32_t)state->accent_color);

    const nl_profile_t *ap = nl_profile_get_active(state);
    fnv64_mix_u32(&h, (uint32_t)(ap ? ap->avatar_id : 0));

    fnv64_mix_u32(&h, (uint32_t)state->active_category);
    fnv64_mix_u32(&h, (uint32_t)state->anim.cat_ind_x_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.cat_ind_w_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.cat_trans_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.cat_trans_dir);

    fnv64_mix_u32(&h, (uint32_t)state->sui.screen);
    fnv64_mix_u32(&h, (uint32_t)state->settings.layout);
    fnv64_mix_u32(&h, (uint32_t)state->settings.scroll_dir);
    fnv64_mix_u32(&h, (uint32_t)(state->settings.show_names ? 1U : 0U));
    fnv64_mix_u32(&h, (uint32_t)state->anim.switch_shortcuts_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.switch_shownames_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.layout_trans_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.layout_from);

    fnv64_mix_u32(&h, (uint32_t)state->diag.count);
    fnv64_mix_u32(&h, (uint32_t)(state->sui.search_active ? 1U : 0U));
    fnv64_mix_u32(&h, (uint32_t)state->sui.search_len);
    fnv64_mix_u32(&h, (uint32_t)state->anim.search_bar_fp);
    if (state->sui.search_len > 0) {
        fnv64_mix_str(&h, state->sui.search_query, state->sui.search_len);
    }

    bool m_online = nl_mouse_is_active(state);
    fnv64_mix_u32(&h, m_online ? 1U : 0U);
    if (m_online && state->sui.hover_valid) {
        nl_hover_kind_t hk = state->sui.hover_kind;
        if (hk == NL_HOVER_PROFILE_BADGE || hk == NL_HOVER_CATEGORY ||
            hk == NL_HOVER_FAV_BTN || hk == NL_HOVER_SETTINGS_BTN ||
            hk == NL_HOVER_GAME_CARD || hk == NL_HOVER_PAGE_DOT ||
            hk == NL_HOVER_PAGE_PREV || hk == NL_HOVER_PAGE_NEXT) {
            fnv64_mix_u32(&h, (uint32_t)hk);
            fnv64_mix_u32(&h, (uint32_t)state->sui.hover_index);
        }
    }

    fnv64_mix_u32(&h, (uint32_t)state->total_games);
    fnv64_mix_u32(&h, (uint32_t)state->filtered_count);
    fnv64_mix_u32(&h, (uint32_t)state->selected_pos);
    fnv64_mix_u32(&h, (uint32_t)state->total_pages);
    fnv64_mix_u32(&h, (uint32_t)state->current_page);
    fnv64_mix_u32(&h, (uint32_t)state->anim.scroll_x_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.scroll_y_fp);
    fnv64_mix_u32(&h, (uint32_t)state->anim.dot_pos_fp);
    fnv64_mix_u32(&h, (uint32_t)(state->anim.launch_anim_active ? 1U : 0U));
    fnv64_mix_u32(&h, (uint32_t)(state->anim.launch_holding ? 1U : 0U));
    fnv64_mix_u32(&h, (uint32_t)state->anim.launch_shrink_fp);

    if (state->filtered_count > 0) {
        nl_layout_metrics_t m;
        get_interpolated_layout_metrics(state, &m);
        int scroll_x = state->anim.scroll_x_fp >> 4;
        int scroll_y = state->anim.scroll_y_fp >> 4;

        bool any_glide = false;
        if (!state->anim.launch_anim_active) {
            for (int gi = 0; gi < state->total_games; gi++) {
                if (state->anim.card_dx_fp[gi] != 0 || state->anim.card_dy_fp[gi] != 0) {
                    any_glide = true;
                    break;
                }
            }
        }
        int f_start = 0;
        int f_end = state->filtered_count;
        if (!any_glide && m.per_page > 0) {
            if (m.is_horizontal && m.page_stride_x > 0) {
                int p_min = (scroll_x - m.page_stride_x) / m.page_stride_x;
                if (p_min < 0) p_min = 0;
                int p_max = (scroll_x + NL_SCREEN_W + m.page_stride_x) / m.page_stride_x;
                f_start = p_min * m.per_page;
                f_end = (p_max + 1) * m.per_page;
            } else if (m.page_stride_y > 0) {
                int p_min = (scroll_y - m.page_stride_y) / m.page_stride_y;
                if (p_min < 0) p_min = 0;
                int p_max = (scroll_y + m.view_bottom + m.page_stride_y) / m.page_stride_y;
                f_start = p_min * m.per_page;
                f_end = (p_max + 1) * m.per_page;
            }
            if (f_start < 0) f_start = 0;
            if (f_end > state->filtered_count) f_end = state->filtered_count;
        }

        if (state->selected_pos >= 0 && state->selected_pos < state->filtered_count) {
            int sgi = state->filtered_indices[state->selected_pos];
            fnv64_mix_u32(&h, (uint32_t)(state->games[sgi].is_favorite ? 1U : 0U));
            if (state->selected_pos < f_start || state->selected_pos >= f_end) {
                const nl_game_t *sg = &state->games[sgi];
                fnv64_mix_u32(&h, (uint32_t)sgi);
                fnv64_mix_u32(&h, (uint32_t)state->anim.card_scale_fp[sgi]);
                fnv64_mix_u32(&h, (uint32_t)sg->icon_source);
                fnv64_mix_u64(&h, (uint64_t)(uintptr_t)sg->icon_pixels);
                fnv64_mix_u64(&h, (uint64_t)(uintptr_t)sg->icon_alpha);
                if (sg->icon_pixels) {
                    int iw = (sg->icon_w > 0) ? (int)sg->icon_w : NL_ICON_V1_W;
                    int ih = (sg->icon_h > 0) ? (int)sg->icon_h : NL_ICON_V1_H;
                    fnv64_mix_u32(&h, icon_data_fingerprint(sg->icon_pixels, sg->icon_alpha, iw, ih));
                }
            }
        }

        for (int f_idx = f_start; f_idx < f_end; f_idx++) {
            int gi = state->filtered_indices[f_idx];
            const nl_game_t *g = &state->games[gi];
            fnv64_mix_u32(&h, (uint32_t)gi);
            fnv64_mix_u32(&h, (uint32_t)state->anim.card_scale_fp[gi]);
            if (any_glide) {
                fnv64_mix_u32(&h, (uint32_t)state->anim.card_dx_fp[gi]);
                fnv64_mix_u32(&h, (uint32_t)state->anim.card_dy_fp[gi]);
            }
            fnv64_mix_u32(&h, ((uint32_t)g->icon_source << 24) |
                              ((uint32_t)(g->is_favorite ? 1U : 0U) << 23) |
                              ((uint32_t)(uint8_t)g->shortcut_key << 15) |
                              (uint32_t)g->accent_color);
            fnv64_mix_u64(&h, (uint64_t)(uintptr_t)g->icon_pixels);
            fnv64_mix_u64(&h, (uint64_t)(uintptr_t)g->icon_alpha);
            if (g->icon_pixels) {
                int iw = (g->icon_w > 0) ? (int)g->icon_w : NL_ICON_V1_W;
                int ih = (g->icon_h > 0) ? (int)g->icon_h : NL_ICON_V1_H;
                fnv64_mix_u32(&h, icon_data_fingerprint(g->icon_pixels, g->icon_alpha, iw, ih));
            }
            fnv64_mix_str(&h, g->title, (int)sizeof(g->title));
        }
    }
    return h;
}

static void render_library_frame_cached(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    uint64_t *out_sig,
    bool copy_to_fb
) {
    uint64_t sig = compute_library_frame_sig(state);
    if (out_sig) *out_sig = sig;

    if (G_LIB_STATIC_FB && G_LIB_STATIC_VALID && G_LIB_STATIC_SIG == sig) {
        if (copy_to_fb) {
            memcpy(fb, G_LIB_STATIC_FB, NL_FB_BYTES);
        }
        return;
    }

    if (!G_LIB_STATIC_FB) {
        G_LIB_STATIC_FB = (uint16_t *)malloc(NL_FB_BYTES);
    }
    if (G_LIB_STATIC_FB) {
        render_library_frame_ex(state, G_LIB_STATIC_FB, th, -1, 32U);
        G_LIB_STATIC_SIG = sig;
        G_LIB_STATIC_VALID = true;
        if (copy_to_fb) {
            memcpy(fb, G_LIB_STATIC_FB, NL_FB_BYTES);
        }
    } else {
        render_library_frame_ex(state, fb, th, -1, 32U);
    }
}

/* Apply fullscreen dim, using G_DIM_BG_FB cache whenever overlay is fully open over a static library */
static void apply_overlay_dim_cached(
    uint16_t *fb,
    uint16_t dim_col,
    unsigned dim_a,
    bool allow_dim_cache,
    uint64_t lib_sig
) {
    if (allow_dim_cache && G_DIM_BG_FB && G_DIM_BG_VALID &&
        G_DIM_BG_SIG == lib_sig && G_DIM_BG_COL == dim_col && G_DIM_BG_ALPHA == (uint8_t)dim_a) {
        memcpy(fb, G_DIM_BG_FB, NL_FB_BYTES);
        return;
    }

    const uint16_t *src = (allow_dim_cache && G_LIB_STATIC_FB && G_LIB_STATIC_VALID && G_LIB_STATIC_SIG == lib_sig)
        ? G_LIB_STATIC_FB
        : fb;
    for (int i = 0; i < NL_FB_PIXELS; i++) {
        fb[i] = nl_rgb565_lerp(src[i], dim_col, dim_a);
    }

    if (allow_dim_cache) {
        if (!G_DIM_BG_FB) {
            G_DIM_BG_FB = (uint16_t *)malloc(NL_FB_BYTES);
        }
        if (G_DIM_BG_FB) {
            memcpy(G_DIM_BG_FB, fb, NL_FB_BYTES);
            G_DIM_BG_SIG = lib_sig;
            G_DIM_BG_COL = dim_col;
            G_DIM_BG_ALPHA = (uint8_t)dim_a;
            G_DIM_BG_VALID = true;
        }
    }
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

/* 1. R16 "Identity Shift & Orbital Prism" Profile Switch Transition:
 *    - Phase 1 (0..108): Outgoing identity softly recedes in depth while incoming avatar arcs up to center.
 *    - Phase 2 (108..184): Elastic spring lock-in at center + 3 staggered crisp prismatic light pulses + 4 orbital sparks.
 *    - Phase 3 (184..256): Smooth dock-to-corner glide directly into the top-left profile badge (17, 12) revealing the library. */
/* ============================================================================
 * #R22 UNIFIED ORGANIC LIQUID PROPAGATION ENGINE (ARM926EJ-S Integer-Only)
 * Reuses N-Launcher's signature theme-change organic harmonic wave language
 * (G_INK_SIN256 1/2/3/5-harmonic deformation) with:
 *   - Asymmetric, irregular organic contours at start (irreg_256 = 256, +/-27%)
 *   - Progressive rounding as each liquid mass expands (irreg_256 -> ~20)
 *   - Gentle living contour undulation via phase drift during propagation
 *   - 4 successive overlapping theme-adaptive colors (Dark/Light/Gray)
 *   - Zero heap allocations & edge-driven scanline rasterizer (60 FPS on CX II-T)
 * ============================================================================ */

#define NL_LIQ_VERTS     64
#define NL_LIQ_MAX_XINTS 8

static int16_t G_LIQ_XS[4][NL_SCREEN_H][NL_LIQ_MAX_XINTS];
static uint8_t G_LIQ_CNT[4][NL_SCREEN_H];

static uint16_t nl_liquid_theme_color(nl_display_mode_t dm, const nl_theme_t *th, int idx) {
    if (idx < 0) idx = 0;
    if (idx > 3) idx = 3;
    if (dm == NL_DISPLAY_GRAY) {
        /* Gray theme: lightest -> increasingly grayed nuances */
        static const uint16_t k_gray4[4] = {
            RGB565(238, 241, 246),
            RGB565(192, 198, 208),
            RGB565(134, 140, 152),
            RGB565( 78,  83,  92)
        };
        return k_gray4[idx];
    }
    if (dm == NL_DISPLAY_LIGHT) {
        /* Light theme: strictly darkest -> lightest across all 4 accents */
        switch (idx) {
            case 0:  return nl_rgb565_lerp(th->text_primary, th->accent_primary, 8U);
            case 1:  return nl_rgb565_lerp(th->accent_primary, th->text_primary, 4U);
            case 2:  return nl_rgb565_lerp(th->accent_primary, RGB565(255, 255, 255), 16U);
            default: return nl_rgb565_lerp(th->bg_main, RGB565(255, 255, 255), 26U);
        }
    }
    /* Dark theme: lightest -> darkest */
    switch (idx) {
        case 0:  return nl_rgb565_lerp(th->accent_secondary, RGB565(255, 255, 255), 22U);
        case 1:  return nl_rgb565_lerp(th->accent_primary, th->accent_secondary, 16U);
        case 2:  return nl_rgb565_lerp(th->accent_primary, th->bg_main, 16U);
        default: return nl_rgb565_lerp(th->bg_main, th->accent_primary, 4U);
    }
}

uint16_t nl_liquid_theme_color_rgb565(nl_display_mode_t dm, nl_accent_color_t accent, int layer_0_to_3) {
    const nl_theme_t *th = nl_get_theme(dm, accent);
    return nl_liquid_theme_color(dm, th, layer_0_to_3);
}

/* Evaluates organic asymmetric liquid radius at angle_256 (0..255).
 * At irreg_256 = 256: distinct multi-lobed organic asymmetry (+/-27%).
 * As irreg_256 decays toward ~20: progressively rounds out into a smooth liquid sphere. */
int nl_liquid_eval_radius(int angle_256, int r_px, int irreg_256, uint32_t seed, int phase_256) {
    if (r_px <= 1) return 1;
    init_ink_sin_lut();
    if (irreg_256 < 0) irreg_256 = 0;
    if (irreg_256 > 256) irreg_256 = 256;

    int th = angle_256 & 255;
    int s0 = (int)((seed * 37U + 13U) & 255U);
    int s1 = (int)((seed * 73U + 41U) & 255U);
    int s2 = (int)((seed * 113U + 97U) & 255U);
    int s3 = (int)((seed * 157U + 19U) & 255U);

    int m1 = G_INK_SIN256[(th + s0 + phase_256) & 255];
    int m2 = G_INK_SIN256[((th * 2) + s1 - phase_256) & 255];
    int m3 = G_INK_SIN256[((th * 3) + s2 + (phase_256 >> 1)) & 255];
    int m5 = G_INK_SIN256[((th * 5) + s3 - (phase_256 >> 1)) & 255];

    /* Harmonic bias roughly in [-70 .. +70] out of 256 (~+/-27% at full irregularity) */
    int raw_bias = (m1 * 18 + m2 * 24 + m3 * 19 + m5 * 11) >> 7;
    int scaled_bias = (raw_bias * irreg_256) >> 8;
    int r_k = (r_px * (256 + scaled_bias) + 128) >> 8;
    return (r_k < 2) ? 2 : r_k;
}

/* Edge-driven scanline bucket rasterizer for a 64-vertex organic closed liquid contour.
 * Writes sorted intersection X lists into G_LIQ_XS[slot][y] / G_LIQ_CNT[slot][y]
 * over [clip_y0, clip_y1) and outputs tight vertical bounds [out_y0, out_y1). */
static void rasterize_organic_liquid_slot(
    int slot,
    int cx,
    int cy,
    int r_px,
    int irreg_256,
    uint32_t seed,
    int phase_256,
    int clip_y0,
    int clip_y1,
    int *out_y0,
    int *out_y1
) {
    if (slot < 0 || slot > 3) return;
    if (clip_y0 < 0) clip_y0 = 0;
    if (clip_y1 > NL_SCREEN_H) clip_y1 = NL_SCREEN_H;
    if (clip_y0 >= clip_y1 || r_px <= 1) {
        if (out_y0) *out_y0 = clip_y1;
        if (out_y1) *out_y1 = clip_y0;
        return;
    }

    init_ink_sin_lut();

    /* Subtle organic off-center skew that fades to 0 as the shape rounds out */
    int skew_x = (G_INK_SIN256[(seed * 41U + 23U + (uint32_t)phase_256) & 255U] * irreg_256) >> 13;
    int skew_y = (G_INK_SIN256[(seed * 67U + 89U - (uint32_t)phase_256) & 255U] * irreg_256) >> 13;
    int ocx = cx + skew_x;
    int ocy = cy + skew_y;

    int16_t vx[NL_LIQ_VERTS];
    int16_t vy[NL_LIQ_VERTS];
    int min_y = clip_y1;
    int max_y = clip_y0;

    for (int k = 0; k < NL_LIQ_VERTS; k++) {
        int th = k * (256 / NL_LIQ_VERTS);
        int rk = nl_liquid_eval_radius(th, r_px, irreg_256, seed, phase_256);
        int cv = G_INK_SIN256[(th + 64) & 255];
        int sv = G_INK_SIN256[th & 255];
        int px = ocx + ((rk * cv + 64) >> 7);
        int py = ocy + ((rk * sv + 64) >> 7);
        vx[k] = (int16_t)px;
        vy[k] = (int16_t)py;
        if (py < min_y) min_y = py;
        if (py > max_y) max_y = py;
    }

    int y0 = (min_y < clip_y0) ? clip_y0 : min_y;
    int y1 = (max_y + 1 > clip_y1) ? clip_y1 : (max_y + 1);
    if (y0 >= y1) {
        if (out_y0) *out_y0 = clip_y1;
        if (out_y1) *out_y1 = clip_y0;
        return;
    }
    memset(&G_LIQ_CNT[slot][y0], 0, (size_t)(y1 - y0));

    for (int k = 0; k < NL_LIQ_VERTS; k++) {
        int nk = (k + 1) & (NL_LIQ_VERTS - 1);
        int x0 = (int)vx[k];
        int ey0 = (int)vy[k];
        int x1 = (int)vx[nk];
        int ey1 = (int)vy[nk];
        if (ey0 == ey1) continue;
        if (ey0 > ey1) {
            int tx = x0; x0 = x1; x1 = tx;
            int ty = ey0; ey0 = ey1; ey1 = ty;
        }
        int sy0 = (ey0 < y0) ? y0 : ey0;
        int sy1 = (ey1 > y1) ? y1 : ey1;
        if (sy0 >= sy1) continue;

        int32_t dx_step = ((int32_t)(x1 - x0) * 65536) / (int32_t)(ey1 - ey0);
        int32_t cur_x = ((int32_t)x0 * 65536) + (int32_t)(sy0 - ey0) * dx_step + 32768;
        for (int y = sy0; y < sy1; y++, cur_x += dx_step) {
            uint8_t c = G_LIQ_CNT[slot][y];
            if (c < NL_LIQ_MAX_XINTS) {
                G_LIQ_XS[slot][y][c] = (int16_t)(cur_x >> 16);
                G_LIQ_CNT[slot][y] = (uint8_t)(c + 1U);
            }
        }
    }

    for (int y = y0; y < y1; y++) {
        int c = (int)G_LIQ_CNT[slot][y];
        if (c == 2) {
            if (G_LIQ_XS[slot][y][0] > G_LIQ_XS[slot][y][1]) {
                int16_t tmp = G_LIQ_XS[slot][y][0];
                G_LIQ_XS[slot][y][0] = G_LIQ_XS[slot][y][1];
                G_LIQ_XS[slot][y][1] = tmp;
            }
        } else if (c > 2) {
            for (int i = 1; i < c; i++) {
                int16_t key = G_LIQ_XS[slot][y][i];
                int j = i - 1;
                while (j >= 0 && G_LIQ_XS[slot][y][j] > key) {
                    G_LIQ_XS[slot][y][j + 1] = G_LIQ_XS[slot][y][j];
                    j--;
                }
                G_LIQ_XS[slot][y][j + 1] = key;
            }
        }
    }

    if (out_y0) *out_y0 = y0;
    if (out_y1) *out_y1 = y1;
}

/* Renders a single soft-edged organic liquid mass with subtle luminous meniscus rim */
static void draw_organic_liquid_blob(
    uint16_t *fb,
    int cx,
    int cy,
    int r_px,
    int irreg_256,
    uint32_t seed,
    int phase_256,
    uint16_t col,
    uint16_t rim_col,
    unsigned alpha_32,
    int clip_x0,
    int clip_y0,
    int clip_x1,
    int clip_y1
) {
    if (!fb || r_px <= 1 || alpha_32 == 0U) return;
    if (alpha_32 > 32U) alpha_32 = 32U;
    if (clip_x0 < 0) clip_x0 = 0;
    if (clip_y0 < 0) clip_y0 = 0;
    if (clip_x1 > NL_SCREEN_W) clip_x1 = NL_SCREEN_W;
    if (clip_y1 > NL_SCREEN_H) clip_y1 = NL_SCREEN_H;
    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) return;

    int y0 = clip_y1, y1 = clip_y0;
    rasterize_organic_liquid_slot(0, cx, cy, r_px, irreg_256, seed, phase_256, clip_y0, clip_y1, &y0, &y1);
    if (y0 >= y1) return;

    const bool opaque = (alpha_32 >= 32U);
    const uint32_t pair_col = (uint32_t)col | ((uint32_t)col << 16);

    for (int y = y0; y < y1; y++) {
        int c = (int)G_LIQ_CNT[0][y];
        if (c < 2) continue;
        uint16_t *row = fb + y * NL_SCREEN_W;

        /* Soften top/bottom 2 scanlines slightly for clean vertical liquid anti-aliasing */
        unsigned row_a = alpha_32;
        if (y == y0 || y == y1 - 1) {
            row_a = (alpha_32 * 14U) >> 5;
        } else if (y == y0 + 1 || y == y1 - 2) {
            row_a = (alpha_32 * 24U) >> 5;
        }
        if (row_a == 0U) continue;
        bool row_opaque = (opaque && row_a >= 32U);

        for (int p = 0; p + 1 < c; p += 2) {
            int raw_l = (int)G_LIQ_XS[0][y][p];
            int raw_r = (int)G_LIQ_XS[0][y][p + 1];
            int xl = (raw_l < clip_x0) ? clip_x0 : raw_l;
            int xr = (raw_r > clip_x1) ? clip_x1 : raw_r;
            if (xl >= xr) continue;

            int span = xr - xl;
            /* Feathered meniscus edge (3 px on unclipped left/right boundaries) */
            int fl = (raw_l >= clip_x0 && span >= 6) ? 3 : 0;
            int fr = (raw_r <= clip_x1 && span >= 6) ? 3 : 0;

            if (fl > 0) {
                row[xl]     = nl_rgb565_lerp(row[xl],     rim_col, (row_a * 10U) >> 5);
                row[xl + 1] = nl_rgb565_lerp(row[xl + 1], rim_col, (row_a * 21U) >> 5);
                row[xl + 2] = nl_rgb565_lerp(row[xl + 2], col,     (row_a * 28U) >> 5);
            }
            if (fr > 0) {
                row[xr - 3] = nl_rgb565_lerp(row[xr - 3], col,     (row_a * 28U) >> 5);
                row[xr - 2] = nl_rgb565_lerp(row[xr - 2], rim_col, (row_a * 21U) >> 5);
                row[xr - 1] = nl_rgb565_lerp(row[xr - 1], rim_col, (row_a * 10U) >> 5);
            }

            int ix0 = xl + fl;
            int ix1 = xr - fr;
            if (ix0 >= ix1) continue;

            if (row_opaque) {
                int x = ix0;
                if ((x & 1) && x < ix1) {
                    row[x++] = col;
                }
                int pairs = (ix1 - x) >> 1;
                uint32_t *dst32 = (uint32_t *)(row + x);
                for (int i = 0; i < pairs; i++) {
                    dst32[i] = pair_col;
                }
                x += pairs << 1;
                if (x < ix1) {
                    row[x] = col;
                }
            } else {
                for (int x = ix0; x < ix1; x++) {
                    row[x] = nl_rgb565_lerp(row[x], col, row_a);
                }
            }
        }
    }
}

/* Draws 4 successive overlapping organic liquid propagations.
 * Each wave starts visibly irregular/asymmetric and progressively rounds out as it expands.
 *   p256: global progress 0..256
 *   reverse_colors: false = 0->1->2->3 (opening/launch), true = 3->2->1->0 (graceful closing) */
static void draw_organic_liquid_waves_4(
    uint16_t *fb,
    nl_display_mode_t dm,
    const nl_theme_t *th,
    int cx,
    int cy,
    int p256,
    int r_start,
    int r_end,
    unsigned max_alpha_32,
    uint32_t base_seed,
    bool reverse_colors,
    bool fade_out_tail,
    int clip_x0,
    int clip_y0,
    int clip_x1,
    int clip_y1
) {
    if (!fb || !th || p256 <= 0 || max_alpha_32 == 0U) return;
    if (p256 > 256) p256 = 256;

    /* 4 staggered starts so the 4 liquid layers visibly succeed & overlap each other */
    static const uint8_t k_delay[4] = {   0,  34,  68, 102 };
    static const uint8_t k_span[4]  = { 196, 184, 170, 154 };

    for (int w = 0; w < 4; w++) {
        int dp = p256 - (int)k_delay[w];
        if (dp <= 0) continue;
        int span = (int)k_span[w];
        int wt = (dp >= span) ? 256 : ((dp * 256) / span);
        int inv = 256 - wt;
        /* Smooth organic cubic-out expansion */
        int ease = 256 - ((inv * inv * inv) >> 16);

        int r_px = r_start + (((r_end - r_start) * ease + 128) >> 8);
        if (r_px <= 1) continue;

        /* Progressive rounding: starts strongly irregular (256), smoothly rounds toward ~22 */
        int rem = 256 - ease;
        int irreg = 22 + ((((rem * rem) >> 8) * 234) >> 8);
        int phase = ((wt * 30) >> 8) + w * 21;

        unsigned wa = max_alpha_32;
        if (wt < 28) {
            wa = (max_alpha_32 * (unsigned)(8 + (wt * 24) / 28)) >> 5;
        }
        if (fade_out_tail && p256 > 172) {
            unsigned tail = (unsigned)(((256 - p256) * 32) / 84);
            wa = (wa * tail) >> 5;
        }
        if (wa == 0U) continue;

        int col_idx = reverse_colors ? (3 - w) : w;
        uint16_t col = nl_liquid_theme_color(dm, th, col_idx);
        uint16_t rim = (dm == NL_DISPLAY_LIGHT)
            ? nl_rgb565_lerp(col, th->accent_secondary, 10U)
            : nl_rgb565_lerp(col, RGB565(255, 255, 255), 11U);

        draw_organic_liquid_blob(
            fb, cx, cy, r_px, irreg,
            base_seed + (uint32_t)w * 47U, phase,
            col, rim, wa,
            clip_x0, clip_y0, clip_x1, clip_y1
        );
    }
}

/* Exact reverse 4-layer organic liquid reveal directly over the live framebuffer:
 * 4 staggered organic fronts expand outward from (cx, cy) — irregular at first,
 * progressively rounding out — peeling away Color 3 -> Color 2 -> Color 1 -> Color 0
 * to reveal the 100% live, already-rendered N-Launcher Library inside with ZERO delay
 * and ZERO extra framebuffer allocation. */
static void draw_organic_liquid_reveal_4(
    uint16_t *fb,
    nl_display_mode_t dm,
    const nl_theme_t *th,
    int cx,
    int cy,
    int p256,
    uint32_t base_seed
) {
    if (!fb || !th || p256 < 0 || p256 >= 256) return;

    /* Compute max corner radius from (cx, cy) so all 4 screen corners clear cleanly */
    int dx_max = (cx > NL_SCREEN_W - cx) ? cx : (NL_SCREEN_W - cx);
    int dy_max = (cy > NL_SCREEN_H - cy) ? cy : (NL_SCREEN_H - cy);
    int r_full = ink_isqrt32(dx_max * dx_max + dy_max * dy_max) + 28;

    /* 4 staggered fronts:
     *   slot 0 (lead):     pushes Color 3 -> reveals Color 2
     *   slot 1 (2nd):      pushes Color 2 -> reveals Color 1
     *   slot 2 (3rd):      pushes Color 1 -> reveals Color 0
     *   slot 3 (aperture): pushes Color 0 -> reveals LIVE N-Launcher Library!
     * Fronts begin with slight initial lead so Frame 1 immediately shows organic multi-layer bands
     * and the live library aperture opens rapidly right from the center. */
    static const int16_t k_lead[4]  = {  60,  38,  18,   0 };
    static const int16_t k_span[4]  = { 196, 212, 228, 248 };

    int sy0[4] = { NL_SCREEN_H, NL_SCREEN_H, NL_SCREEN_H, NL_SCREEN_H };
    int sy1[4] = { 0, 0, 0, 0 };
    bool active[4] = { false, false, false, false };

    for (int s = 0; s < 4; s++) {
        int ep = p256 + (int)k_lead[s];
        if (ep <= 0) continue;
        int wt = (ep >= (int)k_span[s]) ? 256 : ((ep * 256) / (int)k_span[s]);
        int inv = 256 - wt;
        /* Smooth organic acceleration-deceleration curve */
        int ease = 256 - ((inv * inv * inv) >> 16);
        int r_px = (r_full * ease + 128) >> 8;
        if (r_px < 4) continue;

        /* Starts visibly irregular & asymmetric, progressively rounds out toward borders */
        int rem = 256 - ease;
        int irreg = 22 + ((((rem * rem) >> 8) * 234) >> 8);
        int phase = ((wt * 28) >> 8) + s * 29;

        rasterize_organic_liquid_slot(
            s, cx, cy, r_px, irreg,
            base_seed + (uint32_t)s * 53U, phase,
            0, NL_SCREEN_H, &sy0[s], &sy1[s]
        );
        if (sy0[s] < sy1[s]) {
            active[s] = true;
        }
    }

    uint16_t cols[4];
    for (int i = 0; i < 4; i++) {
        cols[i] = nl_liquid_theme_color(dm, th, i);
    }

    /* Rich liquid opacity that dissolves gently on the final 22% so screen corners vanish cleanly */
    unsigned alpha_32 = (p256 < 196)
        ? 30U
        : (unsigned)(((256 - p256) * 30) / 60);
    if (alpha_32 == 0U) return;

    uint8_t row_id[NL_SCREEN_W];
    for (int y = 0; y < NL_SCREEN_H; y++) {
        /* Fast skip if the innermost live-library aperture already spans [0..319] */
        if (active[3] && y >= sy0[3] && y < sy1[3] && G_LIQ_CNT[3][y] == 2 &&
            G_LIQ_XS[3][y][0] <= 0 && G_LIQ_XS[3][y][1] >= NL_SCREEN_W) {
            continue;
        }

        /* Default outside all 4 fronts = layer 4 (cols[3]: final launch color) */
        memset(row_id, 4, NL_SCREEN_W);

        /* Paint successive expanding organic fronts: 0->layer 3, 1->layer 2, 2->layer 1, 3->layer 0 (live library) */
        for (int s = 0; s < 4; s++) {
            if (!active[s] || y < sy0[s] || y >= sy1[s]) continue;
            int cnt = (int)G_LIQ_CNT[s][y];
            uint8_t fill_id = (uint8_t)(3 - s);
            for (int k = 0; k + 1 < cnt; k += 2) {
                int xl = (int)G_LIQ_XS[s][y][k];
                int xr = (int)G_LIQ_XS[s][y][k + 1];
                if (xl < 0) xl = 0;
                if (xr > NL_SCREEN_W) xr = NL_SCREEN_W;
                if (xl < xr) {
                    memset(row_id + xl, fill_id, (size_t)(xr - xl));
                }
            }
        }

        uint16_t *dst = fb + y * NL_SCREEN_W;
        for (int x = 0; x < NL_SCREEN_W; x++) {
            uint8_t id = row_id[x];
            if (id == 0U) continue; /* Live N-Launcher Library shines through untouched! */

            uint16_t target = cols[id - 1U];
            unsigned a = alpha_32;

            /* Soft 2-px horizontal boundary feather against adjacent layer / live library */
            uint8_t left_id  = (x > 0) ? row_id[x - 1] : id;
            uint8_t right_id = (x + 1 < NL_SCREEN_W) ? row_id[x + 1] : id;
            if (left_id == 0U || right_id == 0U) {
                a = (alpha_32 * 16U) >> 5;
            } else if (left_id != id) {
                target = nl_rgb565_lerp(target, cols[left_id - 1U], 12U);
            } else if (right_id != id) {
                target = nl_rgb565_lerp(target, cols[right_id - 1U], 12U);
            }

            dst[x] = (a >= 32U) ? target : nl_rgb565_lerp(dst[x], target, a);
        }
    }
}

/* 1. R16/R22 Orbital Prism Profile Switch Transition enriched with 4-Layer Organic Liquid Propagation:
 *    Preserves 100% of existing incoming/outgoing avatar choreography, elastic spring lock-in,
 *    3 prismatic pulses, 4 orbital sparks, multicolor halo & top-left dock glide, while adding
 *    4 theme-adaptive organic liquid propagations behind the avatars. */
static void render_profile_switch_transition(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    if (!state->anim.prof_switch_active || state->anim.prof_switch_max <= 0) return;

    int tick = state->anim.prof_switch_tick;
    int max_t = state->anim.prof_switch_max;
    int p = (tick * 256) / max_t;
    if (p < 0) p = 0;
    if (p > 256) p = 256;

    /* Subtle atmospheric veil that blooms gently and clears as avatar docks into top-left badge */
    unsigned veil_a = (p < 168)
        ? (unsigned)((p * 12) / 168)
        : (unsigned)(((256 - p) * 12) / 88);
    if (veil_a > 12U) veil_a = 12U;
    if (veil_a > 0U) {
        uint16_t veil_col = th->is_light ? RGB565(188, 198, 214) : RGB565(6, 9, 16);
        for (int i = NL_TOP_BAR_H * NL_SCREEN_W; i < NL_FB_PIXELS; i++) {
            fb[i] = nl_rgb565_lerp(fb[i], veil_col, veil_a);
        }
    }

    int orig_x = state->anim.prof_switch_origin_x;
    int orig_y = state->anim.prof_switch_origin_y;
    int orig_d = (state->anim.prof_switch_origin_d >= 14) ? state->anim.prof_switch_origin_d : 36;
    const int mid_x = 160;
    const int mid_y = 112;
    const int mid_d = 64;
    const int dock_x = 17;
    const int dock_y = 12;
    const int dock_d = 16;
    int av_id = state->anim.prof_switch_avatar_id;
    int from_av = state->anim.prof_switch_from_avatar_id;

    /* #R22 4-Layer Organic Liquid Propagation behind the incoming avatar & existing prismatic pulses */
    {
        int lp = (p * 256) / 244;
        if (lp > 256) lp = 256;
        int l_inv = 256 - lp;
        int l_ease = 256 - ((l_inv * l_inv) >> 8);
        int lcx = orig_x + (((mid_x - orig_x) * l_ease + 128) >> 8);
        int lcy = orig_y + (((mid_y - orig_y) * l_ease + 128) >> 8);
        draw_organic_liquid_waves_4(
            fb, state->display_mode, th,
            lcx, lcy, lp,
            10, 178, 17U,
            0x2201U + (uint32_t)av_id * 19U,
            false, true,
            0, NL_TOP_BAR_H, NL_SCREEN_W, NL_SCREEN_H
        );
    }

    /* Outgoing identity depth recession (only when switching to a different avatar) */
    if (from_av != av_id && p < 128) {
        int rp = (p * 256) / 128;
        int inv_r = 256 - rp;
        int re = 256 - ((inv_r * inv_r) >> 8);
        int out_d = 42 - ((18 * re) >> 8);
        int out_y = mid_y + ((10 * re) >> 8);
        unsigned out_a = (unsigned)(((256 - re) * 20) >> 8);
        if (out_a > 1U && out_d >= 14) {
            draw_circular_avatar_box_alpha(fb, mid_x - out_d / 2, out_y - out_d / 2, out_d, from_av,
                                           NL_TOP_BAR_H, NL_SCREEN_H, out_a, th->bg_main, 8U);
        }
    }

    int cur_cx = mid_x;
    int cur_cy = mid_y;
    int cur_d = mid_d;
    unsigned main_alpha = 32U;

    if (p <= 108) {
        /* Phase 1: Smooth cubic arc from slot into center with subtle upward lift */
        int t = (p * 256) / 108;
        int inv_t = 256 - t;
        int ease = 256 - ((inv_t * inv_t * inv_t) >> 16);
        int arc_lift = ((t * (256 - t)) * 8) >> 14; /* 0 -> 8 px -> 0 */
        cur_cx = orig_x + (((mid_x - orig_x) * ease + 128) >> 8);
        cur_cy = orig_y + (((mid_y - orig_y) * ease + 128) >> 8) - arc_lift;
        int sp = profile_spring_rebound_fp(t);
        cur_d = orig_d + (((mid_d - orig_d) * sp + 128) >> 8);
    } else if (p <= 184) {
        /* Phase 2: Crisp elastic spring settle at center + 3 prismatic light pulses + 4 orbital sparks */
        int t = ((p - 108) * 256) / 76;
        int sp = profile_spring_rebound_fp(t);
        int pulse_d = ((sp - 256) * 26) >> 8; /* Subtle +4..-1..0 elastic settle */
        cur_cx = mid_x;
        cur_cy = mid_y;
        cur_d = mid_d + pulse_d;
    } else {
        /* Phase 3: Smooth dock-to-corner glide directly into top-left profile badge (17, 12) */
        int t = ((p - 184) * 256) / 72;
        int ease = (t * t * (768 - 2 * t)) >> 17; /* Smoothstep 0..256 */
        cur_cx = mid_x + (((dock_x - mid_x) * ease + 128) >> 8);
        cur_cy = mid_y + (((dock_y - mid_y) * ease + 128) >> 8);
        cur_d = mid_d + (((dock_d - mid_d) * ease + 128) >> 8);
        main_alpha = (t < 176) ? 32U : (unsigned)(((256 - t) * 32) / 80);
    }

    /* Succession of 3 delicate prismatic light pulses + glinting orbital sparks at center */
    if (p >= 44 && p < 236) {
        int pp = ((p - 44) * 256) / 192;
        draw_prism_pulse_rings(fb, mid_x, mid_y, pp, 32, 58, 3,
                               state->anim.qp_glow_phase, true, th, NL_TOP_BAR_H, NL_SCREEN_H);
    }

    if (main_alpha > 0U && cur_d >= 12) {
        int clip_top = (p > 184) ? 0 : NL_TOP_BAR_H;
        draw_multicolor_focus_ring(fb, cur_cx, cur_cy, cur_d + 3, (cur_d >= 36) ? 3 : 2,
                                   state->anim.qp_glow_phase, th->is_light, main_alpha,
                                   clip_top, NL_SCREEN_H);
        draw_circular_avatar_box_alpha(fb, cur_cx - cur_d / 2, cur_cy - cur_d / 2, cur_d, av_id,
                                       clip_top, NL_SCREEN_H, main_alpha, 0, 0U);
    }
}

/* 2. R16/R22 Strictly Centered '+' Elastic Morph + 4-Layer Organic Liquid Propagation (ZERO horizontal drift!) */
static void render_profile_plus_transition(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    if (!state->anim.prof_plus_active || state->anim.prof_plus_max <= 0) return;

    int tick = state->anim.prof_plus_tick;
    int max_t = state->anim.prof_plus_max;
    int p = (tick * 256) / max_t;
    if (p < 0 || p > 256) return;

    /* Strictly anchored at exact '+' button center — never drifts awkwardly to the left */
    int cx = state->anim.prof_plus_origin_x;
    int cy = state->anim.prof_plus_origin_y;

    /* #R22 4-Layer Organic Liquid Propagation blooming from '+' center toward the emerging Avatar Picker */
    if (p >= 12 && p < 254) {
        int lp = ((p - 12) * 256) / 242;
        if (lp > 256) lp = 256;
        draw_organic_liquid_waves_4(
            fb, state->display_mode, th,
            cx, cy, lp,
            8, 172, 18U,
            0x3317U,
            false, true,
            10, 26, NL_SCREEN_W - 10, NL_SCREEN_H - 18
        );
    }

    int cur_d = 36;
    int rot_fp = 0;
    unsigned main_alpha = 32U;
    if (p < 56) {
        /* Phase 1: Tactile spring compress right in place (36 -> 30 px) */
        int t = (p * 256) / 56;
        cur_d = 36 - ((6 * t) >> 8);
        rot_fp = (t * 48) >> 8;
    } else if (p < 156) {
        /* Phase 2: Crisp elastic rebound (30 -> 44 -> 40 px) + smooth 90 deg spin */
        int t = ((p - 56) * 256) / 100;
        int sp = profile_spring_rebound_fp(t);
        cur_d = 30 + ((11 * sp) >> 8);
        if (cur_d > 44) cur_d = 44;
        rot_fp = 48 + ((208 * t) >> 8);
    } else {
        /* Phase 3: Clean centered luminous dissolve + aperture bloom preview of compact picker */
        int t = ((p - 156) * 256) / 100;
        cur_d = 40 - ((6 * t) >> 8);
        rot_fp = 256;
        main_alpha = (unsigned)(((256 - t) * 32) >> 8);

        /* Subtle emerging outline of compact floating Avatar Picker (mx=14, my=32, mw=292, mh=176) */
        int inv_t = 256 - t;
        int ease_t = 256 - ((inv_t * inv_t) >> 8);
        int ix = ((256 - ease_t) * 24) >> 8;
        int iy = ((256 - ease_t) * 16) >> 8;
        int pmx = 14 + ix;
        int pmy = 32 + iy;
        int pmw = 292 - ix * 2;
        int pmh = 176 - iy * 2;
        unsigned frame_a = (unsigned)((ease_t * 12) >> 8);
        if (frame_a > 1U) {
            uint16_t bcol = nl_rgb565_lerp(th->bg_main, th->accent_primary, frame_a);
            fb_draw_rounded_outline_clipped(fb, pmx, pmy, pmw, pmh, 10, bcol, 0, NL_SCREEN_H);
        }
    }

    /* 2 crisp concentric prismatic light pulses radiating from exact '+' center (capped r <= 34 px) */
    if (p >= 44) {
        int rp = ((p - 44) * 256) / 212;
        draw_prism_pulse_rings(fb, cx, cy, rp, 18, 34, 2,
                               state->anim.qp_glow_phase, false, th, NL_TOP_BAR_H, NL_SCREEN_H);
    }

    if (main_alpha > 0U && cur_d >= 16) {
        draw_multicolor_focus_ring(fb, cx, cy, cur_d + 2, 2,
                                   state->anim.qp_glow_phase, th->is_light, main_alpha,
                                   NL_TOP_BAR_H, NL_SCREEN_H);
        draw_plus_badge_alpha(fb, cx, cy, cur_d, rot_fp, main_alpha, false, true, th, NL_TOP_BAR_H, NL_SCREEN_H);
    }
}

/* 3. R16/R22 New User Avatar Arrival Confirmation (Centered Elastic Pop + 4-Layer Organic Liquid Bloom + 2 Prismatic Rings) */
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

    /* #R22 Compact 4-layer organic liquid bloom behind the arriving avatar */
    draw_organic_liquid_waves_4(
        fb, state->display_mode, th,
        dest_cx, dest_cy, p,
        8, 52, 16U,
        0x4423U + (uint32_t)av_id * 13U,
        false, true,
        14, 76, NL_SCREEN_W - 14, 164
    );

    draw_prism_pulse_rings(fb, dest_cx, dest_cy, p, 18, 40, 2,
                           state->anim.qp_glow_phase, true, th, NL_TOP_BAR_H, NL_SCREEN_H);

    if (p < 180) {
        int t = (p * 256) / 180;
        int sp = profile_spring_rebound_fp(t);
        int cur_d = (38 * sp + 128) >> 8;
        if (cur_d < 12) cur_d = 12;
        if (cur_d > 44) cur_d = 44;
        unsigned a = (p <= 128) ? 30U : (unsigned)(((180 - p) * 30) / 52);
        draw_multicolor_focus_ring(fb, dest_cx, dest_cy, cur_d + 2, 2,
                                   state->anim.qp_glow_phase, th->is_light, a,
                                   NL_TOP_BAR_H, NL_SCREEN_H);
        draw_circular_avatar_box_alpha(fb, dest_cx - cur_d / 2, dest_cy - cur_d / 2, cur_d, av_id,
                                       NL_TOP_BAR_H, NL_SCREEN_H, a, 0, 0U);
    }
}

/* R16/R22: Minimalist Quick Profile Selector Overlay
 * Preserves 100% of existing elastic dock bloom, spring halo orbit, wake pulses, avatar bounce & padlocks,
 * enriched with 4-layer organic liquid propagation on BOTH opening (badge -> dock) AND closing (dock -> badge). */
static void render_quick_profile_overlay(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    bool lib_bg_pure,
    uint64_t lib_sig
) {
    int raw_fp = state->anim.qp_open_fp;
    if (raw_fp <= 0 && !state->sui.qp_open) return;
    if (raw_fp <= 0) raw_fp = 256;
    if (raw_fp > 256) raw_fp = 256;

    /* Opening uses damped elastic back-ease bloom; closing uses snappy crisp ease-in */
    int inv = 256 - raw_fp;
    int e_fp = 256 - ((inv * inv * inv) >> 16);
    int elastic_fp = e_fp;
    if (state->anim.qp_opening && raw_fp > 96 && raw_fp < 256) {
        int u = raw_fp - 96;          /* 0..160 */
        int bump = (u * (160 - u)) >> 9; /* 0..12 */
        elastic_fp = e_fp + bump;
        if (elastic_fp > 266) elastic_fp = 266;
    }

    unsigned alpha_32 = (unsigned)((e_fp + 4) >> 3);
    if (alpha_32 > 32U) alpha_32 = 32U;

    /* During '+' centered morph into floating avatar picker, smoothly fade out other selector elements */
    unsigned plus_other_a = 32U;
    if (state->anim.prof_plus_active && state->anim.prof_plus_max > 0) {
        int pp = (state->anim.prof_plus_tick * 256) / state->anim.prof_plus_max;
        if (pp > 256) pp = 256;
        plus_other_a = (pp < 128) ? (unsigned)(((128 - pp) * 32) / 128) : 0U;
    }

    unsigned dim_a = (unsigned)((e_fp * 16) >> 8);
    uint16_t dim_col = th->is_light ? RGB565(178, 188, 204) : RGB565(5, 7, 12);
    apply_overlay_dim_cached(fb, dim_col, dim_a, lib_bg_pure && raw_fp >= 256, lib_sig);

    /* Elastic Dock Bloom geometry (clamped strictly inside [10..310] x [72..168]) */
    int dy = ((256 - elastic_fp) * 10) >> 8;
    int zoom_inset_x = ((256 - elastic_fp) * 14) >> 8;
    int zoom_inset_y = ((256 - elastic_fp) * 6) >> 8;
    if (zoom_inset_x < -2) zoom_inset_x = -2;
    if (zoom_inset_y < -1) zoom_inset_y = -1;
    int mx = 12 + zoom_inset_x;
    int my = 76 + dy + zoom_inset_y;
    int mw = 296 - zoom_inset_x * 2;
    int mh = 88 - zoom_inset_y * 2;

    uint16_t box_bg = th->is_light
        ? RGB565(250, 252, 255)
        : ((state->display_mode == NL_DISPLAY_GRAY) ? RGB565(78, 82, 90) : RGB565(18, 22, 32));
    uint16_t border_col = (e_fp < 240)
        ? nl_rgb565_lerp(th->accent_primary, th->border_subtle, (unsigned)((e_fp * 32) >> 8))
        : th->border_subtle;

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
    int cy = 120 + dy;

    int rel = cur - win_start;
    int target_gx = start_x + rel * stride + diam / 2;
    int glow_cx = (state->anim.qp_glow_x_fp > 0) ? (state->anim.qp_glow_x_fp >> 4) : target_gx;
    int trail_cx = (state->anim.qp_trail_x_fp > 0) ? (state->anim.qp_trail_x_fp >> 4) : glow_cx;
    int glow_cy = ((state->anim.qp_glow_y_fp > 0) ? (state->anim.qp_glow_y_fp >> 4) : 120) + dy;

    /* #R22 Opening Organic Liquid Propagation behind/around the dock (from top-left badge -> dock center) */
    if (state->anim.qp_opening && raw_fp > 4 && raw_fp < 252 && plus_other_a >= 8U) {
        int ocx = 24 + (((160 - 24) * e_fp + 128) >> 8);
        int ocy = 24 + (((cy - 24) * e_fp + 128) >> 8);
        unsigned oa = (16U * plus_other_a) >> 5;
        draw_organic_liquid_waves_4(
            fb, state->display_mode, th,
            ocx, ocy, raw_fp,
            10, 152, oa,
            0x5501U,
            false, true,
            0, 0, NL_SCREEN_W, NL_SCREEN_H
        );
    }

    if (plus_other_a > 0U) {
        fb_fill_rounded_rect_clipped(fb, mx - 1, my - 1, mw + 2, mh + 2, 12, border_col, 0, NL_SCREEN_H);
        fb_fill_rounded_rect_clipped(fb, mx, my, mw, mh, 11, box_bg, 0, NL_SCREEN_H);

        /* #R22 Subtle organic liquid wash gliding INSIDE the dock behind avatars during opening */
        if (state->anim.qp_opening && raw_fp > 8 && raw_fp < 248) {
            int icx = mx + 24 + (((160 - (mx + 24)) * e_fp + 128) >> 8);
            unsigned ia = (14U * plus_other_a) >> 5;
            draw_organic_liquid_waves_4(
                fb, state->display_mode, th,
                icx, cy, raw_fp,
                10, 160, ia,
                0x5529U,
                false, true,
                mx + 2, my + 2, mx + mw - 2, my + mh - 2
            );
        }

        /* Prismatic top-edge light sweep during open bloom */
        if (state->anim.qp_opening && raw_fp > 32 && raw_fp < 248) {
            int sw_x = mx + 14 + (((mw - 68) * raw_fp) >> 8);
            unsigned sw_a = (unsigned)(((256 - raw_fp) * 26) >> 8);
            uint16_t sw_c = nl_rgb565_lerp(border_col, th->accent_secondary, sw_a);
            fb_fill_rect_clipped(fb, sw_x, my - 1, 40, 1, sw_c, 0, NL_SCREEN_H);
        }
    }

    bool qp_m = (nl_mouse_is_active(state) && state->sui.hover_valid);
    if (plus_other_a >= 8U) {
        if (win_start > 0) {
            uint16_t lc = (qp_m && state->sui.hover_kind == NL_HOVER_QP_PREV) ? th->accent_primary : th->text_secondary;
            fb_draw_text(fb, mx + 5, cy - 3, "<", lc, 1);
        }
        if (win_start + shown < total_slots) {
            uint16_t rc = (qp_m && state->sui.hover_kind == NL_HOVER_QP_NEXT) ? th->accent_primary : th->text_secondary;
            fb_draw_text(fb, mx + mw - 11, cy - 3, ">", rc, 1);
        }
    }

    /* Elastic Spring Halo Orbit + Succession of 3 Micro Light Pulses during selection glide */
    unsigned halo_a = (alpha_32 * plus_other_a) >> 5;
    if (halo_a > 0U) {
        int wake_dx = glow_cx - trail_cx;
        if (abs(wake_dx) >= 3) {
            static const uint8_t k_pulse_t[3] = { 1, 2, 3 };
            static const uint8_t k_pulse_a[3] = { 10, 16, 22 };
            for (int pi = 0; pi < 3; pi++) {
                int px_dot = trail_cx + (wake_dx * (int)k_pulse_t[pi]) / 4;
                int py_arc = glow_cy - ((pi == 1) ? 3 : 1);
                unsigned pa = (halo_a * (unsigned)k_pulse_a[pi]) >> 5;
                uint16_t pc = nl_rgb565_lerp(box_bg, (pi == 2) ? th->accent_secondary : th->accent_primary, pa);
                fb_fill_aa_capsule_clipped(fb, px_dot - 2, py_arc - 2, 4, 4, pc, my, my + mh);
            }
            draw_multicolor_focus_ring(fb, (glow_cx + trail_cx) / 2, glow_cy, 38, 2,
                                       (uint16_t)(state->anim.qp_glow_phase - 14U), th->is_light,
                                       (halo_a * 12U) >> 5, my, my + mh);
        }

        /* Subtle elastic pulse on open ignition or selection settle */
        if (state->anim.qp_sel_bounce_fp > 24 && plus_other_a >= 16U) {
            int bp = 256 - state->anim.qp_sel_bounce_fp;
            draw_prism_pulse_rings(fb, glow_cx, glow_cy, bp, 19, 27, 1,
                                   state->anim.qp_glow_phase, false, th, my, my + mh);
        }

        int bounce_px = (state->anim.qp_sel_bounce_fp * 4 + 128) >> 8;
        int focus_ring_d = 40 + bounce_px;
        draw_multicolor_focus_ring(fb, glow_cx, glow_cy, focus_ring_d, 3,
                                   state->anim.qp_glow_phase, th->is_light, halo_a, my, my + mh);
    }

    int birth_slot_cx = 160;
    int bounce_px = (state->anim.qp_sel_bounce_fp * 3 + 128) >> 8;
    for (int v = 0; v < shown; v++) {
        int slot = win_start + v;
        int scx = start_x + v * stride + diam / 2;
        if (slot == state->anim.prof_birth_slot) birth_slot_cx = scx;
        bool focused = (slot == cur);
        bool slot_hov = (qp_m && state->sui.hover_kind == NL_HOVER_QP_SLOT && state->sui.hover_index == slot);

        /* Hide the static '+' slot while its strictly centered morph animation plays */
        if (slot >= pcount && state->anim.prof_plus_active) {
            continue;
        }

        unsigned slot_alpha = (alpha_32 * plus_other_a) >> 5;
        if (slot_alpha == 0U) continue;

        /* Synchronized elastic avatar bloom + depth contrast between focused & background slots */
        int sc_fp = (slot >= 0 && slot < NL_MAX_PROFILES + 1 && state->anim.qp_slot_scale_fp[slot] > 0)
            ? state->anim.qp_slot_scale_fp[slot]
            : (focused ? 276 : 228);
        int cur_diam = (diam * sc_fp + 128) >> 8;
        if (focused) cur_diam += bounce_px;
        cur_diam = (cur_diam * (194 + ((62 * elastic_fp) >> 8))) >> 8;
        if (cur_diam < 16) cur_diam = 16;
        if (cur_diam > 42) cur_diam = 42;

        int lift_y = focused ? ((state->anim.qp_sel_bounce_fp > 96) ? -2 : -1) : 0;
        int scy = cy + lift_y;
        int sx = scx - cur_diam / 2;
        int sy = scy - cur_diam / 2;

        if (slot < pcount) {
            const nl_profile_t *p = &state->profiles.items[slot];
            bool is_act = (slot == state->profiles.active_index);
            bool is_prot = nl_profile_is_protected(state, slot);
            if (!focused) {
                uint16_t ring_c = slot_hov
                    ? th->accent_secondary
                    : (is_act ? th->accent_primary : th->border_subtle);
                int ring_pad = is_act ? 2 : 1;
                fb_fill_aa_capsule_clipped(fb, sx - ring_pad, sy - ring_pad,
                                           cur_diam + ring_pad * 2, cur_diam + ring_pad * 2,
                                           ring_c, my, my + mh);
            }
            if (slot_alpha >= 30U && focused) {
                draw_circular_avatar_box(fb, sx, sy, cur_diam, p->avatar_id, my, my + mh, false, th);
            } else {
                /* Subtle depth dimming on non-focused avatars for crisp visual hierarchy */
                unsigned depth_tint = (!focused && !slot_hov) ? 4U : 0U;
                draw_circular_avatar_box_alpha(fb, sx, sy, cur_diam, p->avatar_id, my, my + mh,
                                               slot_alpha, box_bg, depth_tint);
            }

            /* Crisp padlock badge on locked profile ONLY when padlock system is enabled in Settings */
            if (is_prot && slot_alpha >= 16U) {
                int lx = scx + cur_diam / 2 - 4;
                int ly = scy + cur_diam / 2 - 4;
                fb_fill_aa_capsule_clipped(fb, lx - 6, ly - 7, 13, 14, box_bg, my, my + mh);
                draw_padlock_icon(fb, lx, ly, RGB565(251, 191, 36), box_bg, true, my, my + mh);
            }
        } else {
            bool full = (pcount >= NL_MAX_PROFILES || nl_avatar_first_free(state) < 0);
            int rot_fp = focused ? state->anim.qp_plus_focus_fp : 0;
            draw_plus_badge_alpha(fb, scx, scy, cur_diam, rot_fp, slot_alpha, full, focused, th, my, my + mh);
        }
    }

    /* #R22 Purpose-Built Closing Organic Liquid Propagation (NOT a blunt rewind of opening):
     * Sweeps gracefully from the focused avatar back toward the top-left profile badge (17, 12),
     * gathering with reversed tonal progression (3->2->1->0) and rounding cleanly as it settles. */
    if (!state->anim.qp_opening && raw_fp > 4 && raw_fp < 252 && plus_other_a >= 8U) {
        int close_p = 256 - raw_fp; /* 0..256 as selector closes */
        int c_inv = 256 - close_p;
        int c_ease = 256 - ((c_inv * c_inv) >> 8);
        int ccx = glow_cx + (((17 - glow_cx) * c_ease + 128) >> 8);
        int ccy = glow_cy + (((14 - glow_cy) * c_ease + 128) >> 8);
        /* Starts as an organic cloud spanning the receding dock and gathers smoothly into the badge */
        static const uint8_t k_cdel[4] = { 0, 26, 52, 78 };
        for (int w = 0; w < 4; w++) {
            int dp = close_p - (int)k_cdel[w];
            if (dp <= 0) continue;
            int span = 256 - (int)k_cdel[w];
            int wt = (dp >= span) ? 256 : ((dp * 256) / span);
            int winv = 256 - wt;
            int we = 256 - ((winv * winv * winv) >> 16);
            /* Contracts smoothly from organic wide mass (78..108 px) down into rounded 10 px pulse at badge */
            int r_max_w = 108 - w * 10;
            int r_px = r_max_w - (((r_max_w - 10) * we + 128) >> 8);
            /* Irregular at start of closing motion, progressively rounding as it settles into badge */
            int rem = 256 - we;
            int irreg = 24 + ((((rem * rem) >> 8) * 232) >> 8);
            unsigned ca = (wt < 48)
                ? (unsigned)((wt * 16) / 48)
                : (unsigned)(((256 - wt) * 16) / 208);
            ca = (ca * plus_other_a) >> 5;
            if (ca > 1U && r_px >= 4) {
                uint16_t col = nl_liquid_theme_color(state->display_mode, th, 3 - w);
                uint16_t rim = (state->display_mode == NL_DISPLAY_LIGHT)
                    ? nl_rgb565_lerp(col, th->accent_secondary, 10U)
                    : nl_rgb565_lerp(col, RGB565(255, 255, 255), 11U);
                draw_organic_liquid_blob(
                    fb, ccx, ccy, r_px, irreg,
                    0x6611U + (uint32_t)w * 43U, ((wt * 26) >> 8) + w * 19,
                    col, rim, ca,
                    0, 0, NL_SCREEN_W, NL_SCREEN_H
                );
            }
        }
    }

    if (state->anim.prof_birth_active) {
        render_profile_birth_transition(state, fb, th, birth_slot_cx, cy);
    }
}

/* R16/R22: Compact Floating Avatar Picker Modal with Elastic Bloom + Organic Liquid Continuation */
static void render_qp_avatar_picker_overlay(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    bool lib_bg_pure,
    uint64_t lib_sig
) {
    int raw_fp = state->anim.qp_pick_fp;
    if (raw_fp <= 0 && !state->sui.qp_pick_open) return;
    if (raw_fp <= 0) raw_fp = 256;
    if (raw_fp > 256) raw_fp = 256;

    int inv = 256 - raw_fp;
    int e_fp = 256 - ((inv * inv * inv) >> 16);
    int elastic_fp = e_fp;
    if (state->sui.qp_pick_open && raw_fp > 96 && raw_fp < 256) {
        int u = raw_fp - 96;
        elastic_fp = e_fp + ((u * (160 - u)) >> 9);
        if (elastic_fp > 264) elastic_fp = 264;
    }
    unsigned alpha_32 = (unsigned)((e_fp + 4) >> 3);
    if (alpha_32 > 32U) alpha_32 = 32U;

    /* Dim Library smoothly behind the compact modal */
    unsigned dim_a = (unsigned)((e_fp * 17) >> 8);
    uint16_t dim_col = th->is_light ? RGB565(178, 188, 204) : RGB565(5, 7, 12);
    apply_overlay_dim_cached(fb, dim_col, dim_a, lib_bg_pure && raw_fp >= 256, lib_sig);

    /* Compact floating modal: mx=14, my=32, mw=292, mh=176 */
    int dy = ((256 - elastic_fp) * 10) >> 8;
    int inset_x = ((256 - elastic_fp) * 12) >> 8;
    int inset_y = ((256 - elastic_fp) * 7) >> 8;
    if (inset_x < -1) inset_x = -1;
    if (inset_y < -1) inset_y = -1;
    int mx = 14 + inset_x;
    int my = 32 + dy + inset_y;
    int mw = 292 - inset_x * 2;
    int mh = 176 - inset_y * 2;

    uint16_t box_bg = th->is_light ? RGB565(250, 252, 255) : th->card_bg;
    fb_fill_rounded_rect_clipped(fb, mx - 1, my - 1, mw + 2, mh + 2, 11, th->border_subtle, 0, NL_SCREEN_H);
    fb_fill_rounded_rect_clipped(fb, mx, my, mw, mh, 10, box_bg, 0, NL_SCREEN_H);

    /* #R22 Subtle organic liquid continuation inside the compact Avatar Picker card (behind avatars!) */
    if (raw_fp > 8 && raw_fp < 248) {
        int lcx = (state->anim.prof_plus_origin_x > 20 && state->anim.prof_plus_origin_x < NL_SCREEN_W - 20)
            ? state->anim.prof_plus_origin_x : 160;
        int lcy = (state->anim.prof_plus_origin_y > 40 && state->anim.prof_plus_origin_y < NL_SCREEN_H - 40)
            ? state->anim.prof_plus_origin_y : 120;
        if (state->sui.qp_pick_open) {
            int cx_m = lcx + (((160 - lcx) * e_fp + 128) >> 8);
            int cy_m = lcy + (((120 - lcy) * e_fp + 128) >> 8);
            draw_organic_liquid_waves_4(
                fb, state->display_mode, th,
                cx_m, cy_m, raw_fp,
                16, 178, 12U,
                0x7709U,
                false, true,
                mx + 2, my + 2, mx + mw - 2, my + mh - 2
            );
        } else {
            int cp = 256 - raw_fp;
            draw_organic_liquid_waves_4(
                fb, state->display_mode, th,
                160, 120, cp,
                14, 150, 11U,
                0x7741U,
                true, true,
                mx + 2, my + 2, mx + mw - 2, my + mh - 2
            );
        }
    }

    int cat = state->sui.qp_pick_cat;
    if (cat < 0 || cat >= NL_AVATAR_CAT_COUNT) cat = 0;

    bool qp_m = (nl_mouse_is_active(state) && state->sui.hover_valid);
    for (int c = 0; c < NL_AVATAR_CAT_COUNT; c++) {
        int tx = 22 + c * 69;
        int ty = 40 + dy;
        bool csel = (c == cat);
        bool chov = (qp_m && state->sui.hover_kind == NL_HOVER_QP_PICK_TAB && state->sui.hover_index == c);
        uint16_t tbg = csel ? th->accent_primary : th->pill_bg;
        uint16_t tfg = csel ? th->text_on_accent : th->text_secondary;
        fb_fill_rounded_rect_clipped(fb, tx, ty, 66, 15, 4, tbg, my, my + mh);
        if (chov && !csel) {
            fb_draw_rounded_outline_clipped(fb, tx, ty, 66, 15, 4, th->accent_secondary, my, my + mh);
        }
        const char *lbl = nl_avatar_category_label(c);
        int lw = (int)strlen(lbl) * 6;
        fb_draw_text_clipped(fb, tx + (66 - lw) / 2, ty + 4, lbl, tfg, 10, my, my + mh);
    }

    int cstart = 0, ccnt = 0;
    nl_avatar_category_range(cat, &cstart, &ccnt);
    int cols = (cat == 0) ? 8 : 5;
    int diam = (cat == 0) ? 28 : 34;
    int dx_step = (cat == 0) ? 34 : 48;
    int dy_step = (cat == 0) ? 46 : 50;
    int grid_w = (cols - 1) * dx_step + diam;
    int start_x = (NL_SCREEN_W - grid_w) / 2;
    int start_y = ((cat == 0) ? 74 : 70) + dy;

    int slide_dx = 0;
    if (state->anim.prof_cat_trans_fp < 256) {
        int cinv = 256 - state->anim.prof_cat_trans_fp;
        int ce = 256 - ((cinv * cinv * cinv) >> 16);
        int dir = (state->anim.prof_cat_dir >= 0) ? 1 : -1;
        slide_dx = dir * (((256 - ce) * 20 + 128) >> 8);
    }

    int clip_top = my + 24;
    int clip_bot = my + mh - 6;

    /* Draw multicolor luminous halo behind focused avatar */
    {
        int cur = state->sui.qp_pick_cursor;
        if (cur < 0) cur = 0;
        if (cur >= ccnt) cur = ccnt - 1;
        int cr = cur / cols;
        int cc = cur % cols;
        int fcx = start_x + cc * dx_step + slide_dx + diam / 2;
        int fcy = start_y + cr * dy_step + diam / 2;
        draw_multicolor_focus_ring(fb, fcx, fcy, diam + 4, 2,
                                   state->anim.qp_glow_phase, th->is_light, alpha_32,
                                   clip_top, clip_bot);
    }

    for (int i = 0; i < ccnt; i++) {
        int r = i / cols;
        int cc = i % cols;
        int ax = start_x + cc * dx_step + slide_dx;
        int ay = start_y + r * dy_step;
        int av_id = cstart + i;
        bool used = (nl_avatar_find_owner(state, av_id, -1) >= 0);
        bool sel = (i == state->sui.qp_pick_cursor);
        bool hov = (qp_m && state->sui.hover_kind == NL_HOVER_QP_PICK_CELL && state->sui.hover_index == i);

        int draw_d = sel ? (diam + 2) : diam;
        int draw_x = ax - (draw_d - diam) / 2;
        int draw_y = ay - (draw_d - diam) / 2;

        if (!sel && hov) {
            fb_fill_aa_capsule_clipped(fb, draw_x - 1, draw_y - 1, draw_d + 2, draw_d + 2,
                                       th->accent_secondary, clip_top, clip_bot);
        }
        if (alpha_32 >= 30U && !used) {
            draw_circular_avatar_box(fb, draw_x, draw_y, draw_d, av_id, clip_top, clip_bot, false, th);
        } else {
            unsigned a = used ? ((alpha_32 * 11U) >> 5) : alpha_32;
            draw_circular_avatar_box_alpha(fb, draw_x, draw_y, draw_d, av_id,
                                           clip_top, clip_bot, a, box_bg, used ? 12U : 0U);
        }
    }
}

/* #R23 Immediate, Non-blocking Return-from-Game Animation Overlay:
 * Starts on Frame 1 the instant nl_exec() returns, running strictly inside the 60 FPS main loop
 * over the ALREADY restored & 100% interactive library, honoring state->settings.return_anim_mode. */
static void render_return_drop_overlay(const nl_state_t *state, uint16_t *fb, const nl_theme_t *th) {
    if (!state || !fb || !th || !state->anim.return_drop_active || state->anim.return_drop_max <= 0) {
        return;
    }
    nl_return_anim_t rmode = state->settings.return_anim_mode;
    if (rmode == NL_RETURN_ANIM_INSTANT) return;

    int tick = state->anim.return_drop_tick;
    int max_t = state->anim.return_drop_max;
    if (tick < 0) tick = 0;
    if (tick >= max_t) return;
    int p = (tick * 256) / max_t;

    /* Locate returned game card center & size on screen (fallback: center of library viewport) */
    int fcx = 160;
    int fcy = 118;
    int card_w = 84;
    int card_h = 84;
    int card_rad = 11;
    if (state->filtered_count > 0) {
        int fpos = -1;
        if (state->anim.return_drop_game_idx >= 0) {
            for (int i = 0; i < state->filtered_count; i++) {
                if (state->filtered_indices[i] == state->anim.return_drop_game_idx) {
                    fpos = i;
                    break;
                }
            }
        }
        if (fpos < 0 && state->selected_pos >= 0 && state->selected_pos < state->filtered_count) {
            fpos = state->selected_pos;
        }
        if (fpos >= 0) {
            nl_layout_metrics_t m;
            get_interpolated_layout_metrics(state, &m);
            card_w = m.card_w;
            card_h = m.card_h;
            card_rad = m.radius;
            int sx = 0, sy = 0;
            nl_compute_grid_slot_pos(&m, state->filtered_count, fpos, &sx, &sy);
            sx -= (state->anim.scroll_x_fp >> 4);
            sy -= (state->anim.scroll_y_fp >> 4);
            int cx_cand = sx + m.card_w / 2;
            int cy_cand = sy + m.card_h / 2;
            if (cx_cand >= 24 && cx_cand <= NL_SCREEN_W - 24 &&
                cy_cand >= NL_TOP_BAR_H + 12 && cy_cand <= NL_SCREEN_H - 18) {
                fcx = cx_cand;
                fcy = cy_cand;
            }
        }
    }

    if (rmode == NL_RETURN_ANIM_FADE) {
        int inv = 256 - p;
        unsigned alpha = (unsigned)((((inv * inv) >> 8) * 28) >> 8);
        if (alpha == 0U) return;
        uint16_t veil = th->bg_main;
        for (int i = 0; i < NL_FB_PIXELS; i++) {
            uint16_t px = fb[i];
            if (px != veil) fb[i] = nl_rgb565_lerp_fast(px, veil, alpha);
        }
        return;
    }

    if (rmode == NL_RETURN_ANIM_ZOOM) {
        int inv = 256 - p;
        int ease = 256 - ((inv * inv * inv) >> 16);
        int rem = 256 - ease;
        /* Fast settling aperture from full-screen rect shrinking smoothly onto the returned game card */
        int cur_w = card_w + (((NL_SCREEN_W - card_w) * rem + 128) >> 8);
        int cur_h = card_h + (((NL_SCREEN_H - card_h) * rem + 128) >> 8);
        int rx0 = fcx - cur_w / 2;
        int ry0 = fcy - cur_h / 2;
        int rx1 = rx0 + cur_w;
        int ry1 = ry0 + cur_h;
        unsigned veil_a = (unsigned)((rem * 18) >> 8);
        if (veil_a > 0U) {
            uint16_t veil = th->bg_main;
            for (int y = 0; y < NL_SCREEN_H; y++) {
                uint16_t *row = fb + y * NL_SCREEN_W;
                if (y < ry0 || y >= ry1) {
                    for (int x = 0; x < NL_SCREEN_W; x++) {
                        row[x] = nl_rgb565_lerp_fast(row[x], veil, veil_a);
                    }
                } else {
                    int lx = (rx0 > 0) ? rx0 : 0;
                    int rx = (rx1 < NL_SCREEN_W) ? rx1 : NL_SCREEN_W;
                    for (int x = 0; x < lx; x++) row[x] = nl_rgb565_lerp_fast(row[x], veil, veil_a);
                    for (int x = rx; x < NL_SCREEN_W; x++) row[x] = nl_rgb565_lerp_fast(row[x], veil, veil_a);
                }
            }
        }
        int rad = (card_rad * ease) >> 8;
        if (rad < 2) rad = 2;
        uint16_t rim = nl_rgb565_lerp(th->accent_secondary, th->accent_primary, (unsigned)(ease >> 3));
        fb_draw_rounded_outline_clipped(fb, rx0, ry0, cur_w, cur_h, rad, rim, 0, NL_SCREEN_H);
        if (cur_w > 4 && cur_h > 4) {
            fb_draw_rounded_outline_clipped(fb, rx0 + 1, ry0 + 1, cur_w - 2, cur_h - 2,
                                            (rad > 1) ? (rad - 1) : 1, th->accent_secondary, 0, NL_SCREEN_H);
        }
        return;
    }

    uint32_t ret_seed = 0x8813U + (uint32_t)((state->anim.return_drop_game_idx >= 0) ? state->anim.return_drop_game_idx * 17 : 7);
    draw_organic_liquid_reveal_4(fb, state->display_mode, th, fcx, fcy, p, ret_seed);
}

void nl_render_frame(const nl_state_t *state, uint16_t *fb) {
    if (!state || !fb) return;
    G_LAUNCH_BG_VALID = false;
    const nl_theme_t *th = nl_get_theme(state->display_mode, state->accent_color);

    bool is_managed = (state->sui.screen == state->anim.set_last_screen);

    if (state->sui.screen != NL_UI_LIBRARY) {
        int eff_open_fp = (is_managed && state->anim.set_open_fp > 0) ? state->anim.set_open_fp : 256;
        if (eff_open_fp < 256) {
            uint16_t *set_buf = renderer_get_scratch_a();
            if (set_buf) {
                render_library_frame_cached(state, fb, th, NULL, true);
                render_settings_single_screen(state, state->sui.screen, true, set_buf, th);
                composite_settings_open_close(fb, set_buf, eff_open_fp, th);
                int rot256 = (settings_open_ease256(eff_open_fp) * 96) >> 8;
                draw_gear_button_ex(fb, NL_GEAR_BTN_X, NL_GEAR_BTN_Y, NL_GEAR_BTN_D, th, true, rot256);
            } else {
                render_settings_single_screen(state, state->sui.screen, true, fb, th);
            }
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

    bool set_closing = (is_managed && state->anim.set_open_fp > 0 && state->anim.set_prev_screen != NL_UI_LIBRARY);
    bool show_qp     = (state->sui.qp_open || state->anim.qp_open_fp > 0);
    bool show_pick   = (state->sui.qp_pick_open || state->anim.qp_pick_fp > 0);
    bool qp_settled_dim   = (!set_closing && show_qp && !show_pick &&
                             (state->anim.qp_open_fp <= 0 || state->anim.qp_open_fp >= 256));
    bool pick_settled_dim = (!set_closing && !show_qp && !state->anim.prof_plus_active && show_pick &&
                             (state->anim.qp_pick_fp <= 0 || state->anim.qp_pick_fp >= 256));

    /* Render Library (cached when static; skip redundant copy to fb if a settled dimmed overlay immediately overwrites fb) */
    uint64_t lib_sig = 0ULL;
    bool copy_lib_to_fb = !(qp_settled_dim || pick_settled_dim);
    render_library_frame_cached(state, fb, th, &lib_sig, copy_lib_to_fb);

    /* Close Settings: reverse organic liquid-drop contraction + reverse gear rotation */
    if (set_closing) {
        uint16_t *set_buf = renderer_get_scratch_a();
        if (set_buf) {
            render_settings_single_screen(state, state->anim.set_prev_screen, true, set_buf, th);
            composite_settings_open_close(fb, set_buf, state->anim.set_open_fp, th);
        }
        int rot256 = (settings_open_ease256(state->anim.set_open_fp) * 96) >> 8;
        draw_gear_button_ex(fb, NL_GEAR_BTN_X, NL_GEAR_BTN_Y, NL_GEAR_BTN_D, th, state->anim.set_open_fp > 32, rot256);
    }

    /* Minimalist Quick Profile Selector modal overlay (Library) */
    if (show_qp) {
        render_quick_profile_overlay(state, fb, th, !set_closing, lib_sig);
    }

    /* Controlled '+' button glow & dissolve transition on Library */
    if (state->anim.prof_plus_active) {
        render_profile_plus_transition(state, fb, th);
    }

    /* Compact Floating Avatar Picker modal over dimmed Library */
    if (show_pick) {
        render_qp_avatar_picker_overlay(state, fb, th, (!set_closing && !show_qp && !state->anim.prof_plus_active), lib_sig);
    }

    /* 4-Stage Profile Switch Transition over the newly loaded Library */
    if (state->anim.prof_switch_active) {
        render_profile_switch_transition(state, fb, th);
    }

    /* #R21 Non-blocking Return-from-Game Droplet Reveal Overlay over the live Library */
    if (state->anim.return_drop_active && !set_closing && !show_qp && !show_pick) {
        render_return_drop_overlay(state, fb, th);
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

    /* Fast 32-bit solid background fill (pixel-identical to fb_fill_rect_clipped, 2x faster on ARMv5TE) */
    {
        uint32_t pair = (uint32_t)th->bg_main | ((uint32_t)th->bg_main << 16);
        uint32_t *dst32 = (uint32_t *)fb;
        int n32 = NL_FB_PIXELS >> 1;
        for (int i = 0; i < n32; i++) dst32[i] = pair;
    }

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
    if (!fb || !lib_fb || max_steps <= 0) return;
    nl_app_open_anim_t open_mode = state ? state->settings.app_open_anim : NL_OPEN_ANIM_CURRENT;
    if (open_mode == NL_OPEN_ANIM_INSTANT || step >= max_steps) {
        memcpy(fb, lib_fb, NL_FB_BYTES);
        return;
    }

    if (open_mode == NL_OPEN_ANIM_LIQUID) {
        const nl_theme_t *th = state
            ? nl_get_theme(state->display_mode, state->accent_color)
            : nl_get_theme(NL_DISPLAY_DARK, NL_ACCENT_BLUE);
        nl_display_mode_t dm = state ? state->display_mode : NL_DISPLAY_DARK;
        memcpy(fb, lib_fb, NL_FB_BYTES);
        int p256 = (step * 256) / max_steps;
        if (p256 < 0) p256 = 0;
        if (p256 > 255) p256 = 255;
        draw_organic_liquid_reveal_4(fb, dm, th, NL_SCREEN_W / 2, NL_SCREEN_H / 2, p256, 0x51A7U);
        return;
    }

    if (open_mode == NL_OPEN_ANIM_ZOOM) {
        const nl_theme_t *th = state
            ? nl_get_theme(state->display_mode, state->accent_color)
            : nl_get_theme(NL_DISPLAY_DARK, NL_ACCENT_BLUE);
        int p256 = (step * 256) / max_steps;
        if (p256 < 0) p256 = 0;
        if (p256 > 256) p256 = 256;
        int inv = 256 - p256;
        int ease = 256 - ((inv * inv * inv) >> 16);
        int scale_fp = 196 + ((60 * ease) >> 8); /* 0.76x -> 1.00x smooth zoom-in */
        if (scale_fp < 128) scale_fp = 128;
        int inv_scale_fp = (256 * 256) / scale_fp;
        unsigned alpha = (unsigned)((ease * 32) >> 8);
        uint16_t bg = th->bg_main;
        for (int y = 0; y < NL_SCREEN_H; y++) {
            int sy = NL_SCREEN_H / 2 + (((y - NL_SCREEN_H / 2) * inv_scale_fp) >> 8);
            uint16_t *dst = fb + y * NL_SCREEN_W;
            if (sy < 0 || sy >= NL_SCREEN_H) {
                for (int x = 0; x < NL_SCREEN_W; x++) dst[x] = bg;
                continue;
            }
            const uint16_t *src = lib_fb + sy * NL_SCREEN_W;
            for (int x = 0; x < NL_SCREEN_W; x++) {
                int sx = NL_SCREEN_W / 2 + (((x - NL_SCREEN_W / 2) * inv_scale_fp) >> 8);
                if (sx < 0 || sx >= NL_SCREEN_W) {
                    dst[x] = bg;
                } else {
                    dst[x] = nl_rgb565_lerp_fast(bg, src[sx], alpha);
                }
            }
        }
        return;
    }

    /* NL_OPEN_ANIM_CURRENT: exact original smooth splash-to-library crossfade */
    if (!last_splash_fb) {
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

/* #R23 Fast, theme-adapted App Close Animation (4 independent choices; <= 200 ms, 0 frames for Instant) */
void nl_render_app_close_anim(
    const nl_state_t *state,
    uint16_t *fb,
    const uint16_t *base_fb,
    int step,
    int max_steps
) {
    if (!fb || max_steps <= 0) return;
    const nl_theme_t *th = state
        ? nl_get_theme(state->display_mode, state->accent_color)
        : nl_get_theme(NL_DISPLAY_DARK, NL_ACCENT_BLUE);
    nl_display_mode_t dm = state ? state->display_mode : NL_DISPLAY_DARK;
    nl_app_close_anim_t close_mode = state ? state->settings.app_close_anim : NL_CLOSE_ANIM_LIQUID;

    if (close_mode == NL_CLOSE_ANIM_INSTANT) {
        uint32_t pair = (uint32_t)th->bg_main | ((uint32_t)th->bg_main << 16);
        uint32_t *dst32 = (uint32_t *)fb;
        int n32 = NL_FB_PIXELS >> 1;
        for (int i = 0; i < n32; i++) dst32[i] = pair;
        return;
    }

    int p256 = (step * 256) / max_steps;
    if (p256 < 0) p256 = 0;
    if (p256 > 256) p256 = 256;

    if (close_mode == NL_CLOSE_ANIM_FADE) {
        unsigned alpha = (unsigned)((p256 * 32) >> 8);
        uint16_t bg = th->bg_main;
        if (!base_fb || alpha >= 32U) {
            uint32_t pair = (uint32_t)bg | ((uint32_t)bg << 16);
            uint32_t *dst32 = (uint32_t *)fb;
            int n32 = NL_FB_PIXELS >> 1;
            for (int i = 0; i < n32; i++) dst32[i] = pair;
            return;
        }
        for (int i = 0; i < NL_FB_PIXELS; i++) {
            uint16_t px = base_fb[i];
            fb[i] = (px == bg) ? bg : nl_rgb565_lerp_fast(px, bg, alpha);
        }
        return;
    }

    if (close_mode == NL_CLOSE_ANIM_ZOOM) {
        int ease = (p256 * p256) >> 8; /* Crisp ease-in contraction */
        int scale_fp = 256 - ((92 * ease) >> 8); /* 1.00x -> 0.64x */
        if (scale_fp < 96) scale_fp = 96;
        int inv_scale_fp = (256 * 256) / scale_fp;
        unsigned fade_a = (unsigned)((ease * 32) >> 8);
        uint16_t bg = th->bg_main;
        if (!base_fb || fade_a >= 32U) {
            uint32_t pair = (uint32_t)bg | ((uint32_t)bg << 16);
            uint32_t *dst32 = (uint32_t *)fb;
            int n32 = NL_FB_PIXELS >> 1;
            for (int i = 0; i < n32; i++) dst32[i] = pair;
            return;
        }
        for (int y = 0; y < NL_SCREEN_H; y++) {
            int sy = NL_SCREEN_H / 2 + (((y - NL_SCREEN_H / 2) * inv_scale_fp) >> 8);
            uint16_t *dst = fb + y * NL_SCREEN_W;
            if (sy < 0 || sy >= NL_SCREEN_H) {
                for (int x = 0; x < NL_SCREEN_W; x++) dst[x] = bg;
                continue;
            }
            const uint16_t *src = base_fb + sy * NL_SCREEN_W;
            for (int x = 0; x < NL_SCREEN_W; x++) {
                int sx = NL_SCREEN_W / 2 + (((x - NL_SCREEN_W / 2) * inv_scale_fp) >> 8);
                if (sx < 0 || sx >= NL_SCREEN_W) {
                    dst[x] = bg;
                } else {
                    dst[x] = nl_rgb565_lerp_fast(src[sx], bg, fade_a);
                }
            }
        }
        return;
    }

    /* NL_CLOSE_ANIM_LIQUID: 4 successive organic liquid propagations sweeping cleanly across the screen */
    if (base_fb && base_fb != fb) {
        memcpy(fb, base_fb, NL_FB_BYTES);
    }
    draw_organic_liquid_waves_4(
        fb, dm, th,
        NL_SCREEN_W / 2, NL_SCREEN_H / 2,
        p256, 12, 240, 32U, 0x9C31U,
        false, false,
        0, 0, NL_SCREEN_W, NL_SCREEN_H
    );
}

/* build_organic_ink_field uses shared LUT & integer geometry defined near top of renderer.c */

static bool build_organic_ink_field(uint32_t seed) {
    if (!G_INK_FIELD) {
        G_INK_FIELD = (uint8_t *)malloc(NL_FB_PIXELS);
        if (!G_INK_FIELD) {
            return false;
        }
    }
    init_ink_sin_lut();

    uint8_t s0 = (uint8_t)((seed * 37U + 13U) & 255U);
    uint8_t s1 = (uint8_t)((seed * 73U + 41U) & 255U);
    uint8_t s2 = (uint8_t)((seed * 113U + 97U) & 255U);
    uint8_t s3 = (uint8_t)((seed * 157U + 19U) & 255U);

    int16_t dir_scale[256];
    for (int th = 0; th < 256; th++) {
        int m1 = G_INK_SIN256[(th + s0) & 255];
        int m2 = G_INK_SIN256[((th * 2) + s1) & 255];
        int m3 = G_INK_SIN256[((th * 3) + s2) & 255];
        int m5 = G_INK_SIN256[((th * 5) + s3) & 255];
        int bias = (m1 * 10 + m2 * 13 + m3 * 12 + m5 * 7) >> 7;
        dir_scale[th] = (int16_t)(256 + bias);
    }

    int max_v = 1;
    for (int uy = 0; uy < 120; uy++) {
        int y_lo = 120 + uy;
        int y_hi = 119 - uy;
        int wy_lo = (G_INK_SIN256[((y_lo * 2) + 64 + s2) & 255] * 4) >> 7;
        int wy_hi = (G_INK_SIN256[((y_hi * 2) + 64 + s2) & 255] * 4) >> 7;
        uint8_t *row_lo = G_INK_FIELD + y_lo * NL_SCREEN_W;
        uint8_t *row_hi = G_INK_FIELD + y_hi * NL_SCREEN_W;
        int uy2 = uy * uy;

        for (int ux = 0; ux < 160; ux++) {
            int r = ink_isqrt32(ux * ux + uy2);
            int a0 = ink_octant_64(ux, uy);
            int ramp = (r < 56) ? ((r * 256) / 56) : 256;

            int x_r = 160 + ux;
            int x_l = 159 - ux;
            int wx_r = (G_INK_SIN256[((x_r * 2) + s1) & 255] * 4) >> 7;
            int wx_l = (G_INK_SIN256[((x_l * 2) + s1) & 255] * 4) >> 7;

            {
                int th = a0 & 255;
                int wd = (G_INK_SIN256[(((x_r + y_lo) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx_r + wy_lo + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_lo[x_r] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
            {
                int th = (128 - a0) & 255;
                int wd = (G_INK_SIN256[(((x_l + y_lo) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx_l + wy_lo + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_lo[x_l] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
            {
                int th = (128 + a0) & 255;
                int wd = (G_INK_SIN256[(((x_l + y_hi) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx_l + wy_hi + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_hi[x_l] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
            {
                int th = (256 - a0) & 255;
                int wd = (G_INK_SIN256[(((x_r + y_hi) * 2) + s3) & 255] * 3) >> 7;
                int d = ((r * dir_scale[th]) >> 8) + ((ramp * (wx_r + wy_hi + wd)) >> 8);
                if (d < 0) d = 0;
                if (d > 240) d = 240;
                row_hi[x_r] = (uint8_t)d;
                if (d > max_v) max_v = d;
            }
        }
    }

    G_INK_CACHED_SEED = seed;
    G_INK_MAX_VAL = max_v;
    return true;
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

    if (G_INK_CACHED_SEED != seed || !G_INK_FIELD) {
        if (!build_organic_ink_field(seed)) {
            memcpy(fb, new_fb, NL_FB_BYTES);
            return;
        }
    }

    static const uint8_t k_ink_alpha_lut[15] = {
        0, 2, 5, 7, 9, 11, 14, 16, 18, 21, 23, 25, 27, 30, 32
    };
    const int edge_w = 14;
    int t256 = (step * 256) / max_steps;
    if (t256 < 0) t256 = 0;
    if (t256 > 256) t256 = 256;
    int inv = 256 - t256;
    int cubic = 256 - ((inv * inv * inv) >> 16);
    int ease256 = (t256 * 77 + cubic * 179 + 128) >> 8;

    int front = ((G_INK_MAX_VAL + edge_w + 2) * ease256) >> 8;
    const uint8_t *field = G_INK_FIELD;

    for (int i = 0; i < NL_FB_PIXELS; i++) {
        int delta = front - (int)field[i];
        if (delta >= edge_w) {
            fb[i] = new_fb[i];
        } else if (delta <= 0) {
            fb[i] = old_fb[i];
        } else {
            uint16_t o = old_fb[i];
            uint16_t n = new_fb[i];
            fb[i] = (o == n) ? n : nl_rgb565_lerp(o, n, (unsigned)k_ink_alpha_lut[delta]);
        }
    }
}

void nl_render_splash(const nl_state_t *state, uint16_t *fb, int step, int max_steps) {
    if (!fb || max_steps <= 0) return;
    uint32_t elapsed = (step > 0) ? (uint32_t)((step * 900) / max_steps) : 0U;
    nl_render_splash_anim(state, fb, elapsed, true);
}

/* Render a single soft diffused concentric water-drop ripple centered at (cx, cy).
 * Optimized for ARM926EJ-S (zero per-pixel isqrt, exact scanline ring spans, foreground card occlusion):
 * computes only 2 isqrt calls per scanline and skips pixels occluded by the foreground card interior. */
static void draw_launch_concentric_ripple(
    uint16_t *fb,
    int cx,
    int cy,
    int r_16,         /* Ring crest radius in 1/16 px */
    int half_w_16,    /* Soft halo half-width in 1/16 px */
    unsigned alpha_32,/* Peak crest opacity (0..32) */
    uint16_t halo_col,
    uint16_t crest_col,
    int occl_x0,
    int occl_y0,
    int occl_x1,
    int occl_y1
) {
    if (!fb || alpha_32 == 0U || half_w_16 <= 0) return;
    if (alpha_32 > 32U) alpha_32 = 32U;
    if (r_16 < 64) r_16 = 64;

    int r_outer_px = (r_16 + half_w_16 + 15) >> 4;
    int r_inner_px = (r_16 > half_w_16) ? ((r_16 - half_w_16) >> 4) : 0;
    if (r_outer_px <= 0) return;

    int r_outer2 = r_outer_px * r_outer_px;
    int r_inner2 = r_inner_px * r_inner_px;

    /* Strict 4px safety margin from all 4 screen borders so ripples never touch screen edges */
    const int min_margin = 4;
    int y0 = (cy - r_outer_px < min_margin) ? min_margin : (cy - r_outer_px);
    int y1 = (cy + r_outer_px + 1 > NL_SCREEN_H - min_margin) ? (NL_SCREEN_H - min_margin) : (cy + r_outer_px + 1);
    int clip_x0 = min_margin;
    int clip_x1 = NL_SCREEN_W - min_margin;

    const uint16_t white_col = RGB565(255, 255, 255);
    const uint16_t crest_pearl = nl_rgb565_lerp(crest_col, white_col, 15U);
    const bool same_col = (halo_col == crest_pearl);
    const int inv_hw = 65536 / half_w_16;
    const int32_t r16_sq = (int32_t)r_16 * (int32_t)r_16;
    const uint32_t inv_2r_20 = (1U << 20) / (uint32_t)(2 * r_16);
    const bool has_occl = (occl_x0 < occl_x1 && occl_y0 < occl_y1);

    for (int py = y0; py < y1; py++) {
        int dy = py - cy;
        int dy2 = dy * dy;
        if (dy2 >= r_outer2) continue;

        int outer_span = ink_isqrt32(r_outer2 - dy2);
        int inner_span = (dy2 < r_inner2) ? ink_isqrt32(r_inner2 - dy2) : 0;
        int edge_y = (py < 12) ? (py - min_margin) : ((py >= NL_SCREEN_H - 12) ? (NL_SCREEN_H - min_margin - 1 - py) : 8);
        bool row_occl = (has_occl && py >= occl_y0 && py < occl_y1);
        uint16_t *row = fb + py * NL_SCREEN_W;

        /* Two horizontal spans: left [cx - outer_span .. cx - inner_span] and right [cx + inner_span .. cx + outer_span] */
        int spans[2][2];
        int n_spans;
        if (inner_span <= 0) {
            spans[0][0] = cx - outer_span;
            spans[0][1] = cx + outer_span + 1;
            n_spans = 1;
        } else {
            spans[0][0] = cx - outer_span;
            spans[0][1] = cx - inner_span + 1;
            spans[1][0] = cx + inner_span;
            spans[1][1] = cx + outer_span + 1;
            n_spans = 2;
        }

        for (int s = 0; s < n_spans; s++) {
            int sx0 = spans[s][0];
            int sx1 = spans[s][1];
            if (sx0 < clip_x0) sx0 = clip_x0;
            if (sx1 > clip_x1) sx1 = clip_x1;
            if (sx0 >= sx1) continue;

            for (int px = sx0; px < sx1; px++) {
                if (row_occl && px >= occl_x0 && px < occl_x1) {
                    px = occl_x1 - 1;
                    continue;
                }
                int dx = px - cx;
                int d2 = dx * dx + dy2;
                if (d2 >= r_outer2 || d2 < r_inner2) continue;

                int32_t d2_diff = ((int32_t)d2 << 8) - r16_sq;
                uint32_t abs_d2 = (uint32_t)(d2_diff < 0 ? -d2_diff : d2_diff);
                int delta = (int)((abs_d2 * inv_2r_20) >> 20);
                if (delta >= half_w_16) continue;

                int u = ((half_w_16 - delta) * inv_hw) >> 8; /* 0..256 */
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
                if (!same_col && u > 136) {
                    unsigned t_crest = (unsigned)(((u - 136) * 218) >> 10);
                    if (t_crest > 32U) t_crest = 32U;
                    ring_col = nl_rgb565_lerp(halo_col, crest_pearl, t_crest);
                }
                row[px] = nl_rgb565_lerp(row[px], ring_col, a);
            }
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
 *    while the card completes its smooth expansion to fullscreen (320x240).
 *
 * #R14 Performance Architecture (zero visual downgrade):
 * - Caches static library background (full cards + empty chrome) ONCE on step <= 1.
 * - Single-pass background fade + dim that skips pixels occluded by the foreground card.
 * - Scanline-spanned, zero-isqrt-per-pixel, card-occluded ripples.
 * - Separable 1D bilinear scaling for both icon expansion and Stage 4 .scr.tns crossfade. */
/* #R20 Shared Layer-1 cached library fade + dim helper (reused by all 4 launch animations) */
static void launch_render_bg_layer(
    const nl_state_t *state,
    uint16_t *fb,
    const nl_theme_t *th,
    int game_idx,
    int step,
    int search_h,
    bool in_library,
    unsigned other_alpha_32,
    unsigned dim,
    uint16_t dim_target,
    bool has_occl,
    int occl_x0, int occl_y0, int occl_x1, int occl_y1
) {
    if (in_library) {
        uint32_t bg_sig = ((uint32_t)state->display_mode << 24)
                        ^ ((uint32_t)state->accent_color << 16)
                        ^ ((uint32_t)state->current_page << 8)
                        ^ ((uint32_t)state->settings.layout << 20)
                        ^ ((uint32_t)state->settings.scroll_dir << 27)
                        ^ (uint32_t)state->active_category
                        ^ ((uint32_t)state->anim.scroll_x_fp << 10)
                        ^ ((uint32_t)state->anim.scroll_y_fp << 4)
                        ^ (uint32_t)search_h;
        uint16_t *buf_a = renderer_get_scratch_a();
        uint16_t *buf_b = buf_a ? renderer_get_scratch_b() : NULL;
        if (buf_a && buf_b) {
            if (!G_LAUNCH_BG_VALID || step <= 1 || G_LAUNCH_BG_GAME_IDX != game_idx || G_LAUNCH_BG_SIG != bg_sig) {
                render_library_frame_ex(state, buf_a, th, game_idx, 32U);
                render_library_frame_ex(state, buf_b, th, game_idx, 0U);
                for (int py = 0; py < NL_SCREEN_H; py++) {
                    const uint16_t *ra = buf_a + py * NL_SCREEN_W;
                    const uint16_t *rb = buf_b + py * NL_SCREEN_W;
                    int x0 = NL_SCREEN_W, x1 = 0;
                    for (int px = 0; px < NL_SCREEN_W; px++) {
                        if (ra[px] != rb[px]) {
                            if (x0 == NL_SCREEN_W) x0 = px;
                            x1 = px + 1;
                        }
                    }
                    G_LAUNCH_SPAN_X0[py] = (int16_t)x0;
                    G_LAUNCH_SPAN_X1[py] = (int16_t)x1;
                }
                G_LAUNCH_BG_VALID = true;
                G_LAUNCH_BG_GAME_IDX = game_idx;
                G_LAUNCH_BG_SIG = bg_sig;
            }
            for (int py = 0; py < NL_SCREEN_H; py++) {
                uint16_t *dst = fb + py * NL_SCREEN_W;
                const uint16_t *sa = buf_a + py * NL_SCREEN_W;
                const uint16_t *sb = buf_b + py * NL_SCREEN_W;
                bool row_occl = (has_occl && py >= occl_y0 && py < occl_y1);
                int s0_end = row_occl ? occl_x0 : NL_SCREEN_W;
                int s1_start = row_occl ? occl_x1 : NL_SCREEN_W;
                int d0 = (int)G_LAUNCH_SPAN_X0[py];
                int d1 = (int)G_LAUNCH_SPAN_X1[py];

                if (other_alpha_32 == 0U || d0 >= d1) {
                    if (dim == 0U) {
                        if (s0_end > 0) memcpy(dst, sb, (size_t)s0_end * sizeof(uint16_t));
                        if (s1_start < NL_SCREEN_W) memcpy(dst + s1_start, sb + s1_start, (size_t)(NL_SCREEN_W - s1_start) * sizeof(uint16_t));
                    } else {
                        for (int px = 0; px < s0_end; px++) dst[px] = nl_rgb565_lerp(sb[px], dim_target, dim);
                        for (int px = s1_start; px < NL_SCREEN_W; px++) dst[px] = nl_rgb565_lerp(sb[px], dim_target, dim);
                    }
                } else {
                    for (int px = 0; px < s0_end; px++) {
                        uint16_t c = (px >= d0 && px < d1 && sa[px] != sb[px])
                            ? nl_rgb565_lerp(sb[px], sa[px], other_alpha_32)
                            : sb[px];
                        dst[px] = (dim > 0U) ? nl_rgb565_lerp(c, dim_target, dim) : c;
                    }
                    for (int px = s1_start; px < NL_SCREEN_W; px++) {
                        uint16_t c = (px >= d0 && px < d1 && sa[px] != sb[px])
                            ? nl_rgb565_lerp(sb[px], sa[px], other_alpha_32)
                            : sb[px];
                        dst[px] = (dim > 0U) ? nl_rgb565_lerp(c, dim_target, dim) : c;
                    }
                }
            }
        } else {
            render_library_frame_ex(state, fb, th, game_idx, other_alpha_32);
            if (dim > 0U) {
                for (int i = 0; i < NL_FB_PIXELS; i++) {
                    fb[i] = nl_rgb565_lerp(fb[i], dim_target, dim);
                }
            }
        }
    } else {
        nl_render_frame(state, fb);
        if (dim > 0U) {
            for (int i = 0; i < NL_FB_PIXELS; i++) {
                fb[i] = nl_rgb565_lerp(fb[i], dim_target, dim);
            }
        }
    }
}

/* #R22 Legacy 5-drop teardrop primitives replaced by unified 4-layer Organic Liquid Propagation engine above */

/* #R20 Compact 8-dot circular spinner centered directly on an icon (Launch Animation 3) */
static void draw_launch_center_spinner(
    uint16_t *fb,
    int cx, int cy,
    int spin_r,
    int t,
    const nl_theme_t *th
) {
    if (spin_r < 6) spin_r = 6;
    if (spin_r > 14) spin_r = 14;

    /* Subtle translucent contrast plate behind the dots for crisp legibility on any icon artwork */
    int plate_r = spin_r + 5;
    int pr2 = plate_r * plate_r;
    uint16_t plate_col = th->is_light ? RGB565(248, 250, 255) : RGB565(10, 14, 22);
    int py0 = cy - plate_r;
    int py1 = cy + plate_r;
    if (py0 < 0) py0 = 0;
    if (py1 >= NL_SCREEN_H) py1 = NL_SCREEN_H - 1;
    for (int py = py0; py <= py1; py++) {
        int dy = py - cy;
        int rem = pr2 - dy * dy;
        if (rem < 0) continue;
        int sx = ink_isqrt32(rem);
        int x0 = cx - sx;
        int x1 = cx + sx;
        if (x0 < 0) x0 = 0;
        if (x1 >= NL_SCREEN_W) x1 = NL_SCREEN_W - 1;
        uint16_t *row = fb + py * NL_SCREEN_W;
        for (int px = x0; px <= x1; px++) {
            row[px] = nl_rgb565_lerp(row[px], plate_col, 16U);
        }
    }

    /* 8 dots rotating briskly (~2.2 revolutions over launch) */
    int diag = (spin_r * 181 + 128) >> 8; /* ~r / sqrt(2) */
    const int8_t off[8][2] = {
        { (int8_t)0, (int8_t)(-spin_r) },
        { (int8_t)diag, (int8_t)(-diag) },
        { (int8_t)spin_r, (int8_t)0 },
        { (int8_t)diag, (int8_t)diag },
        { (int8_t)0, (int8_t)spin_r },
        { (int8_t)(-diag), (int8_t)diag },
        { (int8_t)(-spin_r), (int8_t)0 },
        { (int8_t)(-diag), (int8_t)(-diag) }
    };
    unsigned phase = (unsigned)(t * 18); /* 0..4608 */
    unsigned head = (phase >> 8) & 7U;
    unsigned sub  = (phase & 0xFFU) >> 3; /* 0..31 */
    uint16_t dot_quiet = nl_rgb565_lerp(plate_col, th->text_secondary, 14U);

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
        int dx = cx + (int)off[i][0] - 2;
        int dy = cy + (int)off[i][1] - 2;
        fb_fill_rounded_rect_clipped(fb, dx, dy, 4, 4, 2, dcol, 0, NL_SCREEN_H);
    }
}

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

    /* 1. Progressive smooth fade-out of all non-selected library icons (32 -> 0 over t in 0..168) */
    unsigned other_alpha_32 = 0U;
    if (t < 168) {
        int fade_idx = (t * 32) / 168;
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
    bool in_library = (state->sui.screen == NL_UI_LIBRARY && state->filtered_count > 0);

    if (in_library) {
        int slot_pos = state->selected_pos;
        if (slot_pos < 0 || slot_pos >= state->filtered_count || state->filtered_indices[slot_pos] != game_idx) {
            for (int fi = 0; fi < state->filtered_count; fi++) {
                if (state->filtered_indices[fi] == game_idx) {
                    slot_pos = fi;
                    break;
                }
            }
        }
        if (slot_pos >= 0 && slot_pos < state->filtered_count && state->filtered_indices[slot_pos] == game_idx) {
            int scroll_x = state->anim.scroll_x_fp >> 4;
            int scroll_y = state->anim.scroll_y_fp >> 4;
            int slot_x = 0, slot_y = 0;
            nl_compute_grid_slot_pos(&m, state->filtered_count, slot_pos, &slot_x, &slot_y);
            sx = slot_x - scroll_x;
            sy = slot_y + search_h - scroll_y;
        }
    }

    int card_cx = sx + m.card_w / 2;
    int card_cy = sy + m.card_h / 2;
    nl_launch_anim_t anim_mode = state->settings.launch_anim_mode;

    /* =========================================================================
     * ANIMATION 3 — SPINNER (Small rotating spinner centered directly ON icon,
     * progressively enlarging icon, STRICTLY ZERO drops & ZERO white halos)
     * ========================================================================= */
    if (anim_mode == NL_LAUNCH_ANIM_SPINNER) {
        unsigned dim = (unsigned)((t * 14) / 256);
        if (dim > 14U) dim = 14U;
        launch_render_bg_layer(state, fb, th, game_idx, step, search_h, in_library,
                               other_alpha_32, dim, dim_target, false, 0, 0, 0, 0);

        /* Progressive smooth icon enlargement (NL_SCALE_SELECTED -> 332/256, gently nudged toward center) */
        int inv = 256 - t;
        int ease = 256 - ((inv * inv * inv) >> 16);
        int cur_scale = NL_SCALE_SELECTED + (((332 - NL_SCALE_SELECTED) * ease + 128) >> 8);
        int icon_w = (m.card_w * cur_scale + 128) >> 8;
        int icon_h = (m.card_h * cur_scale + 128) >> 8;
        if (icon_w > 132) icon_w = 132;
        if (icon_h > 132) icon_h = 132;
        int cur_cx = card_cx + (((160 - card_cx) * ease) >> 10);
        int cur_cy = card_cy + (((120 - card_cy) * ease) >> 10);
        int icon_x = cur_cx - icon_w / 2;
        int icon_y = cur_cy - icon_h / 2;
        int icon_rad = (m.radius * cur_scale + 128) >> 8;
        if (icon_rad > 18) icon_rad = 18;

        draw_game_cover_card_ex(fb, icon_x, icon_y, icon_w, icon_h, icon_rad, 0, sel, th, 0, NL_SCREEN_H, false);

        /* Animated circular spinner centered directly on the enlarging icon */
        int min_d = (icon_w < icon_h) ? icon_w : icon_h;
        int spin_r = min_d / 5;
        if (spin_r < 8) spin_r = 8;
        if (spin_r > 13) spin_r = 13;
        draw_launch_center_spinner(fb, cur_cx, cur_cy, spin_r, t, th);

        /* Clean final handoff crossfade into theme backdrop over last 36 ticks */
        if (t > 220) {
            unsigned fa = (unsigned)(((t - 220) * 32) / 36);
            if (fa > 32U) fa = 32U;
            uint16_t bg_c = th->bg_main;
            if (fa >= 32U) {
                uint32_t pair = (uint32_t)bg_c | ((uint32_t)bg_c << 16);
                uint32_t *dst32 = (uint32_t *)fb;
                for (int i = 0; i < (NL_FB_PIXELS >> 1); i++) dst32[i] = pair;
            } else {
                for (int i = 0; i < NL_FB_PIXELS; i++) fb[i] = nl_rgb565_lerp(fb[i], bg_c, fa);
            }
        }
        return;
    }

    /* =========================================================================
     * ANIMATION 4 — LOADING BAR (Shrink -> Enlarge & glide to screen center ->
     * Rounded progress bar underneath driven by REAL pre-launch stages)
     * ========================================================================= */
    if (anim_mode == NL_LAUNCH_ANIM_LOADING_BAR) {
        unsigned lb_other = 0U;
        if (t < 112) {
            int fidx = (t * 32) / 112;
            if (fidx > 32) fidx = 32;
            lb_other = ((unsigned)(256 - G_EASE_COS_LUT[fidx]) * 32U + 128U) >> 8;
        }
        unsigned dim = (unsigned)((t * 16) / 256);
        if (dim > 16U) dim = 16U;
        launch_render_bg_layer(state, fb, th, game_idx, step, search_h, in_library,
                               lb_other, dim, dim_target, false, 0, 0, 0, 0);

        int icon_w, icon_h, cur_cx, cur_cy, icon_rad;
        if (t < 40) {
            /* Phase 1: Slight crisp shrink in place */
            int p = (t * 256) / 40;
            int s_ease = (p * (512 - p) + 128) >> 8;
            int cur_scale = NL_SCALE_SELECTED - (((NL_SCALE_SELECTED - 224) * s_ease + 128) >> 8);
            icon_w = (m.card_w * cur_scale + 128) >> 8;
            icon_h = (m.card_h * cur_scale + 128) >> 8;
            cur_cx = card_cx;
            cur_cy = card_cy;
            icon_rad = (m.radius * cur_scale + 128) >> 8;
        } else {
            /* Phase 2: Enlarge + glide smoothly to exact screen center (160, 102) */
            int p = (t < 116) ? (((t - 40) * 256) / 76) : 256;
            int inv = 256 - p;
            int ease = 256 - ((inv * inv * inv) >> 16);
            int target_w = (m.card_w * 308 + 128) >> 8;
            int target_h = (m.card_h * 308 + 128) >> 8;
            if (target_w > 104) target_w = 104;
            if (target_h > 104) target_h = 104;
            if (target_w < 68) target_w = 68;
            if (target_h < 68) target_h = 68;
            int sh_w = (m.card_w * 224 + 128) >> 8;
            int sh_h = (m.card_h * 224 + 128) >> 8;
            icon_w = sh_w + (((target_w - sh_w) * ease + 128) >> 8);
            icon_h = sh_h + (((target_h - sh_h) * ease + 128) >> 8);
            cur_cx = card_cx + (((160 - card_cx) * ease + 128) >> 8);
            cur_cy = card_cy + (((102 - card_cy) * ease + 128) >> 8);
            icon_rad = m.radius + ((4 * ease) >> 8);
            if (icon_rad > 16) icon_rad = 16;
        }
        int icon_x = cur_cx - icon_w / 2;
        int icon_y = cur_cy - icon_h / 2;

        draw_game_cover_card_ex(fb, icon_x, icon_y, icon_w, icon_h, icon_rad, 0, sel, th, 0, NL_SCREEN_H, false);

        /* Phase 3: Rounded progress bar fades in smoothly underneath centered icon */
        if (t >= 100) {
            unsigned bar_a = (t >= 132) ? 32U : (unsigned)(((t - 100) * 32) / 32);
            int bar_w = 136;
            int bar_h = 7;
            int bar_x = (NL_SCREEN_W - bar_w) / 2;
            int bar_y = icon_y + icon_h + 16;
            if (bar_y > NL_SCREEN_H - 24) bar_y = NL_SCREEN_H - 24;

            uint16_t track_col = nl_rgb565_lerp(th->bg_main, th->dot_inactive, (bar_a * 22U) >> 5);
            uint16_t border_col = nl_rgb565_lerp(th->bg_main, th->border_subtle, bar_a);
            fb_fill_rounded_rect_clipped(fb, bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, 4, border_col, 0, NL_SCREEN_H);
            fb_fill_rounded_rect_clipped(fb, bar_x, bar_y, bar_w, bar_h, 3, track_col, 0, NL_SCREEN_H);

            int pct = state->anim.launch_load_pct;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            int fill_w = (bar_w * pct + 50) / 100;
            if (fill_w > 0) {
                if (fill_w < 6 && pct > 0) fill_w = 6;
                if (fill_w > bar_w) fill_w = bar_w;
                uint16_t fill_col = nl_rgb565_lerp(th->bg_main, th->accent_primary, bar_a);
                uint16_t sheen_col = nl_rgb565_lerp(fill_col, th->accent_secondary, 16U);
                fb_fill_rounded_rect_clipped(fb, bar_x, bar_y, fill_w, bar_h, 3, fill_col, 0, NL_SCREEN_H);
                if (fill_w >= 8) {
                    fb_fill_rounded_rect_clipped(fb, bar_x + 2, bar_y + 1, fill_w - 4, 2, 1, sheen_col, 0, NL_SCREEN_H);
                }
            }
        }

        /* Clean final handoff when loading reached 100% and animation completes */
        if (t > 236 && state->anim.launch_load_pct >= 100) {
            unsigned fa = (unsigned)(((t - 236) * 32) / 20);
            if (fa > 32U) fa = 32U;
            uint16_t bg_c = th->bg_main;
            if (fa >= 32U) {
                uint32_t pair = (uint32_t)bg_c | ((uint32_t)bg_c << 16);
                uint32_t *dst32 = (uint32_t *)fb;
                for (int i = 0; i < (NL_FB_PIXELS >> 1); i++) dst32[i] = pair;
            } else {
                for (int i = 0; i < NL_FB_PIXELS; i++) fb[i] = nl_rgb565_lerp(fb[i], bg_c, fa);
            }
        }
        return;
    }

    /* =========================================================================
     * ANIMATION 1 (Actuelle - 100% unchanged) & ANIMATION 2 (Theme Drop)
     * ========================================================================= */
    const int shrink_end_t = 36;
    int t_exp = 0;
    int shrink_w = (m.card_w * 232 + 128) >> 8;
    int shrink_h = (m.card_h * 232 + 128) >> 8;
    int shrink_x = sx + (m.card_w - shrink_w) / 2;
    int shrink_y = sy + (m.card_h - shrink_h) / 2;

    int icon_w = shrink_w;
    int icon_h = shrink_h;
    int icon_x = shrink_x;
    int icon_y = shrink_y;
    int icon_rad = m.radius;

    int fs_x = shrink_x, fs_y = shrink_y, fs_w = shrink_w, fs_h = shrink_h, fs_rad = m.radius;

    if (t < shrink_end_t) {
        int shrink_prog = (t * 256) / shrink_end_t;
        int s_ease = (shrink_prog * (512 - shrink_prog) + 128) >> 8;
        int cur_scale = NL_SCALE_SELECTED - (((NL_SCALE_SELECTED - 232) * s_ease + 128) >> 8);
        icon_w = (m.card_w * cur_scale + 128) >> 8;
        icon_h = (m.card_h * cur_scale + 128) >> 8;
        icon_x = sx + (m.card_w - icon_w) / 2;
        icon_y = sy + (m.card_h - icon_h) / 2;
        icon_rad = m.radius;
        fs_x = icon_x; fs_y = icon_y; fs_w = icon_w; fs_h = icon_h; fs_rad = m.radius;
    } else {
        t_exp = ((t - shrink_end_t) * 256) / (256 - shrink_end_t);
        if (t_exp < 0) t_exp = 0;
        if (t_exp > 256) t_exp = 256;

        int icon_p = (t_exp < 168) ? ((t_exp * 256) / 168) : 256;
        int icon_inv = 256 - icon_p;
        int icon_ease = 256 - ((icon_inv * icon_inv * icon_inv) >> 16);
        int cur_scale = 232 + (((294 - 232) * icon_ease + 128) >> 8);
        icon_w = (m.card_w * cur_scale + 128) >> 8;
        icon_h = (m.card_h * cur_scale + 128) >> 8;
        int cur_cx = card_cx + (((160 - card_cx) * icon_ease) >> 10);
        int cur_cy = card_cy + (((120 - card_cy) * icon_ease) >> 10);
        icon_x = cur_cx - icon_w / 2;
        icon_y = cur_cy - icon_h / 2;
        icon_rad = m.radius;

        int fs_start = (anim_mode == NL_LAUNCH_ANIM_THEME_DROP) ? 124 : 72;
        int fs_end   = (anim_mode == NL_LAUNCH_ANIM_THEME_DROP) ? 236 : 204;
        int fs_ease = 0;
        if (t_exp > fs_start) {
            int p = (t_exp >= fs_end) ? 256 : (((t_exp - fs_start) * 256) / (fs_end - fs_start));
            int inv = 256 - p;
            fs_ease = 256 - ((inv * inv * inv) >> 16);
        }
        fs_w = icon_w + (((NL_SCREEN_W - icon_w) * fs_ease + 128) >> 8);
        fs_h = icon_h + (((NL_SCREEN_H - icon_h) * fs_ease + 128) >> 8);
        fs_x = icon_x + (((0 - icon_x) * fs_ease + 128) >> 8);
        fs_y = icon_y + (((0 - icon_y) * fs_ease + 128) >> 8);
        fs_rad = (m.radius * (256 - fs_ease)) >> 8;
    }

    bool has_scr = (t >= shrink_end_t && sel->has_screenshot && sel->scr_pixels != NULL);
    unsigned xfade_alpha = 0U;
    int xf_start = (anim_mode == NL_LAUNCH_ANIM_THEME_DROP) ? 132 : 84;
    int xf_end   = (anim_mode == NL_LAUNCH_ANIM_THEME_DROP) ? 232 : 184;
    if (t >= shrink_end_t && t_exp > xf_start) {
        if (t_exp >= xf_end) {
            xfade_alpha = 32U;
        } else {
            int u256 = ((t_exp - xf_start) * 256) / (xf_end - xf_start);
            int smooth = (u256 * u256 * (768 - 2 * u256)) >> 16;
            xfade_alpha = (unsigned)((smooth * 32 + 128) >> 8);
        }
        if (xfade_alpha > 32U) xfade_alpha = 32U;
    }

    /* Fast path once fullscreen crossfade hits 100% (Animation 1 — Actuelle only) */
    if (anim_mode != NL_LAUNCH_ANIM_THEME_DROP &&
        xfade_alpha >= 32U && fs_x <= 0 && fs_y <= 0 && fs_w >= NL_SCREEN_W && fs_h >= NL_SCREEN_H) {
        if (!has_scr) {
            uint32_t pair = (uint32_t)th->bg_main | ((uint32_t)th->bg_main << 16);
            uint32_t *dst32 = (uint32_t *)fb;
            int n32 = NL_FB_PIXELS >> 1;
            for (int i = 0; i < n32; i++) dst32[i] = pair;
            return;
        }
        const int du_fp = ((NL_COVER_W - 1) << 16) / (NL_SCREEN_W - 1);
        const int dv_fp = ((NL_COVER_H - 1) << 16) / (NL_SCREEN_H - 1);
        uint8_t u_x0[NL_SCREEN_W], u_x1[NL_SCREEN_W], u_fx[NL_SCREEN_W];
        int u_lut = 0;
        for (int px = 0; px < NL_SCREEN_W; px++, u_lut += du_fp) {
            int u256 = u_lut >> 8;
            int sx0 = u256 >> 8;
            int sx1 = (sx0 + 1 < NL_COVER_W) ? (sx0 + 1) : (NL_COVER_W - 1);
            u_x0[px] = (uint8_t)sx0;
            u_x1[px] = (uint8_t)sx1;
            u_fx[px] = (uint8_t)((u256 & 0xFF) >> 3);
        }
        uint16_t vrow[NL_COVER_W];
        int v_fp = 0;
        for (int py = 0; py < NL_SCREEN_H; py++, v_fp += dv_fp) {
            int v256 = v_fp >> 8;
            int sy0 = v256 >> 8;
            int sy1 = (sy0 + 1 < NL_COVER_H) ? (sy0 + 1) : (NL_COVER_H - 1);
            unsigned fy32 = (unsigned)((v256 & 0xFF) >> 3);
            const uint16_t *sr0 = sel->scr_pixels + sy0 * NL_COVER_W;
            const uint16_t *sr1 = sel->scr_pixels + sy1 * NL_COVER_W;
            if (fy32 == 0U || sy0 == sy1) {
                memcpy(vrow, sr0, NL_COVER_W * sizeof(uint16_t));
            } else {
                for (int i = 0; i < NL_COVER_W; i++) {
                    vrow[i] = nl_rgb565_lerp(sr0[i], sr1[i], fy32);
                }
            }
            uint16_t *dst_row = fb + py * NL_SCREEN_W;
            for (int px = 0; px < NL_SCREEN_W; px++) {
                dst_row[px] = nl_rgb565_lerp(vrow[u_x0[px]], vrow[u_x1[px]], u_fx[px]);
            }
        }
        return;
    }

    int fg_x = icon_x, fg_y = icon_y, fg_w = icon_w, fg_h = icon_h;
    if (sel->icon_source != NL_ICON_NONE && sel->icon_pixels != NULL) {
        int iw = (sel->icon_w > 0) ? (int)sel->icon_w : NL_COVER_W;
        int ih = (sel->icon_h > 0) ? (int)sel->icon_h : NL_COVER_H;
        int ox = 0, oy = 0;
        compute_icon_contain_box(iw, ih, icon_w, icon_h, &ox, &oy, &fg_w, &fg_h);
        fg_x = icon_x + ox;
        fg_y = icon_y + oy;
    }
    int occl_pad = icon_rad + 2;
    int occl_x0 = fg_x + occl_pad;
    int occl_y0 = fg_y + occl_pad;
    int occl_x1 = fg_x + fg_w - occl_pad;
    int occl_y1 = fg_y + fg_h - occl_pad;
    if (occl_x0 < 0) occl_x0 = 0;
    if (occl_y0 < 0) occl_y0 = 0;
    if (occl_x1 > NL_SCREEN_W) occl_x1 = NL_SCREEN_W;
    if (occl_y1 > NL_SCREEN_H) occl_y1 = NL_SCREEN_H;
    bool has_occl = (xfade_alpha == 0U && sel->icon_alpha == NULL && occl_x0 < occl_x1 && occl_y0 < occl_y1);

    /* ========================================================================
     * #R22 ANIMATION 2 (Theme Drop — 4-Layer Organic Liquid Propagation):
     * 4 successive irregular organic liquid shapes propagate outward from the
     * selected game card, progressively rounding out and layering in 4 theme-
     * adaptive colors until they cleanly cover the entire screen.
     * ======================================================================== */
    if (anim_mode == NL_LAUNCH_ANIM_THEME_DROP) {
        int cur_icon_cx = icon_x + icon_w / 2;
        int cur_icon_cy = icon_y + icon_h / 2;

        /* Layer 1: Background dim & gentle fade */
        unsigned dim_td = (unsigned)((t * 14) >> 8);
        launch_render_bg_layer(state, fb, th, game_idx, step, search_h, in_library,
                               other_alpha_32, dim_td, dim_target,
                               false, 0, 0, 0, 0);

        /* Compute max diagonal radius from card center to farthest screen corner */
        int dx_max = (cur_icon_cx > NL_SCREEN_W - cur_icon_cx) ? cur_icon_cx : (NL_SCREEN_W - cur_icon_cx);
        int dy_max = (cur_icon_cy > NL_SCREEN_H - cur_icon_cy) ? cur_icon_cy : (NL_SCREEN_H - cur_icon_cy);
        int r_full = ink_isqrt32(dx_max * dx_max + dy_max * dy_max) + 42;

        /* Layer 2: Selected card with existing luminous perimeter sheen before final liquid coverage */
        int card_glow = 0;
        if (t >= shrink_end_t && t_exp < 180) {
            int g_in = (t_exp < 36) ? ((t_exp * 28) / 36) : 28;
            card_glow = (g_in * (180 - t_exp)) / 180;
        }
        if (t_exp < 216) {
            if (card_glow > 0 && icon_rad > 0) {
                if (sel->icon_source != NL_ICON_NONE && sel->icon_alpha != NULL) {
                    int siw = (sel->icon_w > 0) ? (int)sel->icon_w : NL_COVER_W;
                    int sih = (sel->icon_h > 0) ? (int)sel->icon_h : NL_COVER_H;
                    draw_alpha_icon_glow_clipped(fb, fg_x, fg_y, fg_w, fg_h,
                                                 sel->icon_alpha, siw, sih,
                                                 card_glow, th->is_light, 0, NL_SCREEN_H);
                } else {
                    fb_rounded_soft_luminance_clipped(fb, icon_x, icon_y, icon_w, icon_h, icon_rad, card_glow, false, 0, NL_SCREEN_H);
                }
            }
            draw_game_cover_card_ex(fb, icon_x, icon_y, icon_w, icon_h, icon_rad, card_glow, sel, th, 0, NL_SCREEN_H, false);
        }

        /* Layer 3: 4 successive organic liquid masses (irregular at start -> progressively rounding) */
        if (t >= shrink_end_t) {
            if (step >= max_steps) {
                /* Guaranteed 100% clean full-screen final color coverage before game launches */
                uint16_t c3 = nl_liquid_theme_color(state->display_mode, th, 3);
                uint32_t pair = (uint32_t)c3 | ((uint32_t)c3 << 16);
                uint32_t *dst32 = (uint32_t *)fb;
                int n32 = NL_FB_PIXELS >> 1;
                for (int i = 0; i < n32; i++) dst32[i] = pair;
            } else {
                uint32_t lseed = 0x1107U + (uint32_t)(game_idx * 31);
                draw_organic_liquid_waves_4(
                    fb, state->display_mode, th,
                    cur_icon_cx, cur_icon_cy, t_exp,
                    10, r_full, 32U,
                    lseed,
                    false, false,
                    0, 0, NL_SCREEN_W, NL_SCREEN_H
                );
            }
        }
        return;
    }

    /* Layer 1: Background fade + dim (Animation 1 — Actuelle 100% preserved) */
    unsigned dim = 0U;
    if (t <= 140) {
        dim = (unsigned)((t * 6) / 140);
    } else {
        dim = (unsigned)(6 + (((t - 140) * 16) / (256 - 140)));
    }
    if (dim > 22U) dim = 22U;
    launch_render_bg_layer(state, fb, th, game_idx, step, search_h, in_library,
                           other_alpha_32, dim, dim_target,
                           has_occl, occl_x0, occl_y0, occl_x1, occl_y1);

    /* Layer 2: Concentric luminous ripples (Animation 1 — Actuelle 100% preserved) */
    if (t >= shrink_end_t && t_exp < 228) {
        static const uint8_t k_wave_delay[3]   = {   0,  22,  44 };
        static const uint8_t k_wave_life[3]    = { 156, 148, 140 };
        static const uint8_t k_wave_span_px[3] = {  25,  19,  13 };
        static const uint8_t k_wave_peak_a[3]  = {  19,  21,  23 };
        static const uint8_t k_wave_half_w[3]  = {  88,  84,  80 };

        int min_dim = (m.card_w < m.card_h) ? m.card_w : m.card_h;
        int half_card = (min_dim * 236) >> 9;
        if (half_card < 14) half_card = 14;
        int r_start_px = (half_card > 8) ? (half_card - 7) : 4;
        const int r_start_16 = r_start_px << 4;

        for (int w = 0; w < 3; w++) {
            int delay = (int)k_wave_delay[w];
            int life = (int)k_wave_life[w];
            if (t_exp <= delay || t_exp >= delay + life) continue;

            int st = ((t_exp - delay) * 256) / life;
            if (st < 0 || st > 256) continue;

            int inv_st = 256 - st;
            int expand_ease = 256 - ((inv_st * inv_st * inv_st) >> 16);
            int r_end_16 = (half_card + (int)k_wave_span_px[w]) << 4;
            int r_16 = r_start_16 + (((r_end_16 - r_start_16) * expand_ease + 128) >> 8);

            unsigned env_256 = 0U;
            if (st < 40) {
                int idx = (st * 32) / 40;
                if (idx > 32) idx = 32;
                env_256 = (unsigned)G_EASE_COS_LUT[idx];
            } else {
                int rem = 256 - st;
                int norm = (rem * 256) / (256 - 40);
                env_256 = (unsigned)((norm * norm) >> 8);
            }

            unsigned alpha_32 = (env_256 * (unsigned)k_wave_peak_a[w] + 128U) >> 8;
            if (alpha_32 == 0U) continue;

            uint16_t halo_col  = RGB565(255, 255, 255);
            uint16_t crest_col = RGB565(255, 255, 255);
            draw_launch_concentric_ripple(
                fb, card_cx, card_cy, r_16, (int)k_wave_half_w[w],
                alpha_32, halo_col, crest_col,
                has_occl ? occl_x0 : 0,
                has_occl ? occl_y0 : 0,
                has_occl ? occl_x1 : 0,
                has_occl ? occl_y1 : 0
            );
        }
    }

    /* Layer 3: Compact Foreground Icon + perimeter luminance */
    int card_glow = 0;
    if (t >= shrink_end_t && t_exp < 200) {
        int g_in = (t_exp < 40) ? ((t_exp * 28) / 40) : 28;
        card_glow = (g_in * (200 - t_exp)) / 200;
    }
    if (card_glow > 0 && icon_rad > 0 && xfade_alpha < 28U) {
        if (sel->icon_source != NL_ICON_NONE && sel->icon_alpha != NULL) {
            int siw = (sel->icon_w > 0) ? (int)sel->icon_w : NL_COVER_W;
            int sih = (sel->icon_h > 0) ? (int)sel->icon_h : NL_COVER_H;
            draw_alpha_icon_glow_clipped(fb, fg_x, fg_y, fg_w, fg_h,
                                         sel->icon_alpha, siw, sih,
                                         card_glow, th->is_light, 0, NL_SCREEN_H);
        } else {
            fb_rounded_soft_luminance_clipped(fb, icon_x, icon_y, icon_w, icon_h, icon_rad, card_glow, false, 0, NL_SCREEN_H);
        }
    }

    draw_game_cover_card_ex(fb, icon_x, icon_y, icon_w, icon_h, icon_rad, card_glow, sel, th, 0, NL_SCREEN_H, false);

    /* Layer 4: Early Smooth Crossfade Overlay */
    if (xfade_alpha > 0U && fs_w > 0 && fs_h > 0) {
        if (!has_scr) {
            uint16_t bg_c = th->bg_main;
            if (xfade_alpha >= 32U) {
                uint32_t pair = (uint32_t)bg_c | ((uint32_t)bg_c << 16);
                uint32_t *dst32 = (uint32_t *)fb;
                int n32 = NL_FB_PIXELS >> 1;
                for (int i = 0; i < n32; i++) dst32[i] = pair;
            } else {
                for (int i = 0; i < NL_FB_PIXELS; i++) {
                    fb[i] = nl_rgb565_lerp(fb[i], bg_c, xfade_alpha);
                }
            }
        } else {
            int y0 = (fs_y < 0) ? 0 : fs_y;
            int y1 = (fs_y + fs_h > NL_SCREEN_H) ? NL_SCREEN_H : (fs_y + fs_h);
            int x0_clip = (fs_x < 0) ? 0 : fs_x;
            int x1_clip = (fs_x + fs_w > NL_SCREEN_W) ? NL_SCREEN_W : (fs_x + fs_w);
            int du_fp = (fs_w > 1) ? (((NL_COVER_W - 1) << 16) / (fs_w - 1)) : 0;
            int dv_fp = (fs_h > 1) ? (((NL_COVER_H - 1) << 16) / (fs_h - 1)) : 0;
            int v_fp = (y0 - fs_y) * dv_fp;

            uint8_t u_x0[NL_SCREEN_W];
            uint8_t u_x1[NL_SCREEN_W];
            uint8_t u_fx[NL_SCREEN_W];
            int u_lut_fp = (x0_clip - fs_x) * du_fp;
            for (int px = x0_clip; px < x1_clip; px++, u_lut_fp += du_fp) {
                int u256 = u_lut_fp >> 8;
                int sx0 = u256 >> 8;
                int sx1 = sx0 + 1;
                if (sx0 < 0) sx0 = 0;
                if (sx0 >= NL_COVER_W) sx0 = NL_COVER_W - 1;
                if (sx1 >= NL_COVER_W) sx1 = NL_COVER_W - 1;
                u_x0[px] = (uint8_t)sx0;
                u_x1[px] = (uint8_t)sx1;
                u_fx[px] = (uint8_t)((u256 & 0xFF) >> 3);
            }
            uint16_t vrow[NL_COVER_W];

            for (int py = y0; py < y1; py++, v_fp += dv_fp) {
                int dy = py - fs_y;
                int v256 = v_fp >> 8;
                int sy0 = v256 >> 8;
                int sy1 = sy0 + 1;
                if (sy0 < 0) sy0 = 0;
                if (sy0 >= NL_COVER_H) sy0 = NL_COVER_H - 1;
                if (sy1 >= NL_COVER_H) sy1 = NL_COVER_H - 1;
                unsigned fy32 = (unsigned)((v256 & 0xFF) >> 3);
                const uint16_t *sr0 = sel->scr_pixels + sy0 * NL_COVER_W;
                const uint16_t *sr1 = sel->scr_pixels + sy1 * NL_COVER_W;
                if (fy32 == 0U || sy0 == sy1) {
                    memcpy(vrow, sr0, NL_COVER_W * sizeof(uint16_t));
                } else {
                    for (int sx_i = 0; sx_i < NL_COVER_W; sx_i++) {
                        vrow[sx_i] = nl_rgb565_lerp(sr0[sx_i], sr1[sx_i], fy32);
                    }
                }

                uint16_t *dst_row = fb + py * NL_SCREEN_W;
                bool corner_row = (fs_rad > 0) && (dy <= fs_rad || dy >= fs_h - 1 - fs_rad);
                int left_end = corner_row ? (fs_x + fs_rad + 1) : x0_clip;
                int right_start = corner_row ? (fs_x + fs_w - fs_rad - 1) : x1_clip;
                if (left_end < x0_clip) left_end = x0_clip;
                if (left_end > x1_clip) left_end = x1_clip;
                if (right_start < left_end) right_start = left_end;
                if (right_start > x1_clip) right_start = x1_clip;

                for (int px = x0_clip; px < left_end; px++) {
                    bool is_border = false;
                    unsigned cov = aa_rounded_card_pixel(px - fs_x, dy, fs_w, fs_h, fs_rad, &is_border);
                    if (cov == 0U) continue;
                    (void)is_border;
                    uint16_t scr_px = nl_rgb565_lerp(vrow[u_x0[px]], vrow[u_x1[px]], u_fx[px]);
                    unsigned eff_a = (cov >= 32U) ? xfade_alpha : ((xfade_alpha * cov + 16U) >> 5);
                    if (eff_a > 0U) dst_row[px] = nl_rgb565_lerp(dst_row[px], scr_px, eff_a);
                }
                if (xfade_alpha >= 32U) {
                    for (int px = left_end; px < right_start; px++) {
                        dst_row[px] = nl_rgb565_lerp(vrow[u_x0[px]], vrow[u_x1[px]], u_fx[px]);
                    }
                } else {
                    for (int px = left_end; px < right_start; px++) {
                        uint16_t scr_px = nl_rgb565_lerp(vrow[u_x0[px]], vrow[u_x1[px]], u_fx[px]);
                        dst_row[px] = nl_rgb565_lerp(dst_row[px], scr_px, xfade_alpha);
                    }
                }
                for (int px = right_start; px < x1_clip; px++) {
                    bool is_border = false;
                    unsigned cov = aa_rounded_card_pixel(px - fs_x, dy, fs_w, fs_h, fs_rad, &is_border);
                    if (cov == 0U) continue;
                    (void)is_border;
                    uint16_t scr_px = nl_rgb565_lerp(vrow[u_x0[px]], vrow[u_x1[px]], u_fx[px]);
                    unsigned eff_a = (cov >= 32U) ? xfade_alpha : ((xfade_alpha * cov + 16U) >> 5);
                    if (eff_a > 0U) dst_row[px] = nl_rgb565_lerp(dst_row[px], scr_px, eff_a);
                }
            }
        }
    }
}



