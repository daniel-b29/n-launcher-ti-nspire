/****************************************************************************
 * N-Launcher — nl_hotkey.c (Global TI-OS Hotkey Resident Module)
 * Compiled to: /documents/ndless/startup/nl_hotkey.tns
 *
 * Compatible with BOTH Ndless Classique (non-persistant, while active in RAM)
 * and Ndless Persistant on TI-Nspire CX / CX CAS (4.5.x) & CX II-T (6.2.0).
 *
 * Architecture (#R13):
 *  1. Locates Ndless ints_swi_handler via vector 0x28 and "NEXT" magic
 *     (*(swi_handler - 2) == 0x4E455854), extracting BOTH sc_ext_table
 *     (for direct sc_nl_exec without consuming an SWI slot) and sc_addrs_ptr.
 *  2. Installs a pure-stack, 100% re-entrant ARM32 event-consuming wrapper
 *     on get_event+0 with 6-word PC-relative LDR prologue relocation so
 *     TI-OS 6.2.0 prologues (including ldr Rd, [pc, #imm]) attach cleanly at
 *     get_event+0 instead of falling back to gui_gc_finish.
 *  3. Supports BOTH simultaneous hold (Shift+Menu, Ctrl+Menu, Ctrl+Shift+Menu)
 *     and TI-Nspire sticky modifier press (Shift -> Menu, Ctrl -> Menu),
 *     consuming triggering & post-launch events BEFORE TI-OS sees them
 *     (eliminating Rename / Context Menu popups everywhere in TI-OS).
 *  4. Auto-heals stale nl_active.lock.tns files left after an abrupt reboot:
 *     whenever get_event runs with g_hk_ctrl.in_progress == 0, N-Launcher is
 *     provably not executing on the synchronous TI-OS GUI thread, so any
 *     leftover lock file is unlinked directly via os_sc_table[e_unlink].
 *  5. Calls sc_ext_table[10] (sc_nl_exec) directly as a C function pointer
 *     (0 SWI nesting slots consumed -> N-Launcher + child games keep full
 *     2-level SWI recursion without crashing ints_swi_handler!).
 *  6. Uses only direct Nucleus syscalls inside the resident hook and exits
 *     via _exit(0) so crt0 teardown never corrupts resident state.
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

typedef int (*nl_sc_exec_fn_t)(const char *prgm_path, int argsn, char *args[]);
typedef int (*nl_os_unlink_fn_t)(const char *path);
typedef int (*nl_orig_get_event_fn_t)(void *ev);

nl_hk_ctrl_t g_hk_ctrl = {
    NL_HK_CTRL_MAGIC,
    NL_HK_CTRL_VERSION,
    0U,
    (uint32_t)NL_HK_COMBO_CTRL_MENU,
    0U,
    0U,
    0U,
    0U,
    0U,
    { 0U, 0U },
    0U,
    0U,
    "/documents",
    "/documents/ndless/N-Launcher.tns"
};

static volatile int          g_hk_latched = 0;
static volatile int          g_suppress_after_launch = 0;
static nl_os_unlink_fn_t     g_os_unlink_fn = NULL;
static nl_hk_sticky_state_t  g_sticky_state = { 0, 0, false, false };

void __attribute__((weak)) _fini(void) {}

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

static void hk_tiny_delay_ms(unsigned ms) {
    unsigned loops_per_ms = (nl_hwsubtype() == 2) ? 38000U : 22000U;
    unsigned total = ms * loops_per_ms;
    for (volatile unsigned i = 0U; i < total; i++) {
        __asm__ volatile("" ::: "memory");
    }
}

static bool hk_recovery_pressed(void) {
    return (isKeyPressed(KEY_NSPIRE_ESC) != 0) || (on_key_pressed() != 0);
}

static bool hk_poll_combo(nl_hk_combo_t combo) {
    bool ctrl_dn  = isKeyPressed(KEY_NSPIRE_CTRL) != 0;
    bool shift_dn = isKeyPressed(KEY_NSPIRE_SHIFT) != 0;
    bool menu_dn  = isKeyPressed(KEY_NSPIRE_MENU) != 0;
    bool any_dn   = any_key_pressed() != 0;
    bool other_dn = any_dn && !ctrl_dn && !shift_dn && !menu_dn;
    return nl_hk_sticky_eval(&g_sticky_state, combo, ctrl_dn, shift_dn, menu_dn, other_dn);
}

static bool hk_any_trigger_key_down(void) {
    return (isKeyPressed(KEY_NSPIRE_MENU) != 0) ||
           (isKeyPressed(KEY_NSPIRE_CTRL) != 0) ||
           (isKeyPressed(KEY_NSPIRE_SHIFT) != 0);
}

/* Bounded non-WFI key-release barriers:
 * CRITICAL on CX II-T (OS 6.2.0): Ndless's ld_exec_with_args() calls wait_no_key_pressed(),
 * which invokes libndls idle() if any_key_pressed() is non-zero. On CX II-T HW-W+, libndls idle()
 * accesses Classic/CX1 interrupt controller addresses (0xDC000010 / 0x900D000C) and freezes!
 * Waiting here with a pure CPU loop until !any_key_pressed() && !on_key_pressed() ensures
 * ld_exec_with_args() sees all keys released immediately and NEVER calls idle(). */
static bool hk_wait_shortcut_release(void) {
    unsigned clean = 0U;
    for (unsigned i = 0U; i < 400U; i++) {
        if (!any_key_pressed() &&
            !on_key_pressed() &&
            !isKeyPressed(KEY_NSPIRE_MENU) &&
            !isKeyPressed(KEY_NSPIRE_CTRL) &&
            !isKeyPressed(KEY_NSPIRE_SHIFT)) {
            if (++clean >= 5U) return true;
        } else {
            clean = 0U;
        }
        hk_tiny_delay_ms(10U);
    }
    return (!any_key_pressed() && !on_key_pressed());
}

static void hk_wait_exit_keys_release(void) {
    unsigned clean = 0U;
    for (unsigned i = 0U; i < 400U; i++) {
        if (!any_key_pressed() &&
            !on_key_pressed() &&
            !isKeyPressed(KEY_NSPIRE_ESC) &&
            !isKeyPressed(KEY_NSPIRE_MENU) &&
            !isKeyPressed(KEY_NSPIRE_CTRL) &&
            !isKeyPressed(KEY_NSPIRE_SHIFT)) {
            if (++clean >= 5U) return;
        } else {
            clean = 0U;
        }
        hk_tiny_delay_ms(10U);
    }
}

/* Direct Nucleus OS config reader for resident hook context (0 newlib I/O calls).
 * Also auto-heals stale nl_active.lock.tns files when in_progress == 0U (synchronous OS thread). */
static bool nuc_check_hotkey_allowed_and_target(
    const char *docs_root,
    char *out_target,
    size_t target_max,
    bool *out_enabled,
    nl_hk_combo_t *out_combo
) {
    if (out_enabled) *out_enabled = false;
    if (out_combo)   *out_combo = (nl_hk_combo_t)g_hk_ctrl.combo;

    char lock_path[256];
    char guard_path[256];
    char cfg_path[256];

    nl_bg_join_path(lock_path, sizeof(lock_path), docs_root, "/ndless/nl_active.lock.tns");
    if (nuc_lock_exists(lock_path)) {
        /* We are executing inside TI-OS's own synchronous get_event loop while in_progress was 0:
         * N-Launcher cannot be running right now, so this lock file is stale from a prior reset. */
        if (g_os_unlink_fn != NULL) {
            (void)g_os_unlink_fn(lock_path);
        } else {
            return false;
        }
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
    nl_hk_combo_t cfg_combo = (nl_hk_combo_t)g_hk_ctrl.combo;
    char line[320];
    while (nuc_fgets(line, sizeof(line), cfp) != NULL) {
        nl_bg_trim_crlf(line);
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "global_hotkey=", 14) == 0) {
            hk_enabled = (nl_bg_parse_flag_val(p + 14) != 0);
        } else if (strncmp(p, "hotkey_combo=", 13) == 0) {
            cfg_combo = nl_str_to_hk_combo(p + 13);
        } else if (strncmp(p, "launcher_path=", 14) == 0 && p[14] != '\0') {
            nl_bg_copy_str(out_target, target_max, p + 14);
        }
    }
    nuc_fclose(cfp);

    if (out_enabled) *out_enabled = hk_enabled;
    if (out_combo)   *out_combo = cfg_combo;

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

static bool hk_try_launch_launcher(void) {
    if (g_hk_ctrl.enabled == 0U || g_hk_ctrl.in_progress != 0U || g_hk_ctrl.sc_nl_exec_addr == 0U) {
        return false;
    }
    if (nl_isstartup()) {
        return false;
    }

    /* Block only legacy 2bpp/4bpp/8bpp fullscreen game modes (1, 2, 3);
     * allow all TI-OS 16bpp/24bpp color modes on both CX (4.5) and CX II-T (6.2.0) */
    if (has_colors) {
        uint32_t lcd_ctrl = *IO_LCD_CONTROL;
        uint32_t bpp_mode = (lcd_ctrl >> 1) & 0x7U;
        if (bpp_mode == 1U || bpp_mode == 2U || bpp_mode == 3U) {
            return false;
        }
    }

    g_hk_ctrl.in_progress = 1U;
    g_hk_latched = 1;
    nl_hk_sticky_reset(&g_sticky_state);

    const char *docs = get_documents_dir();
    char docs_root[160];
    nl_bg_copy_str(docs_root, sizeof(docs_root),
                   (docs && docs[0]) ? docs : (g_hk_ctrl.docs_root[0] ? g_hk_ctrl.docs_root : "/documents"));
    nl_bg_strip_slash(docs_root);

    char target_path[256];
    bool cfg_enabled = false;
    nl_hk_combo_t cfg_combo = (nl_hk_combo_t)g_hk_ctrl.combo;
    bool allowed = nuc_check_hotkey_allowed_and_target(
        docs_root, target_path, sizeof(target_path), &cfg_enabled, &cfg_combo
    );
    g_hk_ctrl.combo = (uint32_t)cfg_combo;
    if (!cfg_enabled) {
        g_hk_ctrl.enabled = 0U;
    }

    bool launched = false;
    if (allowed) {
        nl_bg_copy_str(g_hk_ctrl.target_path, sizeof(g_hk_ctrl.target_path), target_path);
        if (hk_wait_shortcut_release()) {
            /* Direct call to sc_ext_table[10] (sc_nl_exec -> ld_exec_with_args):
             * consumes 0 SWI handler slots so N-Launcher and all child games keep
             * the complete 2-level ints_swi_handler stack intact! */
            nl_sc_exec_fn_t direct_exec = (nl_sc_exec_fn_t)(uintptr_t)g_hk_ctrl.sc_nl_exec_addr;
            int rc = direct_exec(target_path, 0, NULL);
            if (rc != 0xDEAD && rc != 0xBEEF) {
                g_hk_ctrl.launch_count++;
                launched = true;
            }
            hk_wait_exit_keys_release();
            refresh_osscr();
            /* Suppress stale queued Menu/Shift/Ctrl events left in TI-OS queue after return */
            g_suppress_after_launch = 6;
        }
    }

    g_hk_ctrl.in_progress = 0U;
    return launched;
}

extern uint32_t nl_orig_trampoline_code[6];
void nl_get_event_entry_anchor(void);
void nl_fallback_hook_entry_anchor(void);
int nl_get_event_wrapper(void *ev_raw);

__asm__(
    ".section .text\n"
    ".align 2\n"
    ".word g_hk_ctrl\n"        /* nl_get_event_entry_anchor - 8 */
    ".word 0x4B484C4E\n"       /* nl_get_event_entry_anchor - 4 : 'NLHK' */
    ".global nl_get_event_entry_anchor\n"
    "nl_get_event_entry_anchor:\n"
    "b nl_get_event_wrapper\n" /* Pure tail-branch: preserves r0, sp, lr; 100% re-entrant */
    ".align 2\n"
    ".word g_hk_ctrl\n"        /* nl_fallback_hook_entry_anchor - 8 */
    ".word 0x4B484C4E\n"       /* nl_fallback_hook_entry_anchor - 4 : 'NLHK' */
    ".global nl_fallback_hook_entry_anchor\n"
    "nl_fallback_hook_entry_anchor:\n"
    "b nl_tios_hotkey_hook\n"  /* Annotated entry anchor for fallback HOOK_DEFINE */
    ".align 2\n"
    ".global nl_orig_trampoline_code\n"
    "nl_orig_trampoline_code:\n"
    ".word 0\n"                /* [0] relocated w0 */
    ".word 0\n"                /* [1] relocated w1 */
    ".word 0xE51FF004\n"       /* [2] ldr pc, [pc, #-4] */
    ".word 0\n"                /* [3] orig_site + 8 */
    ".word 0\n"                /* [4] literal pool for w0 (PC-rel LDR) */
    ".word 0\n"                /* [5] literal pool for w1 (PC-rel LDR) */
);

static inline void hk_zero_event_buf(void *ev_raw) {
    if (!ev_raw) return;
    volatile uint32_t *w = (volatile uint32_t *)ev_raw;
    for (unsigned z = 0U; z < 8U; z++) w[z] = 0U;
}

/* 100% Re-entrant Pure-Stack get_event Wrapper:
 * Checks before AND immediately after orig_get_event(ev) returns, consuming
 * the hotkey event (and stale post-launch Menu/modifier events) before TI-OS sees them. */
int nl_get_event_wrapper(void *ev_raw) {
    nl_orig_get_event_fn_t orig_fn = (nl_orig_get_event_fn_t)(uintptr_t)&nl_orig_trampoline_code[0];
    if (!orig_fn) return 0;

    for (;;) {
        if (g_hk_ctrl.enabled != 0U && g_hk_ctrl.in_progress == 0U && g_hk_latched == 0) {
            if (hk_poll_combo((nl_hk_combo_t)g_hk_ctrl.combo) && !hk_recovery_pressed()) {
                if (hk_try_launch_launcher()) {
                    g_hk_ctrl.events_intercepted++;
                    hk_zero_event_buf(ev_raw);
                    continue;
                }
            }
        }

        int rc = orig_fn(ev_raw);

        if (!hk_any_trigger_key_down()) {
            g_hk_latched = 0;
        }

        /* Drain stale queued key events immediately following N-Launcher exit */
        if (g_suppress_after_launch > 0) {
            g_suppress_after_launch--;
            if (ev_raw) {
                uint16_t ev_type = *(const volatile uint16_t *)((const volatile uint8_t *)ev_raw + 4U);
                /* Type 0x08 = key down, 0x10 = key up: drop stale queued Menu/Shift/Ctrl events */
                if ((ev_type == 0x08U || ev_type == 0x10U) && !any_key_pressed()) {
                    hk_zero_event_buf(ev_raw);
                    continue;
                }
            }
        }

        if (g_hk_ctrl.enabled != 0U && g_hk_ctrl.in_progress == 0U && g_hk_latched == 0) {
            bool is_key_up = false;
            if (ev_raw) {
                uint16_t ev_type = *(const volatile uint16_t *)((const volatile uint8_t *)ev_raw + 4U);
                if (ev_type == 0x10U) is_key_up = true;
            }
            if (!is_key_up &&
                hk_poll_combo((nl_hk_combo_t)g_hk_ctrl.combo) &&
                !hk_recovery_pressed()) {
                if (hk_try_launch_launcher()) {
                    g_hk_ctrl.events_intercepted++;
                    hk_zero_event_buf(ev_raw);
                    continue;
                }
            }
        }

        return rc;
    }
}

/* Fallback HOOK_DEFINE when fn_addr+0 cannot be relocated cleanly */
HOOK_DEFINE(nl_tios_hotkey_hook) {
    if (g_hk_ctrl.enabled != 0U && g_hk_ctrl.in_progress == 0U) {
        if (!hk_any_trigger_key_down()) {
            g_hk_latched = 0;
        }
        if (g_hk_latched == 0 && !hk_recovery_pressed() &&
            hk_poll_combo((nl_hk_combo_t)g_hk_ctrl.combo)) {
            if (hk_try_launch_launcher()) {
                g_hk_ctrl.events_intercepted++;
            }
        }
    }
    HOOK_RESTORE_RETURN(nl_tios_hotkey_hook);
}

/* Locate both Ndless syscall tables using canonical vector 0x28 + 'NEXT' signature */
static bool locate_ndless_tables(uintptr_t **out_ext, uintptr_t **out_os) {
    volatile uintptr_t vec_addr = 0x28U;
    const uint32_t *swi_handler = *(const uint32_t * volatile *)vec_addr;
    if (!nl_hotkey_parse_swi_handler(swi_handler, out_ext, out_os)) {
        return false;
    }
    if (!out_ext || !(*out_ext) || !out_os || !(*out_os)) {
        return false;
    }
    uintptr_t *ext = *out_ext;
    if (ext[10] == 0U) { /* sc_nl_exec must be non-null */
        return false;
    }
    return true;
}

/* Find a verified relocatable 2-instruction ARM32 prologue at addr (supporting PC-rel LDR!) or addr+4 */
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
    /* Primary choice: fn_addr+0 with full PC-free OR PC-relative LDR relocation! */
    if (nl_arm_instr_is_relocatable(w[0]) && nl_arm_instr_is_relocatable(w[1])) {
        return fn_addr;
    }
    /* Fallback choice: fn_addr+4 (HOOK_DEFINE requires strictly PC-free words) */
    if (nl_arm_instr_is_pc_free(w[1]) && nl_arm_instr_is_pc_free(w[2])) {
        return fn_addr + 4U;
    }
    return 0U;
}

static bool install_primary_wrapper_at(uintptr_t hook_site) {
    volatile uint32_t *patch = (volatile uint32_t *)hook_site;
    uint32_t w0 = patch[0];
    uint32_t w1 = patch[1];
    uint32_t tramp[6];

    if (!nl_arm_relocate_prologue(hook_site, w0, w1, NULL, NULL, tramp)) {
        return false;
    }

    g_hk_ctrl.orig_words[0] = w0;
    g_hk_ctrl.orig_words[1] = w1;
    for (unsigned i = 0U; i < 6U; i++) {
        nl_orig_trampoline_code[i] = tramp[i];
    }

    int intmask = TCT_Local_Control_Interrupts(-1);
    patch[0] = 0xE51FF004U; /* ldr pc, [pc, #-4] */
    patch[1] = (uint32_t)(uintptr_t)&nl_get_event_entry_anchor;
    clear_cache();
    (void)TCT_Local_Control_Interrupts(intmask);
    return true;
}

#endif /* __arm__ */

int main(int argc, char *argv[]) {
#ifdef __arm__
    const char *docs = get_documents_dir();
    char docs_root[160];
    nl_bg_copy_str(docs_root, sizeof(docs_root), (docs && docs[0]) ? docs : "/documents");
    nl_bg_strip_slash(docs_root);

    char cfg_path[256];
    char fallback_cfg[256];
    char guard_path[256];
    char startup_dir[256];
    char target_path[256];

    nl_bg_join_path(cfg_path, sizeof(cfg_path), docs_root, "/ndless/nlauncher.cfg.tns");
    nl_bg_join_path(fallback_cfg, sizeof(fallback_cfg), docs_root, "/nlauncher.cfg.tns");
    nl_bg_join_path(guard_path, sizeof(guard_path), docs_root, "/ndless/nl_boot_guard.cfg.tns");
    nl_bg_join_path(startup_dir, sizeof(startup_dir), docs_root, "/ndless/startup");
    nl_bg_join_path(target_path, sizeof(target_path), docs_root, "/ndless/N-Launcher.tns");

    bool at_startup = (nl_isstartup() != 0);
    bool spawned_by_nlauncher = false;
    if (argc > 1 && argv && argv[1] && strcmp(argv[1], "from_nl") == 0) {
        spawned_by_nlauncher = true;
    }

    if (at_startup || !spawned_by_nlauncher) {
        /* Clear any stale lock left after a hard reset or when user runs nl_hotkey.tns manually from TI-OS,
         * NEVER when spawned live by a running N-Launcher instance ("from_nl"). */
        (void)nl_bg_set_active_lock(docs_root, false);
    }

    if (at_startup) {
        /* Emergency recovery: if ESC or ON is held at Ndless startup, disable hotkey immediately */
        if (isKeyPressed(KEY_NSPIRE_ESC) || on_key_pressed()) {
            (void)nl_bg_disable_hotkey_in_cfg(cfg_path);
            if (nl_bg_file_exists_any(fallback_cfg)) {
                (void)nl_bg_disable_hotkey_in_cfg(fallback_cfg);
            }
            char hk_stub[256];
            nl_bg_join_path(hk_stub, sizeof(hk_stub), startup_dir, "/nl_hotkey.tns");
            (void)unlink(hk_stub);
            _exit(0);
        }
    }

    /* Check boot guard state */
    nl_boot_guard_state_t gs;
    if (nl_bg_read_guard_state(guard_path, &gs)) {
        if (gs.pending != 0 || gs.recovery != 0) {
            _exit(0);
        }
    }

    /* Read global_hotkey and hotkey_combo from config */
    bool hk_enabled = false;
    nl_hk_combo_t hk_combo = NL_HK_COMBO_CTRL_MENU;
    nl_bg_read_hotkey_cfg_ex(cfg_path, &hk_enabled, &hk_combo, target_path, sizeof(target_path));
    if (!hk_enabled && !nl_bg_file_exists_any(cfg_path)) {
        nl_bg_read_hotkey_cfg_ex(fallback_cfg, &hk_enabled, &hk_combo, target_path, sizeof(target_path));
    }

    /* In Ndless Classic (non-persistent), if the user manually runs nl_hotkey.tns from TI-OS Documents
     * (!at_startup && !spawned_by_nlauncher), automatically enable global_hotkey=1 in config so the
     * shortcut works immediately without requiring the user to open N-Launcher first! */
    if (!hk_enabled && !at_startup && !spawned_by_nlauncher) {
        if (nl_bg_enable_hotkey_in_cfg(cfg_path)) {
            hk_enabled = true;
        }
    }
    if (!hk_enabled) {
        _exit(0);
    }

    /* When launched manually from TI-OS Documents, wait for Enter/Click key release before exiting
     * so ld_exec_with_args()'s post-return wait_no_key_pressed() never calls idle() on CX II-T 6.2.0 */
    if (!at_startup && !spawned_by_nlauncher) {
        hk_wait_exit_keys_release();
    }

    uintptr_t *sc_ext_table = NULL;
    uintptr_t *os_sc_table = NULL;
    if (!locate_ndless_tables(&sc_ext_table, &os_sc_table)) {
        _exit(1);
    }

    /* Cache direct Nucleus unlink syscall pointer for safe stale-lock self-healing in RAM */
    if ((unsigned)e_unlink <= 512U && os_sc_table[e_unlink] != 0U) {
        g_os_unlink_fn = (nl_os_unlink_fn_t)os_sc_table[e_unlink];
    }

    /* Check if NLHK control block is ALREADY resident in RAM -> update live without leaking execmem! */
    uintptr_t fn_get_event = ((unsigned)e_get_event <= 512U) ? os_sc_table[e_get_event] : 0U;
    uintptr_t fn_gc_finish = ((unsigned)e_gui_gc_finish <= 512U) ? os_sc_table[e_gui_gc_finish] : 0U;

    bool third_party = false;
    nl_hk_ctrl_t *live_ctrl = nl_hotkey_find_live_ctrl_at(
        fn_get_event, 0x10000000U, 0x18000000U, &third_party
    );
    if (!live_ctrl && !third_party && fn_gc_finish != 0U) {
        live_ctrl = nl_hotkey_find_live_ctrl_at(
            fn_gc_finish, 0x10000000U, 0x18000000U, &third_party
        );
    }
    if (live_ctrl) {
        live_ctrl->combo = (uint32_t)hk_combo;
        nl_bg_copy_str(live_ctrl->docs_root, sizeof(live_ctrl->docs_root), docs_root);
        nl_bg_copy_str(live_ctrl->target_path, sizeof(live_ctrl->target_path), target_path);
        live_ctrl->enabled = 1U;
        _exit(0);
    }

    /* Resolve safe ARM32 hook site: prioritize get_event+0 with PC-rel LDR relocation */
    bool already_hooked = false;
    uintptr_t target_fn = fn_get_event;
    uintptr_t hook_site = resolve_safe_hook_site(fn_get_event, &already_hooked);
    bool use_primary_wrapper = (hook_site != 0U && hook_site == fn_get_event);
    uint32_t mode_code = use_primary_wrapper ? 1U : ((hook_site != 0U) ? 2U : 0U);

    if (!already_hooked && hook_site == 0U && fn_gc_finish != 0U) {
        target_fn = fn_gc_finish;
        hook_site = resolve_safe_hook_site(fn_gc_finish, &already_hooked);
        use_primary_wrapper = false;
        mode_code = (hook_site != 0U) ? 3U : 0U;
    }

    if (already_hooked || hook_site == 0U) {
        _exit(already_hooked ? 0 : 1);
    }

    g_hk_ctrl.magic              = NL_HK_CTRL_MAGIC;
    g_hk_ctrl.version            = NL_HK_CTRL_VERSION;
    g_hk_ctrl.enabled            = 1U;
    g_hk_ctrl.combo              = (uint32_t)hk_combo;
    g_hk_ctrl.in_progress        = spawned_by_nlauncher ? 1U : 0U;
    g_hk_ctrl.launch_count       = 0U;
    g_hk_ctrl.hook_fn_addr       = (uint32_t)target_fn;
    g_hk_ctrl.hook_site          = (uint32_t)hook_site;
    g_hk_ctrl.sc_nl_exec_addr    = (uint32_t)sc_ext_table[10]; /* direct sc_nl_exec */
    g_hk_ctrl.hook_mode          = mode_code;
    g_hk_ctrl.events_intercepted = 0U;
    nl_bg_copy_str(g_hk_ctrl.docs_root, sizeof(g_hk_ctrl.docs_root), docs_root);
    nl_bg_copy_str(g_hk_ctrl.target_path, sizeof(g_hk_ctrl.target_path), target_path);

    if (use_primary_wrapper) {
        if (!install_primary_wrapper_at(hook_site)) {
            _exit(1);
        }
    } else {
        HOOK_INSTALL(hook_site, nl_tios_hotkey_hook);
        /* Point patched target word to NLHK-annotated entry anchor so nl_hotkey_find_live_ctrl_at()
         * recognizes fallback hooks identically to primary hooks */
        int intmask = TCT_Local_Control_Interrupts(-1);
        ((volatile uint32_t *)hook_site)[1] = (uint32_t)(uintptr_t)&nl_fallback_hook_entry_anchor;
        clear_cache();
        (void)TCT_Local_Control_Interrupts(intmask);
    }

    nl_set_resident();
    _exit(0); /* Bypass crt0 fini/exit cleanup so resident memory stays 100% intact */
    return 0;
#else
    (void)argc;
    (void)argv;
    return 0;
#endif
}
