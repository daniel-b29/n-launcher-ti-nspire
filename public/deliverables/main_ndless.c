/*
 * N-Launcher — Native TI-Nspire CX II-T (Ndless) Hardware Entry Point
 * Architecture: ARMv5TE / ARM926EJ-S (Zehn v1 Relocatable Executable)
 * Display API:  libndls lcd_init(SCR_320x240_565) + lcd_blit() (60 FPS)
 * Icon Capture: In-execution capture during child gameplay via e_nl_lcd_blit
 *               extension table hook (never at game exit, never overwrites!)
 * Anti-Freeze:  SP804 Timer 2 polling + full ESC/ON/Touchpad release barrier
 ****************************************************************************/

#include <os.h>
#include <libndls.h>
#include <keys.h>
#include <syscall-list.h>
#include <sys/stat.h>
#include "nlauncher.h"
#include "boot_guard_core.h"

static nl_state_t *g_state = NULL;
static uint16_t   *g_framebuffer = NULL;
static uint16_t   *g_ink_old_fb = NULL;
static uint16_t   *g_ink_new_fb = NULL;
static uint32_t    g_ink_rendered_seq = 0xFFFFFFFFU;
static uint32_t    g_monotonic_ticks = 0U;

typedef void (*nl_lcd_blit_fn_t)(void *buffer);
typedef nl_lcd_blit_fn_t (*nl_sc_lcd_blit_fn_t)(scr_type_t buffer_type);
typedef BOOL (*nl_sc_lcd_init_fn_t)(scr_type_t type);

typedef void  *(*nl_os_fopen_fn_t)(const char *path, const char *mode);
typedef size_t (*nl_os_fread_fn_t)(void *ptr, size_t size, size_t nmemb, void *stream);
typedef int    (*nl_os_fclose_fn_t)(void *stream);
typedef void   (*nl_os_free_fn_t)(void *ptr);
typedef int    (*nl_os_tp_read_fn_t)(unsigned char p1, unsigned char p2, void *p3);
typedef void   (*nl_os_dialog2_fn_t)(int p1, const char *p2, const char *p3, const char **p4);

static uint32_t            *g_sc_ext_table = NULL;
static uint32_t            *g_os_sc_table = NULL;

static nl_sc_lcd_blit_fn_t  g_orig_sc_lcd_blit = NULL;
static nl_sc_lcd_init_fn_t  g_orig_sc_lcd_init = NULL;
static nl_lcd_blit_fn_t     g_orig_blit_fn[SCR_TYPE_COUNT];

static nl_os_fopen_fn_t     g_orig_os_fopen = NULL;
static nl_os_fread_fn_t     g_orig_os_fread = NULL;
static nl_os_fclose_fn_t    g_orig_os_fclose = NULL;
static nl_os_free_fn_t      g_orig_os_free = NULL;
static nl_os_tp_read_fn_t   g_orig_os_tp_read = NULL;
static nl_os_dialog2_fn_t   g_orig_os_dialog2 = NULL;

static volatile bool        g_cap_armed = false;
static volatile bool        g_cap_done = false;
static volatile bool        g_cap_staged = false;
static volatile bool        g_cap_blit_seen = false;
static volatile bool        g_cap_in_dialog = false;
static volatile bool        g_cap_in_hook = false;
static nl_game_t           *g_cap_target = NULL;
static uint32_t             g_cap_start_rtc = 0;
static uint32_t             g_cap_io_calls = 0;
static uint32_t             g_cap_tp_calls = 0;
static nl_cap_diag_t        g_cap_diag_local;

/* GCC 12+ arm-none-eabi newlib __libc_fini_array compatibility stub */
void __attribute__((weak)) _fini(void) {}

static uint32_t decode_arm_imm12(uint32_t instr) {
    uint32_t rot = ((instr >> 8) & 0xFU) * 2U;
    uint32_t imm = instr & 0xFFU;
    if (rot == 0U) return imm;
    return (imm >> rot) | (imm << (32U - rot));
}

static uintptr_t decode_pc_rel_pool(const uint32_t *instr_ptr, uint32_t instr) {
    uint32_t pc_val = (uint32_t)(uintptr_t)instr_ptr + 8U;
    uint32_t off = instr & 0xFFFU;
    return (instr & (1U << 23)) ? (uintptr_t)(pc_val + off) : (uintptr_t)(pc_val - off);
}

/* Locate & verify BOTH sc_ext_table and sc_addrs_ptr (OS syscalls) via N-ext SWI handler */
static void nl_locate_syscall_tables(void) {
    if (g_sc_ext_table && g_os_sc_table) return;

    volatile uintptr_t vec_addr = 0x28U;
    const uint32_t *swi_handler = *(const uint32_t * const *)vec_addr;
    if (!swi_handler || (((uintptr_t)swi_handler) & 3U) != 0U) return;
    /* N-ext convention: "NEXT" magic sits 8 bytes before SWI handler */
    if (*(swi_handler - 2) != 0x4E455854U) return;

    uint32_t *ext_found = NULL;
    uint32_t *os_found = NULL;

    for (unsigned i = 0; i < 63U; i++) {
        uint32_t w0 = swi_handler[i];
        uint32_t w1 = swi_handler[i + 1U];

        /* Match: cmp r1, #0x200000 (__SYSCALLS_ISEXT) + ldreq r2, [pc, #+-imm] */
        if (!ext_found &&
            (w0 & 0xFFFFF000U) == 0xE3510000U &&
            decode_arm_imm12(w0) == 0x200000U &&
            (w1 & 0xFF7FF000U) == 0x051F2000U) {
            uintptr_t pool_addr = decode_pc_rel_pool(&swi_handler[i + 1U], w1);
            ext_found = *(uint32_t * const *)pool_addr;
        }

        /* Match: cmp r1, #0x400000 (__SYSCALLS_ISEMU) + ldreq r2, =emu + ldrne r2, sc_addrs_ptr */
        if (!os_found && i < 62U &&
            (w0 & 0xFFFFF000U) == 0xE3510000U &&
            decode_arm_imm12(w0) == 0x400000U) {
            uint32_t w2 = swi_handler[i + 2U];
            if ((w2 & 0xFF7FF000U) == 0x151F2000U) {
                uintptr_t pool_addr = decode_pc_rel_pool(&swi_handler[i + 2U], w2);
                os_found = *(uint32_t * const *)pool_addr;
            }
        }
    }

    if (!g_sc_ext_table && ext_found && (((uintptr_t)ext_found) & 3U) == 0U) {
        typedef unsigned (*rev_fn_t)(void);
        rev_fn_t rev_fn = (rev_fn_t)(uintptr_t)ext_found[6];
        if (rev_fn && rev_fn() == nl_ndless_rev() && ext_found[13] != 0U) {
            g_sc_ext_table = ext_found;
        }
    }

    if (!g_os_sc_table && os_found && (((uintptr_t)os_found) & 3U) == 0U && os_found != ext_found) {
        if (os_found[e_fopen] >= 0x10000000U &&
            os_found[e_free] >= 0x10000000U &&
            os_found[e_show_dialog_box2_] >= 0x10000000U) {
            g_os_sc_table = os_found;
        }
    }
}

/* Safe Ndless syscall check for ESC / ON (zero raw MMIO dereference) */
static bool nl_raw_esc_or_on_pressed(void) {
    return on_key_pressed() || isKeyPressed(KEY_NSPIRE_ESC);
}

/* Validate physical/MMU-mapped VRAM pointer before memcpy/memset */
static inline bool nl_is_valid_hw_vram_ptr(const void *ptr) {
    uintptr_t a = (uintptr_t)ptr;
    return ((a >= 0x10000000U && a < 0x18000000U) ||
            (a >= 0xA4000000U && a < 0xA4200000U));
}

static void nl_process_candidate_frame(
    const void *buf,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_pal,
    bool from_blit
) {
    if (!g_cap_armed || g_cap_done || !g_cap_target || !buf || g_cap_in_dialog) return;

    if (from_blit) {
        g_cap_blit_seen = true;
        g_cap_diag_local.blit_frames_seen++;
    } else {
        g_cap_diag_local.vram_probes_seen++;
    }
    g_cap_diag_local.last_scr_type = (int)fmt | (rotated_240x320 ? 16 : 0);

    if (nl_raw_esc_or_on_pressed()) {
        g_cap_diag_local.frames_rejected_esc++;
        return;
    }

    int score = nl_evaluate_frame_score(buf, rotated_240x320, fmt, hw_pal);
    if (score < 0) {
        g_cap_diag_local.frames_rejected_blank++;
        return;
    }

    uint32_t total_seen = g_cap_diag_local.blit_frames_seen + g_cap_diag_local.vram_probes_seen;
    bool steady_state = (from_blit && g_cap_diag_local.blit_frames_seen >= 36U) ||
                        (!from_blit && total_seen >= 12U);

    if (!steady_state) {
        if (!g_cap_staged || score > g_cap_diag_local.best_scene_score) {
            int out_sc = -1;
            if (nl_capture_frame_to_screenshot_ex(g_cap_target, buf, rotated_240x320, fmt, hw_pal, true, &out_sc)) {
                g_cap_staged = true;
                g_cap_diag_local.frames_staged++;
                if (out_sc > g_cap_diag_local.best_scene_score) {
                    g_cap_diag_local.best_scene_score = out_sc;
                }
            }
        }
        return;
    }

    int final_sc = -1;
    if (nl_capture_frame_to_screenshot_ex(g_cap_target, buf, rotated_240x320, fmt, hw_pal, false, &final_sc)) {
        if (final_sc > g_cap_diag_local.best_scene_score) {
            g_cap_diag_local.best_scene_score = final_sc;
        }
        g_cap_done = true;
        g_cap_armed = false;
        g_cap_diag_local.status = from_blit ? NL_CAP_CAPTURED_BLIT : NL_CAP_CAPTURED_VRAM;
        g_cap_target = NULL;
    }
}

static void nl_trampoline_try_capture(const void *buf, bool rotated_240x320, nl_pixel_format_t fmt) {
    if (!g_cap_armed || g_cap_done || !g_cap_target || !buf) return;
    if (g_cap_in_hook) return;
    g_cap_in_hook = true;
    nl_process_candidate_frame(buf, rotated_240x320, fmt, NULL, true);
    g_cap_in_hook = false;
}

static void nl_probe_live_vram(bool force_final_attempt) {
    if (!g_cap_armed || g_cap_done || !g_cap_target || g_cap_in_dialog || g_cap_in_hook) return;
    if (!force_final_attempt && g_cap_blit_seen) return;
    if (!has_colors) return;

    g_cap_in_hook = true;
    const void *vram = (const void *)REAL_SCREEN_BASE_ADDRESS;
    if (nl_is_valid_hw_vram_ptr(vram)) {
        bool hw_is_cx2 = (nl_hwsubtype() == 2);
        nl_process_candidate_frame(vram, hw_is_cx2, NL_PIX_RGB565, NULL, false);
    }
    g_cap_in_hook = false;
}

/* Channel A: All 7 scr_type_t lcd_blit wrappers */
static void nl_blit_wrap_320_565(void *buffer) {
    if (g_orig_blit_fn[SCR_320x240_565]) g_orig_blit_fn[SCR_320x240_565](buffer);
    nl_trampoline_try_capture(buffer, false, NL_PIX_RGB565);
}

static void nl_blit_wrap_320_16(void *buffer) {
    if (g_orig_blit_fn[SCR_320x240_16]) g_orig_blit_fn[SCR_320x240_16](buffer);
    nl_trampoline_try_capture(buffer, false, NL_PIX_RGB565);
}

static void nl_blit_wrap_320_555(void *buffer) {
    if (g_orig_blit_fn[SCR_320x240_555]) g_orig_blit_fn[SCR_320x240_555](buffer);
    nl_trampoline_try_capture(buffer, false, NL_PIX_RGB555);
}

static void nl_blit_wrap_240_565(void *buffer) {
    if (g_orig_blit_fn[SCR_240x320_565]) g_orig_blit_fn[SCR_240x320_565](buffer);
    nl_trampoline_try_capture(buffer, true, NL_PIX_RGB565);
}

static void nl_blit_wrap_240_555(void *buffer) {
    if (g_orig_blit_fn[SCR_240x320_555]) g_orig_blit_fn[SCR_240x320_555](buffer);
    nl_trampoline_try_capture(buffer, true, NL_PIX_RGB555);
}

static void nl_blit_wrap_320_8(void *buffer) {
    if (g_orig_blit_fn[SCR_320x240_8]) g_orig_blit_fn[SCR_320x240_8](buffer);
    nl_trampoline_try_capture(buffer, false, NL_PIX_8BPP);
}

static void nl_blit_wrap_320_4(void *buffer) {
    if (g_orig_blit_fn[SCR_320x240_4]) g_orig_blit_fn[SCR_320x240_4](buffer);
    nl_trampoline_try_capture(buffer, false, NL_PIX_4BPP);
}

static nl_lcd_blit_fn_t nl_hooked_sc_lcd_blit(scr_type_t buffer_type) {
    if (!g_orig_sc_lcd_blit) return NULL;
    nl_lcd_blit_fn_t real_fn = g_orig_sc_lcd_blit(buffer_type);
    if (!real_fn) return NULL;
    if ((unsigned)buffer_type < (unsigned)SCR_TYPE_COUNT) {
        g_orig_blit_fn[buffer_type] = real_fn;
    }
    switch (buffer_type) {
        case SCR_320x240_565: return nl_blit_wrap_320_565;
        case SCR_320x240_16:  return nl_blit_wrap_320_16;
        case SCR_320x240_555: return nl_blit_wrap_320_555;
        case SCR_240x320_565: return nl_blit_wrap_240_565;
        case SCR_240x320_555: return nl_blit_wrap_240_555;
        case SCR_320x240_8:   return nl_blit_wrap_320_8;
        case SCR_320x240_4:   return nl_blit_wrap_320_4;
        default:              return real_fn;
    }
}

static BOOL nl_hooked_sc_lcd_init(scr_type_t type) {
    if (type == SCR_TYPE_INVALID && g_cap_armed && !g_cap_done) {
        nl_probe_live_vram(true);
    }
    if (g_orig_sc_lcd_init) {
        return g_orig_sc_lcd_init(type);
    }
    return FALSE;
}

/* Channel B: Verified OS Syscall Hooks (for classic VRAM / !lcd_blit games & dialog exclusion) */
static void *nl_os_fopen_wrap(const char *path, const char *mode) {
    if (g_cap_armed && !g_cap_done && !g_cap_blit_seen) {
        if ((g_cap_io_calls++ & 15U) == 0U) {
            nl_probe_live_vram(false);
        }
    }
    return g_orig_os_fopen ? g_orig_os_fopen(path, mode) : NULL;
}

static size_t nl_os_fread_wrap(void *ptr, size_t size, size_t nmemb, void *stream) {
    if (g_cap_armed && !g_cap_done && !g_cap_blit_seen) {
        if ((g_cap_io_calls++ & 63U) == 31U) {
            nl_probe_live_vram(false);
        }
    }
    return g_orig_os_fread ? g_orig_os_fread(ptr, size, nmemb, stream) : 0U;
}

static int nl_os_fclose_wrap(void *stream) {
    if (g_cap_armed && !g_cap_done && !g_cap_blit_seen) {
        if ((g_cap_io_calls++ & 15U) == 0U) {
            nl_probe_live_vram(false);
        }
    }
    return g_orig_os_fclose ? g_orig_os_fclose(stream) : -1;
}

static void nl_os_free_wrap(void *ptr) {
    if (g_cap_armed && !g_cap_done && !g_cap_blit_seen) {
        if ((g_cap_io_calls++ & 63U) == 15U) {
            nl_probe_live_vram(false);
        }
    }
    if (g_orig_os_free) {
        g_orig_os_free(ptr);
    }
}

static int nl_os_tp_read_wrap(unsigned char p1, unsigned char p2, void *p3) {
    if (g_cap_armed && !g_cap_done && !g_cap_blit_seen) {
        if ((g_cap_tp_calls++ & 7U) == 0U) {
            nl_probe_live_vram(false);
        }
    }
    return g_orig_os_tp_read ? g_orig_os_tp_read(p1, p2, p3) : -1;
}

static void nl_os_dialog2_wrap(int p1, const char *p2, const char *p3, const char **p4) {
    g_cap_in_dialog = true;
    if (g_orig_os_dialog2) {
        g_orig_os_dialog2(p1, p2, p3, p4);
    }
    g_cap_in_dialog = false;
}

static void nl_install_capture_hooks(void) {
    nl_locate_syscall_tables();

    if (g_sc_ext_table && g_orig_sc_lcd_blit == NULL) {
        memset(g_orig_blit_fn, 0, sizeof(g_orig_blit_fn));
        g_orig_sc_lcd_blit = (nl_sc_lcd_blit_fn_t)(uintptr_t)g_sc_ext_table[13];
        g_sc_ext_table[13] = (uint32_t)(uintptr_t)&nl_hooked_sc_lcd_blit;
        g_cap_diag_local.hook_blit_installed = true;

        if (g_sc_ext_table[15] != 0U && g_orig_sc_lcd_init == NULL) {
            g_orig_sc_lcd_init = (nl_sc_lcd_init_fn_t)(uintptr_t)g_sc_ext_table[15];
            g_sc_ext_table[15] = (uint32_t)(uintptr_t)&nl_hooked_sc_lcd_init;
        }
    }

    if (g_os_sc_table && g_orig_os_fopen == NULL) {
        g_orig_os_fopen   = (nl_os_fopen_fn_t)(uintptr_t)g_os_sc_table[e_fopen];
        g_orig_os_fread   = (nl_os_fread_fn_t)(uintptr_t)g_os_sc_table[e_fread];
        g_orig_os_fclose  = (nl_os_fclose_fn_t)(uintptr_t)g_os_sc_table[e_fclose];
        g_orig_os_free    = (nl_os_free_fn_t)(uintptr_t)g_os_sc_table[e_free];
        g_orig_os_dialog2 = (nl_os_dialog2_fn_t)(uintptr_t)g_os_sc_table[e_show_dialog_box2_];

        if (g_orig_os_fopen)   g_os_sc_table[e_fopen] = (uint32_t)(uintptr_t)&nl_os_fopen_wrap;
        if (g_orig_os_fread)   g_os_sc_table[e_fread] = (uint32_t)(uintptr_t)&nl_os_fread_wrap;
        if (g_orig_os_fclose)  g_os_sc_table[e_fclose] = (uint32_t)(uintptr_t)&nl_os_fclose_wrap;
        if (g_orig_os_free)    g_os_sc_table[e_free] = (uint32_t)(uintptr_t)&nl_os_free_wrap;
        if (g_orig_os_dialog2) g_os_sc_table[e_show_dialog_box2_] = (uint32_t)(uintptr_t)&nl_os_dialog2_wrap;

        if (g_os_sc_table[e_touchpad_read] != 0U) {
            g_orig_os_tp_read = (nl_os_tp_read_fn_t)(uintptr_t)g_os_sc_table[e_touchpad_read];
            g_os_sc_table[e_touchpad_read] = (uint32_t)(uintptr_t)&nl_os_tp_read_wrap;
        }
        g_cap_diag_local.hook_os_installed = true;
    }
}

static void nl_uninstall_capture_hooks(void) {
    if (g_sc_ext_table) {
        if (g_orig_sc_lcd_blit != NULL) {
            g_sc_ext_table[13] = (uint32_t)(uintptr_t)g_orig_sc_lcd_blit;
        }
        if (g_orig_sc_lcd_init != NULL) {
            g_sc_ext_table[15] = (uint32_t)(uintptr_t)g_orig_sc_lcd_init;
        }
    }
    g_orig_sc_lcd_blit = NULL;
    g_orig_sc_lcd_init = NULL;

    if (g_os_sc_table) {
        if (g_orig_os_fopen)   g_os_sc_table[e_fopen] = (uint32_t)(uintptr_t)g_orig_os_fopen;
        if (g_orig_os_fread)   g_os_sc_table[e_fread] = (uint32_t)(uintptr_t)g_orig_os_fread;
        if (g_orig_os_fclose)  g_os_sc_table[e_fclose] = (uint32_t)(uintptr_t)g_orig_os_fclose;
        if (g_orig_os_free)    g_os_sc_table[e_free] = (uint32_t)(uintptr_t)g_orig_os_free;
        if (g_orig_os_tp_read) g_os_sc_table[e_touchpad_read] = (uint32_t)(uintptr_t)g_orig_os_tp_read;
        if (g_orig_os_dialog2) g_os_sc_table[e_show_dialog_box2_] = (uint32_t)(uintptr_t)g_orig_os_dialog2;
    }
    g_orig_os_fopen = NULL;
    g_orig_os_fread = NULL;
    g_orig_os_fclose = NULL;
    g_orig_os_free = NULL;
    g_orig_os_tp_read = NULL;
    g_orig_os_dialog2 = NULL;
}

static void nl_write_internal_cap_log(const nl_cap_diag_t *diag, const char *covers_dir) {
    if (!diag || !covers_dir || covers_dir[0] == '\0') return;
    char dir_tmp[NL_MAX_PATH_LEN];
    snprintf(dir_tmp, sizeof(dir_tmp), "%s", covers_dir);
    for (char *p = dir_tmp + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char s = *p;
            *p = '\0';
            (void)mkdir(dir_tmp, 0777);
            *p = s;
        }
    }
    (void)mkdir(dir_tmp, 0777);

    char log_path[NL_MAX_PATH_LEN];
    snprintf(log_path, sizeof(log_path), "%.*s/capture_diag.log.tns",
             (int)(sizeof(log_path) - 24U), covers_dir);

    FILE *check_fp = fopen(log_path, "rb");
    long cur_sz = 0;
    if (check_fp) {
        fseek(check_fp, 0, SEEK_END);
        cur_sz = ftell(check_fp);
        fclose(check_fp);
    }

    FILE *fp = fopen(log_path, (cur_sz > 8192L) ? "wb" : "ab");
    if (!fp) return;
    fprintf(
        fp,
        "game=%s st=%d blit=%lu vram=%lu rej_b=%lu rej_e=%lu staged=%lu score=%d scr=%d hb=%d ho=%d save=%d\n",
        diag->game_filename,
        (int)diag->status,
        (unsigned long)diag->blit_frames_seen,
        (unsigned long)diag->vram_probes_seen,
        (unsigned long)diag->frames_rejected_blank,
        (unsigned long)diag->frames_rejected_esc,
        (unsigned long)diag->frames_staged,
        diag->best_scene_score,
        diag->last_scr_type,
        diag->hook_blit_installed ? 1 : 0,
        diag->hook_os_installed ? 1 : 0,
        diag->save_ok ? 1 : 0
    );
    fflush(fp);
    fclose(fp);
}

/* Safe non-MMIO, non-WFI millisecond delay calibrated for ARM926EJ-S (CX / CX II-T) */
static void nl_safe_delay_ms(unsigned ms) {
    if (ms == 0U) return;
    g_monotonic_ticks += ms;
    unsigned loops_per_ms = (nl_hwsubtype() == 2) ? 38000U : 22000U;
    unsigned max_guard = ms * loops_per_ms;
    for (volatile unsigned guard = 0U; guard < max_guard; guard++) {
        __asm__ volatile("" ::: "memory");
    }
}

/* =========================================================================
 * SP804 Hardware Timer 2 (0x900D0020, 32,768 Hz, Free-Running, Zero IRQ/WFI)
 * Provides true constant 60.0 FPS (16,667 us = 546 ticks) frame pacing and
 * microsecond-accurate hardware render telemetry on TI-Nspire CX / CX II-T.
 * ========================================================================= */
#define NL_SP804_T2_LOAD_ADDR   0x900D0020U
#define NL_SP804_T2_VALUE_ADDR  0x900D0024U
#define NL_SP804_T2_CTRL_ADDR   0x900D0028U
#define NL_FRAME_TARGET_TICKS   546U    /* 546 / 32768 s = 16,662.6 us (~60.01 Hz) */

static bool     g_hw_timer_ok = false;
static uint32_t g_hw_timer_saved_load = 0U;
static uint32_t g_hw_timer_saved_ctrl = 0U;
static uint32_t g_hw_timer_origin = 0xFFFFFFFFU;

static inline uint32_t nl_hw_timer_raw_val(void) {
#ifdef _TINSPIRE
    return *(volatile const uint32_t *)NL_SP804_T2_VALUE_ADDR;
#else
    return 0U;
#endif
}

static inline uint32_t nl_hw_timer_ticks(void) {
    if (!g_hw_timer_ok) return (g_monotonic_ticks * 33U);
    return (uint32_t)(g_hw_timer_origin - nl_hw_timer_raw_val());
}

static inline uint32_t nl_ticks_to_us(uint32_t ticks) {
    /* ticks * 1,000,000 / 32,768 == (ticks * 15,625) >> 9 (zero 64-bit division!) */
    return (uint32_t)(((uint64_t)ticks * 15625ULL) >> 9);
}

static bool g_hw_timer_verified_once = false;

static void nl_hw_timer_init(void) {
#ifdef _TINSPIRE
    if (g_hw_timer_ok) return;
    volatile uint32_t *t_load = (volatile uint32_t *)NL_SP804_T2_LOAD_ADDR;
    volatile uint32_t *t_ctrl = (volatile uint32_t *)NL_SP804_T2_CTRL_ADDR;

    g_hw_timer_saved_load = *t_load;
    g_hw_timer_saved_ctrl = *t_ctrl;

    /* Enable 32-bit free-running down-counter, prescale /1, IRQ disabled (bit 5 = 0) */
    *t_ctrl = 0x00U;
    *t_load = 0xFFFFFFFFU;
    *t_ctrl = 0x82U;

    /* #R23 Fast path on game return: if SP804 was already verified at boot, skip 1ms self-test delay */
    if (g_hw_timer_verified_once) {
        g_hw_timer_origin = nl_hw_timer_raw_val();
        g_hw_timer_ok = true;
        return;
    }

    uint32_t v0 = nl_hw_timer_raw_val();
    nl_safe_delay_ms(1);
    uint32_t v1 = nl_hw_timer_raw_val();
    uint32_t delta = (uint32_t)(v0 - v1);

    /* At 32,768 Hz, 1 ms is ~33 ticks; accept generous 8..400 window for self-test */
    if (delta >= 8U && delta <= 400U) {
        g_hw_timer_origin = nl_hw_timer_raw_val();
        g_hw_timer_ok = true;
        g_hw_timer_verified_once = true;
    } else {
        *t_ctrl = 0x00U;
        *t_load = g_hw_timer_saved_load;
        *t_ctrl = g_hw_timer_saved_ctrl;
        g_hw_timer_ok = false;
    }
#else
    g_hw_timer_ok = false;
#endif
}

static void nl_hw_timer_restore(void) {
#ifdef _TINSPIRE
    if (!g_hw_timer_ok) return;
    volatile uint32_t *t_load = (volatile uint32_t *)NL_SP804_T2_LOAD_ADDR;
    volatile uint32_t *t_ctrl = (volatile uint32_t *)NL_SP804_T2_CTRL_ADDR;
    *t_ctrl = 0x00U;
    *t_load = g_hw_timer_saved_load;
    *t_ctrl = g_hw_timer_saved_ctrl;
    g_hw_timer_ok = false;
#endif
}

static void nl_hw_timer_pace_deadline(uint32_t deadline_ticks) {
    if (!g_hw_timer_ok) {
        nl_safe_delay_ms(14);
        return;
    }
    /* Bounded busy-poll: zero IRQ, zero WFI, cannot hang even on timer wrap */
    unsigned guard = 0U;
    const unsigned max_guard = (nl_hwsubtype() == 2) ? 900000U : 500000U;
    while ((int32_t)(deadline_ticks - nl_hw_timer_ticks()) > 0) {
        if (++guard >= max_guard) break;
        __asm__ volatile("" ::: "memory");
    }
    g_monotonic_ticks = nl_ticks_to_us(nl_hw_timer_ticks()) / 1000U;
}

static void nl_perf_record_frame(nl_state_t *state, uint32_t dt_us, uint32_t render_us) {
    if (!state) return;
    nl_perf_stats_t *p = &state->perf;
    if (dt_us == 0U) dt_us = 16667U;
    if (dt_us > 500000U) dt_us = 500000U; /* Clamp across game exec pauses */

    if (p->frame_count == 0U) {
        p->min_dt_us = dt_us;
        p->max_dt_us = dt_us;
    } else {
        if (dt_us < p->min_dt_us) p->min_dt_us = dt_us;
        if (dt_us > p->max_dt_us) p->max_dt_us = dt_us;
        uint32_t jit = (dt_us >= p->last_dt_us) ? (dt_us - p->last_dt_us) : (p->last_dt_us - dt_us);
        p->total_jitter_us += jit;
        if (jit > p->max_jitter_us) p->max_jitter_us = jit;
    }
    p->last_dt_us = dt_us;
    p->frame_count++;
    p->total_dt_us += dt_us;
    p->total_render_us += render_us;
    if (render_us > p->max_render_us) p->max_render_us = render_us;
    if (dt_us > 20000U) p->fps_drop_count++;
    if (dt_us > 25000U) p->stutter_count++;

    if (state->sui.screen != NL_UI_LIBRARY || state->anim.set_open_fp > 0) {
        if (render_us > p->menu_render_us) p->menu_render_us = render_us;
    }
    if (state->anim.prof_switch_active || state->anim.prof_birth_active || state->anim.prof_plus_active) {
        if (render_us > p->profile_render_us) p->profile_render_us = render_us;
    }
    if (nl_mouse_is_active(state) && state->anim.cur_opacity_fp > 0) {
        if (render_us > p->cursor_render_us) p->cursor_render_us = render_us;
    }
}

static void nl_save_perf_report(const nl_state_t *state, const char *docs_root) {
    if (!state || !docs_root || docs_root[0] == '\0' || state->perf.frame_count == 0U) return;
    char perf_path[NL_MAX_PATH_LEN];
    snprintf(perf_path, sizeof(perf_path), "%.*s/ndless/nlauncher.perf.tns",
             (int)(sizeof(perf_path) - 32U), docs_root);
    FILE *fp = fopen(perf_path, "wb");
    if (!fp) return;

    const nl_perf_stats_t *p = &state->perf;
    uint32_t avg_dt = p->total_dt_us / p->frame_count;
    if (avg_dt == 0U) avg_dt = 16667U;
    uint32_t avg_fps_x10 = (10000000U + avg_dt / 2U) / avg_dt;
    uint32_t min_fps_x10 = (p->max_dt_us > 0U) ? ((10000000U + p->max_dt_us / 2U) / p->max_dt_us) : 0U;
    uint32_t avg_ren = p->total_render_us / p->frame_count;
    uint32_t avg_jit = (p->frame_count > 1U) ? (p->total_jitter_us / (p->frame_count - 1U)) : 0U;

    fprintf(fp, "# N-Launcher Hardware Performance Telemetry (%s)\n", NL_BUILD_STAMP_STR);
    fprintf(fp, "os=%s\n", state->sys.os_version);
    fprintf(fp, "hw_timer_sp804=%s\n", p->hw_timer_active ? "ACTIVE_32768HZ" : "FALLBACK");
    fprintf(fp, "frames=%lu\n", (unsigned long)p->frame_count);
    fprintf(fp, "fps_avg=%lu.%lu\n", (unsigned long)(avg_fps_x10 / 10U), (unsigned long)(avg_fps_x10 % 10U));
    fprintf(fp, "fps_min=%lu.%lu\n", (unsigned long)(min_fps_x10 / 10U), (unsigned long)(min_fps_x10 % 10U));
    fprintf(fp, "frame_dt_avg_us=%lu\n", (unsigned long)avg_dt);
    fprintf(fp, "frame_dt_min_us=%lu\n", (unsigned long)p->min_dt_us);
    fprintf(fp, "frame_dt_max_us=%lu\n", (unsigned long)p->max_dt_us);
    fprintf(fp, "jitter_avg_us=%lu\n", (unsigned long)avg_jit);
    fprintf(fp, "jitter_max_us=%lu\n", (unsigned long)p->max_jitter_us);
    fprintf(fp, "render_avg_us=%lu\n", (unsigned long)avg_ren);
    fprintf(fp, "render_max_us=%lu\n", (unsigned long)p->max_render_us);
    fprintf(fp, "fps_drops_below_50=%lu\n", (unsigned long)p->fps_drop_count);
    fprintf(fp, "stutters_above_25ms=%lu\n", (unsigned long)p->stutter_count);
    fprintf(fp, "peak_open_render_us=%lu\n", (unsigned long)p->open_render_us);
    fprintf(fp, "peak_menu_render_us=%lu\n", (unsigned long)p->menu_render_us);
    fprintf(fp, "peak_launch_render_us=%lu\n", (unsigned long)p->launch_render_us);
    fprintf(fp, "peak_profile_render_us=%lu\n", (unsigned long)p->profile_render_us);
    fprintf(fp, "peak_cursor_render_us=%lu\n", (unsigned long)p->cursor_render_us);
    fprintf(fp, "mouse_physical_clicks=%lu\n", (unsigned long)p->physical_clicks);
    fprintf(fp, "mouse_tap_clicks=%lu\n", (unsigned long)p->tap_clicks);
    fprintf(fp, "return_restore_us=%lu\n", (unsigned long)p->return_restore_us);
    fprintf(fp, "return_count=%lu\n", (unsigned long)p->return_count);
    fprintf(fp, "open_total_ms=%lu\n", (unsigned long)p->open_total_ms);
    fprintf(fp, "close_total_ms=%lu\n", (unsigned long)p->close_total_ms);
    fflush(fp);
    fclose(fp);
}

static bool nl_any_hardware_input_active(void) {
    if (on_key_pressed() || any_key_pressed()) {
        return true;
    }

    touchpad_report_t tp;
    if (touchpad_scan(&tp) == 0) {
        if (tp.pressed || tp.contact) {
            return true;
        }
    }
    return false;
}

static nl_touchpad_tracker_t g_tp_tracker;
static nl_tp_click_guard_t   g_tp_click_guard;
static nl_save_shortcut_t    g_save_sc;
static bool                  g_tp_step_emitted = false;
static bool                  g_launch_btn_was_down = false;
static bool                  g_mouse_moved = false;
static bool                  g_mouse_action_fired = false;
static bool                  g_mouse_enter_was_down = false;
static bool                  g_enter_consumed_until_release = false;
static bool                  g_was_modal_or_settings = false;
static bool                  g_ignore_esc_until_release = false;
static bool                  g_pending_cfg_save = false;
static bool                  g_pending_prof_save = false;
static bool                  g_pending_lib_save = false;
static bool                  g_pending_cache_save = false;
static int                   g_pending_scr_save_idx = -1;
static bool                  g_pending_cap_log_save = false;
static nl_cap_diag_t         g_pending_cap_diag;

static bool nl_is_modal_or_transition_active(const nl_state_t *state) {
    if (!state) return false;
    if (state->sui.qp_open || state->sui.qp_pick_open || state->sui.qp_pick_pending) return true;
    if (state->anim.prof_switch_active || state->anim.prof_plus_active || state->anim.prof_birth_active) return true;
    if (state->sui.screen == NL_UI_SETTINGS_RESET) return true;
    if (state->sui.screen == NL_UI_SETTINGS_SHORTCUTS &&
        (state->sui.sc_picking_key || state->sui.sc_conflict_prompt)) return true;
    if (state->sui.screen == NL_UI_SETTINGS_ICONS && state->sui.icon_confirm_modal) return true;
    if (state->sui.screen == NL_UI_SETTINGS_DIAG && state->sui.diag_confirm_modal) return true;
    if (state->sui.screen == NL_UI_SETTINGS_PROFILES &&
        state->sui.prof_mode == NL_PROF_MODE_CONFIRM_DELETE) return true;
    return false;
}

static bool nl_other_matrix_key_active(void) {
    return isKeyPressed(KEY_NSPIRE_ESC) ||
           isKeyPressed(KEY_NSPIRE_ENTER) ||
           isKeyPressed(KEY_NSPIRE_TAB) ||
           isKeyPressed(KEY_NSPIRE_MENU) ||
           isKeyPressed(KEY_NSPIRE_DOC) ||
           isKeyPressed(KEY_NSPIRE_VAR) ||
           isKeyPressed(KEY_NSPIRE_DEL);
}

static bool nl_startup_abort_key_pressed(void) {
    return on_key_pressed() || isKeyPressed(KEY_NSPIRE_ESC);
}

/* Hang-proof CX II-T exit barrier: pure CPU polling (zero idle()/WFI),
 * guarantees ESC/ON/any key is completely released before returning to Ndless ploaderhook
 * so Ndless's own wait_no_key_pressed() -> idle() is never entered on CX II-T TI-OS 6.2.0. */
static void nl_wait_exit_safe_release(void) {
    unsigned clean_streak = 0;
    for (unsigned iter = 0; iter < 12000U; iter++) {
        if ((iter & 31U) == 0U) {
            nl_disable_watchdog_safe();
        }
        bool still_down = on_key_pressed() || any_key_pressed() || isKeyPressed(KEY_NSPIRE_ESC);
        if (!still_down) {
            touchpad_report_t tp;
            if (touchpad_scan(&tp) == 0 && (tp.pressed || tp.contact)) {
                still_down = true;
            }
        }
        if (!still_down) {
            clean_streak++;
            if (clean_streak >= 6U) {
                break;
            }
        } else {
            clean_streak = 0;
        }
        nl_safe_delay_ms(5);
    }
    nl_touchpad_tracker_reset(&g_tp_tracker);
    nl_tp_click_reset(&g_tp_click_guard);
}

static void nl_wait_all_keys_released(void) {
    unsigned clean_streak = 0;
    for (unsigned iter = 0; iter < 300U; iter++) {
        if (!nl_any_hardware_input_active()) {
            clean_streak++;
            if (clean_streak >= 3U) {
                nl_touchpad_tracker_reset(&g_tp_tracker);
                nl_tp_click_reset(&g_tp_click_guard);
                nl_save_shortcut_reset(&g_save_sc);
                return;
            }
        } else {
            clean_streak = 0;
        }
        nl_safe_delay_ms(6);
    }
    nl_touchpad_tracker_reset(&g_tp_tracker);
    nl_tp_click_reset(&g_tp_click_guard);
    nl_save_shortcut_reset(&g_save_sc);
}

/* Single-transaction touchpad + keypad polling */
static nl_key_t poll_calculator_key(nl_state_t *state, bool *out_ctrl_changed) {
    g_tp_step_emitted = false;
    g_mouse_moved = false;
    g_mouse_action_fired = false;
    if (out_ctrl_changed) *out_ctrl_changed = false;

    bool esc_or_on_down = isKeyPressed(KEY_NSPIRE_ESC) || on_key_pressed();
    if (g_ignore_esc_until_release) {
        if (!esc_or_on_down) {
            g_ignore_esc_until_release = false;
        }
    } else if (esc_or_on_down) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        if (state && state->sui.ctrl_mouse_active) {
            state->sui.ctrl_mouse_active = false;
            if (state->settings.touchpad_mode == NL_TP_MODE_TOUCHPAD) {
                state->anim.cur_opacity_fp = 0;
            }
            if (out_ctrl_changed) *out_ctrl_changed = true;
        }
        return NL_KEY_ESC;
    }

    bool save_btn_down = is_touchpad && isKeyPressed(KEY_NSPIRE_SCRATCHPAD);
    bool ctrl_down = isKeyPressed(KEY_NSPIRE_CTRL);
    bool s_down = isKeyPressed(KEY_NSPIRE_S);
    bool other_down = nl_other_matrix_key_active();

    /* Temporary CTRL hold switch in Mode 1 (Touchpad -> temporary Mouse Mode while held) */
    if (state != NULL) {
        bool want_ctrl_mouse = ctrl_down && (state->settings.touchpad_mode == NL_TP_MODE_TOUCHPAD);
        if (want_ctrl_mouse != state->sui.ctrl_mouse_active) {
            state->sui.ctrl_mouse_active = want_ctrl_mouse;
            if (want_ctrl_mouse) {
                nl_cursor_wake(state);
                nl_mouse_update_hover(state);
            } else {
                state->anim.mouse_last_contact = false;
                state->anim.mouse_vx_fp = 0;
                state->anim.mouse_vy_fp = 0;
                state->anim.cur_opacity_fp = 0;
                state->sui.hover_valid = false;
                state->sui.hover_kind = NL_HOVER_NONE;
            }
            if (out_ctrl_changed) *out_ctrl_changed = true;
        }
    }

    if (nl_save_shortcut_poll(&g_save_sc, save_btn_down, ctrl_down, s_down, other_down)) {
        return NL_KEY_SETTINGS;
    }

    bool picking_shortcut_key = (state != NULL) &&
                                (state->sui.screen == NL_UI_SETTINGS_SHORTCUTS && state->sui.sc_picking_key);
    bool inline_searching = (state != NULL) &&
                            (state->sui.screen == NL_UI_LIBRARY && state->sui.search_active);
    bool prof_name_edit = (state != NULL) &&
                          (state->sui.screen == NL_UI_SETTINGS_PROFILES &&
                           (state->sui.prof_mode == NL_PROF_MODE_EDIT_NAME ||
                            state->sui.prof_mode == NL_PROF_MODE_CREATE_NAME));
    bool typing_mode = picking_shortcut_key || inline_searching || prof_name_edit;

    bool enter_down = isKeyPressed(KEY_NSPIRE_ENTER);
    bool key5_down = (!typing_mode && isKeyPressed(KEY_NSPIRE_5));
    bool mouse_active = (state != NULL) && nl_mouse_is_active(state);
    bool modal_or_trans = nl_is_modal_or_transition_active(state);
    bool in_modal_or_settings = (state != NULL) && (state->sui.screen != NL_UI_LIBRARY || modal_or_trans);

    /* R16: Strict modal ENTER/5 non-propagation latch.
     * Any ENTER/5 consumed by Quick Profile, Avatar Picker, Settings, or any dialog MUST be
     * physically released before the Library behind it can register launch_holding or NL_KEY_EXE. */
    if (state != NULL && state->sui.consume_exe_release) {
        state->sui.consume_exe_release = false;
        g_enter_consumed_until_release = true;
        g_launch_btn_was_down = false;
        g_mouse_enter_was_down = true;
        state->anim.launch_holding = false;
    }
    if (g_was_modal_or_settings && !in_modal_or_settings && (enter_down || key5_down)) {
        g_enter_consumed_until_release = true;
        g_launch_btn_was_down = false;
        if (g_state) g_state->anim.launch_holding = false;
    }
    g_was_modal_or_settings = in_modal_or_settings;

    if (!enter_down && !key5_down) {
        g_enter_consumed_until_release = false;
        g_mouse_enter_was_down = false;
    }

    if (g_enter_consumed_until_release) {
        g_launch_btn_was_down = false;
        if (g_state) g_state->anim.launch_holding = false;
    } else if (mouse_active && !typing_mode && (enter_down || key5_down)) {
        /* If hovering a specific UI button/pill/arrow/badge/modal control in Mouse Mode, ENTER/5 clicks it once */
        if (state->sui.hover_valid &&
            state->sui.hover_kind != NL_HOVER_NONE &&
            state->sui.hover_kind != NL_HOVER_GAME_CARD &&
            state->sui.hover_kind != NL_HOVER_QP_OUTSIDE) {
            if (!g_mouse_enter_was_down) {
                g_mouse_enter_was_down = true;
                g_enter_consumed_until_release = true;
                g_launch_btn_was_down = false;
                if (g_state) g_state->anim.launch_holding = false;
                nl_save_shortcut_clear_latch(&g_save_sc);
                if (nl_mouse_click(state)) {
                    g_mouse_action_fired = true;
                }
            }
            return NL_KEY_NONE;
        }
    }

    /* On pure Library screen (ZERO modals or profile transitions active), pressing ENTER or 5
     * shrinks the selected icon while held and triggers NL_KEY_EXE upon release. */
    if (!g_enter_consumed_until_release &&
        state != NULL && state->sui.screen == NL_UI_LIBRARY && state->filtered_count > 0 &&
        !modal_or_trans) {
        bool launch_hold_now = enter_down || key5_down;
        if (launch_hold_now) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            if (mouse_active && state->sui.hover_valid &&
                state->sui.hover_kind == NL_HOVER_GAME_CARD &&
                state->sui.hover_index >= 0 &&
                state->sui.hover_index < state->filtered_count) {
                state->selected_pos = state->sui.hover_index;
            }
            g_launch_btn_was_down = true;
            if (g_state) g_state->anim.launch_holding = true;
            return NL_KEY_NONE;
        } else if (g_launch_btn_was_down) {
            g_launch_btn_was_down = false;
            if (g_state) g_state->anim.launch_holding = false;
            return NL_KEY_EXE;
        }
    } else {
        g_launch_btn_was_down = false;
        if (g_state) g_state->anim.launch_holding = false;
        if (!g_enter_consumed_until_release && (enter_down || key5_down)) {
            g_enter_consumed_until_release = true;
            nl_save_shortcut_clear_latch(&g_save_sc);
            return NL_KEY_EXE;
        }
    }

    touchpad_report_t tp;
    if (touchpad_scan(&tp) == 0) {
        g_tp_click_guard.scan_err_streak = 0;
        const touchpad_info_t *tpi = touchpad_getinfo();
        uint16_t pw = (tpi && tpi->width > 0U) ? tpi->width : 2328U;
        uint16_t ph = (tpi && tpi->height > 0U) ? tpi->height : 1691U;

        if (mouse_active) {
            /* Keep hover state fresh before evaluating physical click or tap-to-click */
            nl_mouse_update_hover(state);
            bool phys_pressed = (tp.pressed != 0) || ((tpad_arrow_t)tp.arrow == TPAD_ARROW_CLICK);

            /* Mouse Mode (Mode 2 or temporary CTRL):
             * 1. Real physical click (tp.pressed or TPAD_ARROW_CLICK rising edge) works everywhere.
             * 2. Independent Tap-to-Click (OUI/NON) anywhere on touchpad without sliding. */
            nl_tp_click_action_t act = nl_mouse_tp_poll(
                &g_tp_click_guard,
                tp.contact != 0,
                phys_pressed,
                (tpad_arrow_t)tp.arrow == TPAD_ARROW_CLICK,
                state->sui.hover_valid,
                state->settings.tap_to_click,
                tp.x, tp.y, pw, ph
            );
            nl_touchpad_tracker_reset(&g_tp_tracker);
            if (act != NL_TP_ACT_NONE) {
                if (act == NL_TP_ACT_PHYSICAL_CLICK) {
                    state->perf.physical_clicks++;
                } else if (act == NL_TP_ACT_TAP_CLICK) {
                    state->perf.tap_clicks++;
                }
                state->anim.mouse_vx_fp = 0;
                state->anim.mouse_vy_fp = 0;
                nl_save_shortcut_clear_latch(&g_save_sc);
                state->anim.cur_click_pulse_fp = 256;
                g_mouse_moved = true;
                if (nl_mouse_click(state)) {
                    g_mouse_action_fired = true;
                    if (modal_or_trans) {
                        g_enter_consumed_until_release = true;
                        g_tp_click_guard.tap_tracking = false;
                        g_tp_click_guard.cooldown_ticks = 10;
                    }
                }
                return NL_KEY_NONE;
            }
            /* Freeze pointer tracking while physical button is depressed so dome press never jitters off target */
            if (!phys_pressed) {
                if (nl_mouse_feed_touchpad(state, tp.contact != 0, tp.x, tp.y, pw, ph)) {
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    g_mouse_moved = true;
                }
            } else {
                state->anim.mouse_last_contact = false;
                state->anim.mouse_vx_fp = 0;
                state->anim.mouse_vy_fp = 0;
            }
        } else {
            if (state != NULL && state->sui.screen == NL_UI_LIBRARY &&
                !state->anim.prof_switch_active && !state->anim.prof_plus_active && !state->anim.prof_birth_active) {
                if (nl_tp_click_poll(
                        &g_tp_click_guard,
                        tp.contact != 0,
                        tp.pressed != 0,
                        (tpad_arrow_t)tp.arrow == TPAD_ARROW_CLICK,
                        tp.x, tp.y, pw, ph)) {
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    nl_touchpad_tracker_reset(&g_tp_tracker);
                    if (modal_or_trans) {
                        g_enter_consumed_until_release = true;
                        g_tp_click_guard.cooldown_ticks = 10;
                    }
                    return NL_KEY_EXE;
                }
            } else {
                nl_tp_click_reset(&g_tp_click_guard);
            }

            bool tp_nav_enabled = (state != NULL) && state->settings.touchpad_nav &&
                                  !picking_shortcut_key && !prof_name_edit;
            if (tp_nav_enabled && (!tp.pressed || (tpad_arrow_t)tp.arrow == TPAD_ARROW_CLICK)) {
                nl_key_t slide_key = nl_touchpad_process_sample_ex(
                    &g_tp_tracker, true, tp.contact != 0, tp.x, tp.y, pw, ph,
                    state->settings.touchpad_sensitivity
                );
                nl_cursor_apply_touchpad_nudge(state, &g_tp_tracker, tp.contact != 0);
                if (slide_key != NL_KEY_NONE) {
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    g_tp_step_emitted = true;
                    return slide_key;
                }
            } else {
                nl_touchpad_tracker_reset(&g_tp_tracker);
                if (state != NULL) {
                    nl_cursor_apply_touchpad_nudge(state, &g_tp_tracker, false);
                }
            }
        }

        /* In Mode 1 (Touchpad), directional pad clicks map to arrow keys.
         * In Mode 2 (Mouse), tp.pressed is already handled as a physical mouse click above! */
        if (!mouse_active && tp.pressed && (tpad_arrow_t)tp.arrow != TPAD_ARROW_CLICK) {
            switch ((tpad_arrow_t)tp.arrow) {
                case TPAD_ARROW_LEFT:
                case TPAD_ARROW_LEFTUP:
                case TPAD_ARROW_DOWNLEFT:
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    return NL_KEY_LEFT;
                case TPAD_ARROW_RIGHT:
                case TPAD_ARROW_UPRIGHT:
                case TPAD_ARROW_RIGHTDOWN:
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    return NL_KEY_RIGHT;
                case TPAD_ARROW_UP:
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    return NL_KEY_UP;
                case TPAD_ARROW_DOWN:
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    return NL_KEY_DOWN;
                default:
                    break;
            }
        }
    } else {
        /* Tolerate brief transient I2C scan glitches without wiping active tap/click state */
        g_tp_click_guard.scan_err_streak++;
        if (g_tp_click_guard.scan_err_streak >= NL_TP_SCAN_ERR_RESET_THRESHOLD) {
            nl_touchpad_tracker_reset(&g_tp_tracker);
            nl_tp_click_reset(&g_tp_click_guard);
            if (mouse_active) {
                state->anim.mouse_last_contact = false;
            }
        }
    }

    if (isKeyPressed(KEY_NSPIRE_TAB)) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        return NL_KEY_TAB;
    }
    if (isKeyPressed(KEY_NSPIRE_DOC)) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        return NL_KEY_DOC;
    }
    if (isKeyPressed(KEY_NSPIRE_MENU)) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        return NL_KEY_MENU;
    }

    if (typing_mode) {
        if (isKeyPressed(KEY_NSPIRE_DEL)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return NL_KEY_BACKSPACE;
        }
        if ((inline_searching || prof_name_edit) && isKeyPressed(KEY_NSPIRE_SPACE)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return NL_KEY_SPACE;
        }
        return NL_KEY_NONE;
    }

    if (state != NULL && state->sui.screen == NL_UI_LIBRARY) {
        if (isKeyPressed(KEY_NSPIRE_PERIOD)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return NL_KEY_PROFILE_QUICK;
        }
        if (!state->sui.qp_open && !state->sui.qp_pick_open) {
            /* R15: 0 and - navigate game pages (never profile switching) */
            if (isKeyPressed(KEY_NSPIRE_0)) {
                nl_save_shortcut_clear_latch(&g_save_sc);
                return NL_KEY_PAGE_PREV;
            }
            if (isKeyPressed(KEY_NSPIRE_MINUS)) {
                nl_save_shortcut_clear_latch(&g_save_sc);
                return NL_KEY_PAGE_NEXT;
            }
        }
    }

    if (isKeyPressed(KEY_NSPIRE_4)) { nl_save_shortcut_clear_latch(&g_save_sc); return NL_KEY_LEFT; }
    if (isKeyPressed(KEY_NSPIRE_6)) { nl_save_shortcut_clear_latch(&g_save_sc); return NL_KEY_RIGHT; }
    if (isKeyPressed(KEY_NSPIRE_8)) { nl_save_shortcut_clear_latch(&g_save_sc); return NL_KEY_UP; }
    if (isKeyPressed(KEY_NSPIRE_2)) { nl_save_shortcut_clear_latch(&g_save_sc); return NL_KEY_DOWN; }

    if (isKeyPressed(KEY_NSPIRE_VAR) || isKeyPressed(KEY_NSPIRE_MULTIPLY) || isKeyPressed(KEY_NSPIRE_PLUS)) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        return NL_KEY_FAV;
    }
    if (isKeyPressed(KEY_NSPIRE_DEL)) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        return NL_KEY_RESCAN;
    }
    if (isKeyPressed(KEY_NSPIRE_TRIG) || isKeyPressed(KEY_NSPIRE_7)) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        return NL_KEY_PAGE_PREV;
    }
    if (isKeyPressed(KEY_NSPIRE_EQU) || isKeyPressed(KEY_NSPIRE_9)) {
        nl_save_shortcut_clear_latch(&g_save_sc);
        return NL_KEY_PAGE_NEXT;
    }
    return NL_KEY_NONE;
}

static char poll_alpha_key(const nl_state_t *state) {
    if (!state) return 0;
    if (isKeyPressed(KEY_NSPIRE_CTRL)) return 0;

    static const struct {
        t_key key;
        char  ch;
    } k_alpha_map[] = {
        { KEY_NSPIRE_A, 'A' }, { KEY_NSPIRE_B, 'B' }, { KEY_NSPIRE_C, 'C' }, { KEY_NSPIRE_D, 'D' },
        { KEY_NSPIRE_E, 'E' }, { KEY_NSPIRE_F, 'F' }, { KEY_NSPIRE_G, 'G' }, { KEY_NSPIRE_H, 'H' },
        { KEY_NSPIRE_I, 'I' }, { KEY_NSPIRE_J, 'J' }, { KEY_NSPIRE_K, 'K' }, { KEY_NSPIRE_L, 'L' },
        { KEY_NSPIRE_M, 'M' }, { KEY_NSPIRE_N, 'N' }, { KEY_NSPIRE_O, 'O' }, { KEY_NSPIRE_P, 'P' },
        { KEY_NSPIRE_Q, 'Q' }, { KEY_NSPIRE_R, 'R' }, { KEY_NSPIRE_S, 'S' }, { KEY_NSPIRE_T, 'T' },
        { KEY_NSPIRE_U, 'U' }, { KEY_NSPIRE_V, 'V' }, { KEY_NSPIRE_W, 'W' }, { KEY_NSPIRE_X, 'X' },
        { KEY_NSPIRE_Y, 'Y' }, { KEY_NSPIRE_Z, 'Z' },
        { KEY_NSPIRE_1, '1' }, { KEY_NSPIRE_3, '3' }
    };

    for (size_t i = 0; i < sizeof(k_alpha_map) / sizeof(k_alpha_map[0]); i++) {
        if (k_alpha_map[i].ch == 'S' &&
            (g_save_sc.wait_full_release || g_save_sc.ctrl_latch_ticks > 0)) {
            continue;
        }
        if (isKeyPressed(k_alpha_map[i].key)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return k_alpha_map[i].ch;
        }
    }

    bool picking_shortcut_key = (state->sui.screen == NL_UI_SETTINGS_SHORTCUTS && state->sui.sc_picking_key);
    bool inline_searching = (state->sui.screen == NL_UI_LIBRARY && state->sui.search_active);
    bool prof_name_edit = (state->sui.screen == NL_UI_SETTINGS_PROFILES &&
                           (state->sui.prof_mode == NL_PROF_MODE_EDIT_NAME ||
                            state->sui.prof_mode == NL_PROF_MODE_CREATE_NAME));
    if (picking_shortcut_key || inline_searching || prof_name_edit) {
        if ((inline_searching || prof_name_edit) && isKeyPressed(KEY_NSPIRE_0)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return '0';
        }
        if (isKeyPressed(KEY_NSPIRE_2)) { nl_save_shortcut_clear_latch(&g_save_sc); return '2'; }
        if (isKeyPressed(KEY_NSPIRE_4)) { nl_save_shortcut_clear_latch(&g_save_sc); return '4'; }
        if (isKeyPressed(KEY_NSPIRE_5)) { nl_save_shortcut_clear_latch(&g_save_sc); return '5'; }
        if (isKeyPressed(KEY_NSPIRE_6)) { nl_save_shortcut_clear_latch(&g_save_sc); return '6'; }
        if (isKeyPressed(KEY_NSPIRE_7)) { nl_save_shortcut_clear_latch(&g_save_sc); return '7'; }
        if (isKeyPressed(KEY_NSPIRE_8)) { nl_save_shortcut_clear_latch(&g_save_sc); return '8'; }
        if (isKeyPressed(KEY_NSPIRE_9)) { nl_save_shortcut_clear_latch(&g_save_sc); return '9'; }
        if ((inline_searching || prof_name_edit) && isKeyPressed(KEY_NSPIRE_MINUS)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return '-';
        }
    }

    return 0;
}

static void strip_trailing_slash(char *path) {
    if (!path) return;
    size_t len = strlen(path);
    while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
        path[--len] = '\0';
    }
}

static void build_cfg_paths(
    const char *docs_root,
    const char *self_path,
    char *primary_cfg,
    size_t primary_max,
    char *fallback_cfg,
    size_t fallback_max
) {
    snprintf(primary_cfg, primary_max, "%.*s/ndless/nlauncher.cfg.tns",
             (int)(primary_max > 28U ? primary_max - 28U : 0U), docs_root);

    if (self_path && self_path[0] != '\0') {
        char self_dir[NL_MAX_PATH_LEN];
        snprintf(self_dir, sizeof(self_dir), "%s", self_path);
        char *last_sep = strrchr(self_dir, '/');
        if (last_sep && last_sep != self_dir) {
            *last_sep = '\0';
            snprintf(fallback_cfg, fallback_max, "%.*s/nlauncher.cfg.tns",
                     (int)(fallback_max > 22U ? fallback_max - 22U : 0U), self_dir);
            return;
        }
    }
    snprintf(fallback_cfg, fallback_max, "%.*s/nlauncher.cfg.tns",
             (int)(fallback_max > 22U ? fallback_max - 22U : 0U), docs_root);
}

static bool load_launcher_config(nl_state_t *state, const char *primary_cfg, const char *fallback_cfg) {
    if (nl_load_config(state, primary_cfg)) return true;
    if (fallback_cfg && strcmp(fallback_cfg, primary_cfg) != 0) {
        return nl_load_config(state, fallback_cfg);
    }
    return false;
}

static void save_launcher_config(const nl_state_t *state, const char *primary_cfg, const char *fallback_cfg) {
    if (nl_save_config(state, primary_cfg)) return;
    if (fallback_cfg && strcmp(fallback_cfg, primary_cfg) != 0) {
        (void)nl_save_config(state, fallback_cfg);
    }
}


static void set_working_dir_for_game(const char *game_path) {
    if (!game_path) return;
    char dir_buf[NL_MAX_PATH_LEN];
    strncpy(dir_buf, game_path, sizeof(dir_buf) - 1);
    dir_buf[sizeof(dir_buf) - 1] = '\0';

    char *last_slash = strrchr(dir_buf, '/');
    if (last_slash && last_slash != dir_buf) {
        *last_slash = '\0';
        chdir(dir_buf);
        NU_Set_Current_Dir(dir_buf);
    }
}

static void restore_documents_working_dir(const char *docs_root) {
    if (!docs_root || docs_root[0] == '\0') return;
    chdir(docs_root);
    NU_Set_Current_Dir(docs_root);
}

static void populate_sys_env(nl_state_t *state, const char *docs_root, const char *self_path) {
    if (!state) return;
    snprintf(state->sys.docs_root, sizeof(state->sys.docs_root), "%s", docs_root ? docs_root : "/documents");
    snprintf(state->sys.self_path, sizeof(state->sys.self_path), "%s", self_path ? self_path : "");
    state->sys.has_ndless = true;
    state->sys.ndless_rev = nl_ndless_rev();
    state->sys.hw_is_cx2 = (nl_hwsubtype() == 2);

#ifdef _TINSPIRE
    uint32_t os_sig = *(volatile const uint32_t *)0x10000020U;
    const char *resolved_os = nl_resolve_os_version_string(os_sig);
    snprintf(state->sys.os_version, sizeof(state->sys.os_version), "%s",
             (resolved_os && resolved_os[0] != '\0')
                 ? resolved_os
                 : (state->sys.hw_is_cx2 ? "TI-Nspire CX II" : "TI-Nspire CX"));
#else
    snprintf(state->sys.os_version, sizeof(state->sys.os_version), "%s",
             state->sys.hw_is_cx2 ? "TI-Nspire CX II" : "TI-Nspire CX");
#endif

    state->sys.ndless_persistent =
        nl_check_ndless_persistent_at("/phoenix/syst/poweroff");
}

static void ensure_required_dir(nl_state_t *state, const char *path) {
    if (!path || path[0] == '\0') return;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return;
    }
    bool created = (mkdir(path, 0777) == 0);
    if (!created && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return;
    }
    if (state) {
        const char *name = strrchr(path, '/');
        name = (name && name[1] != '\0') ? (name + 1) : path;
        nl_diag_report(
            state,
            NL_DIAG_E401_DIR_MISSING,
            name,
            path,
            "Verifier dossiers requis",
            "Dossier manquant sur flash",
            "Ressources associees absentes",
            created ? "Dossier recree automatiquement" : "Creation dossier impossible"
        );
    }
}

typedef struct {
    nl_state_t *state;
    uint16_t   *fb;
    uint32_t    start_ticks;
    uint32_t    last_render_ticks;
    uint32_t    last_frame_ms;
    uint32_t    pump_calls;
    bool        is_startup;
} nl_rescan_pump_ctx_t;

static void rescan_scan_progress_cb(const nl_scan_stats_t *stats, void *raw_ctx) {
    nl_rescan_pump_ctx_t *ctx = (nl_rescan_pump_ctx_t *)raw_ctx;
    if (!ctx || !ctx->state || !ctx->fb || !stats) return;

    if (ctx->is_startup && nl_startup_abort_key_pressed()) {
        ctx->state->scan.abort_requested = true;
        return;
    }

    ctx->pump_calls++;
    uint32_t now_t = nl_hw_timer_ticks();
    if (ctx->is_startup) {
        if (!g_hw_timer_ok || (uint32_t)(now_t - ctx->last_render_ticks) >= NL_FRAME_TARGET_TICKS) {
            uint32_t elapsed_ms = g_hw_timer_ok
                ? (nl_ticks_to_us(now_t - ctx->start_ticks) / 1000U)
                : (ctx->last_frame_ms + 16U);
            ctx->last_render_ticks = now_t;
            ctx->last_frame_ms = elapsed_ms;
            nl_render_splash_anim(ctx->state, ctx->fb, elapsed_ms, true);
            lcd_blit(ctx->fb, SCR_320x240_565);
        }
    } else {
        if (!g_hw_timer_ok || (uint32_t)(now_t - ctx->last_render_ticks) >= (NL_FRAME_TARGET_TICKS * 2U)) {
            ctx->last_render_ticks = now_t;
            nl_render_frame(ctx->state, ctx->fb);
            lcd_blit(ctx->fb, SCR_320x240_565);
        }
    }
}

static void rescan_icon_progress_cb(void *raw_ctx) {
    nl_rescan_pump_ctx_t *ctx = (nl_rescan_pump_ctx_t *)raw_ctx;
    if (!ctx || !ctx->state || !ctx->fb) return;

    if (ctx->is_startup && nl_startup_abort_key_pressed()) {
        ctx->state->scan.abort_requested = true;
        return;
    }

    ctx->pump_calls++;
    uint32_t now_t = nl_hw_timer_ticks();
    if (ctx->is_startup) {
        if (!g_hw_timer_ok || (uint32_t)(now_t - ctx->last_render_ticks) >= NL_FRAME_TARGET_TICKS) {
            uint32_t elapsed_ms = g_hw_timer_ok
                ? (nl_ticks_to_us(now_t - ctx->start_ticks) / 1000U)
                : (ctx->last_frame_ms + 16U);
            ctx->last_render_ticks = now_t;
            ctx->last_frame_ms = elapsed_ms;
            nl_render_splash_anim(ctx->state, ctx->fb, elapsed_ms, true);
            lcd_blit(ctx->fb, SCR_320x240_565);
        }
    } else if (!g_hw_timer_ok || (uint32_t)(now_t - ctx->last_render_ticks) >= (NL_FRAME_TARGET_TICKS * 2U)) {
        ctx->last_render_ticks = now_t;
        nl_render_frame(ctx->state, ctx->fb);
        lcd_blit(ctx->fb, SCR_320x240_565);
    }
}

static bool run_safe_rescan(
    const char *docs_root,
    const char *self_path,
    const char *primary_cfg,
    const char *fallback_cfg,
    const char *cache_path,
    bool is_startup,
    nl_rescan_pump_ctx_t *pump_ctx
) {
    static bool s_scan_busy = false;
    if (!g_state || s_scan_busy) return false;
    s_scan_busy = true;
    nl_disable_watchdog_safe();

    /* Release transient 307 KB ink transition buffers before scanning/icon loading */
    free(g_ink_old_fb); g_ink_old_fb = NULL;
    free(g_ink_new_fb); g_ink_new_fb = NULL;
    g_ink_rendered_seq = 0xFFFFFFFFU;
    g_state->anim.ink_active = false;
    g_state->scan.abort_requested = false;

    bool had_profiles_file = (g_state->profiles_path[0] != '\0' && nl_file_exists(g_state->profiles_path));
    if (g_state->profiles.initialized) {
        nl_profile_snapshot_active(g_state);
    }

    char ndless_dir[NL_MAX_PATH_LEN];
    snprintf(ndless_dir, sizeof(ndless_dir), "%.*s/ndless",
             (int)(sizeof(ndless_dir) - 8U), docs_root);
    snprintf(g_state->covers_dir, sizeof(g_state->covers_dir), "%s/ndless/covers", docs_root);
    snprintf(g_state->icons_dir, sizeof(g_state->icons_dir), "%s/ndless/icons", docs_root);
    snprintf(g_state->profiles_path, sizeof(g_state->profiles_path), "%.*s/ndless/nlauncher.profiles.tns",
             (int)(sizeof(g_state->profiles_path) - 34U), docs_root);
    snprintf(g_state->lib_path, sizeof(g_state->lib_path), "%.*s/ndless/nlauncher.lib.tns",
             (int)(sizeof(g_state->lib_path) - 30U), docs_root);
    ensure_required_dir(g_state, ndless_dir);
    ensure_required_dir(g_state, g_state->covers_dir);
    ensure_required_dir(g_state, g_state->icons_dir);

    /* Ensure default game folders are seeded alongside any user-memorized folders */
    nl_folder_seed_defaults(&g_state->folders, docs_root);

    if (!is_startup && g_framebuffer) {
        g_state->scan.active = true;
        g_state->scan.finished = false;
        g_state->scan.empty_confirm_pending = false;
        snprintf(g_state->scan.current_dir, sizeof(g_state->scan.current_dir), "%s", docs_root);
        nl_render_frame(g_state, g_framebuffer);
        lcd_blit(g_framebuffer, SCR_320x240_565);
    }

    /* Transactional, zero-recursion, Nucleus-safe two-phase scan into temporary buffer */
    bool ok = nl_transactional_scan(
        g_state,
        docs_root,
        self_path,
        is_startup,
        false,
        pump_ctx ? rescan_scan_progress_cb : NULL,
        pump_ctx
    );

    if (g_state->scan.abort_requested) {
        nl_disable_watchdog_safe();
        s_scan_busy = false;
        return false;
    }

    if (!g_state->scan.empty_confirm_pending) {
        if (is_startup) {
            load_launcher_config(g_state, primary_cfg, fallback_cfg);
        }
        if (g_state->profiles.initialized) {
            nl_profile_apply_active(g_state);
        } else if (!had_profiles_file) {
            nl_profiles_init_default(g_state);
        }

        /* Icons are strictly optional & decoupled from game presence: load safely with intra-PNG 60 FPS pumping */
        nl_set_icon_decode_progress_cb(pump_ctx ? rescan_icon_progress_cb : NULL, pump_ctx);
        nl_load_all_icons_ex(g_state,
                             pump_ctx ? rescan_icon_progress_cb : NULL,
                             pump_ctx);
        nl_set_icon_decode_progress_cb(NULL, NULL);

        if (g_state->scan.abort_requested) {
            nl_disable_watchdog_safe();
            s_scan_busy = false;
            return false;
        }

        int loaded_ico = 0, fallback_ico = 0;
        for (int i = 0; i < g_state->total_games; i++) {
            if (g_state->games[i].icon_source != NL_ICON_NONE && g_state->games[i].icon_pixels != NULL) {
                loaded_ico++;
            } else {
                fallback_ico++;
            }
        }
        g_state->scan.icons_loaded = loaded_ico;
        g_state->scan.icons_fallback = fallback_ico;

        /* R15/R20: Independently probe valid on-disk .scr.tns captures with smooth progress pumping */
        nl_probe_all_screenshots_ex(g_state, pump_ctx ? rescan_icon_progress_cb : NULL, pump_ctx);
        nl_rebuild_filter(g_state);

        /* Persist icon-independent library + folders index first (atomic + CRC32) */
        if (g_state->lib_path[0] != '\0') {
            (void)nl_save_library_index(g_state, g_state->lib_path);
        }
        save_launcher_config(g_state, primary_cfg, fallback_cfg);
        if (g_state->profiles.initialized) {
            (void)nl_save_profiles(g_state, g_state->profiles_path);
        }
        /* Optional binary cache: never write a 0-game cache that could hide games on next boot */
        if (cache_path && cache_path[0] != '\0' && g_state->total_games > 0) {
            if (nl_save_library_cache(g_state, cache_path)) {
                g_state->sys.cache_valid = true;
            }
        }
    }

    nl_disable_watchdog_safe();
    s_scan_busy = false;
    return ok;
}

static nl_scan_job_t *g_active_scan_job = NULL;

/* Start a non-blocking incremental rescan job that steps in small per-frame batches
 * inside the 60 FPS main loop so cursor movement, CTRL mode switch, ESC and the
 * visible "Annuler" button stay 100% interactive throughout the entire scan. */
static bool start_interactive_rescan(
    const char *docs_root,
    const char *self_path,
    const char *primary_cfg,
    const char *fallback_cfg,
    bool from_library
) {
    if (!g_state) return false;
    if (g_active_scan_job != NULL) {
        nl_scan_job_cancel(g_active_scan_job, g_state);
        nl_scan_job_destroy(g_active_scan_job);
        g_active_scan_job = NULL;
    }

    g_state->anim.ink_active = false;
    free(g_ink_old_fb); g_ink_old_fb = NULL;
    free(g_ink_new_fb); g_ink_new_fb = NULL;
    g_ink_rendered_seq = 0xFFFFFFFFU;
    nl_renderer_cleanup();

    char ndless_dir[NL_MAX_PATH_LEN];
    snprintf(ndless_dir, sizeof(ndless_dir), "%.*s/ndless",
             (int)(sizeof(ndless_dir) - 8U), docs_root);
    snprintf(g_state->covers_dir, sizeof(g_state->covers_dir), "%s/ndless/covers", docs_root);
    snprintf(g_state->icons_dir, sizeof(g_state->icons_dir), "%s/ndless/icons", docs_root);
    snprintf(g_state->profiles_path, sizeof(g_state->profiles_path), "%.*s/ndless/nlauncher.profiles.tns",
             (int)(sizeof(g_state->profiles_path) - 34U), docs_root);
    snprintf(g_state->lib_path, sizeof(g_state->lib_path), "%.*s/ndless/nlauncher.lib.tns",
             (int)(sizeof(g_state->lib_path) - 30U), docs_root);
    ensure_required_dir(g_state, ndless_dir);
    ensure_required_dir(g_state, g_state->covers_dir);
    ensure_required_dir(g_state, g_state->icons_dir);
    nl_folder_seed_defaults(&g_state->folders, docs_root);

    save_launcher_config(g_state, primary_cfg, fallback_cfg);

    g_state->sui.screen = NL_UI_SETTINGS_RESCAN;
    g_state->anim.set_open_fp = 256;
    g_state->anim.set_sec_fp = 256;
    g_state->anim.set_last_screen = NL_UI_SETTINGS_RESCAN;
    g_state->anim.set_prev_screen = from_library ? NL_UI_LIBRARY : NL_UI_SETTINGS_MAIN;

    g_active_scan_job = nl_scan_job_start(g_state, docs_root, self_path);
    if (!g_active_scan_job) {
        g_state->scan.active = false;
        g_state->scan.finished = false;
        nl_show_toast(g_state, "MEMOIRE INSUFFISANTE", 70);
        return false;
    }
    return true;
}

static void nl_flush_pending_screenshot_only(void) {
    if (!g_state) {
        g_pending_scr_save_idx = -1;
        g_pending_cap_log_save = false;
        return;
    }
    if (g_pending_scr_save_idx >= 0 && g_pending_scr_save_idx < g_state->total_games) {
        nl_game_t *g = &g_state->games[g_pending_scr_save_idx];
        if (g->has_screenshot && g->scr_pixels != NULL && nl_screenshot_is_valid_buffer(g->scr_pixels)) {
            bool ok = nl_save_captured_screenshot(g, g_state->covers_dir);
            g_pending_cap_diag.save_ok = ok;
            g_state->cap_diag.save_ok = ok;
            if (!ok) {
                g_pending_cap_diag.status = NL_CAP_FAIL_SAVE_IO;
                g_state->cap_diag.status = NL_CAP_FAIL_SAVE_IO;
            }
        }
        g_pending_scr_save_idx = -1;
    }
    if (g_pending_cap_log_save) {
        nl_write_internal_cap_log(&g_pending_cap_diag, g_state->covers_dir);
        g_pending_cap_log_save = false;
    }
}

/* #R23 Idle-gated staggered NAND Flash flusher:
 * Writes AT MOST ONE small pending file per idle window (>= 2.5s of zero user input and zero animations)
 * so returning from a game NEVER hits a multi-second synchronous Flash write storm! */
static bool nl_flush_one_deferred_save(
    const char *cfg_path,
    const char *fallback_cfg_path,
    const char *cache_path
) {
    if (!g_state) return false;
    if (g_pending_scr_save_idx >= 0) {
        nl_flush_pending_screenshot_only();
        return true;
    }
    if (g_pending_cap_log_save) {
        nl_write_internal_cap_log(&g_pending_cap_diag, g_state->covers_dir);
        g_pending_cap_log_save = false;
        return true;
    }
    if (g_pending_cfg_save) {
        save_launcher_config(g_state, cfg_path, fallback_cfg_path);
        g_pending_cfg_save = false;
        return true;
    }
    if (g_pending_prof_save) {
        (void)nl_save_profiles(g_state, g_state->profiles_path);
        g_pending_prof_save = false;
        return true;
    }
    if (g_pending_lib_save && g_state->lib_path[0] != '\0') {
        (void)nl_save_library_index(g_state, g_state->lib_path);
        g_pending_lib_save = false;
        return true;
    }
    if (g_pending_cache_save && cache_path && cache_path[0] != '\0' && g_state->total_games > 0) {
        if (nl_save_library_cache(g_state, cache_path)) {
            g_state->sys.cache_valid = true;
        }
        g_pending_cache_save = false;
        return true;
    }
    return false;
}

static bool execute_game_launch_and_capture(
    int launched_idx,
    bool is_batch_step,
    const char *docs_root,
    const char *cfg_path,
    const char *fallback_cfg_path,
    const char *cache_path
) {
    (void)cache_path;
    if (!g_state || launched_idx < 0 || launched_idx >= g_state->total_games) return false;
    nl_game_t *target = &g_state->games[launched_idx];

    if (!nl_file_exists(target->path)) {
        if (is_batch_step) {
            nl_batch_capture_advance(g_state, false, true, NL_CAP_FAIL_EXEC_ERROR);
        } else {
            nl_show_toast(g_state, "FICHIER INTROUVABLE", 80);
        }
        g_state->launch_game_idx = -1;
        return true;
    }

    /* Flush any deferred screenshot from a previous game before launching a new one */
    nl_flush_pending_screenshot_only();

    bool prelaunch_prepared = false;

    if (!is_batch_step && g_framebuffer) {
        g_state->anim.return_drop_active = false;
        g_state->anim.return_drop_tick = 0;
        g_state->anim.launch_anim_active = true;
        if (g_state->settings.launch_anim_mode == NL_LAUNCH_ANIM_LOADING_BAR) {
            /* #R20/#R21 Animation 4 (Loading Bar): Track REAL pre-launch stages cleanly while avoiding useless Flash writes */
            g_state->anim.launch_load_pct = 0;
            g_state->anim.launch_load_stage = 0;
            int target_pct = 0;
            uint32_t z_deadline = nl_hw_timer_ticks();
            const int total_steps = 38;
            for (int z = 1; z <= total_steps; z++) {
                /* Execute genuine pre-launch stages once the icon reaches center & bar appears */
                if (z == 16) {
                    /* Stage 1: Verify target binary readiness on Flash */
                    (void)nl_file_exists(target->path);
                    g_state->anim.launch_load_stage = 1;
                    target_pct = 20;
                } else if (z == 20) {
                    /* Stage 2: Load on-demand .scr.tns capture metadata if not in RAM */
                    if (!target->scr_pixels) {
                        (void)nl_load_game_screenshot(target, g_state->covers_dir);
                    }
                    g_state->anim.launch_load_stage = 2;
                    target_pct = 42;
                } else if (z == 25) {
                    /* Stage 3: Persist launcher configuration to Flash ONLY if modified */
                    if (g_pending_cfg_save) {
                        save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                        g_pending_cfg_save = false;
                    }
                    g_state->anim.launch_load_stage = 3;
                    target_pct = 68;
                } else if (z == 30) {
                    /* Stage 4: Persist active profile to Flash ONLY if modified */
                    if (g_pending_prof_save) {
                        (void)nl_save_profiles(g_state, g_state->profiles_path);
                        g_pending_prof_save = false;
                    }
                    g_state->anim.launch_load_stage = 4;
                    target_pct = 88;
                } else if (z == 34) {
                    /* Stage 5: Switch working directory for game execution */
                    set_working_dir_for_game(target->path);
                    g_state->anim.launch_load_stage = 5;
                    target_pct = 100;
                    prelaunch_prepared = true;
                }

                /* Smoothly interpolate displayed bar width strictly toward real completed stage target */
                if (g_state->anim.launch_load_pct < target_pct) {
                    int diff = target_pct - g_state->anim.launch_load_pct;
                    int step_p = (diff + 2) / 3;
                    if (step_p < 4) step_p = 4;
                    g_state->anim.launch_load_pct += step_p;
                    if (g_state->anim.launch_load_pct > target_pct) {
                        g_state->anim.launch_load_pct = target_pct;
                    }
                }
                if (z >= total_steps - 2) {
                    g_state->anim.launch_load_pct = 100;
                }

                uint32_t rt0 = nl_hw_timer_ticks();
                nl_render_launch_zoom(g_state, g_framebuffer, z, total_steps);
                lcd_blit(g_framebuffer, SCR_320x240_565);
                uint32_t rus = nl_ticks_to_us(nl_hw_timer_ticks() - rt0);
                if (rus > g_state->perf.launch_render_us) g_state->perf.launch_render_us = rus;
                z_deadline += NL_FRAME_TARGET_TICKS;
                uint32_t now_z = nl_hw_timer_ticks();
                if ((int32_t)(z_deadline - now_z) < -(int32_t)(NL_FRAME_TARGET_TICKS * 2U)) {
                    z_deadline = now_z;
                }
                nl_hw_timer_pace_deadline(z_deadline);
            }
        } else {
            /* Modes 0 (Actuelle), 1 (Theme Drop), 2 (Spinner): 30-frame 60 FPS transition */
            if (!target->scr_pixels) {
                (void)nl_load_game_screenshot(target, g_state->covers_dir);
            }
            uint32_t z_deadline = nl_hw_timer_ticks();
            for (int z = 1; z <= 30; z++) {
                uint32_t rt0 = nl_hw_timer_ticks();
                nl_render_launch_zoom(g_state, g_framebuffer, z, 30);
                lcd_blit(g_framebuffer, SCR_320x240_565);
                uint32_t rus = nl_ticks_to_us(nl_hw_timer_ticks() - rt0);
                if (rus > g_state->perf.launch_render_us) g_state->perf.launch_render_us = rus;
                z_deadline += NL_FRAME_TARGET_TICKS;
                uint32_t now_z = nl_hw_timer_ticks();
                if ((int32_t)(z_deadline - now_z) < -(int32_t)(NL_FRAME_TARGET_TICKS * 2U)) {
                    z_deadline = now_z;
                }
                nl_hw_timer_pace_deadline(z_deadline);
            }
        }
    } else if (is_batch_step && g_framebuffer) {
        nl_render_frame(g_state, g_framebuffer);
        lcd_blit(g_framebuffer, SCR_320x240_565);
    }

    if (!prelaunch_prepared) {
        /* #R21 Only flush cfg/profiles before exec if actually modified (eliminates redundant Flash writes) */
        if (g_pending_cfg_save) {
            save_launcher_config(g_state, cfg_path, fallback_cfg_path);
            g_pending_cfg_save = false;
        }
        if (g_pending_prof_save) {
            (void)nl_save_profiles(g_state, g_state->profiles_path);
            g_pending_prof_save = false;
        }
        set_working_dir_for_game(target->path);
    }
    if (nl_any_hardware_input_active()) {
        nl_wait_all_keys_released();
    }

    free(g_ink_old_fb); g_ink_old_fb = NULL;
    free(g_ink_new_fb); g_ink_new_fb = NULL;
    g_ink_rendered_seq = 0xFFFFFFFFU;
    /* #R23 Fast-return exec prep:
     * - Frees heavy transient scratch buffers (~530 KB) while keeping current + peek sprites warm.
     * - On TI-Nspire CX II-T (64 MB RAM), keeps g_framebuffer (153 KB) AND G_LIB_STATIC_FB (153 KB)
     *   allocated across nl_exec() (= 0.47% of RAM) so returning from a game needs ZERO malloc and
     *   ZERO full-grid redraw on Frame 1! On CX I (32 MB RAM), frees g_framebuffer & G_LIB_STATIC_FB. */
    nl_renderer_prepare_for_exec(g_state);
    bool keep_fb_across_exec = g_state->sys.hw_is_cx2 && !is_batch_step;
    if (!keep_fb_across_exec) {
        free(g_framebuffer);
        g_framebuffer = NULL;
    }
    nl_hw_timer_restore();
    lcd_init(SCR_TYPE_INVALID);

    memset(&g_cap_diag_local, 0, sizeof(g_cap_diag_local));
    snprintf(g_cap_diag_local.game_filename, sizeof(g_cap_diag_local.game_filename),
             "%s", target->filename);
    g_cap_diag_local.best_scene_score = -1;
    g_cap_diag_local.last_scr_type = -1;

    /* R15/R23: Protect existing valid screenshots on disk without redundant Flash I/O when already known */
    bool has_valid_existing = (target->has_screenshot && target->scr_pixels != NULL &&
                               nl_screenshot_is_valid_buffer(target->scr_pixels)) ||
                              (target->has_screenshot) ||
                              nl_game_has_valid_screenshot_on_disk(target, g_state->covers_dir);
    if (has_valid_existing) {
        target->has_screenshot = true;
    }
    bool allow_capture = !has_valid_existing;
    bool prealloc_for_cap = false;
    if (allow_capture && target->scr_pixels == NULL) {
        target->scr_pixels = (uint16_t *)malloc(NL_COVER_BYTES);
        prealloc_for_cap = (target->scr_pixels != NULL);
    }

    g_cap_done = false;
    g_cap_staged = false;
    g_cap_blit_seen = false;
    g_cap_in_dialog = false;
    g_cap_in_hook = false;
    g_cap_io_calls = 0;
    g_cap_tp_calls = 0;
    g_cap_start_rtc = g_monotonic_ticks / 1000U;
    uint32_t session_start_sec = g_cap_start_rtc;
    g_cap_target = (allow_capture && target->scr_pixels != NULL) ? target : NULL;
    g_cap_armed = (g_cap_target != NULL);

    if (g_cap_armed) {
        nl_install_capture_hooks();
    } else {
        g_cap_diag_local.status = NL_CAP_SKIPPED_EXISTING;
    }

    /* #R23 Track exec boundary in RAM (zero synchronous NAND Flash writes before/after nl_exec!) */
    nl_diag_mark_stage_ram(g_state, NL_STAGE_12_READY);
    int rc = nl_exec(target->path, 0, NULL);

    /* =========================================================================
     * #R23 ULTRA-FAST GAME RETURN CRITICAL PATH
     * Reclaim timer + LCD + Frame 1 blit IMMEDIATELY before any FATFS / chdir /
     * diagnostic work so the stale TI-OS screen never lingers and the library
     * is live & interactive within milliseconds of exiting the game!
     * ========================================================================= */
    nl_hw_timer_init(); /* Fast path: 0 ms delay because SP804 already verified at boot */
    uint32_t ret_t0 = nl_hw_timer_ticks();
    g_state->perf.hw_timer_active = g_hw_timer_ok;

    nl_uninstall_capture_hooks();
    g_cap_armed = false;
    g_cap_target = NULL;
    g_state->last_exec_status = rc;
    nl_diag_mark_stage_ram(g_state, NL_STAGE_12_READY);

    /* Immediately take back the LCD controller (eliminates stale TI-OS intermediate screen!) */
    lcd_init(SCR_320x240_565);

    if (!g_framebuffer) {
        g_framebuffer = (uint16_t *)malloc(NL_FB_BYTES);
        if (!g_framebuffer) {
            nl_recover_memory_on_oom(g_state, "Reprise affichage jeu", target->path);
            g_framebuffer = (uint16_t *)malloc(NL_FB_BYTES);
        }
        if (!g_framebuffer) {
            g_state->anim.launch_anim_active = false;
            restore_documents_working_dir(docs_root);
            return false;
        }
    }

    uint32_t est_play_sec = 5U + (g_cap_diag_local.blit_frames_seen / 30U);
    uint32_t session_end_sec = session_start_sec + est_play_sec;

    if (allow_capture && rc != 0xDEAD && !g_cap_done && g_cap_staged &&
        target->scr_pixels != NULL && nl_screenshot_is_valid_buffer(target->scr_pixels)) {
        target->has_screenshot = true;
        g_cap_done = true;
        g_cap_diag_local.status = NL_CAP_CAPTURED_STAGED;
    }

    bool captured_now = (rc != 0xDEAD) && g_cap_done && target->has_screenshot &&
                        target->scr_pixels != NULL && nl_screenshot_is_valid_buffer(target->scr_pixels);
    bool saved_ok = false;
    if (captured_now) {
        if (is_batch_step) {
            saved_ok = nl_save_captured_screenshot(target, g_state->covers_dir);
            g_cap_diag_local.save_ok = saved_ok;
            if (!saved_ok) {
                target->has_screenshot = has_valid_existing;
                g_cap_diag_local.status = NL_CAP_FAIL_SAVE_IO;
            }
        } else {
            /* Defer .scr.tns Flash write until idle window! */
            saved_ok = true;
            g_cap_diag_local.save_ok = true;
            g_pending_scr_save_idx = launched_idx;
        }
    } else {
        if (prealloc_for_cap && !target->has_screenshot && target->scr_pixels != NULL) {
            free(target->scr_pixels);
            target->scr_pixels = NULL;
        }
        if (allow_capture) {
            if (rc == 0xDEAD) {
                g_cap_diag_local.status = NL_CAP_FAIL_EXEC_ERROR;
            } else if (g_cap_diag_local.blit_frames_seen == 0U &&
                       g_cap_diag_local.vram_probes_seen == 0U) {
                g_cap_diag_local.status = NL_CAP_FAIL_NO_FRAMES;
            } else if (g_cap_diag_local.frames_rejected_esc > 0U &&
                       g_cap_diag_local.frames_rejected_blank == 0U) {
                g_cap_diag_local.status = NL_CAP_FAIL_ESC_HELD;
            } else {
                g_cap_diag_local.status = NL_CAP_FAIL_BLANK_OR_BLACK;
            }
        }
    }

    g_state->cap_diag = g_cap_diag_local;
    if (allow_capture) {
        if (is_batch_step) {
            nl_write_internal_cap_log(&g_cap_diag_local, g_state->covers_dir);
        } else if (!captured_now) {
            /* #R23 Only queue diagnostic log Flash write when a capture actually failed */
            g_pending_cap_diag = g_cap_diag_local;
            g_pending_cap_log_save = true;
        }
    }

    if (rc != 0xDEAD && !is_batch_step) {
        nl_record_play_session(g_state, launched_idx, session_start_sec, session_end_sec);
        /* #R23 Skip redundant O(N log N) library re-sort when active category & sort order
         * do NOT depend on play recency/count/time -> keeps G_LIB_STATIC_FB 100% valid! */
        if (nl_return_needs_filter_rebuild(g_state)) {
            nl_rebuild_filter(g_state);
        }
        g_pending_cfg_save = true;
        g_pending_prof_save = true;
    }
    g_state->launch_game_idx = -1;
    g_state->sui.config_dirty = false;
    g_state->anim.launch_anim_active = false;

    if (is_batch_step) {
        restore_documents_working_dir(docs_root);
        nl_batch_capture_advance(g_state, captured_now && saved_ok, !allow_capture, g_cap_diag_local.status);
        if (nl_any_hardware_input_active()) {
            nl_wait_all_keys_released();
        }
        return true;
    }

    if (rc == 0xDEAD) {
        nl_diag_report(
            g_state,
            NL_DIAG_E201_GAME_MALFORMED,
            target->title,
            target->path,
            "Lancement programme",
            "Execution echouee (0xDEAD)",
            "Programme non lance",
            "Retour sur N-Launcher"
        );
        nl_show_toast(g_state, "ERREUR LANCEMENT", 85);
    } else if (captured_now && saved_ok) {
        nl_show_toast(g_state, "CAPTURE ENREGISTREE !", 85);
    }

    /* #R23 Start immediate non-blocking Return Animation & blit Frame 1 BEFORE chdir/Nucleus cleanup!
     * ZERO synchronous Flash writes, ZERO icon reloads, ZERO blocking key-release waits! */
    nl_return_drop_start(g_state, launched_idx);
    nl_render_frame(g_state, g_framebuffer);
    lcd_blit(g_framebuffer, SCR_320x240_565);

    uint32_t ret_us = nl_ticks_to_us(nl_hw_timer_ticks() - ret_t0);
    g_state->perf.return_restore_us = ret_us;
    g_state->perf.return_count++;

    /* Restore working directory AFTER Frame 1 is already live on screen */
    restore_documents_working_dir(docs_root);

    /* Non-blocking post-return ESC/ENTER release latch:
     * Prevents a still-held game-exit ESC key from quitting N-Launcher while keeping ALL
     * directional, touchpad, and mouse inputs 100% live from Frame 1! */
    g_ignore_esc_until_release = true;
    g_enter_consumed_until_release = true;
    g_launch_btn_was_down = false;
    g_mouse_enter_was_down = false;
    nl_touchpad_tracker_reset(&g_tp_tracker);
    nl_tp_click_reset(&g_tp_click_guard);
    nl_save_shortcut_reset(&g_save_sc);
    return true;
}

typedef struct {
    char docs_root[192];
    char cfg_path[NL_MAX_PATH_LEN];
    char fallback_cfg_path[NL_MAX_PATH_LEN];
    char cache_path[NL_MAX_PATH_LEN];
    char ndless_root_dir[NL_MAX_PATH_LEN];
} nl_boot_paths_t;

int main(int argc, char *argv[]) {
    nl_disable_watchdog_safe();

    g_state = (nl_state_t *)calloc(1, sizeof(nl_state_t));
    g_framebuffer = (uint16_t *)malloc(NL_FB_BYTES);
    nl_boot_paths_t *paths = (nl_boot_paths_t *)calloc(1, sizeof(nl_boot_paths_t));
    if (!g_state || !g_framebuffer || !paths) {
        free(paths);
        free(g_state);
        free(g_framebuffer);
        g_state = NULL;
        g_framebuffer = NULL;
        return 1;
    }

    const char *self_path = (argc > 0 && argv && argv[0]) ? argv[0] : "N-Launcher.tns";
    if (argc > 0 && argv) {
        enable_relative_paths(argv);
    }

    const char *docs_dir_ptr = get_documents_dir();
    char *docs_root = paths->docs_root;
    char *cfg_path = paths->cfg_path;
    char *fallback_cfg_path = paths->fallback_cfg_path;
    char *cache_path = paths->cache_path;
    char *ndless_root_dir = paths->ndless_root_dir;

    snprintf(docs_root, sizeof(paths->docs_root), "%s",
             (docs_dir_ptr && docs_dir_ptr[0] != '\0') ? docs_dir_ptr : "/documents");
    strip_trailing_slash(docs_root);

    build_cfg_paths(docs_root, self_path, cfg_path, sizeof(paths->cfg_path),
                    fallback_cfg_path, sizeof(paths->fallback_cfg_path));
    snprintf(cache_path, sizeof(paths->cache_path), "%.*s/ndless/nlauncher.cache.tns",
             (int)(sizeof(paths->cache_path) - 32U), docs_root);

    /* Mark N-Launcher / child game session active in both Flash lock & RAM control block */
    nl_bg_set_active_lock(docs_root, true);
    nl_hotkey_set_ram_busy(true);

    /* Permanently purge any legacy autostart stub/guard files from /documents/ndless/startup */
    nl_cleanup_legacy_autostart(docs_root);

    if (!lcd_init(SCR_320x240_565)) {
        nl_bg_set_active_lock(docs_root, false);
        nl_hotkey_set_ram_busy(false);
        free(paths);
        free(g_framebuffer);
        free(g_state);
        g_framebuffer = NULL;
        g_state = NULL;
        return 1;
    }

    bool early_abort = false;
    uint16_t *lib_fb = NULL;

    /* 1. Initialize diagnostics & SP804 32,768 Hz hardware frame-pacing timer */
    nl_init_state(g_state);
    nl_hw_timer_init();
    g_state->perf.hw_timer_active = g_hw_timer_ok;
    nl_diag_init(g_state, docs_root);
    snprintf(g_state->profiles_path, sizeof(g_state->profiles_path), "%.*s/ndless/nlauncher.profiles.tns",
             (int)(sizeof(g_state->profiles_path) - 34U), docs_root);
    snprintf(g_state->lib_path, sizeof(g_state->lib_path), "%.*s/ndless/nlauncher.lib.tns",
             (int)(sizeof(g_state->lib_path) - 30U), docs_root);
    populate_sys_env(g_state, docs_root, self_path);
    nl_diag_read_prev_stage(g_state);

    /* Record Stage 1 to Flash; track intermediate stages in RAM to avoid NAND Flash churn at boot */
    nl_diag_record_stage(g_state, NL_STAGE_01_ENTRY, false);
    nl_diag_mark_stage_ram(g_state, NL_STAGE_02_NDLESS);
    nl_diag_mark_stage_ram(g_state, NL_STAGE_03_MEMORY);

    snprintf(ndless_root_dir, sizeof(paths->ndless_root_dir), "%.*s/ndless",
             (int)(sizeof(paths->ndless_root_dir) - 8U), docs_root);
    snprintf(g_state->covers_dir, sizeof(g_state->covers_dir), "%s/ndless/covers", docs_root);
    snprintf(g_state->icons_dir, sizeof(g_state->icons_dir), "%s/ndless/icons", docs_root);
    ensure_required_dir(g_state, ndless_root_dir);
    ensure_required_dir(g_state, g_state->covers_dir);
    ensure_required_dir(g_state, g_state->icons_dir);
    nl_diag_mark_stage_ram(g_state, NL_STAGE_04_DIRS);

    /* Load saved preferences & secondary game_folder= list */
    nl_folder_seed_defaults(&g_state->folders, docs_root);
    load_launcher_config(g_state, cfg_path, fallback_cfg_path);
    nl_diag_mark_stage_ram(g_state, NL_STAGE_05_CONFIG);

    bool had_profiles_boot = nl_file_exists(g_state->profiles_path);
    if (!nl_load_profiles(g_state, g_state->profiles_path)) {
        nl_profiles_init_default(g_state);
        if (!had_profiles_boot) {
            (void)nl_save_profiles(g_state, g_state->profiles_path);
        }
    }
    nl_diag_mark_stage_ram(g_state, NL_STAGE_06_PROFILES);

    if (nl_startup_abort_key_pressed()) {
        early_abort = true;
        goto nl_clean_shutdown;
    }

    /* 2. Draw frame 0 of N-Launcher startup animation in saved mode & accent immediately */
    uint32_t boot_t0 = nl_hw_timer_ticks();
    nl_render_splash_anim(g_state, g_framebuffer, 0U, true);
    lcd_blit(g_framebuffer, SCR_320x240_565);

    /* 3. INSTANT 60 FPS WARM BOOT vs FIRST-TIME COLD SCAN:
     *    - If nlauncher.lib.tns (or legacy #R11 cache) already contains games, load them
     *      instantaneously without running a heavy blocking full-disk scan during splash!
     *    - Only run full transactional scan automatically on first-ever launch (0 games)
     *      or when the user manually triggers "Rescanner les jeux" / DEL. */
    nl_rescan_pump_ctx_t boot_ctx = { g_state, g_framebuffer, boot_t0, boot_t0, 0U, 0U, true };

    bool lib_loaded = nl_load_library_index(g_state, g_state->lib_path);
    if (!lib_loaded) {
        if (nl_load_library_cache(g_state, cache_path) && g_state->total_games > 0) {
            g_state->sys.cache_loaded_at_boot = true;
            /* Drop legacy cached icon blobs and migrate immediately to icon-independent index */
            nl_free_game_icons_only(g_state);
            (void)nl_save_library_index(g_state, g_state->lib_path);
        } else {
            g_state->sys.cache_loaded_at_boot = false;
        }
    } else {
        g_state->sys.cache_loaded_at_boot = true;
    }

    bool need_save_icon_cache = false;
    nl_diag_mark_stage_ram(g_state, NL_STAGE_07_ICONS);
    if (g_state->total_games > 0) {
        /* Warm boot: re-attach saved favorites/stats/shortcuts & active profile, then hydrate pre-decoded icons */
        load_launcher_config(g_state, cfg_path, fallback_cfg_path);
        if (g_state->profiles.initialized) {
            nl_profile_apply_active(g_state);
        }
        g_state->scan.abort_requested = false;

        /* #R20 Startup 60 FPS Optimization:
         *  1. Hydrate pre-decoded RGB565+alpha icons directly from nlauncher.cache.tns (zero PNG decompression!).
         *  2. Register intra-PNG progress callback so any newly added 144x144 PNG still pumps the splash at 60 FPS.
         *  3. Pump splash during .scr.tns probing too, and defer cache save until AFTER the splash finishes. */
        int hydrated_ico = nl_hydrate_icons_from_cache(g_state, cache_path, rescan_icon_progress_cb, &boot_ctx);
        if (g_state->scan.abort_requested || nl_startup_abort_key_pressed()) {
            early_abort = true;
            goto nl_clean_shutdown;
        }
        nl_set_icon_decode_progress_cb(rescan_icon_progress_cb, &boot_ctx);
        nl_load_all_icons_ex(g_state, rescan_icon_progress_cb, &boot_ctx);
        nl_set_icon_decode_progress_cb(NULL, NULL);
        if (g_state->scan.abort_requested || nl_startup_abort_key_pressed()) {
            early_abort = true;
            goto nl_clean_shutdown;
        }
        int loaded_ico = 0, fallback_ico = 0;
        for (int i = 0; i < g_state->total_games; i++) {
            if (g_state->games[i].icon_source != NL_ICON_NONE && g_state->games[i].icon_pixels != NULL) {
                loaded_ico++;
            } else {
                fallback_ico++;
            }
        }
        g_state->scan.icons_loaded = loaded_ico;
        g_state->scan.icons_fallback = fallback_ico;
        g_state->scan.games_found = g_state->total_games;
        g_state->scan.finished = true;
        if (loaded_ico != hydrated_ico) {
            need_save_icon_cache = true;
        }
        /* R15/R20: Probe valid on-disk .scr.tns captures with 60 FPS splash pumping */
        nl_probe_all_screenshots_ex(g_state, rescan_icon_progress_cb, &boot_ctx);
        nl_rebuild_filter(g_state);
    } else {
        /* Cold first-ever boot (no saved index yet): safe two-phase scan at real 60 FPS */
        (void)run_safe_rescan(docs_root, self_path, cfg_path, fallback_cfg_path,
                              cache_path, true, &boot_ctx);
        if (g_state->scan.abort_requested || nl_startup_abort_key_pressed()) {
            early_abort = true;
            goto nl_clean_shutdown;
        }
    }
    nl_diag_mark_stage_ram(g_state, NL_STAGE_08_GAMES);

    if (g_state->diag.count > 0) {
        char warn_msg[48];
        snprintf(warn_msg, sizeof(warn_msg), "BIB. CHARGEE (%d AVERT.)", g_state->diag.count);
        nl_show_toast(g_state, warn_msg, 110);
    }

    /* #R23 Honor independent App Open Animation (app_open_anim) without artificial wait delays */
    nl_diag_mark_stage_ram(g_state, NL_STAGE_09_GFX);
    nl_app_open_anim_t open_anim = g_state->settings.app_open_anim;
    uint32_t final_boot_ms = boot_ctx.last_frame_ms;
    uint32_t boot_deadline = nl_hw_timer_ticks();
    if (open_anim == NL_OPEN_ANIM_CURRENT) {
        while (final_boot_ms < 360U) {
            if (nl_startup_abort_key_pressed()) {
                early_abort = true;
                goto nl_clean_shutdown;
            }
            final_boot_ms += 17U;
            uint32_t bt0 = nl_hw_timer_ticks();
            nl_render_splash_anim(g_state, g_framebuffer, final_boot_ms, true);
            lcd_blit(g_framebuffer, SCR_320x240_565);
            uint32_t brus = nl_ticks_to_us(nl_hw_timer_ticks() - bt0);
            if (brus > g_state->perf.open_render_us) g_state->perf.open_render_us = brus;
            boot_deadline += NL_FRAME_TARGET_TICKS;
            nl_hw_timer_pace_deadline(boot_deadline);
        }
    }

    /* Step initial animation state so layout and selection scales are fully settled */
    (void)nl_anim_step(g_state);

    /* 4. Loading finished -> honor independent app_open_anim transition into Library */
    nl_diag_mark_stage_ram(g_state, NL_STAGE_10_SPLASH);
    if (open_anim == NL_OPEN_ANIM_CURRENT) {
        nl_render_splash_anim(g_state, g_framebuffer, final_boot_ms, false);
        lcd_blit(g_framebuffer, SCR_320x240_565);
    }

    nl_diag_mark_stage_ram(g_state, NL_STAGE_11_LIBRARY);
    if (open_anim == NL_OPEN_ANIM_INSTANT) {
        nl_render_frame(g_state, g_framebuffer);
        lcd_blit(g_framebuffer, SCR_320x240_565);
    } else {
        lib_fb = (uint16_t *)malloc(NL_FB_BYTES);
        if (!lib_fb) {
            nl_recover_memory_on_oom(g_state, "Transition bibliotheque", "lib_fb");
            lib_fb = (uint16_t *)malloc(NL_FB_BYTES);
        }
        if (lib_fb) {
            int trans_steps = (open_anim == NL_OPEN_ANIM_LIQUID) ? 12
                            : (open_anim == NL_OPEN_ANIM_ZOOM) ? 10 : 6;
            nl_render_frame(g_state, lib_fb);
            boot_deadline = nl_hw_timer_ticks();
            for (int tr = 1; tr <= trans_steps; tr++) {
                if (nl_startup_abort_key_pressed()) {
                    early_abort = true;
                    goto nl_clean_shutdown;
                }
                uint32_t bt0 = nl_hw_timer_ticks();
                nl_render_boot_transition(g_state, g_framebuffer, g_framebuffer, lib_fb, tr, trans_steps);
                lcd_blit(g_framebuffer, SCR_320x240_565);
                uint32_t brus = nl_ticks_to_us(nl_hw_timer_ticks() - bt0);
                if (brus > g_state->perf.open_render_us) g_state->perf.open_render_us = brus;
                boot_deadline += NL_FRAME_TARGET_TICKS;
                nl_hw_timer_pace_deadline(boot_deadline);
            }
            memcpy(g_framebuffer, lib_fb, NL_FB_BYTES);
            lcd_blit(g_framebuffer, SCR_320x240_565);
            free(lib_fb);
            lib_fb = NULL;
        } else {
            nl_render_frame(g_state, g_framebuffer);
            lcd_blit(g_framebuffer, SCR_320x240_565);
        }
    }
    g_state->perf.open_total_ms = nl_ticks_to_us(nl_hw_timer_ticks() - boot_t0) / 1000U;
    if (nl_startup_abort_key_pressed()) {
        early_abort = true;
        goto nl_clean_shutdown;
    }
    /* #R23 Zero-stall boot handoff: only wait for key release if a key is currently down before hotkey reconcile,
     * defer icon cache save until idle, and skip redundant post-crossfade redraw. */
    if (nl_any_hardware_input_active()) {
        nl_wait_all_keys_released();
    }
    if (need_save_icon_cache && cache_path[0] != '\0' && g_state->total_games > 0) {
        g_pending_cache_save = true;
    }
    nl_hotkey_reconcile(g_state);
    nl_diag_mark_stage_ram(g_state, NL_STAGE_12_READY);
    g_ignore_esc_until_release = false;

    nl_key_t last_key = NL_KEY_NONE;
    char     last_char = 0;
    unsigned hold_ticks = 0;
    unsigned idle_frames = 0;
    g_pending_cfg_save = false;
    g_pending_prof_save = false;
    g_pending_lib_save = false;
    bool     rescan_opened_from_library = false;
    uint32_t prev_loop_ticks = nl_hw_timer_ticks();
    uint32_t frame_deadline = prev_loop_ticks;

    while (!g_state->should_exit) {
        uint32_t loop_start_ticks = nl_hw_timer_ticks();
        uint32_t frame_dt_us = nl_ticks_to_us(loop_start_ticks - prev_loop_ticks);
        prev_loop_ticks = loop_start_ticks;
        bool need_redraw = false;
        bool ctrl_changed = false;
        bool was_icon_modal_pre = (g_state->sui.screen == NL_UI_SETTINGS_ICONS &&
                                   g_state->sui.icon_confirm_modal &&
                                   g_state->sui.icon_confirm_yes);
        nl_key_t key = poll_calculator_key(g_state, &ctrl_changed);
        char     alpha_ch = (key == NL_KEY_NONE && !g_mouse_action_fired) ? poll_alpha_key(g_state) : 0;

        if (ctrl_changed || g_mouse_moved) {
            need_redraw = true;
        }

        bool trigger = false;
        if (key != NL_KEY_NONE) {
            if (g_tp_step_emitted || key != last_key) {
                trigger = true;
                hold_ticks = 0;
            } else {
                hold_ticks++;
                if ((key == NL_KEY_LEFT || key == NL_KEY_RIGHT ||
                     key == NL_KEY_UP   || key == NL_KEY_DOWN) &&
                    hold_ticks >= 9U && (hold_ticks % 3U == 0U)) {
                    trigger = true;
                }
            }
        } else {
            hold_ticks = 0;
        }
        last_key = key;

        bool char_trigger = (alpha_ch != 0 && alpha_ch != last_char);
        last_char = alpha_ch;

        if (trigger || char_trigger || g_mouse_action_fired) {
            if (trigger && key == NL_KEY_RESCAN && g_state->sui.screen == NL_UI_LIBRARY) {
                if (g_active_scan_job == NULL) {
                    rescan_opened_from_library = true;
                    (void)start_interactive_rescan(docs_root, self_path, cfg_path, fallback_cfg_path, true);
                    nl_mouse_update_hover(g_state);
                    prev_loop_ticks = nl_hw_timer_ticks();
                    frame_deadline = prev_loop_ticks;
                    need_redraw = true;
                }
            } else {
                if (trigger && (key == NL_KEY_DOC || key == NL_KEY_MENU) && g_framebuffer) {
                    if (!g_ink_old_fb) g_ink_old_fb = (uint16_t *)malloc(NL_FB_BYTES);
                    if (!g_ink_new_fb) g_ink_new_fb = (uint16_t *)malloc(NL_FB_BYTES);
                    if ((!g_ink_old_fb || !g_ink_new_fb) && g_state->sys.oom_recoveries == 0U) {
                        nl_recover_memory_on_oom(g_state, "Transition parametres", "ink_fb");
                        if (!g_ink_old_fb) g_ink_old_fb = (uint16_t *)malloc(NL_FB_BYTES);
                        if (!g_ink_new_fb) g_ink_new_fb = (uint16_t *)malloc(NL_FB_BYTES);
                    }
                    if (g_ink_old_fb) {
                        memcpy(g_ink_old_fb, g_framebuffer, NL_FB_BYTES);
                    }
                }

                bool was_icon_modal = was_icon_modal_pre ||
                                      (g_state->sui.screen == NL_UI_SETTINGS_ICONS &&
                                       g_state->sui.icon_confirm_modal &&
                                       g_state->sui.icon_confirm_yes);

                bool handled = g_mouse_action_fired
                    ? true
                    : (trigger
                        ? nl_handle_key(g_state, key)
                        : nl_handle_char_input(g_state, alpha_ch));

                if (handled) {
                    need_redraw = true;
                    if (was_icon_modal && !g_state->sui.icon_confirm_modal) {
                        /* Custom icon was deleted -> games remain intact; refresh index & optional cache */
                        if (g_state->lib_path[0] != '\0') {
                            (void)nl_save_library_index(g_state, g_state->lib_path);
                        }
                        if (g_state->total_games > 0) {
                            (void)nl_save_library_cache(g_state, cache_path);
                        }
                    }
                    if (g_state->scan.commit_empty_req) {
                        g_state->scan.commit_empty_req = false;
                        if (g_state->lib_path[0] != '\0') {
                            (void)nl_save_library_index(g_state, g_state->lib_path);
                        }
                        save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                        (void)remove(cache_path);
                    }
                    if (g_state->sui.rescan_req) {
                        g_state->sui.rescan_req = false;
                        if (g_active_scan_job == NULL) {
                            rescan_opened_from_library = false;
                            (void)start_interactive_rescan(docs_root, self_path, cfg_path, fallback_cfg_path, false);
                            nl_mouse_update_hover(g_state);
                            prev_loop_ticks = nl_hw_timer_ticks();
                            frame_deadline = prev_loop_ticks;
                            need_redraw = true;
                        }
                    }
                    if (g_state->sui.poweroff_req) {
                        g_state->sui.poweroff_req = false;
                        save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                        (void)nl_save_profiles(g_state, g_state->profiles_path);
                        if (g_framebuffer) {
                            nl_render_frame(g_state, g_framebuffer);
                            lcd_blit(g_framebuffer, SCR_320x240_565);
                        }
                        nl_safe_delay_ms(220);
                        nl_wait_all_keys_released();
                        g_state->should_exit = true;
                    }
                    if (g_state->sui.profiles_dirty) {
                        g_state->sui.profiles_dirty = false;
                        g_pending_prof_save = true;
                    }
                    if (g_state->sui.config_dirty || (trigger && (key == NL_KEY_DOC || key == NL_KEY_MENU))) {
                        g_state->sui.config_dirty = false;
                        g_pending_cfg_save = true;
                        g_pending_prof_save = true;
                    }

                    if (g_state->launch_requested &&
                        g_state->launch_game_idx >= 0 &&
                        g_state->launch_game_idx < g_state->total_games) {
                        int launched_idx = g_state->launch_game_idx;
                        g_state->launch_requested = false;
                        if (!execute_game_launch_and_capture(launched_idx, false, docs_root,
                                                             cfg_path, fallback_cfg_path, cache_path)) {
                            break;
                        }
                        last_key = NL_KEY_NONE;
                        last_char = 0;
                        hold_ticks = 0;
                        prev_loop_ticks = nl_hw_timer_ticks();
                        frame_deadline = prev_loop_ticks;
                        need_redraw = false; /* Frame 1 already rendered & blitted immediately on return */
                    }
                }
            }
        }

        /* Non-blocking incremental rescan step: runs a tiny batch per frame so cursor,
         * CTRL mode toggle, ESC and the clickable "Annuler" button stay live at 60 FPS. */
        if (g_active_scan_job != NULL) {
            if (g_state->scan.cancel_requested || g_state->should_exit) {
                nl_scan_job_cancel(g_active_scan_job, g_state);
                nl_scan_job_destroy(g_active_scan_job);
                g_active_scan_job = NULL;
                if (!g_state->should_exit) {
                    nl_ui_screen_t dest = rescan_opened_from_library ? NL_UI_LIBRARY : NL_UI_SETTINGS_MAIN;
                    g_state->sui.screen = dest;
                    g_state->anim.set_last_screen = dest;
                    g_state->anim.set_open_fp = (dest == NL_UI_LIBRARY) ? 0 : 256;
                    g_state->anim.set_sec_fp = 256;
                    nl_show_toast(g_state, "SCAN ANNULE - BIB. CONSERVEE", 70);
                    nl_mouse_update_hover(g_state);
                    need_redraw = true;
                }
                prev_loop_ticks = nl_hw_timer_ticks();
                frame_deadline = prev_loop_ticks;
            } else {
                bool still_running = nl_scan_job_step(g_active_scan_job, g_state, 3);
                need_redraw = true;
                if (!still_running) {
                    bool was_cancelled = nl_scan_job_was_cancelled(g_active_scan_job);
                    nl_scan_job_destroy(g_active_scan_job);
                    g_active_scan_job = NULL;
                    if (!was_cancelled && !g_state->scan.empty_confirm_pending) {
                        nl_probe_all_screenshots(g_state);
                        if (g_state->lib_path[0] != '\0') {
                            (void)nl_save_library_index(g_state, g_state->lib_path);
                        }
                        save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                        if (g_state->profiles.initialized) {
                            (void)nl_save_profiles(g_state, g_state->profiles_path);
                        }
                        if (cache_path[0] != '\0' && g_state->total_games > 0) {
                            if (nl_save_library_cache(g_state, cache_path)) {
                                g_state->sys.cache_valid = true;
                            }
                        }
                        nl_show_toast(g_state, "BIBLIOTHEQUE ACTUALISEE", 60);
                    }
                    nl_mouse_update_hover(g_state);
                    prev_loop_ticks = nl_hw_timer_ticks();
                    frame_deadline = prev_loop_ticks;
                }
            }
        }

        /* Automated Batch Capture step execution on hardware */
        if (!g_state->should_exit &&
            g_state->sui.screen == NL_UI_SETTINGS_CAPTURE_BATCH &&
            g_state->sui.cap_active &&
            g_state->sui.cap_step_req &&
            !g_state->sui.cap_semi_auto) {
            g_state->sui.cap_step_req = false;
            int pos = g_state->sui.cap_pos;
            if (pos >= 0 && pos < g_state->sui.cap_total) {
                int batch_game_idx = g_state->sui.cap_queue[pos];
                if (!execute_game_launch_and_capture(batch_game_idx, true, docs_root,
                                                     cfg_path, fallback_cfg_path, cache_path)) {
                    break;
                }
                last_key = NL_KEY_NONE;
                last_char = 0;
                hold_ticks = 0;
                prev_loop_ticks = nl_hw_timer_ticks();
                frame_deadline = prev_loop_ticks;
                need_redraw = true;
            } else {
                g_state->sui.cap_active = false;
                need_redraw = true;
            }
        }

        bool is_animating = false;
        if (!g_state->should_exit) {
            is_animating = nl_anim_step(g_state);
            if (is_animating) need_redraw = true;
        }
        if (!g_state->anim.ink_active) {
            g_ink_rendered_seq = 0xFFFFFFFFU;
        }

        uint32_t render_us = 0U;
        if (!g_state->should_exit && need_redraw && g_framebuffer) {
            uint32_t rt0 = nl_hw_timer_ticks();
            if (g_state->anim.ink_active && g_ink_old_fb && g_ink_new_fb) {
                /* Render destination frame ONCE per ink transition sequence (eliminates redundant full-screen redraw!) */
                if (g_ink_rendered_seq != g_state->anim.ink_seq) {
                    nl_render_frame(g_state, g_ink_new_fb);
                    g_ink_rendered_seq = g_state->anim.ink_seq;
                }
                nl_render_ink_transition(
                    g_framebuffer,
                    g_ink_old_fb,
                    g_ink_new_fb,
                    g_state->anim.ink_seq,
                    g_state->anim.ink_tick,
                    g_state->anim.ink_max
                );
            } else {
                nl_render_frame(g_state, g_framebuffer);
            }
            lcd_blit(g_framebuffer, SCR_320x240_565);
            render_us = nl_ticks_to_us(nl_hw_timer_ticks() - rt0);
        }

        /* #R23 Idle-gated staggered NAND Flash persistence:
         * NEVER freeze the UI right after returning from a game or toggling settings!
         * Wait for >= 150 idle frames (~2.5s) with zero user input and zero active animations,
         * then write AT MOST ONE pending file per idle window. */
        bool user_active_now = (key != NL_KEY_NONE || alpha_ch != 0 || ctrl_changed ||
                                g_mouse_moved || g_mouse_action_fired ||
                                is_animating || g_state->anim.ink_active ||
                                g_state->anim.return_drop_active || g_active_scan_job != NULL);
        if (user_active_now) {
            idle_frames = 0;
        } else if (idle_frames < 1000U) {
            idle_frames++;
        }

        if (!g_state->should_exit && idle_frames >= 150U &&
            (g_pending_cfg_save || g_pending_prof_save || g_pending_lib_save ||
             g_pending_cache_save || g_pending_scr_save_idx >= 0 || g_pending_cap_log_save)) {
            if (nl_flush_one_deferred_save(cfg_path, fallback_cfg_path, cache_path)) {
                /* Space out remaining pending files by ~1.0s so UI stays responsive */
                idle_frames = 90U;
                prev_loop_ticks = nl_hw_timer_ticks();
                frame_deadline = prev_loop_ticks;
            }
        } else if (!g_state->should_exit && need_redraw) {
            nl_perf_record_frame(g_state, frame_dt_us, render_us);
        }

        if (!g_state->should_exit) {
            frame_deadline += NL_FRAME_TARGET_TICKS;
            uint32_t now_t = nl_hw_timer_ticks();
            if ((int32_t)(frame_deadline - now_t) < -(int32_t)(NL_FRAME_TARGET_TICKS * 2U)) {
                /* If we fell far behind (e.g., rescan), re-anchor cleanly to avoid catch-up burst */
                frame_deadline = now_t + NL_FRAME_TARGET_TICKS;
            }
            nl_hw_timer_pace_deadline(frame_deadline);
        }
    }

nl_clean_shutdown:
    /* Clean, hang-free, fast return to TI-Nspire OS:
     * Cancel any in-flight scan job safely before shutdown so partial state is never written. */
    if (g_active_scan_job != NULL) {
        nl_scan_job_cancel(g_active_scan_job, g_state);
        nl_scan_job_destroy(g_active_scan_job);
        g_active_scan_job = NULL;
    }
    nl_disable_watchdog_safe();
    uint32_t close_t0 = nl_hw_timer_ticks();

    /* #R23 Fast independent App Close Animation (max ~166-200ms, 0 frames for Instant or early_abort) */
    if (!early_abort && g_framebuffer && g_state &&
        g_state->settings.app_close_anim != NL_CLOSE_ANIM_INSTANT) {
        nl_app_close_anim_t cmode = g_state->settings.app_close_anim;
        int close_steps = (cmode == NL_CLOSE_ANIM_LIQUID) ? 12 : 10;
        uint16_t *close_base = (uint16_t *)malloc(NL_FB_BYTES);
        if (close_base) {
            memcpy(close_base, g_framebuffer, NL_FB_BYTES);
        }
        uint32_t close_dl = nl_hw_timer_ticks();
        for (int cs = 1; cs <= close_steps; cs++) {
            nl_render_app_close_anim(g_state, g_framebuffer, close_base ? close_base : g_framebuffer, cs, close_steps);
            lcd_blit(g_framebuffer, SCR_320x240_565);
            close_dl += NL_FRAME_TARGET_TICKS;
            nl_hw_timer_pace_deadline(close_dl);
        }
        free(close_base);
    }

    /* #R23 Dirty-only shutdown persistence: ONLY write files that actually changed!
     * Eliminates 3-6 seconds of redundant Nucleus FATFS writes on every exit. */
    if (!early_abort) {
        nl_flush_pending_screenshot_only();
        if (g_pending_cache_save && cache_path[0] != '\0' && g_state->total_games > 0) {
            (void)nl_save_library_cache(g_state, cache_path);
            g_pending_cache_save = false;
        }
        nl_diag_record_stage(g_state, NL_STAGE_12_READY, true);
        if (g_pending_lib_save && g_state->lib_path[0] != '\0') {
            (void)nl_save_library_index(g_state, g_state->lib_path);
            g_pending_lib_save = false;
        }
        bool had_session_writes = (g_pending_cfg_save || g_pending_prof_save || g_state->perf.return_count > 0U);
        if (g_pending_cfg_save) {
            save_launcher_config(g_state, cfg_path, fallback_cfg_path);
            g_pending_cfg_save = false;
        }
        if (g_pending_prof_save) {
            (void)nl_save_profiles(g_state, g_state->profiles_path);
            g_pending_prof_save = false;
        }
        g_state->perf.close_total_ms = nl_ticks_to_us(nl_hw_timer_ticks() - close_t0) / 1000U;
        if (had_session_writes) {
            nl_save_perf_report(g_state, docs_root);
        }
    } else {
        nl_diag_record_stage(g_state, g_state->diag.current_stage, true);
    }
    nl_bg_set_active_lock(docs_root, false);
    nl_hotkey_set_ram_busy(false);
    restore_documents_working_dir(docs_root);
    nl_hw_timer_restore();
    lcd_init(SCR_TYPE_INVALID);

    free(lib_fb);
    free(g_ink_old_fb); g_ink_old_fb = NULL;
    free(g_ink_new_fb); g_ink_new_fb = NULL;
    nl_renderer_cleanup();
    free(g_framebuffer); g_framebuffer = NULL;
    nl_free_icons(g_state);
    free(g_state); g_state = NULL;
    free(paths);

    /* Wait until ESC/ON/all keys are fully released via pure CPU polling (never WFI)
     * so Ndless ploaderhook returns cleanly to TI-OS 6.2.0 without hanging. */
    nl_wait_exit_safe_release();
    return 0;
}
