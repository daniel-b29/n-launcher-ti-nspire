/****************************************************************************
 * N-Launcher v3 — Automated Modular C Verification & Binary Test Suite
 * Tests:
 *  1. Compiled ARM N-Launcher.tns (Zehn v1 / PRG\0 / v3 metadata) & self-skip
 *  2. Binary discrimination (Zehn/PRG vs TI-Nspire ZIP/XML) & file_exists check
 *  3. Strict 3 categories ONLY (TOUS, FAV, REC) + clean empty states
 *  4. Real .bmp.tns aspect-preserving crop (zero stretching) & 94x68 scaling
 *  5. Existing cover preservation (NEVER overwrite .bmp.tns/.ico.tns) &
 *     blank/black/uniform VRAM capture rejection + real 240x320 capture
 *  6. Interruptible per-card scaling, vertical page scrolling (156px stride),
 *     dot interpolation, and single-launch guard
 *  7. 320x240 RGB565 rendering: splash, rounded clip bounds, bottom row peek
 *     below y=205, launch zoom bounce, and BMP preview export
 ****************************************************************************/

#include "nlauncher.h"
#include "boot_guard_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <setjmp.h>

static int     g_mock_exec_calls = 0;
static int     g_mock_exec_mode  = 0; /* 0 = clean + stabilized, 1 = hardware crash/reboot mid-init (longjmp), 2 = return 0xDEAD, 3 = clean ESC before 30f, 4 = early return 1 leaving pending=1 */
static char    g_mock_guard_path[512] = "";
static int     g_mock_pending_seen_in_exec = -1;
static jmp_buf g_mock_reboot_jmp;

static bool mock_stub_file_ok(const char *path) {
    struct stat st;
    return (path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 16);
}

static int mock_stub_exec(const char *path, int argc, char *argv[]) {
    (void)path;
    (void)argc;
    (void)argv;
    g_mock_exec_calls++;

    /* Verify nl_boot_stub recorded pending=1 BEFORE entering N-Launcher */
    nl_boot_guard_state_t gs;
    nl_bg_read_guard_state(g_mock_guard_path, &gs);
    g_mock_pending_seen_in_exec = gs.pending;

    if (g_mock_exec_mode == 0) {
        /* Normal launch: during early init pending MUST STILL BE 1 */
        assert(gs.pending == 1);
        /* Once main loop reaches NL_BOOT_STABILIZE_FRAMES, N-Launcher marks boot stable */
        assert(nl_bg_write_guard_state(g_mock_guard_path, 0, 0, 0, "none") == true);
        return 0;
    } else if (g_mock_exec_mode == 1) {
        /* Simulated sudden hardware crash / CPU reboot mid-init -> never returns from nl_exec, pending=1 remains on flash */
        longjmp(g_mock_reboot_jmp, 1);
        return 0;
    } else if (g_mock_exec_mode == 2) {
        /* Simulated Zehn loader / Ndless exec failure */
        return 0xDEAD;
    } else if (g_mock_exec_mode == 4) {
        /* Simulated early fatal return (e.g., lcd_init / calloc fail) before clearing pending=1 */
        return 1;
    } else {
        /* Simulated clean ESC exit on frame 5 (before 30 frames): N-Launcher clears pending=0 at clean exit */
        assert(gs.pending == 1);
        assert(nl_bg_write_guard_state(g_mock_guard_path, 0, 0, 0, "none") == true);
        return 0;
    }
}

static void test_on_progress_tick(void *ctx) {
    if (ctx) (*(int *)ctx)++;
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void create_mock_zehn_tns(
    const char *filepath,
    const char *exec_name,
    const char *exec_author,
    uint32_t version,
    bool lcd_blit_flag,
    bool hww_flag,
    size_t pad_bytes
) {
    uint8_t buf[2048];
    memset(buf, 0, sizeof(buf));

    buf[0] = 'P'; buf[1] = 'R'; buf[2] = 'G'; buf[3] = '\0';

    size_t z_off = 0x1ec;
    uint8_t *zh = buf + z_off;

    size_t name_len = strlen(exec_name) + 1;
    size_t auth_len = strlen(exec_author) + 1;
    uint32_t extra_size = (uint32_t)((name_len + auth_len + 3) & ~3U);
    uint32_t flag_count = 5;
    uint32_t reloc_count = 4;
    uint32_t total_size = (uint32_t)(1024 + pad_bytes);

    write_u32_le(zh + 0,  NL_MAGIC_ZEHN);
    write_u32_le(zh + 4,  1);
    write_u32_le(zh + 8,  total_size - (uint32_t)z_off);
    write_u32_le(zh + 12, reloc_count);
    write_u32_le(zh + 16, flag_count);
    write_u32_le(zh + 20, extra_size);
    write_u32_le(zh + 24, total_size + 4096);
    write_u32_le(zh + 28, 0);

    uint8_t *flags = zh + 32 + reloc_count * 4;
    write_u32_le(flags + 0,  0U | (45U << 8));
    write_u32_le(flags + 4,  8U | (0U << 8));
    write_u32_le(flags + 8,  9U | ((uint32_t)name_len << 8));
    write_u32_le(flags + 12, 10U | (version << 8));
    write_u32_le(flags + 16, (lcd_blit_flag ? 13U : 12U) | ((hww_flag ? 1U : 0U) << 8));

    char *extra = (char *)(flags + flag_count * 4);
    memcpy(extra, exec_name, name_len);
    memcpy(extra + name_len, exec_author, auth_len);

    FILE *fp = fopen(filepath, "wb");
    assert(fp != NULL);
    fwrite(buf, 1, 1024, fp);
    for (size_t i = 0; i < pad_bytes; i++) {
        fputc((int)(i & 0xFF), fp);
    }
    fclose(fp);
}

static void create_mock_ti_doc_tns(const char *filepath) {
    uint8_t zip_hdr[64] = {
        0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 'D', 'o', 'c', 'u', 'm', 'e', 'n', 't', '.', 'x', 'm', 'l'
    };
    FILE *fp = fopen(filepath, "wb");
    assert(fp != NULL);
    fwrite(zip_hdr, 1, sizeof(zip_hdr), fp);
    fclose(fp);
}

static void create_mock_ico_32bpp(
    const char *filepath,
    int w,
    int h,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    uint32_t xor_sz = (uint32_t)(w * h * 4);
    uint32_t and_stride = (uint32_t)((((w + 31) >> 5) * 4));
    uint32_t and_sz = and_stride * (uint32_t)h;
    uint32_t dib_sz = 40U + xor_sz + and_sz;
    uint8_t *buf = (uint8_t *)calloc(1U, 22U + dib_sz);
    assert(buf != NULL);

    buf[2] = 1;
    buf[4] = 1;
    buf[6] = (uint8_t)w;
    buf[7] = (uint8_t)h;
    buf[10] = 1;
    buf[12] = 32;
    write_u32_le(buf + 14, dib_sz);
    write_u32_le(buf + 18, 22U);

    write_u32_le(buf + 22, 40U);
    write_u32_le(buf + 26, (uint32_t)w);
    write_u32_le(buf + 30, (uint32_t)(h * 2));
    buf[34] = 1;
    buf[36] = 32;
    write_u32_le(buf + 42, xor_sz + and_sz);

    uint8_t *px = buf + 62;
    for (int i = 0; i < w * h; i++) {
        px[i * 4 + 0] = b;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = r;
        px[i * 4 + 3] = (i == 0) ? 180U : 255U; /* Partial alpha on pixel 0 so alpha buffer is retained */
    }

    FILE *fp = fopen(filepath, "wb");
    assert(fp != NULL);
    fwrite(buf, 1U, 22U + dib_sz, fp);
    fclose(fp);
    free(buf);
}

static bool export_fb_to_bmp(const char *bmp_path, const uint16_t *fb, int w, int h) {
    FILE *fp = fopen(bmp_path, "wb");
    if (!fp) return false;

    uint32_t row_stride = ((uint32_t)w * 3U + 3U) & ~3U;
    uint32_t img_size = row_stride * (uint32_t)h;
    uint32_t file_size = 54U + img_size;

    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    write_u32_le(hdr + 2, file_size);
    write_u32_le(hdr + 10, 54);
    write_u32_le(hdr + 14, 40);
    write_u32_le(hdr + 18, (uint32_t)w);
    write_u32_le(hdr + 22, (uint32_t)h);
    hdr[26] = 1;
    hdr[28] = 24;
    write_u32_le(hdr + 34, img_size);

    fwrite(hdr, 1, 54, fp);

    uint8_t *row_buf = (uint8_t *)calloc(1, row_stride);
    assert(row_buf != NULL);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint16_t px = fb[y * w + x];
            uint8_t r = (uint8_t)(((px >> 11) & 0x1FU) * 255U / 31U);
            uint8_t g = (uint8_t)(((px >> 5)  & 0x3FU) * 255U / 63U);
            uint8_t b = (uint8_t)((px         & 0x1FU) * 255U / 31U);
            row_buf[x * 3 + 0] = b;
            row_buf[x * 3 + 1] = g;
            row_buf[x * 3 + 2] = r;
        }
        fwrite(row_buf, 1, row_stride, fp);
    }
    free(row_buf);
    fclose(fp);
    return true;
}

static bool bytes_contains_sub(const uint8_t *buf, size_t len, const char *sub, size_t sub_len) {
    if (!buf || !sub || sub_len == 0U || len < sub_len) return false;
    for (size_t i = 0; i <= len - sub_len; i++) {
        if (memcmp(buf + i, sub, sub_len) == 0) return true;
    }
    return false;
}

static void test_abort_after_5_cb(const nl_scan_stats_t *stats, void *ctx) {
    nl_state_t *st = (nl_state_t *)ctx;
    if (st && stats && stats->games_found >= 5) {
        st->scan.abort_requested = true;
    }
}

int main(int argc, char *argv[]) {
    printf("============================================================\n");
    printf(" N-LAUNCHER v3 — MODULAR C VERIFICATION & BINARY TEST SUITE\n");
    printf("============================================================\n");

    /* 1. Verify Compiled ARM N-Launcher.tns Executable */
    const char *compiled_tns = (argc > 1) ? argv[1] : "N-Launcher.tns";
    assert(nl_file_exists(compiled_tns) == true);
    assert(nl_file_exists("/tmp/non_existent_game_999.tns") == false);

    nl_exec_info_t self_info;
    nl_bin_format_t fmt = nl_inspect_tns_file(compiled_tns, "/documents/other_launcher.tns", NULL);
    assert(fmt == NL_BIN_SYSTEM);

    FILE *rfp = fopen(compiled_tns, "rb");
    assert(rfp != NULL);
    fseek(rfp, 0, SEEK_END);
    long rsize = ftell(rfp);
    fseek(rfp, 0, SEEK_SET);
    uint8_t rbuf[16384];
    size_t nread = fread(rbuf, 1, sizeof(rbuf), rfp);
    fclose(rfp);

    nl_bin_format_t raw_fmt = nl_inspect_tns_buffer(
        rbuf, nread, (uint32_t)rsize, "test_probe.tns", NULL, &self_info
    );
    assert(raw_fmt == NL_BIN_ZEHN);
    assert(self_info.zehn_version == 1);
    assert(self_info.exec_version == 3);
    assert(self_info.reloc_count > 0);
    assert(strcmp(self_info.exec_name, "N-Launcher") == 0);
    assert(self_info.uses_lcd_blit == true);
    assert(self_info.supports_hww == true);
    printf("[PASS] Compiled ARM Binary '%s' verified (%ld bytes, Zehn v%u, ver %u, relocs %u).\n",
           compiled_tns, rsize, self_info.zehn_version, self_info.exec_version, self_info.reloc_count);

    /* 2. Strict 3-Category Contract Check */
    assert(NL_CAT_COUNT == 3);
    assert(strcmp(nl_category_label(NL_CAT_ALL), "TOUS") == 0);
    assert(strcmp(nl_category_label(NL_CAT_FAVORITES), "FAV") == 0);
    assert(strcmp(nl_category_label(NL_CAT_RECENT), "REC") == 0);
    printf("[PASS] Strict 3 categories verified: TOUS, FAV, REC.\n");

    /* 3. Prepare Virtual Calculator Filesystem */
    int sys_rc = system("rm -rf /tmp/nlauncher_test_fs && mkdir -p /tmp/nlauncher_test_fs/games/fps /tmp/nlauncher_test_fs/games/arcade /tmp/nlauncher_test_fs/ndless/covers /tmp/nlauncher_test_fs/cours");
    assert(sys_rc == 0);

    create_mock_zehn_tns("/tmp/nlauncher_test_fs/games/fps/nDoom.tns",        "nDoom",        "Mrakoplaz", 3, true, true,  142000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/games/fps/nQuake.tns",       "nQuake",       "R.G.",      2, true, true,  198000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/games/arcade/nKaruga.tns",   "nKaruga",      "Matrefeytontias", 1, true, true, 64000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/games/arcade/2048-CX.tns",   "2048 CX",      "Vogtinator",1, true, true,  28000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/games/arcade/PacMan-CX.tns", "Pac-Man CX",   "calc84",    1, true, true,  35000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/games/arcade/Crafti.tns",    "Crafti",       "Vogtinator",2, true, true,  112000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/ndless/gbc4nspire.tns",      "gbc4nspire",   "calc84maniac", 1, false, true, 88000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/ndless/gpSP-Nspire.tns",     "gpSP GBA",     "calc84maniac", 2, true, true, 165000);

    /* Wide 160x68 BMP for Crafti to verify centered aspect crop (left/right stripes cropped cleanly) */
    static uint16_t wide_bmp[160 * 68];
    for (int y = 0; y < 68; y++) {
        for (int x = 0; x < 160; x++) {
            if (x < 33 || x >= 127) {
                wide_bmp[y * 160 + x] = RGB565(255, 0, 255); /* cropped outer margins */
            } else {
                wide_bmp[y * 160 + x] = (y < 24) ? RGB565(56, 189, 248) : RGB565(34, 197, 94);
            }
        }
    }
    assert(export_fb_to_bmp("/tmp/nlauncher_test_fs/games/arcade/Crafti.bmp.tns", wide_bmp, 160, 68) == true);

    create_mock_zehn_tns("/tmp/nlauncher_test_fs/ndless/nDoom.tns",            "nDoom",        "Mrakoplaz", 3, true, true,  142000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/ndless/ndless_resources.tns", "ndless_res",   "Ndless",    1, true, true,  52000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/ndless/ndless_installer.tns", "installer",    "Ndless",    1, true, true,  24000);
    create_mock_zehn_tns("/tmp/nlauncher_test_fs/ndless/N-Launcher.tns",       "N-Launcher",   "Ndless",    3, true, true,  32000);

    create_mock_ti_doc_tns("/tmp/nlauncher_test_fs/cours/Algebre_Lineaire.tns");
    create_mock_ti_doc_tns("/tmp/nlauncher_test_fs/cours/Physique_Mecanique.tns");
    create_mock_ti_doc_tns("/tmp/nlauncher_test_fs/cours/Suites_Et_Limites.tns");

    /* 4. Multi-Directory Scan & Strict Icon Preservation */
    static nl_state_t state;
    nl_init_state(&state);
    snprintf(state.covers_dir, sizeof(state.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
    nl_scan_directory(&state, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
    nl_load_all_icons(&state);

    assert(state.total_games == 8);
    assert(state.rejected_docs == 3);
    assert(state.rejected_sys == 4);
    assert(state.rejected_dups == 1);
    assert(state.total_pages == 2);

    int crafti_idx = -1, ndoom_idx = -1;
    for (int i = 0; i < state.total_games; i++) {
        if (strcmp(state.games[i].title, "Crafti") == 0) crafti_idx = i;
        if (strcmp(state.games[i].title, "nDoom") == 0) ndoom_idx = i;
    }
    assert(crafti_idx >= 0 && ndoom_idx >= 0);
    assert(state.games[crafti_idx].icon_source == NL_ICON_BMP);
    /* Verify aspect-ratio preserving resize (zero cropping): 160x68 -> 128x54 -> 84x35 with center artwork intact */
    assert(state.games[crafti_idx].icon_w == NL_ICON_MAX_DIM);
    assert(state.games[crafti_idx].icon_h == 35);
    assert(state.games[crafti_idx].icon_pixels[(35 / 2) * NL_ICON_MAX_DIM + (NL_ICON_MAX_DIM / 2)] != RGB565(255, 0, 255));
    assert(state.games[ndoom_idx].icon_source == NL_ICON_NONE);

    /* 5. Real VRAM Screenshot Capture + Strict Non-Overwrite + Black/Exit Guard */
    static uint16_t simulated_vram[NL_FB_PIXELS];
    for (int i = 0; i < NL_FB_PIXELS; i++) simulated_vram[i] = NL_VRAM_SENTINEL;
    assert(nl_capture_vram_to_icon(&state.games[ndoom_idx], simulated_vram, true, NULL) == false);

    /* Uniform black frame must also be rejected */
    memset(simulated_vram, 0, sizeof(simulated_vram));
    assert(nl_capture_vram_to_icon(&state.games[ndoom_idx], simulated_vram, true, NULL) == false);

    /* Rich CX II-T 240x320 gameplay frame */
    for (int x = 0; x < NL_SCREEN_W; x++) {
        for (int y = 0; y < NL_SCREEN_H; y++) {
            uint16_t col = (y < 90) ? RGB565(180, 32, 32) :
                           (y < 170) ? RGB565(90, 56, 36) :
                                       RGB565(42, 64, 96);
            if (((x + y) & 15) < 4) col ^= RGB565(24, 32, 16);
            simulated_vram[x * NL_SCREEN_H + y] = col;
        }
    }
    /* Existing BMP cover (Crafti) MUST NEVER be overwritten! */
    assert(nl_capture_vram_to_icon(&state.games[crafti_idx], simulated_vram, true, NULL) == false);
    assert(state.games[crafti_idx].icon_source == NL_ICON_BMP);

    /* nDoom has no cover -> capture succeeds and NEVER overwrites thereafter */
    assert(nl_capture_vram_to_icon(&state.games[ndoom_idx], simulated_vram, true, NULL) == true);
    assert(state.games[ndoom_idx].icon_source == NL_ICON_CAPTURED);
    assert(nl_save_captured_icon(&state.games[ndoom_idx], state.covers_dir) == true);
    assert(nl_capture_vram_to_icon(&state.games[ndoom_idx], simulated_vram, true, NULL) == false);
    printf("[PASS] Cover aspect crop, black/blank rejection, VRAM capture & no-overwrite rule verified.\n");

    /* 5b. Direct 8bpp active-frame capture (bounds-safe 320x240 8-bit) */
    static uint8_t simulated_8bpp[NL_FB_PIXELS];
    static uint16_t hw_pal[256];
    for (int i = 0; i < 256; i++) {
        unsigned c5 = (unsigned)(i >> 3);
        hw_pal[i] = (uint16_t)((c5 << 10) | (((unsigned)(i >> 3)) << 5) | ((unsigned)((255 - i) >> 3)));
    }
    for (int y = 0; y < NL_SCREEN_H; y++) {
        for (int x = 0; x < NL_SCREEN_W; x++) {
            simulated_8bpp[y * NL_SCREEN_W + x] = (uint8_t)((x * 3 + y * 5) & 0xFF);
        }
    }
    int quake_idx = -1;
    for (int i = 0; i < state.total_games; i++) {
        if (strcmp(state.games[i].title, "nQuake") == 0) quake_idx = i;
    }
    assert(quake_idx >= 0);
    assert(nl_capture_frame_to_icon(&state.games[quake_idx], simulated_8bpp, false, true, hw_pal) == true);
    assert(state.games[quake_idx].icon_source == NL_ICON_CAPTURED);
    assert(nl_capture_frame_to_icon(&state.games[quake_idx], simulated_8bpp, false, true, hw_pal) == false);

    /* 5c. Diverse Game Profile Capture Reliability Suite (White-Bg, Primary-Color, GBC, 555, 4bpp, Rejections, Staging) */
    int idx_2048 = -1, idx_pacman = -1, idx_gbc = -1, idx_nkaruga = -1, idx_gpsp = -1;
    for (int i = 0; i < state.total_games; i++) {
        if (strcmp(state.games[i].filename, "2048-CX.tns") == 0) idx_2048 = i;
        else if (strcmp(state.games[i].filename, "PacMan-CX.tns") == 0) idx_pacman = i;
        else if (strcmp(state.games[i].filename, "gbc4nspire.tns") == 0) idx_gbc = i;
        else if (strcmp(state.games[i].filename, "nKaruga.tns") == 0) idx_nkaruga = i;
        else if (strcmp(state.games[i].filename, "gpSP-Nspire.tns") == 0) idx_gpsp = i;
    }
    assert(idx_2048 >= 0 && idx_pacman >= 0 && idx_gbc >= 0 && idx_nkaruga >= 0 && idx_gpsp >= 0);

    static uint16_t test_fb16[NL_FB_PIXELS];

    /* Rejection: Pure white frame (0xFFFF) MUST be rejected */
    for (int i = 0; i < NL_FB_PIXELS; i++) test_fb16[i] = 0xFFFFU;
    assert(nl_evaluate_frame_score(test_fb16, false, NL_PIX_RGB565, NULL) < 0);
    assert(nl_capture_frame_to_icon(&state.games[idx_2048], test_fb16, false, false, NULL) == false);

    /* Rejection: Solid blue frame MUST be rejected */
    for (int i = 0; i < NL_FB_PIXELS; i++) test_fb16[i] = RGB565(30, 80, 200);
    assert(nl_evaluate_frame_score(test_fb16, false, NL_PIX_RGB565, NULL) < 0);

    /* Rejection: Tiny 1-line "Loading..." band on black MUST be rejected (active_bands < 3) */
    memset(test_fb16, 0, sizeof(test_fb16));
    for (int y = 116; y < 124; y++) {
        for (int x = 104; x < 216; x++) {
            if (((x + y) & 3) == 0) test_fb16[y * NL_SCREEN_W + x] = 0xFFFFU;
        }
    }
    assert(nl_evaluate_frame_score(test_fb16, false, NL_PIX_RGB565, NULL) < 0);

    /* Acceptance 1: Light-mode 2048-CX (pure white #FFFFFF dominant background + colored tiles) */
    for (int i = 0; i < NL_FB_PIXELS; i++) test_fb16[i] = 0xFFFFU;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            uint16_t tile_col = ((r + c) & 1) ? RGB565(245, 149, 99) : RGB565(237, 204, 97);
            int bx = 56 + c * 54;
            int by = 18 + r * 52;
            for (int y = by; y < by + 44; y++) {
                for (int x = bx; x < bx + 46; x++) {
                    test_fb16[y * NL_SCREEN_W + x] = tile_col;
                }
            }
        }
    }
    assert(nl_evaluate_frame_score(test_fb16, false, NL_PIX_RGB565, NULL) > 0);
    assert(nl_capture_frame_to_icon(&state.games[idx_2048], test_fb16, false, false, NULL) == true);
    assert(state.games[idx_2048].icon_source == NL_ICON_CAPTURED);
    assert(nl_save_captured_icon(&state.games[idx_2048], state.covers_dir) == true);

    /* Acceptance 2: Primary-color Pac-Man on black background (would collapse under old XOR hash!) */
    memset(test_fb16, 0, sizeof(test_fb16));
    for (int y = 20; y < 220; y++) {
        for (int x = 20; x < 300; x++) {
            if (x < 24 || x >= 296 || y < 24 || y >= 216) {
                test_fb16[y * NL_SCREEN_W + x] = RGB565(0, 0, 255); /* Pure Blue */
            }
        }
    }
    for (int r = 0; r < 5; r++) {
        int dy = 44 + r * 36;
        for (int x = 44; x < 276; x += 20) {
            test_fb16[dy * NL_SCREEN_W + x] = RGB565(255, 255, 0); /* Pure Yellow */
        }
    }
    for (int y = 100; y < 120; y++) {
        for (int x = 80; x < 100; x++) test_fb16[y * NL_SCREEN_W + x] = RGB565(255, 0, 0);   /* Pure Red */
        for (int x = 140; x < 160; x++) test_fb16[y * NL_SCREEN_W + x] = RGB565(0, 255, 255); /* Pure Cyan */
    }
    assert(nl_evaluate_frame_score(test_fb16, false, NL_PIX_RGB565, NULL) > 0);
    assert(nl_capture_frame_to_icon(&state.games[idx_pacman], test_fb16, false, false, NULL) == true);
    assert(state.games[idx_pacman].icon_source == NL_ICON_CAPTURED);

    /* Acceptance 3: Letterboxed GBC (160x144 centered 4-shade monochrome + black sidebars) */
    memset(test_fb16, 0, sizeof(test_fb16));
    static const uint16_t gb_shades[4] = {
        RGB565(155, 188, 15), RGB565(139, 172, 15), RGB565(48, 98, 48), RGB565(15, 56, 15)
    };
    for (int y = 48; y < 192; y++) {
        for (int x = 80; x < 240; x++) {
            test_fb16[y * NL_SCREEN_W + x] = gb_shades[((x >> 3) + (y >> 3)) & 3];
        }
    }
    assert(nl_evaluate_frame_score(test_fb16, false, NL_PIX_RGB565, NULL) > 0);
    assert(nl_capture_frame_to_icon(&state.games[idx_gbc], test_fb16, false, false, NULL) == true);

    /* Acceptance 4: Rotated RGB555 frame (SCR_240x320_555) */
    for (int x = 0; x < NL_SCREEN_W; x++) {
        for (int y = 0; y < NL_SCREEN_H; y++) {
            unsigned r5 = (unsigned)((x * 31) / NL_SCREEN_W);
            unsigned g5 = (unsigned)((y * 31) / NL_SCREEN_H);
            unsigned b5 = (unsigned)(((x + y) & 31));
            test_fb16[x * NL_SCREEN_H + y] = (uint16_t)((r5 << 10) | (g5 << 5) | b5);
        }
    }
    assert(nl_capture_frame_to_icon_ex(&state.games[idx_nkaruga], test_fb16, true, NL_PIX_RGB555, NULL, false, NULL) == true);
    assert(state.games[idx_nkaruga].icon_source == NL_ICON_CAPTURED);

    /* Acceptance 5: 4bpp grayscale + 2-stage staging + richness upgrade + promotion */
    static uint8_t test_fb4[NL_FB_PIXELS / 2];
    /* Stage A: simple striped 4bpp pattern */
    for (int y = 0; y < NL_SCREEN_H; y++) {
        for (int x = 0; x < NL_SCREEN_W; x += 2) {
            uint8_t n0 = (uint8_t)((y & 32) ? 14 : 2);
            uint8_t n1 = n0;
            test_fb4[(y * NL_SCREEN_W + x) >> 1] = (uint8_t)((n0 << 4) | n1);
        }
    }
    int score_stage_a = -1;
    assert(nl_capture_frame_to_icon_ex(&state.games[idx_gpsp], test_fb4, false, NL_PIX_4BPP, NULL, true, &score_stage_a) == true);
    assert(state.games[idx_gpsp].icon_source == NL_ICON_NONE); /* Still staged */
    assert(score_stage_a > 0);

    /* Stage B: rich multi-shade checkerboard/sprite 4bpp frame outscores Stage A */
    for (int y = 0; y < NL_SCREEN_H; y++) {
        for (int x = 0; x < NL_SCREEN_W; x += 2) {
            uint8_t n0 = (uint8_t)(((x >> 3) + (y >> 3) * 3) & 0x0F);
            uint8_t n1 = (uint8_t)((((x + 1) >> 3) + (y >> 3) * 3) & 0x0F);
            test_fb4[(y * NL_SCREEN_W + x) >> 1] = (uint8_t)((n0 << 4) | n1);
        }
    }
    int score_stage_b = -1;
    assert(nl_capture_frame_to_icon_ex(&state.games[idx_gpsp], test_fb4, false, NL_PIX_4BPP, NULL, true, &score_stage_b) == true);
    assert(score_stage_b > score_stage_a);
    state.games[idx_gpsp].icon_source = NL_ICON_CAPTURED; /* Promote on early exit */
    assert(nl_save_captured_icon(&state.games[idx_gpsp], state.covers_dir) == true);
    /* Once captured, non-overwrite guard MUST block any overwrite */
    assert(nl_capture_frame_to_icon_ex(&state.games[idx_gpsp], test_fb16, false, NL_PIX_RGB565, NULL, false, NULL) == false);
    printf("[PASS] Multi-format & multi-game capture suite (white-bg, primary-color, GBC, 555, 4bpp, staging, no-overwrite).\n");

    /* 6. Vertical Scroll, Dot Glide, Interruptible Per-Card Scale & Launch Guard */
    int g0 = state.filtered_indices[0];
    int g6 = state.filtered_indices[6];
    assert(state.anim.card_scale_fp[g0] == NL_SCALE_SELECTED);

    for (int i = 0; i < 6; i++) nl_handle_key(&state, NL_KEY_RIGHT);
    assert(state.selected_pos == 6);
    assert(state.current_page == 1);
    nl_layout_metrics_t lm_s6;
    nl_get_layout_metrics_full(state.settings.layout, state.settings.scroll_dir,
                               state.settings.show_names, state.settings.show_shortcuts,
                               state.filtered_count, &lm_s6);
    assert(state.anim.target_scroll_y_fp == (lm_s6.page_stride_y << 4));
    assert(state.anim.scroll_y_fp != state.anim.target_scroll_y_fp);

    /* Step partway then interrupt immediately back to Left (no lockup) */
    for (int f = 0; f < 3; f++) nl_anim_step(&state);
    nl_handle_key(&state, NL_KEY_LEFT);
    assert(state.selected_pos == 5);
    assert(state.current_page == 0);
    assert(state.anim.target_scroll_y_fp == 0);

    for (int f = 0; f < 40; f++) nl_anim_step(&state);
    assert(state.anim.scroll_y_fp == 0);
    assert(state.anim.dot_pos_fp == 0);
    assert(state.anim.card_scale_fp[g6] == NL_SCALE_NORMAL);
    assert(state.anim.card_scale_fp[state.filtered_indices[5]] == NL_SCALE_SELECTED);

    /* Launch guard prevents second launch during active launch animation */
    state.anim.launch_anim_active = true;
    assert(nl_handle_key(&state, NL_KEY_EXE) == false);
    state.anim.launch_anim_active = false;
    assert(nl_handle_key(&state, NL_KEY_EXE) == true);
    assert(state.launch_requested == true);
    state.launch_requested = false;
    printf("[PASS] Vertical scroll (156px), interruptible card scale, dot glide & launch guard verified.\n");

    /* 7. Config & Reload Persistence + Category Filtering */
    nl_handle_key(&state, NL_KEY_TAB);
    assert(state.active_category == NL_CAT_FAVORITES);
    nl_handle_key(&state, NL_KEY_TAB);
    assert(state.active_category == NL_CAT_RECENT);
    assert(state.filtered_count == 1); /* Launched game recorded in REC */
    nl_handle_key(&state, NL_KEY_ESC);
    assert(state.active_category == NL_CAT_ALL);

    const char *cfg_file = "/tmp/nlauncher_test_fs/ndless/nlauncher.cfg.tns";
    assert(nl_save_config(&state, cfg_file) == true);

    static nl_state_t reloaded;
    nl_init_state(&reloaded);
    snprintf(reloaded.covers_dir, sizeof(reloaded.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
    nl_scan_directory(&reloaded, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
    assert(nl_load_config(&reloaded, cfg_file) == true);
    nl_load_all_icons(&reloaded);
    assert(reloaded.games[ndoom_idx].icon_source == NL_ICON_CAPTURED);
    assert(reloaded.games[crafti_idx].icon_source == NL_ICON_BMP);

    /* 8. Render Splash, Navigation Highlight (only while navigating), Peek & Launch Zoom Bounce */
    static uint16_t fb[NL_FB_PIXELS];
    static uint16_t fb_no_hl[NL_FB_PIXELS];
    nl_render_splash(&reloaded, fb, 10, 12);

    reloaded.anim.launch_anim_active = false;
    nl_render_frame(&reloaded, fb);
    reloaded.anim.launch_anim_active = true;
    nl_render_frame(&reloaded, fb_no_hl);
    reloaded.anim.launch_anim_active = false;

    /* Verify subtle navigation luminance differs around slot 0 ONLY during navigation, zero on launch */
    nl_layout_metrics_t lm_s8;
    nl_get_layout_metrics_full(reloaded.settings.layout, reloaded.settings.scroll_dir,
                               reloaded.settings.show_names, reloaded.settings.show_shortcuts,
                               reloaded.filtered_count, &lm_s8);
    int lum_diff_px = 0;
    for (int y = lm_s8.top_y - 4; y < lm_s8.top_y + lm_s8.card_h + 4; y++) {
        for (int x = lm_s8.margin_x - 4; x < lm_s8.margin_x + lm_s8.card_w + 4; x++) {
            if (x >= 0 && x < NL_SCREEN_W && y >= NL_VIEW_TOP && y < lm_s8.view_bottom) {
                if (fb[y * NL_SCREEN_W + x] != fb_no_hl[y * NL_SCREEN_W + x]) lum_diff_px++;
            }
        }
    }
    assert(lum_diff_px > 40);

    nl_render_frame(&reloaded, fb);
    assert(export_fb_to_bmp("frame_preview.bmp", fb, NL_SCREEN_W, NL_SCREEN_H) == true);

    /* Verify #R17 natural vertical bottom peek (Page 1 starts at y = top_y + page_stride_y = view_bottom - 12)
     * drew non-bg pixels cleanly without overflowing past NL_VIEW_BOTTOM (y=205..209 must stay pure bg_main) */
    const nl_theme_t *th = nl_get_theme(reloaded.display_mode, reloaded.accent_color);
    int peek_pixels = 0;
    int p1_x = 0, p1_y = 0;
    nl_compute_grid_slot_pos(&lm_s8, reloaded.filtered_count, lm_s8.per_page, &p1_x, &p1_y);
    int peek_probe_x = p1_x + lm_s8.card_w / 2;
    for (int y = lm_s8.view_bottom - 12; y < lm_s8.view_bottom; y++) {
        if (fb[y * NL_SCREEN_W + peek_probe_x] != th->bg_main) peek_pixels++;
    }
    assert(peek_pixels > 0);
    for (int y = NL_VIEW_BOTTOM; y < NL_PAGINATION_Y; y++) {
        for (int x = 10; x < 310; x++) {
            assert(fb[y * NL_SCREEN_W + x] == th->bg_main);
        }
    }

    reloaded.anim.launch_anim_active = true;
    nl_render_launch_zoom(&reloaded, fb, 9, 13);
    reloaded.anim.launch_anim_active = false;
    assert(export_fb_to_bmp("frame_zoom_preview.bmp", fb, NL_SCREEN_W, NL_SCREEN_H) == true);

    /* 9. Mode Dark/Light/Gray (DOCK) + 4 Accent Colors (MENU) + Independence + Persistence */
    assert(reloaded.display_mode == NL_DISPLAY_DARK);
    assert(reloaded.accent_color == NL_ACCENT_BLUE);

    /* Snapshot page/selection/animations/toast before toggling */
    nl_handle_key(&reloaded, NL_KEY_PAGE_NEXT);
    for (int f = 0; f < 30; f++) nl_anim_step(&reloaded);
    int saved_pos = reloaded.selected_pos;
    int saved_page = reloaded.current_page;
    int saved_scroll = reloaded.anim.scroll_y_fp;
    int saved_toast = reloaded.anim.toast_ticks;

    /* DOCK cycles Dark -> Light -> Gray -> Dark independently, ZERO toast, ZERO page/selection reset */
    assert(nl_handle_key(&reloaded, NL_KEY_DOC) == true);
    assert(reloaded.display_mode == NL_DISPLAY_LIGHT);
    assert(reloaded.accent_color == NL_ACCENT_BLUE);
    assert(reloaded.selected_pos == saved_pos);
    assert(reloaded.current_page == saved_page);
    assert(reloaded.anim.scroll_y_fp == saved_scroll);
    assert(reloaded.anim.toast_ticks == saved_toast);

    assert(nl_handle_key(&reloaded, NL_KEY_DOC) == true);
    assert(reloaded.display_mode == NL_DISPLAY_GRAY);
    assert(reloaded.accent_color == NL_ACCENT_BLUE);
    assert(nl_handle_key(&reloaded, NL_KEY_DOC) == true);
    assert(reloaded.display_mode == NL_DISPLAY_DARK);
    assert(nl_handle_key(&reloaded, NL_KEY_DOC) == true);
    assert(reloaded.display_mode == NL_DISPLAY_LIGHT);

    /* MENU cycles strictly Blue -> Red -> Green -> Yellow -> Blue independently */
    assert(nl_handle_key(&reloaded, NL_KEY_MENU) == true);
    assert(reloaded.accent_color == NL_ACCENT_RED);
    assert(reloaded.display_mode == NL_DISPLAY_LIGHT);

    assert(nl_handle_key(&reloaded, NL_KEY_MENU) == true);
    assert(reloaded.accent_color == NL_ACCENT_GREEN);
    assert(reloaded.display_mode == NL_DISPLAY_LIGHT);

    assert(nl_handle_key(&reloaded, NL_KEY_MENU) == true);
    assert(reloaded.accent_color == NL_ACCENT_YELLOW);
    assert(reloaded.display_mode == NL_DISPLAY_LIGHT);

    assert(nl_handle_key(&reloaded, NL_KEY_MENU) == true);
    assert(reloaded.accent_color == NL_ACCENT_BLUE);

    /* Leave in Light + Green, save config, reload and verify persistence */
    nl_handle_key(&reloaded, NL_KEY_MENU); /* Red */
    nl_handle_key(&reloaded, NL_KEY_MENU); /* Green */
    assert(reloaded.display_mode == NL_DISPLAY_LIGHT);
    assert(reloaded.accent_color == NL_ACCENT_GREEN);
    assert(nl_save_config(&reloaded, cfg_file) == true);

    static nl_state_t persisted;
    nl_init_state(&persisted);
    snprintf(persisted.covers_dir, sizeof(persisted.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
    nl_scan_directory(&persisted, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
    assert(nl_load_config(&persisted, cfg_file) == true);
    nl_load_all_icons(&persisted);
    assert(persisted.display_mode == NL_DISPLAY_LIGHT);
    assert(persisted.accent_color == NL_ACCENT_GREEN);

    /* Verify all 8 combinations render distinctly and NEVER recolor real game covers */
    uint16_t cover_sample_dark_blue = 0;
    for (int m = 0; m < NL_DISPLAY_COUNT; m++) {
        for (int a = 0; a < NL_ACCENT_COUNT; a++) {
            persisted.display_mode = (nl_display_mode_t)m;
            persisted.accent_color = (nl_accent_color_t)a;
            persisted.selected_pos = 0;
            persisted.current_page = 0;
            persisted.anim.scroll_y_fp = 0;
            persisted.anim.dot_pos_fp = 0;
            nl_render_frame(&persisted, fb);
            /* Pick interior pixel of a non-selected card with real cover (slot != 0) */
            nl_layout_metrics_t lm_s9;
            nl_get_layout_metrics_full(persisted.settings.layout, persisted.settings.scroll_dir,
                                       persisted.settings.show_names, persisted.settings.show_shortcuts,
                                       persisted.filtered_count, &lm_s9);
            for (int slot = 1; slot < persisted.filtered_count && slot < 6; slot++) {
                int gi = persisted.filtered_indices[slot];
                if (persisted.games[gi].icon_source != NL_ICON_NONE) {
                    int sx_c = 0, sy_c = 0;
                    nl_compute_grid_slot_pos(&lm_s9, persisted.filtered_count, slot, &sx_c, &sy_c);
                    int px_x = sx_c + (lm_s9.card_w / 2);
                    int px_y = sy_c + (lm_s9.card_h / 2);
                    uint16_t sample = fb[px_y * NL_SCREEN_W + px_x];
                    if (cover_sample_dark_blue == 0) cover_sample_dark_blue = sample;
                    assert(sample == cover_sample_dark_blue);
                    break;
                }
            }
        }
    }
    assert(cover_sample_dark_blue != 0);

    /* 10. Exhaustive Pagination Indicator Pixel Check (1..19 circular dots; first/mid/penult/last) */
    static const int page_counts[] = {1, 2, 3, 5, 10, 15, 19};
    for (size_t tc = 0; tc < sizeof(page_counts) / sizeof(page_counts[0]); tc++) {
        int tp = page_counts[tc];
        int vis_expected = (tp > NL_PAGINATION_MAX_DOTS) ? NL_PAGINATION_MAX_DOTS : tp;
        int check_pages[4] = {0, tp / 2, tp >= 2 ? tp - 2 : 0, tp - 1};
        for (int ci = 0; ci < 4; ci++) {
            int p = check_pages[ci];
            persisted.total_pages = tp;
            persisted.current_page = p;
            persisted.anim.dot_pos_fp = p << 4;
            for (int m = 0; m < NL_DISPLAY_COUNT; m++) {
                persisted.display_mode = (nl_display_mode_t)m;
                persisted.accent_color = (nl_accent_color_t)((p + m) % NL_ACCENT_COUNT);
                const nl_theme_t *cth = nl_get_theme(persisted.display_mode, persisted.accent_color);
                nl_render_frame(&persisted, fb);

                /* Scan equator scanline of pagination indicator (y = NL_PAGINATION_Y + 3) */
                int mid_y = NL_PAGINATION_Y + 3;
                int run_widths[24];
                uint16_t run_cols[24];
                int run_starts[24];
                int run_count = 0;
                int x = 0;
                while (x < NL_SCREEN_W) {
                    uint16_t c = fb[mid_y * NL_SCREEN_W + x];
                    if (c != cth->bg_main) {
                        int sx = x;
                        while (x < NL_SCREEN_W && fb[mid_y * NL_SCREEN_W + x] == c) x++;
                        assert(run_count < 24);
                        run_starts[run_count] = sx;
                        run_widths[run_count] = x - sx;
                        run_cols[run_count] = c;
                        run_count++;
                    } else {
                        x++;
                    }
                }
                /* Exact count of visible indicators, zero overlap, uniform 6px gaps */
                assert(run_count == vis_expected);
                int active_found = 0;
                for (int r = 0; r < run_count; r++) {
                    int expected_w = (run_cols[r] == cth->accent_primary) ? 18 : 8;
                    if (run_cols[r] == cth->accent_primary) {
                        assert(run_widths[r] == 18);
                        active_found++;
                    } else {
                        assert(run_cols[r] == cth->dot_inactive);
                        assert(run_widths[r] == 8);
                    }
                    /* Verify anti-aliased top/bottom symmetry and non-hard rounded boundary on row 0 */
                    int top_non_bg = 0;
                    int top_aa_blended = 0;
                    for (int tx = run_starts[r]; tx < run_starts[r] + expected_w; tx++) {
                        uint16_t tp = fb[NL_PAGINATION_Y * NL_SCREEN_W + tx];
                        uint16_t bp = fb[(NL_PAGINATION_Y + 7) * NL_SCREEN_W + tx];
                        assert(tp == bp);
                        if (tp != cth->bg_main) {
                            top_non_bg++;
                            if (tp != run_cols[r]) top_aa_blended++;
                        }
                    }
                    assert(top_non_bg == expected_w - 2); /* cols 0 and w-1 are 0, middle columns filled */
                    assert(top_aa_blended >= 4);         /* smooth anti-aliased shoulder pixels */
                    if (r > 0) {
                        int gap = run_starts[r] - (run_starts[r - 1] + run_widths[r - 1]);
                        assert(gap == 6);
                    }
                }
                assert(active_found == 1);
            }
        }
    }
    printf("[PASS] DOCK light/dark, MENU 4-color cycle, persistence, 8 combinations & pagination verified.\n");
    printf("[PASS] Framebuffer rendering verified (splash, subtle nav highlight, rounded clip, 17px bottom peek, launch zoom).\n");

    /* 11. Startup Logo Animation, 8-Dot Spinner, Real-Time Scan Callback & Nested/Fault-Tolerant Persistence */
    int scan_cb_count = 0;
    int icon_cb_count = 0;
    static nl_state_t boot_test;
    nl_init_state(&boot_test);
    snprintf(boot_test.covers_dir, sizeof(boot_test.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
    /* Nested config dir that does NOT exist yet -> nl_save_config must create parents & flush */
    const char *nested_cfg = "/tmp/nlauncher_test_fs/custom_prefs/sub/nlauncher.cfg.tns";
    boot_test.display_mode = NL_DISPLAY_LIGHT;
    boot_test.accent_color = NL_ACCENT_YELLOW;
    assert(nl_save_config(&boot_test, nested_cfg) == true);

    /* Reload BEFORE scan & verify ZERO default-color flash on splash frame 0 */
    static nl_state_t cold_boot;
    nl_init_state(&cold_boot);
    snprintf(cold_boot.covers_dir, sizeof(cold_boot.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
    assert(nl_load_config(&cold_boot, nested_cfg) == true);
    assert(cold_boot.display_mode == NL_DISPLAY_LIGHT);
    assert(cold_boot.accent_color == NL_ACCENT_YELLOW);

    static uint16_t splash_fb0[NL_FB_PIXELS];
    static uint16_t splash_fb1[NL_FB_PIXELS];
    nl_render_splash_anim(&cold_boot, splash_fb0, 0U, true);
    const nl_theme_t *ly_th = nl_get_theme(NL_DISPLAY_LIGHT, NL_ACCENT_YELLOW);
    assert(splash_fb0[0] == ly_th->bg_main); /* Background pixel 0 matches Light mode immediately */

    /* Cooperative progress callbacks fire multiple times during real scan & icon loading */
    nl_scan_directory_ex(&cold_boot, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4,
                         test_on_progress_tick, &scan_cb_count);
    assert(scan_cb_count >= 5);
    assert(nl_load_config(&cold_boot, nested_cfg) == true);
    assert(cold_boot.display_mode == NL_DISPLAY_LIGHT);
    assert(cold_boot.accent_color == NL_ACCENT_YELLOW);
    nl_load_all_icons_ex(&cold_boot, test_on_progress_tick, &icon_cb_count);
    assert(icon_cb_count == cold_boot.total_games);

    /* Verify proportional 1:1 emblem zoom & breathing pulse bounds + visible 8-dot spinner across ALL 8 themes */
    for (int m = 0; m < NL_DISPLAY_COUNT; m++) {
        for (int a = 0; a < NL_ACCENT_COUNT; a++) {
            cold_boot.display_mode = (nl_display_mode_t)m;
            cold_boot.accent_color = (nl_accent_color_t)a;
            const nl_theme_t *bth = nl_get_theme(cold_boot.display_mode, cold_boot.accent_color);

            uint32_t sample_times[4] = {0U, 240U, 720U, 1200U}; /* 24px -> 36px -> 44px -> 36px */
            int prev_w = 0;
            for (int ti = 0; ti < 4; ti++) {
                nl_render_splash_anim(&cold_boot, splash_fb0, sample_times[ti], true);
                assert(splash_fb0[0] == bth->bg_main);

                /* Measure emblem bounding box around (160, 96) */
                int min_x = 999, max_x = -1, min_y = 999, max_y = -1;
                for (int y = 70; y <= 122; y++) {
                    for (int x = 134; x <= 186; x++) {
                        if (splash_fb0[y * NL_SCREEN_W + x] != bth->bg_main) {
                            if (x < min_x) min_x = x;
                            if (x > max_x) max_x = x;
                            if (y < min_y) min_y = y;
                            if (y > max_y) max_y = y;
                        }
                    }
                }
                int bw = max_x - min_x + 1;
                int bh = max_y - min_y + 1;
                assert(bw == bh); /* Strictly 1:1 proportional square emblem */
                assert(bw >= 24 && bw <= 44);
                if (ti == 0) assert(bw == 24);
                if (ti == 1) assert(bw == 36 && bw > prev_w);
                if (ti == 2) assert(bw == 44 && bw > prev_w);
                if (ti == 3) assert(bw == 36 && bw < prev_w);
                prev_w = bw;

                /* Verify 8-dot circular loading spinner around (160, 158) contains active non-bg pixels */
                int spin_px = 0;
                for (int y = 144; y <= 172; y++) {
                    for (int x = 146; x <= 174; x++) {
                        if (splash_fb0[y * NL_SCREEN_W + x] != bth->bg_main) spin_px++;
                    }
                }
                assert(spin_px >= 64);
            }

            /* Verify boot crossfade converges strictly to lib_fb */
            static uint16_t lib_fb[NL_FB_PIXELS];
            static uint16_t trans_fb[NL_FB_PIXELS];
            nl_render_splash_anim(&cold_boot, splash_fb1, 720U, false);
            nl_render_frame(&cold_boot, lib_fb);
            nl_render_boot_transition(&cold_boot, trans_fb, splash_fb1, lib_fb, 0, 5);
            assert(memcmp(trans_fb, splash_fb1, NL_FB_BYTES) == 0);
            nl_render_boot_transition(&cold_boot, trans_fb, splash_fb1, lib_fb, 5, 5);
            assert(memcmp(trans_fb, lib_fb, NL_FB_BYTES) == 0);
        }
    }

    /* Fault-tolerant config parsing with whitespace, CRLF, and corrupted/unknown keys */
    const char *dirty_cfg = "/tmp/nlauncher_test_fs/ndless/dirty_prefs.cfg.tns";
    FILE *dfp = fopen(dirty_cfg, "wb");
    assert(dfp != NULL);
    fprintf(dfp, "display_mode = light  \r\n");
    fprintf(dfp, "accent_color = red\t \r\n");
    fprintf(dfp, "broken_line_no_equals\r\n");
    fclose(dfp);
    assert(nl_load_config(&cold_boot, dirty_cfg) == true);
    assert(cold_boot.display_mode == NL_DISPLAY_LIGHT);
    assert(cold_boot.accent_color == NL_ACCENT_RED);

    /* Changing ONLY DOCK cycles Light -> Gray -> Dark while preserving Red; missing file falls back cleanly */
    nl_handle_key(&cold_boot, NL_KEY_DOC);
    assert(cold_boot.display_mode == NL_DISPLAY_GRAY && cold_boot.accent_color == NL_ACCENT_RED);
    nl_handle_key(&cold_boot, NL_KEY_DOC);
    assert(cold_boot.display_mode == NL_DISPLAY_DARK && cold_boot.accent_color == NL_ACCENT_RED);
    assert(nl_save_config(&cold_boot, dirty_cfg) == true);
    assert(nl_load_config(&boot_test, dirty_cfg) == true);
    assert(boot_test.display_mode == NL_DISPLAY_DARK && boot_test.accent_color == NL_ACCENT_RED);

    static nl_state_t missing_cfg_state;
    nl_init_state(&missing_cfg_state);
    assert(nl_load_config(&missing_cfg_state, "/tmp/nlauncher_test_fs/nonexistent/none.cfg.tns") == false);
    assert(missing_cfg_state.display_mode == NL_DISPLAY_DARK);
    assert(missing_cfg_state.accent_color == NL_ACCENT_BLUE);
    printf("[PASS] Startup logo zoom/pulse, 8-dot spinner (8 themes), progressive scan & resilient persistence verified.\n");

    /* 9b. Organic ink diffusion transition for DOCK / MENU */
    static nl_state_t ink_st;
    nl_init_state(&ink_st);
    assert(ink_st.anim.ink_active == false);
    assert(ink_st.anim.ink_max == NL_INK_TRANS_TICKS);

    static uint16_t ink_old_fb[NL_FB_PIXELS];
    static uint16_t ink_new_fb[NL_FB_PIXELS];
    static uint16_t ink_mid_fb[NL_FB_PIXELS];
    static uint16_t ink_out_fb[NL_FB_PIXELS];

    /* Distinct solid-and-UI old vs new frames to inspect spatial mask cleanly */
    nl_render_frame(&ink_st, ink_old_fb);
    assert(nl_handle_key(&ink_st, NL_KEY_DOC) == true);
    assert(ink_st.display_mode == NL_DISPLAY_LIGHT);
    assert(ink_st.accent_color == NL_ACCENT_BLUE);
    assert(ink_st.anim.ink_active == true);
    assert(ink_st.anim.ink_tick == 0);
    assert(ink_st.anim.ink_seq == 1U);
    nl_render_frame(&ink_st, ink_new_fb);

    /* Exact endpoint checks */
    nl_render_ink_transition(ink_out_fb, ink_old_fb, ink_new_fb, ink_st.anim.ink_seq, 0, NL_INK_TRANS_TICKS);
    assert(memcmp(ink_out_fb, ink_old_fb, NL_FB_BYTES) == 0);
    nl_render_ink_transition(ink_out_fb, ink_old_fb, ink_new_fb, ink_st.anim.ink_seq, NL_INK_TRANS_TICKS, NL_INK_TRANS_TICKS);
    assert(memcmp(ink_out_fb, ink_new_fb, NL_FB_BYTES) == 0);

    /* Mid-expansion (~35% spatial front at step=3) mask test on contrasting sentinel colors */
    static uint16_t mask_old[NL_FB_PIXELS];
    static uint16_t mask_new[NL_FB_PIXELS];
    for (int i = 0; i < NL_FB_PIXELS; i++) {
        mask_old[i] = RGB565(0, 0, 0);
        mask_new[i] = RGB565(248, 252, 248);
    }
    nl_render_ink_transition(ink_mid_fb, mask_old, mask_new, ink_st.anim.ink_seq, 3, NL_INK_TRANS_TICKS);
    /* Center MUST already be transitioned, outer corner MUST still be old */
    assert(ink_mid_fb[120 * NL_SCREEN_W + 160] == mask_new[0]);
    assert(ink_mid_fb[4 * NL_SCREEN_W + 4] == mask_old[0]);

    /* Sample frontier radii across 8 rays to verify organic non-circular asymmetry & zero detached blobs */
    const int dx8[8] = {100, 71, 0, -71, -100, -71, 0, 71};
    const int dy8[8] = {0, 71, 100, 71, 0, -71, -100, -71};
    int min_ray = 999, max_ray = -1;
    for (int d = 0; d < 8; d++) {
        int last_new_r = 0;
        bool seen_old = false;
        bool has_soft_edge = false;
        for (int dist = 1; dist <= 118; dist++) {
            int sx = 160 + (dx8[d] * dist) / 100;
            int sy = 120 + (dy8[d] * dist) / 100;
            uint16_t px = ink_mid_fb[sy * NL_SCREEN_W + sx];
            if (px == mask_new[0]) {
                assert(!seen_old); /* Strictly connected: zero detached splashes outside frontier */
                last_new_r = dist;
            } else if (px != mask_old[0]) {
                has_soft_edge = true;
            } else {
                seen_old = true;
            }
        }
        assert(seen_old);
        assert(has_soft_edge); /* Continuous anti-aliased edge */
        if (last_new_r < min_ray) min_ray = last_new_r;
        if (last_new_r > max_ray) max_ray = last_new_r;
    }
    /* Organic asymmetry: boundary is NOT a uniform circle, yet remains gentle (no wild spikes) */
    assert(max_ray - min_ray >= 6);
    assert(max_ray - min_ray <= 40);

    /* Advance animation partway, interrupt cleanly with MENU press, verify restart & completion */
    for (int t = 0; t < 5; t++) {
        assert(nl_anim_step(&ink_st) == true);
    }
    assert(ink_st.anim.ink_active == true && ink_st.anim.ink_tick == 5);
    assert(nl_handle_key(&ink_st, NL_KEY_MENU) == true);
    assert(ink_st.display_mode == NL_DISPLAY_LIGHT);
    assert(ink_st.accent_color == NL_ACCENT_RED);
    assert(ink_st.anim.ink_active == true && ink_st.anim.ink_tick == 0 && ink_st.anim.ink_seq == 2U);
    assert(ink_st.selected_pos == 0 && ink_st.current_page == 0);

    for (int t = 0; t < NL_INK_TRANS_TICKS; t++) {
        assert(nl_anim_step(&ink_st) == true);
    }
    assert(ink_st.anim.ink_active == false);
    assert(ink_st.anim.ink_tick == NL_INK_TRANS_TICKS);

    /* Launching a game cancels any running ink transition cleanly */
    assert(nl_handle_key(&ink_st, NL_KEY_MENU) == true);
    assert(ink_st.anim.ink_active == true);
    ink_st.total_games = 1;
    ink_st.filtered_count = 1;
    ink_st.filtered_indices[0] = 0;
    ink_st.selected_pos = 0;
    nl_handle_key(&ink_st, NL_KEY_EXE);
    assert(ink_st.anim.ink_active == false);
    printf("[PASS] Organic ink diffusion transition (asymmetry=%d px, soft edge, interruptible) verified.\n",
           max_ray - min_ray);

    nl_free_icons(&state);
    nl_free_icons(&reloaded);
    nl_free_icons(&persisted);
    nl_free_icons(&boot_test);
    nl_free_icons(&cold_boot);
    nl_free_icons(&ink_st);

    /* 12. Settings Menu, Layouts (1x1..4x4), Show Names, Autostart, Shortcuts, Sort, Stats, Search & Safe Reset */
    static nl_state_t cfg_test;
    nl_init_state(&cfg_test);
    snprintf(cfg_test.covers_dir, sizeof(cfg_test.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
    snprintf(cfg_test.sys.docs_root, sizeof(cfg_test.sys.docs_root), "/tmp/nlauncher_test_fs");
    snprintf(cfg_test.sys.self_path, sizeof(cfg_test.sys.self_path), "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns");
    cfg_test.sys.has_ndless = true;
    cfg_test.sys.ndless_rev = 2026U;
    cfg_test.sys.hw_is_cx2 = true;
    snprintf(cfg_test.sys.os_version, sizeof(cfg_test.sys.os_version), "%s",
             nl_resolve_os_version_string(0x1042A600U));
    assert(strcmp(cfg_test.sys.os_version, "6.4.0.74 (CX II-T)") == 0);

    nl_scan_directory(&cfg_test, "/tmp/nlauncher_test_fs", cfg_test.sys.self_path, 4);
    nl_load_all_icons(&cfg_test);
    assert(cfg_test.total_games == 8);

    /* 12a. Top-bar round gear icon rendered in accent_primary, top-right battery indicator permanently removed
     *      (x = 250..290, y = 7..18 is pure bg_top_bar), & Ctrl+S opens/closes settings */
    nl_render_frame(&cfg_test, fb);
    const nl_theme_t *gth = nl_get_theme(cfg_test.display_mode, cfg_test.accent_color);
    assert(fb[(NL_GEAR_BTN_Y + 2) * NL_SCREEN_W + (NL_GEAR_BTN_X + 7)] == gth->accent_primary);
    /* Verify top-right space (x = 250..290, y = 7..18) is completely clean (zero battery icon, zero game counter) */
    for (int y = 7; y <= 18; y++) {
        for (int x = 250; x <= 290; x++) {
            assert(fb[y * NL_SCREEN_W + x] == gth->bg_top_bar);
        }
    }

    assert(nl_handle_key(&cfg_test, NL_KEY_SETTINGS) == true);
    assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
    assert(nl_handle_key(&cfg_test, NL_KEY_ESC) == true);
    assert(cfg_test.sui.screen == NL_UI_LIBRARY);

    /* 12b. All 5 layouts (1x1, 2x2, 3x2, 3x3, 4x4) with show_names ON/OFF and show_shortcuts ON/OFF stay in bounds */
    assert(cfg_test.settings.show_shortcuts == true);
    assert(cfg_test.anim.switch_shortcuts_fp == 256);
    for (int sc = 1; sc >= 0; sc--) {
        cfg_test.settings.show_shortcuts = (sc != 0);
        cfg_test.anim.switch_shortcuts_fp = (sc != 0) ? 256 : 0;
        for (int lay = 0; lay < NL_LAYOUT_COUNT; lay++) {
            for (int sn = 0; sn < 2; sn++) {
                nl_layout_metrics_t lm;
                nl_get_layout_metrics_ex((nl_layout_t)lay, sn != 0, sc != 0, &lm);
                assert(lm.cols >= 1 && lm.rows >= 1);
                assert(lm.margin_x >= 2);
                assert(lm.margin_x + lm.cols * lm.card_w + (lm.cols - 1) * lm.gap_x <= NL_SCREEN_W);
                assert(lm.view_bottom == ((sc != 0) ? NL_VIEW_BOTTOM : NL_VIEW_BOTTOM_NO_SC));
                assert(lm.pagination_y == ((sc != 0) ? NL_PAGINATION_Y : NL_PAGINATION_Y_NO_SC));
                assert(lm.top_y + lm.rows * lm.card_h + lm.rows * lm.label_h <= lm.view_bottom + 6);

                cfg_test.settings.layout = (nl_layout_t)lay;
                cfg_test.settings.show_names = (sn != 0);
                nl_rebuild_filter(&cfg_test);
                nl_render_frame(&cfg_test, fb);
                for (int y = lm.view_bottom; y < lm.pagination_y; y++) {
                    for (int x = 10; x < 310; x++) {
                        assert(fb[y * NL_SCREEN_W + x] == gth->bg_main);
                    }
                }
                if (sc == 0) {
                    /* Bottom bar is completely hidden: bottom scanlines (y = 235..239, x >= 32 right of daniel.b) are pure bg_main */
                    for (int y = 235; y < NL_SCREEN_H; y++) {
                        for (int x = 32; x < 310; x++) {
                            assert(fb[y * NL_SCREEN_W + x] == gth->bg_main);
                        }
                    }
                    /* Pagination capsule is drawn at NL_PAGINATION_Y_NO_SC (y = 226..233) */
                    int pag_px = 0;
                    for (int x = 80; x < 240; x++) {
                        if (fb[(NL_PAGINATION_Y_NO_SC + 3) * NL_SCREEN_W + x] != gth->bg_main) pag_px++;
                    }
                    assert(pag_px >= 8);
                } else {
                    /* Bottom bar is visible at y = 221..239 */
                    assert(fb[237 * NL_SCREEN_W + 32] == gth->bg_bottom_bar);
                }
            }
        }
    }

    /* Toggle show_names and show_shortcuts smooth switch animations (0 <-> 256) and via AFFICHAGE submenu */
    cfg_test.settings.layout = NL_LAYOUT_3X2;
    cfg_test.settings.show_names = false;
    cfg_test.anim.switch_shownames_fp = 0;
    cfg_test.settings.show_names = true;
    assert(nl_anim_step(&cfg_test) == true);
    assert(cfg_test.anim.switch_shownames_fp > 0 && cfg_test.anim.switch_shownames_fp < 256);
    for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
    assert(cfg_test.anim.switch_shownames_fp == 256);
    cfg_test.settings.show_names = false;
    for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
    assert(cfg_test.anim.switch_shownames_fp == 0);

    cfg_test.settings.show_shortcuts = true;
    cfg_test.anim.switch_shortcuts_fp = 256;
    nl_handle_key(&cfg_test, NL_KEY_SETTINGS);
    cfg_test.sui.menu_cursor = NL_SET_MENU_DISPLAY;
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    assert(cfg_test.sui.screen == NL_UI_SETTINGS_DISPLAY);
    assert(cfg_test.sui.display_cursor == 0);
    nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 1: Sens de defilement */
    assert(cfg_test.sui.display_cursor == 1);
    nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 2: Afficher les noms */
    assert(cfg_test.sui.display_cursor == 2);
    nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 3: Afficher les raccourcis */
    assert(cfg_test.sui.display_cursor == 3);
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    assert(cfg_test.settings.show_shortcuts == false);
    assert(nl_anim_step(&cfg_test) == true);
    assert(cfg_test.anim.switch_shortcuts_fp > 0 && cfg_test.anim.switch_shortcuts_fp < 256);
    for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
    assert(cfg_test.anim.switch_shortcuts_fp == 0);
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    assert(cfg_test.settings.show_shortcuts == true);
    for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
    assert(cfg_test.anim.switch_shortcuts_fp == 256);
    nl_handle_key(&cfg_test, NL_KEY_ESC);
    nl_handle_key(&cfg_test, NL_KEY_ESC);
    assert(cfg_test.sui.screen == NL_UI_LIBRARY);

    /* 12c. Legacy autostart files purged cleanly while keeping Ndless Persistant detection intact */
    cfg_test.sys.ndless_persistent = false;
    assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/startup/nl_boot.tns") == false);

    /* Simulate legacy startup files left over from an older installation -> nl_cleanup_legacy_autostart purges them */
    int mk_rc = system("mkdir -p /tmp/nlauncher_test_fs/phoenix/syst/poweroff /tmp/nlauncher_test_fs/ndless/startup");
    assert(mk_rc == 0);
    {
        FILE *pfp = fopen("/tmp/nlauncher_test_fs/phoenix/syst/poweroff/currentdoc.data", "wb");
        assert(pfp != NULL);
        uint8_t wd[8] = {1, 0, 0, 0, 0, 0, 0, 0};
        fwrite(wd, 1, 8, pfp);
        fclose(pfp);
        create_mock_ti_doc_tns("/tmp/nlauncher_test_fs/phoenix/syst/poweroff/currentdoc.tns");
        create_mock_ti_doc_tns("/tmp/nlauncher_test_fs/ndless/startup/nl_boot.tns");
        create_mock_ti_doc_tns("/tmp/nlauncher_test_fs/ndless/startup/nl_boot_stub.tns");
    }
    assert(nl_check_ndless_persistent_at("/tmp/nlauncher_test_fs/phoenix/syst/poweroff") == true);
    cfg_test.sys.ndless_persistent = true;

    nl_cleanup_legacy_autostart(cfg_test.sys.docs_root);
    assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/startup/nl_boot.tns") == false);
    assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/startup/nl_boot_stub.tns") == false);
    assert(nl_file_exists(cfg_test.sys.self_path) == true);

    /* 12d. Custom Game Launch Shortcuts (§4) + Conflict Resolution + Dynamic Key Help (§5) */
    int sc_crafti = -1, sc_ndoom = -1, sc_quake = -1;
    for (int i = 0; i < cfg_test.total_games; i++) {
        if (strcmp(cfg_test.games[i].title, "Crafti") == 0) sc_crafti = i;
        else if (strcmp(cfg_test.games[i].title, "nDoom") == 0) sc_ndoom = i;
        else if (strcmp(cfg_test.games[i].title, "nQuake") == 0) sc_quake = i;
    }
    assert(sc_crafti >= 0 && sc_ndoom >= 0 && sc_quake >= 0);

    assert(nl_shortcut_is_reserved('2') == true);
    assert(nl_shortcut_is_reserved('=') == true);
    assert(nl_shortcut_is_assignable('A') == true);

    nl_handle_key(&cfg_test, NL_KEY_SETTINGS);
    cfg_test.sui.menu_cursor = NL_SET_MENU_SHORTCUTS;
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    assert(cfg_test.sui.screen == NL_UI_SETTINGS_SHORTCUTS);

    /* Assign 'D' to nDoom */
    cfg_test.sui.sc_game_cursor = sc_ndoom;
    nl_handle_key(&cfg_test, NL_KEY_EXE); /* Start key pick */
    assert(cfg_test.sui.sc_picking_key == true);
    /* Reserved key '2' rejected cleanly */
    nl_handle_char_input(&cfg_test, '2');
    assert(cfg_test.games[sc_ndoom].shortcut_key == 0);
    /* Assign 'D' */
    nl_handle_char_input(&cfg_test, 'd');
    assert(cfg_test.games[sc_ndoom].shortcut_key == 'D');
    assert(cfg_test.sui.sc_picking_key == false);

    /* Try assigning 'D' to Crafti -> triggers conflict modal, then confirm replace */
    cfg_test.sui.sc_game_cursor = sc_crafti;
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    nl_handle_char_input(&cfg_test, 'D');
    assert(cfg_test.sui.sc_conflict_prompt == true);
    assert(cfg_test.sui.sc_conflict_owner_idx == sc_ndoom);
    assert(cfg_test.sui.sc_conflict_confirm_yes == false);
    nl_handle_key(&cfg_test, NL_KEY_RIGHT); /* Move to Remplacer */
    nl_handle_key(&cfg_test, NL_KEY_EXE);   /* Confirm */
    assert(cfg_test.sui.sc_conflict_prompt == false);
    assert(cfg_test.games[sc_ndoom].shortcut_key == 0);
    assert(cfg_test.games[sc_crafti].shortcut_key == 'D');

    /* Dynamic shortcut list automatically includes custom Crafti shortcut */
    nl_shortcut_def_t sdefs[40];
    int n_defs = nl_get_shortcut_defs(&cfg_test, sdefs, 40);
    bool found_custom_def = false;
    for (int i = 0; i < n_defs; i++) {
        if (strcmp(sdefs[i].key_label, "TOUCHE D") == 0 &&
            strstr(sdefs[i].action_label, "Crafti") != NULL) {
            found_custom_def = true;
        }
    }
    assert(found_custom_def == true);

    /* Direct launch from Library using custom shortcut 'd' */
    nl_handle_key(&cfg_test, NL_KEY_SETTINGS); /* Closes settings back to library */
    assert(cfg_test.sui.screen == NL_UI_LIBRARY);
    assert(nl_handle_char_input(&cfg_test, 'd') == true);
    assert(cfg_test.launch_requested == true);
    assert(cfg_test.launch_game_idx == sc_crafti);
    cfg_test.launch_requested = false;
    cfg_test.launch_game_idx = -1;

    /* 12e. Stats, Playtime Recording, RTC Formatting & All 7 Sort Modes (§6, §7) */
    cfg_test.games[sc_ndoom].is_favorite = true;
    cfg_test.games[sc_ndoom].launch_count = 5U;
    nl_record_play_session(&cfg_test, sc_ndoom, 1762900000U, 1762900325U); /* +325s */
    /* Abnormal RTC delta > 12h must be ignored */
    nl_record_play_session(&cfg_test, sc_ndoom, 1762900000U, 1762990000U);
    assert(cfg_test.games[sc_ndoom].play_time_sec == 325U);

    char dur_buf[24], ts_buf[24];
    nl_format_duration(325U, dur_buf, sizeof(dur_buf));
    assert(strcmp(dur_buf, "5m 25s") == 0);
    nl_format_timestamp(0U, ts_buf, sizeof(ts_buf));
    assert(strcmp(ts_buf, "--") == 0);
    nl_format_timestamp(1762900325U, ts_buf, sizeof(ts_buf));
    assert(strchr(ts_buf, '/') != NULL && strchr(ts_buf, ':') != NULL);

    cfg_test.active_category = NL_CAT_ALL;
    cfg_test.settings.sort_order = NL_SORT_PLAY_TIME;
    nl_rebuild_filter(&cfg_test);
    assert(cfg_test.filtered_indices[0] == sc_ndoom);

    cfg_test.settings.sort_order = NL_SORT_MOST_LAUNCHED;
    nl_rebuild_filter(&cfg_test);
    assert(cfg_test.filtered_indices[0] == sc_ndoom);

    cfg_test.settings.sort_order = NL_SORT_ALPHA_DESC;
    nl_rebuild_filter(&cfg_test);
    assert(strcmp(cfg_test.games[cfg_test.filtered_indices[0]].title,
                  cfg_test.games[cfg_test.filtered_indices[cfg_test.filtered_count - 1]].title) > 0);

    cfg_test.settings.sort_order = NL_SORT_RECENT_ADD;
    nl_rebuild_filter(&cfg_test);
    assert(cfg_test.games[cfg_test.filtered_indices[0]].added_order >=
           cfg_test.games[cfg_test.filtered_indices[cfg_test.filtered_count - 1]].added_order);

    cfg_test.settings.sort_order = NL_SORT_OLDEST_ADD;
    nl_rebuild_filter(&cfg_test);
    assert(cfg_test.games[cfg_test.filtered_indices[0]].added_order <=
           cfg_test.games[cfg_test.filtered_indices[cfg_test.filtered_count - 1]].added_order);

    /* 12f. Direct Inline Library Search (no Settings submenu) without modifying saved sort_order (§8) */
    assert(NL_SET_MENU_COUNT == 16);
    assert(NL_SET_MENU_DISPLAY == 0);
    assert(NL_SET_MENU_ANIMATIONS == 1);
    cfg_test.sui.screen = NL_UI_LIBRARY;
    cfg_test.active_category = NL_CAT_ALL;
    cfg_test.settings.sort_order = NL_SORT_ALPHA_DESC;
    cfg_test.sui.search_active = false;
    cfg_test.sui.search_len = 0;
    cfg_test.sui.search_query[0] = '\0';
    nl_rebuild_filter(&cfg_test);

    /* Typing unassigned letters immediately opens inline search in Library */
    assert(nl_handle_char_input(&cfg_test, 'q') == true);
    assert(cfg_test.sui.screen == NL_UI_LIBRARY);
    assert(cfg_test.sui.search_active == true);
    assert(strcmp(cfg_test.sui.search_query, "q") == 0);
    assert(nl_anim_step(&cfg_test) == true);
    assert(cfg_test.anim.search_bar_fp > 0);
    nl_handle_char_input(&cfg_test, 'u');
    nl_handle_char_input(&cfg_test, 'a');
    assert(strcmp(cfg_test.sui.search_query, "qua") == 0);
    assert(cfg_test.filtered_count == 1);
    assert(cfg_test.filtered_indices[0] == sc_quake);
    assert(cfg_test.settings.sort_order == NL_SORT_ALPHA_DESC);

    /* Assigned shortcut char ('d' = Crafti) appends to search query while search bar is active */
    assert(nl_handle_char_input(&cfg_test, 'd') == true);
    assert(cfg_test.launch_requested == false);
    assert(strcmp(cfg_test.sui.search_query, "quad") == 0);
    assert(cfg_test.filtered_count == 0);
    for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
    assert(cfg_test.anim.search_bar_fp == 256);
    nl_render_frame(&cfg_test, fb); /* Renders "Aucun jeu trouve (ESC)" cleanly */

    /* BACKSPACE edits query without closing search even when empty */
    assert(nl_handle_key(&cfg_test, NL_KEY_BACKSPACE) == true);
    assert(strcmp(cfg_test.sui.search_query, "qua") == 0);
    assert(cfg_test.filtered_count == 1);
    nl_handle_key(&cfg_test, NL_KEY_BACKSPACE);
    nl_handle_key(&cfg_test, NL_KEY_BACKSPACE);
    nl_handle_key(&cfg_test, NL_KEY_BACKSPACE);
    assert(cfg_test.sui.search_active == true);
    assert(cfg_test.sui.search_len == 0);
    assert(cfg_test.filtered_count == 8);

    /* ESC closes inline search without changing active category or exiting */
    cfg_test.active_category = NL_CAT_ALL;
    assert(nl_handle_key(&cfg_test, NL_KEY_ESC) == true);
    assert(cfg_test.sui.search_active == false);
    assert(cfg_test.active_category == NL_CAT_ALL);
    assert(cfg_test.should_exit == false);
    for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
    assert(cfg_test.anim.search_bar_fp == 0);

    /* Re-type "qua" and launch directly with EXE */
    nl_handle_char_input(&cfg_test, 'q');
    nl_handle_char_input(&cfg_test, 'u');
    nl_handle_char_input(&cfg_test, 'a');
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    assert(cfg_test.launch_requested == true && cfg_test.launch_game_idx == sc_quake);
    cfg_test.launch_requested = false;
    cfg_test.launch_game_idx = -1;
    cfg_test.sui.search_active = false;
    cfg_test.sui.search_len = 0;
    cfg_test.sui.search_query[0] = '\0';
    cfg_test.anim.search_bar_fp = 0;
    nl_rebuild_filter(&cfg_test);

    /* 12g. Render smoke test across ALL active settings screens */
    for (int scr = NL_UI_SETTINGS_MAIN; scr <= NL_UI_SETTINGS_DIAG; scr++) {
        cfg_test.sui.screen = (nl_ui_screen_t)scr;
        nl_render_frame(&cfg_test, fb);
        if (scr == NL_UI_SETTINGS_MAIN) {
            assert(export_fb_to_bmp("settings_preview.bmp", fb, NL_SCREEN_W, NL_SCREEN_H) == true);
        }
    }

    /* 12h. Strict Safe Parameter Reset (§9) + show_shortcuts Persistence */
    cfg_test.display_mode = NL_DISPLAY_LIGHT;
    cfg_test.accent_color = NL_ACCENT_YELLOW;
    cfg_test.settings.layout = NL_LAYOUT_4X4;
    cfg_test.settings.show_names = true;
    cfg_test.settings.show_shortcuts = false;
    cfg_test.settings.sort_order = NL_SORT_PLAY_TIME;
    assert(nl_save_config(&cfg_test, cfg_file) == true);

    static nl_state_t sc_reloaded;
    nl_init_state(&sc_reloaded);
    assert(nl_load_config(&sc_reloaded, cfg_file) == true);
    assert(sc_reloaded.settings.show_shortcuts == false);
    assert(sc_reloaded.anim.switch_shortcuts_fp == 0);
    nl_free_icons(&sc_reloaded);

    /* Open Reset screen -> default focus MUST be safe Annuler (reset_confirm_yes == false) */
    cfg_test.sui.screen = NL_UI_SETTINGS_MAIN;
    cfg_test.sui.menu_cursor = NL_SET_MENU_RESET;
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    assert(cfg_test.sui.screen == NL_UI_SETTINGS_RESET);
    assert(cfg_test.sui.reset_confirm_yes == false);
    /* Toggle to Confirmer and run full reset */
    nl_handle_key(&cfg_test, NL_KEY_RIGHT);
    assert(cfg_test.sui.reset_confirm_yes == true);
    nl_handle_key(&cfg_test, NL_KEY_EXE);
    assert(cfg_test.sui.reset_performed == true);
    assert(nl_save_config(&cfg_test, cfg_file) == true);

    /* Settings reset to factory defaults, while games, covers, favs & stats are 100% preserved! */
    assert(cfg_test.display_mode == NL_DISPLAY_DARK);
    assert(cfg_test.accent_color == NL_ACCENT_BLUE);
    assert(cfg_test.settings.layout == NL_LAYOUT_3X2);
    assert(cfg_test.settings.show_names == false);
    assert(cfg_test.settings.show_shortcuts == true);
    assert(cfg_test.anim.switch_shortcuts_fp == 256);
    assert(cfg_test.settings.sort_order == NL_SORT_ALPHA_ASC);
    assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/startup/nl_boot.tns") == false);
    assert(cfg_test.games[sc_crafti].shortcut_key == 0);

    assert(cfg_test.total_games == 8);
    assert(cfg_test.games[sc_ndoom].is_favorite == true);
    assert(cfg_test.games[sc_ndoom].launch_count == 5U);
    assert(cfg_test.games[sc_ndoom].play_time_sec == 325U);
    assert(cfg_test.games[sc_ndoom].icon_source == NL_ICON_CAPTURED);
    assert(cfg_test.games[sc_crafti].icon_source == NL_ICON_BMP);
    assert(nl_file_exists("/tmp/nlauncher_test_fs/games/fps/nDoom.tns") == true);
    assert(nl_file_exists("/tmp/nlauncher_test_fs/games/arcade/Crafti.bmp.tns") == true);
    assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/covers/nDoom.ico.tns") == true);

    /* 12i. Settings Menu Fluidity & Animations (A - G) + Rapid Input Stability */
    {
        cfg_test.sui.screen = NL_UI_LIBRARY;
        cfg_test.anim.set_last_screen = NL_UI_LIBRARY;
        cfg_test.anim.set_open_fp = 0;

        /* A. Menu Open: progressive fade-in & vertical displacement (R14: set_open_fp: 8 -> 256, ~20 frames) */
        assert(nl_handle_key(&cfg_test, NL_KEY_SETTINGS) == true);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        assert(cfg_test.anim.set_open_fp == 8);
        nl_render_frame(&cfg_test, fb);
        int open_ticks = 0;
        while (cfg_test.anim.set_open_fp < 256 && open_ticks < 32) {
            assert(nl_anim_step(&cfg_test) == true);
            nl_render_frame(&cfg_test, fb);
            open_ticks++;
        }
        assert(cfg_test.anim.set_open_fp == 256 && open_ticks >= 16 && open_ticks <= 24);

        /* C. Option Selection & Smooth List Scroll (set_sel_y_fp, set_list_scroll_fp, set_sel_glow_fp) */
        assert(cfg_test.sui.menu_cursor == 0);
        int y0_fp = cfg_test.anim.set_sel_y_fp;
        assert(nl_handle_key(&cfg_test, NL_KEY_DOWN) == true);
        assert(cfg_test.sui.menu_cursor == 1);
        assert(cfg_test.anim.set_prev_cursor == 0);
        assert(cfg_test.anim.set_sel_glow_fp == 56);
        assert(cfg_test.anim.set_target_sel_y_fp > y0_fp);
        assert(nl_anim_step(&cfg_test) == true);
        assert(cfg_test.anim.set_sel_y_fp > y0_fp && cfg_test.anim.set_sel_y_fp < cfg_test.anim.set_target_sel_y_fp);
        assert(cfg_test.anim.set_sel_glow_fp > 56 && cfg_test.anim.set_sel_glow_fp <= 256);
        nl_render_frame(&cfg_test, fb);
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.set_sel_y_fp == cfg_test.anim.set_target_sel_y_fp);
        assert(cfg_test.anim.set_sel_glow_fp == 256);

        /* Move back up to row 0 (NL_SET_MENU_DISPLAY) */
        assert(nl_handle_key(&cfg_test, NL_KEY_UP) == true);
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);

        /* B. Section Navigation: SETTINGS_MAIN -> SETTINGS_DISPLAY (set_sec_fp: 32 -> 256, dir = +1) */
        assert(cfg_test.sui.menu_cursor == NL_SET_MENU_DISPLAY);
        assert(nl_handle_key(&cfg_test, NL_KEY_EXE) == true);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_DISPLAY);
        assert(cfg_test.anim.set_prev_screen == NL_UI_SETTINGS_MAIN);
        assert(cfg_test.anim.set_sec_dir == 1);
        assert(cfg_test.anim.set_sec_fp == 32);
        nl_render_frame(&cfg_test, fb);
        for (int i = 0; i < 12; i++) {
            nl_anim_step(&cfg_test);
            nl_render_frame(&cfg_test, fb);
        }
        assert(cfg_test.anim.set_sec_fp == 256);

        /* E. Value Modification: Layout transition (layout_from -> layout, layout_trans_fp: 0 -> 256, val_pulse_fp: 256 -> 0) */
        assert(cfg_test.sui.display_cursor == 0);
        assert(cfg_test.settings.layout == NL_LAYOUT_3X2);
        assert(nl_handle_key(&cfg_test, NL_KEY_RIGHT) == true);
        assert(cfg_test.anim.layout_from == NL_LAYOUT_3X2);
        assert(cfg_test.settings.layout == NL_LAYOUT_3X3);
        assert(cfg_test.anim.layout_trans_fp == 0);
        assert(cfg_test.anim.val_pulse_fp == 256);
        assert(nl_anim_step(&cfg_test) == true);
        assert(cfg_test.anim.layout_trans_fp > 0 && cfg_test.anim.layout_trans_fp < 256);
        assert(cfg_test.anim.val_pulse_fp > 0 && cfg_test.anim.val_pulse_fp < 256);
        nl_render_frame(&cfg_test, fb);
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.layout_trans_fp == 256);
        assert(cfg_test.anim.val_pulse_fp == 0);
        /* Restore 3x2 */
        nl_handle_key(&cfg_test, NL_KEY_LEFT);
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
        assert(cfg_test.settings.layout == NL_LAYOUT_3X2);

        /* B (Return) + F. Modal & Confirmation Button Animation (SETTINGS_RESET: modal_fp, modal_btn_fp) */
        assert(nl_handle_key(&cfg_test, NL_KEY_ESC) == true);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        assert(cfg_test.anim.set_sec_dir == -1);
        assert(cfg_test.anim.set_sec_fp == 32);
        /* Rapid input without waiting: immediately navigate down to RESET and open it */
        for (int i = 0; i < (NL_SET_MENU_RESET - NL_SET_MENU_DISPLAY); i++) {
            nl_handle_key(&cfg_test, NL_KEY_DOWN);
        }
        assert(cfg_test.sui.menu_cursor == NL_SET_MENU_RESET);
        assert(nl_handle_key(&cfg_test, NL_KEY_EXE) == true);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_RESET);
        assert(cfg_test.anim.modal_fp == 32);
        assert(cfg_test.anim.modal_btn_fp == 0);
        nl_handle_key(&cfg_test, NL_KEY_RIGHT);
        assert(cfg_test.sui.reset_confirm_yes == true);
        assert(nl_anim_step(&cfg_test) == true);
        assert(cfg_test.anim.modal_fp > 32 && cfg_test.anim.modal_btn_fp > 0);
        nl_render_frame(&cfg_test, fb);
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.modal_fp == 256 && cfg_test.anim.modal_btn_fp == 256);
        nl_handle_key(&cfg_test, NL_KEY_ESC);
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);

        /* G. Menu Close: progressive fade-out & downward displacement (R14: set_open_fp: 248 -> 0, ~16 frames) over library */
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        assert(cfg_test.anim.set_open_fp == 256);
        assert(nl_handle_key(&cfg_test, NL_KEY_ESC) == true);
        assert(cfg_test.sui.screen == NL_UI_LIBRARY);
        assert(cfg_test.anim.set_open_fp == 248);
        nl_render_frame(&cfg_test, fb);
        int close_ticks = 0;
        while (cfg_test.anim.set_open_fp > 0 && close_ticks < 32) {
            assert(nl_anim_step(&cfg_test) == true);
            nl_render_frame(&cfg_test, fb);
            close_ticks++;
        }
        assert(cfg_test.anim.set_open_fp == 0 && close_ticks >= 13 && close_ticks <= 20);
    }

    /* 12j. Touchpad Navigation (Mouse-Like Slide) + Key '5' Launch + Persistence & Reset */
    {
        /* Default state: touchpad_nav is OFF (false) */
        assert(cfg_test.settings.touchpad_nav == false);
        assert(cfg_test.anim.switch_touchpad_fp == 0);

        /* Key '5' is reserved for game launch (never assignable as a custom game shortcut) */
        assert(nl_shortcut_is_reserved('5') == true);
        assert(nl_shortcut_is_assignable('5') == false);

        /* Pressing '5' on the Library screen launches the currently selected game */
        cfg_test.sui.screen = NL_UI_LIBRARY;
        cfg_test.selected_pos = 2;
        cfg_test.launch_requested = false;
        cfg_test.launch_game_idx = -1;
        assert(nl_handle_char_input(&cfg_test, '5') == true);
        assert(cfg_test.launch_requested == true);
        assert(cfg_test.launch_game_idx == cfg_test.filtered_indices[2]);
        cfg_test.launch_requested = false;
        cfg_test.launch_game_idx = -1;
        cfg_test.anim.launch_anim_active = false;
        cfg_test.anim.launch_tick = 0;

        /* Open Settings -> Navigation tactile -> Down to Row 1 (Glissement au pave) -> Toggle ON */
        assert(nl_handle_key(&cfg_test, NL_KEY_SETTINGS) == true);
        cfg_test.sui.menu_cursor = NL_SET_MENU_TOUCHPAD;
        assert(nl_handle_key(&cfg_test, NL_KEY_EXE) == true);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_TOUCHPAD);
        assert(cfg_test.sui.tp_cursor == 0); /* Row 0: Mode de navigation */
        assert(nl_handle_key(&cfg_test, NL_KEY_DOWN) == true);
        assert(cfg_test.sui.tp_cursor == 1); /* Row 1: Glissement au pave (Mode 1) */
        assert(nl_handle_key(&cfg_test, NL_KEY_EXE) == true);
        assert(cfg_test.settings.touchpad_nav == true);
        assert(cfg_test.anim.val_pulse_fp == 256);
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.switch_touchpad_fp == 256);
        nl_render_frame(&cfg_test, fb);

        /* Save & reload config: touchpad_nav=1 persists across restarts */
        assert(nl_save_config(&cfg_test, "/tmp/nl_test_v3.cfg.tns") == true);
        static nl_state_t tp_reloaded;
        nl_init_state(&tp_reloaded);
        assert(tp_reloaded.settings.touchpad_nav == false);
        assert(nl_load_config(&tp_reloaded, "/tmp/nl_test_v3.cfg.tns") == true);
        assert(tp_reloaded.settings.touchpad_nav == true);
        assert(tp_reloaded.anim.switch_touchpad_fp == 256);
        nl_free_icons(&tp_reloaded);

        /* Return to Library and test nl_touchpad_process_sample() */
        nl_handle_key(&cfg_test, NL_KEY_ESC);
        nl_handle_key(&cfg_test, NL_KEY_ESC);
        assert(cfg_test.sui.screen == NL_UI_LIBRARY);

        nl_touchpad_tracker_t tpr;
        nl_touchpad_tracker_reset(&tpr);
        const uint16_t pad_w = 2328U;
        const uint16_t pad_h = 1691U;

        /* When disabled (OFF), sliding finger produces NL_KEY_NONE */
        assert(nl_touchpad_process_sample(&tpr, false, true, 500, 800, pad_w, pad_h) == NL_KEY_NONE);
        assert(nl_touchpad_process_sample(&tpr, false, true, 1600, 800, pad_w, pad_h) == NL_KEY_NONE);

        /* Initial touch down anchors coordinates without moving selection */
        assert(nl_touchpad_process_sample(&tpr, true, true, 1000, 850, pad_w, pad_h) == NL_KEY_NONE);

        /* Micro-jitter (< deadzone) is filtered out */
        for (int j = 0; j < 10; j++) {
            uint16_t jx = (uint16_t)(1000 + ((j & 1) ? 4 : -4));
            uint16_t jy = (uint16_t)(850 + ((j & 1) ? -3 : 3));
            assert(nl_touchpad_process_sample(&tpr, true, true, jx, jy, pad_w, pad_h) == NL_KEY_NONE);
        }

        /* Smooth horizontal slide to the right (+520 raw units ~ +228 norm >= 215) -> 1 step NL_KEY_RIGHT */
        assert(nl_touchpad_process_sample(&tpr, true, true, 1180, 850, pad_w, pad_h) == NL_KEY_NONE);
        assert(nl_touchpad_process_sample(&tpr, true, true, 1360, 850, pad_w, pad_h) == NL_KEY_NONE);
        assert(nl_touchpad_process_sample(&tpr, true, true, 1540, 850, pad_w, pad_h) == NL_KEY_RIGHT);
        /* Immediate cooldown prevents double jump on adjacent frames */
        assert(nl_touchpad_process_sample(&tpr, true, true, 1750, 850, pad_w, pad_h) == NL_KEY_NONE);

        /* Instant direction reversal to the left without lag */
        for (int cd = 0; cd < 4; cd++) {
            (void)nl_touchpad_process_sample(&tpr, true, true, 1750, 850, pad_w, pad_h);
        }
        assert(nl_touchpad_process_sample(&tpr, true, true, 1200, 850, pad_w, pad_h) == NL_KEY_LEFT);

        /* Vertical slide: on CX II-T hardware, y=0 is bottom and y=1691 is top.
         * Decreasing y (finger moving down) -> NL_KEY_DOWN; increasing y (finger moving up) -> NL_KEY_UP */
        nl_touchpad_tracker_reset(&tpr);
        assert(nl_touchpad_process_sample(&tpr, true, true, 1160, 1000, pad_w, pad_h) == NL_KEY_NONE);
        assert(nl_touchpad_process_sample(&tpr, true, true, 1160, 800, pad_w, pad_h) == NL_KEY_NONE);
        assert(nl_touchpad_process_sample(&tpr, true, true, 1160, 600, pad_w, pad_h) == NL_KEY_DOWN);
        for (int cd = 0; cd < 4; cd++) {
            (void)nl_touchpad_process_sample(&tpr, true, true, 1160, 600, pad_w, pad_h);
        }
        assert(nl_touchpad_process_sample(&tpr, true, true, 1160, 1000, pad_w, pad_h) == NL_KEY_UP);

        /* 5-Detent Touchpad Sensitivity Slider (1..5): cursor switch, clamping, persistence, detent settle & thresholds */
        assert(cfg_test.settings.touchpad_sensitivity == NL_TP_SENS_DEFAULT); /* default cran 3/5 (idx 2) */
        assert(nl_touchpad_threshold_for_level(0) == 300);
        assert(nl_touchpad_threshold_for_level(1) == 255);
        assert(nl_touchpad_threshold_for_level(2) == 215);
        assert(nl_touchpad_threshold_for_level(3) == 175);
        assert(nl_touchpad_threshold_for_level(4) == 138);

        /* Re-enter SETTINGS_TOUCHPAD, switch DOWN to row 2 (5-detent sensitivity slider) */
        nl_handle_key(&cfg_test, NL_KEY_SETTINGS);
        cfg_test.sui.menu_cursor = NL_SET_MENU_TOUCHPAD;
        nl_handle_key(&cfg_test, NL_KEY_EXE);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_TOUCHPAD);
        assert(cfg_test.sui.tp_cursor == 0);
        nl_handle_key(&cfg_test, NL_KEY_DOWN);
        assert(cfg_test.sui.tp_cursor == 1);
        nl_handle_key(&cfg_test, NL_KEY_DOWN);
        assert(cfg_test.sui.tp_cursor == 2);

        /* Move across all 5 detents & verify clamp + exact detent alignment */
        nl_handle_key(&cfg_test, NL_KEY_RIGHT);
        assert(cfg_test.settings.touchpad_sensitivity == 3);
        for (int s = 0; s < 16; s++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.tp_slider_pos_fp == 3 * 256);

        nl_handle_key(&cfg_test, NL_KEY_RIGHT);
        assert(cfg_test.settings.touchpad_sensitivity == 4);
        nl_handle_key(&cfg_test, NL_KEY_RIGHT); /* clamped at 4 */
        assert(cfg_test.settings.touchpad_sensitivity == 4);
        for (int s = 0; s < 16; s++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.tp_slider_pos_fp == 4 * 256);

        for (int k = 0; k < 6; k++) nl_handle_key(&cfg_test, NL_KEY_LEFT); /* clamped at 0 */
        assert(cfg_test.settings.touchpad_sensitivity == 0);
        for (int s = 0; s < 24; s++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.tp_slider_pos_fp == 0);
        nl_render_frame(&cfg_test, fb);

        /* Verify highest sensitivity (4 -> thresh 138) emits RIGHT on a shorter slide that does NOT trigger level 0 (thresh 300) */
        nl_touchpad_tracker_reset(&tpr);
        assert(nl_touchpad_process_sample_ex(&tpr, true, true, 1000, 850, pad_w, pad_h, 0) == NL_KEY_NONE);
        assert(nl_touchpad_process_sample_ex(&tpr, true, true, 1360, 850, pad_w, pad_h, 0) == NL_KEY_NONE); /* ~158 norm < 300 */
        nl_touchpad_tracker_reset(&tpr);
        assert(nl_touchpad_process_sample_ex(&tpr, true, true, 1000, 850, pad_w, pad_h, 4) == NL_KEY_NONE);
        assert(nl_touchpad_process_sample_ex(&tpr, true, true, 1360, 850, pad_w, pad_h, 4) == NL_KEY_RIGHT); /* ~158 norm >= 138 */

        /* Verify config save/load preserves touchpad_sensitivity */
        nl_handle_key(&cfg_test, NL_KEY_RIGHT); /* level 1 */
        assert(cfg_test.settings.touchpad_sensitivity == 1);
        assert(nl_save_config(&cfg_test, "/tmp/nl_test_v3.cfg.tns") == true);
        static nl_state_t tp_sens_reloaded;
        nl_init_state(&tp_sens_reloaded);
        assert(nl_load_config(&tp_sens_reloaded, "/tmp/nl_test_v3.cfg.tns") == true);
        assert(tp_sens_reloaded.settings.touchpad_sensitivity == 1);
        assert(tp_sens_reloaded.anim.tp_slider_pos_fp == 256);

        nl_handle_key(&cfg_test, NL_KEY_ESC);
        nl_handle_key(&cfg_test, NL_KEY_ESC);

        /* Factory settings reset returns touchpad_mode to Mode 1 (TOUCHPAD), touchpad_nav to OFF,
         * sensitivity to default 2 (3/5), and cursor_speed to default 2 (3/5) */
        nl_reset_settings_only(&cfg_test, "/tmp/nl_test_v3.cfg.tns");
        assert(cfg_test.settings.touchpad_mode == NL_TP_MODE_TOUCHPAD);
        assert(cfg_test.settings.touchpad_nav == false);
        assert(cfg_test.settings.touchpad_sensitivity == NL_TP_SENS_DEFAULT);
        assert(cfg_test.settings.cursor_speed == NL_CURSOR_SPEED_DEFAULT);
        assert(cfg_test.anim.switch_touchpad_fp == 0);
        assert(cfg_test.anim.tp_slider_pos_fp == NL_TP_SENS_DEFAULT * 256);
    }

    /* 12k. Settings -> Affichage (5 options: Layout, Noms, Raccourcis, Sens de defilement, Eteindre) & DEL Poweroff Shortcut */
    {
        assert(NL_DISPLAY_OPT_COUNT == 5);
        cfg_test.sys.has_ndless = false;
        nl_handle_key(&cfg_test, NL_KEY_SETTINGS);
        cfg_test.sui.menu_cursor = NL_SET_MENU_DISPLAY;
        nl_handle_key(&cfg_test, NL_KEY_EXE);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_DISPLAY);
        assert(cfg_test.sui.display_cursor == 0);
        nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 1: Afficher les noms */
        nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 2: Afficher les raccourcis */
        nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 3: Sens de defilement */
        nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 4: Eteindre la calculatrice */
        assert(cfg_test.sui.display_cursor == 4);
        assert(cfg_test.sui.display_scroll == 2);
        nl_render_frame(&cfg_test, fb);
        assert(cfg_test.sui.poweroff_req == false);
        nl_handle_key(&cfg_test, NL_KEY_EXE);
        assert(cfg_test.sui.poweroff_req == true);
        cfg_test.sui.poweroff_req = false;

        /* Pressing DEL (NL_KEY_RESCAN) in Settings Main triggers poweroff_req */
        nl_handle_key(&cfg_test, NL_KEY_ESC);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        nl_handle_key(&cfg_test, NL_KEY_RESCAN);
        assert(cfg_test.sui.poweroff_req == true);
        cfg_test.sui.poweroff_req = false;
        assert(strstr(cfg_test.anim.toast_text, "EXTINCTION CALCULATRICE") != NULL);

        /* 7. Settings Menu Visual & Ergonomic Overhaul: Icon Badges + Two-Line Hierarchy + Sort Labels */
        assert(strcmp(nl_sort_order_label(NL_SORT_ALPHA_ASC), "Ordre alphabetique (A -> Z)") == 0);
        assert(strcmp(nl_sort_order_label(NL_SORT_ALPHA_DESC), "Ordre alphabetique (Z -> A)") == 0);
        assert(strcmp(nl_sort_order_label(NL_SORT_RECENT_ADD), "Ajoutes recemment") == 0);
        assert(strcmp(nl_sort_order_label(NL_SORT_OLDEST_ADD), "Ajoutes le plus tot") == 0);
        assert(strcmp(nl_sort_order_label(NL_SORT_LAST_USED), "Joues recemment") == 0);
        assert(strcmp(nl_sort_order_label(NL_SORT_MOST_LAUNCHED), "Les plus lances") == 0);
        assert(strcmp(nl_sort_order_label(NL_SORT_PLAY_TIME), "Temps de jeu cumule") == 0);

        cfg_test.sui.screen = NL_UI_LIBRARY;
        nl_handle_key(&cfg_test, NL_KEY_SETTINGS);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        cfg_test.sui.menu_cursor = 0;
        cfg_test.sui.menu_scroll = 0;
        cfg_test.anim.set_open_fp = 256;
        cfg_test.anim.set_sec_fp = 256;
        cfg_test.anim.set_list_scroll_fp = 0;
        cfg_test.anim.set_target_scroll_fp = 0;
        cfg_test.anim.set_sel_y_fp = 32 << 4;
        cfg_test.anim.set_target_sel_y_fp = 32 << 4;
        cfg_test.anim.set_sel_glow_fp = 256;
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
        for (int dm = 0; dm < NL_DISPLAY_COUNT; dm++) {
            for (int ac = 0; ac < NL_ACCENT_COUNT; ac++) {
                cfg_test.display_mode = (nl_display_mode_t)dm;
                cfg_test.accent_color = (nl_accent_color_t)ac;
                const nl_theme_t *sth = nl_get_theme(cfg_test.display_mode, cfg_test.accent_color);
                nl_render_frame(&cfg_test, fb);
                for (int vis_row = 0; vis_row < 6; vis_row++) {
                    int ry = 32 + vis_row * 30;
                    uint16_t row_bg = (vis_row == cfg_test.sui.menu_cursor) ? sth->card_bg_sel : sth->card_bg;
                    /* Verify 18x18 icon badge at x = 17..34, y = ry + 5..ry + 22 has both badge and glyph pixels */
                    int badge_diff = 0;
                    for (int py = ry + 5; py < ry + 23; py++) {
                        for (int px = 17; px < 35; px++) {
                            if (fb[py * NL_SCREEN_W + px] != row_bg) badge_diff++;
                        }
                    }
                    assert(badge_diff >= 180);
                    /* Verify title line (ry + 5..11) and subtitle line (ry + 16..22) both draw text pixels */
                    int title_px = 0, sub_px = 0;
                    for (int px = 41; px < 180; px++) {
                        for (int py = ry + 5; py < ry + 12; py++) {
                            if (fb[py * NL_SCREEN_W + px] != row_bg) title_px++;
                        }
                        for (int py = ry + 16; py < ry + 23; py++) {
                            if (fb[py * NL_SCREEN_W + px] != row_bg) sub_px++;
                        }
                    }
                    assert(title_px >= 25);
                    assert(sub_px >= 25);
                }
            }
        }
    }

    nl_free_icons(&cfg_test);
    printf("[PASS] Settings menu, 5 layouts, show_names, shortcuts, 7 sorts, stats, search, safe reset, fluid animations (A-G), touchpad navigation & visual icon hierarchy verified.\n");

    /* 12l. Settings Shortcut (Direct Scratchpad/Save key + Ctrl+S simultaneous + Sticky Ctrl -> S + Anti-Bounce) */
    {
        nl_save_shortcut_t sc;
        nl_save_shortcut_reset(&sc);

        /* 0. Direct physical Scratchpad/Save button (without Ctrl) triggers once, blocks hold/bounce, even if other_key_down */
        assert(nl_save_shortcut_poll(&sc, true, false, false, true) == true);
        assert(nl_save_shortcut_poll(&sc, true, false, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, true, false, false, false) == false);
        for (int f = 0; f < NL_SETTINGS_RELEASE_TICKS + NL_SETTINGS_COOLDOWN_TICKS; f++) {
            assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        }

        /* 1. Simultaneous Ctrl+S held over multiple frames + 1-frame contact bounce triggers ONLY ONCE */
        assert(nl_save_shortcut_poll(&sc, false, true, true, false) == true);
        assert(nl_save_shortcut_poll(&sc, false, true, true, false) == false);
        /* 1-frame S bounce while Ctrl still held -> blocked */
        assert(nl_save_shortcut_poll(&sc, false, true, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, true, true, false) == false);
        /* 2-frame total release bounce (< NL_SETTINGS_RELEASE_TICKS) -> still blocked */
        assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, true, true, false) == false);
        /* Full release for 3 frames, but still inside 18-frame cooldown -> blocked until cooldown expires */
        for (int f = 0; f < 3; f++) {
            assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        }
        assert(sc.wait_full_release == false);
        /* Drain remaining cooldown ticks */
        for (int f = 0; f < NL_SETTINGS_COOLDOWN_TICKS; f++) {
            assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        }

        /* 2. Sticky sequential Ctrl -> release -> wait 20 frames -> S triggers NL_KEY_SETTINGS cleanly */
        assert(nl_save_shortcut_poll(&sc, false, true, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, true, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        assert(sc.ctrl_latch_ticks == NL_CTRL_LATCH_TICKS);
        for (int f = 0; f < 20; f++) {
            assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        }
        assert(sc.ctrl_latch_ticks == NL_CTRL_LATCH_TICKS - 20);
        assert(nl_save_shortcut_poll(&sc, false, false, true, false) == true);
        assert(sc.ctrl_latch_ticks == 0);
        assert(sc.wait_full_release == true);

        /* Wait full release + cooldown */
        for (int f = 0; f < NL_SETTINGS_RELEASE_TICKS + NL_SETTINGS_COOLDOWN_TICKS; f++) {
            assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        }

        /* 3. Pressing another key cancels sticky Ctrl latch cleanly */
        assert(nl_save_shortcut_poll(&sc, false, true, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        assert(sc.ctrl_latch_ticks == NL_CTRL_LATCH_TICKS);
        assert(nl_save_shortcut_poll(&sc, false, false, false, true) == false);
        assert(sc.ctrl_latch_ticks == 0);
        assert(nl_save_shortcut_poll(&sc, false, false, true, false) == false);

        /* 4. Sticky Ctrl expires naturally after NL_CTRL_LATCH_TICKS idle frames */
        assert(nl_save_shortcut_poll(&sc, false, true, false, false) == false);
        assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        for (int f = 0; f < NL_CTRL_LATCH_TICKS; f++) {
            assert(nl_save_shortcut_poll(&sc, false, false, false, false) == false);
        }
        assert(sc.ctrl_latch_ticks == 0);
        assert(nl_save_shortcut_poll(&sc, false, false, true, false) == false);

        /* 5. Full flow check: Scratchpad/Save -> NL_KEY_SETTINGS -> organic opening anim -> ESC returns to Library */
        static nl_state_t save_st;
        nl_init_state(&save_st);
        assert(save_st.sui.screen == NL_UI_LIBRARY);
        nl_save_shortcut_reset(&sc);
        bool triggered = nl_save_shortcut_poll(&sc, true, false, false, false);
        assert(triggered == true);
        assert(nl_handle_key(&save_st, NL_KEY_SETTINGS) == true);
        assert(save_st.sui.screen == NL_UI_SETTINGS_MAIN);
        assert(save_st.anim.set_open_fp == 8);
        for (int i = 0; i < 24; i++) nl_anim_step(&save_st);
        assert(save_st.anim.set_open_fp == 256);
        assert(nl_handle_key(&save_st, NL_KEY_ESC) == true);
        assert(save_st.sui.screen == NL_UI_LIBRARY);
        for (int i = 0; i < 20; i++) nl_anim_step(&save_st);
        assert(save_st.anim.set_open_fp == 0);
        nl_free_icons(&save_st);
        printf("[PASS] Settings shortcut (Scratchpad/Save key + Ctrl+S), anti-bounce guard & Settings open/ESC cycle verified.\n");
    }

    /* 12m. Gray Mode Persistence, Touchpad Physical Click Guard, Upward Pagination Zero-Overlap,
     *      Liquid-Drop Settings + Rotating Gear, Sliding TAB Pill & Key-Picker Highlight */
    {
        /* 1. Gray Theme (NL_DISPLAY_GRAY) persistence + 12-combo visual distinction */
        static nl_state_t gray_st;
        nl_init_state(&gray_st);
        nl_handle_key(&gray_st, NL_KEY_DOC);
        assert(gray_st.display_mode == NL_DISPLAY_LIGHT);
        nl_handle_key(&gray_st, NL_KEY_DOC);
        assert(gray_st.display_mode == NL_DISPLAY_GRAY);
        nl_handle_key(&gray_st, NL_KEY_MENU);
        assert(gray_st.accent_color == NL_ACCENT_RED);

        const char *gray_cfg = "/tmp/nl_gray_test.cfg.tns";
        assert(nl_save_config(&gray_st, gray_cfg) == true);
        static nl_state_t gray_reloaded;
        nl_init_state(&gray_reloaded);
        assert(nl_load_config(&gray_reloaded, gray_cfg) == true);
        assert(gray_reloaded.display_mode == NL_DISPLAY_GRAY);
        assert(gray_reloaded.accent_color == NL_ACCENT_RED);

        const nl_theme_t *th_d = nl_get_theme(NL_DISPLAY_DARK, NL_ACCENT_BLUE);
        const nl_theme_t *th_l = nl_get_theme(NL_DISPLAY_LIGHT, NL_ACCENT_BLUE);
        const nl_theme_t *th_g = nl_get_theme(NL_DISPLAY_GRAY, NL_ACCENT_BLUE);
        assert(th_g->bg_main != th_d->bg_main && th_g->bg_main != th_l->bg_main);
        assert(th_g->card_bg != th_d->card_bg && th_g->card_bg != th_l->card_bg);

        /* 2. Touchpad physical center click guard (nl_tp_click_poll) */
        nl_tp_click_guard_t cg;
        nl_tp_click_reset(&cg);
        const uint16_t pw = 2328U, ph = 1691U;

        /* Clean click when touching pad -> fires ONCE, blocks hold, re-arms on release */
        assert(nl_tp_click_poll(&cg, true, false, false, 1160, 845, pw, ph) == false);
        assert(nl_tp_click_poll(&cg, true, true, true, 1160, 845, pw, ph) == true);
        assert(nl_tp_click_poll(&cg, true, true, true, 1160, 845, pw, ph) == false);
        assert(nl_tp_click_poll(&cg, true, false, false, 1160, 845, pw, ph) == false);
        for (int r = 0; r < NL_TP_CLICK_RELEASE_TICKS + NL_TP_CLICK_COOLDOWN_TICKS; r++) {
            assert(nl_tp_click_poll(&cg, false, false, false, 0, 0, pw, ph) == false);
        }
        assert(nl_tp_click_poll(&cg, true, true, true, 1160, 845, pw, ph) == true);
        for (int r = 0; r < NL_TP_CLICK_RELEASE_TICKS + NL_TP_CLICK_COOLDOWN_TICKS; r++) {
            assert(nl_tp_click_poll(&cg, false, false, false, 0, 0, pw, ph) == false);
        }

        /* Finger slide > NL_TP_CLICK_SLIDE_CANCEL before pressing cancels center click */
        assert(nl_tp_click_poll(&cg, true, false, false, 600, 845, pw, ph) == false);
        assert(nl_tp_click_poll(&cg, true, false, false, 1300, 845, pw, ph) == false);
        assert(cg.moved_since_down == true);
        assert(nl_tp_click_poll(&cg, true, true, true, 1300, 845, pw, ph) == false);
        for (int r = 0; r < NL_TP_CLICK_RELEASE_TICKS + NL_TP_CLICK_COOLDOWN_TICKS; r++) {
            assert(nl_tp_click_poll(&cg, false, false, false, 0, 0, pw, ph) == false);
        }
        assert(nl_tp_click_poll(&cg, true, true, true, 1160, 845, pw, ph) == true);

        /* 3. Upward pagination zero-overlap guard (row 0 stays strictly above row 1 mid-scroll) */
        static nl_state_t up_st;
        nl_init_state(&up_st);
        nl_scan_directory(&up_st, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
        assert(up_st.total_games == 8);
        up_st.settings.layout = NL_LAYOUT_3X2;
        up_st.settings.show_names = false;
        up_st.settings.show_shortcuts = true;
        for (int i = 0; i < 8; i++) {
            up_st.games[i].icon_source = NL_ICON_CAPTURED;
            up_st.games[i].icon_w = NL_COVER_W;
            up_st.games[i].icon_h = NL_COVER_H;
            if (!up_st.games[i].icon_pixels) {
                up_st.games[i].icon_pixels = (uint16_t *)malloc(NL_COVER_BYTES);
            }
            assert(up_st.games[i].icon_pixels != NULL);
            uint16_t row_col = (i < 3) ? RGB565(220, 40, 40) : RGB565(40, 200, 80);
            for (int p = 0; p < NL_COVER_W * NL_COVER_H; p++) {
                up_st.games[i].icon_pixels[p] = row_col;
            }
        }
        /* Go to page 1 and settle */
        nl_handle_key(&up_st, NL_KEY_PAGE_NEXT);
        for (int i = 0; i < 30; i++) nl_anim_step(&up_st);
        assert(up_st.current_page == 1 && up_st.anim.scroll_y_fp == up_st.anim.target_scroll_y_fp);

        /* Page UP to page 0 and inspect EVERY intermediate animation frame */
        nl_handle_key(&up_st, NL_KEY_PAGE_PREV);
        assert(up_st.current_page == 0 && up_st.anim.target_scroll_y_fp == 0);
        for (int step = 0; step < 12; step++) {
            nl_anim_step(&up_st);
            nl_render_frame(&up_st, fb);
            nl_layout_metrics_t lm_up;
            nl_get_layout_metrics_full(up_st.settings.layout, up_st.settings.scroll_dir,
                                       up_st.settings.show_names, up_st.settings.show_shortcuts,
                                       up_st.filtered_count, &lm_up);
            int x_probe = lm_up.margin_x + (lm_up.card_w / 2);
            int red_max_y = -1;
            int green_min_y = 999;
            for (int y = NL_VIEW_TOP; y < NL_VIEW_BOTTOM; y++) {
                uint16_t px = fb[y * NL_SCREEN_W + x_probe];
                if (px == RGB565(220, 40, 40)) red_max_y = y;
                if (px == RGB565(40, 200, 80) && y < green_min_y) green_min_y = y;
            }
            if (red_max_y >= 0 && green_min_y < 999) {
                assert(red_max_y < green_min_y - 4);
            }
        }

        /* 4. Liquid-drop Settings open/close + TAB sliding pill + Shortcuts key-picker glide */
        for (int dm = 0; dm < NL_DISPLAY_COUNT; dm++) {
            up_st.display_mode = (nl_display_mode_t)dm;
            up_st.sui.screen = NL_UI_LIBRARY;
            nl_handle_key(&up_st, NL_KEY_SETTINGS);
            assert(up_st.sui.screen == NL_UI_SETTINGS_MAIN);
            assert(up_st.anim.set_open_fp > 0 && up_st.anim.set_open_fp < 256);
            nl_render_frame(&up_st, fb);
            nl_anim_step(&up_st);
            nl_render_frame(&up_st, fb);
            for (int i = 0; i < 24; i++) nl_anim_step(&up_st);
            assert(up_st.anim.set_open_fp == 256);

            nl_handle_key(&up_st, NL_KEY_ESC);
            assert(up_st.sui.screen == NL_UI_LIBRARY);
            assert(up_st.anim.set_open_fp > 0 && up_st.anim.set_open_fp < 256);
            nl_render_frame(&up_st, fb);
            for (int i = 0; i < 20; i++) nl_anim_step(&up_st);
            assert(up_st.anim.set_open_fp == 0);
        }

        /* TAB sliding category indicator + horizontal grid slide */
        nl_handle_key(&up_st, NL_KEY_TAB);
        assert(up_st.active_category == NL_CAT_FAVORITES);
        assert(up_st.anim.cat_trans_fp < 256);
        assert(up_st.anim.cat_ind_x_fp != (76 << 4));
        nl_render_frame(&up_st, fb);
        for (int i = 0; i < 16; i++) nl_anim_step(&up_st);
        assert(up_st.anim.cat_ind_x_fp == (76 << 4));
        assert(up_st.anim.cat_trans_fp == 256);

        /* Key picker highlight smooth interpolation in SETTINGS_SHORTCUTS */
        nl_handle_key(&up_st, NL_KEY_SETTINGS);
        for (int i = 0; i < 16; i++) nl_anim_step(&up_st);
        up_st.sui.menu_cursor = NL_SET_MENU_SHORTCUTS;
        nl_handle_key(&up_st, NL_KEY_EXE);
        nl_handle_key(&up_st, NL_KEY_EXE);
        assert(up_st.sui.sc_picking_key == true);
        int start_kx = up_st.anim.sc_key_x_fp;
        nl_handle_key(&up_st, NL_KEY_RIGHT);
        nl_anim_step(&up_st);
        nl_render_frame(&up_st, fb);
        assert(up_st.anim.sc_key_x_fp > start_kx);

        nl_free_icons(&gray_st);
        nl_free_icons(&gray_reloaded);
        nl_free_icons(&up_st);
        printf("[PASS] Gray mode, touchpad click guard, upward pagination zero-overlap, liquid-drop Settings, TAB slide & key-picker glide verified.\n");
    }

    /* 12n. Launch Animation (Press-Shrink + Layer 2 Luminous Rings Behind Layer 3 + Real .scr.tns Crossfade),
     *      Batch Screenshot Generation Queue (Passer/Annuler/Semi-Auto) & Gestion des Icones (3-File Separation) */
    {
        static nl_state_t launch_st;
        nl_init_state(&launch_st);
        snprintf(launch_st.covers_dir, sizeof(launch_st.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
        snprintf(launch_st.sys.docs_root, sizeof(launch_st.sys.docs_root), "/tmp/nlauncher_test_fs");
        snprintf(launch_st.sys.self_path, sizeof(launch_st.sys.self_path), "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns");

        /* Verify .scr.tns files are ignored by game scanner */
        create_mock_zehn_tns("/tmp/nlauncher_test_fs/games/arcade/Crafti.scr.tns", "FakeScr", "Test", 1, true, true, 1024);
        nl_scan_directory(&launch_st, "/tmp/nlauncher_test_fs", launch_st.sys.self_path, 4);
        assert(launch_st.total_games == 8);
        unlink("/tmp/nlauncher_test_fs/games/arcade/Crafti.scr.tns");

        nl_load_all_icons(&launch_st);

        int c_idx = -1, d_idx = -1;
        for (int i = 0; i < launch_st.total_games; i++) {
            if (strcmp(launch_st.games[i].title, "Crafti") == 0) c_idx = i;
            if (strcmp(launch_st.games[i].title, "nDoom") == 0) d_idx = i;
        }
        assert(c_idx >= 0 && d_idx >= 0);
        assert(launch_st.games[c_idx].icon_source == NL_ICON_BMP);
        assert(launch_st.games[c_idx].has_screenshot == false);

        /* 1. Capture a real gameplay screenshot (.scr.tns) for Crafti even though Crafti already has a custom .bmp.tns icon! */
        static uint16_t scr_vram[NL_FB_PIXELS];
        for (int y = 0; y < NL_SCREEN_H; y++) {
            for (int x = 0; x < NL_SCREEN_W; x++) {
                scr_vram[y * NL_SCREEN_W + x] = ((x + y) & 8) ? RGB565(240, 60, 40) : RGB565(30, 60, 220);
            }
        }
        assert(nl_capture_vram_to_screenshot(&launch_st.games[c_idx], scr_vram, false, NULL) == true);
        assert(launch_st.games[c_idx].has_screenshot == true);
        assert(launch_st.games[c_idx].scr_pixels != NULL);
        /* Custom icon (.bmp.tns) MUST remain completely untouched */
        assert(launch_st.games[c_idx].icon_source == NL_ICON_BMP);
        assert(nl_save_captured_screenshot(&launch_st.games[c_idx], launch_st.covers_dir) == true);
        assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/covers/Crafti.scr.tns") == true);
        /* Non-overwrite rule on existing .scr.tns */
        assert(nl_capture_vram_to_screenshot(&launch_st.games[c_idx], scr_vram, false, NULL) == false);

        /* 2. Press-shrink animation when holding LANCER (launch_holding = true -> launch_shrink_fp: 280 -> 232) */
        launch_st.selected_pos = 0;
        int sel_gi = launch_st.filtered_indices[0];
        assert(launch_st.anim.launch_shrink_fp == NL_SCALE_SELECTED);
        launch_st.anim.launch_holding = true;
        for (int t = 0; t < 10; t++) {
            nl_anim_step(&launch_st);
        }
        assert(launch_st.anim.launch_shrink_fp == 232);
        nl_render_frame(&launch_st, fb);
        launch_st.anim.launch_holding = false;
        for (int t = 0; t < 10; t++) {
            nl_anim_step(&launch_st);
        }
        assert(launch_st.anim.launch_shrink_fp == NL_SCALE_SELECTED);
        (void)sel_gi;

        /* 3. Verify 4-Stage Launch Transition:
         *    - Stage 1 (step 1/46): Custom icon shrinks, non-selected icons start progressive fade-out
         *    - Stage 2 & 3 (step 16/46): 3 concentric ripples propagate around (card_cx, card_cy)
         *      strictly BEHIND the custom icon (custom icon interior remains 100% uncorrupted Pure Green),
         *      while non-selected icons fade smoothly toward transparency
         *    - Stage 4 (step 34/46 & 46/46): Non-selected icons are 100% transparent, custom icon
         *      crossfades smoothly into the .scr.tns screenshot and completes 320x240 expansion */
        free(launch_st.games[c_idx].icon_pixels);
        launch_st.games[c_idx].icon_pixels = (uint16_t *)malloc(NL_COVER_BYTES);
        assert(launch_st.games[c_idx].icon_pixels != NULL);
        launch_st.games[c_idx].icon_w = NL_COVER_W;
        launch_st.games[c_idx].icon_h = NL_COVER_H;
        for (int p = 0; p < NL_COVER_W * NL_COVER_H; p++) {
            launch_st.games[c_idx].icon_pixels[p] = RGB565(20, 200, 40);  /* Pure Green Icon */
            launch_st.games[c_idx].scr_pixels[p]  = RGB565(220, 40, 200); /* Pure Magenta Screenshot */
        }
        launch_st.launch_game_idx = c_idx;
        for (int fi = 0; fi < launch_st.filtered_count; fi++) {
            if (launch_st.filtered_indices[fi] == c_idx) {
                launch_st.selected_pos = fi;
                break;
            }
        }

        nl_layout_metrics_t lm;
        nl_get_layout_metrics_full(launch_st.settings.layout, launch_st.settings.scroll_dir,
                                   launch_st.settings.show_names, launch_st.settings.show_shortcuts,
                                   launch_st.filtered_count, &lm);
        int sel_x = 0, sel_y = 0;
        nl_compute_grid_slot_pos(&lm, launch_st.filtered_count, launch_st.selected_pos, &sel_x, &sel_y);
        int sel_cx = sel_x + lm.card_w / 2;
        int sel_cy = sel_y + lm.card_h / 2;

        /* Step 1/46 (shrink start): custom icon is Pure Green across the selected card (> 2000 px) */
        nl_render_launch_zoom(&launch_st, fb, 1, 46);
        int green_cnt = 0;
        for (int p = 0; p < NL_SCREEN_W * NL_SCREEN_H; p++) {
            if (fb[p] == RGB565(20, 200, 40)) green_cnt++;
        }
        assert(green_cnt > 2000);

        /* Step 16/46 (ripple propagation window, t_exp ~ 61):
         * - Selected custom icon center is STILL 100% Pure Green (ripples are BEHIND the icon, and screenshot crossfade hasn't started yet!)
         * - Ripples are visible just outside the selected icon's right/bottom perimeter
         * - Screen edges (e.g. x=0..3, x=316..319) are never touched by ripples */
        nl_render_launch_zoom(&launch_st, fb, 16, 46);
        assert(fb[sel_cy * NL_SCREEN_W + sel_cx] == RGB565(20, 200, 40));
        int green_mid = 0;
        for (int p = 0; p < NL_SCREEN_W * NL_SCREEN_H; p++) {
            if (fb[p] == RGB565(20, 200, 40)) green_mid++;
        }
        assert(green_mid > 1800);

        /* Step 26/46 (#R15 early crossfade midpoint, t_exp ~ 126): custom icon & screenshot blended cleanly */
        nl_render_launch_zoom(&launch_st, fb, 26, 46);
        int blended_cnt = 0;
        for (int p = 0; p < NL_SCREEN_W * NL_SCREEN_H; p++) {
            if (fb[p] != RGB565(20, 200, 40) && fb[p] != RGB565(220, 40, 200)) blended_cnt++;
        }
        assert(blended_cnt > 4000);

        /* Verify screen edge far from selected slot remains dim background, untouched by ripples */
        uint16_t edge_px = fb[210 * NL_SCREEN_W + 318];
        assert(edge_px != RGB565(255, 255, 255));

        /* Step 46/46 (final): fullscreen 320x240 is 100% real screenshot (RGB565(220, 40, 200)) */
        nl_render_launch_zoom(&launch_st, fb, 46, 46);
        assert(fb[120 * NL_SCREEN_W + 160] == RGB565(220, 40, 200));
        assert(fb[20 * NL_SCREEN_W + 20] == RGB565(220, 40, 200));
        assert(fb[220 * NL_SCREEN_W + 300] == RGB565(220, 40, 200));

        /* 4. Batch Screenshot Capture Queue (Passer, Annuler, Semi-Auto fallback) */
        nl_handle_key(&launch_st, NL_KEY_SETTINGS);
        launch_st.sui.menu_cursor = NL_SET_MENU_CAPTURE_BATCH;
        nl_handle_key(&launch_st, NL_KEY_EXE);
        assert(launch_st.sui.screen == NL_UI_SETTINGS_CAPTURE_BATCH);
        /* Crafti already has a screenshot, so 7 of 8 games are queued */
        assert(launch_st.sui.cap_total == 7);
        assert(launch_st.sui.cap_active == true);
        assert(launch_st.sui.cap_pos == 0);
        nl_render_frame(&launch_st, fb);

        /* Skip first queued game via RIGHT */
        assert(nl_handle_key(&launch_st, NL_KEY_RIGHT) == true);
        assert(launch_st.sui.cap_pos == 1);
        assert(launch_st.sui.cap_skipped == 1);

        /* Simulate a completed capture on second queued game */
        assert(nl_batch_capture_advance(&launch_st, true, false, NL_CAP_CAPTURED_BLIT) == true);
        assert(launch_st.sui.cap_pos == 2);
        assert(launch_st.sui.cap_captured == 1);

        /* Simulate a no-frame game -> triggers semi-auto prompt without losing progress */
        assert(nl_batch_capture_advance(&launch_st, false, false, NL_CAP_FAIL_NO_FRAMES) == true);
        assert(launch_st.sui.cap_semi_auto == true);
        assert(launch_st.sui.cap_pos == 2);
        nl_render_frame(&launch_st, fb);

        /* Cancel batch capture with ESC -> preserves completed captures and returns to SETTINGS_MAIN */
        assert(nl_handle_key(&launch_st, NL_KEY_ESC) == true);
        assert(launch_st.sui.cap_active == false);
        assert(launch_st.sui.cap_cancelled == true);
        assert(launch_st.sui.cap_captured == 1);
        assert(launch_st.sui.screen == NL_UI_SETTINGS_MAIN);
        nl_render_frame(&launch_st, fb);

        /* 5. Gestion des Icones: 3-column grid navigation + delete confirmation modal + strict 3-file separation */
        launch_st.sui.menu_cursor = NL_SET_MENU_ICONS;
        assert(nl_handle_key(&launch_st, NL_KEY_EXE) == true);
        assert(launch_st.sui.screen == NL_UI_SETTINGS_ICONS);
        assert(launch_st.sui.icon_cursor == 0);
        nl_render_frame(&launch_st, fb);

        /* Navigate to Crafti in the 3-column grid */
        launch_st.sui.icon_cursor = c_idx;
        nl_handle_key(&launch_st, NL_KEY_EXE);
        assert(launch_st.sui.icon_confirm_modal == true);
        assert(launch_st.sui.icon_confirm_yes == false); /* Safe default = Annuler */
        nl_render_frame(&launch_st, fb);

        /* Switch to Supprimer and confirm */
        nl_handle_key(&launch_st, NL_KEY_RIGHT);
        assert(launch_st.sui.icon_confirm_yes == true);
        nl_handle_key(&launch_st, NL_KEY_EXE);
        assert(launch_st.sui.icon_confirm_modal == false);

        /* Verify ONLY Crafti's custom icon (.bmp.tns) was deleted:
         * - Crafti reverts to fallback emblem (NL_ICON_NONE, icon_pixels == NULL)
         * - Crafti's executable (.tns) is 100% intact
         * - Crafti's gameplay screenshot (.scr.tns & has_screenshot) is 100% intact
         * - Other games' icons (e.g. nDoom.ico.tns) are 100% intact */
        assert(launch_st.games[c_idx].icon_source == NL_ICON_NONE);
        assert(launch_st.games[c_idx].icon_pixels == NULL);
        assert(nl_file_exists("/tmp/nlauncher_test_fs/games/arcade/Crafti.bmp.tns") == false);
        assert(nl_file_exists("/tmp/nlauncher_test_fs/games/arcade/Crafti.tns") == true);
        assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/covers/Crafti.scr.tns") == true);
        assert(launch_st.games[c_idx].has_screenshot == true);
        assert(launch_st.games[c_idx].scr_pixels != NULL);
        assert(launch_st.games[d_idx].icon_source == NL_ICON_CAPTURED);
        assert(nl_file_exists("/tmp/nlauncher_test_fs/ndless/covers/nDoom.ico.tns") == true);

        /* 12o. Genuine Windows .ico (16/24/32-bit DIB) in /documents/ndless/icons/<stem>.ico.tns + invalid fallback */
        (void)mkdir("/tmp/nlauncher_test_fs/ndless/icons", 0777);
        {
            const char *ico_path = "/tmp/nlauncher_test_fs/ndless/icons/Crafti.ico.tns";
            FILE *ifp = fopen(ico_path, "wb");
            assert(ifp != NULL);
            uint8_t ico_hdr[22 + 40];
            memset(ico_hdr, 0, sizeof(ico_hdr));
            /* ICONDIR: reserved=0, type=1, count=1 */
            ico_hdr[2] = 1; ico_hdr[4] = 1;
            /* ICONDIRENTRY: 94x68, 24-bit, offset=22 */
            uint32_t stride = (NL_COVER_W * 3U + 3U) & ~3U;
            uint32_t img_bytes = 40U + stride * NL_COVER_H;
            ico_hdr[6] = NL_COVER_W;
            ico_hdr[7] = NL_COVER_H;
            ico_hdr[10] = 1;
            ico_hdr[12] = 24;
            ico_hdr[14] = (uint8_t)(img_bytes & 0xFFU);
            ico_hdr[15] = (uint8_t)((img_bytes >> 8) & 0xFFU);
            ico_hdr[16] = (uint8_t)((img_bytes >> 16) & 0xFFU);
            ico_hdr[18] = 22;
            /* BITMAPINFOHEADER: biSize=40, biWidth=94, biHeight=68*2=136, biPlanes=1, biBitCount=24 */
            ico_hdr[22] = 40;
            ico_hdr[26] = NL_COVER_W;
            ico_hdr[30] = (uint8_t)(NL_COVER_H * 2);
            ico_hdr[34] = 1;
            ico_hdr[36] = 24;
            fwrite(ico_hdr, 1, sizeof(ico_hdr), ifp);
            uint8_t row_data[300];
            memset(row_data, 0, sizeof(row_data));
            for (int x = 0; x < NL_COVER_W; x++) {
                row_data[x * 3 + 0] = 240; /* B */
                row_data[x * 3 + 1] = 120; /* G */
                row_data[x * 3 + 2] = 30;  /* R */
            }
            for (int y = 0; y < NL_COVER_H; y++) {
                fwrite(row_data, 1, stride, ifp);
            }
            fclose(ifp);

            assert(nl_load_game_icon(&launch_st.games[c_idx], launch_st.covers_dir) == true);
            assert(launch_st.games[c_idx].icon_source == NL_ICON_ICO);
            assert(launch_st.games[c_idx].icon_pixels != NULL);
            assert(launch_st.games[c_idx].icon_pixels[0] == RGB565(30, 120, 240));
            /* Screenshot (.scr.tns) remains strictly separate */
            assert(launch_st.games[c_idx].has_screenshot == true);
            assert(nl_delete_custom_icon(&launch_st.games[c_idx], launch_st.covers_dir) == true);
            assert(launch_st.games[c_idx].icon_source == NL_ICON_NONE);
            assert(launch_st.games[c_idx].has_screenshot == true);
        }

        nl_free_icons(&launch_st);
        printf("[PASS] 4-Layer Launch Zoom (shrink + rings behind card + .scr.tns crossfade), Windows .ico (/ndless/icons/) & Gestion des Icones verified.\n");
    }

    /* 12p. Redesigned All-5 Library Grids (1x1..4x4), Aspect-Preserving RGBA .ico Selection & Adaptive Pagination (>19 Dashes) */
    {
        /* 1. Verify all 5 layouts x 4 toggle combinations (square cards, tight bounds, zero overflow) */
        for (int l = 0; l < NL_LAYOUT_COUNT; l++) {
            for (int sn = 0; sn < 2; sn++) {
                for (int sc = 0; sc < 2; sc++) {
                    nl_layout_metrics_t m;
                    nl_get_layout_metrics_ex((nl_layout_t)l, sn != 0, sc != 0, &m);
                    assert(m.card_w == m.card_h);
                    assert(m.card_w >= 24);
                    assert(m.margin_x >= 10);
                    assert(m.top_y >= NL_VIEW_TOP + 2);
                    int right_edge = 2 * m.margin_x + m.cols * m.card_w + (m.cols - 1) * m.gap_x;
                    assert(right_edge <= NL_SCREEN_W);
                    int last_bot = m.top_y + (m.rows - 1) * m.row_stride_y + m.card_h + m.label_h;
                    assert(last_bot <= m.view_bottom);
                }
            }
        }

        /* 2. Multi-entry .ico: 16x16 low-res + 128x72 (16:9) 32bpp RGBA -> picks 128x72, preserves 16:9 & partial alpha */
        static nl_state_t ico_st;
        nl_init_state(&ico_st);
        snprintf(ico_st.covers_dir, sizeof(ico_st.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
        nl_scan_directory(&ico_st, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
        assert(ico_st.total_games >= 1);

        {
            char ico_path[512];
            char stem[64];
            nl_extract_game_stem(ico_st.games[0].filename, stem, sizeof(stem));
            snprintf(ico_path, sizeof(ico_path), "/tmp/nlauncher_test_fs/ndless/icons/%s.ico.tns", stem);
            FILE *fp = fopen(ico_path, "wb");
            assert(fp != NULL);

            uint32_t sz1 = 40U + 16U * 16U * 4U;
            uint32_t sz2 = 40U + 128U * 72U * 4U;
            uint32_t off1 = 6U + 2U * 16U;
            uint32_t off2 = off1 + sz1;

            uint8_t dir_hdr[6 + 32];
            memset(dir_hdr, 0, sizeof(dir_hdr));
            dir_hdr[2] = 1; dir_hdr[4] = 2;
            /* Entry 0: 16x16 32bpp */
            dir_hdr[6] = 16; dir_hdr[7] = 16; dir_hdr[10] = 1; dir_hdr[12] = 32;
            dir_hdr[14] = (uint8_t)(sz1 & 0xFFU); dir_hdr[15] = (uint8_t)((sz1 >> 8) & 0xFFU);
            dir_hdr[18] = (uint8_t)(off1 & 0xFFU); dir_hdr[19] = (uint8_t)((off1 >> 8) & 0xFFU);
            /* Entry 1: 128x72 32bpp */
            dir_hdr[22] = 128; dir_hdr[23] = 72; dir_hdr[26] = 1; dir_hdr[28] = 32;
            dir_hdr[30] = (uint8_t)(sz2 & 0xFFU); dir_hdr[31] = (uint8_t)((sz2 >> 8) & 0xFFU);
            dir_hdr[34] = (uint8_t)(off2 & 0xFFU); dir_hdr[35] = (uint8_t)((off2 >> 8) & 0xFFU);
            fwrite(dir_hdr, 1, sizeof(dir_hdr), fp);

            /* Write entry 0 (red 16x16) */
            uint8_t bih[40];
            memset(bih, 0, sizeof(bih));
            bih[0] = 40; bih[4] = 16; bih[8] = 32; bih[12] = 1; bih[14] = 32;
            fwrite(bih, 1, 40, fp);
            for (int i = 0; i < 16 * 16; i++) {
                uint8_t px[4] = {0, 0, 255, 255};
                fwrite(px, 1, 4, fp);
            }

            /* Write entry 1 (blue 128x72 with 50% alpha) */
            memset(bih, 0, sizeof(bih));
            bih[0] = 40; bih[4] = 128; bih[8] = 144; bih[12] = 1; bih[14] = 32;
            fwrite(bih, 1, 40, fp);
            for (int i = 0; i < 128 * 72; i++) {
                uint8_t px[4] = {240, 80, 20, 128};
                fwrite(px, 1, 4, fp);
            }
            fclose(fp);

            assert(nl_load_game_icon(&ico_st.games[0], ico_st.covers_dir) == true);
            assert(ico_st.games[0].icon_source == NL_ICON_ICO);
            assert(ico_st.games[0].icon_w == NL_ICON_MAX_DIM);
            assert(ico_st.games[0].icon_h == 47);
            assert(ico_st.games[0].icon_alpha != NULL);
            assert(ico_st.games[0].icon_alpha[0] == 128);
            unlink(ico_path);
        }

        /* 3. Render every layout (1x1..4x4) with names ON/OFF and shortcuts ON/OFF cleanly */
        for (int l = 0; l < NL_LAYOUT_COUNT; l++) {
            ico_st.settings.layout = (nl_layout_t)l;
            for (int sn = 0; sn < 2; sn++) {
                ico_st.settings.show_names = (sn != 0);
                ico_st.anim.switch_shownames_fp = sn ? 256 : 0;
                for (int sc = 0; sc < 2; sc++) {
                    ico_st.settings.show_shortcuts = (sc != 0);
                    ico_st.anim.switch_shortcuts_fp = sc ? 256 : 0;
                    nl_render_frame(&ico_st, fb);
                }
            }
        }

        /* 4. Adaptive pagination: <= 19 uses circular dots, >= 20 uses compact dashes across 20, 42, 128 pages */
        static const int high_pages[] = {20, 42, 128};
        const nl_theme_t *dth = nl_get_theme(NL_DISPLAY_DARK, NL_ACCENT_BLUE);
        ico_st.display_mode = NL_DISPLAY_DARK;
        ico_st.accent_color = NL_ACCENT_BLUE;
        ico_st.settings.layout = NL_LAYOUT_3X2;
        ico_st.settings.show_shortcuts = true;
        ico_st.anim.switch_shortcuts_fp = 256;

        for (size_t hi = 0; hi < sizeof(high_pages) / sizeof(high_pages[0]); hi++) {
            int tp = high_pages[hi];
            int mid_p = tp / 2;
            ico_st.total_pages = tp;
            ico_st.current_page = mid_p;
            ico_st.anim.dot_pos_fp = mid_p << 4;
            nl_render_frame(&ico_st, fb);

            int mid_y = NL_PAGINATION_Y + 3;
            int min_x = NL_SCREEN_W, max_x = -1, active_px = 0, inactive_px = 0;
            for (int x = 0; x < NL_SCREEN_W; x++) {
                uint16_t c = fb[mid_y * NL_SCREEN_W + x];
                if (c != dth->bg_main) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (c == dth->accent_primary) active_px++;
                    if (c == dth->dot_inactive) inactive_px++;
                }
            }
            assert(min_x >= 16 && max_x <= 304);
            assert(active_px >= 1);
            assert(inactive_px >= tp - 1);
        }

        nl_free_icons(&ico_st);
        printf("[PASS] All 5 square icon layouts, 16:9 RGBA multi-entry .ico & adaptive dash pagination verified.\n");
    }

    /* 12q. 1/4/8bpp Indexed Palette ICO, 1-bit AND Mask, PNG-Compressed ICO, Symmetric Alpha Trim,
     *      Direct .ico Fallback & Native Framebuffer Preview Export */
    {
        static nl_state_t adv_st;
        nl_init_state(&adv_st);
        snprintf(adv_st.covers_dir, sizeof(adv_st.covers_dir), "/tmp/nlauncher_test_fs/ndless/covers");
        nl_scan_directory(&adv_st, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
        assert(adv_st.total_games >= 1);

        char stem[64];
        nl_extract_game_stem(adv_st.games[0].filename, stem, sizeof(stem));
        char ico_path[512];
        snprintf(ico_path, sizeof(ico_path), "/tmp/nlauncher_test_fs/ndless/icons/%s.ico.tns", stem);

        /* 1. Indexed 8bpp, 4bpp, 1bpp palette ICO + 1-bit AND transparency mask (20x20) */
        const int bpps[3] = {8, 4, 1};
        for (int bi = 0; bi < 3; bi++) {
            int ibpp = bpps[bi];
            int iw = 20, ih = 20;
            uint32_t pal_cnt = 1U << ibpp;
            uint32_t xor_stride = (((uint32_t)iw * (uint32_t)ibpp + 31U) >> 5) * 4U;
            uint32_t and_stride = (((uint32_t)iw + 31U) >> 5) * 4U;
            uint32_t dib_sz = 40U + pal_cnt * 4U + xor_stride * (uint32_t)ih + and_stride * (uint32_t)ih;
            uint8_t *buf = (uint8_t *)calloc(1, 22U + dib_sz);
            assert(buf != NULL);

            buf[2] = 1; buf[4] = 1;
            buf[6] = (uint8_t)iw; buf[7] = (uint8_t)ih;
            buf[8] = (uint8_t)(pal_cnt & 0xFFU);
            buf[10] = 1; buf[12] = (uint8_t)ibpp;
            write_u32_le(buf + 14, dib_sz);
            write_u32_le(buf + 18, 22U);

            write_u32_le(buf + 22, 40U);
            write_u32_le(buf + 26, (uint32_t)iw);
            write_u32_le(buf + 30, (uint32_t)(ih * 2));
            buf[34] = 1; buf[36] = (uint8_t)ibpp;

            uint8_t *pal = buf + 62;
            /* index 0 = green (BGR=20,220,40), index 1 = orange-red (BGR=10,80,240) */
            pal[0] = 20; pal[1] = 220; pal[2] = 40;
            pal[4] = 10; pal[5] = 80;  pal[6] = 240;

            uint8_t *xor_px = pal + pal_cnt * 4U;
            uint8_t *and_px = xor_px + xor_stride * (uint32_t)ih;
            for (int y = 0; y < ih; y++) {
                int frow = ih - 1 - y;
                for (int x = 0; x < iw; x++) {
                    uint8_t idx = (x >= iw / 2) ? 1U : 0U;
                    if (ibpp == 8) {
                        xor_px[(uint32_t)frow * xor_stride + (uint32_t)x] = idx;
                    } else if (ibpp == 4) {
                        xor_px[(uint32_t)frow * xor_stride + (uint32_t)(x >> 1)] |=
                            (uint8_t)(idx << ((x & 1) ? 0 : 4));
                    } else {
                        xor_px[(uint32_t)frow * xor_stride + (uint32_t)(x >> 3)] |=
                            (uint8_t)(idx << (7 - (x & 7)));
                    }
                    if (y == 0) {
                        and_px[(uint32_t)frow * and_stride + (uint32_t)(x >> 3)] |=
                            (uint8_t)(1U << (7 - (x & 7)));
                    }
                }
            }

            FILE *fp = fopen(ico_path, "wb");
            assert(fp != NULL);
            fwrite(buf, 1, 22U + dib_sz, fp);
            fclose(fp);
            free(buf);

            if (adv_st.games[0].icon_pixels) { free(adv_st.games[0].icon_pixels); adv_st.games[0].icon_pixels = NULL; }
            if (adv_st.games[0].icon_alpha)  { free(adv_st.games[0].icon_alpha);  adv_st.games[0].icon_alpha = NULL; }
            assert(nl_load_game_icon(&adv_st.games[0], adv_st.covers_dir) == true);
            assert(adv_st.games[0].icon_source == NL_ICON_ICO);
            assert(adv_st.games[0].icon_w == 20 && adv_st.games[0].icon_h == 20);
            assert(adv_st.games[0].icon_alpha != NULL);
            assert(adv_st.games[0].icon_alpha[0] == 0);
            assert(adv_st.games[0].icon_alpha[15 * 20 + 2] == 255);
            assert(adv_st.games[0].icon_pixels[15 * 20 + 2] == RGB565(40, 220, 20));
            assert(adv_st.games[0].icon_pixels[15 * 20 + 18] == RGB565(240, 80, 10));
        }
        unlink(ico_path);

        /* 2. PNG-compressed .ico (uncompressed deflate block inside zlib) + symmetric 8px transparent outer trim (48x48 -> 34x34) */
        {
            const int pw = 48, ph = 48;
            const uint32_t raw_row = 1U + (uint32_t)pw * 4U;
            const uint32_t raw_len = raw_row * (uint32_t)ph;
            uint8_t *raw = (uint8_t *)calloc(1, raw_len);
            assert(raw != NULL);
            for (int y = 0; y < ph; y++) {
                uint8_t *row = raw + (uint32_t)y * raw_row;
                row[0] = 0; /* filter None */
                if (y >= 8 && y < 40) {
                    for (int x = 8; x < 40; x++) {
                        uint8_t *px = row + 1 + x * 4;
                        px[0] = 56; px[1] = 189; px[2] = 248; px[3] = 255;
                    }
                }
            }
            /* Adler-32 of raw */
            uint32_t a1 = 1U, a2 = 0U;
            for (uint32_t i = 0; i < raw_len; i++) {
                a1 = (a1 + raw[i]) % 65521U;
                a2 = (a2 + a1) % 65521U;
            }
            uint32_t adler = (a2 << 16) | a1;
            uint32_t zlen = 2U + 5U + raw_len + 4U;
            uint8_t *zbuf = (uint8_t *)malloc(zlen);
            assert(zbuf != NULL);
            zbuf[0] = 0x78; zbuf[1] = 0x01;
            zbuf[2] = 0x01; /* BFINAL=1, stored block */
            zbuf[3] = (uint8_t)(raw_len & 0xFFU);
            zbuf[4] = (uint8_t)((raw_len >> 8) & 0xFFU);
            zbuf[5] = (uint8_t)(~raw_len & 0xFFU);
            zbuf[6] = (uint8_t)((~raw_len >> 8) & 0xFFU);
            memcpy(zbuf + 7, raw, raw_len);
            zbuf[7 + raw_len + 0] = (uint8_t)((adler >> 24) & 0xFFU);
            zbuf[7 + raw_len + 1] = (uint8_t)((adler >> 16) & 0xFFU);
            zbuf[7 + raw_len + 2] = (uint8_t)((adler >> 8) & 0xFFU);
            zbuf[7 + raw_len + 3] = (uint8_t)(adler & 0xFFU);
            free(raw);

            uint32_t png_sz = 8U + (12U + 13U) + (12U + zlen) + 12U;
            uint8_t *ico_png = (uint8_t *)calloc(1, 22U + png_sz);
            assert(ico_png != NULL);
            ico_png[2] = 1; ico_png[4] = 1;
            ico_png[6] = (uint8_t)pw; ico_png[7] = (uint8_t)ph;
            ico_png[10] = 1; ico_png[12] = 32;
            write_u32_le(ico_png + 14, png_sz);
            write_u32_le(ico_png + 18, 22U);

            uint8_t *p = ico_png + 22;
            static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
            memcpy(p, png_sig, 8); p += 8;
            /* IHDR */
            p[3] = 13; p[4] = 'I'; p[5] = 'H'; p[6] = 'D'; p[7] = 'R';
            p[11] = (uint8_t)pw; p[15] = (uint8_t)ph; p[16] = 8; p[17] = 6;
            p += 25;
            /* IDAT */
            p[0] = (uint8_t)((zlen >> 24) & 0xFFU); p[1] = (uint8_t)((zlen >> 16) & 0xFFU);
            p[2] = (uint8_t)((zlen >> 8) & 0xFFU);  p[3] = (uint8_t)(zlen & 0xFFU);
            p[4] = 'I'; p[5] = 'D'; p[6] = 'A'; p[7] = 'T';
            memcpy(p + 8, zbuf, zlen);
            p += 12U + zlen;
            free(zbuf);
            /* IEND */
            p[4] = 'I'; p[5] = 'E'; p[6] = 'N'; p[7] = 'D';

            /* Write as direct `.ico` (without `.tns`) to also test fallback filename lookup */
            char raw_ico_path[512];
            snprintf(raw_ico_path, sizeof(raw_ico_path), "/tmp/nlauncher_test_fs/ndless/icons/%s.ico", stem);
            FILE *fp = fopen(raw_ico_path, "wb");
            assert(fp != NULL);
            fwrite(ico_png, 1, 22U + png_sz, fp);
            fclose(fp);
            free(ico_png);

            if (adv_st.games[0].icon_pixels) { free(adv_st.games[0].icon_pixels); adv_st.games[0].icon_pixels = NULL; }
            if (adv_st.games[0].icon_alpha)  { free(adv_st.games[0].icon_alpha);  adv_st.games[0].icon_alpha = NULL; }
            assert(nl_load_game_icon(&adv_st.games[0], adv_st.covers_dir) == true);
            assert(adv_st.games[0].icon_source == NL_ICON_ICO);
            assert(adv_st.games[0].icon_w == 34 && adv_st.games[0].icon_h == 34);
            assert(adv_st.games[0].icon_alpha != NULL);
            assert(adv_st.games[0].icon_pixels[17 * 34 + 17] == RGB565(56, 189, 248));
            unlink(raw_ico_path);
        }

        /* 3. Optional export of genuine C framebuffer previews across all 5 layouts */
        const char *exp_dir = getenv("NL_EXPORT_FRAMES");
        if (exp_dir && exp_dir[0] != '\0') {
            static const uint8_t pal_rgb[8][3] = {
                {220, 38, 38}, {245, 158, 11}, {56, 189, 248}, {139, 92, 246},
                {234, 179, 8}, {37, 99, 235},  {16, 185, 129}, {244, 63, 94}
            };
            for (int gi = 0; gi < adv_st.total_games && gi < 8; gi++) {
                const int iw = 96, ih = 96;
                if (adv_st.games[gi].icon_pixels) free(adv_st.games[gi].icon_pixels);
                if (adv_st.games[gi].icon_alpha) free(adv_st.games[gi].icon_alpha);
                adv_st.games[gi].icon_pixels = (uint16_t *)malloc((size_t)(iw * ih) * sizeof(uint16_t));
                adv_st.games[gi].icon_alpha  = (uint8_t *)malloc((size_t)(iw * ih));
                assert(adv_st.games[gi].icon_pixels != NULL && adv_st.games[gi].icon_alpha != NULL);
                adv_st.games[gi].icon_source = NL_ICON_ICO;
                adv_st.games[gi].icon_w = iw;
                adv_st.games[gi].icon_h = ih;
                uint8_t br = pal_rgb[gi % 8][0], bg = pal_rgb[gi % 8][1], bb = pal_rgb[gi % 8][2];
                for (int y = 0; y < ih; y++) {
                    for (int x = 0; x < iw; x++) {
                        bool emblem = (x >= 22 && x < 74 && y >= 22 && y < 74 && ((x + y + gi * 7) % 19 < 11));
                        uint8_t pr = emblem ? 248 : (uint8_t)((br * (iw - y / 2)) / iw);
                        uint8_t pg = emblem ? 250 : (uint8_t)((bg * (iw - y / 2)) / iw);
                        uint8_t pb = emblem ? 252 : (uint8_t)((bb * (iw - y / 2)) / iw);
                        adv_st.games[gi].icon_pixels[y * iw + x] = RGB565(pr, pg, pb);
                        adv_st.games[gi].icon_alpha[y * iw + x] = 255;
                    }
                }
            }
            static const char *lnames[NL_LAYOUT_COUNT] = {"1x1", "2x1", "3x2", "3x3", "4x4"};
            adv_st.display_mode = NL_DISPLAY_DARK;
            adv_st.accent_color = NL_ACCENT_BLUE;
            adv_st.settings.show_names = false;
            adv_st.anim.switch_shownames_fp = 0;
            adv_st.settings.show_shortcuts = true;
            adv_st.anim.switch_shortcuts_fp = 256;
            adv_st.selected_pos = 0;
            adv_st.current_page = 0;
            adv_st.anim.scroll_y_fp = 0;
            adv_st.anim.dot_pos_fp = 0;
            adv_st.anim.card_scale_fp[0] = NL_SCALE_SELECTED;
            for (int l = 0; l < NL_LAYOUT_COUNT; l++) {
                adv_st.settings.layout = (nl_layout_t)l;
                nl_render_frame(&adv_st, fb);
                char bmp_out[512];
                snprintf(bmp_out, sizeof(bmp_out), "%s/native_%s.bmp", exp_dir, lnames[l]);
                assert(export_fb_to_bmp(bmp_out, fb, NL_SCREEN_W, NL_SCREEN_H) == true);
            }
        }

        nl_free_icons(&adv_st);
        printf("[PASS] Indexed 1/4/8bpp ICOs, AND masks, PNG-compressed ICOs, symmetric alpha trim & .ico fallback verified.\n");
    }

    /* 12r. Legacy Autostart Cleanup & Boot-Guard Safety Verification */
    {
        const char *bg_root = "/tmp/nl_boot_guard_test";
        int rm_rc = system("rm -rf /tmp/nl_boot_guard_test && mkdir -p /tmp/nl_boot_guard_test/ndless/startup /tmp/nl_boot_guard_test/phoenix/syst/poweroff /tmp/nl_boot_guard_test/games");
        assert(rm_rc == 0);

        const char *bg_launcher = "/tmp/nl_boot_guard_test/ndless/N-Launcher.tns";
        const char *bg_cfg      = "/tmp/nl_boot_guard_test/ndless/nlauncher.cfg.tns";
        const char *bg_guard    = "/tmp/nl_boot_guard_test/ndless/nl_boot_guard.cfg.tns";
        const char *bg_stub     = "/tmp/nl_boot_guard_test/ndless/startup/nl_boot.tns";
        const char *bg_stub_up  = "/tmp/nl_boot_guard_test/ndless/startup/NL_BOOT.TNS";
        const char *bg_stub_old = "/tmp/nl_boot_guard_test/ndless/startup/nl_boot_stub.tns";
        const char *bg_stub_nl  = "/tmp/nl_boot_guard_test/ndless/startup/N-Launcher.tns";
        const char *bg_game     = "/tmp/nl_boot_guard_test/games/Crafti.tns";

        create_mock_zehn_tns(bg_launcher, "N-Launcher", "daniel.b", 3, true, true, 128);
        create_mock_zehn_tns(bg_game, "Crafti", "Vogtinator", 1, true, true, 128);
        create_mock_ti_doc_tns(bg_stub);
        create_mock_ti_doc_tns(bg_stub_up);
        create_mock_ti_doc_tns(bg_stub_old);
        create_mock_ti_doc_tns(bg_stub_nl);
        create_mock_ti_doc_tns(bg_guard);
        snprintf(g_mock_guard_path, sizeof(g_mock_guard_path), "%s", bg_guard);
        (void)mock_stub_file_ok;
        (void)mock_stub_exec;

        /* Verify nl_cleanup_legacy_autostart deletes ALL legacy startup files while keeping /ndless/N-Launcher.tns and games intact */
        assert(nl_file_exists(bg_stub) == true);
        assert(nl_file_exists(bg_stub_up) == true);
        assert(nl_file_exists(bg_stub_old) == true);
        assert(nl_file_exists(bg_stub_nl) == true);
        assert(nl_file_exists(bg_guard) == true);

        nl_cleanup_legacy_autostart(bg_root);

        assert(nl_file_exists(bg_stub) == false);
        assert(nl_file_exists(bg_stub_up) == false);
        assert(nl_file_exists(bg_stub_old) == false);
        assert(nl_file_exists(bg_stub_nl) == false);
        assert(nl_file_exists(bg_guard) == false);
        assert(nl_file_exists(bg_launcher) == true);
        assert(nl_file_exists(bg_game) == true);

        /* Verify config with legacy autostart=1 and show_battery=1 loads cleanly and ignores dead options */
        FILE *cfp = fopen(bg_cfg, "wb");
        assert(cfp != NULL);
        fprintf(cfp, "display_mode=gray\naccent_color=red\nlayout=4x4\nautostart=1\nshow_battery=1\n");
        fclose(cfp);

        static nl_state_t bg_st;
        nl_init_state(&bg_st);
        snprintf(bg_st.sys.docs_root, sizeof(bg_st.sys.docs_root), "%s", bg_root);
        snprintf(bg_st.sys.self_path, sizeof(bg_st.sys.self_path), "%s", bg_launcher);
        nl_scan_directory(&bg_st, bg_root, bg_launcher, 4);
        assert(nl_load_config(&bg_st, bg_cfg) == true);
        assert(bg_st.display_mode == NL_DISPLAY_GRAY);
        assert(bg_st.accent_color == NL_ACCENT_RED);
        assert(bg_st.settings.layout == NL_LAYOUT_4X4);

        nl_free_icons(&bg_st);
        printf("[PASS] Legacy autostart cleanup, startup stub purge & config migration verified.\n");
    }

    /* 12s. Persistent Binary Library & Pre-Decoded Icon Cache (nlauncher.cache.tns),
     *      Settings « Actualiser la bibliotheque » & Experimental Global TI-OS Hotkey (nl_hotkey.tns) */
    {
        /* 1. Verify standard IEEE 802.3 CRC32 test vector ("123456789" -> 0xCBF43926) */
        assert(nl_crc32_compute("123456789", 9U) == 0xCBF43926U);

        const char *c_root     = "/tmp/nl_cache_hotkey_test";
        const char *c_launcher = "/tmp/nl_cache_hotkey_test/ndless/N-Launcher.tns";
        const char *c_cfg      = "/tmp/nl_cache_hotkey_test/ndless/nlauncher.cfg.tns";
        const char *c_cache    = "/tmp/nl_cache_hotkey_test/ndless/nlauncher.cache.tns";
        const char *c_hk_stub  = "/tmp/nl_cache_hotkey_test/ndless/startup/nl_hotkey.tns";
        const char *c_game1    = "/tmp/nl_cache_hotkey_test/games/Crafti.tns";
        const char *c_game2    = "/tmp/nl_cache_hotkey_test/games/nDoom.tns";
        const char *c_ico1     = "/tmp/nl_cache_hotkey_test/ndless/icons/Crafti.ico.tns";
        const char *c_scr2     = "/tmp/nl_cache_hotkey_test/ndless/covers/nDoom.scr.tns";

        int rc_mk = system("rm -rf /tmp/nl_cache_hotkey_test && mkdir -p /tmp/nl_cache_hotkey_test/ndless/startup /tmp/nl_cache_hotkey_test/ndless/icons /tmp/nl_cache_hotkey_test/ndless/covers /tmp/nl_cache_hotkey_test/games");
        assert(rc_mk == 0);

        create_mock_zehn_tns(c_launcher, "N-Launcher", "daniel.b", 3, true, true, 128);
        create_mock_zehn_tns(c_game1, "Crafti", "Vogtinator", 2, true, true, 128);
        create_mock_zehn_tns(c_game2, "nDoom", "Mrakoplaz", 1, true, false, 128);
        create_mock_ico_32bpp(c_ico1, 48, 48, 56, 189, 248);

        /* First boot: full recursive scan + icon load + save cache + save config */
        static nl_state_t first_boot;
        nl_init_state(&first_boot);
        snprintf(first_boot.sys.docs_root, sizeof(first_boot.sys.docs_root), "%s", c_root);
        snprintf(first_boot.sys.self_path, sizeof(first_boot.sys.self_path), "%s", c_launcher);
        snprintf(first_boot.covers_dir, sizeof(first_boot.covers_dir), "%s/ndless/covers", c_root);
        snprintf(first_boot.icons_dir, sizeof(first_boot.icons_dir), "%s/ndless/icons", c_root);
        first_boot.sys.has_ndless = true;
        first_boot.sys.ndless_persistent = true;

        /* Missing cache on first boot -> nl_load_library_cache returns false */
        assert(nl_load_library_cache(&first_boot, c_cache) == false);
        assert(first_boot.sys.cache_valid == false);

        nl_scan_directory(&first_boot, c_root, c_launcher, 4);
        assert(first_boot.total_games == 2);
        nl_load_all_icons(&first_boot);

        /* Add a gameplay screenshot (.scr.tns) to game 1 and custom stats/favorites/shortcuts */
        int idx_crafti = (strcmp(first_boot.games[0].title, "Crafti") == 0) ? 0 : 1;
        int idx_ndoom  = 1 - idx_crafti;
        assert(first_boot.games[idx_crafti].icon_source == NL_ICON_ICO);
        assert(first_boot.games[idx_crafti].icon_w == 48 && first_boot.games[idx_crafti].icon_h == 48);
        assert(first_boot.games[idx_crafti].icon_pixels != NULL);
        assert(first_boot.games[idx_crafti].icon_alpha != NULL);
        uint16_t expected_center_px = first_boot.games[idx_crafti].icon_pixels[24 * 48 + 24];
        uint8_t  expected_center_al = first_boot.games[idx_crafti].icon_alpha[24 * 48 + 24];

        first_boot.games[idx_ndoom].scr_pixels = (uint16_t *)malloc(NL_COVER_BYTES);
        assert(first_boot.games[idx_ndoom].scr_pixels != NULL);
        for (int p = 0; p < NL_COVER_W * NL_COVER_H; p++) {
            first_boot.games[idx_ndoom].scr_pixels[p] = ((p & 64) != 0) ? RGB565(220, 38, 38) : RGB565(250, 120, 40);
        }
        first_boot.games[idx_ndoom].has_screenshot = true;
        assert(nl_save_captured_screenshot(&first_boot.games[idx_ndoom], first_boot.covers_dir) == true);
        assert(nl_file_exists(c_scr2) == true);

        first_boot.display_mode = NL_DISPLAY_LIGHT;
        first_boot.accent_color = NL_ACCENT_GREEN;
        first_boot.settings.layout = NL_LAYOUT_2X2;
        first_boot.games[idx_crafti].is_favorite = true;
        first_boot.games[idx_crafti].launch_count = 19U;
        first_boot.games[idx_crafti].play_time_sec = 1420U;
        first_boot.games[idx_crafti].shortcut_key = 'K';

        assert(nl_save_config(&first_boot, c_cfg) == true);
        assert(nl_save_library_cache(&first_boot, c_cache) == true);
        assert(nl_file_exists(c_cache) == true);

        /* Delete the raw .ico.tns on disk to prove cold boot from cache uses pre-decoded icon buffers
         * inside nlauncher.cache.tns without touching individual .ico.tns files! */
        assert(unlink(c_ico1) == 0);

        /* Second boot: fast cold load directly from nlauncher.cache.tns (ZERO directory scan!) */
        static nl_state_t cached_boot;
        nl_init_state(&cached_boot);
        snprintf(cached_boot.sys.docs_root, sizeof(cached_boot.sys.docs_root), "%s", c_root);
        snprintf(cached_boot.sys.self_path, sizeof(cached_boot.sys.self_path), "%s", c_launcher);
        snprintf(cached_boot.covers_dir, sizeof(cached_boot.covers_dir), "%s/ndless/covers", c_root);
        snprintf(cached_boot.icons_dir, sizeof(cached_boot.icons_dir), "%s/ndless/icons", c_root);

        assert(nl_load_library_cache(&cached_boot, c_cache) == true);
        assert(cached_boot.sys.cache_valid == true);
        assert(cached_boot.sys.cache_crc32 != 0U);
        assert(cached_boot.total_games == 2);
        assert(nl_load_config(&cached_boot, c_cfg) == true);
        nl_load_all_screenshots(&cached_boot);

        int c_idx_crafti = (strcmp(cached_boot.games[0].title, "Crafti") == 0) ? 0 : 1;
        int c_idx_ndoom  = 1 - c_idx_crafti;
        assert(cached_boot.games[c_idx_crafti].icon_source == NL_ICON_ICO);
        assert(cached_boot.games[c_idx_crafti].icon_w == 48 && cached_boot.games[c_idx_crafti].icon_h == 48);
        assert(cached_boot.games[c_idx_crafti].icon_pixels != NULL);
        assert(cached_boot.games[c_idx_crafti].icon_alpha != NULL);
        assert(cached_boot.games[c_idx_crafti].icon_pixels[24 * 48 + 24] == expected_center_px);
        assert(cached_boot.games[c_idx_crafti].icon_alpha[24 * 48 + 24] == expected_center_al);
        assert(cached_boot.games[c_idx_crafti].is_favorite == true);
        assert(cached_boot.games[c_idx_crafti].launch_count == 19U);
        assert(cached_boot.games[c_idx_crafti].play_time_sec == 1420U);
        assert(cached_boot.games[c_idx_crafti].shortcut_key == 'K');
        assert(cached_boot.games[c_idx_ndoom].has_screenshot == true);
        assert(cached_boot.games[c_idx_ndoom].scr_pixels != NULL);
        assert(cached_boot.display_mode == NL_DISPLAY_LIGHT);
        assert(cached_boot.accent_color == NL_ACCENT_GREEN);
        assert(cached_boot.settings.layout == NL_LAYOUT_2X2);

        /* Test 2: Flipping 1 bit in nlauncher.cache.tns payload triggers CRC32 mismatch -> returns false */
        {
            FILE *cfp = fopen(c_cache, "r+b");
            assert(cfp != NULL);
            fseek(cfp, (long)sizeof(nl_cache_header_t) + 12L, SEEK_SET);
            uint8_t b = 0;
            assert(fread(&b, 1, 1, cfp) == 1);
            b ^= 0x40U;
            fseek(cfp, (long)sizeof(nl_cache_header_t) + 12L, SEEK_SET);
            assert(fwrite(&b, 1, 1, cfp) == 1);
            fclose(cfp);

            static nl_state_t corrupt_boot;
            nl_init_state(&corrupt_boot);
            assert(nl_load_library_cache(&corrupt_boot, c_cache) == false);
            assert(corrupt_boot.sys.cache_valid == false);
            assert(corrupt_boot.total_games == 0);
            nl_free_icons(&corrupt_boot);
        }

        /* Re-save valid cache, then delete one .tns executable on flash -> cache detects stale entry & returns false */
        assert(nl_save_library_cache(&first_boot, c_cache) == true);
        assert(unlink(c_game2) == 0);
        {
            static nl_state_t stale_boot;
            nl_init_state(&stale_boot);
            assert(nl_load_library_cache(&stale_boot, c_cache) == false);
            assert(stale_boot.sys.cache_valid == false);
            nl_free_icons(&stale_boot);
        }
        create_mock_zehn_tns(c_game2, "nDoom", "Mrakoplaz", 1, true, false, 128);
        assert(nl_save_library_cache(&first_boot, c_cache) == true);

        /* Test 3: Settings « Rescanner les jeux » (NL_SET_MENU_REFRESH_LIB) opens NL_UI_SETTINGS_RESCAN & sets rescan_req */
        cached_boot.sui.screen = NL_UI_SETTINGS_MAIN;
        cached_boot.sui.menu_cursor = NL_SET_MENU_REFRESH_LIB;
        cached_boot.sui.rescan_req = false;
        assert(nl_handle_key(&cached_boot, NL_KEY_EXE) == true);
        assert(cached_boot.sui.screen == NL_UI_SETTINGS_RESCAN);
        assert(cached_boot.sui.rescan_req == true);
        assert(strstr(cached_boot.anim.toast_text, "BIBLIOTHEQUE ACTUALISEE") != NULL);
        /* Return cleanly to NL_UI_SETTINGS_MAIN before Test 4 */
        assert(nl_handle_key(&cached_boot, NL_KEY_ESC) == true);
        assert(cached_boot.sui.screen == NL_UI_SETTINGS_MAIN);

        /* Test 4: Experimental Global TI-OS Hotkey (nl_hotkey.tns + nl_hotkey_set + safety guards) */
        assert(cached_boot.settings.global_hotkey == false); /* Disabled by default */
        assert(cached_boot.sys.hotkey_active == false);
        cached_boot.sys.has_ndless = true;
        cached_boot.sui.menu_cursor = NL_SET_MENU_HOTKEY;
        assert(nl_handle_key(&cached_boot, NL_KEY_EXE) == true);
        assert(cached_boot.sui.screen == NL_UI_SETTINGS_HOTKEY);
        nl_render_frame(&cached_boot, fb);

        /* Enable hotkey via EXE in NL_UI_SETTINGS_HOTKEY */
        assert(nl_handle_key(&cached_boot, NL_KEY_EXE) == true);
        assert(cached_boot.settings.global_hotkey == true);
        assert(cached_boot.sys.hotkey_active == true);
        assert(nl_file_exists(c_hk_stub) == true);
        assert(nl_hotkey_verify_active(c_root, &cached_boot.sys) == true);
        assert(nl_save_config(&cached_boot, c_cfg) == true);

        /* Verify ARM32 PC-free prologue validator (nl_arm_instr_is_pc_free) */
        assert(nl_arm_instr_is_pc_free(0xE92D47F0U) == true);  /* PUSH {r4-r10, lr} */
        assert(nl_arm_instr_is_pc_free(0xE24DD010U) == true);  /* SUB sp, sp, #16 */
        assert(nl_arm_instr_is_pc_free(0xE1A04000U) == true);  /* MOV r4, r0 */
        assert(nl_arm_instr_is_pc_free(0xE59F0014U) == false); /* LDR r0, [pc, #20] -> rejected! */
        assert(nl_arm_instr_is_pc_free(0xEB000010U) == false); /* BL ... -> rejected! */
        assert(nl_arm_instr_is_pc_free(0xE28F0008U) == false); /* ADR r0, ... -> rejected! */

        /* Verify nl_hotkey_should_trigger safety guards:
         *  - Blocked when nl_active.lock.tns is present (game or N-Launcher running)
         *  - Blocked when ESC/ON recovery key is held
         *  - Blocked when boot guard has pending=1 (crash detected)
         *  - Allowed ONLY when in TI-OS menus with global_hotkey=1 and zero active lock */
        char resolved_target[256];
        nl_bg_set_active_lock(c_root, true);
        assert(nl_bg_is_active_lock_present(c_root) == true);
        assert(nl_hotkey_should_trigger(true, true, false, false, false, c_root, mock_stub_file_ok, resolved_target, sizeof(resolved_target)) == false);

        nl_bg_set_active_lock(c_root, false);
        assert(nl_bg_is_active_lock_present(c_root) == false);
        assert(nl_hotkey_should_trigger(true, true, true, false, false, c_root, mock_stub_file_ok, resolved_target, sizeof(resolved_target)) == false);
        assert(nl_hotkey_should_trigger(false, true, false, false, false, c_root, mock_stub_file_ok, resolved_target, sizeof(resolved_target)) == false);
        assert(nl_hotkey_should_trigger(true, true, false, false, false, c_root, mock_stub_file_ok, resolved_target, sizeof(resolved_target)) == true);
        assert(strcmp(resolved_target, c_launcher) == 0);

        /* Immediate disable from Settings removes nl_hotkey.tns */
        assert(nl_handle_key(&cached_boot, NL_KEY_EXE) == true);
        assert(cached_boot.settings.global_hotkey == false);
        assert(cached_boot.sys.hotkey_active == false);
        assert(nl_file_exists(c_hk_stub) == false);
        assert(nl_save_config(&cached_boot, c_cfg) == true);
        assert(nl_hotkey_should_trigger(true, true, false, false, false, c_root, mock_stub_file_ok, resolved_target, sizeof(resolved_target)) == false);

        nl_free_icons(&first_boot);
        nl_free_icons(&cached_boot);
        printf("[PASS] Persistent library & pre-decoded icon cache (CRC32), Settings refresh & global TI-OS hotkey verified.\n");
    }

    /* 23. Deferred Last-Used Sort on Return & Pure White Launch Glows Across All Themes */
    {
        static nl_state_t ls;
        nl_init_state(&ls);
        ls.settings.layout = NL_LAYOUT_3X2;
        ls.settings.sort_order = NL_SORT_LAST_USED;
        ls.settings.show_names = false;
        ls.settings.show_shortcuts = true;
        ls.total_games = 4;
        for (int i = 0; i < 4; i++) {
            snprintf(ls.games[i].title, sizeof(ls.games[i].title), "Game_%d", i);
            snprintf(ls.games[i].filename, sizeof(ls.games[i].filename), "Game_%d.tns", i);
            ls.games[i].last_played_rank = (uint32_t)(10 - i); /* Game_0=10, Game_1=9, Game_2=8, Game_3=7 */
            ls.games[i].last_played_ts = 1762900000U + (uint32_t)((10 - i) * 60);
            ls.games[i].icon_source = NL_ICON_ICO;
            ls.games[i].icon_w = 64;
            ls.games[i].icon_h = 64;
            ls.games[i].icon_pixels = (uint16_t *)malloc(64 * 64 * sizeof(uint16_t));
            assert(ls.games[i].icon_pixels != NULL);
            for (int p = 0; p < 64 * 64; p++) {
                ls.games[i].icon_pixels[p] = RGB565(30, 64, 140);
            }
        }
        ls.play_counter = 10U;
        nl_rebuild_filter(&ls);
        assert(ls.filtered_count == 4);
        assert(ls.filtered_indices[0] == 0);
        assert(ls.filtered_indices[1] == 1);
        assert(ls.filtered_indices[2] == 2);
        assert(ls.filtered_indices[3] == 3);

        /* Select Game_2 at slot 2 and press EXE:
         * Game_2 MUST keep its exact slot (slot 2) throughout the entire launch animation!
         * Zero library items may move or reorder during launch! */
        ls.selected_pos = 2;
        assert(nl_handle_key(&ls, NL_KEY_EXE) == true);
        assert(ls.launch_requested == true);
        assert(ls.launch_game_idx == 2);
        assert(ls.selected_pos == 2);
        assert(ls.filtered_indices[0] == 0);
        assert(ls.filtered_indices[1] == 1);
        assert(ls.filtered_indices[2] == 2);
        assert(ls.filtered_indices[3] == 3);
        for (int i = 0; i < 4; i++) {
            assert(ls.anim.card_dx_fp[i] == 0);
            assert(ls.anim.card_dy_fp[i] == 0);
        }

        /* Verify across all 4 accent themes (BLUE, RED, GREEN, YELLOW) that the 3 concentric
         * launch ripples are pure white (neutral RGB where R5 == B5 in RGB565) */
        nl_layout_metrics_t lm;
        nl_get_layout_metrics_full(ls.settings.layout, ls.settings.scroll_dir,
                                   ls.settings.show_names, ls.settings.show_shortcuts,
                                   ls.filtered_count, &lm);
        int slot2_cx = lm.margin_x + 2 * (lm.card_w + lm.gap_x) + lm.card_w / 2;
        int slot2_cy = lm.top_y + lm.card_h / 2;
        int ray_x = slot2_cx;
        int ray_y0 = slot2_cy + (lm.card_h / 2) + 8;
        int ray_y1 = ray_y0 + 22;
        if (ray_y1 > NL_VIEW_BOTTOM - 2) ray_y1 = NL_VIEW_BOTTOM - 2;

        for (int acc = 0; acc < 4; acc++) {
            ls.display_mode = NL_DISPLAY_DARK;
            ls.accent_color = (nl_accent_color_t)acc;
            nl_render_launch_zoom(&ls, fb, 13, 46);
            uint16_t bg_px = fb[205 * NL_SCREEN_W + ray_x]; /* Empty area far below ripple */
            int lit_ripple_pixels = 0;
            for (int py = ray_y0; py < ray_y1; py++) {
                uint16_t px = fb[py * NL_SCREEN_W + ray_x];
                if (px == bg_px) continue;
                lit_ripple_pixels++;
                /* Verify px is produced solely by 1 or 2 passes of pure white RGB565(255,255,255) lerp over bg_px */
                bool matched_pure_white = false;
                for (unsigned a0 = 1U; a0 <= 32U && !matched_pure_white; a0++) {
                    uint16_t p1 = nl_rgb565_lerp(bg_px, RGB565(255, 255, 255), a0);
                    if (p1 == px) {
                        matched_pure_white = true;
                        break;
                    }
                    for (unsigned a1 = 1U; a1 <= 32U; a1++) {
                        if (nl_rgb565_lerp(p1, RGB565(255, 255, 255), a1) == px) {
                            matched_pure_white = true;
                            break;
                        }
                    }
                }
                assert(matched_pure_white);
            }
            assert(lit_ripple_pixels >= 3);
        }

        /* Now simulate exiting Game_2 and returning to N-Launcher:
         * Sort updates ONLY now, moving Game_2 to slot 0 and initializing smooth slot-glide offsets! */
        ls.launch_requested = false;
        nl_record_play_session(&ls, 2, 1762910000U, 1762910180U);
        nl_rebuild_filter(&ls);
        ls.launch_game_idx = -1;

        assert(ls.filtered_indices[0] == 2);
        assert(ls.filtered_indices[1] == 0);
        assert(ls.filtered_indices[2] == 1);
        assert(ls.filtered_indices[3] == 3);
        assert(ls.selected_pos == 0);
        /* Game_2 moved from col 2 to col 0 -> positive initial dx glide offset that decays smoothly to 0 */
        assert(ls.anim.card_dx_fp[2] > 0);
        assert(ls.anim.card_dx_fp[0] < 0);
        assert(ls.anim.card_dx_fp[1] < 0);
        for (int step = 0; step < 32; step++) {
            nl_anim_step(&ls);
        }
        assert(ls.anim.card_dx_fp[2] == 0);
        assert(ls.anim.card_dx_fp[0] == 0);
        assert(ls.anim.card_dx_fp[1] == 0);

        nl_free_icons(&ls);
        printf("[PASS] Deferred last-used sort on game return & pure white launch ripples across all 4 themes verified.\n");
    }

    /* 24. Multi-User Profile System, 43 Exclusive Avatars, Quick Selector, Shortcuts & Isolation */
    {
        nl_state_t *ps = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ps != NULL);
        nl_init_state(ps);

        /* Mock 3 shared games in library */
        ps->total_games = 3;
        snprintf(ps->games[0].title, sizeof(ps->games[0].title), "2048");
        snprintf(ps->games[0].filename, sizeof(ps->games[0].filename), "2048.tns");
        snprintf(ps->games[0].path, sizeof(ps->games[0].path), "/tmp/nl_p_game0.tns");
        snprintf(ps->games[1].title, sizeof(ps->games[1].title), "Crafti");
        snprintf(ps->games[1].filename, sizeof(ps->games[1].filename), "Crafti.tns");
        snprintf(ps->games[1].path, sizeof(ps->games[1].path), "/tmp/nl_p_game1.tns");
        snprintf(ps->games[2].title, sizeof(ps->games[2].title), "gbSP");
        snprintf(ps->games[2].filename, sizeof(ps->games[2].filename), "gbSP.tns");
        snprintf(ps->games[2].path, sizeof(ps->games[2].path), "/tmp/nl_p_game2.tns");
        FILE *gf = fopen(ps->games[1].path, "wb");
        assert(gf != NULL);
        fwrite("TEST_TNS_GAME_OK_1234", 1, 21, gf);
        fclose(gf);

        /* Primary default profile created automatically */
        assert(ps->profiles.count == 1);
        assert(ps->profiles.active_index == 0);
        assert(ps->profiles.items[0].is_primary == true);
        assert(ps->profiles.items[0].avatar_id == 0);

        /* Configure Profile 1 preferences & game data */
        ps->games[0].is_favorite = true;
        ps->games[0].launch_count = 7;
        ps->games[0].shortcut_key = 'A';
        ps->settings.layout = NL_LAYOUT_2X2;
        ps->settings.sort_order = NL_SORT_MOST_LAUNCHED;
        ps->display_mode = NL_DISPLAY_DARK;
        ps->accent_color = NL_ACCENT_BLUE;
        nl_rebuild_filter(ps);

        /* Reserve check on '0', '.', '-' */
        assert(nl_shortcut_is_reserved('0') == true);
        assert(nl_shortcut_is_reserved('.') == true);
        assert(nl_shortcut_is_reserved('-') == true);
        assert(nl_shortcut_is_assignable('0') == false);
        const char *assign_str = NULL;
        int n_assign = nl_get_assignable_keys(&assign_str);
        assert(n_assign == 28 && strchr(assign_str, '0') == NULL);

        /* 4 categories cover all 43 avatars (16 + 9 + 9 + 9) */
        int sum_cnt = 0;
        for (int c = 0; c < NL_AVATAR_CAT_COUNT; c++) {
            int st = 0, cnt = 0;
            nl_avatar_category_range(c, &st, &cnt);
            sum_cnt += cnt;
            assert(nl_avatar_category_label(c)[0] != '\0');
        }
        assert(sum_cnt == 43);

        /* Cannot create profile with occupied avatar 0 */
        assert(nl_profile_create(ps, "Alice", 0) == -1);
        /* Create Alice (avatar 18, Fortnite) and Bob (avatar 34, Rabbins) */
        int p_alice = nl_profile_create(ps, "Alice", 18);
        assert(p_alice == 1);
        assert(ps->profiles.active_index == 1);
        /* Alice starts clean & isolated from Profile 1 favorites/stats/shortcuts! */
        assert(ps->games[0].is_favorite == false);
        assert(ps->games[0].launch_count == 0);
        assert(ps->games[0].shortcut_key == 0);
        ps->games[1].is_favorite = true;
        ps->games[1].shortcut_key = 'C';
        ps->settings.layout = NL_LAYOUT_4X4;
        ps->display_mode = NL_DISPLAY_LIGHT; /* Global shared visual mode */

        int p_bob = nl_profile_create(ps, "Bob", 34);
        assert(p_bob == 2);
        assert(ps->display_mode == NL_DISPLAY_LIGHT); /* Global shared kept! */
        assert(ps->games[1].is_favorite == false);

        /* #R15: '0' and '-' are reassigned to page navigation (NOT profile switching).
         * Verify switching profiles cleanly via nl_profile_switch_to preserves per-profile favorites/shortcuts/layout. */
        assert(ps->profiles.active_index == 2);
        assert(nl_handle_char_input(ps, '0') == false); /* Single page: does not switch profile */
        assert(ps->profiles.active_index == 2);
        assert(nl_handle_char_input(ps, '-') == false); /* Single page: does not switch profile */
        assert(ps->profiles.active_index == 2);
        assert(nl_profile_switch_to(ps, 1) == true);    /* Switch to Alice */
        assert(ps->profiles.active_index == 1);
        assert(ps->games[1].is_favorite == true);
        assert(ps->games[1].shortcut_key == 'C');
        assert(ps->settings.layout == NL_LAYOUT_4X4);
        assert(nl_profile_switch_to(ps, 0) == true);    /* Switch to Profil 1 */
        assert(ps->profiles.active_index == 0);
        assert(ps->games[0].is_favorite == true);
        assert(ps->games[0].launch_count == 7);
        assert(ps->games[0].shortcut_key == 'A');
        assert(ps->settings.layout == NL_LAYOUT_2X2);

        /* During inline search, '0' and '-' type into search query */
        assert(nl_handle_char_input(ps, '2') == true);
        assert(ps->sui.search_active == true);
        assert(nl_handle_char_input(ps, '0') == true);
        assert(ps->profiles.active_index == 0);
        assert(strcmp(ps->sui.search_query, "20") == 0);
        assert(nl_handle_key(ps, NL_KEY_ESC) == true);
        assert(ps->sui.search_active == false);

        /* Quick Selector open via '.', browse, switch, and '+' flow without edit/delete */
        assert(nl_handle_char_input(ps, '.') == true);
        assert(ps->sui.qp_open == true);
        for (int f = 0; f < 16; f++) nl_anim_step(ps);
        assert(ps->anim.qp_open_fp == 256);

        /* Render preview for Quick Selector in Light vs Dark & inspect ring independence from accent_color */
        ps->display_mode = NL_DISPLAY_DARK;
        ps->accent_color = NL_ACCENT_BLUE;
        nl_render_frame(ps, fb);
        /* Top-left header avatar pixels vs active profile (0 vs 1) */
        uint16_t hdr_p0 = fb[12 * NL_SCREEN_W + 17];
        /* Ring pixels at minimalist QP cy=120 must be independent of accent_color */
        int ring_px_x = (ps->anim.qp_glow_x_fp >> 4) - 19;
        uint16_t ring_blue = fb[120 * NL_SCREEN_W + ring_px_x];
        ps->accent_color = NL_ACCENT_RED;
        nl_render_frame(ps, fb);
        uint16_t ring_red = fb[120 * NL_SCREEN_W + ring_px_x];
        assert(ring_blue == ring_red);

        ps->display_mode = NL_DISPLAY_LIGHT;
        nl_render_frame(ps, fb);
        uint16_t ring_light = fb[120 * NL_SCREEN_W + ring_px_x];
        assert(ring_light != ring_blue);

        /* Navigate right -> Alice in Quick Selector and confirm */
        assert(nl_handle_key(ps, NL_KEY_RIGHT) == true);
        assert(ps->sui.qp_cursor == 1);
        assert(nl_handle_key(ps, NL_KEY_EXE) == true);
        assert(ps->sui.qp_open == false);
        assert(ps->profiles.active_index == 1);
        for (int f = 0; f < 16; f++) nl_anim_step(ps);
        ps->display_mode = NL_DISPLAY_DARK;
        nl_render_frame(ps, fb);
        assert(fb[12 * NL_SCREEN_W + 17] != hdr_p0); /* Header avatar visibly updated! */

        /* Rename & change avatar frees old icon for reuse */
        assert(nl_profile_rename(ps, 1, "Alicia") == true);
        assert(strcmp(ps->profiles.items[1].name, "Alicia") == 0);
        assert(nl_profile_set_avatar(ps, 1, 25) == true);
        assert(nl_avatar_find_owner(ps, 18, -1) == -1); /* Old icon 18 freed! */
        assert(nl_profile_set_avatar(ps, 2, 18) == true);

        /* Render Settings Profils screens for verification */
        ps->sui.screen = NL_UI_SETTINGS_PROFILES;
        ps->sui.prof_mode = NL_PROF_MODE_LIST;
        ps->sui.prof_cursor = 2; /* Row 0=Padlock toggle, Row 1=Create, Row 2=First profile */
        nl_render_frame(ps, fb);

        ps->sui.prof_mode = NL_PROF_MODE_EDIT_AVATAR;
        ps->sui.prof_detail_idx = 1;
        ps->sui.prof_avatar_cat = 1;
        ps->sui.prof_avatar_cursor = 2;
        nl_render_frame(ps, fb);

        /* Persistence save/reload */
        const char *prof_tmp = "/tmp/nl_test_profiles.tns";
        assert(nl_save_profiles(ps, prof_tmp) == true);
        nl_state_t *pr = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(pr != NULL);
        nl_init_state(pr);
        pr->total_games = 3;
        for (int g = 0; g < 3; g++) pr->games[g] = ps->games[g];
        assert(nl_load_profiles(pr, prof_tmp) == true);
        assert(pr->profiles.count == 3);
        assert(pr->profiles.active_index == 1);
        assert(strcmp(pr->profiles.items[1].name, "Alicia") == 0);
        assert(pr->profiles.items[1].avatar_id == 25);
        assert(pr->games[1].is_favorite == true);
        assert(pr->games[1].shortcut_key == 'C');
        remove(prof_tmp);

        /* Protect primary profile from deletion, delete secondary active profile cleanly without touching .tns file */
        assert(nl_profile_delete(pr, 0) == false);
        assert(nl_profile_delete(pr, 1) == true);
        assert(pr->profiles.count == 2);
        assert(pr->profiles.active_index == 0);
        assert(nl_file_exists("/tmp/nl_p_game1.tns") == true);
        remove("/tmp/nl_p_game1.tns");

        /* Test 43/43 full avatar cap */
        for (int a = 0; a < NL_AVATAR_COUNT; a++) {
            if (nl_avatar_find_owner(pr, a, -1) < 0) {
                assert(nl_profile_create(pr, "UserX", a) >= 0);
            }
        }
        assert(pr->profiles.count == NL_MAX_PROFILES);
        assert(nl_avatar_first_free(pr) == -1);
        assert(nl_profile_create(pr, "Extra", 0) == -1);

        nl_free_icons(pr);
        nl_free_icons(ps);
        free(pr);
        free(ps);
        printf("[PASS] Multi-profile system, 43 exclusive avatars, Quick Selector, 0/-/., isolation & persistence verified.\n");
    }

    /* 25. Library Screen Occupancy (320x240), Incomplete Row Centering & 4-Side Independent .ICO Margin Trimming */
    {
        /* 25a. Verify adaptive 4x4 layout for 9, 10, 12, and 16 games (with & without names) */
        nl_layout_metrics_t m9_no, m9_nm, m10_no, m12_no, m16_no, m16_nm;
        nl_get_layout_metrics_for_count(NL_LAYOUT_4X4, false, true, 9, &m9_no);
        nl_get_layout_metrics_for_count(NL_LAYOUT_4X4, true, true, 9, &m9_nm);
        nl_get_layout_metrics_for_count(NL_LAYOUT_4X4, false, true, 10, &m10_no);
        nl_get_layout_metrics_for_count(NL_LAYOUT_4X4, false, true, 12, &m12_no);
        nl_get_layout_metrics_for_count(NL_LAYOUT_4X4, false, true, 16, &m16_no);
        nl_get_layout_metrics_for_count(NL_LAYOUT_4X4, true, true, 16, &m16_nm);

        /* 9, 10, 12 games in 4x4 have 3 active rows -> cards scale up to 56x56 (no names) / 49x49 (with names) */
        assert(m9_no.card_w == 56 && m9_no.card_h == 56);
        assert(m10_no.card_w == 56 && m12_no.card_w == 56);
        assert(m9_nm.card_w == 49 && m9_nm.card_h == 49);
        assert(m16_no.card_w == 42 && m16_no.card_h == 42);
        assert(m16_nm.card_w == 34 && m16_nm.card_h == 34);

        /* Verify horizontal centering of incomplete last row in 4x4 for 9, 10, 11, and 12 games */
        int x9_last = 0, y9_last = 0;
        nl_compute_grid_slot_pos(&m9_no, 9, 8, &x9_last, &y9_last);
        assert(x9_last == (NL_SCREEN_W - m9_no.card_w) / 2); /* 1 item on row 2 -> strictly centered at x=132 */

        int x10_0 = 0, y10_0 = 0, x10_1 = 0, y10_1 = 0;
        nl_compute_grid_slot_pos(&m10_no, 10, 8, &x10_0, &y10_0);
        nl_compute_grid_slot_pos(&m10_no, 10, 9, &x10_1, &y10_1);
        int row10_w = 2 * m10_no.card_w + m10_no.gap_x;
        assert(x10_0 == (NL_SCREEN_W - row10_w) / 2);
        assert(x10_1 == x10_0 + m10_no.card_w + m10_no.gap_x);
        assert(NL_SCREEN_W - (x10_1 + m10_no.card_w) == x10_0); /* Equal left and right margins */

        int x11_0 = 0, y11_0 = 0, x11_2 = 0, y11_2 = 0;
        nl_compute_grid_slot_pos(&m10_no, 11, 8, &x11_0, &y11_0);
        nl_compute_grid_slot_pos(&m10_no, 11, 10, &x11_2, &y11_2);
        int row11_w = 3 * m10_no.card_w + 2 * m10_no.gap_x;
        assert(x11_0 == (NL_SCREEN_W - row11_w) / 2);
        assert(NL_SCREEN_W - (x11_2 + m10_no.card_w) == x11_0);

        /* Full row of 4 (12 games) aligns with m12_no.margin_x */
        int x12_0 = 0, y12_0 = 0;
        nl_compute_grid_slot_pos(&m12_no, 12, 8, &x12_0, &y12_0);
        assert(x12_0 == m12_no.margin_x);

        /* Verify incomplete row centering in 3x3 (7 and 8 games), 3x2 (4 and 5 games), and 2x2 (3 games) */
        nl_layout_metrics_t m3x3_7, m3x2_4, m2x2_3;
        nl_get_layout_metrics_for_count(NL_LAYOUT_3X3, false, true, 7, &m3x3_7);
        int x7_last = 0, y7_last = 0;
        nl_compute_grid_slot_pos(&m3x3_7, 7, 6, &x7_last, &y7_last);
        assert(x7_last == (NL_SCREEN_W - m3x3_7.card_w) / 2);

        nl_get_layout_metrics_for_count(NL_LAYOUT_3X2, false, true, 4, &m3x2_4);
        int x4_last = 0, y4_last = 0;
        nl_compute_grid_slot_pos(&m3x2_4, 4, 3, &x4_last, &y4_last);
        assert(x4_last == (NL_SCREEN_W - m3x2_4.card_w) / 2);

        nl_get_layout_metrics_for_count(NL_LAYOUT_2X2, false, true, 3, &m2x2_3);
        int x3_last = 0, y3_last = 0;
        nl_compute_grid_slot_pos(&m2x2_3, 3, 2, &x3_last, &y3_last);
        assert(x3_last == (NL_SCREEN_W - m2x2_3.card_w) / 2);

        /* 25b. Verify 4-side independent transparent margin trimming on asymmetric .ico */
        {
            const int pw = 64, ph = 64;
            const uint32_t raw_row = 1U + (uint32_t)pw * 4U;
            const uint32_t raw_len = raw_row * (uint32_t)ph;
            uint8_t *raw = (uint8_t *)calloc(1, raw_len);
            assert(raw != NULL);
            /* Opaque 44x44 icon shifted asymmetrically: x in [4..47] (left pad=4, right pad=16), y in [14..57] (top pad=14, bottom pad=6) */
            for (int y = 0; y < ph; y++) {
                uint8_t *row = raw + (uint32_t)y * raw_row;
                row[0] = 0;
                if (y >= 14 && y < 58) {
                    for (int x = 4; x < 48; x++) {
                        uint8_t *px = row + 1 + x * 4;
                        px[0] = 240; px[1] = 95; px[2] = 35; px[3] = 255;
                    }
                }
            }
            uint32_t a1 = 1U, a2 = 0U;
            for (uint32_t i = 0; i < raw_len; i++) {
                a1 = (a1 + raw[i]) % 65521U;
                a2 = (a2 + a1) % 65521U;
            }
            uint32_t adler = (a2 << 16) | a1;
            uint32_t zlen = 2U + 5U + raw_len + 4U;
            uint8_t *zbuf = (uint8_t *)malloc(zlen);
            assert(zbuf != NULL);
            zbuf[0] = 0x78; zbuf[1] = 0x01; zbuf[2] = 0x01;
            zbuf[3] = (uint8_t)(raw_len & 0xFFU);
            zbuf[4] = (uint8_t)((raw_len >> 8) & 0xFFU);
            zbuf[5] = (uint8_t)(~raw_len & 0xFFU);
            zbuf[6] = (uint8_t)((~raw_len >> 8) & 0xFFU);
            memcpy(zbuf + 7, raw, raw_len);
            zbuf[7 + raw_len + 0] = (uint8_t)((adler >> 24) & 0xFFU);
            zbuf[7 + raw_len + 1] = (uint8_t)((adler >> 16) & 0xFFU);
            zbuf[7 + raw_len + 2] = (uint8_t)((adler >> 8) & 0xFFU);
            zbuf[7 + raw_len + 3] = (uint8_t)(adler & 0xFFU);
            free(raw);

            uint32_t png_sz = 8U + (12U + 13U) + (12U + zlen) + 12U;
            uint8_t *ico_png = (uint8_t *)calloc(1, 22U + png_sz);
            assert(ico_png != NULL);
            ico_png[2] = 1; ico_png[4] = 1;
            ico_png[6] = (uint8_t)pw; ico_png[7] = (uint8_t)ph;
            ico_png[10] = 1; ico_png[12] = 32;
            write_u32_le(ico_png + 14, png_sz);
            write_u32_le(ico_png + 18, 22U);

            uint8_t *p = ico_png + 22;
            static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
            memcpy(p, png_sig, 8); p += 8;
            p[3] = 13; p[4] = 'I'; p[5] = 'H'; p[6] = 'D'; p[7] = 'R';
            p[11] = (uint8_t)pw; p[15] = (uint8_t)ph; p[16] = 8; p[17] = 6;
            p += 25;
            p[0] = (uint8_t)((zlen >> 24) & 0xFFU); p[1] = (uint8_t)((zlen >> 16) & 0xFFU);
            p[2] = (uint8_t)((zlen >> 8) & 0xFFU);  p[3] = (uint8_t)(zlen & 0xFFU);
            p[4] = 'I'; p[5] = 'D'; p[6] = 'A'; p[7] = 'T';
            memcpy(p + 8, zbuf, zlen);
            p += 12U + zlen;
            free(zbuf);
            p[4] = 'I'; p[5] = 'E'; p[6] = 'N'; p[7] = 'D';

            const char *asym_ico_path = "/tmp/nlauncher_test_fs/ndless/icons/asym_game.ico.tns";
            FILE *fp = fopen(asym_ico_path, "wb");
            assert(fp != NULL);
            fwrite(ico_png, 1, 22U + png_sz, fp);
            fclose(fp);
            free(ico_png);

            nl_game_t asym_game;
            memset(&asym_game, 0, sizeof(asym_game));
            strncpy(asym_game.title, "AsymIcon", sizeof(asym_game.title) - 1);
            strncpy(asym_game.filename, "asym_game.tns", sizeof(asym_game.filename) - 1);
            strncpy(asym_game.path, "/tmp/nlauncher_test_fs/games/asym_game.tns", sizeof(asym_game.path) - 1);
            assert(nl_load_game_icon(&asym_game, "/tmp/nlauncher_test_fs/ndless/covers") == true);
            assert(asym_game.icon_source == NL_ICON_ICO);
            assert(asym_game.icon_w == 46);
            assert(asym_game.icon_h == 46);
            assert(asym_game.icon_pixels != NULL);
            free(asym_game.icon_pixels);
            free(asym_game.icon_alpha);
            remove(asym_ico_path);
        }

        /* 25c. Render library frames for 9, 10, 12, and 16 games (with and without names) and verify non-empty centered cards */
        nl_state_t *ls = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ls != NULL);
        nl_init_state(ls);
        ls->total_games = 16;
        for (int g = 0; g < 16; g++) {
            snprintf(ls->games[g].title, sizeof(ls->games[g].title), "Game %d", g + 1);
            snprintf(ls->games[g].filename, sizeof(ls->games[g].filename), "game%d.tns", g + 1);
            snprintf(ls->games[g].path, sizeof(ls->games[g].path), "/documents/games/game%d.tns", g + 1);
            ls->games[g].icon_source = NL_ICON_ICO;
            ls->games[g].icon_w = 56;
            ls->games[g].icon_h = 56;
            ls->games[g].icon_pixels = (uint16_t *)malloc(56 * 56 * sizeof(uint16_t));
            ls->games[g].icon_alpha  = (uint8_t *)malloc(56 * 56);
            assert(ls->games[g].icon_pixels != NULL && ls->games[g].icon_alpha != NULL);
            for (int p = 0; p < 56 * 56; p++) {
                int py = p / 56, px = p % 56;
                uint8_t r = (uint8_t)(40 + ((g * 37 + px * 3) & 0x7F));
                uint8_t gr = (uint8_t)(50 + ((g * 53 + py * 3) & 0x7F));
                uint8_t b = (uint8_t)(90 + ((g * 29 + (px ^ py) * 2) & 0x7F));
                ls->games[g].icon_pixels[p] = (uint16_t)(((r >> 3) << 11) | ((gr >> 2) << 5) | (b >> 3));
                ls->games[g].icon_alpha[p] = 255;
            }
        }
        ls->settings.layout = NL_LAYOUT_4X4;
        ls->settings.show_shortcuts = true;

        int counts_to_check[4] = { 9, 10, 12, 16 };
        for (int ci = 0; ci < 4; ci++) {
            int cnt = counts_to_check[ci];
            ls->total_games = cnt;
            for (int sn = 0; sn <= 1; sn++) {
                ls->settings.show_names = (sn != 0);
                nl_rebuild_filter(ls);
                for (int step = 0; step < 24; step++) nl_anim_step(ls);
                nl_render_frame(ls, fb);
                /* Verify that the last game on the page is rendered at its centered slot position */
                nl_layout_metrics_t mc;
                nl_get_layout_metrics_for_count(ls->settings.layout, ls->settings.show_names, ls->settings.show_shortcuts, cnt, &mc);
                int lx = 0, ly = 0;
                nl_compute_grid_slot_pos(&mc, cnt, cnt - 1, &lx, &ly);
                int cx = lx + mc.card_w / 2;
                int cy = ly + mc.card_h / 2;
                assert(fb[cy * NL_SCREEN_W + cx] != fb[30 * NL_SCREEN_W + 2]);
            }
        }
        ls->total_games = 16;
        nl_free_icons(ls);
        free(ls);
        printf("[PASS] Adaptive 320x240 screen occupancy, incomplete row centering & 4-side .ICO margin trimming verified.\n");
    }

    /* 26. Multi-Profile Animation Overhaul: 4-Stage Switch, '+' Ghost Zoom, Direct Avatar Creation & Birth Confirmation */
    {
        nl_state_t *pa = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(pa != NULL);
        nl_init_state(pa);
        pa->total_games = 4;
        for (int g = 0; g < 4; g++) {
            snprintf(pa->games[g].title, sizeof(pa->games[g].title), "AnimGame %d", g + 1);
            snprintf(pa->games[g].filename, sizeof(pa->games[g].filename), "animgame%d.tns", g + 1);
            snprintf(pa->games[g].path, sizeof(pa->games[g].path), "/documents/games/animgame%d.tns", g + 1);
        }
        nl_rebuild_filter(pa);
        assert(nl_profile_create(pa, "Maxime", 10) == 1);
        assert(nl_profile_switch_to(pa, 0) == true);
        assert(pa->profiles.count == 2);
        assert(pa->profiles.active_index == 0);

        /* 26a. Open Quick Selector ('.'), verify cascade entrance & navigation wake/bounce/plus rotation */
        assert(nl_handle_char_input(pa, '.') == true);
        assert(pa->sui.qp_open == true);
        assert(pa->anim.qp_open_fp < 256);
        for (int f = 0; f < 18; f++) {
            nl_anim_step(pa);
            nl_render_frame(pa, fb);
        }
        assert(pa->anim.qp_open_fp == 256);
        assert(pa->anim.qp_slot_scale_fp[0] > pa->anim.qp_slot_scale_fp[1]);

        /* Move right to Maxime (slot 1): verify glow wake trails behind glow head & spring bounce fires */
        assert(nl_handle_key(pa, NL_KEY_RIGHT) == true);
        assert(pa->sui.qp_cursor == 1);
        assert(pa->anim.qp_sel_bounce_fp > 0);
        nl_anim_step(pa);
        assert(pa->anim.qp_trail_x_fp < pa->anim.qp_glow_x_fp);
        nl_render_frame(pa, fb);

        /* Move right to '+' button (slot 2): verify '+' focus animation activates */
        assert(nl_handle_key(pa, NL_KEY_RIGHT) == true);
        assert(pa->sui.qp_cursor == 2);
        for (int f = 0; f < 10; f++) {
            nl_anim_step(pa);
            nl_render_frame(pa, fb);
        }
        assert(pa->anim.qp_plus_focus_fp == 256);

        /* 26b. #R15: Press EXE on '+' button: controlled enlargement animation -> compact floating Avatar Picker modal */
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->sui.screen == NL_UI_LIBRARY);
        assert(pa->sui.qp_pick_pending == true);
        assert(pa->anim.prof_plus_active == true);
        assert(pa->anim.prof_plus_tick == 0);
        assert(pa->anim.prof_plus_origin_x > 160);
        assert(pa->anim.prof_plus_origin_y == 120);

        /* Step through controlled '+' creation transition frames -> seamlessly opens floating Avatar Picker modal */
        for (int f = 0; f < pa->anim.prof_plus_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_plus_active == false);
        assert(pa->sui.qp_open == false);
        assert(pa->sui.qp_pick_open == true);
        for (int f = 0; f < 18; f++) {
            nl_anim_step(pa);
            nl_render_frame(pa, fb);
        }
        assert(pa->anim.qp_pick_fp == 256);

        /* 26c. Validate an avatar with EXE in floating modal: creates "Profil 3" directly, triggers birth confirmation, returns to Quick Selector */
        assert(nl_handle_key(pa, NL_KEY_RIGHT) == true);
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->profiles.count == 3);
        assert(strcmp(pa->profiles.items[2].name, "Profil 3") == 0);
        assert(pa->sui.screen == NL_UI_LIBRARY);
        assert(pa->sui.qp_pick_open == false);
        assert(pa->sui.qp_open == true);
        assert(pa->sui.qp_cursor == 2);
        assert(pa->anim.prof_birth_active == true);
        for (int f = 0; f < pa->anim.prof_birth_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_birth_active == false);

        /* 26d. Select Maxime (slot 1) with LEFT + EXE: triggers clean Profile Switch animation at cy=120 */
        assert(nl_handle_key(pa, NL_KEY_LEFT) == true);
        assert(pa->sui.qp_cursor == 1);
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->sui.qp_open == false);
        assert(pa->profiles.active_index == 1);
        assert(pa->anim.prof_switch_active == true);
        assert(pa->anim.prof_switch_avatar_id == pa->profiles.items[1].avatar_id);
        assert(pa->anim.prof_switch_origin_y == 120);

        /* Render every frame of the switch transition */
        for (int f = 0; f < pa->anim.prof_switch_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_switch_active == false);

        /* 26e. Rapid successive profile switches via Quick Selector mid-animation stay coherent without artifacts */
        assert(nl_handle_char_input(pa, '.') == true);
        assert(nl_handle_key(pa, NL_KEY_RIGHT) == true);
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->profiles.active_index == 2);
        assert(pa->anim.prof_switch_active == true);
        nl_anim_step(pa);
        nl_render_frame(pa, fb);
        /* Immediately switch again before previous switch animation finishes */
        assert(nl_handle_char_input(pa, '.') == true);
        assert(nl_handle_key(pa, NL_KEY_LEFT) == true);
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->profiles.active_index == 1);
        assert(pa->anim.prof_switch_active == true);
        assert(pa->anim.prof_switch_tick == 0);
        assert(pa->anim.prof_switch_avatar_id == pa->profiles.items[1].avatar_id);
        for (int f = 0; f < pa->anim.prof_switch_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_switch_active == false);

        nl_free_icons(pa);
        free(pa);
        printf("[PASS] 4-stage Profile Switch animation, '+' Ghost Creation, Direct Avatar Selection & Birth Confirmation verified.\n");
    }

    /* 27. Mode 1 (Pave tactile) vs Mode 2 (Souris) & 3 Cursor Styles (Fleche, Cercle, Pointeur) */
    {
        nl_state_t *cs = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(cs != NULL);
        nl_init_state(cs);
        nl_scan_directory(cs, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
        nl_rebuild_filter(cs);
        assert(cs->filtered_count == 8);

        /* 27a. Default Mode 1 (Pave tactile): NO graphical cursor rendered */
        assert(cs->settings.touchpad_mode == NL_TP_MODE_TOUCHPAD);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_ARROW);
        assert(cs->settings.cursor_speed == NL_CURSOR_SPEED_DEFAULT);
        assert(nl_mouse_is_active(cs) == false);
        assert(nl_cursor_is_visible(cs) == false);
        assert(cs->selected_pos == 0);
        assert(nl_handle_key(cs, NL_KEY_RIGHT) == true);
        assert(cs->selected_pos == 1);
        assert(cs->launch_requested == false);

        /* Open Settings -> Navigation tactile (Tab 0) & switch Row 0 from Pave tactile -> Souris */
        nl_handle_key(cs, NL_KEY_SETTINGS);
        cs->sui.menu_cursor = NL_SET_MENU_TOUCHPAD;
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->sui.screen == NL_UI_SETTINGS_TOUCHPAD);
        assert(cs->sui.tp_tab == 0);
        assert(cs->sui.tp_cursor == 0);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_mode == NL_TP_MODE_MOUSE);
        assert(nl_mouse_is_active(cs) == true);
        assert(nl_cursor_is_visible(cs) == true);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_ARROW);

        /* Navigate Tab 0 rows: 0=Mode, 1=Glissement, 2=Sensibilite, 3=Vitesse souris, 4=Apparence */
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 1);
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 2);
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 3);
        nl_handle_key(cs, NL_KEY_RIGHT); /* Raise cursor_speed 2 -> 3 */
        assert(cs->settings.cursor_speed == 3);
        for (int s = 0; s < 16; s++) nl_anim_step(cs);
        assert(cs->anim.tp_speed_slider_pos_fp == 3 * 256);

        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 4);
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 5);
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 6);
        nl_handle_key(cs, NL_KEY_EXE); /* Open Tab 1: Personnaliser le curseur */
        assert(cs->sui.tp_tab == 1);
        assert(cs->sui.tp_style_cursor == 0); /* Card 0 = Fleche */

        /* Select Card 1: Cercle */
        nl_handle_key(cs, NL_KEY_RIGHT);
        assert(cs->sui.tp_style_cursor == 1);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_CIRCLE);
        nl_render_frame(cs, fb);

        /* Select Card 2: Pointeur (animated gradient pointer hand) */
        nl_handle_key(cs, NL_KEY_RIGHT);
        assert(cs->sui.tp_style_cursor == 2);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER);
        for (int f = 0; f < 6; f++) {
            nl_anim_step(cs);
            nl_render_frame(cs, fb);
        }

        /* Select Card 0: Fleche */
        nl_handle_key(cs, NL_KEY_LEFT);
        nl_handle_key(cs, NL_KEY_LEFT);
        assert(cs->sui.tp_style_cursor == 0);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_ARROW);

        /* Render all 3 styles (ARROW, CIRCLE, POINTER) across all 3 display modes & 4 accent colors */
        for (int dm = 0; dm < 3; dm++) {
            cs->display_mode = (nl_display_mode_t)dm;
            for (int ac = 0; ac < 4; ac++) {
                cs->accent_color = (nl_accent_color_t)ac;
                cs->settings.touchpad_cursor_style = NL_CURSOR_STYLE_ARROW;
                nl_anim_step(cs);
                nl_render_frame(cs, fb);
                cs->settings.touchpad_cursor_style = NL_CURSOR_STYLE_CIRCLE;
                nl_anim_step(cs);
                nl_render_frame(cs, fb);
                cs->settings.touchpad_cursor_style = NL_CURSOR_STYLE_POINTER;
                nl_anim_step(cs);
                nl_render_frame(cs, fb);
            }
        }
        cs->display_mode = NL_DISPLAY_DARK;
        cs->accent_color = NL_ACCENT_BLUE;
        cs->settings.touchpad_cursor_style = NL_CURSOR_STYLE_POINTER;

        /* Persistence of touchpad_mode=mouse, cursor_speed=3, and touchpad_cursor_style=pointer */
        assert(nl_save_config(cs, "/tmp/nl_test_cursor.cfg.tns") == true);
        nl_state_t *reloaded_cs = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(reloaded_cs != NULL);
        nl_init_state(reloaded_cs);
        assert(nl_load_config(reloaded_cs, "/tmp/nl_test_cursor.cfg.tns") == true);
        assert(reloaded_cs->settings.touchpad_mode == NL_TP_MODE_MOUSE);
        assert(reloaded_cs->settings.cursor_speed == 3);
        assert(reloaded_cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER);
        assert(nl_mouse_is_active(reloaded_cs) == true);
        assert(nl_cursor_is_visible(reloaded_cs) == true);
        nl_free_icons(reloaded_cs);
        nl_free_icons(cs);
        free(reloaded_cs);
        free(cs);
        printf("[PASS] Mode Souris settings, 3 PNG cursor styles (Fleche/Cercle/Pointeur) & config persistence verified.\n");
    }

    /* 28. Complete Mouse Mode Behavior: Free 2D Pointer, Hardware Y-Axis, CTRL Hold, Non-Launch Hover & Universal Clicks */
    {
        nl_state_t *ms = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ms != NULL);
        nl_init_state(ms);
        nl_scan_directory(ms, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
        nl_rebuild_filter(ms);
        assert(ms->filtered_count == 8);

        /* 28a. Mode 1 (Pave tactile) pixel-exact zero-cursor invariant */
        ms->settings.touchpad_mode = NL_TP_MODE_TOUCHPAD;
        ms->sui.ctrl_mouse_active = false;
        ms->anim.cur_opacity_fp = 0;
        nl_render_frame(ms, fb);
        uint16_t *fb_cmp = (uint16_t *)malloc(NL_FB_BYTES);
        assert(fb_cmp != NULL);
        memcpy(fb_cmp, fb, NL_FB_BYTES);

        /* Holding CTRL temporarily enables cursor & mouse mode without mutating touchpad_mode or config_dirty */
        ms->sui.config_dirty = false;
        ms->sui.ctrl_mouse_active = true;
        assert(nl_mouse_is_active(ms) == true);
        assert(nl_cursor_is_visible(ms) == true);
        assert(ms->settings.touchpad_mode == NL_TP_MODE_TOUCHPAD);
        assert(ms->sui.config_dirty == false);
        nl_mouse_move_to(ms, 160, 120);
        nl_render_frame(ms, fb);
        int diff_ctrl = 0;
        for (int i = 0; i < NL_FB_PIXELS; i++) {
            if (fb[i] != fb_cmp[i]) diff_ctrl++;
        }
        assert(diff_ctrl > 30); /* Graphical cursor is genuinely drawn when CTRL held */

        /* Releasing CTRL immediately hides cursor (opacity = 0) & returns framebuffer to clean Mode 1 */
        ms->sui.ctrl_mouse_active = false;
        nl_anim_step(ms);
        assert(nl_mouse_is_active(ms) == false);
        assert(nl_cursor_is_visible(ms) == false);
        assert(ms->anim.cur_opacity_fp == 0);
        nl_render_frame(ms, fb);
        assert(memcmp(fb, fb_cmp, NL_FB_BYTES) == 0);
        free(fb_cmp);

        /* 28b. Enable persistent Mode 2 (Souris) & feed hardware touchpad deltas (inverted Y hardware axis) */
        ms->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        ms->settings.touchpad_cursor_style = NL_CURSOR_STYLE_ARROW;
        nl_mouse_move_to(ms, 160, 120);
        assert(nl_mouse_feed_touchpad(ms, true, 1000, 1000, 2328U, 1691U) == false); /* Contact anchor */
        /* Finger moves right (+250) and down on pad (raw_y 1000 -> 800 on hardware where y=0 is bottom) */
        int px_before = ms->anim.mouse_x_fp >> 4;
        int py_before = ms->anim.mouse_y_fp >> 4;
        assert(nl_mouse_feed_touchpad(ms, true, 1250, 800, 2328U, 1691U) == true);
        assert((ms->anim.mouse_x_fp >> 4) > px_before);
        assert((ms->anim.mouse_y_fp >> 4) > py_before); /* Downward touchpad motion moves screen pointer DOWN */

        /* Verify cursor_speed=4 scales pointer displacement faster than cursor_speed=0 */
        nl_mouse_move_to(ms, 160, 120);
        ms->anim.mouse_last_contact = false;
        ms->settings.cursor_speed = 0;
        (void)nl_mouse_feed_touchpad(ms, true, 1000, 1000, 2328U, 1691U);
        (void)nl_mouse_feed_touchpad(ms, true, 1300, 1000, 2328U, 1691U);
        int delta_slow = (ms->anim.mouse_x_fp >> 4) - 160;

        nl_mouse_move_to(ms, 160, 120);
        ms->anim.mouse_last_contact = false;
        ms->settings.cursor_speed = 4;
        (void)nl_mouse_feed_touchpad(ms, true, 1000, 1000, 2328U, 1691U);
        (void)nl_mouse_feed_touchpad(ms, true, 1300, 1000, 2328U, 1691U);
        int delta_fast = (ms->anim.mouse_x_fp >> 4) - 160;
        assert(delta_fast > delta_slow * 2);
        ms->settings.cursor_speed = NL_CURSOR_SPEED_DEFAULT;

        /* Idle attenuation leaves cursor visible (>= 112) in Mode 2 */
        for (int i = 0; i < 110; i++) nl_anim_step(ms);
        assert(nl_cursor_is_visible(ms) == true);
        assert(ms->anim.cur_opacity_fp == 112);

        /* 28c. Hover over Game Card 0 NEVER launches; clicking Card 1 selects & launches Card 1 */
        ms->sui.screen = NL_UI_LIBRARY;
        nl_layout_metrics_t lm;
        nl_get_layout_metrics_for_count(ms->settings.layout, ms->settings.show_names,
                                        ms->settings.show_shortcuts, ms->filtered_count, &lm);
        int c0x = 0, c0y = 0, c1x = 0, c1y = 0;
        nl_compute_grid_slot_pos(&lm, ms->filtered_count, 0, &c0x, &c0y);
        nl_compute_grid_slot_pos(&lm, ms->filtered_count, 1, &c1x, &c1y);
        ms->launch_requested = false;
        nl_mouse_move_to(ms, c0x + lm.card_w / 2, c0y + lm.card_h / 2);
        assert(ms->sui.hover_valid == true);
        assert(ms->sui.hover_kind == NL_HOVER_GAME_CARD);
        assert(ms->sui.hover_index == 0);
        assert(ms->launch_requested == false);

        nl_mouse_move_to(ms, c1x + lm.card_w / 2, c1y + lm.card_h / 2);
        assert(ms->sui.hover_kind == NL_HOVER_GAME_CARD && ms->sui.hover_index == 1);
        assert(nl_mouse_click(ms) == true);
        assert(ms->launch_requested == true);
        assert(ms->launch_game_idx == ms->filtered_indices[1]);
        ms->launch_requested = false;
        ms->launch_game_idx = -1;
        ms->anim.launch_anim_active = false;
        ms->anim.launch_holding = false;
        ms->anim.launch_tick = 0;

        /* 28d. Top-Bar Favorite Star Button (x=156, y=12), Categories (FAV/TOUS), Mode-2 Pagination '<'/'>', QP Overlay */
        const nl_theme_t *gth = nl_get_theme(ms->display_mode, ms->accent_color);
        nl_render_frame(ms, fb);
        /* Ensure x=250..290, y=7..18 in Library top bar still strictly matches bg_top_bar when mouse parked at x=156 */
        nl_mouse_move_to(ms, 156, 12);
        assert(ms->sui.hover_kind == NL_HOVER_FAV_BTN);
        bool prev_fav = ms->games[ms->filtered_indices[ms->selected_pos]].is_favorite;
        assert(nl_mouse_click(ms) == true);
        assert(ms->games[ms->filtered_indices[ms->selected_pos]].is_favorite == !prev_fav);

        nl_mouse_move_to(ms, 88, 12); /* FAV pill */
        assert(ms->sui.hover_kind == NL_HOVER_CATEGORY && ms->sui.hover_index == NL_CAT_FAVORITES);
        assert(nl_mouse_click(ms) == true);
        assert(ms->active_category == NL_CAT_FAVORITES);

        nl_mouse_move_to(ms, 50, 12); /* TOUS pill */
        assert(nl_mouse_click(ms) == true);
        assert(ms->active_category == NL_CAT_ALL);
        for (int i = 0; i < 16; i++) nl_anim_step(ms);
        nl_render_frame(ms, fb);

        /* Pagination '>' arrow click advances page 0 -> page 1 */
        assert(ms->total_pages >= 2 && ms->current_page == 0);
        nl_mouse_move_to(ms, 178, lm.pagination_y + 3);
        assert(ms->sui.hover_kind == NL_HOVER_PAGE_NEXT);
        assert(nl_mouse_click(ms) == true);
        assert(ms->current_page == 1);

        /* Quick Profile Badge click -> QP Overlay -> click slot 0 switches/closes -> outside click closes */
        nl_mouse_move_to(ms, 17, 12);
        assert(ms->sui.hover_kind == NL_HOVER_PROFILE_BADGE);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.qp_open == true);
        nl_mouse_move_to(ms, 300, 12); /* Outside panel -> click closes QP */
        assert(ms->sui.hover_kind == NL_HOVER_QP_OUTSIDE);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.qp_open == false);

        /* 28e. Top-Bar Settings Gear click -> Navigation tactile -> Notch & Card clicks -> ESC pill click */
        nl_mouse_move_to(ms, 302, 12);
        assert(ms->sui.hover_kind == NL_HOVER_SETTINGS_BTN);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.screen == NL_UI_SETTINGS_MAIN);

        nl_mouse_move_to(ms, 120, 102); /* Row 2 = Navigation tactile (Row 1 = Animations) */
        assert(ms->sui.hover_kind == NL_HOVER_SET_ROW && ms->sui.hover_index == NL_SET_MENU_TOUCHPAD);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.screen == NL_UI_SETTINGS_TOUCHPAD);

        /* Click notch 5 (idx 4) on Row 3 Cursor Speed slider (y=170, x=286) */
        nl_mouse_move_to(ms, 34 + 4 * 63, 170);
        assert(ms->sui.hover_kind == NL_HOVER_SET_SLIDER_NOTCH);
        assert(ms->sui.hover_index == 3 && ms->sui.hover_sub == 4);
        assert(nl_mouse_click(ms) == true);
        assert(ms->settings.cursor_speed == 4);

        /* Click Tab 1 ("2. Apparence curseur", x=220, y=36) & Card 2 (Pointeur, x=260, y=100) */
        nl_mouse_move_to(ms, 220, 36);
        assert(ms->sui.hover_kind == NL_HOVER_SET_TAB && ms->sui.hover_index == 1);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.tp_tab == 1);

        nl_mouse_move_to(ms, 260, 100);
        assert(ms->sui.hover_kind == NL_HOVER_SET_STYLE_CARD && ms->sui.hover_index == 2);
        assert(nl_mouse_click(ms) == true);
        assert(ms->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER);

        /* Click top-right ESC Ret. pill: Tab 1 -> Tab 0 -> Settings Main -> Library */
        nl_mouse_move_to(ms, 280, 12);
        assert(ms->sui.hover_kind == NL_HOVER_SET_ESC);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.screen == NL_UI_SETTINGS_TOUCHPAD && ms->sui.tp_tab == 0);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.screen == NL_UI_SETTINGS_MAIN);
        assert(nl_mouse_click(ms) == true);
        assert(ms->sui.screen == NL_UI_LIBRARY);
        (void)gth;

        nl_free_icons(ms);
        free(ms);
        printf("[PASS] Complete Mode Souris (Mode1/Mode2, CTRL hold, hardware Y-axis, non-launch hover & universal clicks) verified.\n");
    }

    /* =====================================================================
     * 29. CENTRALIZED DIAGNOSTICS, 12-STAGE BOOT FORENSICS & FAULT ISOLATION
     *     (E102, E103, E104, E201, E301, E401, E501, E601, E801, E901)
     * ===================================================================== */
    {
        assert(strcmp(nl_resolve_os_version_string(0x1042A580U), "6.2.0.333 (CX II-T)") == 0);
        assert(strcmp(nl_resolve_os_version_string(0x103B1E60U), "4.5.0.1180 (CAS CX)") == 0);

        const char *diag_root = "/tmp/nl_diag_suite_fs";
        assert(system("rm -rf /tmp/nl_diag_suite_fs && mkdir -p /tmp/nl_diag_suite_fs/ndless/covers /tmp/nl_diag_suite_fs/ndless/icons /tmp/nl_diag_suite_fs/games") == 0);

        const char *valid_game = "/tmp/nl_diag_suite_fs/games/Minecraft.tns";
        const char *bad_game   = "/tmp/nl_diag_suite_fs/games/BrokenBin.tns";
        const char *self_exe   = "/tmp/nl_diag_suite_fs/ndless/N-Launcher.tns";
        create_mock_zehn_tns(valid_game, "Minecraft", "Mojang", 1, true, false, 0);
        create_mock_zehn_tns(self_exe, "N-Launcher", "daniel.b", 3, true, true, 0);
        {
            FILE *bfp = fopen(bad_game, "wb");
            assert(bfp != NULL);
            const char junk[] = "CORRUPT_HEADER_NOT_ZEHN";
            fwrite(junk, 1, sizeof(junk) - 1U, bfp);
            fclose(bfp);
        }

        /* Write corrupt config & corrupt profiles to verify backup preservation + E301/E601 */
        const char *bad_cfg  = "/tmp/nl_diag_suite_fs/ndless/nlauncher.cfg.tns";
        const char *bad_prof = "/tmp/nl_diag_suite_fs/ndless/nlauncher.profiles.tns";
        {
            FILE *cfp = fopen(bad_cfg, "wb");
            assert(cfp != NULL);
            fprintf(cfp, "\xFF\xFE\x00CORRUPT_CFG_NO_KEYS");
            fclose(cfp);

            FILE *pfp = fopen(bad_prof, "wb");
            assert(pfp != NULL);
            fprintf(pfp, "garbage_not_profile_header\nprofile_count=abc\n");
            fclose(pfp);
        }

        /* Simulate prior incomplete boot (crash at Stage 10 in Classique) */
        {
            FILE *sfp = fopen("/tmp/nl_diag_suite_fs/ndless/nlauncher.stage.tns", "w");
            assert(sfp != NULL);
            fprintf(sfp, "stage=10\nclean_exit=0\nndless_mode=Classique\nos=6.2.0.333 (CX II-T)\n");
            fclose(sfp);
        }

        nl_state_t *ds = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ds != NULL);
        nl_init_state(ds);
        snprintf(ds->sys.docs_root, sizeof(ds->sys.docs_root), "%s", diag_root);
        snprintf(ds->sys.self_path, sizeof(ds->sys.self_path), "%s", self_exe);
        snprintf(ds->covers_dir, sizeof(ds->covers_dir), "%s/ndless/covers", diag_root);
        snprintf(ds->icons_dir, sizeof(ds->icons_dir), "%s/ndless/icons", diag_root);
        snprintf(ds->profiles_path, sizeof(ds->profiles_path), "%s", bad_prof);
        ds->sys.has_ndless = true;
        ds->sys.hw_is_cx2 = true;
        ds->sys.ndless_persistent = false;
        snprintf(ds->sys.os_version, sizeof(ds->sys.os_version), "6.2.0.333 (CX II-T)");

        nl_diag_init(ds, diag_root);
        nl_diag_read_prev_stage(ds);
        assert(ds->sys.prior_incomplete_boot == true);
        assert(ds->sys.last_crash_stage == NL_STAGE_10_SPLASH);

        /* Record all 12 stages in order */
        for (int st = NL_STAGE_01_ENTRY; st <= NL_STAGE_12_READY; st++) {
            nl_diag_record_stage(ds, (nl_boot_stage_t)st, st == NL_STAGE_12_READY);
            assert(ds->diag.stage_reached[st] == true);
        }

        /* Scan games -> E201 isolates BrokenBin.tns while keeping Minecraft.tns launchable */
        nl_scan_directory(ds, "/tmp/nl_diag_suite_fs/games", self_exe, 3);
        assert(ds->total_games == 1);
        assert(strcmp(ds->games[0].title, "Minecraft") == 0);

        /* Missing icon & screenshot -> E102 + E104, game stays launchable */
        assert(nl_load_game_icon_diag(ds, &ds->games[0], ds->covers_dir) == false);
        assert(nl_load_game_screenshot_diag(ds, &ds->games[0], ds->covers_dir) == false);
        nl_rebuild_filter(ds);
        ds->sui.screen = NL_UI_LIBRARY;
        ds->selected_pos = 0;
        assert(nl_handle_key(ds, NL_KEY_EXE) == true);
        assert(ds->launch_requested == true && ds->launch_game_idx == 0);
        ds->launch_requested = false;
        ds->launch_game_idx = -1;

        /* Corrupt .ico.tns -> E103, NL_ICON_NONE */
        {
            FILE *ifp = fopen("/tmp/nl_diag_suite_fs/ndless/icons/Minecraft.ico.tns", "wb");
            assert(ifp != NULL);
            const char bad_ico[] = "NOT_NL_ICON";
            fwrite(bad_ico, 1, sizeof(bad_ico) - 1U, ifp);
            fclose(ifp);
        }
        assert(nl_load_game_icon_diag(ds, &ds->games[0], ds->covers_dir) == false);
        assert(ds->games[0].icon_source == NL_ICON_NONE);

        /* Corrupt .scr.tns -> E501 */
        {
            FILE *cfp = fopen("/tmp/nl_diag_suite_fs/ndless/covers/Minecraft.scr.tns", "wb");
            assert(cfp != NULL);
            const char bad_scr[] = "BAD_SCR";
            fwrite(bad_scr, 1, sizeof(bad_scr) - 1U, cfp);
            fclose(cfp);
        }
        assert(nl_load_game_screenshot_diag(ds, &ds->games[0], ds->covers_dir) == false);

        /* Corrupt config & profiles -> E301 & E601 with .bak.tns preserved */
        assert(nl_load_config(ds, bad_cfg) == false);
        assert(nl_file_exists("/tmp/nl_diag_suite_fs/ndless/nlauncher.cfg.bak.tns") == true);
        assert(nl_load_profiles(ds, bad_prof) == false);
        assert(nl_file_exists("/tmp/nl_diag_suite_fs/ndless/nlauncher.profiles.bak.tns") == true);

        /* Deduplication + E401 + E801 OOM recovery */
        int cnt_before = ds->diag.count;
        nl_diag_report(ds, NL_DIAG_E401_DIR_MISSING, "covers", ds->covers_dir, "Check", "Missing", "None", "Recreated");
        assert(ds->diag.count == cnt_before + 1);
        nl_diag_report(ds, NL_DIAG_E401_DIR_MISSING, "covers", ds->covers_dir, "Check", "Missing", "None", "Recreated");
        assert(ds->diag.count == cnt_before + 1);
        assert(ds->diag.entries[cnt_before].count == 2U);

        /* E801 frees scr_pixels while preserving icon_pixels */
        ds->games[0].scr_pixels = (uint16_t *)malloc(NL_COVER_BYTES);
        ds->games[0].has_screenshot = true;
        ds->games[0].icon_pixels = (uint16_t *)calloc((size_t)(NL_ICON_V1_W * NL_ICON_V1_H), sizeof(uint16_t));
        ds->games[0].icon_w = NL_ICON_V1_W;
        ds->games[0].icon_h = NL_ICON_V1_H;
        ds->games[0].icon_source = NL_ICON_ICO;
        nl_recover_memory_on_oom(ds, "Test OOM", "lib_fb");
        assert(ds->sys.oom_recoveries == 1U);
        assert(ds->games[0].scr_pixels == NULL);
        assert(ds->games[0].has_screenshot == false);
        assert(ds->games[0].icon_pixels != NULL);

        /* Verify all 10 error codes recorded and persistent diag log written */
        const nl_diag_code_t expected_codes[10] = {
            NL_DIAG_E901_PREV_CRASH,
            NL_DIAG_E201_GAME_MALFORMED,
            NL_DIAG_E102_ICON_MISSING,
            NL_DIAG_E104_SCREENSHOT_MISSING,
            NL_DIAG_E103_ICON_CORRUPT,
            NL_DIAG_E501_ASSET_INVALID,
            NL_DIAG_E301_CONFIG_INVALID,
            NL_DIAG_E601_PROFILE_CORRUPT,
            NL_DIAG_E401_DIR_MISSING,
            NL_DIAG_E801_LOW_MEMORY
        };
        for (int k = 0; k < 10; k++) {
            bool found_code = false;
            for (int e = 0; e < ds->diag.count; e++) {
                if (ds->diag.entries[e].code == expected_codes[k]) {
                    found_code = true;
                    break;
                }
            }
            assert(found_code == true);
        }
        assert(nl_file_exists(ds->sys.diag_log_path) == true);

        /* Diagnostics & Journaux UI navigation, detail overlay, Tab 1, and safe clear confirm modal */
        nl_handle_key(ds, NL_KEY_SETTINGS);
        ds->sui.menu_cursor = NL_SET_MENU_DIAG;
        nl_handle_key(ds, NL_KEY_EXE);
        assert(ds->sui.screen == NL_UI_SETTINGS_DIAG);
        assert(ds->sui.diag_tab == 0);
        nl_render_frame(ds, fb);

        /* Open detail modal for first entry */
        ds->sui.diag_cursor = 0;
        nl_handle_key(ds, NL_KEY_EXE);
        assert(ds->sui.diag_detail_open == true);
        nl_render_frame(ds, fb);
        nl_handle_key(ds, NL_KEY_ESC);
        assert(ds->sui.diag_detail_open == false);

        /* Switch to Tab 1 (Etapes boot & Systeme) and render */
        nl_handle_key(ds, NL_KEY_RIGHT);
        assert(ds->sui.diag_tab == 1);
        nl_render_frame(ds, fb);
        nl_handle_key(ds, NL_KEY_LEFT);
        assert(ds->sui.diag_tab == 0);

        /* Clear button at index == diag.count opens confirm modal defaulting to Annuler */
        ds->sui.diag_cursor = ds->diag.count;
        nl_handle_key(ds, NL_KEY_EXE);
        assert(ds->sui.diag_confirm_modal == true);
        assert(ds->sui.diag_confirm_yes == false);
        nl_render_frame(ds, fb);
        nl_handle_key(ds, NL_KEY_RIGHT);
        assert(ds->sui.diag_confirm_yes == true);
        nl_handle_key(ds, NL_KEY_EXE);
        assert(ds->sui.diag_confirm_modal == false);
        assert(ds->diag.count == 0);
        assert(ds->total_games == 1); /* Never deletes games or profiles! */

        nl_free_icons(ds);
        free(ds);
        printf("[PASS] Section 29 Diagnostics, 12-stage boot forensics, OOM recovery, fault isolation & UI verified.\n");
    }

    /* =====================================================================
     * 30. #R10 GLOBAL TI-OS HOTKEY (SWI DECODER, 3 COMBOS, SAFETY MATRIX,
     *     LIVE RAM STATE, MOUSE/KEY UI & BINARY STAMPS)
     * ===================================================================== */
    {
        /* 30a. Verify N-Launcher.tns contains Build #R10 stamp and nl_hotkey.tns is valid Zehn */
        FILE *tfp = fopen(compiled_tns, "rb");
        assert(tfp != NULL);
        fseek(tfp, 0, SEEK_END);
        long tsz = ftell(tfp);
        assert(tsz > 10000L);
        fseek(tfp, 0, SEEK_SET);
        uint8_t *tbuf = (uint8_t *)malloc((size_t)tsz);
        assert(tbuf != NULL);
        assert(fread(tbuf, 1, (size_t)tsz, tfp) == (size_t)tsz);
        fclose(tfp);
        assert(bytes_contains_sub(tbuf, (size_t)tsz, "#R23", 4));
        free(tbuf);

        char hk_bin_path[512];
        const char *slash = strrchr(compiled_tns, '/');
        if (slash) {
            size_t dlen = (size_t)(slash - compiled_tns);
            snprintf(hk_bin_path, sizeof(hk_bin_path), "%.*s/nl_hotkey.tns", (int)dlen, compiled_tns);
        } else {
            snprintf(hk_bin_path, sizeof(hk_bin_path), "nl_hotkey.tns");
        }
        FILE *hkfp = fopen(hk_bin_path, "rb");
        assert(hkfp != NULL);
        fseek(hkfp, 0, SEEK_END);
        long hksz = ftell(hkfp);
        assert(hksz > 1024L && hksz < 65536L);
        fseek(hkfp, 0, SEEK_SET);
        uint8_t *hkbuf = (uint8_t *)malloc((size_t)hksz);
        assert(hkbuf != NULL);
        assert(fread(hkbuf, 1, (size_t)hksz, hkfp) == (size_t)hksz);
        fclose(hkfp);
        assert(memcmp(hkbuf, "PRG\0", 4) == 0);
        assert(bytes_contains_sub(hkbuf, (size_t)hksz, "ZEHN", 4) ||
               bytes_contains_sub(hkbuf, (size_t)hksz, "Zehn", 4));
        assert(bytes_contains_sub(hkbuf, (size_t)hksz, "nl_hotkey", 9));
        assert(bytes_contains_sub(hkbuf, (size_t)hksz, "#R23", 4));
        free(hkbuf);

        /* 30b. Verify N-ext SWI handler decoder (nl_hotkey_parse_swi_handler) */
        uintptr_t mock_ext_tbl[16];
        uintptr_t mock_os_tbl[8];
        memset(mock_ext_tbl, 0, sizeof(mock_ext_tbl));
        memset(mock_os_tbl, 0, sizeof(mock_os_tbl));

        struct {
            uint32_t pre[2];   /* pre[0] = "NEXT" (swi_handler - 2 words) */
            uint32_t swi[8];   /* swi[0]..swi[5] instructions, swi[6..7] pad */
            uintptr_t pool_ext;
            uintptr_t pool_os;
            uint32_t pad[64];
        } sim;
        memset(&sim, 0, sizeof(sim));
        sim.pool_ext = (uintptr_t)mock_ext_tbl;
        sim.pool_os  = (uintptr_t)mock_os_tbl;

        uint32_t off_ext = (uint32_t)((uintptr_t)&sim.pool_ext - ((uintptr_t)&sim.swi[1] + 8U));
        uint32_t off_os  = (uint32_t)((uintptr_t)&sim.pool_os  - ((uintptr_t)&sim.swi[4] + 8U));
        assert(off_ext < 0x1000U && off_os < 0x1000U);

        /* Encode exact ints_swi_handler pattern used on OS 4.5 & OS 6.2 */
        sim.pre[0] = 0x00000000U; /* Without "NEXT" first -> must reject */
        sim.swi[0] = 0xE3510602U; /* cmp r1, #0x200000 */
        sim.swi[1] = 0x059F2000U | (off_ext & 0xFFFU); /* ldreq r2, [pc, #off_ext] */
        sim.swi[2] = 0xE3510501U; /* cmp r1, #0x400000 */
        sim.swi[3] = 0x059F2000U; /* ldreq r2, =emu */
        sim.swi[4] = 0x159F2000U | (off_os & 0xFFFU);  /* ldrne r2, [pc, #off_os] */
        sim.swi[5] = 0xE7920100U; /* ldr r0, [r2, r0, lsl #2] */

        uintptr_t *out_ext = NULL;
        uintptr_t *out_os  = NULL;
        assert(nl_hotkey_parse_swi_handler(sim.swi, &out_ext, &out_os) == false);

        sim.pre[0] = 0x4E455854U; /* "NEXT" at *(swi_handler - 2) */
        assert(nl_hotkey_parse_swi_handler(sim.swi, &out_ext, &out_os) == true);
        assert(out_ext == mock_ext_tbl);
        assert(out_os  == mock_os_tbl);

        /* Verify third-party hook rejection vs clean prologue */
        uint32_t clean_fn[4] = { 0xE92D47F0U, 0xE24DD010U, 0xE1A04000U, 0xE12FFF1EU };
        bool third_party = false;
        assert(nl_hotkey_find_live_ctrl_at((uintptr_t)clean_fn, 0U, 0U, &third_party) == NULL);
        assert(third_party == false);

        uint32_t hooked_fn[4] = { 0xE51FF004U, 0x10401000U, 0xE1A04000U, 0xE12FFF1EU };
        /* Note: don't dereference 0x10401000 when ram_min/ram_max rejects out-of-range pointer */
        assert(nl_hotkey_find_live_ctrl_at((uintptr_t)hooked_fn,
                                           (uintptr_t)hooked_fn,
                                           (uintptr_t)hooked_fn + 0x1000U,
                                           &third_party) == NULL);
        assert(third_party == true);

        /* 30c. Combo parsing, labels, strict matching & config persistence */
        assert(strcmp(nl_hk_combo_to_str(NL_HK_COMBO_CTRL_MENU), "ctrl_menu") == 0);
        assert(strcmp(nl_hk_combo_to_str(NL_HK_COMBO_SHIFT_MENU), "shift_menu") == 0);
        assert(strcmp(nl_hk_combo_to_str(NL_HK_COMBO_CTRL_SHIFT_MENU), "ctrl_shift_menu") == 0);
        assert(nl_str_to_hk_combo("shift_menu") == NL_HK_COMBO_SHIFT_MENU);
        assert(nl_str_to_hk_combo("ctrl_shift_menu") == NL_HK_COMBO_CTRL_SHIFT_MENU);
        assert(nl_str_to_hk_combo("anything_else") == NL_HK_COMBO_CTRL_MENU);

        assert(nl_hk_combo_matches(NL_HK_COMBO_CTRL_MENU, true, false, true) == true);
        assert(nl_hk_combo_matches(NL_HK_COMBO_CTRL_MENU, true, true, true) == false);
        assert(nl_hk_combo_matches(NL_HK_COMBO_SHIFT_MENU, false, true, true) == true);
        assert(nl_hk_combo_matches(NL_HK_COMBO_SHIFT_MENU, true, true, true) == false);
        assert(nl_hk_combo_matches(NL_HK_COMBO_CTRL_SHIFT_MENU, true, true, true) == true);
        assert(nl_hk_combo_matches(NL_HK_COMBO_CTRL_SHIFT_MENU, true, false, true) == false);

        /* 30d. Full UI + storage + RAM live hook + combo persistence workflow */
        const char *hk_root = "/tmp/nl_hotkey_r10_fs";
        assert(system("rm -rf /tmp/nl_hotkey_r10_fs && mkdir -p /tmp/nl_hotkey_r10_fs/ndless") == 0);
        const char *hk_launcher = "/tmp/nl_hotkey_r10_fs/ndless/N-Launcher.tns";
        const char *hk_cfg      = "/tmp/nl_hotkey_r10_fs/ndless/nlauncher.cfg.tns";
        create_mock_zehn_tns(hk_launcher, "N-Launcher", "daniel.b", 3, true, true, 0);

        nl_state_t *hs = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(hs != NULL);
        nl_init_state(hs);
        snprintf(hs->sys.docs_root, sizeof(hs->sys.docs_root), "%s", hk_root);
        snprintf(hs->sys.self_path, sizeof(hs->sys.self_path), "%s", hk_launcher);
        hs->sys.has_ndless = true;
        hs->sys.ndless_persistent = false; /* Simulate Ndless Classique session */

        /* Navigate Settings -> Raccourci global TI-OS */
        nl_handle_key(hs, NL_KEY_SETTINGS);
        hs->sui.menu_cursor = NL_SET_MENU_HOTKEY;
        nl_handle_key(hs, NL_KEY_EXE);
        assert(hs->sui.screen == NL_UI_SETTINGS_HOTKEY);
        assert(hs->sui.hotkey_cursor == 0);
        assert(hs->settings.global_hotkey == false);
        assert(hs->sys.hotkey_ram_active == false);

        /* Enable Row 0 with EXE */
        assert(nl_handle_key(hs, NL_KEY_EXE) == true);
        assert(hs->settings.global_hotkey == true);
        assert(hs->sys.hotkey_active == true);
        assert(hs->sys.hotkey_ram_active == true);

        /* Move DOWN to Row 1 (Combinaison) and cycle to SHIFT+MENU -> CTRL+SHIFT+MENU */
        assert(nl_handle_key(hs, NL_KEY_DOWN) == true);
        assert(hs->sui.hotkey_cursor == 1);
        assert(nl_handle_key(hs, NL_KEY_RIGHT) == true);
        assert(hs->settings.hotkey_combo == (int)NL_HK_COMBO_SHIFT_MENU);
        assert(nl_handle_key(hs, NL_KEY_RIGHT) == true);
        assert(hs->settings.hotkey_combo == (int)NL_HK_COMBO_CTRL_SHIFT_MENU);

        /* Save, cold-reload config, and verify combo + global_hotkey preserved */
        assert(nl_save_config(hs, hk_cfg) == true);
        nl_render_frame(hs, fb);

        static nl_state_t reloaded_hk;
        nl_init_state(&reloaded_hk);
        snprintf(reloaded_hk.sys.docs_root, sizeof(reloaded_hk.sys.docs_root), "%s", hk_root);
        snprintf(reloaded_hk.sys.self_path, sizeof(reloaded_hk.sys.self_path), "%s", hk_launcher);
        reloaded_hk.sys.has_ndless = true;
        assert(nl_load_config(&reloaded_hk, hk_cfg) == true);
        assert(reloaded_hk.settings.global_hotkey == true);
        assert(reloaded_hk.settings.hotkey_combo == (int)NL_HK_COMBO_CTRL_SHIFT_MENU);
        nl_hotkey_reconcile(&reloaded_hk);
        assert(reloaded_hk.sys.hotkey_ram_active == true);

        /* Verify nl_hotkey_should_trigger_ex respects active combo, lock & recovery */
        char out_tgt[256];
        assert(nl_hotkey_should_trigger_ex(NL_HK_COMBO_CTRL_SHIFT_MENU, true, false, true,
                                           false, false, false, hk_root, mock_stub_file_ok,
                                           out_tgt, sizeof(out_tgt)) == false);
        assert(nl_hotkey_should_trigger_ex(NL_HK_COMBO_CTRL_SHIFT_MENU, true, true, true,
                                           false, false, false, hk_root, mock_stub_file_ok,
                                           out_tgt, sizeof(out_tgt)) == true);
        assert(strcmp(out_tgt, hk_launcher) == 0);

        /* Active lock blocks hotkey while N-Launcher or game is open */
        nl_bg_set_active_lock(hk_root, true);
        assert(nl_hotkey_should_trigger_ex(NL_HK_COMBO_CTRL_SHIFT_MENU, true, true, true,
                                           false, false, false, hk_root, mock_stub_file_ok,
                                           out_tgt, sizeof(out_tgt)) == false);
        nl_bg_set_active_lock(hk_root, false);

        /* Mouse interaction in NL_UI_SETTINGS_HOTKEY (Row 1 click cycles combo back to Ctrl+Menu, Row 0 click disables) */
        hs->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        hs->settings.touchpad_cursor_show = true;
        nl_mouse_move_to(hs, 160, 96);
        assert(hs->sui.hover_kind == NL_HOVER_SET_ROW && hs->sui.hover_index == 1);
        assert(nl_mouse_click(hs) == true);
        assert(hs->settings.hotkey_combo == (int)NL_HK_COMBO_CTRL_MENU);

        nl_mouse_move_to(hs, 160, 50);
        assert(hs->sui.hover_kind == NL_HOVER_SET_ROW && hs->sui.hover_index == 0);
        assert(nl_mouse_click(hs) == true);
        assert(hs->settings.global_hotkey == false);
        assert(hs->sys.hotkey_active == false);
        assert(hs->sys.hotkey_ram_active == false);
        assert(nl_file_exists("/tmp/nl_hotkey_r10_fs/ndless/startup/nl_hotkey.tns") == false);

        /* Strictly verify zero legacy boot autostart stub created */
        assert(nl_file_exists("/tmp/nl_hotkey_r10_fs/ndless/startup/nl_boot.tns") == false);

        nl_free_icons(hs);
        free(hs);
        printf("[PASS] Section 30 #R10 Global TI-OS Hotkey (SWI parser, 3 combos, Classique/Persistant RAM & UI) verified.\n");
    }

    /* 31. Full A-to-Z Audit Regression Suite: .bak.tns exclusion, nl_bg_enable_hotkey_in_cfg, Profile show_shortcuts=true + touchpad_cursor_* persistence, 44-slot Quick Profile scales & Display hitbox parity */
    {
        /* 31a. .bak.tns backup files and icons/ folder are ignored by scanner without false E201 */
        assert(nl_is_system_or_self("/documents/ndless/nlauncher.cfg.bak.tns", "/documents/ndless/N-Launcher.tns") == true);
        assert(nl_is_system_or_self("/documents/ndless/nlauncher.profiles.bak.tns", "/documents/ndless/N-Launcher.tns") == true);

        /* 31b. nl_bg_enable_hotkey_in_cfg updates global_hotkey=0 -> 1 while preserving other keys */
        const char *test_cfg_path = "/tmp/nl_audit_hk_cfg.tns";
        {
            FILE *fp = fopen(test_cfg_path, "wb");
            assert(fp != NULL);
            fprintf(fp, "theme=3\nglobal_hotkey=0\nhotkey_combo=2\nlayout=2\n");
            fclose(fp);
        }
        assert(nl_bg_enable_hotkey_in_cfg(test_cfg_path) == true);
        {
            bool hk_en = false;
            nl_hk_combo_t hk_cb = NL_HK_COMBO_CTRL_MENU;
            char hk_tgt[256] = "";
            nl_bg_read_hotkey_cfg_ex(test_cfg_path, &hk_en, &hk_cb, hk_tgt, sizeof(hk_tgt));
            assert(hk_en == true);
            assert(hk_cb == NL_HK_COMBO_CTRL_SHIFT_MENU);
            remove(test_cfg_path);
        }

        /* 31c. Newly created profiles default to show_shortcuts=true and persist touchpad_cursor_show + touchpad_cursor_style */
        nl_state_t *ps = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ps != NULL);
        nl_init_state(ps);
        int p1 = nl_profile_create(ps, "AuditUser", 14);
        assert(p1 == 1);
        assert(ps->profiles.active_index == p1);
        assert(ps->profiles.items[p1].show_shortcuts == true);
        assert(ps->profiles.items[p1].touchpad_cursor_show == false);
        assert(ps->profiles.items[p1].touchpad_cursor_style == (int)NL_CURSOR_STYLE_ARROW);

        assert(nl_profile_switch_to(ps, 0) == true);
        assert(nl_profile_switch_to(ps, p1) == true);
        ps->settings.touchpad_cursor_show = true;
        ps->settings.touchpad_cursor_style = NL_CURSOR_STYLE_POINTER;
        nl_profile_snapshot_active(ps);

        const char *prof_audit_path = "/tmp/nl_audit_profiles.tns";
        assert(nl_save_profiles(ps, prof_audit_path) == true);

        nl_state_t *ps_load = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ps_load != NULL);
        nl_init_state(ps_load);
        assert(nl_load_profiles(ps_load, prof_audit_path) == true);
        assert(ps_load->profiles.active_index == p1);
        assert(ps_load->settings.touchpad_cursor_show == true);
        assert(ps_load->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER);
        assert(ps_load->anim.switch_tp_cursor_fp == 256);
        remove(prof_audit_path);

        /* 31d. 43 profiles + '+' slot (44 slots total) in Quick Profile Selector animate & render cleanly */
        for (int n = ps->profiles.count; n < NL_MAX_PROFILES; n++) {
            int av = nl_avatar_first_free(ps);
            assert(av >= 0);
            char pname[16];
            snprintf(pname, sizeof(pname), "P%d", n + 1);
            assert(nl_profile_create(ps, pname, av) == n);
        }
        assert(ps->profiles.count == NL_MAX_PROFILES);
        ps->sui.screen = NL_UI_LIBRARY;
        ps->sui.qp_open = true;
        ps->sui.qp_cursor = NL_MAX_PROFILES; /* Slot 43 ('+' slot) */
        for (int step = 0; step < 8; step++) {
            nl_anim_step(ps);
            nl_render_frame(ps, fb);
        }
        assert(ps->anim.qp_slot_scale_fp[NL_MAX_PROFILES] > ps->anim.qp_slot_scale_fp[0]);

        /* 31e. SETTINGS_DISPLAY 31px stride Mouse Mode hover verification */
        ps->sui.qp_open = false;
        ps->sui.screen = NL_UI_SETTINGS_DISPLAY;
        ps->sui.display_scroll = 0;
        ps->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        ps->settings.touchpad_cursor_show = true;
        nl_mouse_move_to(ps, 160, 30 + 2 * 31 + 14);
        assert(ps->sui.hover_valid == true);
        assert(ps->sui.hover_kind == NL_HOVER_SET_ROW);
        assert(ps->sui.hover_index == 2);

        nl_free_icons(ps_load);
        free(ps_load);
        nl_free_icons(ps);
        free(ps);
        printf("[PASS] Section 31 A-to-Z Audit regressions (.bak.tns, hotkey cfg auto-enable, profile cursor persistence & 44-slot QP) verified.\n");
    }

    /* 32. #R11 Mouse Mode Physical Click, Independent Tap-to-Click, 3 Cursor Sizes x 3 Models & Perf/Cleanup */
    {
        /* 32a. Physical click works IMMEDIATELY after cursor slide without lifting finger, no double trigger */
        nl_tp_click_guard_t cg;
        nl_tp_click_reset(&cg);
        const uint16_t pw = 2328, ph = 1691;
        /* Slide finger across touchpad */
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 600, 800, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1200, 900, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1700, 950, pw, ph) == NL_TP_ACT_NONE);
        /* Physically click dome switch right at the end of the slide (even off-center!) */
        assert(nl_mouse_tp_poll(&cg, true, true, false, true, true, 1700, 950, pw, ph) == NL_TP_ACT_PHYSICAL_CLICK);
        /* Holding button does NOT double-fire */
        assert(nl_mouse_tp_poll(&cg, true, true, false, true, true, 1700, 950, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, true, true, false, true, true, 1700, 950, pw, ph) == NL_TP_ACT_NONE);
        /* Release button -> no spurious tap emitted after physical click */
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1700, 950, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, false, false, false, true, true, 1700, 950, pw, ph) == NL_TP_ACT_NONE);

        /* 32b. Tap-to-Click ON: single short center tap triggers once, cooldown prevents double-tap */
        nl_tp_click_reset(&cg);
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1164, 845, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1170, 850, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, false, false, false, true, true, 1170, 850, pw, ph) == NL_TP_ACT_TAP_CLICK);
        /* Immediate bounce tap within cooldown (13 ticks) MUST be ignored */
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1164, 845, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, false, false, false, true, true, 1164, 845, pw, ph) == NL_TP_ACT_NONE);
        /* Advance past cooldown */
        for (int k = 0; k < 16; k++) {
            assert(nl_mouse_tp_poll(&cg, false, false, false, true, true, 0, 0, pw, ph) == NL_TP_ACT_NONE);
        }
        /* Slide during contact MUST cancel tap */
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1000, 845, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, true, 1400, 845, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, false, false, false, true, true, 1400, 845, pw, ph) == NL_TP_ACT_NONE);

        /* 32c. Tap-to-Click OFF: center tap is ignored, while physical click STILL works! */
        nl_tp_click_reset(&cg);
        assert(nl_mouse_tp_poll(&cg, true, false, false, true, false, 1164, 845, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, false, false, false, true, false, 1164, 845, pw, ph) == NL_TP_ACT_NONE);
        assert(nl_mouse_tp_poll(&cg, true, true, true, true, false, 1164, 845, pw, ph) == NL_TP_ACT_PHYSICAL_CLICK);

        /* 32d. All 3 cursor models x 3 proportional sizes: exact metrics, hotspots & safe rendering */
        static const nl_cursor_style_t all_styles[3] = {
            NL_CURSOR_STYLE_ARROW, NL_CURSOR_STYLE_CIRCLE, NL_CURSOR_STYLE_POINTER
        };
        static const nl_cursor_size_t all_sizes[3] = {
            NL_CURSOR_SIZE_SMALL, NL_CURSOR_SIZE_MEDIUM, NL_CURSOR_SIZE_LARGE
        };
        for (int m = 0; m < 3; m++) {
            int prev_w = 0, prev_h = 0;
            for (int sz = 0; sz < 3; sz++) {
                int w = 0, h = 0, hx = 0, hy = 0;
                nl_cursor_get_size_metrics(all_styles[m], all_sizes[sz], &w, &h, &hx, &hy);
                assert(w > prev_w && h > prev_h);
                assert(hx >= 0 && hx < w && hy >= 0 && hy < h);
                prev_w = w;
                prev_h = h;
            }
        }

        /* 32e. Touchpad Settings Tab 0 (7 scrollable rows) + Tab 1 (Model + Size cards) + Independence + Persistence */
        nl_state_t *ms = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ms != NULL);
        nl_init_state(ms);
        ms->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        ms->settings.touchpad_cursor_show = true;
        ms->sui.screen = NL_UI_SETTINGS_TOUCHPAD;
        ms->sui.tp_tab = 0;
        ms->sui.tp_cursor = 0;
        ms->sui.tp_scroll = 0;
        ms->anim.set_open_fp = 256;
        ms->anim.set_last_screen = NL_UI_SETTINGS_TOUCHPAD;

        /* Navigate down through all 7 rows verifying scroll 0 -> 2 */
        for (int r = 1; r <= 6; r++) {
            assert(nl_handle_key(ms, NL_KEY_DOWN) == true);
            assert(ms->sui.tp_cursor == r);
        }
        assert(ms->sui.tp_scroll == 2);

        /* Row 5 = Taille du curseur (move up 1 from Row 6) */
        assert(nl_handle_key(ms, NL_KEY_UP) == true);
        assert(ms->sui.tp_cursor == 5);
        assert(ms->settings.cursor_size == NL_CURSOR_SIZE_MEDIUM);
        assert(nl_handle_key(ms, NL_KEY_RIGHT) == true);
        assert(ms->settings.cursor_size == NL_CURSOR_SIZE_LARGE);
        /* Verify changing size did NOT alter model, speed, sensitivity, or tap_to_click */
        assert(ms->settings.touchpad_cursor_style == NL_CURSOR_STYLE_ARROW);
        assert(ms->settings.cursor_speed == 2);
        assert(ms->settings.touchpad_sensitivity == 2);
        assert(ms->settings.tap_to_click == false);

        /* Row 4 = Tapotement pour cliquer (move up 1) -> toggle ON (OUI) */
        assert(nl_handle_key(ms, NL_KEY_UP) == true);
        assert(ms->sui.tp_cursor == 4);
        assert(nl_handle_key(ms, NL_KEY_EXE) == true);
        assert(ms->settings.tap_to_click == true);
        assert(ms->settings.cursor_size == NL_CURSOR_SIZE_LARGE);

        /* Switch to Tab 1 (Personnaliser le curseur), click Pointer model (index 2) & Small size (index 0) */
        assert(nl_handle_key(ms, NL_KEY_TAB) == true);
        assert(ms->sui.tp_tab == 1);
        nl_mouse_move_to(ms, 14 + 2 * 101 + 45, 90);
        assert(ms->sui.hover_kind == NL_HOVER_SET_STYLE_CARD && ms->sui.hover_index == 2);
        assert(nl_mouse_click(ms) == true);
        assert(ms->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER);
        assert(ms->settings.cursor_size == NL_CURSOR_SIZE_LARGE); /* Model change preserves size */

        nl_mouse_move_to(ms, 14 + 0 * 101 + 45, 175);
        assert(ms->sui.hover_kind == NL_HOVER_SET_SIZE_CARD && ms->sui.hover_index == 0);
        assert(nl_mouse_click(ms) == true);
        assert(ms->settings.cursor_size == NL_CURSOR_SIZE_SMALL);
        assert(ms->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER); /* Size change preserves model */

        /* Render all 9 model x size combinations including screen corners (0,0) and (319,239) */
        for (int m = 0; m < 3; m++) {
            for (int sz = 0; sz < 3; sz++) {
                ms->settings.touchpad_cursor_style = all_styles[m];
                ms->settings.cursor_size = all_sizes[sz];
                nl_mouse_move_to(ms, 0, 0);
                nl_render_frame(ms, fb);
                nl_mouse_move_to(ms, 319, 239);
                nl_render_frame(ms, fb);
            }
        }

        /* Restore Pointer + Small + tap_to_click=true (all non-default) and verify full persistence across cfg + profiles */
        ms->settings.touchpad_cursor_style = NL_CURSOR_STYLE_POINTER;
        ms->settings.cursor_size = NL_CURSOR_SIZE_SMALL;
        ms->settings.tap_to_click = true;
        ms->settings.cursor_speed = 4;
        nl_profile_snapshot_active(ms);

        const char *r11_cfg = "/tmp/nl_r11_mouse_cfg.tns";
        const char *r11_prof = "/tmp/nl_r11_mouse_prof.tns";
        assert(nl_save_config(ms, r11_cfg) == true);
        assert(nl_save_profiles(ms, r11_prof) == true);

        nl_state_t *ms_re = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(ms_re != NULL);
        nl_init_state(ms_re);
        assert(nl_load_config(ms_re, r11_cfg) == true);
        assert(ms_re->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER);
        assert(ms_re->settings.cursor_size == NL_CURSOR_SIZE_SMALL);
        assert(ms_re->settings.tap_to_click == true);
        assert(ms_re->settings.cursor_speed == 4);

        nl_init_state(ms_re);
        assert(nl_load_profiles(ms_re, r11_prof) == true);
        assert(ms_re->settings.touchpad_cursor_style == NL_CURSOR_STYLE_POINTER);
        assert(ms_re->settings.cursor_size == NL_CURSOR_SIZE_SMALL);
        assert(ms_re->settings.tap_to_click == true);
        assert(ms_re->settings.cursor_speed == 4);

        remove(r11_cfg);
        remove(r11_prof);
        nl_free_icons(ms_re);
        free(ms_re);
        nl_free_icons(ms);
        free(ms);
        printf("[PASS] Section 32 #R11 Mouse Mode (physical click + tap-to-click), 3 cursor sizes x 3 models & persistence verified.\n");
    }

    /* =====================================================================
     * 33. #R12 COMPLETE TEST MATRIX A-G:
     *     DECOUPLED ICONS (0 / 1 / 78 DIVERSE ICONS / DELETE ICONS),
     *     ICON-INDEPENDENT LIBRARY INDEX (nlauncher.lib.tns),
     *     TRANSACTIONAL ZERO-RECURSION RESCAN + EMPTY-SCAN GUARD,
     *     CONSECUTIVE RESTARTS + AUTO STARTUP SCAN & ERROR RESILIENCE
     * ===================================================================== */
    {
        const char *r12_root = "/tmp/nl_r12_matrix_fs";
        assert(system("rm -rf /tmp/nl_r12_matrix_fs && mkdir -p /tmp/nl_r12_matrix_fs/ndless/icons /tmp/nl_r12_matrix_fs/ndless/covers /tmp/nl_r12_matrix_fs/games /tmp/nl_r12_matrix_fs/MesJeuxPerso") == 0);
        const char *r12_self = "/tmp/nl_r12_matrix_fs/ndless/N-Launcher.tns";
        const char *r12_lib  = "/tmp/nl_r12_matrix_fs/ndless/nlauncher.lib.tns";
        const char *r12_cfg  = "/tmp/nl_r12_matrix_fs/ndless/nlauncher.cfg.tns";
        const char *r12_prof = "/tmp/nl_r12_matrix_fs/ndless/nlauncher.profiles.tns";
        const char *r12_cache = "/tmp/nl_r12_matrix_fs/ndless/nlauncher.cache.tns";
        create_mock_zehn_tns(r12_self, "N-Launcher", "daniel.b", 3, true, true, 0);

        /* --- TEST A: 0 icons -> games discovered, persisted, rendered & launchable --- */
        for (int i = 0; i < 6; i++) {
            char gp[256], title[32];
            snprintf(gp, sizeof(gp), "%s/games/Game_%02d.tns", r12_root, i + 1);
            snprintf(title, sizeof(title), "Game%02d", i + 1);
            create_mock_zehn_tns(gp, title, "Dev", 1, true, false, 0);
        }

        nl_state_t *stA = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(stA != NULL);
        nl_init_state(stA);
        snprintf(stA->covers_dir, sizeof(stA->covers_dir), "%s/ndless/covers", r12_root);
        snprintf(stA->icons_dir, sizeof(stA->icons_dir), "%s/ndless/icons", r12_root);
        snprintf(stA->lib_path, sizeof(stA->lib_path), "%s", r12_lib);
        nl_folder_seed_defaults(&stA->folders, r12_root);
        assert(nl_transactional_scan(stA, r12_root, r12_self, true, false, NULL, NULL) == true);
        assert(stA->total_games == 6);
        nl_load_all_icons(stA);
        for (int i = 0; i < 6; i++) {
            assert(stA->games[i].icon_source == NL_ICON_NONE);
            assert(stA->games[i].icon_pixels == NULL);
        }
        nl_rebuild_filter(stA);
        nl_render_frame(stA, fb);
        assert(nl_handle_key(stA, NL_KEY_EXE) == true);
        assert(stA->launch_requested == true && stA->launch_game_idx >= 0);
        stA->launch_requested = false;
        assert(nl_save_library_index(stA, r12_lib) == true);
        nl_free_icons(stA);
        free(stA);
        printf("[PASS] #R12 Test A: 0 icons -> 6 games visible, rendered & launchable.\n");

        /* --- TEST B: 1 icon -> matching game shows icon, all 5 others stay visible & launchable --- */
        {
            char ico1[256];
            snprintf(ico1, sizeof(ico1), "%s/ndless/icons/Game_01.ico.tns", r12_root);
            FILE *fp = fopen(ico1, "wb");
            assert(fp != NULL);
            uint8_t ihdr[22] = {0,0, 1,0, 1,0, 32,32,0,0, 1,0, 32,0, 0,0,0,0, 22,0,0,0};
            uint32_t img_sz = 40U + 32U * 32U * 4U;
            ihdr[14] = (uint8_t)(img_sz & 0xFFU);
            ihdr[15] = (uint8_t)((img_sz >> 8) & 0xFFU);
            ihdr[16] = (uint8_t)((img_sz >> 16) & 0xFFU);
            fwrite(ihdr, 1, sizeof(ihdr), fp);
            uint8_t bih[40];
            memset(bih, 0, sizeof(bih));
            bih[0] = 40; bih[4] = 32; bih[8] = 64; bih[12] = 1; bih[14] = 32;
            fwrite(bih, 1, 40, fp);
            for (int p = 0; p < 32 * 32; p++) {
                uint8_t bgra[4] = { 220, 140, 40, 255 };
                fwrite(bgra, 1, 4, fp);
            }
            fclose(fp);
        }
        nl_state_t *stB = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(stB != NULL);
        nl_init_state(stB);
        snprintf(stB->covers_dir, sizeof(stB->covers_dir), "%s/ndless/covers", r12_root);
        snprintf(stB->icons_dir, sizeof(stB->icons_dir), "%s/ndless/icons", r12_root);
        assert(nl_load_library_index(stB, r12_lib) == true);
        assert(stB->total_games == 6);
        nl_load_all_icons(stB);
        int ico_with = 0, ico_generic = 0;
        for (int i = 0; i < stB->total_games; i++) {
            if (stB->games[i].icon_source == NL_ICON_ICO && stB->games[i].icon_pixels != NULL) ico_with++;
            else ico_generic++;
        }
        assert(ico_with == 1 && ico_generic == 5);
        nl_rebuild_filter(stB);
        nl_render_frame(stB, fb);
        nl_free_icons(stB);
        free(stB);
        printf("[PASS] #R12 Test B: 1 icon -> 1 custom icon + 5 generic fallback games.\n");

        /* --- TEST C: 78 realistic diverse icons (32bpp, 256x256 oversized, 24bpp, 8bpp, 4bpp, PNG-in-ICO) --- */
        assert(system("rm -f /tmp/nl_r12_matrix_fs/games/*.tns /tmp/nl_r12_matrix_fs/ndless/icons/*") == 0);
        for (int i = 0; i < 78; i++) {
            char gp[256], ip[256], title[32];
            snprintf(gp, sizeof(gp), "%s/games/Title_%02d.tns", r12_root, i + 1);
            snprintf(title, sizeof(title), "Title%02d", i + 1);
            create_mock_zehn_tns(gp, title, "Author", 1, true, false, 0);

            snprintf(ip, sizeof(ip), "%s/ndless/icons/Title_%02d.ico.tns", r12_root, i + 1);
            FILE *fp = fopen(ip, "wb");
            assert(fp != NULL);
            int variant = i % 6;
            if (variant == 0 || variant == 1) {
                /* 32bpp RGBA ICO: 64x64 (variant 0) or 256x256 oversized (variant 1) */
                int dim = (variant == 0) ? 64 : 256;
                uint32_t img_sz = 40U + (uint32_t)dim * (uint32_t)dim * 4U;
                uint8_t ihdr[22] = {0,0, 1,0, 1,0, (uint8_t)(dim == 256 ? 0 : dim), (uint8_t)(dim == 256 ? 0 : dim), 0,0, 1,0, 32,0,
                                    (uint8_t)(img_sz & 0xFFU), (uint8_t)((img_sz >> 8) & 0xFFU),
                                    (uint8_t)((img_sz >> 16) & 0xFFU), (uint8_t)((img_sz >> 24) & 0xFFU),
                                    22,0,0,0};
                fwrite(ihdr, 1, sizeof(ihdr), fp);
                uint8_t bih[40];
                memset(bih, 0, sizeof(bih));
                bih[0] = 40;
                bih[4] = (uint8_t)(dim & 0xFF); bih[5] = (uint8_t)((dim >> 8) & 0xFF);
                int h2 = dim * 2;
                bih[8] = (uint8_t)(h2 & 0xFF); bih[9] = (uint8_t)((h2 >> 8) & 0xFF);
                bih[12] = 1; bih[14] = 32;
                fwrite(bih, 1, 40, fp);
                uint8_t row_px[256 * 4];
                for (int y = 0; y < dim; y++) {
                    for (int x = 0; x < dim; x++) {
                        row_px[x * 4 + 0] = (uint8_t)((i * 19 + x) & 0xFF);
                        row_px[x * 4 + 1] = (uint8_t)((i * 37 + y) & 0xFF);
                        row_px[x * 4 + 2] = (uint8_t)((i * 53 + x + y) & 0xFF);
                        row_px[x * 4 + 3] = 255;
                    }
                    fwrite(row_px, 1, (size_t)dim * 4U, fp);
                }
            } else if (variant == 2) {
                /* 24bpp RGB ICO + AND mask (48x48) */
                int dim = 48;
                uint32_t xor_row = (((uint32_t)dim * 24U + 31U) / 32U) * 4U;
                uint32_t and_row = (((uint32_t)dim + 31U) / 32U) * 4U;
                uint32_t img_sz = 40U + xor_row * (uint32_t)dim + and_row * (uint32_t)dim;
                uint8_t ihdr[22] = {0,0, 1,0, 1,0, 48,48, 0,0, 1,0, 24,0,
                                    (uint8_t)(img_sz & 0xFFU), (uint8_t)((img_sz >> 8) & 0xFFU),
                                    (uint8_t)((img_sz >> 16) & 0xFFU), 0, 22,0,0,0};
                fwrite(ihdr, 1, sizeof(ihdr), fp);
                uint8_t bih[40];
                memset(bih, 0, sizeof(bih));
                bih[0] = 40; bih[4] = 48; bih[8] = 96; bih[12] = 1; bih[14] = 24;
                fwrite(bih, 1, 40, fp);
                for (int y = 0; y < dim; y++) {
                    for (int x = 0; x < dim; x++) {
                        uint8_t bgr[3] = {(uint8_t)(x * 4), (uint8_t)(y * 4), (uint8_t)(i * 3)};
                        fwrite(bgr, 1, 3, fp);
                    }
                }
                uint8_t and_zero[16];
                memset(and_zero, 0, sizeof(and_zero));
                for (int y = 0; y < dim; y++) fwrite(and_zero, 1, and_row, fp);
            } else if (variant == 3) {
                /* 8bpp indexed palette ICO + AND mask (32x32) */
                int dim = 32;
                uint32_t img_sz = 40U + 256U * 4U + 32U * 32U + 32U * 4U;
                uint8_t ihdr[22] = {0,0, 1,0, 1,0, 32,32, 0,0, 1,0, 8,0,
                                    (uint8_t)(img_sz & 0xFFU), (uint8_t)((img_sz >> 8) & 0xFFU), 0,0, 22,0,0,0};
                fwrite(ihdr, 1, sizeof(ihdr), fp);
                uint8_t bih[40];
                memset(bih, 0, sizeof(bih));
                bih[0] = 40; bih[4] = 32; bih[8] = 64; bih[12] = 1; bih[14] = 8;
                fwrite(bih, 1, 40, fp);
                for (int c = 0; c < 256; c++) {
                    uint8_t pal[4] = {(uint8_t)c, (uint8_t)(255 - c), (uint8_t)((c * 3) & 0xFF), 0};
                    fwrite(pal, 1, 4, fp);
                }
                for (int p = 0; p < dim * dim; p++) {
                    uint8_t idx = (uint8_t)((p + i) & 0xFF);
                    fwrite(&idx, 1, 1, fp);
                }
                uint8_t and_z[32 * 4];
                memset(and_z, 0, sizeof(and_z));
                fwrite(and_z, 1, sizeof(and_z), fp);
            } else if (variant == 4) {
                /* 4bpp indexed palette ICO + AND mask (32x32) */
                int dim = 32;
                uint32_t img_sz = 40U + 16U * 4U + 32U * 16U + 32U * 4U;
                uint8_t ihdr[22] = {0,0, 1,0, 1,0, 32,32, 16,0, 1,0, 4,0,
                                    (uint8_t)(img_sz & 0xFFU), (uint8_t)((img_sz >> 8) & 0xFFU), 0,0, 22,0,0,0};
                fwrite(ihdr, 1, sizeof(ihdr), fp);
                uint8_t bih[40];
                memset(bih, 0, sizeof(bih));
                bih[0] = 40; bih[4] = 32; bih[8] = 64; bih[12] = 1; bih[14] = 4; bih[32] = 16;
                fwrite(bih, 1, 40, fp);
                for (int c = 0; c < 16; c++) {
                    uint8_t pal[4] = {(uint8_t)(c * 16), (uint8_t)(240 - c * 14), (uint8_t)(c * 12), 0};
                    fwrite(pal, 1, 4, fp);
                }
                for (int p = 0; p < dim * 16; p++) {
                    uint8_t b = (uint8_t)(((p & 15) << 4) | ((p + i) & 15));
                    fwrite(&b, 1, 1, fp);
                }
                uint8_t and_z[32 * 4];
                memset(and_z, 0, sizeof(and_z));
                fwrite(and_z, 1, sizeof(and_z), fp);
            } else {
                /* Variant 5: Valid PNG-in-ICO (16x16 RGBA stored DEFLATE block inside zlib) */
                const uint32_t w = 16, h = 16;
                uint8_t raw[16 * (1 + 16 * 4)];
                for (uint32_t y = 0; y < h; y++) {
                    raw[y * 65] = 0; /* filter None */
                    for (uint32_t x = 0; x < w; x++) {
                        uint8_t *px = raw + y * 65 + 1 + x * 4;
                        px[0] = (uint8_t)(40 + x * 12);
                        px[1] = (uint8_t)(60 + y * 10);
                        px[2] = (uint8_t)((i * 7) & 0xFF);
                        px[3] = 255;
                    }
                }
                uint32_t raw_len = (uint32_t)sizeof(raw);
                uint32_t s1 = 1U, s2 = 0U;
                for (uint32_t k = 0; k < raw_len; k++) {
                    s1 = (s1 + raw[k]) % 65521U;
                    s2 = (s2 + s1) % 65521U;
                }
                uint32_t adler = (s2 << 16) | s1;
                uint32_t z_len = 2U + 5U + raw_len + 4U;
                uint32_t png_len = 8U + (12U + 13U) + (12U + z_len) + 12U;

                uint8_t ihdr[22] = {0,0, 1,0, 1,0, 16,16, 0,0, 1,0, 32,0,
                                    (uint8_t)(png_len & 0xFFU), (uint8_t)((png_len >> 8) & 0xFFU), 0,0, 22,0,0,0};
                fwrite(ihdr, 1, sizeof(ihdr), fp);
                static const uint8_t png_sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
                fwrite(png_sig, 1, 8, fp);
                /* IHDR chunk */
                uint8_t ihdr_c[25] = {0,0,0,13, 'I','H','D','R', 0,0,0,16, 0,0,0,16, 8, 6, 0, 0, 0, 0,0,0,0};
                fwrite(ihdr_c, 1, 25, fp);
                /* IDAT chunk */
                uint8_t idat_hdr[8] = {
                    (uint8_t)((z_len >> 24) & 0xFFU), (uint8_t)((z_len >> 16) & 0xFFU),
                    (uint8_t)((z_len >> 8) & 0xFFU), (uint8_t)(z_len & 0xFFU),
                    'I','D','A','T'
                };
                fwrite(idat_hdr, 1, 8, fp);
                uint8_t zhdr[7] = {0x78, 0x01, 0x01,
                                   (uint8_t)(raw_len & 0xFFU), (uint8_t)((raw_len >> 8) & 0xFFU),
                                   (uint8_t)((~raw_len) & 0xFFU), (uint8_t)(((~raw_len) >> 8) & 0xFFU)};
                fwrite(zhdr, 1, 7, fp);
                fwrite(raw, 1, raw_len, fp);
                uint8_t adler_be[4] = {(uint8_t)(adler >> 24), (uint8_t)(adler >> 16), (uint8_t)(adler >> 8), (uint8_t)adler};
                fwrite(adler_be, 1, 4, fp);
                uint8_t dummy_crc[4] = {0,0,0,0};
                fwrite(dummy_crc, 1, 4, fp);
                /* IEND chunk */
                uint8_t iend_c[12] = {0,0,0,0, 'I','E','N','D', 0,0,0,0};
                fwrite(iend_c, 1, 12, fp);
            }
            fclose(fp);
        }

        nl_state_t *stC = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(stC != NULL);
        nl_init_state(stC);
        snprintf(stC->covers_dir, sizeof(stC->covers_dir), "%s/ndless/covers", r12_root);
        snprintf(stC->icons_dir, sizeof(stC->icons_dir), "%s/ndless/icons", r12_root);
        snprintf(stC->lib_path, sizeof(stC->lib_path), "%s", r12_lib);
        snprintf(stC->profiles_path, sizeof(stC->profiles_path), "%s", r12_prof);
        nl_profiles_init_default(stC);
        nl_folder_seed_defaults(&stC->folders, r12_root);
        assert(nl_transactional_scan(stC, r12_root, r12_self, true, false, NULL, NULL) == true);
        assert(stC->total_games == 78);
        nl_load_all_icons(stC);
        int loaded_78 = 0;
        for (int i = 0; i < 78; i++) {
            assert(stC->games[i].icon_source == NL_ICON_ICO);
            assert(stC->games[i].icon_pixels != NULL);
            assert(stC->games[i].icon_w <= NL_ICON_MAX_DIM && stC->games[i].icon_h <= NL_ICON_MAX_DIM);
            loaded_78++;
        }
        assert(loaded_78 == 78);
        assert(stC->icon_ram_bytes > 0U && stC->icon_ram_bytes <= NL_ICON_RAM_BUDGET);
        /* Mark 3 favorites & create a second profile to verify preservation */
        stC->games[0].is_favorite = true;
        stC->games[10].is_favorite = true;
        stC->games[77].is_favorite = true;
        nl_profile_snapshot_active(stC);
        assert(nl_save_library_index(stC, r12_lib) == true);
        assert(nl_save_config(stC, r12_cfg) == true);
        assert(nl_save_profiles(stC, r12_prof) == true);
        nl_rebuild_filter(stC);
        /* Render multiple pages of the 78-game grid */
        for (int pg = 0; pg < 3; pg++) {
            nl_render_frame(stC, fb);
            nl_handle_key(stC, NL_KEY_PAGE_NEXT);
        }
        nl_free_icons(stC);
        free(stC);
        printf("[PASS] #R12 Test C: 78 diverse icons (32bpp/256px/24bpp/8bpp/4bpp/PNG-in-ICO) loaded safely within RAM budget.\n");

        /* --- TEST D: Delete ALL 78 icons & icons directory -> restart -> ALL 78 games remain visible & launchable --- */
        assert(system("rm -rf /tmp/nl_r12_matrix_fs/ndless/icons") == 0);
        nl_state_t *stD = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(stD != NULL);
        nl_init_state(stD);
        snprintf(stD->covers_dir, sizeof(stD->covers_dir), "%s/ndless/covers", r12_root);
        snprintf(stD->icons_dir, sizeof(stD->icons_dir), "%s/ndless/icons", r12_root);
        snprintf(stD->lib_path, sizeof(stD->lib_path), "%s", r12_lib);
        snprintf(stD->profiles_path, sizeof(stD->profiles_path), "%s", r12_prof);
        assert(nl_load_config(stD, r12_cfg) == true);
        assert(nl_load_profiles(stD, r12_prof) == true);
        assert(nl_load_library_index(stD, r12_lib) == true);
        assert(stD->total_games == 78);
        assert(nl_transactional_scan(stD, r12_root, r12_self, true, false, NULL, NULL) == true);
        nl_profile_apply_active(stD);
        nl_load_all_icons(stD);
        assert(stD->total_games == 78);
        int fav_after_del = 0;
        for (int i = 0; i < 78; i++) {
            assert(stD->games[i].icon_source == NL_ICON_NONE);
            assert(stD->games[i].icon_pixels == NULL);
            assert(nl_file_exists(stD->games[i].path) == true);
            if (stD->games[i].is_favorite) fav_after_del++;
        }
        assert(fav_after_del == 3);
        nl_rebuild_filter(stD);
        nl_render_frame(stD, fb);
        assert(nl_handle_key(stD, NL_KEY_EXE) == true);
        assert(stD->launch_requested == true && stD->launch_game_idx >= 0);
        stD->launch_requested = false;
        printf("[PASS] #R12 Test D: Deleting all 78 icons & icons dir preserves all 78 games, favorites & launchability.\n");

        /* --- TEST E: « Rescanner les jeux » repeatable 5x + progress UI + empty-scan confirmation guard --- */
        stD->sui.screen = NL_UI_SETTINGS_MAIN;
        stD->sui.menu_cursor = NL_SET_MENU_REFRESH_LIB;
        assert(nl_handle_key(stD, NL_KEY_EXE) == true);
        assert(stD->sui.screen == NL_UI_SETTINGS_RESCAN);
        assert(stD->sui.rescan_req == true);
        for (int rep = 0; rep < 5; rep++) {
            stD->sui.rescan_req = false;
            assert(nl_transactional_scan(stD, r12_root, r12_self, false, false, NULL, NULL) == true);
            nl_profile_apply_active(stD);
            nl_load_all_icons(stD);
            assert(stD->total_games == 78);
            assert(stD->scan.finished == true);
            assert(stD->scan.empty_confirm_pending == false);
            assert(stD->profiles.initialized == true && stD->profiles.count >= 1);
            nl_render_frame(stD, fb);
            /* Pressing DEL inside NL_UI_SETTINGS_RESCAN triggers another repeatable rescan */
            assert(nl_handle_key(stD, NL_KEY_RESCAN) == true);
            assert(stD->sui.rescan_req == true);
        }
        /* Now delete all games -> manual rescan MUST trigger empty_confirm_pending & preserve existing 78 games! */
        assert(system("rm -f /tmp/nl_r12_matrix_fs/games/*.tns") == 0);
        assert(nl_transactional_scan(stD, r12_root, r12_self, false, false, NULL, NULL) == true);
        assert(stD->scan.empty_confirm_pending == true);
        assert(stD->scan.empty_confirm_replace == false); /* Safe default = Conserver */
        assert(stD->total_games == 78); /* Library untouched! */
        nl_render_frame(stD, fb);
        /* Press EXE on default "Conserver" -> keeps all 78 games */
        assert(nl_handle_key(stD, NL_KEY_EXE) == true);
        assert(stD->scan.empty_confirm_pending == false);
        assert(stD->total_games == 78);
        /* Run rescan again & explicitly choose "Remplacer" -> empties library cleanly */
        assert(nl_transactional_scan(stD, r12_root, r12_self, false, false, NULL, NULL) == true);
        assert(stD->scan.empty_confirm_pending == true);
        assert(nl_handle_key(stD, NL_KEY_RIGHT) == true);
        assert(stD->scan.empty_confirm_replace == true);
        assert(nl_handle_key(stD, NL_KEY_EXE) == true);
        assert(stD->scan.commit_empty_req == true);
        assert(stD->total_games == 0);
        nl_free_icons(stD);
        free(stD);
        printf("[PASS] #R12 Test E: Rescanner les jeux repeatable 5x + progress/summary UI + empty-scan guard verified.\n");

        /* --- TEST F: 4 consecutive restarts + custom memorized folder + startup auto-scan add/remove --- */
        {
            char g1[256], g2[256], g3[256];
            snprintf(g1, sizeof(g1), "%s/MesJeuxPerso/Alpha.tns", r12_root);
            snprintf(g2, sizeof(g2), "%s/MesJeuxPerso/Beta.tns", r12_root);
            snprintf(g3, sizeof(g3), "%s/MesJeuxPerso/Gamma.tns", r12_root);
            create_mock_zehn_tns(g1, "Alpha", "Dev", 1, true, false, 0);
            create_mock_zehn_tns(g2, "Beta", "Dev", 1, true, false, 0);

            nl_state_t *stF = (nl_state_t *)calloc(1, sizeof(nl_state_t));
            assert(stF != NULL);
            nl_init_state(stF);
            snprintf(stF->lib_path, sizeof(stF->lib_path), "%s", r12_lib);
            snprintf(stF->profiles_path, sizeof(stF->profiles_path), "%s", r12_prof);
            nl_profiles_init_default(stF);
            int p_custom = nl_profile_create(stF, "Daniel", 5);
            assert(p_custom == 1);
            nl_folder_seed_defaults(&stF->folders, r12_root);
            assert(nl_transactional_scan(stF, r12_root, r12_self, true, false, NULL, NULL) == true);
            assert(stF->total_games == 2);
            /* Custom folder /tmp/nl_r12_matrix_fs/MesJeuxPerso must be automatically memorized */
            bool has_custom_dir = false;
            for (int f = 0; f < stF->folders.count; f++) {
                if (strstr(stF->folders.paths[f], "MesJeuxPerso") != NULL) has_custom_dir = true;
            }
            assert(has_custom_dir == true);
            stF->games[0].is_favorite = true;
            nl_profile_snapshot_active(stF);
            assert(nl_save_library_index(stF, r12_lib) == true);
            assert(nl_save_config(stF, r12_cfg) == true);
            assert(nl_save_profiles(stF, r12_prof) == true);
            nl_free_icons(stF);
            free(stF);

            /* 4 consecutive cold restarts:
             * Boot 1: unchanged (2 games, 1 fav, custom profile)
             * Boot 2: add Gamma.tns -> startup scan detects +1 (3 games)
             * Boot 3: remove Beta.tns -> startup scan detects -1 (2 games)
             * Boot 4: delete cache (binary reinstall) -> library & custom folder preserved */
            for (int boot = 1; boot <= 4; boot++) {
                if (boot == 2) create_mock_zehn_tns(g3, "Gamma", "Dev", 1, true, false, 0);
                if (boot == 3) unlink(g2);
                if (boot == 4) unlink(r12_cache);

                nl_state_t *sb = (nl_state_t *)calloc(1, sizeof(nl_state_t));
                assert(sb != NULL);
                nl_init_state(sb);
                snprintf(sb->lib_path, sizeof(sb->lib_path), "%s", r12_lib);
                snprintf(sb->profiles_path, sizeof(sb->profiles_path), "%s", r12_prof);
                assert(nl_load_config(sb, r12_cfg) == true);
                assert(nl_load_profiles(sb, r12_prof) == true);
                assert(nl_load_library_index(sb, r12_lib) == true);
                assert(nl_transactional_scan(sb, r12_root, r12_self, true, false, NULL, NULL) == true);
                nl_profile_apply_active(sb);
                assert(sb->profiles.active_index == 1);
                assert(strcmp(sb->profiles.items[1].name, "Daniel") == 0);
                if (boot == 1) assert(sb->total_games == 2);
                if (boot == 2) assert(sb->total_games == 3 && sb->scan.games_added == 1);
                if (boot == 3) assert(sb->total_games == 2 && sb->scan.games_removed == 1);
                if (boot == 4) assert(sb->total_games == 2);
                assert(nl_save_library_index(sb, r12_lib) == true);
                nl_free_icons(sb);
                free(sb);
            }
        }
        printf("[PASS] #R12 Test F: 4 consecutive restarts + memorized folders + startup auto-scan add/remove verified.\n");

        /* --- TEST G: Error resilience (corrupt/malicious ICOs, INT32_MIN, overflow, corrupt lib/cache/cfg, missing dir) --- */
        {
            assert(system("mkdir -p /tmp/nl_r12_matrix_fs/ndless/icons") == 0);
            nl_state_t *stG = (nl_state_t *)calloc(1, sizeof(nl_state_t));
            assert(stG != NULL);
            nl_init_state(stG);
            snprintf(stG->covers_dir, sizeof(stG->covers_dir), "%s/ndless/covers", r12_root);
            snprintf(stG->icons_dir, sizeof(stG->icons_dir), "%s/ndless/icons", r12_root);
            nl_diag_init(stG, r12_root);

            /* G1. Missing saved folder -> logged cleanly, zero crash */
            nl_folder_add(&stG->folders, "/tmp/nl_r12_matrix_fs/DossierSupprimeInexistant");
            assert(nl_transactional_scan(stG, r12_root, r12_self, false, false, NULL, NULL) == true);
            assert(stG->total_games == 2);

            /* G2. Malicious ICO files (INT32_MIN height, uint32 offset overflow, invalid PNG bit depth, truncated) */
            const char *mal_ico = "/tmp/nl_r12_matrix_fs/ndless/icons/Alpha.ico.tns";
            /* Case 1: INT32_MIN height */
            {
                FILE *fp = fopen(mal_ico, "wb");
                assert(fp != NULL);
                uint8_t ihdr[22] = {0,0, 1,0, 1,0, 32,32, 0,0, 1,0, 32,0, 64,0,0,0, 22,0,0,0};
                fwrite(ihdr, 1, 22, fp);
                uint8_t bih[64];
                memset(bih, 0, sizeof(bih));
                bih[0] = 40; bih[4] = 32;
                bih[11] = 0x80; /* bi_h_signed = INT32_MIN (0x80000000) */
                bih[12] = 1; bih[14] = 32;
                fwrite(bih, 1, sizeof(bih), fp);
                fclose(fp);
                assert(nl_load_game_icon_diag(stG, &stG->games[0], stG->covers_dir) == false);
                assert(stG->games[0].icon_source == NL_ICON_NONE);
            }
            /* Case 2: uint32 img_offset + img_size overflow */
            {
                FILE *fp = fopen(mal_ico, "wb");
                assert(fp != NULL);
                uint8_t ihdr[22] = {0,0, 1,0, 1,0, 32,32, 0,0, 1,0, 32,0, 0x00,0x01,0x00,0x00, 0xF0,0xFF,0xFF,0xFF};
                fwrite(ihdr, 1, 22, fp);
                fclose(fp);
                assert(nl_load_game_icon_diag(stG, &stG->games[0], stG->covers_dir) == false);
            }
            /* Case 3: Invalid PNG-in-ICO (color_type=3 palette with bit_depth=16, illegal per RFC 2083) */
            {
                FILE *fp = fopen(mal_ico, "wb");
                assert(fp != NULL);
                uint8_t ihdr[22] = {0,0, 1,0, 1,0, 16,16, 0,0, 1,0, 32,0, 45,0,0,0, 22,0,0,0};
                fwrite(ihdr, 1, 22, fp);
                static const uint8_t bad_png[45] = {
                    0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A,
                    0,0,0,13,'I','H','D','R', 0,0,0,16, 0,0,0,16, 16, 3, 0, 0, 0, 0,0,0,0,
                    0,0,0,0,'I','E','N','D', 0,0,0,0
                };
                fwrite(bad_png, 1, sizeof(bad_png), fp);
                fclose(fp);
                assert(nl_load_game_icon_diag(stG, &stG->games[0], stG->covers_dir) == false);
            }
            /* Case 4: Truncated 5-byte ICO */
            {
                FILE *fp = fopen(mal_ico, "wb");
                assert(fp != NULL);
                fwrite("BAD!!", 1, 5, fp);
                fclose(fp);
                assert(nl_load_game_icon_diag(stG, &stG->games[0], stG->covers_dir) == false);
            }

            /* G3. Corrupt nlauncher.lib.tns -> rejected + .bak.tns created, zero crash */
            {
                FILE *fp = fopen(r12_lib, "wb");
                assert(fp != NULL);
                fprintf(fp, "CORRUPT_NLLB_DATA_12345\n");
                fclose(fp);
                assert(nl_load_library_index(stG, r12_lib) == false);
                assert(nl_file_exists("/tmp/nl_r12_matrix_fs/ndless/nlauncher.lib.bak.tns") == true);
            }

            /* G4. Zero-game nlauncher.cache.tns -> strictly rejected (never hides games on boot!) */
            {
                nl_cache_header_t zero_hdr;
                memset(&zero_hdr, 0, sizeof(zero_hdr));
                zero_hdr.magic = NL_CACHE_MAGIC;
                zero_hdr.version = NL_CACHE_VERSION;
                zero_hdr.total_games = 0U;
                zero_hdr.payload_bytes = 0U;
                zero_hdr.payload_crc32 = 0U;
                FILE *fp = fopen(r12_cache, "wb");
                assert(fp != NULL);
                fwrite(&zero_hdr, 1, sizeof(zero_hdr), fp);
                fclose(fp);
                assert(nl_load_library_cache(stG, r12_cache) == false);
            }

            nl_free_icons(stG);
            free(stG);
        }
        printf("[PASS] #R12 Test G: Full error resilience (malicious ICOs, INT32_MIN, overflow, corrupt lib & 0-game cache) verified.\n");
    }

    /* 34. #R13 Real Hardware Stability & Feature Matrix:
     *     - 78-game / 0-icon two-phase Nucleus-safe scan + instant warm boot from nlauncher.lib.tns
     *     - Non-game ROM/data .tns fast-skip before stat()/fopen()
     *     - Early ESC abort during scan & icon loading (zero leaks, existing library untouched)
     *     - Mode Souris physical click (anywhere, no hover/center gate) & tap-to-click (anywhere on pad)
     *     - Global TI-OS Hotkey get_event+0 ARM PC-rel LDR relocation + sticky Shift/Ctrl + Menu */
    {
        const char *r13_root = "/tmp/nl_r13_78games_fs";
        const char *r13_self = "/tmp/nl_r13_78games_fs/ndless/N-Launcher.tns";
        const char *r13_lib  = "/tmp/nl_r13_78games_fs/ndless/nlauncher.lib.tns";
        int mk_r13 = system("rm -rf /tmp/nl_r13_78games_fs && mkdir -p /tmp/nl_r13_78games_fs/ndless/icons /tmp/nl_r13_78games_fs/ndless/covers /tmp/nl_r13_78games_fs/games/arcade /tmp/nl_r13_78games_fs/games/rpg /tmp/nl_r13_78games_fs/games/puzzle /tmp/nl_r13_78games_fs/roms /tmp/nl_r13_78games_fs/homebrew");
        assert(mk_r13 == 0);
        create_mock_zehn_tns(r13_self, "N-Launcher", "daniel.b", 3, true, true, 128);

        /* Populate 78 genuine games across 5 folders + 0 custom icons + 0 covers */
        static const char *r13_dirs[5] = {
            "/tmp/nl_r13_78games_fs/games/arcade",
            "/tmp/nl_r13_78games_fs/games/rpg",
            "/tmp/nl_r13_78games_fs/games/puzzle",
            "/tmp/nl_r13_78games_fs/roms",
            "/tmp/nl_r13_78games_fs/homebrew"
        };
        for (int g = 0; g < 78; g++) {
            char gp[256], gt[48];
            snprintf(gp, sizeof(gp), "%s/Game_%02d.tns", r13_dirs[g % 5], g + 1);
            snprintf(gt, sizeof(gt), "Game %02d", g + 1);
            create_mock_zehn_tns(gp, gt, "Author", 1, true, false, 128);
        }
        /* Add 12 non-game emulator ROM / save / text .tns files that must be fast-skipped */
        static const char *rom_names[12] = {
            "mario.gba.tns", "zelda.gbc.tns", "tetris.gb.tns", "metroid.nes.tns",
            "chrono.smc.tns", "dkc.sfc.tns", "oot.z64.tns", "nintendogs.nds.tns",
            "zelda.srm.tns", "save1.sav.tns", "slot0.state.tns", "readme.txt.tns"
        };
        for (int r = 0; r < 12; r++) {
            char rp[256];
            snprintf(rp, sizeof(rp), "/tmp/nl_r13_78games_fs/roms/%s", rom_names[r]);
            create_mock_ti_doc_tns(rp);
        }

        /* 34a. Cold two-phase transactional scan of 78 games with 0 icons */
        nl_state_t *st78 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(st78 != NULL);
        nl_init_state(st78);
        snprintf(st78->sys.docs_root, sizeof(st78->sys.docs_root), "%s", r13_root);
        snprintf(st78->sys.self_path, sizeof(st78->sys.self_path), "%s", r13_self);
        snprintf(st78->covers_dir, sizeof(st78->covers_dir), "%s/ndless/covers", r13_root);
        snprintf(st78->icons_dir, sizeof(st78->icons_dir), "%s/ndless/icons", r13_root);
        snprintf(st78->lib_path, sizeof(st78->lib_path), "%s", r13_lib);
        nl_folder_seed_defaults(&st78->folders, r13_root);

        assert(nl_transactional_scan(st78, r13_root, r13_self, true, false, NULL, NULL) == true);
        assert(st78->total_games == 78);
        assert(st78->scan.finished == true);
        assert(st78->scan.abort_requested == false);

        /* Load icons (0 custom icons -> 0 icon RAM, 78 monograms, zero fopen crashes) */
        nl_load_all_icons_ex(st78, NULL, NULL);
        for (int i = 0; i < 78; i++) {
            assert(st78->games[i].icon_source == NL_ICON_NONE);
            assert(st78->games[i].icon_pixels == NULL);
            assert(st78->games[i].scr_pixels == NULL);
        }
        assert(nl_save_library_index(st78, r13_lib) == true);

        /* 34b. Instant warm boot from nlauncher.lib.tns (zero disk rescan!) */
        nl_state_t *warm78 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(warm78 != NULL);
        nl_init_state(warm78);
        snprintf(warm78->sys.docs_root, sizeof(warm78->sys.docs_root), "%s", r13_root);
        snprintf(warm78->sys.self_path, sizeof(warm78->sys.self_path), "%s", r13_self);
        snprintf(warm78->covers_dir, sizeof(warm78->covers_dir), "%s/ndless/covers", r13_root);
        snprintf(warm78->icons_dir, sizeof(warm78->icons_dir), "%s/ndless/icons", r13_root);
        snprintf(warm78->lib_path, sizeof(warm78->lib_path), "%s", r13_lib);

        assert(nl_load_library_index(warm78, r13_lib) == true);
        assert(warm78->total_games == 78);
        assert(warm78->folders.count >= 5);
        nl_load_all_icons_ex(warm78, NULL, NULL);
        nl_rebuild_filter(warm78);
        assert(warm78->filtered_count == 78);
        nl_render_frame(warm78, fb);

        /* 34c. Early ESC abort mid-scan & during icon loading leaves existing 78 games intact & zero leaks */
        assert(nl_transactional_scan(warm78, r13_root, r13_self, true, false, test_abort_after_5_cb, warm78) == false);
        assert(warm78->scan.abort_requested == true);
        assert(warm78->total_games == 78);
        nl_load_all_icons_ex(warm78, NULL, NULL);
        assert(warm78->total_games == 78);
        warm78->scan.abort_requested = false;

        /* 34d. Mode Souris #R13: physical click works everywhere & tap-to-click works anywhere on pad */
        {
            nl_tp_click_guard_t mg;
            nl_tp_click_reset(&mg);
            /* Physical click at corner (0,0) with hover_active=false MUST fire immediately */
            assert(nl_mouse_tp_poll(&mg, false, true, false, false, false, 0, 0, 2328, 1691) == NL_TP_ACT_PHYSICAL_CLICK);
            /* Holding button down does not double-fire */
            assert(nl_mouse_tp_poll(&mg, false, true, false, false, false, 0, 0, 2328, 1691) == NL_TP_ACT_NONE);
            /* Release button after physical click does NOT fire a spurious tap */
            assert(nl_mouse_tp_poll(&mg, false, false, false, false, true, 0, 0, 2328, 1691) == NL_TP_ACT_NONE);

            /* Wait out cooldown */
            for (int c = 0; c < NL_TP_CLICK_COOLDOWN_TICKS + 2; c++) {
                assert(nl_mouse_tp_poll(&mg, false, false, false, false, true, 0, 0, 2328, 1691) == NL_TP_ACT_NONE);
            }

            /* Capacitive tap at touchpad corner (120, 120) with gentle 60-unit wobble -> MUST fire TAP_CLICK */
            assert(nl_mouse_tp_poll(&mg, true, false, false, false, true, 120, 120, 2328, 1691) == NL_TP_ACT_NONE);
            assert(nl_mouse_tp_poll(&mg, true, false, false, false, true, 180, 150, 2328, 1691) == NL_TP_ACT_NONE);
            assert(nl_mouse_tp_poll(&mg, false, false, false, false, true, 180, 150, 2328, 1691) == NL_TP_ACT_TAP_CLICK);

            /* Cooldown elapsed -> slide > NL_TP_TAP_SLIDE_CANCEL (150) cancels tap */
            for (int c = 0; c < NL_TP_TAP_COOLDOWN_TICKS + 2; c++) {
                assert(nl_mouse_tp_poll(&mg, false, false, false, false, true, 0, 0, 2328, 1691) == NL_TP_ACT_NONE);
            }
            assert(nl_mouse_tp_poll(&mg, true, false, false, false, true, 500, 500, 2328, 1691) == NL_TP_ACT_NONE);
            assert(nl_mouse_tp_poll(&mg, true, false, false, false, true, 1200, 500, 2328, 1691) == NL_TP_ACT_NONE);
            assert(nl_mouse_tp_poll(&mg, false, false, false, false, true, 1200, 500, 2328, 1691) == NL_TP_ACT_NONE);

            /* Long hold > NL_TP_TAP_MAX_TICKS (26) cancels tap */
            for (int t = 0; t < NL_TP_TAP_MAX_TICKS + 2; t++) {
                assert(nl_mouse_tp_poll(&mg, true, false, false, false, true, 900, 900, 2328, 1691) == NL_TP_ACT_NONE);
            }
            assert(nl_mouse_tp_poll(&mg, false, false, false, false, true, 900, 900, 2328, 1691) == NL_TP_ACT_NONE);

            /* 2px forgiving margin on game card hit testing */
            warm78->settings.touchpad_mode = NL_TP_MODE_MOUSE;
            warm78->sui.screen = NL_UI_LIBRARY;
            nl_layout_metrics_t lm13;
            nl_get_layout_metrics_for_count(warm78->settings.layout, warm78->settings.show_names,
                                            warm78->settings.show_shortcuts, warm78->filtered_count, &lm13);
            int sx0 = 0, sy0 = 0;
            nl_compute_grid_slot_pos(&lm13, warm78->filtered_count, 0, &sx0, &sy0);
            nl_mouse_move_to(warm78, sx0 - 1, sy0);
            assert(warm78->sui.hover_valid == true);
            assert(warm78->sui.hover_kind == NL_HOVER_GAME_CARD);
            assert(warm78->sui.hover_index == 0);
        }

        /* 34e. Global TI-OS Hotkey #R13: ARM get_event+0 PC-rel LDR relocation + sticky Shift/Ctrl + Menu */
        {
            /* Mock get_event prologue with PC-relative LDR at word 1:
             *   word 0: PUSH {r4-r7, lr}     = 0xE92D40F0
             *   word 1: LDR r4, [pc, #0x10]  = 0xE59F4010 -> literal sits at word (1 + 2 + 4) = word 7
             *   word 7: 0x11223344 */
            uint32_t mock_fn[8] = {
                0xE92D40F0U,
                0xE59F4010U,
                0xE1A05000U,
                0xE12FFF1EU,
                0U, 0U, 0U,
                0x11223344U
            };
            assert(nl_arm_is_pc_rel_ldr(mock_fn[0]) == false);
            assert(nl_arm_is_pc_rel_ldr(mock_fn[1]) == true);
            assert(nl_arm_instr_is_relocatable(mock_fn[0]) == true);
            assert(nl_arm_instr_is_relocatable(mock_fn[1]) == true);

            uint32_t tramp[6];
            memset(tramp, 0, sizeof(tramp));
            assert(nl_arm_relocate_prologue((uintptr_t)mock_fn, mock_fn[0], mock_fn[1], NULL, NULL, tramp) == true);
            assert(tramp[0] == mock_fn[0]);
            /* tramp[1] rewritten to LDR r4, [pc, #8] -> pc=(&tramp[1]+8)=&tramp[3], +8=&tramp[5]! */
            assert(tramp[1] == ((0xE59F4010U & 0xFF7FF000U) | 0x00800008U));
            assert(tramp[2] == 0xE51FF004U); /* ldr pc, [pc, #-4] */
            assert(tramp[3] == (uint32_t)(uintptr_t)&mock_fn[2]);
            assert(tramp[5] == 0x11223344U);

            /* Sticky & simultaneous Shift/Ctrl + Menu state machine */
            nl_hk_sticky_state_t sk;
            nl_hk_sticky_reset(&sk);
            /* Simultaneous Shift + Menu triggers immediately */
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_SHIFT_MENU, false, true, true, false) == true);
            /* Sticky Shift press -> release -> Menu within window triggers */
            nl_hk_sticky_reset(&sk);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_SHIFT_MENU, false, true, false, false) == false);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_SHIFT_MENU, false, false, false, false) == false);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_SHIFT_MENU, false, false, true, false) == true);
            /* Sticky Shift cancelled by another key does NOT trigger Menu */
            nl_hk_sticky_reset(&sk);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_SHIFT_MENU, false, true, false, false) == false);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_SHIFT_MENU, false, false, false, true) == false);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_SHIFT_MENU, false, false, true, false) == false);
            /* Sticky Ctrl + Menu works for NL_HK_COMBO_CTRL_MENU */
            nl_hk_sticky_reset(&sk);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_CTRL_MENU, true, false, false, false) == false);
            assert(nl_hk_sticky_eval(&sk, NL_HK_COMBO_CTRL_MENU, false, false, true, false) == true);
        }

        nl_free_icons(st78);
        nl_free_icons(warm78);
        free(st78);
        free(warm78);
        printf("[PASS] Section 34 #R13 (78-game/0-icon warm boot & two-phase scan, early ESC abort, Mode Souris physical/tap clicks & get_event+0 ARM relocation/sticky hotkey) verified.\n");
    }

    /* =====================================================================
     * 35. #R14 POST-R13 REFINEMENT SUITE:
     *     - Priority 1: Non-blocking incremental nl_scan_job_t + ESC & Annuler button cancel
     *     - Priority 2: Smooth 78-game page transitions (no 31% jump, no v_settled snap, wrap clamp)
     *     - Priority 3: Perceptible Settings open (~333ms) & close (~267ms) durations
     *     - Priority 4: Optimized 4-stage launch zoom + 3 pure-white ripples + .scr crossfade
     *     - Priority 5: Direct native .png.tns / .png icon decoding, 84x84 downscale & no-upscale guard
     * ===================================================================== */
    {
        /* Helper lambda-style PNG writer (valid uncompressed zlib-stored PNG: RGB color_type=2 or RGBA color_type=6) */
        const char *r14_root = "/tmp/nl_test_r14";
        const char *r14_docs = "/tmp/nl_test_r14/documents";
        const char *r14_games = "/tmp/nl_test_r14/documents/games";
        const char *r14_ndless = "/tmp/nl_test_r14/documents/ndless";
        const char *r14_icons = "/tmp/nl_test_r14/documents/ndless/icons";
        const char *r14_covers = "/tmp/nl_test_r14/documents/ndless/covers";
        (void)mkdir(r14_root, 0777);
        (void)mkdir(r14_docs, 0777);
        (void)mkdir(r14_games, 0777);
        (void)mkdir(r14_ndless, 0777);
        (void)mkdir(r14_icons, 0777);
        (void)mkdir(r14_covers, 0777);

        /* Write 78 valid Zehn .tns executables */
        for (int g = 0; g < 78; g++) {
            char gp[256], gt[48];
            snprintf(gp, sizeof(gp), "%s/r14_game_%02d.tns", r14_games, g + 1);
            snprintf(gt, sizeof(gt), "R14 Game %02d", g + 1);
            create_mock_zehn_tns(gp, gt, "Author", 1, true, false, 128);
        }

        /* Write 3 direct native PNG icons:
         *  1) r14_game_01.png.tns -> 128x128 RGBA (downscales cleanly to 84x84, icon_low_res == false)
         *  2) r14_game_02.png     -> 160x80  RGB  (aspect preserved -> 84x42, icon_low_res == false)
         *  3) r14_game_03.png.tns -> 16x16   RGBA (low-res -> stays 16x16, NO upscaling, icon_low_res == true) */
        struct {
            const char *path;
            int w;
            int h;
            int ctype; /* 2=RGB, 6=RGBA */
            uint8_t r, g, b;
        } png_specs[3] = {
            { "/tmp/nl_test_r14/documents/ndless/icons/r14_game_01.png.tns", 128, 128, 6,  30, 144, 255 },
            { "/tmp/nl_test_r14/documents/ndless/icons/r14_game_02.png",     160,  80, 2,  40, 210, 110 },
            { "/tmp/nl_test_r14/documents/ndless/icons/r14_game_03.png.tns",  16,  16, 6, 240,  70,  60 }
        };
        for (int pi = 0; pi < 3; pi++) {
            int pw = png_specs[pi].w;
            int ph = png_specs[pi].h;
            int bpp = (png_specs[pi].ctype == 6) ? 4 : 3;
            size_t row_sz = (size_t)(1 + pw * bpp);
            size_t raw_sz = row_sz * (size_t)ph;
            uint8_t *raw = (uint8_t *)malloc(raw_sz);
            assert(raw != NULL);
            for (int y = 0; y < ph; y++) {
                uint8_t *rptr = raw + (size_t)y * row_sz;
                rptr[0] = 0; /* filter None */
                for (int x = 0; x < pw; x++) {
                    uint8_t *p = rptr + 1 + x * bpp;
                    p[0] = png_specs[pi].r;
                    p[1] = png_specs[pi].g;
                    p[2] = png_specs[pi].b;
                    if (bpp == 4) p[3] = 255;
                }
            }
            /* Build zlib stream using stored deflate blocks (max 65535 bytes/block) */
            size_t n_blocks = (raw_sz + 65534U) / 65535U;
            size_t z_sz = 2U + raw_sz + n_blocks * 5U + 4U;
            uint8_t *zbuf = (uint8_t *)malloc(z_sz);
            assert(zbuf != NULL);
            size_t zp = 0;
            zbuf[zp++] = 0x78;
            zbuf[zp++] = 0x01;
            uint32_t s1 = 1U, s2 = 0U;
            for (size_t i = 0; i < raw_sz; i++) {
                s1 = (s1 + raw[i]) % 65521U;
                s2 = (s2 + s1) % 65521U;
            }
            size_t off = 0;
            while (off < raw_sz) {
                size_t chunk = raw_sz - off;
                if (chunk > 65535U) chunk = 65535U;
                bool final_blk = (off + chunk >= raw_sz);
                zbuf[zp++] = final_blk ? 0x01 : 0x00;
                zbuf[zp++] = (uint8_t)(chunk & 0xFFU);
                zbuf[zp++] = (uint8_t)((chunk >> 8) & 0xFFU);
                uint16_t nlen = (uint16_t)(~chunk);
                zbuf[zp++] = (uint8_t)(nlen & 0xFFU);
                zbuf[zp++] = (uint8_t)((nlen >> 8) & 0xFFU);
                memcpy(zbuf + zp, raw + off, chunk);
                zp += chunk;
                off += chunk;
            }
            uint32_t adler = (s2 << 16) | s1;
            zbuf[zp++] = (uint8_t)((adler >> 24) & 0xFFU);
            zbuf[zp++] = (uint8_t)((adler >> 16) & 0xFFU);
            zbuf[zp++] = (uint8_t)((adler >> 8) & 0xFFU);
            zbuf[zp++] = (uint8_t)(adler & 0xFFU);

            FILE *pfp = fopen(png_specs[pi].path, "wb");
            assert(pfp != NULL);
            static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            fwrite(png_sig, 1, 8, pfp);
            /* IHDR chunk */
            uint8_t ihdr[13] = {
                (uint8_t)(pw >> 24), (uint8_t)(pw >> 16), (uint8_t)(pw >> 8), (uint8_t)pw,
                (uint8_t)(ph >> 24), (uint8_t)(ph >> 16), (uint8_t)(ph >> 8), (uint8_t)ph,
                8, (uint8_t)png_specs[pi].ctype, 0, 0, 0
            };
            uint8_t len4[4] = {0, 0, 0, 13};
            uint8_t zero4[4] = {0, 0, 0, 0};
            fwrite(len4, 1, 4, pfp); fwrite("IHDR", 1, 4, pfp); fwrite(ihdr, 1, 13, pfp); fwrite(zero4, 1, 4, pfp);
            /* IDAT chunk */
            uint8_t zlen4[4] = {
                (uint8_t)(zp >> 24), (uint8_t)(zp >> 16), (uint8_t)(zp >> 8), (uint8_t)zp
            };
            fwrite(zlen4, 1, 4, pfp); fwrite("IDAT", 1, 4, pfp); fwrite(zbuf, 1, zp, pfp); fwrite(zero4, 1, 4, pfp);
            /* IEND chunk */
            fwrite(zero4, 1, 4, pfp); fwrite("IEND", 1, 4, pfp); fwrite(zero4, 1, 4, pfp);
            fclose(pfp);
            free(zbuf);
            free(raw);
        }

        nl_state_t *st14 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(st14 != NULL);
        nl_init_state(st14);
        snprintf(st14->icons_dir, sizeof(st14->icons_dir), "%s", r14_icons);
        snprintf(st14->covers_dir, sizeof(st14->covers_dir), "%s", r14_covers);

        /* 35a. Run full incremental non-blocking scan job to completion */
        nl_scan_job_t *job1 = nl_scan_job_start(st14, r14_docs, "/documents/ndless/N-Launcher.tns");
        assert(job1 != NULL);
        assert(st14->scan.active == true);
        assert(st14->scan.finished == false);
        int steps_taken = 0;
        int prev_pct = -1;
        while (nl_scan_job_step(job1, st14, 3)) {
            steps_taken++;
            assert(st14->scan.progress_pct >= prev_pct);
            prev_pct = st14->scan.progress_pct;
            assert(steps_taken < 500);
        }
        assert(steps_taken >= 15); /* Truly incremental across many small frames */
        assert(nl_scan_job_is_done(job1) == true);
        assert(nl_scan_job_was_cancelled(job1) == false);
        nl_scan_job_destroy(job1);
        assert(st14->total_games == 78);
        assert(st14->scan.active == false);
        assert(st14->scan.finished == true);
        assert(st14->scan.progress_pct == 100);

        /* Verify Priority 5: direct native PNG icons decoded cleanly */
        int found_png1 = -1, found_png2 = -1, found_png3 = -1;
        for (int i = 0; i < st14->total_games; i++) {
            if (strcmp(st14->games[i].filename, "r14_game_01.tns") == 0) found_png1 = i;
            if (strcmp(st14->games[i].filename, "r14_game_02.tns") == 0) found_png2 = i;
            if (strcmp(st14->games[i].filename, "r14_game_03.tns") == 0) found_png3 = i;
        }
        assert(found_png1 >= 0 && found_png2 >= 0 && found_png3 >= 0);
        assert(st14->games[found_png1].icon_source == NL_ICON_PNG);
        assert(st14->games[found_png1].icon_w == NL_ICON_MAX_DIM && st14->games[found_png1].icon_h == NL_ICON_MAX_DIM);
        assert(st14->games[found_png1].icon_low_res == false);
        assert(st14->games[found_png2].icon_source == NL_ICON_PNG);
        assert(st14->games[found_png2].icon_w == NL_ICON_MAX_DIM && st14->games[found_png2].icon_h == 42);
        assert(st14->games[found_png2].icon_low_res == false);
        assert(st14->games[found_png3].icon_source == NL_ICON_PNG);
        assert(st14->games[found_png3].icon_w == 16 && st14->games[found_png3].icon_h == 16); /* NO upscaling! */
        assert(st14->games[found_png3].icon_low_res == true);
        /* Verify 78 native 84x84 RGBA icons fit safely within 2 MB NL_ICON_RAM_BUDGET */
        assert((size_t)78U * (size_t)NL_ICON_MAX_DIM * (size_t)NL_ICON_MAX_DIM * 3U < (size_t)NL_ICON_RAM_BUDGET);

        /* 35a-ii. Mid-scan Cancel via Mouse « Annuler » click AND ESC preserves old library intact */
        st14->games[found_png1].is_favorite = true;
        st14->games[found_png1].launch_count = 42;
        st14->sui.screen = NL_UI_SETTINGS_RESCAN;
        st14->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        nl_scan_job_t *job2 = nl_scan_job_start(st14, r14_docs, "/documents/ndless/N-Launcher.tns");
        assert(job2 != NULL);
        for (int s = 0; s < 5; s++) {
            assert(nl_scan_job_step(job2, st14, 3) == true);
        }
        assert(st14->scan.active == true);
        /* Render active rescan screen with Annuler button */
        nl_render_frame(st14, fb);
        /* Mouse hover + click on visible "Annuler (ESC)" button (96..224, 189..211) */
        nl_mouse_move_to(st14, 160, 198);
        assert(st14->sui.hover_valid == true);
        assert(st14->sui.hover_kind == NL_HOVER_MODAL_BTN);
        assert(st14->sui.hover_index == 0);
        assert(nl_mouse_click(st14) == true);
        assert(st14->scan.cancel_requested == true);
        /* Also verify ESC key triggers cancel_requested */
        st14->scan.cancel_requested = false;
        assert(nl_handle_key(st14, NL_KEY_ESC) == true);
        assert(st14->scan.cancel_requested == true);
        /* Execute safe cancel & verify old 78-game library + metadata are 100% preserved */
        nl_scan_job_cancel(job2, st14);
        assert(nl_scan_job_was_cancelled(job2) == true);
        nl_scan_job_destroy(job2);
        assert(st14->scan.active == false);
        assert(st14->total_games == 78);
        assert(st14->games[found_png1].is_favorite == true);
        assert(st14->games[found_png1].launch_count == 42);
        assert(st14->games[found_png1].icon_source == NL_ICON_PNG && st14->games[found_png1].icon_pixels != NULL);

        /* 35b. Priority 2: Smooth 78-game page transitions (no frame-1 31% jump, monotonic, wrap clamp) */
        st14->sui.screen = NL_UI_LIBRARY;
        st14->anim.set_open_fp = 0;
        st14->selected_pos = 0;
        nl_rebuild_filter(st14);
        assert(st14->filtered_count == 78);
        assert(st14->total_pages == 13);
        int start_scroll = st14->anim.scroll_y_fp;
        assert(nl_handle_key(st14, NL_KEY_PAGE_NEXT) == true);
        assert(st14->current_page == 1);
        int target_scroll = st14->anim.target_scroll_y_fp;
        int full_stride = target_scroll - start_scroll;
        assert(full_stride > 0);
        nl_anim_step(st14);
        int step1_delta = st14->anim.scroll_y_fp - start_scroll;
        /* Frame-1 step must be smooth (~10.7%), strictly < 16% (never the old 31% teleport!) */
        assert(step1_delta > 0 && step1_delta * 100 < full_stride * 16);
        int prev_scroll = st14->anim.scroll_y_fp;
        int scroll_frames = 1;
        while (st14->anim.scroll_y_fp != target_scroll && scroll_frames < 30) {
            nl_anim_step(st14);
            assert(st14->anim.scroll_y_fp >= prev_scroll);
            prev_scroll = st14->anim.scroll_y_fp;
            scroll_frames++;
        }
        assert(st14->anim.scroll_y_fp == target_scroll);
        assert(scroll_frames >= 12 && scroll_frames <= 16);

        /* Rapid consecutive page switch + wrap-around (page 0 -> page 12 on 78 games) */
        assert(nl_handle_key(st14, NL_KEY_PAGE_PREV) == true); /* back to page 0 */
        for (int k = 0; k < 20; k++) nl_anim_step(st14);
        assert(st14->current_page == 0);
        assert(nl_handle_key(st14, NL_KEY_LEFT) == true); /* wrap to last game (page 12) */
        assert(st14->current_page == 12);
        nl_anim_step(st14);
        /* Visual entry distance must be clamped to <= 1.6 pages instead of 12 full pages */
        int wrap_dist = st14->anim.target_scroll_y_fp - st14->anim.scroll_start_y_fp;
        assert(wrap_dist > 0 && wrap_dist <= (full_stride * 16) / 10);
        for (int k = 0; k < 20; k++) {
            nl_anim_step(st14);
            nl_render_frame(st14, fb);
        }
        assert(st14->anim.scroll_y_fp == st14->anim.target_scroll_y_fp);

        /* 35c. Priority 3: Perceptible Settings open (~333 ms = 20 frames) & close (~267 ms = 16 frames) */
        st14->anim.set_open_fp = 0;
        assert(nl_handle_key(st14, NL_KEY_SETTINGS) == true);
        assert(st14->sui.screen == NL_UI_SETTINGS_MAIN);
        assert(st14->anim.set_open_fp == 8);
        int open_frames = 0;
        while (st14->anim.set_open_fp < 256 && open_frames < 40) {
            nl_anim_step(st14);
            open_frames++;
        }
        assert(st14->anim.set_open_fp == 256);
        assert(open_frames >= 18 && open_frames <= 22); /* ~300..367 ms at 60 FPS */

        assert(nl_handle_key(st14, NL_KEY_ESC) == true);
        assert(st14->sui.screen == NL_UI_LIBRARY);
        assert(st14->anim.set_open_fp == 248);
        int close_frames = 0;
        while (st14->anim.set_open_fp > 0 && close_frames < 40) {
            nl_anim_step(st14);
            close_frames++;
        }
        assert(st14->anim.set_open_fp == 0);
        assert(close_frames >= 14 && close_frames <= 18); /* ~233..300 ms at 60 FPS */

        /* 35d. Priority 4: Optimized 4-Stage Launch Animation with 3 Pure-White Ripples + .scr Crossfade */
        st14->selected_pos = 0;
        nl_rebuild_filter(st14);
        int lidx = st14->filtered_indices[0];
        st14->launch_game_idx = lidx;
        st14->anim.launch_anim_active = true;
        st14->games[lidx].scr_pixels = (uint16_t *)malloc(NL_COVER_W * NL_COVER_H * sizeof(uint16_t));
        assert(st14->games[lidx].scr_pixels != NULL);
        for (int p = 0; p < NL_COVER_W * NL_COVER_H; p++) {
            st14->games[lidx].scr_pixels[p] = RGB565(24, 210, 120);
        }
        st14->games[lidx].has_screenshot = true;

        for (int z = 1; z <= 46; z++) {
            nl_render_launch_zoom(st14, fb, z, 46);
        }
        /* Final frame (step 46/46) must have expanded screenshot cleanly across fullscreen center */
        uint16_t center_px = fb[(NL_SCREEN_H / 2) * NL_SCREEN_W + (NL_SCREEN_W / 2)];
        assert(center_px == RGB565(24, 210, 120));
        st14->anim.launch_anim_active = false;
        nl_renderer_cleanup();

        nl_free_icons(st14);
        free(st14);
        printf("[PASS] Section 35 #R14 (non-blocking cancellable rescan, smooth 78-game page transitions, Settings ~333ms/267ms open/close, optimized 4-stage launch zoom & direct PNG icons) verified.\n");
    }

    /* =====================================================================
     * 36. #R15 OVERHAUL SUITE:
     *     (1) Bulletproof .scr.tns preservation, non-blank validation & 3-case launch crossfade
     *     (2) Minimalist Quick Profile Selector (zero ACT./counter/names/footer)
     *     (3) Controlled '+' animation & compact floating Avatar Picker modal
     *     (4) Padlock profile protection system (global toggle + per-profile lock)
     *     (5) Reassigned '0' & '-' keys for previous/next game page
     *     (6) Vertical vs Horizontal page scroll direction + 12px adjacent peek
     *     (7) High-FPS launch zoom across Dark/Light/Gray themes
     * ===================================================================== */
    {
        assert(strstr(NL_BUILD_STAMP_STR, "#R23") != NULL);

        const char *r15_root = "/tmp/nl_r15_suite_fs";
        assert(system("rm -rf /tmp/nl_r15_suite_fs && mkdir -p /tmp/nl_r15_suite_fs/ndless/covers /tmp/nl_r15_suite_fs/ndless/icons /tmp/nl_r15_suite_fs/games") == 0);

        nl_state_t *s15 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s15 != NULL);
        nl_init_state(s15);
        snprintf(s15->sys.docs_root, sizeof(s15->sys.docs_root), "%s", r15_root);
        snprintf(s15->covers_dir, sizeof(s15->covers_dir), "%s/ndless/covers", r15_root);
        snprintf(s15->icons_dir, sizeof(s15->icons_dir), "%s/ndless/icons", r15_root);
        snprintf(s15->profiles_path, sizeof(s15->profiles_path), "%s/ndless/nlauncher.profiles.tns", r15_root);

        /* Populate 18 games (3 pages in 3x2) */
        for (int g = 0; g < 18; g++) {
            char gp[256], gt[64];
            snprintf(gp, sizeof(gp), "%s/games/Game%02d.tns", r15_root, g + 1);
            snprintf(gt, sizeof(gt), "Game %02d", g + 1);
            create_mock_zehn_tns(gp, gt, "Author", 1, true, false, 0);
        }
        assert(nl_scan_directory(s15, "/tmp/nl_r15_suite_fs/games", "/tmp/nl_r15_suite_fs/ndless/N-Launcher.tns", 3) == 18);
        nl_rebuild_filter(s15);
        assert(s15->total_games == 18);
        assert(s15->total_pages == 3);

        /* 36a. Screenshot validation & bulletproof preservation */
        uint16_t *blank_buf = (uint16_t *)calloc(NL_COVER_PIXELS, sizeof(uint16_t));
        uint16_t *solid_buf = (uint16_t *)malloc(NL_COVER_BYTES);
        uint16_t *scene_buf = (uint16_t *)malloc(NL_COVER_BYTES);
        assert(blank_buf && solid_buf && scene_buf);
        for (int p = 0; p < NL_COVER_PIXELS; p++) {
            solid_buf[p] = RGB565(40, 40, 40);
            int x = p % NL_COVER_W;
            int y = p / NL_COVER_W;
            scene_buf[p] = RGB565((x * 255) / NL_COVER_W, (y * 255) / NL_COVER_H, 160);
        }
        assert(nl_screenshot_is_valid_buffer(blank_buf) == false);
        assert(nl_screenshot_is_valid_buffer(solid_buf) == false);
        assert(nl_screenshot_is_valid_buffer(scene_buf) == true);

        /* Attempting to save a blank capture must fail cleanly */
        s15->games[0].scr_pixels = blank_buf;
        s15->games[0].has_screenshot = true;
        assert(nl_save_captured_screenshot(&s15->games[0], s15->covers_dir) == false);
        assert(nl_game_has_valid_screenshot_on_disk(&s15->games[0], s15->covers_dir) == false);

        /* Save genuine scene screenshot via atomic .tmp.tns -> rename */
        s15->games[0].scr_pixels = scene_buf;
        s15->games[0].has_screenshot = true;
        assert(nl_save_captured_screenshot(&s15->games[0], s15->covers_dir) == true);
        assert(nl_game_has_valid_screenshot_on_disk(&s15->games[0], s15->covers_dir) == true);

        /* Overwrite attempt with blank/corrupt capture MUST preserve existing valid screenshot on disk! */
        s15->games[0].scr_pixels = blank_buf;
        s15->games[0].has_screenshot = true;
        assert(nl_save_captured_screenshot(&s15->games[0], s15->covers_dir) == false);
        assert(nl_game_has_valid_screenshot_on_disk(&s15->games[0], s15->covers_dir) == true);
        s15->games[0].scr_pixels = NULL;

        /* O(1) probe marks game 0 has_screenshot=true, game 1 has_screenshot=false, without loading RAM */
        nl_free_icons(s15);
        s15->games[0].has_screenshot = false;
        nl_probe_all_screenshots(s15);
        assert(s15->games[0].has_screenshot == true);
        assert(s15->games[0].scr_pixels == NULL);
        assert(s15->games[1].has_screenshot == false);

        /* On-demand load succeeds and matches saved pixels */
        assert(nl_load_game_screenshot(&s15->games[0], s15->covers_dir) == true);
        assert(s15->games[0].scr_pixels != NULL);
        assert(s15->games[0].scr_pixels[0] == scene_buf[0]);
        assert(s15->games[0].scr_pixels[NL_COVER_PIXELS / 2] == scene_buf[NL_COVER_PIXELS / 2]);

        /* 36b. Unified 3-Case Launch Crossfade across Dark, Light, Gray themes */
        /* Case 1: WITH screenshot -> final frame shows real screenshot */
        s15->launch_game_idx = 0;
        s15->anim.launch_anim_active = true;
        for (int f = 1; f <= 30; f++) {
            nl_render_launch_zoom(s15, fb, f, 30);
        }
        /* Exact top-left + bilinear-upscaled center & bottom-right match real screenshot (not theme backdrop) */
        assert(fb[0] == scene_buf[0]);
        {
            const uint16_t pairs[2][2] = {
                { fb[(NL_SCREEN_H / 2) * NL_SCREEN_W + (NL_SCREEN_W / 2)], scene_buf[(NL_COVER_H / 2) * NL_COVER_W + (NL_COVER_W / 2)] },
                { fb[NL_FB_PIXELS - 1], scene_buf[NL_COVER_PIXELS - 1] }
            };
            for (int k = 0; k < 2; k++) {
                uint16_t got_c = pairs[k][0];
                uint16_t exp_c = pairs[k][1];
                int dr = (int)((got_c >> 11) & 0x1F) - (int)((exp_c >> 11) & 0x1F);
                int dg = (int)((got_c >> 5) & 0x3F) - (int)((exp_c >> 5) & 0x3F);
                int db = (int)(got_c & 0x1F) - (int)(exp_c & 0x1F);
                assert(dr >= -2 && dr <= 2 && dg >= -2 && dg <= 2 && db >= -2 && db <= 2);
                assert(got_c != nl_get_theme(s15->display_mode, s15->accent_color)->bg_main);
            }
        }
        s15->anim.launch_anim_active = false;
        nl_renderer_cleanup();

        /* Case 2: Custom icon WITHOUT screenshot -> NEVER stretched; fades into clean theme backdrop bg_main */
        s15->games[1].icon_pixels = (uint16_t *)malloc((size_t)(NL_ICON_V1_W * NL_ICON_V1_H) * sizeof(uint16_t));
        assert(s15->games[1].icon_pixels != NULL);
        for (int p = 0; p < NL_ICON_V1_W * NL_ICON_V1_H; p++) {
            s15->games[1].icon_pixels[p] = RGB565(250, 30, 30); /* Bright red custom icon */
        }
        s15->games[1].icon_w = NL_ICON_V1_W;
        s15->games[1].icon_h = NL_ICON_V1_H;
        s15->games[1].icon_source = NL_ICON_ICO;
        s15->games[1].has_screenshot = false;
        s15->games[1].scr_pixels = NULL;

        for (int dm = 0; dm < 3; dm++) {
            s15->display_mode = (nl_display_mode_t)dm;
            const nl_theme_t *th = nl_get_theme(s15->display_mode, s15->accent_color);
            s15->launch_game_idx = 1;
            s15->anim.launch_anim_active = true;
            for (int f = 1; f <= 30; f++) {
                nl_render_launch_zoom(s15, fb, f, 30);
            }
            /* At final frame (30/30), corners AND center must cleanly equal th->bg_main (NOT stretched red icon!) */
            assert(fb[0] == th->bg_main);
            assert(fb[(NL_SCREEN_H / 2) * NL_SCREEN_W + (NL_SCREEN_W / 2)] == th->bg_main);
            assert(fb[NL_FB_PIXELS - 1] == th->bg_main);
            s15->anim.launch_anim_active = false;
            nl_renderer_cleanup();
        }

        /* Case 3: Monogram (no custom icon, no screenshot) -> clean theme backdrop bg_main */
        s15->display_mode = NL_DISPLAY_DARK;
        s15->launch_game_idx = 2;
        s15->games[2].has_screenshot = false;
        s15->games[2].icon_pixels = NULL;
        s15->anim.launch_anim_active = true;
        for (int f = 1; f <= 30; f++) {
            nl_render_launch_zoom(s15, fb, f, 30);
        }
        assert(fb[(NL_SCREEN_H / 2) * NL_SCREEN_W + (NL_SCREEN_W / 2)] == nl_get_theme(NL_DISPLAY_DARK, s15->accent_color)->bg_main);
        s15->anim.launch_anim_active = false;
        nl_renderer_cleanup();

        free(blank_buf);
        free(solid_buf);
        free(scene_buf);

        /* 36c. Reassigned '0' and '-' keys navigate Previous / Next Game Page (never profile switching) */
        s15->sui.screen = NL_UI_LIBRARY;
        s15->current_page = 0;
        s15->selected_pos = 0;
        int prof_before_0_minus = s15->profiles.active_index;
        assert(nl_handle_char_input(s15, '-') == true);  /* Page 0 -> Page 1 */
        assert(s15->current_page == 1);
        assert(nl_handle_char_input(s15, '-') == true);  /* Page 1 -> Page 2 */
        assert(s15->current_page == 2);
        assert(nl_handle_char_input(s15, '-') == true);  /* Page 2 -> wraps to Page 0 */
        assert(s15->current_page == 0);
        assert(nl_handle_char_input(s15, '0') == true);  /* Page 0 -> wraps to Page 2 */
        assert(s15->current_page == 2);
        assert(nl_handle_char_input(s15, '0') == true);  /* Page 2 -> Page 1 */
        assert(s15->current_page == 1);
        assert(nl_handle_char_input(s15, '0') == true);  /* Page 1 -> Page 0 */
        assert(s15->current_page == 0);
        assert(s15->profiles.active_index == prof_before_0_minus);
        /* Also test dedicated NL_KEY_PAGE_NEXT / NL_KEY_PAGE_PREV events */
        assert(nl_handle_key(s15, NL_KEY_PAGE_NEXT) == true);
        assert(s15->current_page == 1);
        assert(nl_handle_key(s15, NL_KEY_PAGE_PREV) == true);
        assert(s15->current_page == 0);

        /* 36d. Vertical vs Horizontal Scroll Direction setting, adjacent peek metrics & persistence */
        assert(s15->settings.scroll_dir == NL_SCROLL_VERTICAL);
        nl_handle_key(s15, NL_KEY_SETTINGS);
        s15->sui.menu_cursor = NL_SET_MENU_DISPLAY;
        nl_handle_key(s15, NL_KEY_EXE);
        assert(s15->sui.screen == NL_UI_SETTINGS_DISPLAY);
        s15->sui.display_cursor = 1; /* Sens de defilement */
        assert(nl_handle_key(s15, NL_KEY_RIGHT) == true);
        assert(s15->settings.scroll_dir == NL_SCROLL_HORIZONTAL);
        nl_render_frame(s15, fb);
        nl_handle_key(s15, NL_KEY_ESC);
        nl_handle_key(s15, NL_KEY_ESC);
        assert(s15->sui.screen == NL_UI_LIBRARY);

        /* Verify horizontal metrics: page_stride_x == 280 -> peek ~12px on left & right, gap >= 16px */
        nl_layout_metrics_t hlm;
        nl_get_layout_metrics_full(NL_LAYOUT_3X2, NL_SCROLL_HORIZONTAL, true, true, 18, &hlm);
        assert(hlm.is_horizontal == true);
        assert(hlm.page_stride_x == 280);
        int right_peek_x = hlm.margin_x + hlm.page_stride_x;
        assert(right_peek_x == 308); /* 320 - 308 = 12px peek! */
        int inter_page_gap = hlm.page_stride_x - (hlm.cols * hlm.card_w + (hlm.cols - 1) * hlm.gap_x);
        assert(inter_page_gap >= 16);

        /* Navigate horizontally across pages using RIGHT arrow and verify smooth scroll_x_fp */
        s15->current_page = 0;
        s15->selected_pos = 0;
        nl_handle_key(s15, NL_KEY_RIGHT); /* col 1 */
        nl_handle_key(s15, NL_KEY_RIGHT); /* col 2 */
        nl_handle_key(s15, NL_KEY_RIGHT); /* wraps to next page (page 1, col 0)! */
        assert(s15->current_page == 1);
        for (int f = 0; f < 24; f++) {
            nl_anim_step(s15);
            nl_render_frame(s15, fb);
        }
        assert(s15->anim.scroll_x_fp == s15->anim.target_scroll_x_fp);
        assert(s15->anim.scroll_x_fp == hlm.page_stride_x * 16);

        /* Clicking right peeking card in Mouse Mode scrolls to Page 2 without launching */
        s15->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        s15->settings.touchpad_cursor_show = true;
        s15->launch_requested = false;
        nl_mouse_move_to(s15, 314, hlm.top_y + 20);
        assert(s15->sui.hover_kind == NL_HOVER_PAGE_NEXT);
        assert(nl_mouse_click(s15) == true);
        assert(s15->current_page == 2);
        assert(s15->launch_requested == false);

        /* Clicking left peeking card scrolls back to Page 1 */
        nl_mouse_move_to(s15, 6, hlm.top_y + 20);
        assert(s15->sui.hover_kind == NL_HOVER_PAGE_PREV);
        assert(nl_mouse_click(s15) == true);
        assert(s15->current_page == 1);
        s15->settings.touchpad_mode = NL_TP_MODE_TOUCHPAD;

        /* 36e. Padlock Profile Protection System (global toggle + per-profile lock) */
        assert(s15->settings.profile_lock_enabled == false);
        int p_locked = nl_profile_create(s15, "SafeUser", 12);
        assert(p_locked == 1);
        assert(s15->profiles.items[p_locked].is_locked == false);
        assert(nl_profile_is_protected(s15, p_locked) == false);

        /* Enable global protection & lock SafeUser */
        s15->settings.profile_lock_enabled = true;
        assert(nl_profile_toggle_lock(s15, p_locked) == true);
        assert(s15->profiles.items[p_locked].is_locked == true);
        assert(nl_profile_is_protected(s15, p_locked) == true);

        /* Locked profile blocks rename, avatar change, deletion, favorite toggle, shortcut assign */
        assert(nl_profile_rename(s15, p_locked, "Hacked") == false);
        assert(strcmp(s15->profiles.items[p_locked].name, "SafeUser") == 0);
        assert(nl_profile_set_avatar(s15, p_locked, 15) == false);
        assert(s15->profiles.items[p_locked].avatar_id == 12);
        assert(nl_profile_delete(s15, p_locked) == false);
        assert(s15->profiles.count == 2);

        /* Favorite toggle & shortcut assign blocked on active locked profile */
        s15->sui.screen = NL_UI_LIBRARY;
        s15->selected_pos = 0;
        int g0 = s15->filtered_indices[0];
        bool fav_before = s15->games[g0].is_favorite;
        nl_handle_key(s15, NL_KEY_FAV);
        assert(s15->games[g0].is_favorite == fav_before);
        s15->sui.screen = NL_UI_SETTINGS_SHORTCUTS;
        s15->sui.sc_game_cursor = 0;
        s15->sui.sc_picking_key = false;
        nl_handle_key(s15, NL_KEY_EXE);
        assert(s15->sui.sc_picking_key == false);

        /* Render Quick Selector & Settings Profiles with padlock visible */
        s15->sui.qp_open = true;
        for (int f = 0; f < 10; f++) nl_anim_step(s15);
        nl_render_frame(s15, fb);
        s15->sui.qp_open = false;

        s15->sui.screen = NL_UI_SETTINGS_PROFILES;
        s15->sui.prof_mode = NL_PROF_MODE_LIST;
        s15->sui.prof_cursor = 0; /* Row 0 = Padlock global toggle */
        nl_render_frame(s15, fb);
        s15->sui.prof_mode = NL_PROF_MODE_DETAIL;
        s15->sui.prof_detail_idx = p_locked;
        s15->sui.prof_detail_cursor = 3; /* Deverrouiller le profil */
        nl_render_frame(s15, fb);

        /* Persist config + profiles & verify scroll_dir + profile_lock_enabled + is_locked survive reload */
        const char *r15_cfg = "/tmp/nl_r15_suite_fs/ndless/nlauncher.cfg.tns";
        assert(nl_save_config(s15, r15_cfg) == true);
        assert(nl_save_profiles(s15, s15->profiles_path) == true);

        nl_state_t *s15_rel = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s15_rel != NULL);
        nl_init_state(s15_rel);
        assert(nl_load_config(s15_rel, r15_cfg) == true);
        assert(s15_rel->settings.scroll_dir == NL_SCROLL_HORIZONTAL);
        assert(s15_rel->settings.profile_lock_enabled == true);
        assert(nl_load_profiles(s15_rel, s15->profiles_path) == true);
        assert(s15_rel->profiles.count == 2);
        assert(s15_rel->profiles.items[1].is_locked == true);
        assert(nl_profile_is_protected(s15_rel, 1) == true);

        /* Disabling global profile_lock_enabled immediately hides all padlock restrictions */
        s15_rel->settings.profile_lock_enabled = false;
        assert(nl_profile_is_protected(s15_rel, 1) == false);
        assert(nl_profile_rename(s15_rel, 1, "UnlockedName") == true);
        assert(strcmp(s15_rel->profiles.items[1].name, "UnlockedName") == 0);

        nl_free_icons(s15_rel);
        free(s15_rel);
        nl_free_icons(s15);
        free(s15);
        printf("[PASS] Section 36 #R15 (bulletproof screenshots, 3-case launch crossfade, minimalist QP, floating avatar picker, padlock protection, 0/- page nav & horizontal peek) verified.\n");
    }

    /* =====================================================================
     * 37. #R17 CORRECTION & VERIFICATION SUITE:
     *     (1) Exact R15 Horizontal Mode restored (12px natural left/right icon peeks,
     *         zero independent top/bottom miniature carousel strips, full vertical grid)
     *     (2) Exact Vertical Mirror of R15 Horizontal Mode (12px natural top & bottom
     *         real icon peeks across all 5 layouts in multi-page vertical mode, smooth
     *         vertical scroll, interactive click-to-page without launching, single-page
     *         vertical remains full-size)
     *     (3) Preserved Modal ENTER / Click Non-Propagation Fix (consume_exe_release)
     *     (4) Preserved Creative "Orbital Prism" Profile Animations
     * ===================================================================== */
    {
        assert(strstr(NL_BUILD_STAMP_STR, "#R23") != NULL);

        nl_state_t *s16 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s16 != NULL);
        nl_init_state(s16);

        /* Populate 18 games (3 pages in 3x2) with real custom 48x48 covers */
        s16->total_games = 18;
        uint16_t page_colors[3] = {
            RGB565(220, 60, 60),   /* Page 0: warm red */
            RGB565(50, 200, 90),   /* Page 1: emerald green */
            RGB565(70, 130, 240)   /* Page 2: cobalt blue */
        };
        for (int i = 0; i < 18; i++) {
            nl_game_t *g = &s16->games[i];
            memset(g, 0, sizeof(*g));
            snprintf(g->title, sizeof(g->title), "Game %02d", i + 1);
            snprintf(g->filename, sizeof(g->filename), "r17_%02d.tns", i);
            snprintf(g->path, sizeof(g->path), "/documents/games/r17_%02d.tns", i);
            g->bin_format = NL_BIN_ZEHN;
            g->icon_w = 48;
            g->icon_h = 48;
            g->icon_pixels = (uint16_t *)malloc(48 * 48 * sizeof(uint16_t));
            assert(g->icon_pixels != NULL);
            uint16_t col = page_colors[i / 6];
            for (int p = 0; p < 48 * 48; p++) g->icon_pixels[p] = col;
        }
        nl_rebuild_filter(s16);

        /* 37a. Verify Horizontal (R15 exact) & Vertical (R17 mirrored peek) Metrics across ALL 5 layouts */
        const nl_layout_t layouts[5] = {
            NL_LAYOUT_1X1, NL_LAYOUT_2X2, NL_LAYOUT_3X2, NL_LAYOUT_3X3, NL_LAYOUT_4X4
        };
        for (int li = 0; li < 5; li++) {
            nl_layout_metrics_t lm_h_multi, lm_h_single, lm_v_multi, lm_v_single;
            nl_get_layout_metrics_full(layouts[li], NL_SCROLL_HORIZONTAL, true, true, 32, &lm_h_multi);
            nl_get_layout_metrics_full(layouts[li], NL_SCROLL_HORIZONTAL, true, true, 1,  &lm_h_single);
            nl_get_layout_metrics_full(layouts[li], NL_SCROLL_VERTICAL,   true, true, 32, &lm_v_multi);
            nl_get_layout_metrics_full(layouts[li], NL_SCROLL_VERTICAL,   true, true, 1,  &lm_v_single);

            /* Horizontal Mode = exact R15 (no top/bottom carousel shrinkage; 12px left/right side peeks) */
            assert(lm_h_multi.is_horizontal == true);
            assert(lm_h_multi.margin_x >= 28);
            int h_right_peek_x = lm_h_multi.margin_x + lm_h_multi.page_stride_x;
            assert(h_right_peek_x <= NL_SCREEN_W - 10 && h_right_peek_x < NL_SCREEN_W);
            int h_content_w = lm_h_multi.cols * lm_h_multi.card_w + (lm_h_multi.cols - 1) * lm_h_multi.gap_x;
            assert(lm_h_multi.page_stride_x >= h_content_w + 16);
            if (layouts[li] == NL_LAYOUT_3X2) {
                assert(lm_h_multi.top_y < 44); /* R15 full-height 3x2 grid restored! */
            }

            /* Vertical Multi-Page Mode = exact vertical mirror (natural 12px top & bottom real icon peeks) */
            assert(lm_v_multi.is_horizontal == false);
            assert(lm_v_multi.top_y >= NL_VIEW_TOP + 20);
            int v_content_h = lm_v_multi.rows * (lm_v_multi.card_h + lm_v_multi.label_h) +
                              (lm_v_multi.rows - 1) * lm_v_multi.gap_y;
            int v_bot_peek_y = lm_v_multi.top_y + lm_v_multi.page_stride_y;
            int v_top_peek_end_y = lm_v_multi.top_y + v_content_h - lm_v_multi.page_stride_y;
            /* Next page peeks ~12px from view_bottom (205); Prev page peeks ~11-12px from NL_VIEW_TOP (26) */
            assert(v_bot_peek_y == lm_v_multi.view_bottom - 12);
            assert(v_top_peek_end_y >= NL_VIEW_TOP + 10 && v_top_peek_end_y <= NL_VIEW_TOP + 13);
            assert(lm_v_multi.page_stride_y >= v_content_h + 8);

            /* Single-page vertical stays full-size (strictly larger cards than multi-page vertical peek) */
            assert(lm_v_single.card_h > lm_v_multi.card_h);
            assert(lm_v_single.page_stride_y == lm_v_single.row_stride_y * lm_v_single.rows);
        }

        /* 37b. Render Vertical Multi-Page Mode (Page 1 of 3) & Verify Real Covers Peek at TOP & BOTTOM */
        s16->settings.scroll_dir = NL_SCROLL_VERTICAL;
        s16->settings.layout = NL_LAYOUT_3X2;
        s16->display_mode = NL_DISPLAY_DARK;
        s16->current_page = 1; /* Middle page: Prev = Page 0 (red), Current = Page 1 (green), Next = Page 2 (blue) */
        nl_layout_metrics_t vlm;
        nl_get_layout_metrics_full(NL_LAYOUT_3X2, NL_SCROLL_VERTICAL, true, true, 18, &vlm);
        s16->anim.scroll_y_fp = vlm.page_stride_y * 16;
        s16->anim.target_scroll_y_fp = vlm.page_stride_y * 16;
        s16->anim.scroll_x_fp = 0;
        s16->anim.target_scroll_x_fp = 0;
        nl_render_frame(s16, fb);

        const nl_theme_t *th_dark = nl_get_theme(NL_DISPLAY_DARK, s16->accent_color);
        /* Top peek band (y = NL_VIEW_TOP..NL_VIEW_TOP+11) across col 0..2 must show real Page 0 cover pixels */
        int top_peek_px = 0;
        for (int y = NL_VIEW_TOP; y < NL_VIEW_TOP + 11; y++) {
            for (int x = vlm.margin_x + 4; x < vlm.margin_x + vlm.card_w - 4; x++) {
                if (fb[y * NL_SCREEN_W + x] != th_dark->bg_main) top_peek_px++;
            }
        }
        assert(top_peek_px > 120);

        /* Bottom peek band (y = view_bottom-12..view_bottom-1) across col 0..2 must show real Page 2 cover pixels */
        int bot_peek_px = 0;
        for (int y = vlm.view_bottom - 12; y < vlm.view_bottom; y++) {
            for (int x = vlm.margin_x + 4; x < vlm.margin_x + vlm.card_w - 4; x++) {
                if (fb[y * NL_SCREEN_W + x] != th_dark->bg_main) bot_peek_px++;
            }
        }
        assert(bot_peek_px > 120);

        /* 37c. Interactive Hover & Click on Vertical Top & Bottom Peeking Icons jumps page WITHOUT launching */
        s16->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        s16->settings.touchpad_cursor_show = true;
        s16->launch_requested = false;
        int col1_cx = vlm.margin_x + vlm.card_w + vlm.gap_x + vlm.card_w / 2;

        /* Hover & click top peeking icon (Page 0) -> jumps to Page 0 without launching */
        nl_mouse_move_to(s16, col1_cx, NL_VIEW_TOP + 5);
        assert(s16->sui.hover_kind == NL_HOVER_PAGE_PREV);
        assert(nl_mouse_click(s16) == true);
        assert(s16->current_page == 0);
        assert(s16->launch_requested == false);

        /* Settle onto Page 0, then hover & click bottom peeking icon (Page 1) -> jumps to Page 1 without launching */
        for (int f = 0; f < 24; f++) nl_anim_step(s16);
        assert(s16->anim.scroll_y_fp == 0);
        nl_mouse_move_to(s16, col1_cx, vlm.view_bottom - 6);
        assert(s16->sui.hover_kind == NL_HOVER_PAGE_NEXT);
        assert(nl_mouse_click(s16) == true);
        assert(s16->current_page == 1);
        assert(s16->launch_requested == false);

        /* Also verify Horizontal Mode (R15 exact): left/right side peeks click cleanly & NO top/bottom carousel bands exist */
        s16->settings.scroll_dir = NL_SCROLL_HORIZONTAL;
        nl_layout_metrics_t hlm;
        nl_get_layout_metrics_full(NL_LAYOUT_3X2, NL_SCROLL_HORIZONTAL, true, true, 18, &hlm);
        s16->current_page = 1;
        s16->anim.scroll_x_fp = hlm.page_stride_x * 16;
        s16->anim.target_scroll_x_fp = hlm.page_stride_x * 16;
        s16->anim.scroll_y_fp = 0;
        s16->anim.target_scroll_y_fp = 0;
        nl_render_frame(s16, fb);

        /* Clicking right peeking icon -> Page 2; left peeking icon -> Page 1 (no game launch) */
        nl_mouse_move_to(s16, NL_SCREEN_W - 6, hlm.top_y + 20);
        assert(s16->sui.hover_kind == NL_HOVER_PAGE_NEXT);
        assert(nl_mouse_click(s16) == true);
        assert(s16->current_page == 2);
        assert(s16->launch_requested == false);
        for (int f = 0; f < 24; f++) nl_anim_step(s16);
        nl_mouse_move_to(s16, 6, hlm.top_y + 20);
        assert(s16->sui.hover_kind == NL_HOVER_PAGE_PREV);
        assert(nl_mouse_click(s16) == true);
        assert(s16->current_page == 1);
        assert(s16->launch_requested == false);
        s16->settings.touchpad_mode = NL_TP_MODE_TOUCHPAD;

        /* 37d. Modal ENTER / Click Non-Propagation Fix + Transition Launch Block */
        int p2 = nl_profile_create(s16, "", 19);
        assert(p2 == 1);
        s16->profiles.active_index = 0;
        s16->sui.screen = NL_UI_LIBRARY;
        s16->launch_requested = false;
        s16->sui.consume_exe_release = false;

        /* Open Quick Profile with '.' -> qp_opening == true */
        assert(nl_handle_char_input(s16, '.') == true);
        assert(s16->sui.qp_open == true);
        assert(s16->anim.qp_opening == true);
        for (int f = 0; f < 16; f++) {
            nl_anim_step(s16);
            nl_render_frame(s16, fb);
        }
        assert(s16->anim.qp_open_fp == 256);

        /* Move to Profile 1 -> damped spring halo velocity kicks in & settles */
        assert(nl_handle_key(s16, NL_KEY_RIGHT) == true);
        assert(s16->sui.qp_cursor == 1);
        nl_anim_step(s16);
        assert(s16->anim.qp_glow_vx_fp != 0);
        for (int f = 0; f < 24; f++) {
            nl_anim_step(s16);
            nl_render_frame(s16, fb);
        }
        assert(s16->anim.qp_glow_vx_fp == 0);

        /* Press ENTER to switch profile -> closes QP, latches consume_exe_release, NEVER launches game */
        uint8_t prev_av = s16->profiles.items[0].avatar_id;
        uint8_t next_av = s16->profiles.items[1].avatar_id;
        assert(nl_handle_key(s16, NL_KEY_EXE) == true);
        assert(s16->sui.qp_open == false);
        assert(s16->anim.qp_opening == false);
        assert(s16->sui.consume_exe_release == true);
        assert(s16->anim.launch_holding == false);
        assert(s16->launch_requested == false);
        assert(s16->profiles.active_index == 1);

        /* Verify outgoing & incoming avatars captured for Orbital Prism Identity Shift */
        assert(s16->anim.prof_switch_active == true);
        assert(s16->anim.prof_switch_from_avatar_id == prev_av);
        assert(s16->anim.prof_switch_avatar_id == next_av);
        assert(s16->anim.prof_switch_max == 20);

        /* Stray NL_KEY_EXE or Mouse click over a game card DURING profile transition MUST NOT launch */
        nl_handle_key(s16, NL_KEY_EXE);
        assert(s16->launch_requested == false);
        s16->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        nl_mouse_move_to(s16, hlm.margin_x + 20, hlm.top_y + 20);
        nl_mouse_click(s16);
        assert(s16->launch_requested == false);
        s16->settings.touchpad_mode = NL_TP_MODE_TOUCHPAD;

        /* Step & render full 20-frame Profile Switch animation across all 3 themes */
        const nl_display_mode_t themes[3] = { NL_DISPLAY_DARK, NL_DISPLAY_LIGHT, NL_DISPLAY_GRAY };
        for (int ti = 0; ti < 3; ti++) {
            s16->display_mode = themes[ti];
            s16->anim.prof_switch_active = true;
            s16->anim.prof_switch_tick = 0;
            for (int f = 0; f < s16->anim.prof_switch_max; f++) {
                nl_render_frame(s16, fb);
                nl_anim_step(s16);
            }
            assert(s16->anim.prof_switch_active == false);
        }

        /* 37e. Strictly Centered '+' Button Morph -> Floating Avatar Picker -> Avatar Birth */
        s16->display_mode = NL_DISPLAY_DARK;
        s16->sui.consume_exe_release = false;
        assert(nl_handle_char_input(s16, '.') == true);
        for (int f = 0; f < 16; f++) nl_anim_step(s16);
        s16->sui.qp_cursor = s16->profiles.count; /* '+' slot */
        assert(nl_handle_key(s16, NL_KEY_EXE) == true);
        assert(s16->anim.prof_plus_active == true);
        assert(s16->anim.prof_plus_max == 14);
        assert(s16->sui.consume_exe_release == true);
        assert(s16->launch_requested == false);
        int anchored_x = s16->anim.prof_plus_origin_x;
        int anchored_y = s16->anim.prof_plus_origin_y;
        assert(anchored_x > 160 && anchored_y == 120);

        /* Step '+' morph: origin stays anchored & hands off smoothly to qp_pick_open */
        for (int f = 0; f < s16->anim.prof_plus_max; f++) {
            assert(s16->anim.prof_plus_origin_x == anchored_x);
            assert(s16->anim.prof_plus_origin_y == anchored_y);
            nl_render_frame(s16, fb);
            nl_anim_step(s16);
        }
        assert(s16->anim.prof_plus_active == false);
        assert(s16->sui.qp_pick_open == true);
        assert(s16->anim.qp_pick_fp >= 48);

        /* Render Avatar Picker bloom and create new profile with ENTER (no game launch!) */
        for (int f = 0; f < 14; f++) {
            nl_anim_step(s16);
            nl_render_frame(s16, fb);
        }
        s16->sui.consume_exe_release = false;
        s16->sui.qp_pick_cursor = 7;
        assert(nl_handle_key(s16, NL_KEY_EXE) == true);
        assert(s16->sui.qp_pick_open == false);
        assert(s16->sui.consume_exe_release == true);
        assert(s16->launch_requested == false);
        assert(s16->anim.prof_birth_active == true);
        assert(s16->profiles.count == 3);

        for (int f = 0; f < s16->anim.prof_birth_max; f++) {
            nl_render_frame(s16, fb);
            nl_anim_step(s16);
        }
        assert(s16->anim.prof_birth_active == false);

        /* Close Quick Profile (avatar birth returns cleanly to QP dock) then verify Settings modal EXE sets consume_exe_release */
        if (s16->sui.qp_open) nl_handle_key(s16, NL_KEY_ESC);
        s16->sui.consume_exe_release = false;
        nl_handle_key(s16, NL_KEY_SETTINGS);
        assert(s16->sui.screen == NL_UI_SETTINGS_MAIN);
        nl_handle_key(s16, NL_KEY_EXE);
        assert(s16->sui.consume_exe_release == true);
        assert(s16->launch_requested == false);

        nl_free_icons(s16);
        free(s16);
        printf("[PASS] Section 37 #R17 (exact R15 horizontal side-peeks, mirrored vertical top/bottom natural icon peeks across 5 layouts, zero carousels, modal ENTER latch & Orbital Prism animations) verified.\n");
    }

    /* ======================================================================
     * 38. #R18 TWO SURGICAL VISUAL FIXES VERIFICATION:
     *     (1) Horizontal Mode ONLY: pagination renders in background layer BEHIND
     *         library cards/icons (cards draw over pagination when overlapping;
     *         Vertical Mode remains 100% untouched).
     *     (2) Quick Profile Selector: zero residual horizontal bar/dash below or
     *         cutting across avatars on Dark, Light, and Gray themes while keeping
     *         the luminous multicolor halo and all profile animations intact.
     * ====================================================================== */
    {
        nl_state_t *s18 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s18 != NULL);
        nl_init_state(s18);
        assert(strstr(NL_BUILD_STAMP_STR, "#R23") != NULL);

        /* Populate 40 games (3 pages in 4x4) with bright distinct RGB565 icons */
        s18->total_games = 40;
        for (int i = 0; i < s18->total_games; i++) {
            snprintf(s18->games[i].title, sizeof(s18->games[i].title), "Game%02d", i + 1);
            snprintf(s18->games[i].filename, sizeof(s18->games[i].filename), "game%02d.tns", i + 1);
            snprintf(s18->games[i].path, sizeof(s18->games[i].path), "/documents/Jeux/game%02d.tns", i + 1);
            s18->games[i].icon_source = NL_ICON_CAPTURED;
            s18->games[i].icon_w = 32;
            s18->games[i].icon_h = 32;
            s18->games[i].icon_pixels = (uint16_t *)malloc(32U * 32U * sizeof(uint16_t));
            assert(s18->games[i].icon_pixels != NULL);
            uint16_t solid = RGB565(240, 48, 48); /* High-contrast vivid red card */
            for (int p = 0; p < 32 * 32; p++) s18->games[i].icon_pixels[p] = solid;
            s18->games[i].icon_alpha = NULL;
        }

        /* 38a. Horizontal Mode 4x4 (show_shortcuts=false, search_bar open => row 3 overlaps pagination_y=226):
         *      Verify pagination dots exist in the gaps AND game card pixels draw ON TOP of pagination! */
        s18->settings.layout = NL_LAYOUT_4X4;
        s18->settings.scroll_dir = NL_SCROLL_HORIZONTAL;
        s18->settings.show_shortcuts = false;
        s18->settings.show_names = false;
        s18->anim.switch_shortcuts_fp = 0;
        s18->display_mode = NL_DISPLAY_LIGHT;
        s18->accent_color = NL_ACCENT_BLUE;
        nl_rebuild_filter(s18);
        s18->anim.scroll_x_fp = 0;
        s18->anim.scroll_y_fp = 0;
        s18->anim.dot_pos_fp  = 0;

        nl_layout_metrics_t hlm;
        nl_get_layout_metrics_full(NL_LAYOUT_4X4, NL_SCROLL_HORIZONTAL, false, false, s18->filtered_count, &hlm);
        assert(hlm.is_horizontal == true);
        assert(hlm.pagination_y == NL_PAGINATION_Y_NO_SC);

        /* First verify normal view still shows pagination dots at y = pagination_y + 3 */
        const nl_theme_t *lth = nl_get_theme(NL_DISPLAY_LIGHT, NL_ACCENT_BLUE);
        nl_render_frame(s18, fb);
        int pag_accent_px = 0;
        for (int x = 120; x < 200; x++) {
            if (fb[(hlm.pagination_y + 3) * NL_SCREEN_W + x] == lth->accent_primary) pag_accent_px++;
        }
        assert(pag_accent_px >= 10);

        /* Now shift cards vertically into pagination_y via inline search bar (search_h=28):
         * row 3 cards (col 1 & col 2) now span y = 203..249 right over pagination_y = 226..233.
         * Because pagination is drawn FIRST (behind cards) in Horizontal Mode, card pixels MUST
         * cover pagination dots at the card interior — NEVER overwritten by pagination accent color! */
        s18->sui.search_active = true;
        s18->anim.search_bar_fp = 256;
        nl_render_frame(s18, fb);
        int slot_x = 0, slot_y = 0;
        /* f_idx = 13 -> row 3, col 1 (sits right across x ~ 100..146, overlapping left pagination dot!) */
        nl_compute_grid_slot_pos(&hlm, s18->filtered_count, 13, &slot_x, &slot_y);
        int sample_x = slot_x + hlm.card_w / 2;
        int sample_y = hlm.pagination_y + 3;
        uint16_t card_over_pag_px = fb[sample_y * NL_SCREEN_W + sample_x];
        /* Card interior must be vivid red (RGB565(240,48,48)), NOT background and NOT pagination dot color */
        assert(card_over_pag_px != lth->bg_main);
        assert(card_over_pag_px != lth->accent_primary);
        assert(card_over_pag_px != lth->dot_inactive);
        assert(((card_over_pag_px >> 11) & 31U) >= 24U); /* Strong red channel from game icon on top */
        s18->sui.search_active = false;
        s18->anim.search_bar_fp = 0;

        /* Verify Vertical Mode remains 100% untouched (clips strictly at view_bottom, pagination drawn on top cleanly) */
        s18->settings.scroll_dir = NL_SCROLL_VERTICAL;
        nl_rebuild_filter(s18);
        s18->anim.scroll_x_fp = 0;
        s18->anim.scroll_y_fp = 0;
        s18->anim.dot_pos_fp  = 0;
        nl_layout_metrics_t vlm18;
        nl_get_layout_metrics_full(NL_LAYOUT_4X4, NL_SCROLL_VERTICAL, false, false, s18->filtered_count, &vlm18);
        assert(vlm18.is_horizontal == false);
        nl_render_frame(s18, fb);
        for (int y = vlm18.view_bottom; y < vlm18.pagination_y; y++) {
            for (int x = 10; x < 310; x++) {
                assert(fb[y * NL_SCREEN_W + x] == lth->bg_main);
            }
        }

        /* 38b. Quick Profile Selector: ZERO residual horizontal line/bar below or behind avatars
         *      across Dark, Light, and Gray themes, while multicolor luminous halo remains 100% intact! */
        s18->settings.scroll_dir = NL_SCROLL_HORIZONTAL;
        nl_rebuild_filter(s18);
        s18->anim.scroll_x_fp = 0;
        s18->anim.scroll_y_fp = 0;
        s18->anim.dot_pos_fp  = 0;
        s18->profiles.count = 3;
        s18->profiles.active_index = 0;
        s18->profiles.items[0].avatar_id = 0;
        s18->profiles.items[1].avatar_id = 1;
        s18->profiles.items[2].avatar_id = 2;
        s18->settings.profile_lock_enabled = false;
        s18->sui.qp_open = true;
        s18->sui.qp_cursor = 0;
        s18->anim.qp_open_fp = 256;
        s18->anim.qp_opening = false;
        s18->anim.qp_sel_bounce_fp = 0;

        /* Compute exact slot 0 center (shown = 4 slots: 3 profiles + '+', diam=34, stride=44) */
        const int shown = 4;
        const int diam = 34;
        const int stride = 44;
        const int strip_w = shown * diam + (shown - 1) * (stride - diam);
        const int start_x = 160 - strip_w / 2;
        const int scx0 = start_x + diam / 2;
        const int cy = 120;
        s18->anim.qp_glow_x_fp = scx0 << 4;
        s18->anim.qp_trail_x_fp = scx0 << 4;
        s18->anim.qp_glow_y_fp = cy << 4;
        s18->anim.qp_slot_scale_fp[0] = 276;
        s18->anim.qp_slot_scale_fp[1] = 228;
        s18->anim.qp_slot_scale_fp[2] = 228;
        s18->anim.qp_slot_scale_fp[3] = 228;

        const nl_display_mode_t modes18[3] = { NL_DISPLAY_DARK, NL_DISPLAY_LIGHT, NL_DISPLAY_GRAY };
        for (int mi = 0; mi < 3; mi++) {
            s18->display_mode = modes18[mi];
            const nl_theme_t *mth = nl_get_theme(modes18[mi], NL_ACCENT_BLUE);
            uint16_t box_bg = mth->is_light
                ? RGB565(250, 252, 255)
                : ((modes18[mi] == NL_DISPLAY_GRAY) ? RGB565(78, 82, 90) : RGB565(18, 22, 32));
            nl_render_frame(s18, fb);

            /* 1) Confirm luminous multicolor halo IS still intact around selected avatar (radius ~21px at left/right/top) */
            int halo_px = 0;
            for (int dx = -24; dx <= -19; dx++) {
                if (fb[cy * NL_SCREEN_W + (scx0 + dx)] != box_bg) halo_px++;
            }
            for (int dx = 19; dx <= 24; dx++) {
                if (fb[cy * NL_SCREEN_W + (scx0 + dx)] != box_bg) halo_px++;
            }
            assert(halo_px >= 6);

            /* 2) Confirm ZERO residual horizontal dash/line below the halo at y = cy + 27 .. cy + 36
             *    (where the old active pill / name slot / divider sat, well below ring outer edge cy+26) */
            for (int y = cy + 27; y <= cy + 36; y++) {
                for (int x = scx0 - 20; x <= scx0 + 20; x++) {
                    assert(fb[y * NL_SCREEN_W + x] == box_bg);
                }
            }

            /* 3) Confirm the old 8x3 active accent pill at (scx0, cy + 23..25) is GONE (no pure accent_primary block
             *    and no dark/gray shadow bar RGB565(4,6,10)/RGB565(196,206,220) cutting across bottom of halo at sy+cur_diam) */
            uint16_t old_sh_col = mth->is_light ? RGB565(196, 206, 220) : RGB565(4, 6, 10);
            int cur_diam = (diam * 276 + 128) >> 8; /* 37 */
            int sy0 = (cy - 1) - cur_diam / 2;
            int sh_hits = 0;
            for (int y = sy0 + cur_diam; y < sy0 + cur_diam + 4; y++) {
                for (int x = scx0 - 12; x <= scx0 + 12; x++) {
                    if (fb[y * NL_SCREEN_W + x] == old_sh_col) sh_hits++;
                }
            }
            assert(sh_hits == 0);
        }

        nl_free_icons(s18);
        free(s18);
        printf("[PASS] Section 38 #R18 (horizontal-only pagination background z-order + Quick Profile zero residual line across Dark/Light/Gray) verified.\n");
    }

    /* ======================================================================
     * 39. #R19 CUSTOM 144x144 PNG ICON PERFORMANCE OPTIMIZATION & ZERO-VISUAL-CHANGE
     *     CACHE VERIFICATION:
     *     (a) Fast 144x144 RGBA & RGB PNG decode + 32-bit area-averaging downscale
     *         to 84x84 + dead-alpha pruning when final icon is 100% opaque
     *     (b) Bit-for-bit identical rendering between cold uncached & warm cached
     *         card sprites / static library FB / dimmed overlay FB across layouts
     *         (4x4, 3x3, 3x2, 1x1) and themes (Dark, Light, Gray)
     *     (c) In-place icon pixel mutation invalidation, mouse hover invalidation,
     *         and OOM memory recovery cleanup (nl_recover_memory_on_oom)
     * ====================================================================== */
    {
        const char *r19_root = "/tmp/nl_r19_suite_fs";
        assert(system("rm -rf /tmp/nl_r19_suite_fs && mkdir -p /tmp/nl_r19_suite_fs/ndless/icons /tmp/nl_r19_suite_fs/games") == 0);

        /* Write 3 real 144x144 PNG icons (uncompressed zlib-stored DEFLATE blocks):
         *  0: 144x144 RGBA with rounded transparent corners -> keeps icon_alpha != NULL
         *  1: 144x144 RGBA with alpha==255 everywhere       -> prunes dead icon_alpha == NULL
         *  2: 144x144 RGB  (color_type=2 fast unpack)       -> icon_alpha == NULL */
        const char *gnames[3] = { "rgba144", "opaque144", "rgb144" };
        for (int gi = 0; gi < 3; gi++) {
            char tns_p[256], png_p[256];
            snprintf(tns_p, sizeof(tns_p), "%s/games/%s.tns", r19_root, gnames[gi]);
            FILE *gf = fopen(tns_p, "wb");
            assert(gf != NULL);
            fwrite("PRG\0TEST", 1, 8, gf);
            fclose(gf);

            snprintf(png_p, sizeof(png_p), "%s/ndless/icons/%s.png.tns", r19_root, gnames[gi]);
            const int pw = 144, ph = 144;
            const int ctype = (gi == 2) ? 2 : 6;
            const int bpp = (ctype == 6) ? 4 : 3;
            const size_t raw_len = (size_t)ph * (1U + (size_t)pw * (size_t)bpp);
            uint8_t *raw = (uint8_t *)malloc(raw_len);
            assert(raw != NULL);
            for (int y = 0; y < ph; y++) {
                uint8_t *row = raw + (size_t)y * (1U + (size_t)pw * (size_t)bpp);
                row[0] = 0; /* Filter: None */
                for (int x = 0; x < pw; x++) {
                    uint8_t *px = row + 1 + x * bpp;
                    if (gi == 0) {
                        px[0] = (uint8_t)(40 + (x * 180) / pw);
                        px[1] = (uint8_t)(80 + (y * 140) / ph);
                        px[2] = 220;
                        bool corner = (x < 12 && y < 12) || (x >= pw - 12 && y < 12) ||
                                      (x < 12 && y >= ph - 12) || (x >= pw - 12 && y >= ph - 12);
                        px[3] = corner ? 0 : 255;
                    } else if (gi == 1) {
                        px[0] = 32; px[1] = 216; px[2] = 96; px[3] = 255;
                    } else {
                        px[0] = 232; px[1] = 88; px[2] = 48;
                    }
                }
            }

            /* Build valid zlib stream with stored DEFLATE blocks (max 65535 bytes/block) */
            size_t nblocks = (raw_len + 65534U) / 65535U;
            size_t zcap = 2U + raw_len + nblocks * 5U + 4U;
            uint8_t *zbuf = (uint8_t *)malloc(zcap);
            assert(zbuf != NULL);
            size_t zpos = 0;
            zbuf[zpos++] = 0x78;
            zbuf[zpos++] = 0x01;
            uint32_t s1 = 1U, s2 = 0U;
            for (size_t i = 0; i < raw_len; i++) {
                s1 = (s1 + raw[i]) % 65521U;
                s2 = (s2 + s1) % 65521U;
            }
            size_t rem = raw_len, off = 0;
            while (rem > 0) {
                size_t blk = (rem > 65535U) ? 65535U : rem;
                rem -= blk;
                zbuf[zpos++] = (rem == 0) ? 0x01 : 0x00;
                zbuf[zpos++] = (uint8_t)(blk & 0xFFU);
                zbuf[zpos++] = (uint8_t)((blk >> 8) & 0xFFU);
                uint16_t nlen = (uint16_t)(~blk);
                zbuf[zpos++] = (uint8_t)(nlen & 0xFFU);
                zbuf[zpos++] = (uint8_t)((nlen >> 8) & 0xFFU);
                memcpy(zbuf + zpos, raw + off, blk);
                zpos += blk;
                off += blk;
            }
            uint32_t adler = (s2 << 16) | s1;
            zbuf[zpos++] = (uint8_t)((adler >> 24) & 0xFFU);
            zbuf[zpos++] = (uint8_t)((adler >> 16) & 0xFFU);
            zbuf[zpos++] = (uint8_t)((adler >> 8) & 0xFFU);
            zbuf[zpos++] = (uint8_t)(adler & 0xFFU);

            FILE *pf = fopen(png_p, "wb");
            assert(pf != NULL);
            static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            fwrite(png_sig, 1, 8, pf);
            uint8_t ihdr[25] = {
                0, 0, 0, 13, 'I', 'H', 'D', 'R',
                0, 0, 0, 144, 0, 0, 0, 144,
                8, (uint8_t)ctype, 0, 0, 0,
                0, 0, 0, 0 /* dummy CRC (decoder ignores CRC) */
            };
            fwrite(ihdr, 1, 25, pf);
            uint8_t idat_hdr[8] = {
                (uint8_t)((zpos >> 24) & 0xFFU), (uint8_t)((zpos >> 16) & 0xFFU),
                (uint8_t)((zpos >> 8) & 0xFFU), (uint8_t)(zpos & 0xFFU),
                'I', 'D', 'A', 'T'
            };
            fwrite(idat_hdr, 1, 8, pf);
            fwrite(zbuf, 1, zpos, pf);
            uint8_t zero_crc[4] = {0, 0, 0, 0};
            fwrite(zero_crc, 1, 4, pf);
            uint8_t iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0, 0, 0, 0};
            fwrite(iend, 1, 12, pf);
            fclose(pf);
            free(zbuf);
            free(raw);
        }

        nl_state_t *s19 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s19 != NULL);
        nl_init_state(s19);
        snprintf(s19->sys.docs_root, sizeof(s19->sys.docs_root), "%s", r19_root);
        snprintf(s19->covers_dir, sizeof(s19->covers_dir), "%s/ndless/covers", r19_root);
        snprintf(s19->icons_dir, sizeof(s19->icons_dir), "%s/ndless/icons", r19_root);
        s19->total_games = 3;
        for (int i = 0; i < 3; i++) {
            snprintf(s19->games[i].title, sizeof(s19->games[i].title), "%s", gnames[i]);
            snprintf(s19->games[i].filename, sizeof(s19->games[i].filename), "%s.tns", gnames[i]);
            snprintf(s19->games[i].path, sizeof(s19->games[i].path), "%s/games/%s.tns", r19_root, gnames[i]);
            assert(nl_load_game_icon(&s19->games[i], s19->covers_dir) == true);
        }

        /* 39a. Verify 144x144 decode dimensions, colors, alpha preservation & dead-alpha pruning */
        assert(s19->games[0].icon_source == NL_ICON_PNG);
        assert(s19->games[0].icon_w == NL_ICON_MAX_DIM && s19->games[0].icon_h == NL_ICON_MAX_DIM);
        assert(s19->games[0].icon_pixels != NULL && s19->games[0].icon_alpha != NULL);
        assert(s19->games[0].icon_alpha[0] == 0); /* Corner remained transparent */
        assert(s19->games[0].icon_alpha[(NL_ICON_MAX_DIM / 2) * NL_ICON_MAX_DIM + (NL_ICON_MAX_DIM / 2)] == 255);

        assert(s19->games[1].icon_source == NL_ICON_PNG);
        assert(s19->games[1].icon_w == NL_ICON_MAX_DIM && s19->games[1].icon_h == NL_ICON_MAX_DIM);
        assert(s19->games[1].icon_pixels != NULL);
        assert(s19->games[1].icon_alpha == NULL); /* Dead alpha pruned automatically! */
        uint16_t c1 = s19->games[1].icon_pixels[42 * NL_ICON_MAX_DIM + 42];
        assert(((c1 >> 5) & 63U) >= 50U); /* Strong green */

        assert(s19->games[2].icon_source == NL_ICON_PNG);
        assert(s19->games[2].icon_w == NL_ICON_MAX_DIM && s19->games[2].icon_h == NL_ICON_MAX_DIM);
        assert(s19->games[2].icon_pixels != NULL && s19->games[2].icon_alpha == NULL);
        uint16_t c2 = s19->games[2].icon_pixels[42 * NL_ICON_MAX_DIM + 42];
        assert(((c2 >> 11) & 31U) >= 26U); /* Strong red */

        /* Expand to 24 games with 84x84 RGBA icons for full multi-page cache equivalence tests */
        s19->total_games = 24;
        for (int i = 3; i < 24; i++) {
            snprintf(s19->games[i].title, sizeof(s19->games[i].title), "Icon%02d", i);
            snprintf(s19->games[i].filename, sizeof(s19->games[i].filename), "icon%02d.tns", i);
            snprintf(s19->games[i].path, sizeof(s19->games[i].path), "%s/games/icon%02d.tns", r19_root, i);
            s19->games[i].icon_source = NL_ICON_PNG;
            s19->games[i].icon_w = NL_ICON_MAX_DIM;
            s19->games[i].icon_h = NL_ICON_MAX_DIM;
            s19->games[i].icon_pixels = (uint16_t *)malloc((size_t)NL_ICON_MAX_DIM * NL_ICON_MAX_DIM * sizeof(uint16_t));
            s19->games[i].icon_alpha  = (uint8_t *)malloc((size_t)NL_ICON_MAX_DIM * NL_ICON_MAX_DIM);
            assert(s19->games[i].icon_pixels != NULL && s19->games[i].icon_alpha != NULL);
            for (int y = 0; y < NL_ICON_MAX_DIM; y++) {
                for (int x = 0; x < NL_ICON_MAX_DIM; x++) {
                    int idx = y * NL_ICON_MAX_DIM + x;
                    s19->games[i].icon_pixels[idx] = RGB565((x * 3 + i * 9) & 255, (y * 3 + i * 5) & 255, (120 + i * 4) & 255);
                    s19->games[i].icon_alpha[idx] = (x < 6 && y < 6) ? 64 : 255;
                }
            }
        }

        uint16_t *fb_a = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_b = (uint16_t *)malloc(NL_FB_BYTES);
        assert(fb_a != NULL && fb_b != NULL);

        /* 39b. Bit-for-bit cold vs warm cache equivalence across layouts & themes */
        const nl_layout_t lays19[4] = { NL_LAYOUT_4X4, NL_LAYOUT_3X3, NL_LAYOUT_3X2, NL_LAYOUT_1X1 };
        const nl_display_mode_t modes19[3] = { NL_DISPLAY_DARK, NL_DISPLAY_LIGHT, NL_DISPLAY_GRAY };
        for (int li = 0; li < 4; li++) {
            for (int mi = 0; mi < 3; mi++) {
                s19->settings.layout = lays19[li];
                s19->settings.scroll_dir = NL_SCROLL_HORIZONTAL;
                s19->display_mode = modes19[mi];
                nl_rebuild_filter(s19);
                s19->anim.scroll_x_fp = 0;
                s19->anim.scroll_y_fp = 0;
                s19->anim.dot_pos_fp = 0;
                for (int g = 0; g < s19->total_games; g++) {
                    s19->anim.card_scale_fp[g] = (g == 0) ? NL_SCALE_SELECTED : NL_SCALE_NORMAL;
                }
                nl_renderer_cleanup();
                nl_render_frame(s19, fb_a); /* Cold render -> builds sprite + static FB caches */
                nl_render_frame(s19, fb_b); /* Warm render -> hits static FB cache */
                assert(memcmp(fb_a, fb_b, NL_FB_BYTES) == 0);

                /* Force static FB cache miss via scroll offset while keeping sprite cache warm */
                s19->anim.scroll_x_fp = 16; /* 1px scroll */
                nl_render_frame(s19, fb_b); /* Warm sprite cache */
                nl_renderer_cleanup();
                nl_render_frame(s19, fb_a); /* Cold sprite cache */
                assert(memcmp(fb_a, fb_b, NL_FB_BYTES) == 0);
            }
        }

        /* 39c. In-place icon pixel mutation invalidates BOTH sprite cache & static library cache */
        s19->settings.layout = NL_LAYOUT_4X4;
        s19->display_mode = NL_DISPLAY_DARK;
        nl_rebuild_filter(s19);
        s19->anim.scroll_x_fp = 0;
        nl_render_frame(s19, fb_a);
        int vis_gi = s19->filtered_indices[1];
        for (int p = 0; p < NL_ICON_MAX_DIM * NL_ICON_MAX_DIM; p++) {
            s19->games[vis_gi].icon_pixels[p] = RGB565(255, 255, 0); /* Bright yellow in-place */
        }
        nl_render_frame(s19, fb_b);
        assert(memcmp(fb_a, fb_b, NL_FB_BYTES) != 0);

        /* 39d. Dimmed overlay cache (Quick Profile 60 FPS halo) bit-for-bit parity vs cold render */
        s19->sui.qp_open = true;
        s19->anim.qp_open_fp = 256;
        s19->anim.qp_opening = false;
        for (int f = 0; f < 8; f++) {
            nl_anim_step(s19);
            nl_render_frame(s19, fb_b); /* Warm G_DIM_BG_FB hits */
        }
        nl_renderer_cleanup();
        nl_render_frame(s19, fb_a); /* Cold rebuild at exact same halo phase */
        assert(memcmp(fb_a, fb_b, NL_FB_BYTES) == 0);
        s19->sui.qp_open = false;
        s19->anim.qp_open_fp = 0;

        /* 39e. Mouse movement updates cursor while static library cache stays valid & hover invalidates properly */
        s19->settings.touchpad_mode = NL_TP_MODE_MOUSE;
        nl_mouse_move_to(s19, 100, 100);
        nl_render_frame(s19, fb_a);
        nl_mouse_move_to(s19, 104, 100);
        nl_render_frame(s19, fb_b);
        assert(memcmp(fb_a, fb_b, NL_FB_BYTES) != 0); /* Cursor moved */
        nl_renderer_cleanup();
        nl_render_frame(s19, fb_a);
        assert(memcmp(fb_a, fb_b, NL_FB_BYTES) == 0); /* Identical to cold render */

        /* 39f. OOM recovery cleans up all renderer caches safely and re-renders cleanly */
        nl_recover_memory_on_oom(s19, "r19_test", NULL);
        nl_render_frame(s19, fb_b);
        nl_renderer_cleanup();
        nl_render_frame(s19, fb_a);
        assert(memcmp(fb_a, fb_b, NL_FB_BYTES) == 0);

        free(fb_a);
        free(fb_b);
        nl_free_icons(s19);
        free(s19);
        assert(system("rm -rf /tmp/nl_r19_suite_fs") == 0);
        printf("[PASS] Section 39 #R19 (144x144 PNG fast decode/32-bit downscale/dead-alpha pruning, bit-for-bit sprite/library/dimmed cache parity, in-place mutation invalidation & OOM cleanup) verified.\n");
    }

    /* =========================================================================
     * 40. #R20 ICON TRANSPARENCY & ENLARGEMENT FIXES, 4 LAUNCH ANIMATIONS,
     *     60 FPS STARTUP HYDRATION/INTRA-PNG PUMPING & "daniel.b" SIGNATURE
     * ========================================================================= */
    {
        assert(strstr(NL_BUILD_STAMP_STR, "#R23") != NULL);
        const char *r20_root = "/tmp/nl_r20_suite_fs";
        assert(system("rm -rf /tmp/nl_r20_suite_fs && mkdir -p /tmp/nl_r20_suite_fs/ndless/covers /tmp/nl_r20_suite_fs/ndless/icons /tmp/nl_r20_suite_fs/games") == 0);

        nl_state_t *s20 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s20 != NULL);
        nl_init_state(s20);
        snprintf(s20->sys.docs_root, sizeof(s20->sys.docs_root), "%s", r20_root);
        snprintf(s20->covers_dir, sizeof(s20->covers_dir), "%s/ndless/covers", r20_root);
        snprintf(s20->icons_dir, sizeof(s20->icons_dir), "%s/ndless/icons", r20_root);
        s20->total_games = 6;
        for (int i = 0; i < 6; i++) {
            snprintf(s20->games[i].title, sizeof(s20->games[i].title), "Game%d", i);
            snprintf(s20->games[i].filename, sizeof(s20->games[i].filename), "game%d.tns", i);
            snprintf(s20->games[i].path, sizeof(s20->games[i].path), "%s/games/game%d.tns", r20_root, i);
            s20->games[i].accent_color = RGB565(80 + i * 25, 140, 220);
        }

        /* Game 0: Transparent 84x84 PNG icon (floating diamond/cube with fully transparent corners!) */
        s20->games[0].icon_source = NL_ICON_PNG;
        s20->games[0].icon_w = NL_ICON_MAX_DIM;
        s20->games[0].icon_h = NL_ICON_MAX_DIM;
        s20->games[0].icon_pixels = (uint16_t *)malloc((size_t)NL_ICON_MAX_DIM * NL_ICON_MAX_DIM * sizeof(uint16_t));
        s20->games[0].icon_alpha  = (uint8_t *)malloc((size_t)NL_ICON_MAX_DIM * NL_ICON_MAX_DIM);
        assert(s20->games[0].icon_pixels && s20->games[0].icon_alpha);
        for (int y = 0; y < NL_ICON_MAX_DIM; y++) {
            for (int x = 0; x < NL_ICON_MAX_DIM; x++) {
                int idx = y * NL_ICON_MAX_DIM + x;
                int dx = x - NL_ICON_MAX_DIM / 2;
                int dy = y - NL_ICON_MAX_DIM / 2;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                bool inside = (dx + dy <= 28);
                s20->games[0].icon_pixels[idx] = inside ? RGB565(40, 210, 60) : RGB565(0, 0, 0);
                s20->games[0].icon_alpha[idx]  = inside ? 255 : 0;
            }
        }
        /* Game 1: No custom icon (NL_ICON_NONE) -> default launcher card must stay intact */
        s20->games[1].icon_source = NL_ICON_NONE;

        /* Game 2: Has valid .scr.tns screenshot */
        s20->games[2].has_screenshot = true;
        s20->games[2].scr_pixels = (uint16_t *)malloc(NL_COVER_BYTES);
        assert(s20->games[2].scr_pixels != NULL);
        for (int i = 0; i < NL_COVER_W * NL_COVER_H; i++) {
            s20->games[2].scr_pixels[i] = RGB565(220, 60, 120);
        }

        nl_rebuild_filter(s20);
        s20->settings.layout = NL_LAYOUT_2X2;
        s20->settings.show_shortcuts = true;
        nl_rebuild_filter(s20);

        /* 40a. Icon Fix 1A & 1B: Transparent PNG corners show EXACT th->bg_main (zero gray slab & zero border),
         *      both when unselected AND when enlarged on selection, across Dark/Light/Gray! */
        nl_layout_metrics_t lm20;
        nl_get_layout_metrics_for_count(NL_LAYOUT_2X2, s20->settings.show_names,
                                        s20->settings.show_shortcuts, s20->filtered_count, &lm20);
        int g0x = 0, g0y = 0, g1x = 0, g1y = 0;
        nl_compute_grid_slot_pos(&lm20, s20->filtered_count, 0, &g0x, &g0y);
        nl_compute_grid_slot_pos(&lm20, s20->filtered_count, 1, &g1x, &g1y);

        const nl_display_mode_t dms20[3] = { NL_DISPLAY_DARK, NL_DISPLAY_LIGHT, NL_DISPLAY_GRAY };
        for (int di = 0; di < 3; di++) {
            s20->display_mode = dms20[di];
            const nl_theme_t *th20 = nl_get_theme(s20->display_mode, s20->accent_color);
            /* Unselected Game 0 (select Game 1) */
            s20->selected_pos = 1;
            for (int g = 0; g < s20->total_games; g++) {
                s20->anim.card_scale_fp[g] = (g == 1) ? NL_SCALE_SELECTED : NL_SCALE_NORMAL;
            }
            nl_renderer_cleanup();
            nl_render_frame(s20, fb);
            /* Transparent corner of Game 0 must be exact bg_main (zero gray backdrop slab & zero 1px border!) */
            assert(fb[(g0y + 1) * NL_SCREEN_W + (g0x + 1)] == th20->bg_main);
            assert(fb[(g0y + 6) * NL_SCREEN_W + (g0x + 6)] == th20->bg_main);
            /* Center of Game 0 diamond must be vivid green (not bg_main) */
            assert(fb[(g0y + lm20.card_h / 2) * NL_SCREEN_W + (g0x + lm20.card_w / 2)] != th20->bg_main);
            /* Game 1 (default monogram card without custom icon) must still have its card background (not bg_main) */
            assert(fb[(g1y + 10) * NL_SCREEN_W + (g1x + 10)] != th20->bg_main);

            /* Selected & enlarging Game 0 (scale_fp = NL_SCALE_SELECTED) -> still ZERO gray rectangular border! */
            s20->selected_pos = 0;
            s20->anim.card_scale_fp[0] = NL_SCALE_SELECTED;
            s20->anim.card_scale_fp[1] = NL_SCALE_NORMAL;
            nl_renderer_cleanup();
            nl_render_frame(s20, fb);
            int dw0 = (lm20.card_w * NL_SCALE_SELECTED + 128) >> 8;
            int dh0 = (lm20.card_h * NL_SCALE_SELECTED + 128) >> 8;
            int dx0 = g0x - (dw0 - lm20.card_w) / 2;
            int dy0 = g0y - (dh0 - lm20.card_h) / 2;
            if (dy0 < NL_VIEW_TOP + 1) dy0 = NL_VIEW_TOP + 1;
            /* Outer corner of enlarging bounding box must be exact bg_main (no gray box border!) */
            assert(fb[(dy0 + 1) * NL_SCREEN_W + (dx0 + 1)] == th20->bg_main);
            assert(fb[(dy0 + 4) * NL_SCREEN_W + (dx0 + 4)] == th20->bg_main);
        }

        /* 40b. Ultra-discreet bottom-left "daniel.b" micro-signature check (with & without bottom bar) */
        for (int sc = 0; sc < 2; sc++) {
            s20->settings.show_shortcuts = (sc == 1);
            s20->anim.switch_shortcuts_fp = (sc == 1) ? 256 : 0;
            s20->display_mode = NL_DISPLAY_DARK;
            const nl_theme_t *th20 = nl_get_theme(s20->display_mode, s20->accent_color);
            nl_renderer_cleanup();
            nl_render_frame(s20, fb);
            uint16_t expected_bg = (sc == 1) ? th20->bg_bottom_bar : th20->bg_main;
            int sig_lit = 0;
            for (int py = NL_SCREEN_H - 6; py < NL_SCREEN_H - 1; py++) {
                for (int px = 3; px < 28; px++) {
                    if (fb[py * NL_SCREEN_W + px] != expected_bg) sig_lit++;
                }
            }
            assert(sig_lit >= 35 && sig_lit <= 80); /* Crisp 3x5 "daniel.b" pixels lit */
        }

        /* 40c. Settings -> Animations -> Animation de lancement navigation & persistence (config + profiles) */
        s20->sui.screen = NL_UI_LIBRARY;
        nl_handle_key(s20, NL_KEY_SETTINGS);
        assert(s20->sui.screen == NL_UI_SETTINGS_MAIN);
        assert(s20->sui.menu_cursor == NL_SET_MENU_DISPLAY);
        nl_handle_key(s20, NL_KEY_DOWN);
        assert(s20->sui.menu_cursor == NL_SET_MENU_ANIMATIONS);
        nl_handle_key(s20, NL_KEY_EXE);
        assert(s20->sui.screen == NL_UI_SETTINGS_ANIMATIONS);
        assert(s20->sui.anim_cursor == 0);
        nl_render_frame(s20, fb);

        /* Navigate to Row 2 (Lancement des jeux) and cycle Theme Drop -> Spinner -> Loading Bar */
        nl_handle_key(s20, NL_KEY_DOWN);
        nl_handle_key(s20, NL_KEY_DOWN);
        assert(s20->sui.anim_cursor == 2);
        nl_handle_key(s20, NL_KEY_EXE);
        assert(s20->settings.launch_anim_mode == NL_LAUNCH_ANIM_THEME_DROP);
        nl_render_frame(s20, fb);

        nl_handle_key(s20, NL_KEY_RIGHT);
        assert(s20->settings.launch_anim_mode == NL_LAUNCH_ANIM_SPINNER);
        nl_render_frame(s20, fb);

        nl_handle_key(s20, NL_KEY_EXE);
        assert(s20->settings.launch_anim_mode == NL_LAUNCH_ANIM_LOADING_BAR);
        nl_render_frame(s20, fb);

        /* Config persistence */
        const char *cfg20 = "/tmp/nl_r20_suite_fs/ndless/nlauncher.cfg.tns";
        const char *prof20 = "/tmp/nl_r20_suite_fs/ndless/nlauncher.profiles.tns";
        assert(nl_save_config(s20, cfg20) == true);
        nl_profiles_init_default(s20);
        nl_profile_snapshot_active(s20);
        assert(nl_save_profiles(s20, prof20) == true);

        nl_state_t *s20_rel = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s20_rel != NULL);
        nl_init_state(s20_rel);
        assert(nl_load_config(s20_rel, cfg20) == true);
        assert(s20_rel->settings.launch_anim_mode == NL_LAUNCH_ANIM_LOADING_BAR);
        assert(nl_load_profiles(s20_rel, prof20) == true);
        assert(s20_rel->settings.launch_anim_mode == NL_LAUNCH_ANIM_LOADING_BAR);
        nl_free_icons(s20_rel);
        free(s20_rel);

        /* 40d. Render ALL 4 launch animations across Dark/Light/Gray, with & without .scr.tns, all 5 layouts */
        s20->sui.screen = NL_UI_LIBRARY;
        uint16_t *fb_m0 = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_m1 = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_m2 = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_m3a = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_m3b = (uint16_t *)malloc(NL_FB_BYTES);
        assert(fb_m0 && fb_m1 && fb_m2 && fb_m3a && fb_m3b);

        s20->display_mode = NL_DISPLAY_DARK;
        s20->launch_game_idx = 0;
        s20->selected_pos = 0;
        s20->anim.launch_anim_active = true;

        /* Mid-frame distinctness between Mode 0 (Actuelle), Mode 1 (Theme Drop), Mode 2 (Spinner) */
        s20->settings.launch_anim_mode = NL_LAUNCH_ANIM_CURRENT;
        nl_render_launch_zoom(s20, fb_m0, 14, 30);
        s20->settings.launch_anim_mode = NL_LAUNCH_ANIM_THEME_DROP;
        nl_render_launch_zoom(s20, fb_m1, 14, 30);
        s20->settings.launch_anim_mode = NL_LAUNCH_ANIM_SPINNER;
        nl_render_launch_zoom(s20, fb_m2, 14, 30);
        assert(memcmp(fb_m0, fb_m1, NL_FB_BYTES) != 0);
        assert(memcmp(fb_m0, fb_m2, NL_FB_BYTES) != 0);
        assert(memcmp(fb_m1, fb_m2, NL_FB_BYTES) != 0);

        /* Mode 3 (Loading Bar) reflects real launch_load_pct difference (20% vs 88%) */
        s20->settings.launch_anim_mode = NL_LAUNCH_ANIM_LOADING_BAR;
        s20->anim.launch_load_pct = 20;
        nl_render_launch_zoom(s20, fb_m3a, 24, 38);
        s20->anim.launch_load_pct = 88;
        nl_render_launch_zoom(s20, fb_m3b, 24, 38);
        assert(memcmp(fb_m3a, fb_m3b, NL_FB_BYTES) != 0);

        /* Exercise all 4 modes x 3 themes x Game 0 (transparent PNG) & Game 2 (.scr.tns) without crashes */
        for (int am = 0; am < NL_LAUNCH_ANIM_COUNT; am++) {
            s20->settings.launch_anim_mode = (nl_launch_anim_t)am;
            for (int di = 0; di < 3; di++) {
                s20->display_mode = dms20[di];
                s20->launch_game_idx = (di == 2) ? 2 : 0;
                s20->anim.launch_load_pct = 50;
                nl_render_launch_zoom(s20, fb, 1, 30);
                nl_render_launch_zoom(s20, fb, 15, 30);
                s20->anim.launch_load_pct = 100;
                nl_render_launch_zoom(s20, fb, 30, 30);
            }
        }
        s20->anim.launch_anim_active = false;

        /* 40e. Warm-boot nl_hydrate_icons_from_cache & nl_probe_all_screenshots_ex callback pumping */
        const char *cache20 = "/tmp/nl_r20_suite_fs/ndless/nlauncher.cache.tns";
        assert(nl_save_library_cache(s20, cache20) == true);
        nl_free_game_icons_only(s20);
        assert(s20->games[0].icon_pixels == NULL);
        int hyd_cnt = nl_hydrate_icons_from_cache(s20, cache20, NULL, NULL);
        assert(hyd_cnt == 1);
        assert(s20->games[0].icon_source == NL_ICON_PNG);
        assert(s20->games[0].icon_pixels != NULL && s20->games[0].icon_alpha != NULL);
        assert(s20->games[0].icon_alpha[0] == 0);
        assert(s20->games[0].icon_alpha[(NL_ICON_MAX_DIM / 2) * NL_ICON_MAX_DIM + (NL_ICON_MAX_DIM / 2)] == 255);
        nl_probe_all_screenshots_ex(s20, NULL, NULL);

        free(fb_m0); free(fb_m1); free(fb_m2); free(fb_m3a); free(fb_m3b);
        nl_free_icons(s20);
        free(s20);
        assert(system("rm -rf /tmp/nl_r20_suite_fs") == 0);
        printf("[PASS] Section 40 #R20 (Icon transparency & zoom border fixes, 4 launch animations, Settings->Animations persistence, warm-boot cache hydration & daniel.b micro-signature) verified.\n");
    }

    /* =========================================================================
     * 41. #R21 FAST GAME-RETURN OPTIMIZATION (EXEC SPRITE RETENTION),
     *     NON-BLOCKING RETURN DROPLET REVEAL & PROFILE DROPLET ANIMATIONS
     * ========================================================================= */
    {
        assert(strstr(NL_BUILD_STAMP_STR, "#R23") != NULL);
        nl_renderer_cleanup();
        assert(nl_renderer_cached_sprite_count() == 0);

        nl_state_t *s21 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s21 != NULL);
        nl_init_state(s21);
        nl_profiles_init_default(s21);
        s21->settings.layout = NL_LAYOUT_3X3;
        s21->settings.scroll_dir = NL_SCROLL_HORIZONTAL;
        s21->total_games = 18;

        /* Populate 18 games across 2 pages (3x3) with synthetic pre-decoded 84x84 icons (downscaled into G_CARD_CACHE) */
        for (int i = 0; i < 18; i++) {
            snprintf(s21->games[i].title, sizeof(s21->games[i].title), "Game%02d", i + 1);
            snprintf(s21->games[i].filename, sizeof(s21->games[i].filename), "game%02d.tns", i + 1);
            snprintf(s21->games[i].path, sizeof(s21->games[i].path), "/documents/ndless/game%02d.tns", i + 1);
            s21->games[i].icon_w = 84;
            s21->games[i].icon_h = 84;
            s21->games[i].icon_source = NL_ICON_PNG;
            s21->games[i].icon_pixels = (uint16_t *)malloc(84U * 84U * sizeof(uint16_t));
            s21->games[i].icon_alpha  = (uint8_t *)malloc(84U * 84U);
            assert(s21->games[i].icon_pixels != NULL && s21->games[i].icon_alpha != NULL);
            uint16_t col = (uint16_t)(((i * 3 + 7) << 11) | ((i * 5 + 12) << 5) | (i * 2 + 9));
            for (int p = 0; p < 84 * 84; p++) {
                s21->games[i].icon_pixels[p] = col;
                s21->games[i].icon_alpha[p] = 255U;
            }
        }
        nl_rebuild_filter(s21);
        for (int st = 0; st < 20; st++) (void)nl_anim_step(s21);

        /* 41a. Verify nl_renderer_prepare_for_exec preserves visible-page sprites while evicting off-page sprites across 5 launches */
        uint16_t *fb_base = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_ret  = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_qp0  = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_qp1  = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_pl   = (uint16_t *)malloc(NL_FB_BYTES);
        assert(fb_base && fb_ret && fb_qp0 && fb_qp1 && fb_pl);

        for (int cycle = 0; cycle < 5; cycle++) {
            /* Touch page 1 first so off-page sprites populate cache, then return to page 0 */
            s21->selected_pos = 9 + (cycle % 9);
            for (int st = 0; st < 15; st++) (void)nl_anim_step(s21);
            nl_render_frame(s21, fb_base);

            s21->selected_pos = cycle % 9;
            for (int st = 0; st < 15; st++) (void)nl_anim_step(s21);
            nl_render_frame(s21, fb_base);
            int before_exec = nl_renderer_cached_sprite_count();
            assert(before_exec >= 12);

            /* Prepare for child game exec: frees transient FBs + off-page sprites while keeping visible page sprites warm */
            nl_renderer_prepare_for_exec(s21);
            int kept_after_exec = nl_renderer_cached_sprite_count();
            assert(kept_after_exec >= 8 && kept_after_exec <= before_exec);

            /* Return immediately with non-blocking droplet reveal */
            nl_return_drop_start(s21, s21->filtered_indices[s21->selected_pos]);
            assert(s21->anim.return_drop_active == true);
            assert(s21->anim.return_drop_tick == 0);
            nl_render_frame(s21, fb_ret);
            int diff_ret = 0;
            for (int p = 0; p < NL_FB_PIXELS; p++) {
                if (fb_ret[p] != fb_base[p]) diff_ret++;
            }
            assert(diff_ret > 800); /* Visible luminous droplet reveal + soft veil on Frame 1 */
        }

        /* 41b. Verify Return Droplet animation works across all 3 themes (Dark, Light, Gray)
         *      AND is strictly non-blocking (navigation, Settings, and re-launch work immediately!) */
        nl_display_mode_t dms21[3] = { NL_DISPLAY_DARK, NL_DISPLAY_LIGHT, NL_DISPLAY_GRAY };
        for (int di = 0; di < 3; di++) {
            s21->display_mode = dms21[di];
            nl_return_drop_start(s21, 0);
            for (int t = 0; t < 25; t++) {
                nl_render_frame(s21, fb_ret);
                (void)nl_anim_step(s21);
            }
            assert(s21->anim.return_drop_active == false);
        }

        /* Immediate navigation during active return droplet animation MUST succeed with zero delay */
        s21->display_mode = NL_DISPLAY_DARK;
        s21->selected_pos = 0;
        nl_return_drop_start(s21, 0);
        assert(s21->anim.return_drop_active == true);
        assert(nl_handle_key(s21, NL_KEY_RIGHT) == true);
        assert(s21->selected_pos == 1);
        assert(s21->anim.return_drop_active == true); /* Navigation stays live while droplets smoothly finish */

        /* Opening Settings or launching immediately cancels return droplet reveal cleanly */
        assert(nl_handle_key(s21, NL_KEY_SETTINGS) == true);
        assert(s21->sui.screen == NL_UI_SETTINGS_MAIN);
        assert(s21->anim.return_drop_active == false);
        assert(nl_handle_key(s21, NL_KEY_ESC) == true);
        assert(s21->sui.screen == NL_UI_LIBRARY);

        /* 41c. Verify Quick Profile opening droplets & '+' button droplets render cleanly across themes */
        for (int di = 0; di < 3; di++) {
            s21->display_mode = dms21[di];
            s21->sui.qp_open = true;
            s21->anim.qp_opening = true;
            s21->anim.qp_open_fp = 110;
            nl_render_frame(s21, fb_qp0);
            s21->anim.qp_open_fp = 256;
            nl_render_frame(s21, fb_qp1);
            int diff_qp = 0;
            for (int p = 0; p < NL_FB_PIXELS; p++) {
                if (fb_qp0[p] != fb_qp1[p]) diff_qp++;
            }
            assert(diff_qp > 500);

            /* '+' button transition droplets */
            s21->anim.prof_plus_active = true;
            s21->anim.prof_plus_tick = 7;
            s21->anim.prof_plus_max = 14;
            s21->anim.prof_plus_origin_x = 182;
            s21->anim.prof_plus_origin_y = 120;
            nl_render_frame(s21, fb_pl);
            s21->anim.prof_plus_active = false;
            s21->sui.qp_open = false;
            s21->anim.qp_open_fp = 0;

            /* Avatar picker opening trailing droplets */
            s21->sui.qp_pick_open = true;
            s21->anim.qp_pick_fp = 96;
            nl_render_frame(s21, fb_pl);
            s21->sui.qp_pick_open = false;
            s21->anim.qp_pick_fp = 0;
        }

        free(fb_base); free(fb_ret); free(fb_qp0); free(fb_qp1); free(fb_pl);
        nl_free_icons(s21);
        nl_profiles_free(s21);
        free(s21);
        printf("[PASS] Section 41 #R21 (Fast game-return warm sprite retention across 5 launches, non-blocking return reveal across 3 themes, immediate input responsiveness, and Quick Profile / '+' animations) verified.\n");
    }

    /* ========================================================================
     * 42. #R22 UNIFIED 4-LAYER ORGANIC LIQUID PROPAGATION VERIFICATION
     *     - Organic asymmetry at start (irreg=256) -> progressive rounding (irreg=22)
     *     - 4-color theme progression luminance ordering (Dark, Light, Gray)
     *     - Game Launch (NL_LAUNCH_ANIM_THEME_DROP) 4-wave full-screen coverage
     *     - Non-blocking Game Return reverse 4-layer organic reveal
     *     - Profile Open, Dedicated Profile Close, Profile Switch, '+' & Avatar Picker + Birth
     * ======================================================================== */
    {
        assert(strstr(NL_BUILD_STAMP_STR, "#R23") != NULL);

        /* 42a. Verify organic contour starts visibly irregular (non-circular) & progressively rounds out */
        int min_start = 9999, max_start = 0;
        int min_end = 9999, max_end = 0;
        for (int a = 0; a < 256; a += 4) {
            int rs = nl_liquid_eval_radius(a, 100, 256, 0x1107U, 0);
            int re = nl_liquid_eval_radius(a, 100, 22,  0x1107U, 28);
            if (rs < min_start) min_start = rs;
            if (rs > max_start) max_start = rs;
            if (re < min_end)   min_end   = re;
            if (re > max_end)   max_end   = re;
        }
        int spread_start = max_start - min_start;
        int spread_end   = max_end - min_end;
        assert(spread_start >= 22);       /* Clearly irregular & asymmetric organic contour at start (>= 22%) */
        assert(spread_end <= 6);          /* Progressively rounded out at end (<= 6%) */
        assert(spread_start > spread_end * 4);

        /* 42b. Verify exact 4-color theme progression luminance ordering for Dark, Light, Gray */
        nl_accent_color_t accents22[4] = { NL_ACCENT_BLUE, NL_ACCENT_RED, NL_ACCENT_GREEN, NL_ACCENT_YELLOW };
        for (int ai = 0; ai < 4; ai++) {
            int lum_dark[4], lum_light[4], lum_gray[4];
            for (int l = 0; l < 4; l++) {
                uint16_t cd = nl_liquid_theme_color_rgb565(NL_DISPLAY_DARK,  accents22[ai], l);
                uint16_t cl = nl_liquid_theme_color_rgb565(NL_DISPLAY_LIGHT, accents22[ai], l);
                uint16_t cg = nl_liquid_theme_color_rgb565(NL_DISPLAY_GRAY,  accents22[ai], l);
                lum_dark[l]  = (int)((cd >> 11) & 31) * 3 + (int)((cd >> 5) & 63) * 3 + (int)(cd & 31);
                lum_light[l] = (int)((cl >> 11) & 31) * 3 + (int)((cl >> 5) & 63) * 3 + (int)(cl & 31);
                lum_gray[l]  = (int)((cg >> 11) & 31) * 3 + (int)((cg >> 5) & 63) * 3 + (int)(cg & 31);
            }
            /* Dark: lightest -> darkest */
            assert(lum_dark[0] > lum_dark[1] && lum_dark[1] > lum_dark[2] && lum_dark[2] > lum_dark[3]);
            /* Light: darkest -> lightest */
            assert(lum_light[0] < lum_light[1] && lum_light[1] < lum_light[2] && lum_light[2] < lum_light[3]);
            /* Gray: lightest -> increasingly grayed */
            assert(lum_gray[0] > lum_gray[1] && lum_gray[1] > lum_gray[2] && lum_gray[2] > lum_gray[3]);
        }

        /* 42c. Render full 30-frame Game Launch (Theme Drop), 22-frame Game Return, and all 4 Profile transitions */
        nl_state_t *s22 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        uint16_t *fb22  = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb22b = (uint16_t *)malloc(NL_FB_BYTES);
        assert(s22 != NULL && fb22 != NULL && fb22b != NULL);
        nl_init_state(s22);
        nl_profiles_init_default(s22);
        s22->total_games = 6;
        for (int i = 0; i < 6; i++) {
            snprintf(s22->games[i].title, sizeof(s22->games[i].title), "Liquid%d", i + 1);
            snprintf(s22->games[i].filename, sizeof(s22->games[i].filename), "liquid%d.tns", i + 1);
            snprintf(s22->games[i].path, sizeof(s22->games[i].path), "/documents/games/liquid%d.tns", i + 1);
        }
        nl_rebuild_filter(s22);
        s22->settings.launch_anim_mode = NL_LAUNCH_ANIM_THEME_DROP;

        nl_display_mode_t dms22[3] = { NL_DISPLAY_DARK, NL_DISPLAY_LIGHT, NL_DISPLAY_GRAY };
        for (int di = 0; di < 3; di++) {
            s22->display_mode = dms22[di];

            /* Game Launch 4-layer organic propagation (frames 0..30) */
            for (int step = 0; step <= 30; step++) {
                nl_render_launch_zoom(s22, fb22, step, 30);
            }
            /* Final frame must cleanly cover full 320x240 screen with layer 3 final theme color */
            uint16_t final_c3 = nl_liquid_theme_color_rgb565(dms22[di], s22->accent_color, 3);
            assert(fb22[0] == final_c3);
            assert(fb22[NL_SCREEN_W - 1] == final_c3);
            assert(fb22[(NL_SCREEN_H - 1) * NL_SCREEN_W] == final_c3);
            assert(fb22[NL_FB_PIXELS - 1] == final_c3);
            assert(fb22[120 * NL_SCREEN_W + 160] == final_c3);

            /* Game Return non-blocking reverse 4-layer organic reveal */
            nl_render_frame(s22, fb22b); /* Pure library reference */
            nl_return_drop_start(s22, 0);
            assert(s22->anim.return_drop_max == 18);
            for (int rt = 0; rt < 24; rt++) {
                nl_render_frame(s22, fb22);
                if (rt == 6) {
                    /* Early-mid reveal: corners still carry liquid bands while live aperture is opening */
                    int diff_mid = 0;
                    for (int px = 0; px < NL_FB_PIXELS; px++) {
                        if (fb22[px] != fb22b[px]) diff_mid++;
                    }
                    assert(diff_mid > 1000 && diff_mid < NL_FB_PIXELS);
                }
                (void)nl_anim_step(s22);
            }
            assert(s22->anim.return_drop_active == false);

            /* Profile A: Quick Profile Opening organic liquid propagation */
            s22->sui.qp_open = true;
            s22->anim.qp_opening = true;
            s22->anim.qp_open_fp = 128;
            nl_render_frame(s22, fb22);

            /* Profile D: Dedicated Quick Profile Closing organic liquid propagation */
            s22->sui.qp_open = false;
            s22->anim.qp_opening = false;
            s22->anim.qp_open_fp = 128;
            nl_render_frame(s22, fb22b);
            int diff_open_close = 0;
            for (int px = 0; px < NL_FB_PIXELS; px++) {
                if (fb22[px] != fb22b[px]) diff_open_close++;
            }
            assert(diff_open_close > 500); /* Closing propagation is visually distinct from opening! */
            s22->anim.qp_open_fp = 0;

            /* Profile B: Profile Switch enriched with 4-layer organic liquid propagation */
            s22->anim.prof_switch_active = true;
            s22->anim.prof_switch_max = 26;
            s22->anim.prof_switch_origin_x = 116;
            s22->anim.prof_switch_origin_y = 120;
            s22->anim.prof_switch_origin_d = 34;
            s22->anim.prof_switch_avatar_id = 2;
            s22->anim.prof_switch_from_avatar_id = 0;
            for (int pt = 0; pt < 26; pt += 4) {
                s22->anim.prof_switch_tick = pt;
                nl_render_frame(s22, fb22);
            }
            s22->anim.prof_switch_active = false;

            /* Profile C: '+' Button + Avatar Picker Open/Close + Profile Birth organic liquid */
            s22->sui.qp_open = true;
            s22->anim.qp_open_fp = 256;
            s22->anim.prof_plus_active = true;
            s22->anim.prof_plus_max = 16;
            s22->anim.prof_plus_origin_x = 182;
            s22->anim.prof_plus_origin_y = 120;
            for (int pt = 0; pt < 16; pt += 3) {
                s22->anim.prof_plus_tick = pt;
                nl_render_frame(s22, fb22);
            }
            s22->anim.prof_plus_active = false;
            s22->sui.qp_open = false;
            s22->anim.qp_open_fp = 0;

            s22->sui.qp_pick_open = true;
            s22->anim.qp_pick_fp = 112;
            nl_render_frame(s22, fb22);
            s22->sui.qp_pick_open = false;
            s22->anim.qp_pick_fp = 112;
            nl_render_frame(s22, fb22);
            s22->anim.qp_pick_fp = 0;

            s22->sui.qp_open = true;
            s22->anim.qp_open_fp = 256;
            s22->anim.prof_birth_active = true;
            s22->anim.prof_birth_max = 18;
            s22->anim.prof_birth_tick = 8;
            s22->anim.prof_birth_avatar_id = 5;
            s22->anim.prof_birth_slot = 0;
            nl_render_frame(s22, fb22);
            s22->anim.prof_birth_active = false;
            s22->sui.qp_open = false;
            s22->anim.qp_open_fp = 0;
        }

        free(fb22); free(fb22b);
        nl_profiles_free(s22);
        free(s22);
        printf("[PASS] Section 42 #R22 (Unified 4-layer organic liquid propagation: irregular->rounded contour, Dark/Light/Gray 4-color progressions, Game Launch, non-blocking Game Return reveal, and Profile Open/Close/Switch/+/Birth) verified.\n");
    }

    /* ========================================================================
     * 43. #R23 ULTRA-FAST GAME RETURN (CX II-T STATIC FB + ADJACENT PEEK WARM
     *     SPRITES + CONDITIONAL FILTER REBUILD) & 4 INDEPENDENT ANIMATION
     *     CATEGORIES (APP OPEN / APP CLOSE / GAME LAUNCH / GAME RETURN)
     * ======================================================================== */
    {
        assert(strstr(NL_BUILD_STAMP_STR, "#R23") != NULL);
        nl_renderer_cleanup();
        assert(system("rm -rf /tmp/nl_r23_suite_fs && mkdir -p /tmp/nl_r23_suite_fs/ndless") == 0);

        nl_state_t *s23 = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        uint16_t *fb_a  = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_b  = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_c  = (uint16_t *)malloc(NL_FB_BYTES);
        uint16_t *fb_d  = (uint16_t *)malloc(NL_FB_BYTES);
        assert(s23 && fb_a && fb_b && fb_c && fb_d);

        nl_init_state(s23);
        nl_profiles_init_default(s23);
        s23->sys.hw_is_cx2 = true; /* TI-Nspire CX II-T (64 MB RAM) */
        s23->settings.layout = NL_LAYOUT_3X3;
        s23->settings.scroll_dir = NL_SCROLL_HORIZONTAL;

        /* Verify #R23 defaults for all 4 independent animation categories */
        assert(s23->settings.app_open_anim    == NL_OPEN_ANIM_CURRENT);
        assert(s23->settings.app_close_anim   == NL_CLOSE_ANIM_LIQUID);
        assert(s23->settings.launch_anim_mode == NL_LAUNCH_ANIM_CURRENT);
        assert(s23->settings.return_anim_mode == NL_RETURN_ANIM_LIQUID);

        /* 43a. Strict independence across all 4 rows in Settings -> Animations */
        s23->sui.screen = NL_UI_LIBRARY;
        nl_handle_key(s23, NL_KEY_SETTINGS);
        nl_handle_key(s23, NL_KEY_DOWN);
        nl_handle_key(s23, NL_KEY_EXE);
        assert(s23->sui.screen == NL_UI_SETTINGS_ANIMATIONS);
        assert(s23->sui.anim_cursor == 0);

        /* Row 0: Ouverture de l'app -> change to ZOOM, verify rows 1/2/3 untouched */
        nl_handle_key(s23, NL_KEY_RIGHT);
        assert(s23->settings.app_open_anim == NL_OPEN_ANIM_LIQUID);
        nl_handle_key(s23, NL_KEY_EXE);
        assert(s23->settings.app_open_anim    == NL_OPEN_ANIM_ZOOM);
        assert(s23->settings.app_close_anim   == NL_CLOSE_ANIM_LIQUID);
        assert(s23->settings.launch_anim_mode == NL_LAUNCH_ANIM_CURRENT);
        assert(s23->settings.return_anim_mode == NL_RETURN_ANIM_LIQUID);

        /* Row 1: Fermeture de l'app -> change to FADE, verify rows 0/2/3 untouched */
        nl_handle_key(s23, NL_KEY_DOWN);
        assert(s23->sui.anim_cursor == 1);
        nl_handle_key(s23, NL_KEY_RIGHT);
        assert(s23->settings.app_open_anim    == NL_OPEN_ANIM_ZOOM);
        assert(s23->settings.app_close_anim   == NL_CLOSE_ANIM_FADE);
        assert(s23->settings.launch_anim_mode == NL_LAUNCH_ANIM_CURRENT);
        assert(s23->settings.return_anim_mode == NL_RETURN_ANIM_LIQUID);

        /* Row 2: Lancement des jeux -> change to SPINNER, verify rows 0/1/3 untouched */
        nl_handle_key(s23, NL_KEY_DOWN);
        assert(s23->sui.anim_cursor == 2);
        nl_handle_key(s23, NL_KEY_RIGHT);
        nl_handle_key(s23, NL_KEY_RIGHT);
        assert(s23->settings.app_open_anim    == NL_OPEN_ANIM_ZOOM);
        assert(s23->settings.app_close_anim   == NL_CLOSE_ANIM_FADE);
        assert(s23->settings.launch_anim_mode == NL_LAUNCH_ANIM_SPINNER);
        assert(s23->settings.return_anim_mode == NL_RETURN_ANIM_LIQUID);

        /* Row 3: Retour des jeux -> change to ZOOM via LEFT wrap/cycle, verify rows 0/1/2 untouched */
        nl_handle_key(s23, NL_KEY_DOWN);
        assert(s23->sui.anim_cursor == 3);
        nl_handle_key(s23, NL_KEY_RIGHT);
        assert(s23->settings.app_open_anim    == NL_OPEN_ANIM_ZOOM);
        assert(s23->settings.app_close_anim   == NL_CLOSE_ANIM_FADE);
        assert(s23->settings.launch_anim_mode == NL_LAUNCH_ANIM_SPINNER);
        assert(s23->settings.return_anim_mode == NL_RETURN_ANIM_ZOOM);

        /* Mouse click on Row 1 advances ONLY Row 1 (FADE -> ZOOM) */
        s23->sui.ctrl_mouse_active = true;
        nl_mouse_move_to(s23, 160, 48 + 1 * 28 + 10);
        assert(nl_mouse_click(s23) == true);
        s23->sui.ctrl_mouse_active = false;
        assert(s23->sui.anim_cursor == 1);
        assert(s23->settings.app_close_anim   == NL_CLOSE_ANIM_ZOOM);
        assert(s23->settings.app_open_anim    == NL_OPEN_ANIM_ZOOM);
        assert(s23->settings.launch_anim_mode == NL_LAUNCH_ANIM_SPINNER);
        assert(s23->settings.return_anim_mode == NL_RETURN_ANIM_ZOOM);

        /* Render all 4 selected rows cleanly */
        for (int r = 0; r < NL_ANIM_CAT_COUNT; r++) {
            s23->sui.anim_cursor = r;
            nl_render_frame(s23, fb_a);
        }

        /* 43b. Full persistence in config AND profiles + profile switch isolation + factory reset */
        const char *cfg23  = "/tmp/nl_r23_suite_fs/ndless/nlauncher.cfg.tns";
        const char *prof23 = "/tmp/nl_r23_suite_fs/ndless/nlauncher.profiles.tns";
        assert(nl_save_config(s23, cfg23) == true);
        nl_profile_snapshot_active(s23);
        int p2_idx = nl_profile_create(s23, "FastProf", 3);
        assert(p2_idx == 1);
        s23->settings.app_open_anim    = NL_OPEN_ANIM_INSTANT;
        s23->settings.app_close_anim   = NL_CLOSE_ANIM_INSTANT;
        s23->settings.launch_anim_mode = NL_LAUNCH_ANIM_LOADING_BAR;
        s23->settings.return_anim_mode = NL_RETURN_ANIM_INSTANT;
        nl_profile_snapshot_active(s23);
        assert(nl_save_profiles(s23, prof23) == true);

        /* Switch back to profile 0 and verify profile 0's 4 categories restored */
        assert(nl_profile_switch_to(s23, 0) == true);
        assert(s23->settings.app_open_anim    == NL_OPEN_ANIM_ZOOM);
        assert(s23->settings.app_close_anim   == NL_CLOSE_ANIM_ZOOM);
        assert(s23->settings.launch_anim_mode == NL_LAUNCH_ANIM_SPINNER);
        assert(s23->settings.return_anim_mode == NL_RETURN_ANIM_ZOOM);

        /* Reload into fresh state */
        nl_state_t *s23_rel = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(s23_rel != NULL);
        nl_init_state(s23_rel);
        assert(nl_load_config(s23_rel, cfg23) == true);
        assert(s23_rel->settings.app_open_anim    == NL_OPEN_ANIM_ZOOM);
        assert(s23_rel->settings.app_close_anim   == NL_CLOSE_ANIM_ZOOM);
        assert(s23_rel->settings.launch_anim_mode == NL_LAUNCH_ANIM_SPINNER);
        assert(s23_rel->settings.return_anim_mode == NL_RETURN_ANIM_ZOOM);
        assert(nl_load_profiles(s23_rel, prof23) == true);
        assert(s23_rel->profiles.count == 2);
        assert(s23_rel->profiles.items[1].app_open_anim    == NL_OPEN_ANIM_INSTANT);
        assert(s23_rel->profiles.items[1].app_close_anim   == NL_CLOSE_ANIM_INSTANT);
        assert(s23_rel->profiles.items[1].launch_anim_mode == NL_LAUNCH_ANIM_LOADING_BAR);
        assert(s23_rel->profiles.items[1].return_anim_mode == NL_RETURN_ANIM_INSTANT);

        /* Factory reset restores all 4 categories to defaults */
        assert(nl_reset_settings_only(s23_rel, cfg23) == true);
        assert(s23_rel->settings.app_open_anim    == NL_OPEN_ANIM_CURRENT);
        assert(s23_rel->settings.app_close_anim   == NL_CLOSE_ANIM_LIQUID);
        assert(s23_rel->settings.launch_anim_mode == NL_LAUNCH_ANIM_CURRENT);
        assert(s23_rel->settings.return_anim_mode == NL_RETURN_ANIM_LIQUID);
        nl_profiles_free(s23_rel);
        free(s23_rel);

        /* 43c. CX II-T Fast-Return verification:
         *      - 36 games across 4 pages (3x3) with 84x84 icons
         *      - G_LIB_STATIC_FB stays cached across nl_renderer_prepare_for_exec (hw_is_cx2 == true)
         *      - Current page 0 + adjacent peek page 1 stay warm, while distant page 3 is evicted
         *      - nl_return_needs_filter_rebuild() skips rebuild for ALPHA_ASC, requires for LAST_USED / RECENT
         */
        s23->sui.screen = NL_UI_LIBRARY;
        s23->settings.layout = NL_LAYOUT_3X3;
        s23->settings.scroll_dir = NL_SCROLL_HORIZONTAL;
        s23->total_games = 36;
        for (int i = 0; i < 36; i++) {
            snprintf(s23->games[i].title, sizeof(s23->games[i].title), "Fast%02d", i + 1);
            snprintf(s23->games[i].filename, sizeof(s23->games[i].filename), "fast%02d.tns", i + 1);
            snprintf(s23->games[i].path, sizeof(s23->games[i].path), "/documents/ndless/fast%02d.tns", i + 1);
            s23->games[i].icon_w = 84;
            s23->games[i].icon_h = 84;
            s23->games[i].icon_source = NL_ICON_PNG;
            s23->games[i].icon_pixels = (uint16_t *)malloc(84U * 84U * sizeof(uint16_t));
            s23->games[i].icon_alpha  = (uint8_t *)malloc(84U * 84U);
            assert(s23->games[i].icon_pixels && s23->games[i].icon_alpha);
            uint16_t col = (uint16_t)((((i * 5 + 3) & 31) << 11) | (((i * 7 + 9) & 63) << 5) | ((i * 3 + 11) & 31));
            for (int p = 0; p < 84 * 84; p++) {
                s23->games[i].icon_pixels[p] = col;
                s23->games[i].icon_alpha[p] = 255U;
            }
        }
        nl_rebuild_filter(s23);

        s23->active_category = NL_CAT_ALL;
        s23->settings.sort_order = NL_SORT_ALPHA_ASC;
        assert(nl_return_needs_filter_rebuild(s23) == false);
        s23->settings.sort_order = NL_SORT_ALPHA_DESC;
        assert(nl_return_needs_filter_rebuild(s23) == false);
        s23->settings.sort_order = NL_SORT_LAST_USED;
        assert(nl_return_needs_filter_rebuild(s23) == true);
        s23->settings.sort_order = NL_SORT_MOST_LAUNCHED;
        assert(nl_return_needs_filter_rebuild(s23) == true);
        s23->settings.sort_order = NL_SORT_PLAY_TIME;
        assert(nl_return_needs_filter_rebuild(s23) == true);
        s23->settings.sort_order = NL_SORT_ALPHA_ASC;
        s23->active_category = NL_CAT_RECENT;
        assert(nl_return_needs_filter_rebuild(s23) == true);
        s23->active_category = NL_CAT_ALL;

        /* Warm all 4 pages via real horizontal navigation, then return to page 0 */
        s23->selected_pos = 0;
        nl_rebuild_filter(s23);
        nl_render_frame(s23, fb_a);
        for (int pstep = 0; pstep < 3; pstep++) {
            for (int k = 0; k < 3; k++) (void)nl_handle_key(s23, NL_KEY_RIGHT);
            for (int st = 0; st < 25; st++) (void)nl_anim_step(s23);
            nl_render_frame(s23, fb_a);
        }
        for (int pstep = 0; pstep < 3; pstep++) {
            for (int k = 0; k < 3; k++) (void)nl_handle_key(s23, NL_KEY_LEFT);
            for (int st = 0; st < 25; st++) (void)nl_anim_step(s23);
            nl_render_frame(s23, fb_a);
        }
        assert(s23->current_page == 0);
        int before23 = nl_renderer_cached_sprite_count();
        assert(before23 >= 24);
        assert(nl_renderer_has_static_fb_cached() == true);

        nl_renderer_prepare_for_exec(s23);
        int after23 = nl_renderer_cached_sprite_count();
        assert(nl_renderer_has_static_fb_cached() == true);  /* CX II-T preserves G_LIB_STATIC_FB! */
        assert(after23 >= 12 && after23 < before23);         /* Keeps page 0 + page 1 peek, evicts distant pages 2-3 */

        /* Verify CX I (hw_is_cx2 == false) still frees static FB to protect tighter 32 MB RAM */
        s23->sys.hw_is_cx2 = false;
        nl_renderer_prepare_for_exec(s23);
        assert(nl_renderer_has_static_fb_cached() == false);
        s23->sys.hw_is_cx2 = true;
        nl_render_frame(s23, fb_a);

        /* 43d. Render ALL 4 App Open, ALL 4 App Close, and ALL 4 Return modes with distinct visual outputs */
        nl_display_mode_t dms23[3] = { NL_DISPLAY_DARK, NL_DISPLAY_LIGHT, NL_DISPLAY_GRAY };
        nl_render_splash(s23, fb_b, 20, 20); /* Splash reference */
        for (int di = 0; di < 3; di++) {
            s23->display_mode = dms23[di];
            nl_render_frame(s23, fb_a); /* Library reference */

            /* App Open: CURRENT vs LIQUID vs ZOOM vs INSTANT at mid-frame */
            s23->settings.app_open_anim = NL_OPEN_ANIM_CURRENT;
            nl_render_boot_transition(s23, fb_c, fb_b, fb_a, 8, 16);
            s23->settings.app_open_anim = NL_OPEN_ANIM_LIQUID;
            nl_render_boot_transition(s23, fb_d, fb_b, fb_a, 8, 16);
            assert(memcmp(fb_c, fb_d, NL_FB_BYTES) != 0);
            s23->settings.app_open_anim = NL_OPEN_ANIM_ZOOM;
            nl_render_boot_transition(s23, fb_c, fb_b, fb_a, 8, 16);
            assert(memcmp(fb_c, fb_d, NL_FB_BYTES) != 0);
            s23->settings.app_open_anim = NL_OPEN_ANIM_INSTANT;
            nl_render_boot_transition(s23, fb_c, fb_b, fb_a, 8, 16);
            assert(memcmp(fb_c, fb_a, NL_FB_BYTES) == 0);

            /* App Close: LIQUID vs FADE vs ZOOM vs INSTANT at mid-frame */
            s23->settings.app_close_anim = NL_CLOSE_ANIM_LIQUID;
            nl_render_app_close_anim(s23, fb_c, fb_a, 6, 12);
            s23->settings.app_close_anim = NL_CLOSE_ANIM_FADE;
            nl_render_app_close_anim(s23, fb_d, fb_a, 5, 10);
            assert(memcmp(fb_c, fb_d, NL_FB_BYTES) != 0);
            s23->settings.app_close_anim = NL_CLOSE_ANIM_ZOOM;
            nl_render_app_close_anim(s23, fb_c, fb_a, 5, 10);
            assert(memcmp(fb_c, fb_d, NL_FB_BYTES) != 0);
            s23->settings.app_close_anim = NL_CLOSE_ANIM_INSTANT;
            nl_render_app_close_anim(s23, fb_c, fb_a, 1, 1);

            /* Game Return: LIQUID (18t) vs ZOOM (14t) vs FADE (10t) vs INSTANT (0t) */
            s23->settings.return_anim_mode = NL_RETURN_ANIM_INSTANT;
            nl_return_drop_start(s23, 0);
            assert(s23->anim.return_drop_active == false);

            s23->settings.return_anim_mode = NL_RETURN_ANIM_FADE;
            nl_return_drop_start(s23, 0);
            assert(s23->anim.return_drop_active == true && s23->anim.return_drop_max == 10);
            s23->anim.return_drop_tick = 4;
            nl_render_frame(s23, fb_c);

            s23->settings.return_anim_mode = NL_RETURN_ANIM_ZOOM;
            nl_return_drop_start(s23, 0);
            assert(s23->anim.return_drop_active == true && s23->anim.return_drop_max == 14);
            s23->anim.return_drop_tick = 5;
            nl_render_frame(s23, fb_d);
            assert(memcmp(fb_c, fb_d, NL_FB_BYTES) != 0);

            s23->settings.return_anim_mode = NL_RETURN_ANIM_LIQUID;
            nl_return_drop_start(s23, 0);
            assert(s23->anim.return_drop_active == true && s23->anim.return_drop_max == 18);
            s23->anim.return_drop_tick = 6;
            nl_render_frame(s23, fb_c);
            assert(memcmp(fb_c, fb_d, NL_FB_BYTES) != 0);
            s23->anim.return_drop_active = false;
        }

        free(fb_a); free(fb_b); free(fb_c); free(fb_d);
        nl_free_icons(s23);
        nl_profiles_free(s23);
        free(s23);
        assert(system("rm -rf /tmp/nl_r23_suite_fs") == 0);
        printf("[PASS] Section 43 #R23 (Ultra-fast CX II-T game return: preserved static FB & adjacent peek sprites + conditional filter rebuild; 4 independent animation categories: App Open / App Close / Game Launch / Game Return + persistence + profiles + factory reset) verified.\n");
    }

    nl_renderer_cleanup();

    printf("============================================================\n");
    printf(" ALL NATIVE C v3.1 TESTS PASSED SUCCESSFULLY (100%%)\n");
    printf("============================================================\n");
    return 0;
}



