/****************************************************************************
 * N-Launcher — covers.c
 * Real Cover Artwork Loading (.bmp.tns / .ico.tns) with Aspect-Preserving
 * Center-Crop, Existing Cover Preservation, and Strict VRAM Capture Validation
 ****************************************************************************/

#include "nlauncher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

static int str_icmp_c(const char *a, const char *b) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void build_sidecar_path(const char *game_path, const char *ext_tns, char *out, size_t max_len) {
    size_t i = 0;
    while (i + 1 < max_len && game_path[i] != '\0') {
        out[i] = game_path[i];
        i++;
    }
    out[i] = '\0';

    if (i > 4 && str_icmp_c(out + i - 4, ".tns") == 0) {
        out[i - 4] = '\0';
        i -= 4;
    }
    size_t elen = strlen(ext_tns);
    if (i + elen + 1 < max_len) {
        memcpy(out + i, ext_tns, elen + 1);
    }
}

static bool ensure_pixel_buf(uint16_t **dst_buf) {
    if (!dst_buf) return false;
    if (!*dst_buf) {
        *dst_buf = (uint16_t *)malloc(NL_COVER_BYTES);
    }
    return *dst_buf != NULL;
}

static void free_game_icon_buffers(nl_game_t *game) {
    if (!game) return;
    if (game->icon_pixels) {
        free(game->icon_pixels);
        game->icon_pixels = NULL;
    }
    if (game->icon_alpha) {
        free(game->icon_alpha);
        game->icon_alpha = NULL;
    }
    game->icon_w = 0;
    game->icon_h = 0;
    game->icon_low_res = false;
}

static bool alloc_game_icon_buf(nl_game_t *game, int w, int h, bool alloc_alpha) {
    if (!game || w <= 0 || h <= 0 || w > NL_ICON_MAX_DIM || h > NL_ICON_MAX_DIM) {
        return false;
    }
    free_game_icon_buffers(game);
    size_t count = (size_t)w * (size_t)h;
#ifdef __arm__
    /* Guarantee >= 512 KB contiguous heap headroom on TI-Nspire so 78+ icons NEVER starve OS/FBs */
    void *headroom = malloc(524288U);
    if (!headroom) {
        return false;
    }
    free(headroom);
#endif
    game->icon_pixels = (uint16_t *)malloc(count * sizeof(uint16_t));
    if (!game->icon_pixels) return false;
    if (alloc_alpha) {
        game->icon_alpha = (uint8_t *)malloc(count);
        if (!game->icon_alpha) {
            free(game->icon_pixels);
            game->icon_pixels = NULL;
            return false;
        }
    }
    game->icon_w = (uint16_t)w;
    game->icon_h = (uint16_t)h;
    return true;
}

/* Preserve exact aspect ratio of custom icons within NL_ICON_MAX_DIM x NL_ICON_MAX_DIM (zero cropping/stretching) */
static void compute_icon_fit_dims(int src_w, int src_h, int *out_w, int *out_h) {
    if (src_w <= 0 || src_h <= 0) {
        *out_w = 1;
        *out_h = 1;
        return;
    }
    if (src_w <= NL_ICON_MAX_DIM && src_h <= NL_ICON_MAX_DIM) {
        *out_w = src_w;
        *out_h = src_h;
        return;
    }
    if (src_w >= src_h) {
        int dw = NL_ICON_MAX_DIM;
        int dh = (int)(((int64_t)src_h * NL_ICON_MAX_DIM + (src_w / 2)) / src_w);
        if (dh < 1) dh = 1;
        if (dh > NL_ICON_MAX_DIM) dh = NL_ICON_MAX_DIM;
        *out_w = dw;
        *out_h = dh;
    } else {
        int dh = NL_ICON_MAX_DIM;
        int dw = (int)(((int64_t)src_w * NL_ICON_MAX_DIM + (src_h / 2)) / src_h);
        if (dw < 1) dw = 1;
        if (dw > NL_ICON_MAX_DIM) dw = NL_ICON_MAX_DIM;
        *out_w = dw;
        *out_h = dh;
    }
}

/* Compute centered aspect-preserving crop window ("cover" fit, used only for 320x240 VRAM -> 94x68 screenshots) */
static void compute_aspect_cover_crop(int src_w, int src_h, int dst_w, int dst_h,
                                      int *out_x0, int *out_y0, int *out_cw, int *out_ch) {
    if (src_w <= 0 || src_h <= 0) {
        *out_x0 = 0; *out_y0 = 0; *out_cw = 1; *out_ch = 1;
        return;
    }
    if ((int64_t)src_w * dst_h > (int64_t)dst_w * src_h) {
        int cw = (int)(((int64_t)src_h * dst_w) / dst_h);
        if (cw < 1) cw = 1;
        if (cw > src_w) cw = src_w;
        *out_cw = cw;
        *out_ch = src_h;
        *out_x0 = (src_w - cw) / 2;
        *out_y0 = 0;
    } else {
        int ch = (int)(((int64_t)src_w * dst_h) / dst_w);
        if (ch < 1) ch = 1;
        if (ch > src_h) ch = src_h;
        *out_cw = src_w;
        *out_ch = ch;
        *out_x0 = 0;
        *out_y0 = (src_h - ch) / 2;
    }
}

/* Load NLIC file into target RGB565 buffer (94x68 or legacy 88x48) */
static bool load_nlic_pixels(const char *path, uint16_t **dst_pixels) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    uint8_t hdr[16];
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        return false;
    }

    uint32_t magic = read_u32_le(hdr + 0);
    uint16_t ver   = read_u16_le(hdr + 4);
    uint16_t w     = read_u16_le(hdr + 6);
    uint16_t h     = read_u16_le(hdr + 8);

    if (magic != NL_ICON_MAGIC || ver != 1 ||
        !((w == NL_COVER_W && h == NL_COVER_H) || (w == NL_ICON_V1_W && h == NL_ICON_V1_H))) {
        fclose(fp);
        return false;
    }

#ifdef __arm__
    /* Verify 256 KB heap headroom on calculator before allocating .scr.tns buffer
     * so preloading screenshots never starves stage 11 library transition or OS allocations */
    if (!*dst_pixels) {
        void *headroom = malloc(262144U);
        if (!headroom) {
            fclose(fp);
            return false;
        }
        free(headroom);
    }
#endif

    if (!ensure_pixel_buf(dst_pixels)) {
        fclose(fp);
        return false;
    }

    size_t src_pixels = (size_t)w * (size_t)h;
    size_t src_bytes = src_pixels * 2;
    uint8_t *raw = (uint8_t *)malloc(src_bytes);
    if (!raw) {
        fclose(fp);
        return false;
    }
    if (fread(raw, 1, src_bytes, fp) != src_bytes) {
        free(raw);
        fclose(fp);
        return false;
    }
    fclose(fp);

    uint16_t *out = *dst_pixels;
    if (w == NL_COVER_W && h == NL_COVER_H) {
        for (int i = 0; i < NL_COVER_PIXELS; i++) {
            out[i] = read_u16_le(raw + i * 2);
        }
    } else {
        int cx0, cy0, cw, ch;
        compute_aspect_cover_crop(w, h, NL_COVER_W, NL_COVER_H, &cx0, &cy0, &cw, &ch);
        for (int dy = 0; dy < NL_COVER_H; dy++) {
            int sy = cy0 + (dy * ch) / NL_COVER_H;
            if (sy >= h) sy = h - 1;
            for (int dx = 0; dx < NL_COVER_W; dx++) {
                int sx = cx0 + (dx * cw) / NL_COVER_W;
                if (sx >= w) sx = w - 1;
                out[dy * NL_COVER_W + dx] = read_u16_le(raw + (sy * w + sx) * 2);
            }
        }
    }

    free(raw);
    return true;
}

/* Load NLIC file for game cover icon */
static bool load_nlic_file(const char *path, nl_game_t *game) {
    if (!game) return false;
    if (game->icon_alpha) {
        free(game->icon_alpha);
        game->icon_alpha = NULL;
    }
    if (!load_nlic_pixels(path, &game->icon_pixels)) return false;
    game->icon_w = NL_COVER_W;
    game->icon_h = NL_COVER_H;
    game->icon_source = NL_ICON_CAPTURED;
    return true;
}

static void decode_dib_pixel_rgba(const uint8_t *row_buf, int sx, uint16_t bpp, uint32_t comp,
                                  const uint8_t *palette, uint32_t pal_count,
                                  uint8_t *out_r, uint8_t *out_g, uint8_t *out_b, uint8_t *out_a) {
    if (bpp == 1U || bpp == 4U || bpp == 8U) {
        uint32_t idx = 0U;
        if (bpp == 8U) {
            idx = row_buf[sx];
        } else if (bpp == 4U) {
            uint8_t byte_val = row_buf[sx >> 1];
            idx = (sx & 1) ? (uint32_t)(byte_val & 0x0FU) : (uint32_t)((byte_val >> 4) & 0x0FU);
        } else {
            uint8_t byte_val = row_buf[sx >> 3];
            idx = (uint32_t)((byte_val >> (7 - (sx & 7))) & 1U);
        }
        if (palette && idx < pal_count) {
            const uint8_t *pe = palette + idx * 4U;
            *out_b = pe[0];
            *out_g = pe[1];
            *out_r = pe[2];
        } else {
            *out_r = *out_g = *out_b = 0U;
        }
        *out_a = 255U;
        return;
    }

    const uint8_t *p = row_buf + (size_t)sx * (size_t)(bpp / 8U);
    if (bpp == 16U) {
        uint16_t v = read_u16_le(p);
        if (comp == 3U) {
            unsigned r5 = (v >> 11) & 0x1FU;
            unsigned g6 = (v >> 5)  & 0x3FU;
            unsigned b5 =  v        & 0x1FU;
            *out_r = (uint8_t)((r5 << 3) | (r5 >> 2));
            *out_g = (uint8_t)((g6 << 2) | (g6 >> 4));
            *out_b = (uint8_t)((b5 << 3) | (b5 >> 2));
        } else {
            unsigned r5 = (v >> 10) & 0x1FU;
            unsigned g5 = (v >> 5)  & 0x1FU;
            unsigned b5 =  v        & 0x1FU;
            *out_r = (uint8_t)((r5 << 3) | (r5 >> 2));
            *out_g = (uint8_t)((g5 << 3) | (g5 >> 2));
            *out_b = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
        *out_a = 255U;
    } else if (bpp == 32U) {
        *out_b = p[0];
        *out_g = p[1];
        *out_r = p[2];
        *out_a = p[3];
    } else {
        *out_b = p[0];
        *out_g = p[1];
        *out_r = p[2];
        *out_a = 255U;
    }
}

static nl_progress_cb_t G_ICON_DECODE_CB  = NULL;
static void            *G_ICON_DECODE_CTX = NULL;

void nl_set_icon_decode_progress_cb(nl_progress_cb_t cb, void *ctx) {
    G_ICON_DECODE_CB  = cb;
    G_ICON_DECODE_CTX = ctx;
}

static inline void pump_icon_decode_progress(void) {
    if (G_ICON_DECODE_CB) {
        G_ICON_DECODE_CB(G_ICON_DECODE_CTX);
    }
}

/* Downsample an RGBA8888 buffer into game->icon_pixels / icon_alpha with symmetric empty-margin trim */
static bool finalize_rgba_into_game_icon(const uint8_t *rgba, int src_w, int src_h, nl_game_t *game) {
    if (!rgba || src_w <= 0 || src_h <= 0 || !game) return false;

    bool any_nonzero_alpha = false;
    bool any_partial_alpha = false;
    size_t total_px = (size_t)src_w * (size_t)src_h;
    for (size_t i = 0; i < total_px; i++) {
        uint8_t a = rgba[i * 4U + 3U];
        if (a > 0U) any_nonzero_alpha = true;
        if (a < 255U) any_partial_alpha = true;
    }

    /* Independent 4-side transparent outer margin trim when artwork sits inside transparent padding */
    int crop_x = 0, crop_y = 0, crop_w = src_w, crop_h = src_h;
    if (any_nonzero_alpha && any_partial_alpha && src_w >= 24 && src_h >= 24) {
        int min_x = src_w, min_y = src_h, max_x = -1, max_y = -1;
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                if (rgba[((size_t)y * (size_t)src_w + (size_t)x) * 4U + 3U] > 16U) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
        if (max_x >= min_x && max_y >= min_y) {
            int pad_l = min_x;
            int pad_r = src_w - 1 - max_x;
            int pad_t = min_y;
            int pad_b = src_h - 1 - max_y;
            int x0 = (pad_l >= 3) ? (min_x - 1) : 0;
            int x1 = (pad_r >= 3) ? (max_x + 1) : (src_w - 1);
            int y0 = (pad_t >= 3) ? (min_y - 1) : 0;
            int y1 = (pad_b >= 3) ? (max_y + 1) : (src_h - 1);
            int cw = x1 - x0 + 1;
            int ch = y1 - y0 + 1;
            if (cw >= 16 && ch >= 16) {
                crop_x = x0;
                crop_y = y0;
                crop_w = cw;
                crop_h = ch;
            }
        }
    }

    int dst_w = 0, dst_h = 0;
    compute_icon_fit_dims(crop_w, crop_h, &dst_w, &dst_h);

    /* Re-check partial alpha strictly inside the cropped region so trimmed opaque icons avoid dead alpha buffers */
    bool keep_alpha = false;
    if (any_partial_alpha && any_nonzero_alpha) {
        if (crop_x == 0 && crop_y == 0 && crop_w == src_w && crop_h == src_h) {
            keep_alpha = true;
        } else {
            for (int y = crop_y; y < crop_y + crop_h && !keep_alpha; y++) {
                const uint8_t *row_a = rgba + ((size_t)y * (size_t)src_w + (size_t)crop_x) * 4U + 3U;
                for (int x = 0; x < crop_w; x++) {
                    if (row_a[(size_t)x * 4U] < 255U) {
                        keep_alpha = true;
                        break;
                    }
                }
            }
        }
    }

    if (!alloc_game_icon_buf(game, dst_w, dst_h, keep_alpha)) {
        return false;
    }
    /* Identify low-resolution sources (< 32x32) without ever artificially upscaling them */
    game->icon_low_res = (crop_w < 32 || crop_h < 32);

    /* Exact 1:1 native pixel copy when source already fits <= NL_ICON_MAX_DIM (zero blur, zero upscaling) */
    if (dst_w == crop_w && dst_h == crop_h) {
        for (int dy = 0; dy < dst_h; dy++) {
            const uint8_t *src_row = rgba + ((size_t)(crop_y + dy) * (size_t)src_w + (size_t)crop_x) * 4U;
            uint16_t *dst_row = game->icon_pixels + (size_t)dy * (size_t)dst_w;
            uint8_t *alp_row = keep_alpha ? (game->icon_alpha + (size_t)dy * (size_t)dst_w) : NULL;
            for (int dx = 0; dx < dst_w; dx++) {
                const uint8_t *p = src_row + (size_t)dx * 4U;
                dst_row[dx] = RGB565(p[0], p[1], p[2]);
                if (alp_row) alp_row[dx] = p[3];
            }
        }
        return true;
    }

    /* High-precision fractional area-averaging downscaler (256-scale overlap weights, premultiplied alpha).
     * #R19: Precompute X span tables + use 32-bit integer arithmetic (eliminates 28,000+ 64-bit software
     * divisions per 144x144 PNG on ARM926EJ-S while preserving identical downscaled output quality). */
    uint32_t x0_tbl[NL_ICON_MAX_DIM];
    uint32_t x1_tbl[NL_ICON_MAX_DIM];
    int sx_start_tbl[NL_ICON_MAX_DIM];
    int sx_end_tbl[NL_ICON_MAX_DIM];
    for (int dx = 0; dx < dst_w; dx++) {
        uint32_t x0_256 = (uint32_t)crop_x * 256U + ((uint32_t)dx * (uint32_t)crop_w * 256U) / (uint32_t)dst_w;
        uint32_t x1_256 = (uint32_t)crop_x * 256U + ((uint32_t)(dx + 1) * (uint32_t)crop_w * 256U) / (uint32_t)dst_w;
        if (x1_256 <= x0_256) x1_256 = x0_256 + 256U;
        int sx_s = (int)(x0_256 >> 8);
        int sx_e = (int)((x1_256 + 255U) >> 8);
        if (sx_e > crop_x + crop_w) sx_e = crop_x + crop_w;
        x0_tbl[dx] = x0_256;
        x1_tbl[dx] = x1_256;
        sx_start_tbl[dx] = sx_s;
        sx_end_tbl[dx] = sx_e;
    }

    /* For crop_w/crop_h <= 512 (covers 144x144, 256x256, 512x512), unshifted 32-bit accumulators
     * never exceed ~815M (< 4.29B UINT32_MAX) and match uint64_t bit-for-bit! */
    const bool shift_wa = (crop_w > 512 || crop_h > 512);
    bool any_out_partial_alpha = false;

    for (int dy = 0; dy < dst_h; dy++) {
        if ((dy & 15) == 0) pump_icon_decode_progress();
        uint32_t y0_256 = (uint32_t)crop_y * 256U + ((uint32_t)dy * (uint32_t)crop_h * 256U) / (uint32_t)dst_h;
        uint32_t y1_256 = (uint32_t)crop_y * 256U + ((uint32_t)(dy + 1) * (uint32_t)crop_h * 256U) / (uint32_t)dst_h;
        if (y1_256 <= y0_256) y1_256 = y0_256 + 256U;
        int sy_start = (int)(y0_256 >> 8);
        int sy_end   = (int)((y1_256 + 255U) >> 8);
        if (sy_end > crop_y + crop_h) sy_end = crop_y + crop_h;

        uint16_t *dst_pix_row = game->icon_pixels + (size_t)dy * (size_t)dst_w;
        uint8_t  *dst_alp_row = keep_alpha ? (game->icon_alpha + (size_t)dy * (size_t)dst_w) : NULL;

        for (int dx = 0; dx < dst_w; dx++) {
            uint32_t x0_256 = x0_tbl[dx];
            uint32_t x1_256 = x1_tbl[dx];
            int sx_start = sx_start_tbl[dx];
            int sx_end   = sx_end_tbl[dx];

            uint32_t acc_r = 0U, acc_g = 0U, acc_b = 0U, acc_a = 0U, acc_w = 0U;
            for (int sy = sy_start; sy < sy_end; sy++) {
                uint32_t py0 = (uint32_t)sy * 256U;
                uint32_t py1 = py0 + 256U;
                uint32_t oy0 = (py0 > y0_256) ? py0 : y0_256;
                uint32_t oy1 = (py1 < y1_256) ? py1 : y1_256;
                if (oy1 <= oy0) continue;
                uint32_t wy = oy1 - oy0; /* 1..256 */

                const uint8_t *src_row = rgba + (size_t)sy * (size_t)src_w * 4U;
                for (int sx = sx_start; sx < sx_end; sx++) {
                    uint32_t px0 = (uint32_t)sx * 256U;
                    uint32_t px1 = px0 + 256U;
                    uint32_t ox0 = (px0 > x0_256) ? px0 : x0_256;
                    uint32_t ox1 = (px1 < x1_256) ? px1 : x1_256;
                    if (ox1 <= ox0) continue;
                    uint32_t w_area = (wy * (ox1 - ox0) + 128U) >> 8; /* 1..256 */
                    if (w_area == 0U) w_area = 1U;

                    const uint8_t *p = src_row + (size_t)sx * 4U;
                    if (keep_alpha) {
                        uint32_t wa = shift_wa ? ((w_area * (uint32_t)p[3] + 16U) >> 5) : (w_area * (uint32_t)p[3]);
                        acc_r += (uint32_t)p[0] * wa;
                        acc_g += (uint32_t)p[1] * wa;
                        acc_b += (uint32_t)p[2] * wa;
                        acc_a += wa;
                    } else {
                        acc_r += (uint32_t)p[0] * w_area;
                        acc_g += (uint32_t)p[1] * w_area;
                        acc_b += (uint32_t)p[2] * w_area;
                    }
                    acc_w += w_area;
                }
            }
            if (acc_w == 0U) acc_w = 1U;
            unsigned r = 25U, g = 32U, b = 44U;
            if (keep_alpha) {
                uint32_t denom_a = shift_wa ? ((acc_w + 16U) >> 5) : acc_w;
                if (denom_a == 0U) denom_a = 1U;
                unsigned a_avg = (unsigned)((acc_a + (denom_a >> 1)) / denom_a);
                if (a_avg > 255U) a_avg = 255U;
                dst_alp_row[dx] = (uint8_t)a_avg;
                if (a_avg < 255U) any_out_partial_alpha = true;
                if (acc_a > 0U) {
                    uint32_t half_a = acc_a >> 1;
                    r = (unsigned)((acc_r + half_a) / acc_a);
                    g = (unsigned)((acc_g + half_a) / acc_a);
                    b = (unsigned)((acc_b + half_a) / acc_a);
                }
            } else {
                uint32_t half_w = acc_w >> 1;
                r = (unsigned)((acc_r + half_w) / acc_w);
                g = (unsigned)((acc_g + half_w) / acc_w);
                b = (unsigned)((acc_b + half_w) / acc_w);
            }
            if (r > 255U) r = 255U;
            if (g > 255U) g = 255U;
            if (b > 255U) b = 255U;
            dst_pix_row[dx] = RGB565(r, g, b);
        }
    }

    /* If downscaling produced a 100% opaque icon, drop dead alpha buffer to save 33% RAM & unlock pure-RGB565 fast paths */
    if (keep_alpha && !any_out_partial_alpha && game->icon_alpha) {
        free(game->icon_alpha);
        game->icon_alpha = NULL;
    }
    return true;
}

/* Stream a DIB pixel array (1/4/8/16/24/32bpp + optional 1-bit ICO AND mask) into game icon */
static bool decode_dib_into_game_icon(FILE *fp, uint32_t pixel_offset, uint32_t pal_offset,
                                      uint32_t clr_used, int src_w, int src_h,
                                      uint16_t bpp, uint32_t comp, bool top_down,
                                      bool is_ico, long file_size, nl_game_t *game) {
    if (src_w <= 0 || src_w > 512 || src_h <= 0 || src_h > 512) return false;
    if (file_size <= 0L) return false;
    uint32_t bits_per_row = (uint32_t)src_w * (uint32_t)bpp;
    uint32_t xor_stride = ((bits_per_row + 31U) >> 5) * 4U;
    if (xor_stride == 0U || (uint64_t)pixel_offset + (uint64_t)src_h * (uint64_t)xor_stride > (uint64_t)file_size) {
        return false;
    }

    uint8_t *palette = NULL;
    uint32_t pal_count = 0U;
    if (bpp <= 8U) {
        uint32_t max_pal = 1U << bpp;
        pal_count = (clr_used > 0U && clr_used <= max_pal) ? clr_used : max_pal;
        if ((uint64_t)pal_offset + (uint64_t)pal_count * 4ULL > (uint64_t)file_size) {
            return false;
        }
        palette = (uint8_t *)calloc(256U, 4U);
        if (!palette) return false;
        if (fseek(fp, (long)pal_offset, SEEK_SET) != 0 ||
            fread(palette, 4U, pal_count, fp) != pal_count) {
            free(palette);
            return false;
        }
    }

    /* Cap intermediate RGBA decode buffer to <= 128x128 (64 KB max instead of 1 MB for 512x512!) */
    int work_w = src_w;
    int work_h = src_h;
    if (work_w > 128 || work_h > 128) {
        if (work_w >= work_h) {
            work_w = 128;
            work_h = (int)(((int64_t)src_h * 128 + (src_w / 2)) / src_w);
            if (work_h < 1) work_h = 1;
            if (work_h > 128) work_h = 128;
        } else {
            work_h = 128;
            work_w = (int)(((int64_t)src_w * 128 + (src_h / 2)) / src_h);
            if (work_w < 1) work_w = 1;
            if (work_w > 128) work_w = 128;
        }
    }

#ifdef __arm__
    void *headroom = malloc(524288U);
    if (!headroom) {
        free(palette);
        return false;
    }
    free(headroom);
#endif

    size_t rgba_bytes = (size_t)work_w * (size_t)work_h * 4U;
    uint8_t *rgba = (uint8_t *)malloc(rgba_bytes);
    uint8_t *row_buf = (uint8_t *)malloc(xor_stride);
    if (!rgba || !row_buf) {
        free(palette);
        free(rgba);
        free(row_buf);
        return false;
    }

    bool any_nonzero_alpha = false;
    for (int wy = 0; wy < work_h; wy++) {
        int sy = (work_h == src_h) ? wy : ((wy * src_h) / work_h);
        if (sy >= src_h) sy = src_h - 1;
        int file_row = top_down ? sy : (src_h - 1 - sy);
        long seek_pos = (long)((uint64_t)pixel_offset + (uint64_t)file_row * (uint64_t)xor_stride);
        if (fseek(fp, seek_pos, SEEK_SET) != 0 || fread(row_buf, 1, xor_stride, fp) != xor_stride) {
            free(palette);
            free(rgba);
            free(row_buf);
            return false;
        }
        uint8_t *dst_row = rgba + (size_t)wy * (size_t)work_w * 4U;
        for (int wx = 0; wx < work_w; wx++) {
            int sx = (work_w == src_w) ? wx : ((wx * src_w) / work_w);
            if (sx >= src_w) sx = src_w - 1;
            uint8_t r, g, b, a;
            decode_dib_pixel_rgba(row_buf, sx, bpp, comp, palette, pal_count, &r, &g, &b, &a);
            if (bpp == 32U && a > 0U) any_nonzero_alpha = true;
            dst_row[wx * 4 + 0] = r;
            dst_row[wx * 4 + 1] = g;
            dst_row[wx * 4 + 2] = b;
            dst_row[wx * 4 + 3] = a;
        }
    }
    free(palette);
    free(row_buf);

    /* Apply 1-bit AND transparency mask for non-32bpp ICOs or legacy 32bpp with all-zero alpha */
    if (is_ico && (bpp < 32U || !any_nonzero_alpha)) {
        uint32_t and_stride = (((uint32_t)src_w + 31U) >> 5) * 4U;
        uint64_t and_offset = (uint64_t)pixel_offset + (uint64_t)src_h * (uint64_t)xor_stride;
        if (and_stride > 0U && and_offset + (uint64_t)src_h * (uint64_t)and_stride <= (uint64_t)file_size) {
            uint8_t *and_buf = (uint8_t *)malloc(and_stride);
            if (and_buf) {
                bool and_ok = true;
                bool has_opaque = false;
                bool has_trans = false;
                for (int wy = 0; wy < work_h; wy++) {
                    int sy = (work_h == src_h) ? wy : ((wy * src_h) / work_h);
                    if (sy >= src_h) sy = src_h - 1;
                    int file_row = top_down ? sy : (src_h - 1 - sy);
                    long seek_pos = (long)(and_offset + (uint64_t)file_row * (uint64_t)and_stride);
                    if (fseek(fp, seek_pos, SEEK_SET) != 0 || fread(and_buf, 1, and_stride, fp) != and_stride) {
                        and_ok = false;
                        break;
                    }
                    uint8_t *dst_row = rgba + (size_t)wy * (size_t)work_w * 4U;
                    for (int wx = 0; wx < work_w; wx++) {
                        int sx = (work_w == src_w) ? wx : ((wx * src_w) / work_w);
                        if (sx >= src_w) sx = src_w - 1;
                        uint8_t bit = (uint8_t)((and_buf[sx >> 3] >> (7 - (sx & 7))) & 1U);
                        if (bit) {
                            has_trans = true;
                            dst_row[wx * 4 + 3] = 0U;
                        } else {
                            has_opaque = true;
                            dst_row[wx * 4 + 3] = 255U;
                        }
                    }
                }
                free(and_buf);
                /* If legacy 32bpp had alpha=0 everywhere and no valid AND mask, treat as opaque */
                if (!and_ok || (!has_opaque && !any_nonzero_alpha)) {
                    for (size_t i = 0; i < (size_t)work_w * (size_t)work_h; i++) {
                        rgba[i * 4U + 3U] = 255U;
                    }
                } else if (!has_trans && bpp == 32U && !any_nonzero_alpha) {
                    for (size_t i = 0; i < (size_t)work_w * (size_t)work_h; i++) {
                        rgba[i * 4U + 3U] = 255U;
                    }
                }
            }
        } else if (bpp == 32U && !any_nonzero_alpha) {
            for (size_t i = 0; i < (size_t)work_w * (size_t)work_h; i++) {
                rgba[i * 4U + 3U] = 255U;
            }
        }
    }

    bool ok = finalize_rgba_into_game_icon(rgba, work_w, work_h, game);
    free(rgba);
    return ok;
}

/* ============================================================================
 * Pure C11 RFC1950/1951 Inflate + PNG Unfilter (for PNG-compressed .ico files)
 * ============================================================================ */
typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
    uint32_t       bit_buf;
    int            bit_cnt;
} nl_bitstream_t;

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
    int      max_syms;
} nl_huff_t;

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

static int bs_read_bits(nl_bitstream_t *bs, int n, uint32_t *out_val) {
    while (bs->bit_cnt < n) {
        if (bs->pos >= bs->len) return -1;
        bs->bit_buf |= (uint32_t)bs->data[bs->pos++] << bs->bit_cnt;
        bs->bit_cnt += 8;
    }
    *out_val = bs->bit_buf & ((1U << n) - 1U);
    bs->bit_buf >>= n;
    bs->bit_cnt -= n;
    return 0;
}

static bool huff_build(nl_huff_t *h, const uint8_t *lens, int num_syms) {
    if (!h || !lens || num_syms <= 0 || num_syms > 288) return false;
    uint16_t offs[16];
    h->max_syms = num_syms;
    for (int i = 0; i < 16; i++) h->counts[i] = 0U;
    for (int i = 0; i < num_syms; i++) {
        if (lens[i] >= 16U) return false;
        h->counts[lens[i]]++;
    }
    if (h->counts[0] == (uint16_t)num_syms) return true;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= (int)h->counts[len];
        if (left < 0) return false;
    }
    offs[1] = 0U;
    for (int len = 1; len < 15; len++) {
        offs[len + 1] = (uint16_t)(offs[len] + h->counts[len]);
    }
    for (int sym = 0; sym < num_syms; sym++) {
        if (lens[sym] != 0U) {
            uint16_t dst_idx = offs[lens[sym]]++;
            if ((int)dst_idx >= num_syms) return false;
            h->symbols[dst_idx] = (uint16_t)sym;
        }
    }
    return true;
}

static int huff_decode(nl_bitstream_t *bs, const nl_huff_t *h, int *out_sym) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= 15; len++) {
        uint32_t bit = 0U;
        if (bs_read_bits(bs, 1, &bit) != 0) return -1;
        code |= (int)bit;
        int count = (int)h->counts[len];
        if (code - count < first) {
            int pos = index + (code - first);
            if (pos < 0 || pos >= h->max_syms || pos >= 288) return -1;
            *out_sym = (int)h->symbols[pos];
            return 0;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

typedef struct {
    nl_huff_t lit_h;
    nl_huff_t dist_h;
    nl_huff_t cl_h;
    uint8_t   lengths[288 + 32];
    uint8_t   cl_lens[19];
} nl_inflate_scratch_t;

static bool inflate_zlib_stream(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len) {
    if (!src || !dst || !out_len || src_len < 6U) return false;
    uint8_t cmf = src[0];
    uint8_t flg = src[1];
    if ((cmf & 0x0FU) != 8U) return false;
    if ((((uint32_t)cmf * 256U + (uint32_t)flg) % 31U) != 0U) return false;
    size_t hdr_off = (flg & 0x20U) ? 6U : 2U;
    if (hdr_off >= src_len) return false;

    nl_inflate_scratch_t *sc = (nl_inflate_scratch_t *)calloc(1U, sizeof(nl_inflate_scratch_t));
    if (!sc) return false;

    nl_bitstream_t bs;
    bs.data = src + hdr_off;
    bs.len = src_len - hdr_off;
    bs.pos = 0U;
    bs.bit_buf = 0U;
    bs.bit_cnt = 0;

    static const uint16_t len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const uint8_t len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const uint16_t dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const uint8_t dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static const uint8_t cl_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    size_t out_pos = 0U;
    uint32_t bfinal = 0U;
    do {
        uint32_t btype = 0U;
        if (bs_read_bits(&bs, 1, &bfinal) != 0) { free(sc); return false; }
        if (bs_read_bits(&bs, 2, &btype) != 0)  { free(sc); return false; }

        if (btype == 0U) {
            bs.bit_buf = 0U;
            bs.bit_cnt = 0;
            if (bs.pos + 4U > bs.len) { free(sc); return false; }
            uint16_t blen = read_u16_le(bs.data + bs.pos);
            uint16_t bnlen = read_u16_le(bs.data + bs.pos + 2U);
            bs.pos += 4U;
            if (((uint32_t)blen + (uint32_t)bnlen) != 0xFFFFU) { free(sc); return false; }
            if (bs.pos + blen > bs.len || out_pos + blen > dst_cap) { free(sc); return false; }
            memcpy(dst + out_pos, bs.data + bs.pos, blen);
            bs.pos += blen;
            out_pos += blen;
        } else if (btype == 1U || btype == 2U) {
            if (btype == 1U) {
                for (int i = 0; i <= 143; i++) sc->lengths[i] = 8U;
                for (int i = 144; i <= 255; i++) sc->lengths[i] = 9U;
                for (int i = 256; i <= 279; i++) sc->lengths[i] = 7U;
                for (int i = 280; i <= 287; i++) sc->lengths[i] = 8U;
                for (int i = 0; i < 32; i++) sc->lengths[288 + i] = 5U;
                if (!huff_build(&sc->lit_h, sc->lengths, 288) ||
                    !huff_build(&sc->dist_h, sc->lengths + 288, 32)) {
                    free(sc);
                    return false;
                }
            } else {
                uint32_t hlit_r = 0U, hdist_r = 0U, hclen_r = 0U;
                if (bs_read_bits(&bs, 5, &hlit_r) != 0 ||
                    bs_read_bits(&bs, 5, &hdist_r) != 0 ||
                    bs_read_bits(&bs, 4, &hclen_r) != 0) {
                    free(sc);
                    return false;
                }
                int hlit = (int)hlit_r + 257;
                int hdist = (int)hdist_r + 1;
                int hclen = (int)hclen_r + 4;
                if (hlit > 286 || hdist > 30) { free(sc); return false; }

                memset(sc->cl_lens, 0, sizeof(sc->cl_lens));
                for (int i = 0; i < hclen; i++) {
                    uint32_t v = 0U;
                    if (bs_read_bits(&bs, 3, &v) != 0) { free(sc); return false; }
                    sc->cl_lens[cl_order[i]] = (uint8_t)v;
                }
                if (!huff_build(&sc->cl_h, sc->cl_lens, 19)) { free(sc); return false; }

                int total_codes = hlit + hdist;
                int idx = 0;
                while (idx < total_codes) {
                    int sym = 0;
                    if (huff_decode(&bs, &sc->cl_h, &sym) != 0) { free(sc); return false; }
                    if (sym < 16) {
                        sc->lengths[idx++] = (uint8_t)sym;
                    } else {
                        uint8_t rep_val = 0U;
                        int rep_cnt = 0;
                        uint32_t extra = 0U;
                        if (sym == 16) {
                            if (idx == 0 || bs_read_bits(&bs, 2, &extra) != 0) { free(sc); return false; }
                            rep_val = sc->lengths[idx - 1];
                            rep_cnt = 3 + (int)extra;
                        } else if (sym == 17) {
                            if (bs_read_bits(&bs, 3, &extra) != 0) { free(sc); return false; }
                            rep_cnt = 3 + (int)extra;
                        } else {
                            if (bs_read_bits(&bs, 7, &extra) != 0) { free(sc); return false; }
                            rep_cnt = 11 + (int)extra;
                        }
                        if (idx + rep_cnt > total_codes) { free(sc); return false; }
                        for (int r = 0; r < rep_cnt; r++) sc->lengths[idx++] = rep_val;
                    }
                }
                if (!huff_build(&sc->lit_h, sc->lengths, hlit) ||
                    !huff_build(&sc->dist_h, sc->lengths + hlit, hdist)) {
                    free(sc);
                    return false;
                }
            }

            size_t next_pump_pos = out_pos + 2048U;
            for (;;) {
                int sym = 0;
                if (huff_decode(&bs, &sc->lit_h, &sym) != 0) { free(sc); return false; }
                if (sym < 256) {
                    if (out_pos >= dst_cap) { free(sc); return false; }
                    dst[out_pos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else {
                    int lidx = sym - 257;
                    if (lidx < 0 || lidx >= 29) { free(sc); return false; }
                    uint32_t lex = 0U;
                    if (len_extra[lidx] > 0 && bs_read_bits(&bs, len_extra[lidx], &lex) != 0) { free(sc); return false; }
                    size_t mlen = (size_t)len_base[lidx] + (size_t)lex;

                    int dsym = 0;
                    if (huff_decode(&bs, &sc->dist_h, &dsym) != 0 || dsym < 0 || dsym >= 30) { free(sc); return false; }
                    uint32_t dex = 0U;
                    if (dist_extra[dsym] > 0 && bs_read_bits(&bs, dist_extra[dsym], &dex) != 0) { free(sc); return false; }
                    size_t mdist = (size_t)dist_base[dsym] + (size_t)dex;
                    if (mdist == 0U || mdist > out_pos || out_pos + mlen > dst_cap) { free(sc); return false; }
                    for (size_t k = 0U; k < mlen; k++) {
                        dst[out_pos] = dst[out_pos - mdist];
                        out_pos++;
                    }
                }
                if (out_pos >= next_pump_pos) {
                    pump_icon_decode_progress();
                    next_pump_pos = out_pos + 2048U;
                }
            }
        } else {
            free(sc);
            return false;
        }
    } while (!bfinal);

    free(sc);
    *out_len = out_pos;
    return true;
}

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static bool png_unfilter_pass(uint8_t *raw, int pw, int ph, int bpp_bytes, int stride) {
    uint8_t *prev = NULL;
    for (int y = 0; y < ph; y++) {
        if ((y & 31) == 0) pump_icon_decode_progress();
        uint8_t *row = raw + (size_t)y * (size_t)(1 + stride);
        uint8_t ftype = row[0];
        uint8_t *pix = row + 1;
        if (ftype > 4U) return false;
        for (int i = 0; i < stride; i++) {
            uint8_t a = (i >= bpp_bytes) ? pix[i - bpp_bytes] : 0U;
            uint8_t b = prev ? prev[i] : 0U;
            uint8_t c = (prev && i >= bpp_bytes) ? prev[i - bpp_bytes] : 0U;
            if (ftype == 1U) pix[i] = (uint8_t)(pix[i] + a);
            else if (ftype == 2U) pix[i] = (uint8_t)(pix[i] + b);
            else if (ftype == 3U) pix[i] = (uint8_t)(pix[i] + (((unsigned)a + (unsigned)b) >> 1));
            else if (ftype == 4U) pix[i] = (uint8_t)(pix[i] + png_paeth(a, b, c));
        }
        prev = pix;
    }
    (void)pw;
    return true;
}

static uint8_t png_sample_at(const uint8_t *row, int x, int bit_depth, int ch_idx, int n_ch) {
    if (bit_depth == 8) {
        return row[x * n_ch + ch_idx];
    } else if (bit_depth == 16) {
        return row[(x * n_ch + ch_idx) * 2];
    } else {
        int bit_off = (x * n_ch + ch_idx) * bit_depth;
        uint8_t byte_v = row[bit_off >> 3];
        int shift = 8 - bit_depth - (bit_off & 7);
        uint8_t mask = (uint8_t)((1U << bit_depth) - 1U);
        return (uint8_t)((byte_v >> shift) & mask);
    }
}

static bool decode_png_memory_to_game_icon(const uint8_t *png, size_t png_len, nl_game_t *game) {
    static const uint8_t sig[8] = {0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    if (!png || png_len < 33U || memcmp(png, sig, 8U) != 0) return false;

    uint32_t w = 0U, h = 0U;
    uint8_t bit_depth = 0U, color_type = 0U, interlace = 0U;
    bool have_ihdr = false, have_iend = false;

    uint8_t *plte = (uint8_t *)calloc(256U, 3U);
    uint8_t *trns_alpha = (uint8_t *)malloc(256U);
    if (!plte || !trns_alpha) {
        free(plte);
        free(trns_alpha);
        return false;
    }
    int plte_cnt = 0;
    memset(trns_alpha, 255U, 256U);
    bool have_trns_key = false;
    uint16_t trns_r = 0U, trns_g = 0U, trns_b = 0U;

    uint8_t *idat_buf = NULL;
    size_t idat_len = 0U;
    size_t idat_cap = 0U;

    size_t pos = 8U;
    while (pos + 12U <= png_len) {
        uint32_t clen = read_u32_be(png + pos);
        const uint8_t *ctype = png + pos + 4U;
        if ((uint64_t)pos + 12ULL + (uint64_t)clen > (uint64_t)png_len) {
            free(plte); free(trns_alpha); free(idat_buf);
            return false;
        }
        const uint8_t *cdata = png + pos + 8U;
        if (memcmp(ctype, "IHDR", 4) == 0) {
            if (clen != 13U) { free(plte); free(trns_alpha); free(idat_buf); return false; }
            w = read_u32_be(cdata + 0);
            h = read_u32_be(cdata + 4);
            bit_depth = cdata[8];
            color_type = cdata[9];
            if (w == 0U || w > 1024U || h == 0U || h > 1024U) { free(plte); free(trns_alpha); free(idat_buf); return false; }
            if (cdata[10] != 0U || cdata[11] != 0U || cdata[12] > 1U) { free(plte); free(trns_alpha); free(idat_buf); return false; }
            interlace = cdata[12];
            have_ihdr = true;
        } else if (memcmp(ctype, "PLTE", 4) == 0) {
            if (clen % 3U != 0U || clen > 768U) { free(plte); free(trns_alpha); free(idat_buf); return false; }
            plte_cnt = (int)(clen / 3U);
            memcpy(plte, cdata, clen);
        } else if (memcmp(ctype, "tRNS", 4) == 0) {
            if (color_type == 3U) {
                int n = (clen > 256U) ? 256 : (int)clen;
                memcpy(trns_alpha, cdata, (size_t)n);
            } else if (color_type == 0U && clen >= 2U) {
                have_trns_key = true;
                trns_r = (uint16_t)(((uint16_t)cdata[0] << 8) | cdata[1]);
            } else if (color_type == 2U && clen >= 6U) {
                have_trns_key = true;
                trns_r = (uint16_t)(((uint16_t)cdata[0] << 8) | cdata[1]);
                trns_g = (uint16_t)(((uint16_t)cdata[2] << 8) | cdata[3]);
                trns_b = (uint16_t)(((uint16_t)cdata[4] << 8) | cdata[5]);
            }
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            if ((uint64_t)idat_len + (uint64_t)clen > 1024ULL * 1024ULL) {
                free(plte); free(trns_alpha); free(idat_buf);
                return false;
            }
            if (idat_len + (size_t)clen > idat_cap) {
                size_t new_cap = (idat_cap == 0U) ? 8192U : idat_cap * 2U;
                while (new_cap < idat_len + (size_t)clen) new_cap *= 2U;
                uint8_t *nb = (uint8_t *)realloc(idat_buf, new_cap);
                if (!nb) { free(plte); free(trns_alpha); free(idat_buf); return false; }
                idat_buf = nb;
                idat_cap = new_cap;
            }
            memcpy(idat_buf + idat_len, cdata, clen);
            idat_len += (size_t)clen;
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            have_iend = true;
            break;
        }
        pos += 12U + (size_t)clen;
    }

    if (!have_ihdr || !have_iend || !idat_buf || idat_len == 0U) {
        free(plte); free(trns_alpha); free(idat_buf);
        return false;
    }

    /* Strict RFC 2083 (color_type, bit_depth) validation */
    int n_ch = 0;
    bool bd_ok = false;
    if (color_type == 0U) {
        n_ch = 1;
        bd_ok = (bit_depth == 1U || bit_depth == 2U || bit_depth == 4U || bit_depth == 8U || bit_depth == 16U);
    } else if (color_type == 2U) {
        n_ch = 3;
        bd_ok = (bit_depth == 8U || bit_depth == 16U);
    } else if (color_type == 3U) {
        n_ch = 1;
        bd_ok = (plte_cnt > 0) && (bit_depth == 1U || bit_depth == 2U || bit_depth == 4U || bit_depth == 8U);
    } else if (color_type == 4U) {
        n_ch = 2;
        bd_ok = (bit_depth == 8U || bit_depth == 16U);
    } else if (color_type == 6U) {
        n_ch = 4;
        bd_ok = (bit_depth == 8U || bit_depth == 16U);
    }
    if (!bd_ok || n_ch <= 0) {
        free(plte); free(trns_alpha); free(idat_buf);
        return false;
    }

    /* Normalize 16-bit tRNS key to 8-bit sample representation used by png_sample_at */
    uint8_t key_r8 = (bit_depth == 16U) ? (uint8_t)(trns_r >> 8) : (uint8_t)(trns_r & 0xFFU);
    uint8_t key_g8 = (bit_depth == 16U) ? (uint8_t)(trns_g >> 8) : (uint8_t)(trns_g & 0xFFU);
    uint8_t key_b8 = (bit_depth == 16U) ? (uint8_t)(trns_b >> 8) : (uint8_t)(trns_b & 0xFFU);

    size_t max_raw_bytes = (size_t)h * (1U + ((size_t)w * (size_t)n_ch * (size_t)bit_depth + 7U) / 8U) +
                           (interlace ? (size_t)h * 7U + 64U : 0U);

    /* For very large PNGs (> 512px), downsample during decode into <= 256x256 work RGBA to stay RAM-safe on CX II-T */
    int work_w = (int)w;
    int work_h = (int)h;
    bool stream_downscale = (w > 512U || h > 512U);
    if (stream_downscale) {
        if (w >= h) {
            work_w = 256;
            work_h = (int)(((int64_t)h * 256 + ((int64_t)w / 2)) / (int64_t)w);
        } else {
            work_h = 256;
            work_w = (int)(((int64_t)w * 256 + ((int64_t)h / 2)) / (int64_t)h);
        }
        if (work_w < 1) work_w = 1;
        if (work_h < 1) work_h = 1;
        if (work_w > 256) work_w = 256;
        if (work_h > 256) work_h = 256;
    }
    size_t rgba_bytes = (size_t)work_w * (size_t)work_h * 4U;
    if (max_raw_bytes > 4300U * 1024U || rgba_bytes > 1024U * 1024U) {
        free(plte); free(trns_alpha); free(idat_buf);
        return false;
    }
#ifdef __arm__
    {
        void *headroom = malloc(max_raw_bytes + rgba_bytes + 524288U);
        if (!headroom) {
            free(plte); free(trns_alpha); free(idat_buf);
            return false;
        }
        free(headroom);
    }
#endif
    uint8_t *raw = (uint8_t *)malloc(max_raw_bytes);
    uint8_t *rgba = (uint8_t *)calloc((size_t)work_w * (size_t)work_h, 4U);
    if (!raw || !rgba) {
        free(plte); free(trns_alpha); free(idat_buf);
        free(raw);
        free(rgba);
        return false;
    }

    size_t raw_len = 0U;
    if (!inflate_zlib_stream(idat_buf, idat_len, raw, max_raw_bytes, &raw_len)) {
        free(plte); free(trns_alpha); free(idat_buf);
        free(raw);
        free(rgba);
        return false;
    }
    free(idat_buf);

    int bpp_bytes = ((int)n_ch * (int)bit_depth + 7) / 8;
    if (bpp_bytes < 1) bpp_bytes = 1;

    static const int a7_x0[7] = {0, 4, 0, 2, 0, 1, 0};
    static const int a7_y0[7] = {0, 0, 4, 0, 2, 0, 1};
    static const int a7_dx[7] = {8, 8, 4, 4, 2, 2, 1};
    static const int a7_dy[7] = {8, 8, 8, 4, 4, 2, 2};
    int num_passes = interlace ? 7 : 1;
    size_t raw_off = 0U;

    if (stream_downscale && !interlace) {
        int stride = ((int)w * n_ch * (int)bit_depth + 7) / 8;
        size_t pass_bytes = (size_t)h * (size_t)(1 + stride);
        if (pass_bytes > raw_len || !png_unfilter_pass(raw, (int)w, (int)h, bpp_bytes, stride)) {
            free(plte); free(trns_alpha); free(raw); free(rgba);
            return false;
        }
        uint8_t scale_max = (bit_depth < 8U) ? (uint8_t)((1U << bit_depth) - 1U) : 255U;
        for (int wy = 0; wy < work_h; wy++) {
            int sy0 = (int)(((int64_t)wy * (int64_t)h) / work_h);
            int sy1 = (int)(((int64_t)(wy + 1) * (int64_t)h + work_h - 1) / work_h);
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sy1 > (int)h) sy1 = (int)h;
            for (int wx = 0; wx < work_w; wx++) {
                int sx0 = (int)(((int64_t)wx * (int64_t)w) / work_w);
                int sx1 = (int)(((int64_t)(wx + 1) * (int64_t)w + work_w - 1) / work_w);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sx1 > (int)w) sx1 = (int)w;
                uint32_t ar = 0U, ag = 0U, ab = 0U, aa = 0U, cnt = 0U;
                for (int sy = sy0; sy < sy1; sy++) {
                    const uint8_t *row_pix = raw + (size_t)sy * (size_t)(1 + stride) + 1U;
                    for (int sx = sx0; sx < sx1; sx++) {
                        uint8_t r = 0U, g = 0U, b = 0U, a = 255U;
                        if (color_type == 3U) {
                            uint8_t idx = png_sample_at(row_pix, sx, bit_depth, 0, 1);
                            if ((int)idx < plte_cnt) {
                                r = plte[idx * 3 + 0]; g = plte[idx * 3 + 1]; b = plte[idx * 3 + 2];
                                a = trns_alpha[idx];
                            }
                        } else if (color_type == 0U || color_type == 4U) {
                            uint8_t v = png_sample_at(row_pix, sx, bit_depth, 0, n_ch);
                            uint8_t g8 = (bit_depth < 8U) ? (uint8_t)((v * 255U) / scale_max) : v;
                            r = g = b = g8;
                            if (color_type == 4U) a = png_sample_at(row_pix, sx, bit_depth, 1, 2);
                            else if (have_trns_key && v == key_r8) a = 0U;
                        } else {
                            r = png_sample_at(row_pix, sx, bit_depth, 0, n_ch);
                            g = png_sample_at(row_pix, sx, bit_depth, 1, n_ch);
                            b = png_sample_at(row_pix, sx, bit_depth, 2, n_ch);
                            if (color_type == 6U) a = png_sample_at(row_pix, sx, bit_depth, 3, 4);
                            else if (have_trns_key && r == key_r8 && g == key_g8 && b == key_b8) a = 0U;
                        }
                        ar += (uint32_t)r * (uint32_t)a;
                        ag += (uint32_t)g * (uint32_t)a;
                        ab += (uint32_t)b * (uint32_t)a;
                        aa += (uint32_t)a;
                        cnt++;
                    }
                }
                if (cnt == 0U) cnt = 1U;
                uint8_t *out = rgba + ((size_t)wy * (size_t)work_w + (size_t)wx) * 4U;
                out[3] = (uint8_t)((aa + cnt / 2U) / cnt);
                if (aa > 0U) {
                    out[0] = (uint8_t)((ar + aa / 2U) / aa);
                    out[1] = (uint8_t)((ag + aa / 2U) / aa);
                    out[2] = (uint8_t)((ab + aa / 2U) / aa);
                } else {
                    out[0] = out[1] = out[2] = 0U;
                }
            }
        }
    } else {
        for (int pass = 0; pass < num_passes; pass++) {
            int px0 = interlace ? a7_x0[pass] : 0;
            int py0 = interlace ? a7_y0[pass] : 0;
            int pdx = interlace ? a7_dx[pass] : 1;
            int pdy = interlace ? a7_dy[pass] : 1;
            int pw = ((int)w > px0) ? (((int)w - px0 + pdx - 1) / pdx) : 0;
            int ph = ((int)h > py0) ? (((int)h - py0 + pdy - 1) / pdy) : 0;
            if (pw <= 0 || ph <= 0) continue;

            int stride = (pw * n_ch * (int)bit_depth + 7) / 8;
            size_t pass_bytes = (size_t)ph * (size_t)(1 + stride);
            if (raw_off + pass_bytes > raw_len ||
                !png_unfilter_pass(raw + raw_off, pw, ph, bpp_bytes, stride)) {
                free(plte); free(trns_alpha);
                free(raw);
                free(rgba);
                return false;
            }

            /* #R19 Fast row unpack for standard non-interlaced 8-bit RGBA/RGB PNGs (e.g. 144x144 icons) */
            if (!interlace && !stream_downscale && bit_depth == 8U && color_type == 6U) {
                for (int py = 0; py < ph; py++) {
                    const uint8_t *row_pix = raw + raw_off + (size_t)py * (size_t)(1 + stride) + 1U;
                    memcpy(rgba + (size_t)py * (size_t)work_w * 4U, row_pix, (size_t)pw * 4U);
                }
            } else if (!interlace && !stream_downscale && bit_depth == 8U && color_type == 2U && !have_trns_key) {
                for (int py = 0; py < ph; py++) {
                    const uint8_t *src = raw + raw_off + (size_t)py * (size_t)(1 + stride) + 1U;
                    uint8_t *dst = rgba + (size_t)py * (size_t)work_w * 4U;
                    for (int px = 0; px < pw; px++, src += 3, dst += 4) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = 255U;
                    }
                }
            } else {
                uint8_t scale_max = (bit_depth < 8U) ? (uint8_t)((1U << bit_depth) - 1U) : 255U;
                for (int py = 0; py < ph; py++) {
                    const uint8_t *row_pix = raw + raw_off + (size_t)py * (size_t)(1 + stride) + 1U;
                    int dy = py0 + py * pdy;
                    int wy = stream_downscale ? (int)(((int64_t)dy * work_h) / (int64_t)h) : dy;
                    if (wy >= work_h) wy = work_h - 1;
                    for (int px = 0; px < pw; px++) {
                        int dx = px0 + px * pdx;
                        int wx = stream_downscale ? (int)(((int64_t)dx * work_w) / (int64_t)w) : dx;
                        if (wx >= work_w) wx = work_w - 1;
                        uint8_t r = 0U, g = 0U, b = 0U, a = 255U;
                        if (color_type == 3U) {
                            uint8_t idx = png_sample_at(row_pix, px, bit_depth, 0, 1);
                            if ((int)idx < plte_cnt) {
                                r = plte[idx * 3 + 0];
                                g = plte[idx * 3 + 1];
                                b = plte[idx * 3 + 2];
                                a = trns_alpha[idx];
                            }
                        } else if (color_type == 0U || color_type == 4U) {
                            uint8_t v = png_sample_at(row_pix, px, bit_depth, 0, n_ch);
                            uint8_t g8 = (bit_depth < 8U) ? (uint8_t)((v * 255U) / scale_max) : v;
                            r = g = b = g8;
                            if (color_type == 4U) {
                                a = png_sample_at(row_pix, px, bit_depth, 1, 2);
                            } else if (have_trns_key && v == key_r8) {
                                a = 0U;
                            }
                        } else {
                            r = png_sample_at(row_pix, px, bit_depth, 0, n_ch);
                            g = png_sample_at(row_pix, px, bit_depth, 1, n_ch);
                            b = png_sample_at(row_pix, px, bit_depth, 2, n_ch);
                            if (color_type == 6U) {
                                a = png_sample_at(row_pix, px, bit_depth, 3, 4);
                            } else if (have_trns_key && r == key_r8 && g == key_g8 && b == key_b8) {
                                a = 0U;
                            }
                        }
                        uint8_t *out = rgba + ((size_t)wy * (size_t)work_w + (size_t)wx) * 4U;
                        out[0] = r; out[1] = g; out[2] = b; out[3] = a;
                    }
                }
            }
            raw_off += pass_bytes;
        }
    }

    bool ok = finalize_rgba_into_game_icon(rgba, work_w, work_h, game);
    free(plte);
    free(trns_alpha);
    free(raw);
    free(rgba);
    return ok;
}

/* Direct native standalone PNG loader (.png.tns / .png): zero forced .ico conversion, crisp quality */
static bool load_png_file_scaled(const char *path, nl_game_t *game) {
    if (!path || !game) return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    uint8_t sig_hdr[8];
    static const uint8_t png_sig[8] = {0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    if (fread(sig_hdr, 1U, 8U, fp) != 8U || memcmp(sig_hdr, png_sig, 8U) != 0) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long file_size = ftell(fp);
    if (file_size < 33L || file_size > 1048576L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    if (!buf) {
        fclose(fp);
        return false;
    }
    bool read_ok = (fread(buf, 1U, (size_t)file_size, fp) == (size_t)file_size);
    fclose(fp);
    if (!read_ok) {
        free(buf);
        return false;
    }
    bool ok = decode_png_memory_to_game_icon(buf, (size_t)file_size, game);
    free(buf);
    if (!ok) return false;
    game->icon_source = NL_ICON_PNG;
    return true;
}

/* Load 1/4/8/16/24/32-bit BMP (.bmp.tns or .bmp) preserving exact proportions */
static bool load_bmp_file_scaled(const char *path, nl_game_t *game) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long file_size = ftell(fp);
    if (file_size < 54L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    uint8_t hdr[54];
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        return false;
    }
    if (hdr[0] != 'B' || hdr[1] != 'M') {
        fclose(fp);
        return false;
    }

    uint32_t pixel_offset = read_u32_le(hdr + 10);
    uint32_t bi_size      = read_u32_le(hdr + 14);
    int32_t  bmp_w        = (int32_t)read_u32_le(hdr + 18);
    int32_t  bmp_h_signed = (int32_t)read_u32_le(hdr + 22);
    uint16_t bpp          = read_u16_le(hdr + 28);
    uint32_t compression  = read_u32_le(hdr + 30);
    uint32_t clr_used     = read_u32_le(hdr + 46);

    bool top_down = (bmp_h_signed < 0);
    int64_t bmp_h64 = top_down ? -(int64_t)bmp_h_signed : (int64_t)bmp_h_signed;

    if (bmp_w <= 0 || bmp_w > 512 || bmp_h64 <= 0 || bmp_h64 > 512 || file_size > 1048576L) {
        fclose(fp);
        return false;
    }
    if (bi_size < 40U || bi_size > 124U ||
        !(bpp == 1U || bpp == 4U || bpp == 8U || bpp == 16U || bpp == 24U || bpp == 32U) ||
        (compression != 0U && !(compression == 3U && (bpp == 16U || bpp == 32U)))) {
        fclose(fp);
        return false;
    }

    uint32_t pal_offset = 14U + bi_size;
    bool ok = decode_dib_into_game_icon(fp, pixel_offset, pal_offset, clr_used,
                                        (int)bmp_w, (int)bmp_h64,
                                        bpp, compression, top_down, false, file_size, game);
    fclose(fp);
    if (!ok) return false;
    game->icon_source = NL_ICON_BMP;
    return true;
}

/* Load genuine Windows .ico (1/4/8/16/24/32-bit DIB + AND mask AND PNG-compressed entries) */
static bool load_ico_file_scaled(const char *path, nl_game_t *game) {
    if (!path || !game) return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long file_size = ftell(fp);
    if (file_size < 46L || file_size > 1048576L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    uint8_t ico_hdr[6];
    if (fread(ico_hdr, 1, sizeof(ico_hdr), fp) != sizeof(ico_hdr)) {
        fclose(fp);
        return false;
    }

    uint16_t reserved = read_u16_le(ico_hdr + 0);
    uint16_t ico_type = read_u16_le(ico_hdr + 2);
    uint16_t count    = read_u16_le(ico_hdr + 4);

    if (reserved != 0U || ico_type != 1U || count == 0U || count > 64U) {
        fclose(fp);
        return false;
    }

    long min_data_off = 6L + (long)count * 16L;
    if (file_size < min_data_off + 24L) {
        fclose(fp);
        return false;
    }

    typedef struct {
        int      score;
        bool     is_png;
        uint32_t img_offset;
        uint32_t bytes_in_res;
        uint32_t pixel_offset;
        uint32_t pal_offset;
        uint32_t clr_used;
        int32_t  w;
        int32_t  h;
        uint16_t bpp;
        uint32_t comp;
        bool     top_down;
    } ico_cand_t;

    const int max_cands = (count < 32U) ? (int)count : 32;
    ico_cand_t *cands = (ico_cand_t *)calloc((size_t)max_cands, sizeof(ico_cand_t));
    if (!cands) {
        fclose(fp);
        return false;
    }
    int n_cands = 0;

    for (uint16_t idx = 0; idx < count && n_cands < max_cands; idx++) {
        if (fseek(fp, 6L + (long)idx * 16L, SEEK_SET) != 0) break;
        uint8_t dir_ent[16];
        if (fread(dir_ent, 1, sizeof(dir_ent), fp) != sizeof(dir_ent)) break;

        int ent_w = (dir_ent[0] == 0) ? 256 : (int)dir_ent[0];
        int ent_h = (dir_ent[1] == 0) ? 256 : (int)dir_ent[1];
        uint16_t ent_bpp = read_u16_le(dir_ent + 6);
        uint32_t bytes_in_res = read_u32_le(dir_ent + 8);
        uint32_t img_offset   = read_u32_le(dir_ent + 12);

        if ((uint64_t)img_offset < (uint64_t)min_data_off ||
            (uint64_t)img_offset + 24ULL > (uint64_t)file_size) {
            continue;
        }
        if (bytes_in_res < 24U || (uint64_t)img_offset + (uint64_t)bytes_in_res > (uint64_t)file_size) {
            if ((uint64_t)img_offset + 40ULL > (uint64_t)file_size) {
                continue;
            }
            bytes_in_res = (uint32_t)((uint64_t)file_size - (uint64_t)img_offset);
        }
        if (bytes_in_res > 524288U) {
            continue;
        }

        if (fseek(fp, (long)img_offset, SEEK_SET) != 0) continue;
        uint8_t dib[40];
        size_t hdr_read = fread(dib, 1, sizeof(dib), fp);
        if (hdr_read < 24U) continue;

        /* PNG-compressed .ico entry (\x89PNG\r\n\x1a\n) */
        if (dib[0] == 0x89U && dib[1] == 'P' && dib[2] == 'N' && dib[3] == 'G' &&
            dib[4] == 0x0DU && dib[5] == 0x0AU && dib[6] == 0x1AU && dib[7] == 0x0AU &&
            bytes_in_res >= 33U && memcmp(dib + 12, "IHDR", 4) == 0) {
            uint32_t pw = read_u32_be(dib + 16);
            uint32_t ph = read_u32_be(dib + 20);
            if (pw == 0U || pw > 512U || ph == 0U || ph > 512U) continue;
            uint16_t pbpp = (ent_bpp > 0U) ? ent_bpp : 32U;
            int min_dim = (pw < ph) ? (int)pw : (int)ph;
            int res_score = (min_dim >= NL_ICON_MAX_DIM)
                ? (NL_ICON_MAX_DIM * NL_ICON_MAX_DIM - (((int)pw > NL_ICON_MAX_DIM) ? ((int)pw - NL_ICON_MAX_DIM) : 0))
                : ((int)pw * (int)ph);
            ico_cand_t *c = &cands[n_cands++];
            memset(c, 0, sizeof(*c));
            c->score = res_score * 16 + (int)pbpp * 100 + ((pw == ph) ? 2000 : 0);
            c->is_png = true;
            c->img_offset = img_offset;
            c->bytes_in_res = bytes_in_res;
            continue;
        }

        if (hdr_read < 40U) continue;
        uint32_t bi_size      = read_u32_le(dib + 0);
        int32_t  bi_w         = (int32_t)read_u32_le(dib + 4);
        int32_t  bi_h_signed  = (int32_t)read_u32_le(dib + 8);
        uint16_t bi_planes    = read_u16_le(dib + 12);
        uint16_t bpp          = read_u16_le(dib + 14);
        uint32_t compression  = read_u32_le(dib + 16);
        uint32_t clr_used     = read_u32_le(dib + 32);

        if (bi_size < 40U || bi_size > 124U || bi_planes != 1U) continue;
        if (!(bpp == 1U || bpp == 4U || bpp == 8U || bpp == 16U || bpp == 24U || bpp == 32U)) continue;
        if (!(compression == 0U || (compression == 3U && (bpp == 16U || bpp == 32U)))) continue;
        if (bi_w <= 0 || bi_w > 512) continue;
        (void)ent_w;

        bool top_down = (bi_h_signed < 0);
        int64_t abs_h = top_down ? -(int64_t)bi_h_signed : (int64_t)bi_h_signed;
        int64_t ico_h = (ent_h > 0 && abs_h == (int64_t)ent_h) ? (int64_t)ent_h : (abs_h / 2);
        if (ico_h <= 0 || ico_h > 512) continue;

        uint64_t extra_masks = (compression == 3U && bi_size == 40U) ? 12ULL : 0ULL;
        uint64_t pal_offset64 = (uint64_t)img_offset + (uint64_t)bi_size + extra_masks;
        uint32_t pal_count = 0U;
        if (bpp <= 8U) {
            uint32_t max_pal = 1U << bpp;
            pal_count = (clr_used > 0U && clr_used <= max_pal) ? clr_used : max_pal;
        }
        uint64_t pixel_offset64 = pal_offset64 + (uint64_t)pal_count * 4ULL;
        uint32_t xor_stride = (((uint32_t)bi_w * (uint32_t)bpp + 31U) >> 5) * 4U;
        if (pixel_offset64 + (uint64_t)ico_h * (uint64_t)xor_stride > (uint64_t)file_size) {
            continue;
        }

        int min_dim = (bi_w < (int)ico_h) ? bi_w : (int)ico_h;
        int res_score = (min_dim >= NL_ICON_MAX_DIM)
            ? (NL_ICON_MAX_DIM * NL_ICON_MAX_DIM - ((bi_w > NL_ICON_MAX_DIM) ? (bi_w - NL_ICON_MAX_DIM) : 0))
            : (bi_w * (int)ico_h);
        ico_cand_t *c = &cands[n_cands++];
        memset(c, 0, sizeof(*c));
        c->score = res_score * 16 + (int)bpp * 100 + ((bi_w == (int)ico_h) ? 2000 : 0);
        c->is_png = false;
        c->img_offset = img_offset;
        c->bytes_in_res = bytes_in_res;
        c->pixel_offset = (uint32_t)pixel_offset64;
        c->pal_offset = (uint32_t)pal_offset64;
        c->clr_used = clr_used;
        c->w = bi_w;
        c->h = (int32_t)ico_h;
        c->bpp = bpp;
        c->comp = compression;
        c->top_down = top_down;
    }

    /* Sort candidates descending by score and try decoding best valid one */
    for (int i = 0; i < n_cands; i++) {
        for (int j = i + 1; j < n_cands; j++) {
            if (cands[j].score > cands[i].score) {
                ico_cand_t tmp = cands[i];
                cands[i] = cands[j];
                cands[j] = tmp;
            }
        }
    }

    bool ok = false;
    for (int i = 0; i < n_cands && !ok; i++) {
        if (cands[i].is_png) {
            uint8_t *png_buf = (uint8_t *)malloc(cands[i].bytes_in_res);
            if (!png_buf) continue;
            if (fseek(fp, (long)cands[i].img_offset, SEEK_SET) == 0 &&
                fread(png_buf, 1U, cands[i].bytes_in_res, fp) == cands[i].bytes_in_res) {
                ok = decode_png_memory_to_game_icon(png_buf, cands[i].bytes_in_res, game);
            }
            free(png_buf);
        } else {
            ok = decode_dib_into_game_icon(fp, cands[i].pixel_offset, cands[i].pal_offset,
                                           cands[i].clr_used, (int)cands[i].w, (int)cands[i].h,
                                           cands[i].bpp, cands[i].comp, cands[i].top_down,
                                           true, file_size, game);
        }
    }

    free(cands);
    fclose(fp);
    if (!ok) return false;
    game->icon_source = NL_ICON_ICO;
    return true;
}

static void derive_icons_dir_from_covers(const char *covers_dir, char *out_icons_dir, size_t max_len) {
    if (!out_icons_dir || max_len == 0) return;
    out_icons_dir[0] = '\0';
    if (!covers_dir || covers_dir[0] == '\0') {
        snprintf(out_icons_dir, max_len, "/documents/ndless/icons");
        return;
    }
    size_t len = strlen(covers_dir);
    while (len > 1 && (covers_dir[len - 1] == '/' || covers_dir[len - 1] == '\\')) {
        len--;
    }
    if (len >= 7 && str_icmp_c(covers_dir + len - 7, "/covers") == 0) {
        size_t prefix_len = len - 7;
        snprintf(out_icons_dir, max_len, "%.*s/icons", (int)prefix_len, covers_dir);
    } else {
        snprintf(out_icons_dir, max_len, "%.*s/icons", (int)len, covers_dir);
    }
}

#define NL_ICON_EXT_COUNT 6
static const char *const G_ICON_EXTS[NL_ICON_EXT_COUNT] = {
    ".png.tns", ".png", ".ico.tns", ".ico", ".bmp.tns", ".bmp"
};

static bool load_custom_icon_candidate(const char *path, nl_game_t *game) {
    if (!path || !game) return false;
    if (load_png_file_scaled(path, game)) return true;
    if (load_ico_file_scaled(path, game)) return true;
    if (load_bmp_file_scaled(path, game)) return true;
    if (load_nlic_file(path, game)) return true;
    return false;
}

static bool load_icon_from_dir_ci(const char *dir_path, const char *stem, nl_game_t *game) {
    if (!dir_path || dir_path[0] == '\0' || !stem || stem[0] == '\0') return false;
    char candidate[NL_MAX_PATH_LEN * 2];
    for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
        snprintf(candidate, sizeof(candidate), "%s/%s%s", dir_path, stem, G_ICON_EXTS[i]);
        if (load_custom_icon_candidate(candidate, game)) return true;
    }
    DIR *dp = opendir(dir_path);
    if (!dp) return false;
    char target_names[NL_ICON_EXT_COUNT][NL_MAX_NAME_LEN + 16];
    for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
        snprintf(target_names[i], sizeof(target_names[i]), "%s%s", stem, G_ICON_EXTS[i]);
    }
    struct dirent *de;
    bool loaded = false;
    while (!loaded && (de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;
        for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
            if (str_icmp_c(de->d_name, target_names[i]) == 0) {
                snprintf(candidate, sizeof(candidate), "%s/%s", dir_path, de->d_name);
                if (load_custom_icon_candidate(candidate, game)) {
                    loaded = true;
                    break;
                }
            }
        }
    }
    closedir(dp);
    return loaded;
}

bool nl_load_game_icon(nl_game_t *game, const char *covers_dir) {
    if (!game) return false;

    char stem[NL_MAX_NAME_LEN];
    nl_extract_game_stem(game->filename, stem, sizeof(stem));

    char candidate[NL_MAX_PATH_LEN * 2];
    char icons_dir[NL_MAX_PATH_LEN];
    derive_icons_dir_from_covers(covers_dir, icons_dir, sizeof(icons_dir));

    /* 1. Custom .png.tns / .png / .ico.tns / .ico / .bmp.tns / .bmp inside /documents/ndless/icons/ */
    if (icons_dir[0] != '\0' && load_icon_from_dir_ci(icons_dir, stem, game)) {
        return true;
    }

    /* 2. Custom icon next to executable */
    for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
        build_sidecar_path(game->path, G_ICON_EXTS[i], candidate, sizeof(candidate));
        if (load_custom_icon_candidate(candidate, game)) return true;
    }

    /* 3. Custom icon inside /documents/ndless/covers/ (never .scr.tns!) */
    if (covers_dir && covers_dir[0] != '\0' && load_icon_from_dir_ci(covers_dir, stem, game)) {
        return true;
    }

    return false;
}

bool nl_screenshot_is_valid_buffer(const uint16_t *pixels) {
    if (!pixels) return false;
    uint16_t first = pixels[0];
    int non_black = 0;
    int diff_from_first = 0;
    for (int i = 0; i < NL_COVER_PIXELS; i += 3) {
        uint16_t px = pixels[i];
        if (px != first) diff_from_first++;
        if (px == 0U || px == NL_VRAM_SENTINEL) continue;
        unsigned r = (unsigned)((px >> 11) & 0x1FU);
        unsigned g = (unsigned)((px >> 5)  & 0x3FU);
        unsigned b = (unsigned)(px & 0x1FU);
        if (r >= 2U || g >= 4U || b >= 2U) {
            non_black++;
        }
    }
    /* Reject all-black, sentinel-only, and flat single-color blank screens */
    return (non_black >= (NL_COVER_PIXELS / 150)) && (diff_from_first >= 64);
}

static bool validate_nlic_file_on_disk(const char *path) {
    if (!path || path[0] == '\0') return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    uint8_t hdr[16];
    if (fread(hdr, 1, 16, fp) != 16) {
        fclose(fp);
        return false;
    }
    uint32_t magic = read_u32_le(hdr + 0);
    uint16_t ver   = read_u16_le(hdr + 4);
    uint16_t w     = read_u16_le(hdr + 6);
    uint16_t h     = read_u16_le(hdr + 8);
    if (magic != NL_ICON_MAGIC || ver != 1U ||
        !((w == NL_COVER_W && h == NL_COVER_H) || (w == NL_ICON_V1_W && h == NL_ICON_V1_H))) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long fsz = ftell(fp);
    fclose(fp);
    long expected = 16L + (long)w * (long)h * 2L;
    return (fsz >= expected);
}

static bool find_screenshot_path_ci(const nl_game_t *game, const char *covers_dir, char *out_path, size_t max_len) {
    if (!game || !out_path || max_len == 0U) return false;
    out_path[0] = '\0';

    char stem[NL_MAX_NAME_LEN];
    nl_extract_game_stem(game->filename, stem, sizeof(stem));
    char candidate[NL_MAX_PATH_LEN];

    if (covers_dir && covers_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s/%s.scr.tns", covers_dir, stem);
        if (validate_nlic_file_on_disk(candidate)) {
            snprintf(out_path, max_len, "%s", candidate);
            return true;
        }
        /* Case-insensitive fallback scan inside covers_dir */
        DIR *dp = opendir(covers_dir);
        if (dp) {
            char target_name[NL_MAX_NAME_LEN + 16];
            snprintf(target_name, sizeof(target_name), "%s.scr.tns", stem);
            struct dirent *de;
            while ((de = readdir(dp)) != NULL) {
                if (de->d_name[0] == '.') continue;
                if (str_icmp_c(de->d_name, target_name) == 0) {
                    snprintf(candidate, sizeof(candidate), "%.*s/%.*s",
                             180, covers_dir, 63, de->d_name);
                    if (validate_nlic_file_on_disk(candidate)) {
                        snprintf(out_path, max_len, "%s", candidate);
                        closedir(dp);
                        return true;
                    }
                }
            }
            closedir(dp);
        }
    }

    build_sidecar_path(game->path, ".scr.tns", candidate, sizeof(candidate));
    if (validate_nlic_file_on_disk(candidate)) {
        snprintf(out_path, max_len, "%s", candidate);
        return true;
    }
    return false;
}

bool nl_game_has_valid_screenshot_on_disk(const nl_game_t *game, const char *covers_dir) {
    char found[NL_MAX_PATH_LEN];
    return find_screenshot_path_ci(game, covers_dir, found, sizeof(found));
}

bool nl_load_game_screenshot(nl_game_t *game, const char *covers_dir) {
    if (!game) return false;
    char found_path[NL_MAX_PATH_LEN];
    if (!find_screenshot_path_ci(game, covers_dir, found_path, sizeof(found_path))) {
        return false;
    }

    uint16_t *tmp_pix = NULL;
    if (!load_nlic_pixels(found_path, &tmp_pix) || !tmp_pix) {
        free(tmp_pix);
        return false;
    }
    if (!nl_screenshot_is_valid_buffer(tmp_pix)) {
        free(tmp_pix);
        return false;
    }
    if (game->scr_pixels) {
        free(game->scr_pixels);
    }
    game->scr_pixels = tmp_pix;
    game->has_screenshot = true;
    return true;
}

static bool path_exists_any_size(const char *p) {
    if (!p || p[0] == '\0') return false;
    FILE *fp = fopen(p, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

static bool find_existing_icon_candidate(
    const nl_game_t *game,
    const char *covers_dir,
    char *out_found_path,
    size_t found_max,
    char *out_expected_path,
    size_t expected_max
) {
    if (!game) return false;
    char stem[NL_MAX_NAME_LEN];
    nl_extract_game_stem(game->filename, stem, sizeof(stem));

    char icons_dir[NL_MAX_PATH_LEN];
    derive_icons_dir_from_covers(covers_dir, icons_dir, sizeof(icons_dir));
    if (out_expected_path && expected_max > 0U) {
        int dir_lim = (expected_max > 64U) ? (int)(expected_max - 64U) : 64;
        snprintf(out_expected_path, expected_max, "%.*s/%.*s.png.tns",
                 dir_lim, icons_dir[0] ? icons_dir : "/documents/ndless/icons",
                 47, stem);
    }

    char candidate[NL_MAX_PATH_LEN * 2];
    const char *dirs[2] = { icons_dir, covers_dir };
    for (int d = 0; d < 2; d++) {
        if (!dirs[d] || dirs[d][0] == '\0') continue;
        for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
            snprintf(candidate, sizeof(candidate), "%.*s/%.*s%s",
                     200, dirs[d], 47, stem, G_ICON_EXTS[i]);
            if (nl_file_exists(candidate)) {
                if (out_found_path && found_max > 0U) {
                    snprintf(out_found_path, found_max, "%.*s", (int)(found_max - 1U), candidate);
                }
                return true;
            }
        }
        DIR *dp = opendir(dirs[d]);
        if (dp) {
            char target_names[NL_ICON_EXT_COUNT][NL_MAX_NAME_LEN + 16];
            for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
                snprintf(target_names[i], sizeof(target_names[i]), "%s%s", stem, G_ICON_EXTS[i]);
            }
            struct dirent *de;
            while ((de = readdir(dp)) != NULL) {
                if (de->d_name[0] == '.') continue;
                for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
                    if (str_icmp_c(de->d_name, target_names[i]) == 0) {
                        snprintf(candidate, sizeof(candidate), "%.*s/%.*s",
                                 200, dirs[d], 120, de->d_name);
                        if (out_found_path && found_max > 0U) {
                            snprintf(out_found_path, found_max, "%.*s", (int)(found_max - 1U), candidate);
                        }
                        closedir(dp);
                        return true;
                    }
                }
            }
            closedir(dp);
        }
    }

    for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
        build_sidecar_path(game->path, G_ICON_EXTS[i], candidate, sizeof(candidate));
        if (path_exists_any_size(candidate)) {
            if (out_found_path && found_max > 0U) {
                snprintf(out_found_path, found_max, "%.*s", (int)(found_max - 1U), candidate);
            }
            return true;
        }
    }
    return false;
}

bool nl_load_game_icon_diag(nl_state_t *state, nl_game_t *game, const char *covers_dir) {
    if (!game) return false;
    if (nl_load_game_icon(game, covers_dir)) {
        return true;
    }
    if (!state) return false;

    char found_path[NL_MAX_PATH_LEN];
    char expected_path[NL_MAX_PATH_LEN];
    found_path[0] = '\0';
    expected_path[0] = '\0';

    if (find_existing_icon_candidate(game, covers_dir, found_path, sizeof(found_path),
                                     expected_path, sizeof(expected_path))) {
        nl_diag_report(
            state,
            NL_DIAG_E103_ICON_CORRUPT,
            game->title[0] ? game->title : game->filename,
            found_path,
            "Decoder icone du jeu",
            "Format .png/.ico/.bmp invalide ou corrompu",
            "Jeu toujours disponible et lancable",
            "Icone ignoree + icone par defaut"
        );
    } else {
        nl_diag_report(
            state,
            NL_DIAG_E102_ICON_MISSING,
            game->title[0] ? game->title : game->filename,
            expected_path,
            "Charger icone du jeu",
            "Fichier icone introuvable",
            "Jeu toujours disponible et lancable",
            "Icone de remplacement utilisee"
        );
    }
    return false;
}

bool nl_load_game_screenshot_diag(nl_state_t *state, nl_game_t *game, const char *covers_dir) {
    if (!game) return false;
    if (nl_load_game_screenshot(game, covers_dir)) {
        return true;
    }
    if (!state) return false;

    char stem[NL_MAX_NAME_LEN];
    nl_extract_game_stem(game->filename, stem, sizeof(stem));

    char cov_cand[NL_MAX_PATH_LEN];
    char side_cand[NL_MAX_PATH_LEN];
    cov_cand[0] = '\0';
    side_cand[0] = '\0';

    if (covers_dir && covers_dir[0] != '\0') {
        snprintf(cov_cand, sizeof(cov_cand), "%s/%s.scr.tns", covers_dir, stem);
    }
    build_sidecar_path(game->path, ".scr.tns", side_cand, sizeof(side_cand));

    if ((cov_cand[0] != '\0' && path_exists_any_size(cov_cand)) ||
        (side_cand[0] != '\0' && path_exists_any_size(side_cand))) {
        const char *bad_p = (cov_cand[0] != '\0' && path_exists_any_size(cov_cand)) ? cov_cand : side_cand;
        nl_diag_report(
            state,
            NL_DIAG_E501_ASSET_INVALID,
            game->title[0] ? game->title : game->filename,
            bad_p,
            "Charger capture .scr.tns",
            "Capture .scr.tns corrompue ou invalide",
            "Apercu graphique ignore",
            "Animation par defaut conservee"
        );
    } else {
        nl_diag_report(
            state,
            NL_DIAG_E104_SCREENSHOT_MISSING,
            game->title[0] ? game->title : game->filename,
            cov_cand[0] ? cov_cand : side_cand,
            "Charger capture d'ecran",
            "Fichier .scr.tns absent",
            "Jeu lancable sans apercu capture",
            "Animation de remplacement utilisee"
        );
    }
    return false;
}

#define NL_ICON_DIR_INDEX_MAX 256

typedef struct {
    char dir[NL_MAX_PATH_LEN];
    char names[NL_ICON_DIR_INDEX_MAX][64];
    int  count;
} nl_icon_dir_index_t;

static void build_icon_dir_index(nl_icon_dir_index_t *idx, const char *dir_path) {
    if (!idx) return;
    idx->dir[0] = '\0';
    idx->count = 0;
    if (!dir_path || dir_path[0] == '\0') return;
    snprintf(idx->dir, sizeof(idx->dir), "%s", dir_path);
    DIR *dp = opendir(dir_path);
    if (!dp) return;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL && idx->count < NL_ICON_DIR_INDEX_MAX) {
        if (de->d_name[0] == '.') continue;
        snprintf(idx->names[idx->count], sizeof(idx->names[idx->count]), "%.*s",
                 (int)(sizeof(idx->names[idx->count]) - 1U), de->d_name);
        idx->count++;
    }
    closedir(dp);
}

static bool find_in_dir_index(const nl_icon_dir_index_t *idx, const char *stem,
                              const char *const *exts, int ext_count, char *out_path, size_t max_len) {
    if (!idx || idx->count <= 0 || !stem || stem[0] == '\0') return false;
    char target[64];
    for (int e = 0; e < ext_count; e++) {
        snprintf(target, sizeof(target), "%.*s%s", 47, stem, exts[e]);
        for (int i = 0; i < idx->count; i++) {
            if (str_icmp_c(idx->names[i], target) == 0) {
                snprintf(out_path, max_len, "%.*s/%.*s",
                         (int)(max_len > 70U ? max_len - 70U : 64U), idx->dir,
                         63, idx->names[i]);
                return true;
            }
        }
    }
    return false;
}

uint32_t nl_game_icon_ram_bytes(const nl_game_t *g) {
    if (!g || !g->icon_pixels || g->icon_w == 0U || g->icon_h == 0U) return 0U;
    uint32_t px = (uint32_t)g->icon_w * (uint32_t)g->icon_h;
    return px * 2U + (g->icon_alpha ? px : 0U);
}

static uint32_t game_icon_ram_bytes(const nl_game_t *g) {
    return nl_game_icon_ram_bytes(g);
}

void nl_probe_all_screenshots_ex(nl_state_t *state, nl_progress_cb_t progress_cb, void *progress_ctx) {
    if (!state || state->total_games <= 0) return;

    /* Single directory listing of covers_dir for O(1) I/O probing across 78+ games (0 KB pixel RAM) */
    nl_icon_dir_index_t *cov_idx = (nl_icon_dir_index_t *)calloc(1U, sizeof(nl_icon_dir_index_t));
    if (cov_idx && state->covers_dir[0] != '\0') {
        build_icon_dir_index(cov_idx, state->covers_dir);
    }
    if (progress_cb) progress_cb(progress_ctx);
    static const char *const k_scr_ext[1] = { ".scr.tns" };

    for (int i = 0; i < state->total_games; i++) {
        if (state->scan.abort_requested || state->scan.cancel_requested) break;
        nl_game_t *g = &state->games[i];
        if (g->has_screenshot && g->scr_pixels != NULL && nl_screenshot_is_valid_buffer(g->scr_pixels)) {
            if (progress_cb && (i & 1) == 0) progress_cb(progress_ctx);
            continue;
        }
        char stem[NL_MAX_NAME_LEN];
        nl_extract_game_stem(g->filename, stem, sizeof(stem));
        char cand[NL_MAX_PATH_LEN];
        bool valid = false;

        if (cov_idx && find_in_dir_index(cov_idx, stem, k_scr_ext, 1, cand, sizeof(cand))) {
            valid = validate_nlic_file_on_disk(cand);
        }
        if (!valid) {
            build_sidecar_path(g->path, ".scr.tns", cand, sizeof(cand));
            valid = validate_nlic_file_on_disk(cand);
        }
        g->has_screenshot = valid;
        if (progress_cb && (i & 1) == 0) progress_cb(progress_ctx);
    }

    free(cov_idx);
}

void nl_probe_all_screenshots(nl_state_t *state) {
    nl_probe_all_screenshots_ex(state, NULL, NULL);
}

void nl_load_all_screenshots(nl_state_t *state) {
    if (!state) return;
    for (int i = 0; i < state->total_games; i++) {
        if (!state->games[i].has_screenshot || !state->games[i].scr_pixels) {
            (void)nl_load_game_screenshot(&state->games[i], state->covers_dir);
        }
    }
}

static bool filename_is_icon_asset(const char *name) {
    if (!name || name[0] == '\0') return false;
    size_t len = strlen(name);
    if (len >= 8U && str_icmp_c(name + len - 8U, ".png.tns") == 0) return true;
    if (len >= 4U && str_icmp_c(name + len - 4U, ".png") == 0) return true;
    if (len >= 8U && str_icmp_c(name + len - 8U, ".ico.tns") == 0) return true;
    if (len >= 4U && str_icmp_c(name + len - 4U, ".ico") == 0) return true;
    if (len >= 8U && str_icmp_c(name + len - 8U, ".bmp.tns") == 0) return true;
    if (len >= 4U && str_icmp_c(name + len - 4U, ".bmp") == 0) return true;
    return false;
}

static bool dir_contains_any_icon_asset(const char *dir_path) {
    if (!dir_path || dir_path[0] == '\0') return false;
    DIR *dp = opendir(dir_path);
    if (!dp) return false;
    bool found = false;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (filename_is_icon_asset(de->d_name)) {
            found = true;
            break;
        }
    }
    closedir(dp);
    return found;
}

static void extract_game_parent_dir_c(const char *filepath, char *out_dir, size_t max_len) {
    if (!out_dir || max_len == 0U) return;
    out_dir[0] = '\0';
    if (!filepath || filepath[0] == '\0') return;
    snprintf(out_dir, max_len, "%s", filepath);
    char *slash = strrchr(out_dir, '/');
    char *bslash = strrchr(out_dir, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash && slash != out_dir) {
        *slash = '\0';
    } else if (slash == out_dir) {
        out_dir[1] = '\0';
    } else {
        out_dir[0] = '\0';
    }
}

typedef struct {
    char dir[NL_MAX_GAME_FOLDERS][NL_MAX_PATH_LEN];
    bool has_icons[NL_MAX_GAME_FOLDERS];
    int  count;
} nl_sidecar_folder_cache_t;

static bool sidecar_folder_has_icons(nl_sidecar_folder_cache_t *cache, const char *game_path) {
    if (!cache || !game_path || game_path[0] == '\0') return false;
    char parent[NL_MAX_PATH_LEN];
    extract_game_parent_dir_c(game_path, parent, sizeof(parent));
    if (parent[0] == '\0') return false;
    for (int i = 0; i < cache->count; i++) {
        if (str_icmp_c(cache->dir[i], parent) == 0) {
            return cache->has_icons[i];
        }
    }
    bool found = dir_contains_any_icon_asset(parent);
    if (cache->count < NL_MAX_GAME_FOLDERS) {
        snprintf(cache->dir[cache->count], sizeof(cache->dir[cache->count]), "%s", parent);
        cache->has_icons[cache->count] = found;
        cache->count++;
    }
    return found;
}

struct nl_icon_batch {
    nl_icon_dir_index_t       *icons_idx;
    nl_icon_dir_index_t       *covers_idx;
    nl_sidecar_folder_cache_t *side_cache;
    char                       icons_dir[NL_MAX_PATH_LEN];
    char                       covers_dir[NL_MAX_PATH_LEN];
};

nl_icon_batch_t *nl_icon_batch_start(const char *covers_dir) {
    nl_icon_batch_t *b = (nl_icon_batch_t *)calloc(1U, sizeof(nl_icon_batch_t));
    if (!b) return NULL;
    b->icons_idx  = (nl_icon_dir_index_t *)calloc(1U, sizeof(nl_icon_dir_index_t));
    b->covers_idx = (nl_icon_dir_index_t *)calloc(1U, sizeof(nl_icon_dir_index_t));
    b->side_cache = (nl_sidecar_folder_cache_t *)calloc(1U, sizeof(nl_sidecar_folder_cache_t));
    if (covers_dir && covers_dir[0] != '\0') {
        snprintf(b->covers_dir, sizeof(b->covers_dir), "%s", covers_dir);
    }
    derive_icons_dir_from_covers(covers_dir, b->icons_dir, sizeof(b->icons_dir));
    if (b->icons_idx && b->icons_dir[0] != '\0') {
        build_icon_dir_index(b->icons_idx, b->icons_dir);
    }
    if (b->covers_idx && b->covers_dir[0] != '\0') {
        build_icon_dir_index(b->covers_idx, b->covers_dir);
    }
    return b;
}

bool nl_icon_batch_find_file(nl_icon_batch_t *batch, const nl_game_t *game, char *out_path, size_t max_len) {
    if (!batch || !game || !out_path || max_len == 0U) return false;
    out_path[0] = '\0';
    char stem[NL_MAX_NAME_LEN];
    nl_extract_game_stem(game->filename, stem, sizeof(stem));
    if (batch->icons_idx && batch->icons_idx->count > 0 &&
        find_in_dir_index(batch->icons_idx, stem, G_ICON_EXTS, NL_ICON_EXT_COUNT, out_path, max_len)) {
        return true;
    }
    if (batch->side_cache && sidecar_folder_has_icons(batch->side_cache, game->path)) {
        for (int e = 0; e < NL_ICON_EXT_COUNT; e++) {
            build_sidecar_path(game->path, G_ICON_EXTS[e], out_path, max_len);
            if (path_exists_any_size(out_path)) return true;
        }
        out_path[0] = '\0';
    }
    if (batch->covers_idx && batch->covers_idx->count > 0 &&
        find_in_dir_index(batch->covers_idx, stem, G_ICON_EXTS, NL_ICON_EXT_COUNT, out_path, max_len)) {
        return true;
    }
    return false;
}

bool nl_icon_batch_decode_file(const char *cand_path, nl_game_t *game) {
    if (!cand_path || cand_path[0] == '\0' || !game) return false;
    return load_custom_icon_candidate(cand_path, game);
}

void nl_icon_batch_destroy(nl_icon_batch_t *batch) {
    if (!batch) return;
    free(batch->side_cache);
    free(batch->covers_idx);
    free(batch->icons_idx);
    free(batch);
}

void nl_load_all_icons_ex(nl_state_t *state, nl_progress_cb_t progress_cb, void *progress_ctx) {
    if (!state) return;

    nl_icon_batch_t *batch = nl_icon_batch_start(state->covers_dir);
    char *cand = (char *)malloc(NL_MAX_PATH_LEN);

    uint32_t total_ram = 0U;
    int loaded_cnt = 0;
    int fallback_cnt = 0;
    bool budget_warned = false;

    for (int i = 0; i < state->total_games; i++) {
        if (state->scan.abort_requested || state->scan.cancel_requested) break;
        if ((i & 15) == 0) nl_disable_watchdog_safe();

        nl_game_t *g = &state->games[i];

        /* Locate current icon file on disk (icons_dir -> sidecar ONLY if folder has icons -> covers_dir).
         * With 0 icons installed, this performs ZERO per-game fopen() syscalls! */
        bool has_icon_file = (batch && cand) ? nl_icon_batch_find_file(batch, g, cand, NL_MAX_PATH_LEN) : false;

        if (!has_icon_file) {
            /* Icon file is absent/deleted -> cleanly fall back to generic icon; NEVER touch game entry! */
            free_game_icon_buffers(g);
            g->icon_source = NL_ICON_NONE;
            fallback_cnt++;
        } else if (g->icon_source != NL_ICON_NONE && g->icon_pixels != NULL) {
            /* Icon already decoded in RAM and file still exists on disk */
            total_ram += game_icon_ram_bytes(g);
            loaded_cnt++;
        } else if (total_ram >= NL_ICON_RAM_BUDGET) {
            /* Respect strict RAM budget so 78+ icons never crash CX II-T */
            free_game_icon_buffers(g);
            g->icon_source = NL_ICON_NONE;
            fallback_cnt++;
            if (!budget_warned) {
                budget_warned = true;
                nl_diag_report(
                    state,
                    NL_DIAG_E801_LOW_MEMORY,
                    g->title[0] ? g->title : g->filename,
                    cand ? cand : state->icons_dir,
                    "Budget RAM icones",
                    "Limite memoire icones atteinte",
                    "Jeux restants affichent l'icone generique",
                    "Tous les jeux restent disponibles et lancables"
                );
            }
        } else {
            bool ok = cand ? load_custom_icon_candidate(cand, g) : nl_load_game_icon(g, state->covers_dir);
            if (ok) {
                total_ram += game_icon_ram_bytes(g);
                loaded_cnt++;
            } else {
                free_game_icon_buffers(g);
                g->icon_source = NL_ICON_NONE;
                fallback_cnt++;
                nl_diag_report(
                    state,
                    NL_DIAG_E103_ICON_CORRUPT,
                    g->title[0] ? g->title : g->filename,
                    cand ? cand : state->icons_dir,
                    "Decoder icone du jeu",
                    "Format .png/.ico/.bmp invalide ou corrompu",
                    "Jeu toujours disponible et lancable",
                    "Icone ignoree + icone par defaut"
                );
            }
        }

        if (progress_cb) progress_cb(progress_ctx);
    }

    state->icon_ram_bytes      = total_ram;
    state->scan.icons_loaded   = loaded_cnt;
    state->scan.icons_fallback = fallback_cnt;

    free(cand);
    nl_icon_batch_destroy(batch);
}

void nl_load_all_icons(nl_state_t *state) {
    nl_load_all_icons_ex(state, NULL, NULL);
}

void nl_free_screenshots(nl_state_t *state) {
    if (!state) return;
    for (int i = 0; i < state->total_games; i++) {
        if (state->games[i].scr_pixels) {
            free(state->games[i].scr_pixels);
            state->games[i].scr_pixels = NULL;
        }
        state->games[i].has_screenshot = false;
    }
}

void nl_free_game_icons_only(nl_state_t *state) {
    if (!state) return;
    for (int i = 0; i < state->total_games; i++) {
        free_game_icon_buffers(&state->games[i]);
        state->games[i].icon_source = NL_ICON_NONE;
    }
    state->icon_ram_bytes = 0U;
}

void nl_free_media_buffers(nl_state_t *state) {
    if (!state) return;
    nl_free_game_icons_only(state);
    nl_free_screenshots(state);
}

void nl_free_icons(nl_state_t *state) {
    if (!state) return;
    nl_free_media_buffers(state);
    nl_profiles_free(state);
}

static void ensure_dir_tree(const char *dir_path) {
    if (!dir_path || dir_path[0] == '\0') return;
    char tmp[NL_MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    size_t len = strlen(tmp);
    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
        tmp[--len] = '\0';
    }
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            (void)mkdir(tmp, 0777);
            *p = saved;
        }
    }
    (void)mkdir(tmp, 0777);
}

static bool write_nlic_file(const char *target_path, const uint8_t *hdr, const uint8_t *raw) {
    if (!target_path || target_path[0] == '\0' || !hdr || !raw) return false;

    /* Atomic temp-write + verify + rename so an existing valid .scr.tns is NEVER damaged on I/O error */
    char tmp_path[NL_MAX_PATH_LEN];
    size_t len = strlen(target_path);
    if (len > 4U && str_icmp_c(target_path + len - 4U, ".tns") == 0) {
        snprintf(tmp_path, sizeof(tmp_path), "%.*s.tmp.tns", (int)(len - 4U), target_path);
    } else {
        snprintf(tmp_path, sizeof(tmp_path), "%.*s.tmp.tns",
                 (int)(sizeof(tmp_path) - 10U), target_path);
    }

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) return false;

    bool write_ok = (fwrite(hdr, 1, 16, fp) == 16) &&
                    (fwrite(raw, 1, NL_COVER_BYTES, fp) == NL_COVER_BYTES) &&
                    (fflush(fp) == 0) &&
                    (ferror(fp) == 0);
    int close_rc = fclose(fp);
    if (!write_ok || close_rc != 0 || !validate_nlic_file_on_disk(tmp_path)) {
        (void)remove(tmp_path);
        return false;
    }

    /* Replace target only after temp file is 100% verified on disk */
    (void)remove(target_path);
    if (rename(tmp_path, target_path) == 0) {
        return true;
    }
    /* Fallback copy if rename fails across mountpoints */
    FILE *src = fopen(tmp_path, "rb");
    if (!src) {
        (void)remove(tmp_path);
        return false;
    }
    FILE *dst = fopen(target_path, "wb");
    if (!dst) {
        fclose(src);
        (void)remove(tmp_path);
        return false;
    }
    uint8_t buf[256];
    size_t nr;
    bool copy_ok = true;
    while ((nr = fread(buf, 1U, sizeof(buf), src)) > 0U) {
        if (fwrite(buf, 1U, nr, dst) != nr) { copy_ok = false; break; }
    }
    if (fflush(dst) != 0 || ferror(dst) != 0) copy_ok = false;
    fclose(dst);
    fclose(src);
    (void)remove(tmp_path);
    return copy_ok;
}

static bool save_nlic_to_targets(
    const nl_game_t *game,
    const uint16_t *pixels,
    const char *covers_dir,
    const char *ext_tns
) {
    if (!game || !pixels || !ext_tns) return false;

    uint8_t hdr[16];
    memset(hdr, 0, sizeof(hdr));
    write_u32_le(hdr + 0, NL_ICON_MAGIC);
    write_u16_le(hdr + 4, 1);
    write_u16_le(hdr + 6, NL_COVER_W);
    write_u16_le(hdr + 8, NL_COVER_H);

    uint8_t *raw = (uint8_t *)malloc(NL_COVER_BYTES);
    if (!raw) return false;
    for (int i = 0; i < NL_COVER_PIXELS; i++) {
        write_u16_le(raw + i * 2, pixels[i]);
    }

    char target_path[NL_MAX_PATH_LEN];
    bool saved = false;

    if (covers_dir && covers_dir[0] != '\0') {
        ensure_dir_tree(covers_dir);
        char stem[NL_MAX_NAME_LEN];
        nl_extract_game_stem(game->filename, stem, sizeof(stem));
        snprintf(target_path, sizeof(target_path), "%s/%s%s", covers_dir, stem, ext_tns);
        saved = write_nlic_file(target_path, hdr, raw);
    }

    if (!saved) {
        build_sidecar_path(game->path, ext_tns, target_path, sizeof(target_path));
        saved = write_nlic_file(target_path, hdr, raw);
    }

    free(raw);
    return saved;
}

bool nl_save_captured_icon(const nl_game_t *game, const char *covers_dir) {
    if (!game) return false;
    return save_nlic_to_targets(game, game->icon_pixels, covers_dir, ".ico.tns");
}

bool nl_save_captured_screenshot(const nl_game_t *game, const char *covers_dir) {
    if (!game || !nl_screenshot_is_valid_buffer(game->scr_pixels)) return false;
    return save_nlic_to_targets(game, game->scr_pixels, covers_dir, ".scr.tns");
}

bool nl_delete_captured_icon(nl_game_t *game, const char *covers_dir) {
    if (!game) return false;

    char stem[NL_MAX_NAME_LEN];
    nl_extract_game_stem(game->filename, stem, sizeof(stem));

    char candidate[NL_MAX_PATH_LEN * 2];
    char icons_dir[NL_MAX_PATH_LEN];
    derive_icons_dir_from_covers(covers_dir, icons_dir, sizeof(icons_dir));
    if (icons_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s/%s.ico.tns", icons_dir, stem);
        (void)remove(candidate);
    }
    if (covers_dir && covers_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s/%s.ico.tns", covers_dir, stem);
        (void)remove(candidate);
    }
    build_sidecar_path(game->path, ".ico.tns", candidate, sizeof(candidate));
    (void)remove(candidate);

    free_game_icon_buffers(game);
    game->icon_source = NL_ICON_NONE;
    nl_load_game_icon(game, covers_dir);
    return true;
}

/* Delete ONLY the custom icon (.png/.ico/.bmp) of this single game; NEVER touch .scr.tns or .tns */
bool nl_delete_custom_icon(nl_game_t *game, const char *covers_dir) {
    if (!game) return false;

    char stem[NL_MAX_NAME_LEN];
    nl_extract_game_stem(game->filename, stem, sizeof(stem));

    char candidate[NL_MAX_PATH_LEN * 2];
    char icons_dir[NL_MAX_PATH_LEN];
    derive_icons_dir_from_covers(covers_dir, icons_dir, sizeof(icons_dir));

    if (icons_dir[0] != '\0') {
        for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
            snprintf(candidate, sizeof(candidate), "%s/%s%s", icons_dir, stem, G_ICON_EXTS[i]);
            (void)remove(candidate);
        }
    }

    for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
        build_sidecar_path(game->path, G_ICON_EXTS[i], candidate, sizeof(candidate));
        (void)remove(candidate);
    }

    if (covers_dir && covers_dir[0] != '\0') {
        for (int i = 0; i < NL_ICON_EXT_COUNT; i++) {
            snprintf(candidate, sizeof(candidate), "%s/%s%s", covers_dir, stem, G_ICON_EXTS[i]);
            (void)remove(candidate);
        }
    }

    free_game_icon_buffers(game);
    game->icon_source = NL_ICON_NONE;
    return true;
}

static uint16_t palette_entry_to_rgb565(uint16_t pal) {
    unsigned b5 = (pal >> 10) & 0x1FU;
    unsigned g5 = (pal >> 5)  & 0x1FU;
    unsigned r5 =  pal        & 0x1FU;
    unsigned i1 = (pal >> 15) & 0x01U;
    unsigned g6 = (g5 << 1) | i1;
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static uint16_t rgb555_to_rgb565(uint16_t v) {
    unsigned r5 = (v >> 10) & 0x1FU;
    unsigned g5 = (v >> 5)  & 0x1FU;
    unsigned b5 =  v        & 0x1FU;
    unsigned g6 = (g5 << 1) | (g5 >> 4);
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void prepare_palette_565(
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256,
    uint16_t pal565[256]
) {
    if (fmt == NL_PIX_8BPP) {
        for (int i = 0; i < 256; i++) {
            pal565[i] = palette_entry_to_rgb565(hw_palette256[i]);
        }
    } else if (fmt == NL_PIX_4BPP) {
        bool has_pal = false;
        if (hw_palette256) {
            uint16_t p0 = hw_palette256[0];
            for (int i = 1; i < 16; i++) {
                if (hw_palette256[i] != p0) {
                    has_pal = true;
                    break;
                }
            }
        }
        for (int i = 0; i < 16; i++) {
            if (has_pal) {
                pal565[i] = palette_entry_to_rgb565(hw_palette256[i]);
            } else {
                uint8_t shade = (uint8_t)(i * 17U);
                pal565[i] = RGB565(shade, shade, shade);
            }
        }
    }
}

static inline uint16_t sample_vram_pixel_ex(
    const uint16_t *vram16,
    const uint8_t  *vram8,
    nl_pixel_format_t fmt,
    bool rotated_240x320,
    const uint16_t *pal565,
    int x,
    int y
) {
    int idx = rotated_240x320 ? (x * NL_SCREEN_H + y) : (y * NL_SCREEN_W + x);
    switch (fmt) {
        case NL_PIX_RGB555:
            return rgb555_to_rgb565(vram16[idx]);
        case NL_PIX_8BPP:
            return pal565[vram8[idx]];
        case NL_PIX_4BPP: {
            uint8_t byte = vram8[idx >> 1];
            uint8_t nibble = (idx & 1) ? (uint8_t)(byte & 0x0FU) : (uint8_t)((byte >> 4) & 0x0FU);
            return pal565[nibble];
        }
        case NL_PIX_RGB565:
        default:
            return vram16[idx];
    }
}

int nl_evaluate_frame_score(
    const void *frame_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256
) {
    if (!frame_base) return -1;
    if (fmt == NL_PIX_8BPP && !hw_palette256) return -1;

    const uint16_t *vram16 = (const uint16_t *)frame_base;
    const uint8_t  *vram8  = (const uint8_t *)frame_base;
    uint16_t pal565[256];
    prepare_palette_565(fmt, hw_palette256, pal565);

    int total_samples = 0;
    int sentinel_pixels = 0;
    int non_black_pixels = 0;
    int edge_transitions = 0;
    uint16_t hist64[64];
    memset(hist64, 0, sizeof(hist64));
    uint16_t band_edges[8];
    memset(band_edges, 0, sizeof(band_edges));
    uint16_t prev_row_px[80];
    memset(prev_row_px, 0, sizeof(prev_row_px));

    for (int y = 8; y < NL_SCREEN_H - 8; y += 4) {
        int band = ((y - 8) * 8) / (NL_SCREEN_H - 16);
        if (band < 0) band = 0;
        if (band > 7) band = 7;

        uint16_t left_px = 0;
        int col = 0;
        for (int x = 8; x < NL_SCREEN_W - 8; x += 4) {
            uint16_t px = sample_vram_pixel_ex(vram16, vram8, fmt, rotated_240x320, pal565, x, y);
            total_samples++;
            if (px == NL_VRAM_SENTINEL) {
                sentinel_pixels++;
            }

            unsigned r5 = (px >> 11) & 0x1FU;
            unsigned g6 = (px >> 5)  & 0x3FU;
            unsigned b5 =  px        & 0x1FU;
            unsigned luma = r5 * 2U + g6 + b5 * 2U; /* 0..187 */

            /* White pixels ARE genuine game pixels (light backgrounds: 2048, Chess, Sudoku, Dino) */
            if (px != NL_VRAM_SENTINEL && luma > 10U) {
                non_black_pixels++;
            }

            if (x > 8 && px != left_px) {
                edge_transitions++;
                band_edges[band]++;
            }
            if (y > 8 && col < 80 && px != prev_row_px[col]) {
                edge_transitions++;
                band_edges[band]++;
            }
            left_px = px;
            if (col < 80) {
                prev_row_px[col] = px;
            }
            col++;

            /* True 64-bin RGB222 histogram (no XOR primary-color collapse!) */
            unsigned bin = (((r5 >> 3) & 3U) << 4) |
                           (((g6 >> 4) & 3U) << 2) |
                           ((b5 >> 3) & 3U);
            hist64[bin]++;
        }
    }

    if (total_samples <= 0) return -1;
    if (sentinel_pixels > (total_samples * 35) / 100) return -1;
    if (non_black_pixels < 50) return -1;

    int max_bin_count = 0;
    int distinct_bins = 0;
    for (int b = 0; b < 64; b++) {
        if ((int)hist64[b] > max_bin_count) {
            max_bin_count = (int)hist64[b];
        }
        if (hist64[b] >= 2U) {
            distinct_bins++;
        }
    }

    int non_dom_pixels = total_samples - max_bin_count;
    if (non_dom_pixels < 50 || distinct_bins < 2) return -1;

    int active_bands = 0;
    for (int b = 0; b < 8; b++) {
        if (band_edges[b] >= 2U) {
            active_bands++;
        }
    }
    if (edge_transitions < 24 || active_bands < 3) return -1;

    int capped_non_dom = (non_dom_pixels > 2400) ? 2400 : non_dom_pixels;
    int capped_edges   = (edge_transitions > 1600) ? 1600 : edge_transitions;
    int capped_bins    = (distinct_bins > 24) ? 24 : distinct_bins;

    return capped_bins * 120 + (capped_non_dom / 4) + (capped_edges / 2) + active_bands * 40;
}

static bool capture_frame_to_pixels(
    uint16_t **dst_pixels,
    const void *frame_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256,
    int *out_score
) {
    if (out_score) *out_score = -1;
    if (!dst_pixels || !frame_base) return false;
    if (fmt == NL_PIX_8BPP && !hw_palette256) return false;

    int score = nl_evaluate_frame_score(frame_base, rotated_240x320, fmt, hw_palette256);
    if (out_score) *out_score = score;
    if (score < 0) return false;

    if (!ensure_pixel_buf(dst_pixels)) return false;
    uint16_t *out = *dst_pixels;

    const uint16_t *vram16 = (const uint16_t *)frame_base;
    const uint8_t  *vram8  = (const uint8_t *)frame_base;
    uint16_t pal565[256];
    prepare_palette_565(fmt, hw_palette256, pal565);

    /* Aspect-ratio-preserving centered crop from 320x240 -> 94x68 (zero distortion) */
    int cx0 = 0, cy0 = 0, cw = NL_SCREEN_W, ch = NL_SCREEN_H;
    compute_aspect_cover_crop(NL_SCREEN_W, NL_SCREEN_H, NL_COVER_W, NL_COVER_H, &cx0, &cy0, &cw, &ch);

    for (int dy = 0; dy < NL_COVER_H; dy++) {
        int sy0 = cy0 + (dy * ch) / NL_COVER_H;
        int sy1 = cy0 + ((dy + 1) * ch - 1) / NL_COVER_H;
        if (sy1 >= NL_SCREEN_H) sy1 = NL_SCREEN_H - 1;
        int sy_mid = (sy0 + sy1) >> 1;

        for (int dx = 0; dx < NL_COVER_W; dx++) {
            int sx0 = cx0 + (dx * cw) / NL_COVER_W;
            int sx1 = cx0 + ((dx + 1) * cw - 1) / NL_COVER_W;
            if (sx1 >= NL_SCREEN_W) sx1 = NL_SCREEN_W - 1;
            int sx_mid = (sx0 + sx1) >> 1;

            uint16_t p0 = sample_vram_pixel_ex(vram16, vram8, fmt, rotated_240x320, pal565, sx0, sy0);
            uint16_t p1 = sample_vram_pixel_ex(vram16, vram8, fmt, rotated_240x320, pal565, sx_mid, sy0);
            uint16_t p2 = sample_vram_pixel_ex(vram16, vram8, fmt, rotated_240x320, pal565, sx0, sy_mid);
            uint16_t p3 = sample_vram_pixel_ex(vram16, vram8, fmt, rotated_240x320, pal565, sx_mid, sy_mid);

            unsigned r = (((p0 >> 11) & 0x1FU) + ((p1 >> 11) & 0x1FU) +
                          ((p2 >> 11) & 0x1FU) + ((p3 >> 11) & 0x1FU) + 2U) >> 2;
            unsigned g = (((p0 >> 5)  & 0x3FU) + ((p1 >> 5)  & 0x3FU) +
                          ((p2 >> 5)  & 0x3FU) + ((p3 >> 5)  & 0x3FU) + 2U) >> 2;
            unsigned b = ((p0 & 0x1FU) + (p1 & 0x1FU) +
                          (p2 & 0x1FU) + (p3 & 0x1FU) + 2U) >> 2;

            out[dy * NL_COVER_W + dx] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }

    return true;
}

bool nl_capture_frame_to_icon_ex(
    nl_game_t *game,
    const void *frame_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256,
    bool stage_only,
    int *out_score
) {
    if (out_score) *out_score = -1;
    if (!game || !frame_base) return false;

    /* NEVER overwrite an existing valid cover (.bmp.tns or .ico.tns) */
    if (game->icon_source != NL_ICON_NONE && game->icon_pixels != NULL) {
        return false;
    }

    if (game->icon_alpha) {
        free(game->icon_alpha);
        game->icon_alpha = NULL;
    }
    if (!capture_frame_to_pixels(&game->icon_pixels, frame_base, rotated_240x320, fmt, hw_palette256, out_score)) {
        return false;
    }
    game->icon_w = NL_COVER_W;
    game->icon_h = NL_COVER_H;

    if (!stage_only) {
        game->icon_source = NL_ICON_CAPTURED;
    }
    return true;
}

/* Capture live 320x240 / 240x320 / 8bpp active game frame ONLY when game has no valid cover yet! */
bool nl_capture_frame_to_icon(
    nl_game_t *game,
    const void *frame_base,
    bool rotated_240x320,
    bool is_8bpp,
    const uint16_t *hw_palette256
) {
    return nl_capture_frame_to_icon_ex(
        game,
        frame_base,
        rotated_240x320,
        is_8bpp ? NL_PIX_8BPP : NL_PIX_RGB565,
        hw_palette256,
        false,
        NULL
    );
}

static bool vram_looks_8bpp(const void *vram_base, const uint16_t *hw_palette256) {
    if (!vram_base || !hw_palette256) return false;
    const uint16_t *vram16 = (const uint16_t *)vram_base;
    int upper_sentinel = 0;
    for (int i = NL_FB_PIXELS / 2; i < NL_FB_PIXELS; i += 8) {
        if (vram16[i] == NL_VRAM_SENTINEL) upper_sentinel++;
    }
    int upper_samples = (NL_FB_PIXELS / 2) / 8;
    return (upper_sentinel > (upper_samples * 95) / 100);
}

bool nl_capture_vram_to_icon(
    nl_game_t *game,
    const void *vram_base,
    bool rotated_240x320,
    const uint16_t *hw_palette256
) {
    if (!game || !vram_base) return false;
    bool is_8bpp = vram_looks_8bpp(vram_base, hw_palette256);
    return nl_capture_frame_to_icon_ex(
        game,
        vram_base,
        rotated_240x320,
        is_8bpp ? NL_PIX_8BPP : NL_PIX_RGB565,
        hw_palette256,
        false,
        NULL
    );
}

bool nl_capture_frame_to_screenshot_ex(
    nl_game_t *game,
    const void *frame_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256,
    bool stage_only,
    int *out_score
) {
    if (out_score) *out_score = -1;
    if (!game || !frame_base) return false;

    /* NEVER overwrite an existing valid screenshot (.scr.tns) */
    if (game->has_screenshot && game->scr_pixels != NULL) {
        return false;
    }

    if (!capture_frame_to_pixels(&game->scr_pixels, frame_base, rotated_240x320, fmt, hw_palette256, out_score)) {
        return false;
    }

    if (!stage_only) {
        game->has_screenshot = true;
    }
    return true;
}

bool nl_capture_vram_to_screenshot(
    nl_game_t *game,
    const void *vram_base,
    bool rotated_240x320,
    const uint16_t *hw_palette256
) {
    if (!game || !vram_base) return false;
    bool is_8bpp = vram_looks_8bpp(vram_base, hw_palette256);
    return nl_capture_frame_to_screenshot_ex(
        game,
        vram_base,
        rotated_240x320,
        is_8bpp ? NL_PIX_8BPP : NL_PIX_RGB565,
        hw_palette256,
        false,
        NULL
    );
}


