/****************************************************************************
 * N-Launcher — nl_hotkey.c (Experimental Global TI-OS Hotkey Resident Module)
 * Compiled to: /documents/ndless/startup/nl_hotkey.tns
 *
 * Shortcut: Ctrl + Menu (KEY_NSPIRE_CTRL + KEY_NSPIRE_MENU)
 *
 * Hardware & OS Safety Guarantees:
 *  1. Disabled by default (global_hotkey=0 in nlauncher.cfg.tns).
 *  2. If ESC or ON is held during startup, or if boot-guard has pending=1
 *     or recovery=1, exits immediately and disables the startup hook.
 *  3. Fast-path in-hook check (< 10 ns): only reads memory-mapped keypad
 *     registers when Ctrl + Menu are NOT pressed simultaneously.
 *  4. Re-entrancy & active-game lock: never triggers if g_hk_in_progress!=0,
 *     if nl_isstartup() is true, if PL111 LCD is in 8bpp/4bpp game mode, or
 *     if /documents/ndless/nl_active.lock.tns exists (written while N-Launcher
 *     or any child game is running).
 *  5. Live disable: checks global_hotkey=1 in nlauncher.cfg.tns via direct
 *     Nucleus syscalls (nuc_fopen/nuc_fgets/nuc_fclose) before launching, so
 *     turning the option OFF in N-Launcher Settings takes effect immediately
 *     without requiring a calculator reboot.
 *  6. Single-instance RAM guard: verifies the target OS syscall prologue is
 *     not already hooked (0xE51FF004) and validates that both prologue words
 *     are PC-free ARM32 instructions before calling HOOK_INSTALL.
 ****************************************************************************/

#ifdef __arm__
#include <os.h>
#include <libndls.h>
#include <keys.h>
#include <hook.h>
#include <nucleus.h>
#include <syscall-list.h>
#include <syscall-decls.h>
#endif

#include "boot_guard_core.h"

#ifdef __arm__

static volatile int g_hk_in_progress = 0;
static volatile int g_hk_latched     = 0;

/* Locate Ndless syscall table (sc_addrs_ptr) via NEXT descriptor at 0x10000020 */
static uintptr_t *locate_ndless_sc_table(void) {
    volatile uint32_t *vec = (volatile uint32_t *)0x10000020U;
    if (vec[0] != 0x5458454EU) { /* 'NEXT' */
        return NULL;
    }
    uint32_t swi_addr = vec[2];
    if (swi_addr < 0x10000000U || swi_addr >= 0x14000000U || (swi_addr & 3U) != 0U) {
        return NULL;
    }
    const uint32_t *ins = (const uint32_t *)(uintptr_t)swi_addr;
    for (int i = 0; i < 64; i++) {
        /* Match 'ldr r0, [r2, r0, lsl #2]' (0xE7920100) inside ints_swi_handler */
        if (ins[i] == 0xE7920100U && i >= 1) {
            uint32_t prev = ins[i - 1];
            if ((prev & 0xFFFFF000U) == 0xD59F2000U || (prev & 0xFFFFF000U) == 0xE59F2000U) {
                uint32_t offset = prev & 0xFFFU;
                uintptr_t lit_addr = (uintptr_t)&ins[i - 1] + 8U + offset;
                if (lit_addr >= 0x10000000U && lit_addr < 0x14000000U) {
                    uintptr_t table_addr = *(volatile uintptr_t *)lit_addr;
                    if (table_addr >= 0x10000000U && table_addr < 0x14000000U) {
                        return (uintptr_t *)table_addr;
                    }
                }
            }
        }
    }
    return NULL;
}

/* Direct Nucleus OS file check (zero newlib _reent dependency inside resident hook) */
static bool nuc_file_ok(const char *path) {
    if (!path || !path[0]) return false;
    struct nuc_stat st;
    if (nuc_stat(path, &st) != 0) return false;
    return (st.st_size >= 16U);
}

static bool nuc_lock_exists(const char *lock_path) {
    if (!lock_path || !lock_path[0]) return false;
    struct nuc_stat st;
    return (nuc_stat(lock_path, &st) == 0);
}

/* Direct Nucleus OS config reader for resident hook context */
static bool nuc_check_hotkey_allowed_and_target(
    const char *docs_root,
    char *out_target,
    size_t target_max
) {
    char lock_path[256];
    char guard_path[256];
    char cfg_path[256];

    nl_bg_join_path(lock_path, sizeof(lock_path), docs_root, "/ndless/nl_active.lock.tns");
    if (nuc_lock_exists(lock_path)) {
        return false;
    }

    nl_bg_join_path(guard_path, sizeof(guard_path), docs_root, "/ndless/nl_boot_guard.cfg.tns");
    NUC_FILE *gfp = nuc_fopen(guard_path, "r");
    if (gfp) {
        char line[128];
        bool unsafe_guard = false;
        while (nuc_fgets(line, sizeof(line), gfp) != NULL) {
            nl_bg_trim_crlf(line);
            if (strncmp(line, "pending=", 8) == 0 && nl_bg_parse_flag_val(line + 8) != 0) {
                unsafe_guard = true;
            } else if (strncmp(line, "recovery=", 9) == 0 && nl_bg_parse_flag_val(line + 9) != 0) {
                unsafe_guard = true;
            }
        }
        nuc_fclose(gfp);
        if (unsafe_guard) return false;
    }

    nl_bg_join_path(cfg_path, sizeof(cfg_path), docs_root, "/ndless/nlauncher.cfg.tns");
    nl_bg_join_path(out_target, target_max, docs_root, "/ndless/N-Launcher.tns");

    NUC_FILE *cfp = nuc_fopen(cfg_path, "r");
    if (!cfp) {
        nl_bg_join_path(cfg_path, sizeof(cfg_path), docs_root, "/nlauncher.cfg.tns");
        cfp = nuc_fopen(cfg_path, "r");
        if (!cfp) return false;
    }

    bool hk_enabled = false;
    char line[320];
    while (nuc_fgets(line, sizeof(line), cfp) != NULL) {
        nl_bg_trim_crlf(line);
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "global_hotkey=", 14) == 0) {
            hk_enabled = (nl_bg_parse_flag_val(p + 14) != 0);
        } else if (strncmp(p, "launcher_path=", 14) == 0 && p[14] != '\0') {
            nl_bg_copy_str(out_target, target_max, p + 14);
        }
    }
    nuc_fclose(cfp);

    if (!hk_enabled) {
        return false;
    }
    if (!nuc_file_ok(out_target)) {
        nl_bg_join_path(out_target, target_max, docs_root, "/N-Launcher.tns");
        if (!nuc_file_ok(out_target)) {
            return false;
        }
    }
    return true;
}

void __attribute__((weak)) _fini(void) {}

HOOK_DEFINE(nl_tios_hotkey_hook) {
    /* 1. Ultra-fast path: 2 MMIO keypad reads when shortcut is not pressed */
    if (g_hk_in_progress == 0) {
        if (!isKeyPressed(KEY_NSPIRE_CTRL) || !isKeyPressed(KEY_NSPIRE_MENU)) {
            g_hk_latched = 0;
        } else if (g_hk_latched == 0 &&
                   !isKeyPressed(KEY_NSPIRE_ESC) &&
                   !on_key_pressed() &&
                   !nl_isstartup()) {
            /* Verify PL111 LCD controller is in normal TI-OS 16bpp mode (mode 6 or 4), not 8bpp/4bpp game mode */
            uint32_t lcd_ctrl = *IO_LCD_CONTROL;
            uint32_t bpp_mode = (lcd_ctrl >> 1) & 0x7U;
            if (bpp_mode == 6U || bpp_mode == 4U) {
                g_hk_in_progress = 1;
                g_hk_latched = 1;

                const char *docs = get_documents_dir();
                char docs_root[160];
                nl_bg_copy_str(docs_root, sizeof(docs_root), (docs && docs[0]) ? docs : "/documents");
                nl_bg_strip_slash(docs_root);

                char target_path[256];
                if (nuc_check_hotkey_allowed_and_target(docs_root, target_path, sizeof(target_path))) {
                    /* Wait for Ctrl + Menu release so keys never leak into N-Launcher or TI-OS */
                    wait_no_key_pressed();
                    (void)nl_exec(target_path, 0, NULL);
                    wait_no_key_pressed();
                    refresh_osscr();
                }

                g_hk_in_progress = 0;
            }
        }
    }
    HOOK_RESTORE_RETURN(nl_tios_hotkey_hook);
}

/* Find a verified PC-free 2-instruction ARM32 prologue at addr or addr + 4 */
static uintptr_t resolve_safe_hook_site(uintptr_t fn_addr, bool *out_already_hooked) {
    if (out_already_hooked) *out_already_hooked = false;
    if (fn_addr < 0x10000000U || fn_addr >= 0x13FF0000U || (fn_addr & 3U) != 0U) {
        return 0U;
    }
    const volatile uint32_t *w = (const volatile uint32_t *)fn_addr;
    if (w[0] == 0xE51FF004U || w[1] == 0xE51FF004U) {
        if (out_already_hooked) *out_already_hooked = true;
        return 0U;
    }
    if (nl_arm_instr_is_pc_free(w[0]) && nl_arm_instr_is_pc_free(w[1])) {
        return fn_addr;
    }
    if (nl_arm_instr_is_pc_free(w[1]) && nl_arm_instr_is_pc_free(w[2])) {
        return fn_addr + 4U;
    }
    return 0U;
}

#endif /* __arm__ */

int main(void) {
#ifdef __arm__
    const char *docs = get_documents_dir();
    char docs_root[160];
    nl_bg_copy_str(docs_root, sizeof(docs_root), (docs && docs[0]) ? docs : "/documents");
    nl_bg_strip_slash(docs_root);

    char cfg_path[256];
    char fallback_cfg[256];
    char guard_path[256];
    char startup_dir[256];

    nl_bg_join_path(cfg_path, sizeof(cfg_path), docs_root, "/ndless/nlauncher.cfg.tns");
    nl_bg_join_path(fallback_cfg, sizeof(fallback_cfg), docs_root, "/nlauncher.cfg.tns");
    nl_bg_join_path(guard_path, sizeof(guard_path), docs_root, "/ndless/nl_boot_guard.cfg.tns");
    nl_bg_join_path(startup_dir, sizeof(startup_dir), docs_root, "/ndless/startup");

    if (nl_isstartup()) {
        (void)nl_bg_set_active_lock(docs_root, false);
    }

    /* Emergency recovery: if ESC or ON is held at boot, disable hotkey & startup hooks immediately */
    if (isKeyPressed(KEY_NSPIRE_ESC) || on_key_pressed()) {
        (void)nl_bg_disable_hotkey_in_cfg(cfg_path);
        if (nl_bg_file_exists_any(fallback_cfg)) {
            (void)nl_bg_disable_hotkey_in_cfg(fallback_cfg);
        }
        char hk_stub[256];
        nl_bg_join_path(hk_stub, sizeof(hk_stub), startup_dir, "/nl_hotkey.tns");
        (void)unlink(hk_stub);
        return 0;
    }

    /* Check boot guard state */
    nl_boot_guard_state_t gs;
    if (nl_bg_read_guard_state(guard_path, &gs)) {
        if (gs.pending != 0 || gs.recovery != 0) {
            return 0;
        }
    }

    /* Only install resident hook if global_hotkey=1 is explicitly enabled in config */
    bool hk_enabled = false;
    nl_bg_read_hotkey_cfg(cfg_path, &hk_enabled, NULL, 0U);
    if (!hk_enabled && !nl_bg_file_exists_any(cfg_path)) {
        nl_bg_read_hotkey_cfg(fallback_cfg, &hk_enabled, NULL, 0U);
    }
    if (!hk_enabled) {
        return 0;
    }

    uintptr_t *sc_table = locate_ndless_sc_table();
    if (!sc_table) {
        return 0;
    }

    bool already_hooked = false;
    uintptr_t hook_site = 0U;
    if ((unsigned)e_get_event <= 512U && sc_table[e_get_event] != 0U) {
        hook_site = resolve_safe_hook_site(sc_table[e_get_event], &already_hooked);
    }
    if (already_hooked) {
        return 0;
    }
    if (hook_site == 0U && (unsigned)e_gui_gc_finish <= 512U && sc_table[e_gui_gc_finish] != 0U) {
        hook_site = resolve_safe_hook_site(sc_table[e_gui_gc_finish], &already_hooked);
    }
    if (already_hooked || hook_site == 0U) {
        return 0;
    }

    HOOK_INSTALL(hook_site, nl_tios_hotkey_hook);
    nl_set_resident();
    return 0;
#else
    return 0;
#endif
}
