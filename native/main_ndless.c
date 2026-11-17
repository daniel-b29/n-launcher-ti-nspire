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

static bool nl_other_matrix_key_active(void) {
    return isKeyPressed(KEY_NSPIRE_ESC) ||
           isKeyPressed(KEY_NSPIRE_ENTER) ||
           isKeyPressed(KEY_NSPIRE_TAB) ||
           isKeyPressed(KEY_NSPIRE_MENU) ||
           isKeyPressed(KEY_NSPIRE_DOC) ||
           isKeyPressed(KEY_NSPIRE_VAR) ||
           isKeyPressed(KEY_NSPIRE_DEL);
}

static void nl_wait_all_keys_released(void) {
    unsigned clean_streak = 0;
    for (unsigned iter = 0; iter < 300U; iter++) {
        if (!nl_any_hardware_input_active()) {
            clean_streak++;
            if (clean_streak >= 4U) {
                nl_touchpad_tracker_reset(&g_tp_tracker);
                nl_tp_click_reset(&g_tp_click_guard);
                nl_save_shortcut_reset(&g_save_sc);
                return;
            }
        } else {
            clean_streak = 0;
        }
        nl_safe_delay_ms(12);
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

    if (isKeyPressed(KEY_NSPIRE_ESC) || on_key_pressed()) {
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

    /* If hovering a specific UI button/pill/arrow/badge in Mouse Mode, ENTER/5 clicks that hovered control */
    if (mouse_active && !typing_mode && (enter_down || key5_down)) {
        if (state->sui.hover_valid &&
            state->sui.hover_kind != NL_HOVER_NONE &&
            state->sui.hover_kind != NL_HOVER_GAME_CARD &&
            state->sui.hover_kind != NL_HOVER_QP_OUTSIDE) {
            if (!g_mouse_enter_was_down) {
                g_mouse_enter_was_down = true;
                nl_save_shortcut_clear_latch(&g_save_sc);
                if (nl_mouse_click(state)) {
                    g_mouse_action_fired = true;
                }
            }
            return NL_KEY_NONE;
        }
    } else {
        g_mouse_enter_was_down = false;
    }

    /* On the Library screen, pressing ENTER or 5 shrinks the selected icon while held
     * and triggers NL_KEY_EXE upon release so the luminous rings & screenshot fade play smoothly */
    if (state != NULL && state->sui.screen == NL_UI_LIBRARY && state->filtered_count > 0 && !state->sui.qp_open) {
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
        if (enter_down || key5_down) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return NL_KEY_EXE;
        }
    }

    touchpad_report_t tp;
    if (touchpad_scan(&tp) == 0) {
        const touchpad_info_t *tpi = touchpad_getinfo();
        uint16_t pw = (tpi && tpi->width > 0U) ? tpi->width : 2328U;
        uint16_t ph = (tpi && tpi->height > 0U) ? tpi->height : 1691U;

        if (mouse_active) {
            /* Mouse Mode (Mode 2 or temporary CTRL): free pointer movement + universal click */
            if (nl_tp_click_poll(
                    &g_tp_click_guard,
                    tp.contact != 0,
                    tp.pressed != 0,
                    (tpad_arrow_t)tp.arrow == TPAD_ARROW_CLICK,
                    tp.x, tp.y, pw, ph)) {
                nl_save_shortcut_clear_latch(&g_save_sc);
                nl_touchpad_tracker_reset(&g_tp_tracker);
                if (nl_mouse_click(state)) {
                    g_mouse_action_fired = true;
                }
                return NL_KEY_NONE;
            }
            nl_touchpad_tracker_reset(&g_tp_tracker);
            if (nl_mouse_feed_touchpad(state, tp.contact != 0, tp.x, tp.y, pw, ph)) {
                nl_save_shortcut_clear_latch(&g_save_sc);
                g_mouse_moved = true;
            }
        } else {
            if (state != NULL && state->sui.screen == NL_UI_LIBRARY) {
                if (nl_tp_click_poll(
                        &g_tp_click_guard,
                        tp.contact != 0,
                        tp.pressed != 0,
                        (tpad_arrow_t)tp.arrow == TPAD_ARROW_CLICK,
                        tp.x, tp.y, pw, ph)) {
                    nl_save_shortcut_clear_latch(&g_save_sc);
                    nl_touchpad_tracker_reset(&g_tp_tracker);
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

        if (tp.pressed && (tpad_arrow_t)tp.arrow != TPAD_ARROW_CLICK) {
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
        nl_touchpad_tracker_reset(&g_tp_tracker);
        nl_tp_click_reset(&g_tp_click_guard);
        if (mouse_active) {
            state->anim.mouse_last_contact = false;
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
        if (isKeyPressed(KEY_NSPIRE_0)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return NL_KEY_PROFILE_PREV;
        }
        if (isKeyPressed(KEY_NSPIRE_MINUS)) {
            nl_save_shortcut_clear_latch(&g_save_sc);
            return NL_KEY_PROFILE_NEXT;
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

typedef struct {
    nl_state_t *state;
    uint16_t   *fb;
    uint32_t    last_frame_ms;
    uint32_t    pump_calls;
} nl_boot_pump_ctx_t;

static uint32_t boot_elapsed_ms(nl_boot_pump_ctx_t *ctx) {
    ctx->pump_calls += 6U;
    return ctx->pump_calls;
}

static void boot_progress_pump(void *raw_ctx) {
    nl_boot_pump_ctx_t *ctx = (nl_boot_pump_ctx_t *)raw_ctx;
    if (!ctx || !ctx->state || !ctx->fb) return;

    uint32_t now_ms = boot_elapsed_ms(ctx);
    if (now_ms - ctx->last_frame_ms >= 24U) {
        ctx->last_frame_ms = now_ms;
        nl_render_splash_anim(ctx->state, ctx->fb, now_ms, true);
        lcd_blit(ctx->fb, SCR_320x240_565);
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
    snprintf(state->sys.os_version, sizeof(state->sys.os_version), "%s",
             state->sys.hw_is_cx2 ? "TI-Nspire CX II" : "TI-Nspire CX");

    char res_path[NL_MAX_PATH_LEN];
    snprintf(res_path, sizeof(res_path), "%.*s/ndless/ndless_resources.tns",
             (int)(sizeof(res_path) - 32U), state->sys.docs_root);
    state->sys.ndless_persistent = nl_file_exists(res_path);
}

static void perform_full_scan(
    const char *docs_root,
    const char *self_path,
    const char *primary_cfg,
    const char *fallback_cfg,
    const char *cache_path,
    nl_progress_cb_t progress_cb,
    void *progress_ctx
) {
    bool had_profiles_file = (g_state->profiles_path[0] != '\0' && nl_file_exists(g_state->profiles_path));
    if (had_profiles_file && g_state->profiles.initialized) {
        nl_profile_snapshot_active(g_state);
    }

    nl_free_icons(g_state);
    memset(g_state->games, 0, sizeof(g_state->games));
    g_state->total_games = 0;
    memset(g_state->filtered_indices, 0, sizeof(g_state->filtered_indices));
    g_state->filtered_count = 0;

    snprintf(g_state->covers_dir, sizeof(g_state->covers_dir), "%s/ndless/covers", docs_root);
    snprintf(g_state->icons_dir, sizeof(g_state->icons_dir), "%s/ndless/icons", docs_root);
    snprintf(g_state->profiles_path, sizeof(g_state->profiles_path), "%.*s/ndless/nlauncher.profiles.tns",
             (int)(sizeof(g_state->profiles_path) - 34U), docs_root);
    (void)mkdir(g_state->covers_dir, 0777);
    (void)mkdir(g_state->icons_dir, 0777);

    /* Priority scan of dedicated game folders first, then full /documents tree */
    char sub_path[NL_MAX_PATH_LEN];
    snprintf(sub_path, sizeof(sub_path), "%s/games", docs_root);
    nl_scan_directory_ex(g_state, sub_path, self_path, 3, progress_cb, progress_ctx);

    snprintf(sub_path, sizeof(sub_path), "%s/ndless", docs_root);
    nl_scan_directory_ex(g_state, sub_path, self_path, 3, progress_cb, progress_ctx);

    snprintf(sub_path, sizeof(sub_path), "%s/roms", docs_root);
    nl_scan_directory_ex(g_state, sub_path, self_path, 3, progress_cb, progress_ctx);

    nl_scan_directory_ex(g_state, docs_root, self_path, 4, progress_cb, progress_ctx);

    load_launcher_config(g_state, primary_cfg, fallback_cfg);
    if (had_profiles_file && g_state->profiles.initialized) {
        nl_profile_apply_active(g_state);
    } else {
        /* Migrate existing nlauncher.cfg.tns settings & game stats into Profil 1 */
        nl_profiles_init_default(g_state);
    }
    nl_autostart_reconcile(g_state);
    nl_hotkey_reconcile(g_state);
    nl_load_all_icons_ex(g_state, progress_cb, progress_ctx);

    if (cache_path && cache_path[0] != '\0') {
        if (nl_save_library_cache(g_state, cache_path)) {
            g_state->sys.cache_valid = true;
        }
    }
}

int main(int argc, char *argv[]) {
    g_state = (nl_state_t *)calloc(1, sizeof(nl_state_t));
    g_framebuffer = (uint16_t *)calloc(NL_SCREEN_W * NL_SCREEN_H, sizeof(uint16_t));
    if (!g_state || !g_framebuffer) {
        free(g_state);
        free(g_framebuffer);
        return 1;
    }

    const char *self_path = (argc > 0 && argv && argv[0]) ? argv[0] : "N-Launcher.tns";
    if (argc > 0 && argv) {
        enable_relative_paths(argv);
    }

    const char *docs_dir_ptr = get_documents_dir();
    char docs_root[192];
    snprintf(docs_root, sizeof(docs_root), "%s",
             (docs_dir_ptr && docs_dir_ptr[0] != '\0') ? docs_dir_ptr : "/documents");
    strip_trailing_slash(docs_root);

    char cfg_path[NL_MAX_PATH_LEN];
    char fallback_cfg_path[NL_MAX_PATH_LEN];
    char cache_path[NL_MAX_PATH_LEN];
    build_cfg_paths(docs_root, self_path, cfg_path, sizeof(cfg_path),
                    fallback_cfg_path, sizeof(fallback_cfg_path));
    snprintf(cache_path, sizeof(cache_path), "%.*s/ndless/nlauncher.cache.tns",
             (int)(sizeof(cache_path) - 32U), docs_root);

    /* Mark N-Launcher / child game session active so resident nl_hotkey.tns never re-enters */
    nl_bg_set_active_lock(docs_root, true);

    /* Permanently purge any legacy autostart stub/guard files from /documents/ndless/startup */
    nl_cleanup_legacy_autostart(docs_root);

    if (!lcd_init(SCR_320x240_565)) {
        nl_bg_set_active_lock(docs_root, false);
        free(g_framebuffer);
        free(g_state);
        return 1;
    }

    /* 1. Load saved prefs & verify hardware/Ndless state BEFORE drawing pixel 0 (NO default flash) */
    nl_init_state(g_state);
    snprintf(g_state->profiles_path, sizeof(g_state->profiles_path), "%.*s/ndless/nlauncher.profiles.tns",
             (int)(sizeof(g_state->profiles_path) - 34U), docs_root);
    populate_sys_env(g_state, docs_root, self_path);
    load_launcher_config(g_state, cfg_path, fallback_cfg_path);
    (void)nl_load_profiles(g_state, g_state->profiles_path);
    nl_autostart_reconcile(g_state);
    nl_hotkey_reconcile(g_state);

    /* 2. Draw frame 0 of N-Launcher startup animation in saved mode & accent immediately */
    nl_render_splash_anim(g_state, g_framebuffer, 0U, true);
    lcd_blit(g_framebuffer, SCR_320x240_565);

    /* 3. Fast boot from validated persistent cache (nlauncher.cache.tns) or fallback to full scan */
    nl_boot_pump_ctx_t boot_ctx = { g_state, g_framebuffer, 0U, 0U };
    snprintf(g_state->covers_dir, sizeof(g_state->covers_dir), "%s/ndless/covers", docs_root);
    snprintf(g_state->icons_dir, sizeof(g_state->icons_dir), "%s/ndless/icons", docs_root);
    (void)mkdir(g_state->covers_dir, 0777);
    (void)mkdir(g_state->icons_dir, 0777);

    if (nl_load_library_cache(g_state, cache_path)) {
        g_state->sys.cache_loaded_at_boot = true;
        load_launcher_config(g_state, cfg_path, fallback_cfg_path);
        if (!nl_load_profiles(g_state, g_state->profiles_path)) {
            nl_profiles_init_default(g_state);
            (void)nl_save_profiles(g_state, g_state->profiles_path);
        }
        nl_autostart_reconcile(g_state);
        nl_hotkey_reconcile(g_state);
        nl_load_all_screenshots(g_state);
        nl_rebuild_filter(g_state);
    } else {
        g_state->sys.cache_loaded_at_boot = false;
        perform_full_scan(docs_root, self_path, cfg_path, fallback_cfg_path,
                          cache_path, boot_progress_pump, &boot_ctx);
        if (!nl_load_profiles(g_state, g_state->profiles_path)) {
            nl_profiles_init_default(g_state);
            (void)nl_save_profiles(g_state, g_state->profiles_path);
        }
        nl_rebuild_filter(g_state);
    }

    /* Ensure the opening splash animation plays smoothly even on fast cached boots */
    uint32_t final_boot_ms = boot_ctx.last_frame_ms;
    while (final_boot_ms < 360U) {
        final_boot_ms += 24U;
        nl_render_splash_anim(g_state, g_framebuffer, final_boot_ms, true);
        lcd_blit(g_framebuffer, SCR_320x240_565);
        nl_safe_delay_ms(14);
    }

    /* Step initial animation state so layout and selection scales are fully settled */
    (void)nl_anim_step(g_state);

    /* 4. Loading finished -> hide spinner & smoothly crossfade directly into library */
    uint16_t *last_splash_fb = (uint16_t *)malloc(NL_FB_BYTES);
    uint16_t *lib_fb = (uint16_t *)malloc(NL_FB_BYTES);
    if (last_splash_fb && lib_fb) {
        nl_render_splash_anim(g_state, last_splash_fb, final_boot_ms, false);
        lcd_blit(last_splash_fb, SCR_320x240_565);
        nl_render_frame(g_state, lib_fb);
        for (int tr = 1; tr <= 5; tr++) {
            nl_safe_delay_ms(13);
            nl_render_boot_transition(g_state, g_framebuffer, last_splash_fb, lib_fb, tr, 5);
            lcd_blit(g_framebuffer, SCR_320x240_565);
        }
    } else {
        nl_render_frame(g_state, g_framebuffer);
        lcd_blit(g_framebuffer, SCR_320x240_565);
    }
    free(last_splash_fb);
    free(lib_fb);
    nl_wait_all_keys_released();

    nl_key_t last_key = NL_KEY_NONE;
    char     last_char = 0;
    unsigned hold_ticks = 0;

    while (!g_state->should_exit) {
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
                g_state->anim.ink_active = false;
                save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                perform_full_scan(docs_root, self_path, cfg_path, fallback_cfg_path,
                                  cache_path, NULL, NULL);
                nl_show_toast(g_state, "BIBLIOTHEQUE ACTUALISEE", 60);
                need_redraw = true;
            } else {
                if (trigger && (key == NL_KEY_DOC || key == NL_KEY_MENU) && g_framebuffer) {
                    if (!g_ink_old_fb) g_ink_old_fb = (uint16_t *)malloc(NL_FB_BYTES);
                    if (!g_ink_new_fb) g_ink_new_fb = (uint16_t *)malloc(NL_FB_BYTES);
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
                        /* Custom icon was deleted -> update persistent cache immediately */
                        (void)nl_save_library_cache(g_state, cache_path);
                    }
                    if (g_state->sui.rescan_req) {
                        nl_ui_screen_t keep_scr = g_state->sui.screen;
                        int keep_cur = g_state->sui.menu_cursor;
                        int keep_scr_off = g_state->sui.menu_scroll;
                        g_state->sui.rescan_req = false;
                        save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                        perform_full_scan(docs_root, self_path, cfg_path, fallback_cfg_path,
                                          cache_path, NULL, NULL);
                        g_state->sui.screen = keep_scr;
                        g_state->sui.menu_cursor = keep_cur;
                        g_state->sui.menu_scroll = keep_scr_off;
                        g_state->anim.set_open_fp = 256;
                        g_state->anim.set_last_screen = keep_scr;
                        nl_show_toast(g_state, "BIBLIOTHEQUE ACTUALISEE", 60);
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
                        (void)nl_save_profiles(g_state, g_state->profiles_path);
                    }
                    if (g_state->sui.config_dirty || (trigger && (key == NL_KEY_DOC || key == NL_KEY_MENU))) {
                        g_state->sui.config_dirty = false;
                        save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                        (void)nl_save_profiles(g_state, g_state->profiles_path);
                    }

                    if (g_state->launch_requested &&
                        g_state->launch_game_idx >= 0 &&
                        g_state->launch_game_idx < g_state->total_games) {

                        int launched_idx = g_state->launch_game_idx;
                        nl_game_t *target = &g_state->games[launched_idx];
                        g_state->launch_requested = false;

                        if (!nl_file_exists(target->path)) {
                            nl_show_toast(g_state, "FICHIER INTROUVABLE", 80);
                            g_state->launch_game_idx = -1;
                        } else {
                            g_state->anim.launch_anim_active = true;

                            /* Smooth 4-stage launch transition with 3 concentric water-drop ripples (46 frames, ~740ms) */
                            for (int z = 1; z <= 46; z++) {
                                nl_render_launch_zoom(g_state, g_framebuffer, z, 46);
                                lcd_blit(g_framebuffer, SCR_320x240_565);
                                nl_safe_delay_ms(16);
                            }

                            save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                            (void)nl_save_profiles(g_state, g_state->profiles_path);
                            set_working_dir_for_game(target->path);
                            nl_wait_all_keys_released();

                            free(g_ink_old_fb); g_ink_old_fb = NULL;
                            free(g_ink_new_fb); g_ink_new_fb = NULL;
                            free(g_framebuffer);
                            g_framebuffer = NULL;
                            lcd_init(SCR_TYPE_INVALID);

                            memset(&g_cap_diag_local, 0, sizeof(g_cap_diag_local));
                            snprintf(g_cap_diag_local.game_filename, sizeof(g_cap_diag_local.game_filename),
                                     "%s", target->filename);
                            g_cap_diag_local.best_scene_score = -1;
                            g_cap_diag_local.last_scr_type = -1;

                            bool allow_capture = !target->has_screenshot;
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

                            int rc = nl_exec(target->path, 0, NULL);
                            uint32_t est_play_sec = 5U + (g_cap_diag_local.blit_frames_seen / 30U);
                            uint32_t session_end_sec = session_start_sec + est_play_sec;
                            g_state->last_exec_status = rc;

                            nl_uninstall_capture_hooks();
                            g_cap_armed = false;
                            g_cap_target = NULL;

                            if (allow_capture && rc != 0xDEAD && !g_cap_done && g_cap_staged &&
                                target->scr_pixels != NULL) {
                                target->has_screenshot = true;
                                g_cap_done = true;
                                g_cap_diag_local.status = NL_CAP_CAPTURED_STAGED;
                            }

                            bool captured_now = (rc != 0xDEAD) && g_cap_done && target->has_screenshot;
                            bool saved_ok = false;
                            if (captured_now) {
                                saved_ok = nl_save_captured_screenshot(target, g_state->covers_dir);
                                g_cap_diag_local.save_ok = saved_ok;
                                if (!saved_ok) {
                                    g_cap_diag_local.status = NL_CAP_FAIL_SAVE_IO;
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
                                nl_write_internal_cap_log(&g_cap_diag_local, g_state->covers_dir);
                            }

                            restore_documents_working_dir(docs_root);

                            if (rc != 0xDEAD) {
                                nl_record_play_session(g_state, launched_idx, session_start_sec, session_end_sec);
                                nl_rebuild_filter(g_state);
                            }
                            g_state->launch_game_idx = -1;
                            g_state->sui.config_dirty = false;
                            save_launcher_config(g_state, cfg_path, fallback_cfg_path);
                            (void)nl_save_profiles(g_state, g_state->profiles_path);

                            g_framebuffer = (uint16_t *)calloc(NL_SCREEN_W * NL_SCREEN_H, sizeof(uint16_t));
                            if (!g_framebuffer) {
                                g_state->anim.launch_anim_active = false;
                                break;
                            }
                            lcd_init(SCR_320x240_565);
                            g_state->anim.launch_anim_active = false;
                            nl_wait_all_keys_released();
                            last_key = NL_KEY_NONE;
                            last_char = 0;
                            hold_ticks = 0;

                            if (rc == 0xDEAD) {
                                nl_show_toast(g_state, "ERREUR LANCEMENT", 85);
                            } else if (captured_now && saved_ok) {
                                nl_show_toast(g_state, "CAPTURE ENREGISTREE !", 85);
                            }
                            need_redraw = true;
                        }
                    }
                }
            }
        }

        if (!g_state->should_exit && nl_anim_step(g_state)) {
            need_redraw = true;
        }

        if (!g_state->should_exit && need_redraw && g_framebuffer) {
            if (g_state->anim.ink_active && g_ink_old_fb && g_ink_new_fb) {
                nl_render_frame(g_state, g_ink_new_fb);
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
        }

        if (!g_state->should_exit) {
            nl_safe_delay_ms(16);
        }
    }

    /* Clean, hang-free return to TI-Nspire OS: clear active lock cleanly */
    nl_bg_set_active_lock(docs_root, false);
    save_launcher_config(g_state, cfg_path, fallback_cfg_path);
    (void)nl_save_profiles(g_state, g_state->profiles_path);
    restore_documents_working_dir(docs_root);
    lcd_init(SCR_TYPE_INVALID);

    nl_wait_all_keys_released();

    free(g_ink_old_fb);
    free(g_ink_new_fb);
    free(g_framebuffer);
    nl_free_icons(g_state);
    free(g_state);
    return 0;
}
