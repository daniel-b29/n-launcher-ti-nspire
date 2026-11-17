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
    uint8_t rbuf[8192];
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
    /* Verify aspect-ratio preserving resize (zero cropping): 160x68 -> 128x54 with center artwork intact */
    assert(state.games[crafti_idx].icon_w == 128);
    assert(state.games[crafti_idx].icon_h == 54);
    assert(state.games[crafti_idx].icon_pixels[(54 / 2) * 128 + 64] != RGB565(255, 0, 255));
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
    assert(state.anim.target_scroll_y_fp == (NL_PAGE_STRIDE_Y << 4));
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
    int lum_diff_px = 0;
    for (int y = 27; y < 105; y++) {
        for (int x = 6; x < 108; x++) {
            if (fb[y * NL_SCREEN_W + x] != fb_no_hl[y * NL_SCREEN_W + x]) lum_diff_px++;
        }
    }
    assert(lum_diff_px > 40);

    nl_render_frame(&reloaded, fb);
    assert(export_fb_to_bmp("frame_preview.bmp", fb, NL_SCREEN_W, NL_SCREEN_H) == true);

    /* Verify bottom row peek (Row 2 starts at y=188 < NL_VIEW_BOTTOM=205) drew non-bg pixels cleanly
     * without overflowing past NL_VIEW_BOTTOM (y=205..209 must stay pure bg_main) */
    const nl_theme_t *th = nl_get_theme(reloaded.display_mode, reloaded.accent_color);
    int peek_pixels = 0;
    for (int y = 190; y < 204; y++) {
        if (fb[y * NL_SCREEN_W + 25] != th->bg_main) peek_pixels++;
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
            for (int slot = 1; slot < persisted.filtered_count && slot < 6; slot++) {
                int gi = persisted.filtered_indices[slot];
                if (persisted.games[gi].icon_source != NL_ICON_NONE) {
                    int px_x = NL_GRID_MARGIN_X + (slot % NL_GRID_COLS) * (NL_GRID_CARD_W + NL_GRID_GAP_X) + (NL_GRID_CARD_W / 2);
                    int px_y = NL_GRID_TOP_Y + (slot / NL_GRID_COLS) * NL_ROW_STRIDE_Y + (NL_GRID_CARD_H / 2);
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
    assert(cfg_test.settings.show_battery == false);
    assert(cfg_test.anim.switch_battery_fp == 0);
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
                    /* Bottom bar is completely hidden: bottom scanlines (y = 235..239) are pure bg_main, zero empty bar */
                    for (int y = 235; y < NL_SCREEN_H; y++) {
                        for (int x = 10; x < 310; x++) {
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
                    assert(fb[237 * NL_SCREEN_W + 10] == gth->bg_bottom_bar);
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
    nl_handle_key(&cfg_test, NL_KEY_DOWN);
    assert(cfg_test.sui.display_cursor == 1);
    nl_handle_key(&cfg_test, NL_KEY_DOWN);
    assert(cfg_test.sui.display_cursor == 2);
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

    /* 12c. Autostart permanently removed: nl_autostart_set always returns false and purges any legacy startup files */
    cfg_test.sys.ndless_persistent = false;
    char diag_buf[64];
    assert(nl_autostart_set(&cfg_test, true, diag_buf, sizeof(diag_buf)) == false);
    assert(cfg_test.settings.autostart == false);
    assert(cfg_test.sys.autostart_active == false);
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
    assert(nl_autostart_verify_active(cfg_test.sys.docs_root, &cfg_test.sys) == false);
    assert(nl_autostart_set(&cfg_test, true, diag_buf, sizeof(diag_buf)) == false);
    assert(cfg_test.settings.autostart == false);
    assert(cfg_test.sys.autostart_active == false);
    nl_autostart_reconcile(&cfg_test);
    assert(cfg_test.settings.autostart == false);
    assert(cfg_test.sys.autostart_active == false);
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
    assert(NL_SET_MENU_COUNT == 14);
    assert(NL_SET_MENU_DISPLAY == 0);
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
    for (int scr = NL_UI_SETTINGS_MAIN; scr <= NL_UI_SETTINGS_PROFILES; scr++) {
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
    assert(nl_autostart_set(&cfg_test, true, diag_buf, sizeof(diag_buf)) == false);
    assert(cfg_test.settings.autostart == false);
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
    assert(cfg_test.settings.autostart == false);
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

        /* A. Menu Open: progressive fade-in & vertical displacement (set_open_fp: 48 -> 256) */
        assert(nl_handle_key(&cfg_test, NL_KEY_SETTINGS) == true);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        assert(cfg_test.anim.set_open_fp == 48);
        nl_render_frame(&cfg_test, fb);
        int open_ticks = 0;
        while (cfg_test.anim.set_open_fp < 256 && open_ticks < 20) {
            assert(nl_anim_step(&cfg_test) == true);
            nl_render_frame(&cfg_test, fb);
            open_ticks++;
        }
        assert(cfg_test.anim.set_open_fp == 256 && open_ticks >= 4 && open_ticks <= 10);

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

        /* G. Menu Close: progressive fade-out & downward displacement (set_open_fp: 208 -> 0) over library */
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        assert(cfg_test.anim.set_open_fp == 256);
        assert(nl_handle_key(&cfg_test, NL_KEY_ESC) == true);
        assert(cfg_test.sui.screen == NL_UI_LIBRARY);
        assert(cfg_test.anim.set_open_fp == 208);
        nl_render_frame(&cfg_test, fb);
        int close_ticks = 0;
        while (cfg_test.anim.set_open_fp > 0 && close_ticks < 20) {
            assert(nl_anim_step(&cfg_test) == true);
            nl_render_frame(&cfg_test, fb);
            close_ticks++;
        }
        assert(cfg_test.anim.set_open_fp == 0 && close_ticks >= 4 && close_ticks <= 10);
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

        /* Open Settings -> Navigation au pave tactile -> Toggle ON */
        assert(nl_handle_key(&cfg_test, NL_KEY_SETTINGS) == true);
        cfg_test.sui.menu_cursor = NL_SET_MENU_TOUCHPAD;
        assert(nl_handle_key(&cfg_test, NL_KEY_EXE) == true);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_TOUCHPAD);
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

        /* Re-enter SETTINGS_TOUCHPAD, switch DOWN to row 1 (5-detent slider) */
        nl_handle_key(&cfg_test, NL_KEY_SETTINGS);
        cfg_test.sui.menu_cursor = NL_SET_MENU_TOUCHPAD;
        nl_handle_key(&cfg_test, NL_KEY_EXE);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_TOUCHPAD);
        assert(cfg_test.sui.tp_cursor == 0);
        nl_handle_key(&cfg_test, NL_KEY_DOWN);
        assert(cfg_test.sui.tp_cursor == 1);

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

        /* Factory settings reset returns touchpad_nav to OFF (false) and sensitivity to default 2 (3/5) */
        nl_reset_settings_only(&cfg_test, "/tmp/nl_test_v3.cfg.tns");
        assert(cfg_test.settings.touchpad_nav == false);
        assert(cfg_test.settings.touchpad_sensitivity == NL_TP_SENS_DEFAULT);
        assert(cfg_test.anim.switch_touchpad_fp == 0);
        assert(cfg_test.anim.tp_slider_pos_fp == NL_TP_SENS_DEFAULT * 256);
    }

    /* 12k. Optical TI-OS Battery Detection (Solution A), 3 Levels + Unavailable, 60-Min Periodic RTC & Settings -> Affichage */
    {
        uint16_t *tios_vram = (uint16_t *)calloc(NL_FB_PIXELS, sizeof(uint16_t));
        assert(tios_vram != NULL);

        /* 1. Blank/uniform/sentinel VRAM must return NL_BAT_UNAVAILABLE (never fake a level) */
        assert(nl_detect_battery_from_vram(tios_vram, true, NL_PIX_RGB565, NULL) == NL_BAT_UNAVAILABLE);
        assert(nl_detect_battery_from_vram(tios_vram, false, NL_PIX_RGB565, NULL) == NL_BAT_UNAVAILABLE);
        for (int i = 0; i < NL_FB_PIXELS; i++) tios_vram[i] = NL_VRAM_SENTINEL;
        assert(nl_detect_battery_from_vram(tios_vram, true, NL_PIX_RGB565, NULL) == NL_BAT_UNAVAILABLE);

        /* 2. Test all 3 battery levels + red-alert low in BOTH rotated (240x320 CX II-T) and direct (320x240) orientations */
        for (int rot = 0; rot < 2; rot++) {
            bool is_rot = (rot == 1);

            nl_stamp_tios_battery_icon(tios_vram, is_rot, NL_BAT_HIGH, false);
            assert(nl_detect_battery_from_vram(tios_vram, is_rot, NL_PIX_RGB565, NULL) == NL_BAT_HIGH);
            /* Orientation fallback also succeeds if caller passes opposite orientation flag */
            assert(nl_detect_battery_from_vram(tios_vram, !is_rot, NL_PIX_RGB565, NULL) == NL_BAT_HIGH);

            nl_stamp_tios_battery_icon(tios_vram, is_rot, NL_BAT_MEDIUM, false);
            assert(nl_detect_battery_from_vram(tios_vram, is_rot, NL_PIX_RGB565, NULL) == NL_BAT_MEDIUM);

            nl_stamp_tios_battery_icon(tios_vram, is_rot, NL_BAT_LOW, false);
            assert(nl_detect_battery_from_vram(tios_vram, is_rot, NL_PIX_RGB565, NULL) == NL_BAT_LOW);

            nl_stamp_tios_battery_icon(tios_vram, is_rot, NL_BAT_LOW, true);
            assert(nl_detect_battery_from_vram(tios_vram, is_rot, NL_PIX_RGB565, NULL) == NL_BAT_LOW);

            nl_stamp_tios_battery_icon(tios_vram, is_rot, NL_BAT_UNAVAILABLE, false);
            assert(nl_detect_battery_from_vram(tios_vram, is_rot, NL_PIX_RGB565, NULL) == NL_BAT_UNAVAILABLE);
        }
        free(tios_vram);

        /* 3. Labels for all 4 states */
        assert(strcmp(nl_battery_level_label(NL_BAT_HIGH), "Eleve") == 0);
        assert(strcmp(nl_battery_level_label(NL_BAT_MEDIUM), "Moyen") == 0);
        assert(strcmp(nl_battery_level_label(NL_BAT_LOW), "Faible") == 0);
        assert(strcmp(nl_battery_level_label(NL_BAT_UNAVAILABLE), "Indisponible") == 0);

        /* 4. Verify top bar never draws a battery indicator even if show_battery is toggled in memory */
        cfg_test.sui.screen = NL_UI_LIBRARY;
        cfg_test.settings.show_battery = false;
        for (int i = 0; i < 16; i++) nl_anim_step(&cfg_test);
        assert(cfg_test.anim.set_open_fp == 0);
        assert(cfg_test.anim.switch_battery_fp == 0);
        const nl_theme_t *th_cur = nl_get_theme(cfg_test.display_mode, cfg_test.accent_color);
        cfg_test.sys.battery_level = NL_BAT_HIGH;
        nl_render_frame(&cfg_test, fb);
        for (int y = NL_BAT_IND_Y; y < NL_BAT_IND_Y + NL_BAT_IND_H; y++) {
            for (int x = NL_BAT_IND_X; x < NL_BAT_IND_X + NL_BAT_IND_W; x++) {
                assert(fb[y * NL_SCREEN_W + x] == th_cur->bg_top_bar);
            }
        }

        /* 5. Settings -> Affichage has 4 options (0: Grille, 1: Titres, 2: Barre d'aide, 3: Eteindre la calculatrice) */
        assert(NL_DISPLAY_OPT_COUNT == 4);
        cfg_test.sys.has_ndless = false;
        nl_handle_key(&cfg_test, NL_KEY_SETTINGS);
        cfg_test.sui.menu_cursor = NL_SET_MENU_DISPLAY;
        nl_handle_key(&cfg_test, NL_KEY_EXE);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_DISPLAY);
        assert(cfg_test.sui.display_cursor == 0);
        nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 1: Afficher les noms */
        nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 2: Afficher les raccourcis */
        nl_handle_key(&cfg_test, NL_KEY_DOWN); /* 3: Eteindre la calculatrice */
        assert(cfg_test.sui.display_cursor == 3);
        assert(cfg_test.sui.display_scroll == 1);
        nl_render_frame(&cfg_test, fb);
        assert(cfg_test.sui.poweroff_req == false);
        nl_handle_key(&cfg_test, NL_KEY_EXE);
        assert(cfg_test.sui.poweroff_req == true);
        cfg_test.sui.poweroff_req = false;

        /* Save & reload: show_battery and autostart always stay false */
        assert(nl_save_config(&cfg_test, "/tmp/nl_test_v3.cfg.tns") == true);
        static nl_state_t bat_reloaded;
        nl_init_state(&bat_reloaded);
        assert(bat_reloaded.settings.show_battery == false);
        assert(bat_reloaded.settings.autostart == false);
        assert(nl_load_config(&bat_reloaded, "/tmp/nl_test_v3.cfg.tns") == true);
        assert(bat_reloaded.settings.show_battery == false);
        assert(bat_reloaded.settings.autostart == false);
        assert(bat_reloaded.anim.switch_battery_fp == 0);
        nl_free_icons(&bat_reloaded);

        /* Pressing DEL (NL_KEY_RESCAN) in Settings triggers poweroff_req */
        nl_handle_key(&cfg_test, NL_KEY_ESC);
        assert(cfg_test.sui.screen == NL_UI_SETTINGS_MAIN);
        nl_handle_key(&cfg_test, NL_KEY_RESCAN);
        assert(cfg_test.sui.poweroff_req == true);
        cfg_test.sui.poweroff_req = false;
        assert(strstr(cfg_test.anim.toast_text, "EXTINCTION CALCULATRICE") != NULL);

        /* Factory settings reset keeps show_battery and autostart OFF (false) */
        nl_reset_settings_only(&cfg_test, "/tmp/nl_test_v3.cfg.tns");
        assert(cfg_test.settings.show_battery == false);
        assert(cfg_test.settings.autostart == false);
        assert(cfg_test.anim.switch_battery_fp == 0);

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
    printf("[PASS] Settings menu, 5 layouts, show_names, autostart pre-flight, shortcuts, 7 sorts, stats, search, safe reset, fluid animations (A-G), touchpad navigation, optical battery indicator & visual icon hierarchy verified.\n");

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
        assert(save_st.anim.set_open_fp == 48);
        for (int i = 0; i < 16; i++) nl_anim_step(&save_st);
        assert(save_st.anim.set_open_fp == 256);
        assert(nl_handle_key(&save_st, NL_KEY_ESC) == true);
        assert(save_st.sui.screen == NL_UI_LIBRARY);
        for (int i = 0; i < 16; i++) nl_anim_step(&save_st);
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
            /* Scan column x = NL_GRID_MARGIN_X + (NL_GRID_CARD_W / 2) (center of col 0) for red (row 0) vs green (row 1) Y bounds */
            int x_probe = NL_GRID_MARGIN_X + (NL_GRID_CARD_W / 2);
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
            for (int i = 0; i < 16; i++) nl_anim_step(&up_st);
            assert(up_st.anim.set_open_fp == 256);

            nl_handle_key(&up_st, NL_KEY_ESC);
            assert(up_st.sui.screen == NL_UI_LIBRARY);
            assert(up_st.anim.set_open_fp > 0 && up_st.anim.set_open_fp < 256);
            nl_render_frame(&up_st, fb);
            for (int i = 0; i < 16; i++) nl_anim_step(&up_st);
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
        nl_get_layout_metrics_ex(launch_st.settings.layout, launch_st.settings.show_names, launch_st.settings.show_shortcuts, &lm);
        int sel_col = launch_st.selected_pos % lm.cols;
        int sel_row = launch_st.selected_pos / lm.cols;
        int sel_cx = lm.margin_x + sel_col * (lm.card_w + lm.gap_x) + lm.card_w / 2;
        int sel_cy = lm.top_y + sel_row * lm.row_stride_y + lm.card_h / 2;

        /* Step 1/46 (shrink start): custom icon is Pure Green across the selected card (> 3000 px) */
        nl_render_launch_zoom(&launch_st, fb, 1, 46);
        int green_cnt = 0;
        for (int p = 0; p < NL_SCREEN_W * NL_SCREEN_H; p++) {
            if (fb[p] == RGB565(20, 200, 40)) green_cnt++;
        }
        assert(green_cnt > 3000);

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
        assert(green_mid > 2400);

        /* Step 34/46 (Stage 4 crossfade + expansion, t_exp ~ 178): custom icon & screenshot blended */
        nl_render_launch_zoom(&launch_st, fb, 34, 46);
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
                    assert(m.card_w >= 32);
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
            assert(ico_st.games[0].icon_w == 128);
            assert(ico_st.games[0].icon_h == 72);
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

        /* Verify config with legacy autostart=1 and show_battery=1 forces both to false on load */
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
        assert(bg_st.settings.autostart == false);
        assert(bg_st.settings.show_battery == false);
        assert(bg_st.anim.switch_autostart_fp == 0);
        assert(bg_st.anim.switch_battery_fp == 0);

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
            first_boot.games[idx_ndoom].scr_pixels[p] = RGB565(220, 38, 38);
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

        /* Test 3: Settings « Actualiser la bibliotheque » (NL_SET_MENU_REFRESH_LIB) sets rescan_req = true */
        cached_boot.sui.screen = NL_UI_SETTINGS_MAIN;
        cached_boot.sui.menu_cursor = NL_SET_MENU_REFRESH_LIB;
        cached_boot.sui.rescan_req = false;
        assert(nl_handle_key(&cached_boot, NL_KEY_EXE) == true);
        assert(cached_boot.sui.rescan_req == true);
        assert(strstr(cached_boot.anim.toast_text, "BIBLIOTHEQUE ACTUALISEE") != NULL);

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
        nl_get_layout_metrics_ex(ls.settings.layout, ls.settings.show_names, ls.settings.show_shortcuts, &lm);
        int slot2_cx = lm.margin_x + 2 * (lm.card_w + lm.gap_x) + lm.card_w / 2;
        int slot2_cy = lm.top_y + lm.card_h / 2;
        int ray_x = slot2_cx;
        int ray_y0 = slot2_cy + (lm.card_h / 2) + 4;
        int ray_y1 = ray_y0 + 22;
        if (ray_y1 > NL_VIEW_BOTTOM - 2) ray_y1 = NL_VIEW_BOTTOM - 2;

        for (int acc = 0; acc < 4; acc++) {
            ls.display_mode = NL_DISPLAY_DARK;
            ls.accent_color = (nl_accent_color_t)acc;
            nl_render_launch_zoom(&ls, fb, 18, 46);
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

        /* Step shortcuts '0' / '-' (strictly NO wrap-around) */
        assert(nl_handle_char_input(ps, '-') == false); /* Already at last profile (Bob) */
        assert(ps->profiles.active_index == 2);
        assert(nl_handle_char_input(ps, '0') == true);  /* Switch left -> Alice */
        assert(ps->profiles.active_index == 1);
        assert(ps->games[1].is_favorite == true);
        assert(ps->games[1].shortcut_key == 'C');
        assert(ps->settings.layout == NL_LAYOUT_4X4);
        assert(nl_handle_char_input(ps, '0') == true);  /* Switch left -> Profil 1 */
        assert(ps->profiles.active_index == 0);
        assert(ps->games[0].is_favorite == true);
        assert(ps->games[0].launch_count == 7);
        assert(ps->games[0].shortcut_key == 'A');
        assert(ps->settings.layout == NL_LAYOUT_2X2);
        assert(nl_handle_char_input(ps, '0') == false); /* Strictly NO wrap at 0 */
        assert(ps->profiles.active_index == 0);

        /* During inline search, '0' and '-' type into search query instead of switching profiles */
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
        /* Ring pixels must be independent of accent_color */
        int ring_px_x = (ps->anim.qp_glow_x_fp >> 4) - 19;
        uint16_t ring_blue = fb[114 * NL_SCREEN_W + ring_px_x];
        ps->accent_color = NL_ACCENT_RED;
        nl_render_frame(ps, fb);
        uint16_t ring_red = fb[114 * NL_SCREEN_W + ring_px_x];
        assert(ring_blue == ring_red);

        ps->display_mode = NL_DISPLAY_LIGHT;
        nl_render_frame(ps, fb);
        uint16_t ring_light = fb[114 * NL_SCREEN_W + ring_px_x];
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
        ps->sui.prof_cursor = 1;
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
        for (int f = 0; f < 10; f++) {
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

        /* 26b. Press EXE on '+' button: verify direct CREATE_AVATAR mode (NO name prompt) & '+' ghost zoom animation */
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->sui.screen == NL_UI_SETTINGS_PROFILES);
        assert(pa->sui.prof_mode == NL_PROF_MODE_CREATE_AVATAR);
        assert(pa->sui.prof_from_quick == true);
        assert(pa->anim.prof_plus_active == true);
        assert(pa->anim.prof_plus_tick == 0);
        assert(pa->anim.prof_plus_origin_x > 160);
        assert(pa->anim.prof_plus_origin_y == 114);

        /* Step through '+' creation transition frames and render each stage (water-drop + 4 ghost crosses + rebound) */
        for (int f = 0; f < pa->anim.prof_plus_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_plus_active == false);
        nl_render_frame(pa, fb);

        /* 26c. Validate an avatar with EXE: creates "Profil 3" directly, triggers birth confirmation, returns to Quick Selector */
        assert(nl_handle_key(pa, NL_KEY_RIGHT) == true);
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->profiles.count == 3);
        assert(strcmp(pa->profiles.items[2].name, "Profil 3") == 0);
        assert(pa->sui.screen == NL_UI_LIBRARY);
        assert(pa->sui.qp_open == true);
        assert(pa->sui.qp_cursor == 2);
        assert(pa->anim.prof_birth_active == true);
        for (int f = 0; f < pa->anim.prof_birth_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_birth_active == false);

        /* 26d. Select Maxime (slot 1) with LEFT + EXE: triggers 4-stage Profile Switch animation */
        assert(nl_handle_key(pa, NL_KEY_LEFT) == true);
        assert(pa->sui.qp_cursor == 1);
        assert(nl_handle_key(pa, NL_KEY_EXE) == true);
        assert(pa->sui.qp_open == false);
        assert(pa->profiles.active_index == 1);
        assert(pa->anim.prof_switch_active == true);
        assert(pa->anim.prof_switch_avatar_id == pa->profiles.items[1].avatar_id);
        assert(pa->anim.prof_switch_origin_y == 114);

        /* Render every frame of the 4-stage switch (water-drop -> 4 ghosts zoom -> spring rebound -> fade-out) */
        for (int f = 0; f < pa->anim.prof_switch_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_switch_active == false);

        /* 26e. Rapid successive profile switches ('-' then '0' mid-animation) stay coherent without artifacts */
        assert(nl_handle_char_input(pa, '-') == true);
        assert(pa->profiles.active_index == 2);
        assert(pa->anim.prof_switch_active == true);
        nl_anim_step(pa);
        nl_render_frame(pa, fb);
        /* Immediately switch again before previous switch animation finishes */
        assert(nl_handle_char_input(pa, '0') == true);
        assert(pa->profiles.active_index == 1);
        assert(pa->anim.prof_switch_active == true);
        assert(pa->anim.prof_switch_tick == 0);
        assert(pa->anim.prof_switch_avatar_id == pa->profiles.items[1].avatar_id);
        for (int f = 0; f < pa->anim.prof_switch_max; f++) {
            nl_render_frame(pa, fb);
            nl_anim_step(pa);
        }
        assert(pa->anim.prof_switch_active == false);

        free(pa);
        printf("[PASS] 4-stage Profile Switch animation, '+' Ghost Creation, Direct Avatar Selection & Birth Confirmation verified.\n");
    }

    /* 27. Customizable Touchpad Graphical Cursor (Build 2026-11-17 #R7 — All 12 Mandatory Tests) */
    {
        nl_state_t *cs = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(cs != NULL);
        nl_init_state(cs);
        nl_scan_directory(cs, "/tmp/nlauncher_test_fs", "/tmp/nlauncher_test_fs/ndless/N-Launcher.tns", 4);
        nl_rebuild_filter(cs);
        assert(cs->filtered_count == 8);

        /* 1. Classic navigation without cursor (default: touchpad_cursor_show == false) */
        assert(cs->settings.touchpad_cursor_show == false);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_ARROW);
        assert(nl_cursor_is_visible(cs) == false);
        assert(cs->selected_pos == 0);
        assert(nl_handle_key(cs, NL_KEY_RIGHT) == true);
        assert(cs->selected_pos == 1);
        assert(cs->launch_requested == false);

        /* 2. Open Settings -> Navigation tactile (Tab 0) & enable "Afficher le curseur : OUI" (Row 2) */
        nl_handle_key(cs, NL_KEY_SETTINGS);
        cs->sui.menu_cursor = NL_SET_MENU_TOUCHPAD;
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->sui.screen == NL_UI_SETTINGS_TOUCHPAD);
        assert(cs->sui.tp_tab == 0);
        assert(cs->sui.tp_cursor == 0);

        /* Row 0 -> Row 1 -> Row 2 (Afficher le curseur : OUI / NON) */
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 1);
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 2);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_show == true);
        assert(nl_cursor_is_visible(cs) == true);

        /* 3. Disable cursor ("Afficher le curseur : NON") and verify touchpad navigation remains independent */
        cs->settings.touchpad_nav = true;
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_show == false);
        assert(cs->settings.touchpad_nav == true); /* Touchpad navigation NOT disabled by hiding cursor */
        assert(nl_cursor_is_visible(cs) == false);
        /* Re-enable cursor visibility */
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_show == true);
        assert(nl_cursor_is_visible(cs) == true);

        /* 4, 5 & 6. Open "Personnaliser le curseur" tab (Row 3 -> EXE or TAB) and cycle Aucun / Fleche / Cercle */
        nl_handle_key(cs, NL_KEY_DOWN);
        assert(cs->sui.tp_cursor == 3);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->sui.tp_tab == 1);
        assert(cs->sui.tp_style_cursor == (int)NL_CURSOR_STYLE_ARROW);

        /* Switch to Option 0: Aucun curseur */
        nl_handle_key(cs, NL_KEY_LEFT);
        assert(cs->sui.tp_style_cursor == (int)NL_CURSOR_STYLE_NONE);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_NONE);
        assert(cs->settings.touchpad_cursor_show == true); /* Independent toggle stays OUI */
        assert(nl_cursor_is_visible(cs) == false);         /* Style 'Aucun' hides graphical sprite */
        nl_render_frame(cs, fb);

        /* Switch to Option 2: Cercle (icons8-select-48.png) */
        nl_handle_key(cs, NL_KEY_RIGHT);
        nl_handle_key(cs, NL_KEY_RIGHT);
        assert(cs->sui.tp_style_cursor == (int)NL_CURSOR_STYLE_CIRCLE);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_CIRCLE);
        assert(nl_cursor_is_visible(cs) == true);
        nl_render_frame(cs, fb);

        /* Switch back to Option 1: Fleche (icons8-select-cursor-24.png) */
        nl_handle_key(cs, NL_KEY_LEFT);
        assert(cs->sui.tp_style_cursor == (int)NL_CURSOR_STYLE_ARROW);
        nl_handle_key(cs, NL_KEY_EXE);
        assert(cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_ARROW);
        assert(nl_cursor_is_visible(cs) == true);
        nl_render_frame(cs, fb);

        /* Return to Tab 0 via TAB, then exit Settings to Library */
        nl_handle_key(cs, NL_KEY_TAB);
        assert(cs->sui.tp_tab == 0);
        nl_handle_key(cs, NL_KEY_ESC);
        nl_handle_key(cs, NL_KEY_ESC);
        assert(cs->sui.screen == NL_UI_LIBRARY);

        /* 7. Render both Arrow & Circle across all 4 accent colors and all 3 display modes (Nuit, Jour, Gris) */
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
            }
        }
        cs->display_mode = NL_DISPLAY_DARK;
        cs->accent_color = NL_ACCENT_BLUE;
        cs->settings.touchpad_cursor_style = NL_CURSOR_STYLE_ARROW;

        /* 8 & 11. Move in all 4 directions (LEFT, RIGHT, UP, DOWN), verify trail + inactivity attenuation + ZERO auto-launch */
        cs->selected_pos = 0;
        nl_cursor_wake(cs);
        for (int i = 0; i < 16; i++) nl_anim_step(cs);
        int x0 = cs->anim.cur_x_fp;
        int y0 = cs->anim.cur_y_fp;

        assert(nl_handle_key(cs, NL_KEY_RIGHT) == true);
        assert(cs->launch_requested == false);
        nl_anim_step(cs);
        assert(cs->anim.cur_trail_alpha[0] > 0); /* Luminous trail spawned during movement */
        nl_render_frame(cs, fb);
        for (int i = 0; i < 20; i++) nl_anim_step(cs);
        assert(cs->anim.cur_x_fp > x0);

        assert(nl_handle_key(cs, NL_KEY_DOWN) == true);
        assert(cs->launch_requested == false);
        for (int i = 0; i < 20; i++) nl_anim_step(cs);
        assert(cs->anim.cur_y_fp > y0);

        assert(nl_handle_key(cs, NL_KEY_LEFT) == true);
        assert(cs->launch_requested == false);
        for (int i = 0; i < 20; i++) nl_anim_step(cs);

        assert(nl_handle_key(cs, NL_KEY_UP) == true);
        assert(cs->launch_requested == false);
        for (int i = 0; i < 20; i++) nl_anim_step(cs);
        assert(abs(cs->anim.cur_x_fp - x0) <= 16);
        assert(abs(cs->anim.cur_y_fp - y0) <= 16);

        /* Verify smooth inactivity attenuation after >= 75 idle ticks (256 -> 112) and wake back to 256 */
        for (int i = 0; i < 110; i++) nl_anim_step(cs);
        assert(cs->anim.cur_idle_ticks >= 75);
        assert(cs->anim.cur_opacity_fp == 112);
        nl_touchpad_tracker_t tp_nudge;
        memset(&tp_nudge, 0, sizeof(tp_nudge));
        tp_nudge.in_contact = true;
        tp_nudge.acc_x = 120;
        tp_nudge.acc_y = -80;
        nl_cursor_apply_touchpad_nudge(cs, &tp_nudge, true);
        nl_anim_step(cs);
        assert(cs->anim.cur_idle_ticks == 0);
        for (int i = 0; i < 12; i++) nl_anim_step(cs);
        assert(cs->anim.cur_opacity_fp == 256);

        /* 9 & 10. Synchronize across all 5 grid layouts (1x1, 2x2, 3x2, 3x3, 4x4), page changes & incomplete last rows */
        for (int lay = 0; lay < NL_LAYOUT_COUNT; lay++) {
            cs->settings.layout = (nl_layout_t)lay;
            nl_rebuild_filter(cs);
            for (int pos = 0; pos < cs->filtered_count; pos++) {
                cs->selected_pos = pos;
                int tx = 0, ty = 0;
                nl_cursor_compute_target(cs, &tx, &ty);
                assert(tx >= 6 && tx <= NL_SCREEN_W - 6);
                assert(ty >= 4 && ty <= NL_SCREEN_H - 6);
            }
        }
        cs->settings.layout = NL_LAYOUT_3X2;
        nl_rebuild_filter(cs);

        /* 12. Persistence of touchpad_cursor_show & touchpad_cursor_style across save/load */
        cs->settings.touchpad_cursor_show = true;
        cs->settings.touchpad_cursor_style = NL_CURSOR_STYLE_CIRCLE;
        assert(nl_save_config(cs, "/tmp/nl_test_cursor.cfg.tns") == true);

        nl_state_t *reloaded_cs = (nl_state_t *)calloc(1, sizeof(nl_state_t));
        assert(reloaded_cs != NULL);
        nl_init_state(reloaded_cs);
        assert(nl_load_config(reloaded_cs, "/tmp/nl_test_cursor.cfg.tns") == true);
        assert(reloaded_cs->settings.touchpad_cursor_show == true);
        assert(reloaded_cs->settings.touchpad_cursor_style == NL_CURSOR_STYLE_CIRCLE);
        assert(reloaded_cs->anim.switch_tp_cursor_fp == 256);

        free(reloaded_cs);
        free(cs);
        printf("[PASS] Customizable Touchpad Graphical Cursor (Arrow/Circle/None, Trail, Hotspots, Themes & Persistence) verified.\n");
    }

    printf("============================================================\n");
    printf(" ALL NATIVE C v3.1 TESTS PASSED SUCCESSFULLY (100%%)\n");
    printf("============================================================\n");
    return 0;
}



