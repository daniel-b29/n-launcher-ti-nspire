/****************************************************************************
 * N-Launcher — boot_guard_core.h
 * Shared Crash-Loop Protection, Config Patching & Recovery Mode Logic
 *
 * Used identically by:
 *  - nl_boot_stub.c  (minimal ARM startup stub in /documents/ndless/startup)
 *  - main_ndless.c   (N-Launcher stabilization & recovery status reporting)
 *  - test_runner.c   (host unit & reboot-loop fault-injection verification)
 *
 * Design guarantees:
 *  1. Ultra-low stack footprint (safe inside Ndless plh_startup() OS thread)
 *  2. Autostart is automatically disabled in-place (autostart=0) while
 *     preserving 100% of user prefs, themes, layouts, favorites, stats & keys
 *  3. Any pending=1 from a crash before main-loop stability IMMEDIATELY stops
 *     autostart, removes startup stubs, and lets TI-OS boot normally
 *  4. Physical recovery key (ESC or ON) aborts launch cleanly to TI-OS
 ****************************************************************************/

#ifndef BOOT_GUARD_CORE_H
#define BOOT_GUARD_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NL_BOOT_STABILIZE_FRAMES 30

typedef enum {
    NL_STUB_OK_CLEAN = 0,
    NL_STUB_SKIP_NOT_STARTUP = 1,
    NL_STUB_SKIP_DISABLED = 2,
    NL_STUB_ABORT_RECOVERY_KEY = 3,
    NL_STUB_ABORT_CRASH_GUARD = 4,
    NL_STUB_ABORT_MISSING_TARGET = 5,
    NL_STUB_ABORT_EXEC_FAILED = 6
} nl_stub_outcome_t;

typedef enum {
    NL_HK_COMBO_CTRL_MENU       = 0, /* Ctrl + Menu (defaut, intercepte avant TI-OS) */
    NL_HK_COMBO_SHIFT_MENU      = 1, /* Shift + Menu (aucun raccourci TI-OS natif) */
    NL_HK_COMBO_CTRL_SHIFT_MENU = 2, /* Ctrl + Shift + Menu */
    NL_HK_COMBO_COUNT           = 3
} nl_hk_combo_t;

#define NL_HK_CTRL_MAGIC   0x4B484C4EU /* "NLHK" little-endian */
#define NL_HK_CTRL_VERSION 13U         /* #R13 */

typedef struct {
    uint32_t          magic;              /* NL_HK_CTRL_MAGIC */
    uint32_t          version;            /* NL_HK_CTRL_VERSION */
    volatile uint32_t enabled;            /* 1 = active in RAM, 0 = disabled live */
    volatile uint32_t combo;              /* nl_hk_combo_t */
    volatile uint32_t in_progress;        /* Re-entrancy launch guard & N-Launcher active flag */
    volatile uint32_t launch_count;       /* Successful hotkey launches */
    uint32_t          hook_fn_addr;       /* Target OS function (e.g. get_event) */
    uint32_t          hook_site;          /* Patched ARM32 instruction site */
    uint32_t          sc_nl_exec_addr;    /* Direct sc_ext_table[10] (0 SWI slots!) */
    uint32_t          orig_words[2];      /* Relocated original 2 instructions */
    uint32_t          hook_mode;          /* 1=get_event+0 wrapper, 2=get_event+4, 3=gui_gc_finish */
    volatile uint32_t events_intercepted; /* Triggering events cleanly consumed before TI-OS */
    char              docs_root[160];
    char              target_path[256];
} nl_hk_ctrl_t;

typedef struct {
    int  pending;
    int  recovery;
    int  disabled;
    char reason[32];
} nl_boot_guard_state_t;

static inline const char *nl_hk_combo_to_str(nl_hk_combo_t c) {
    switch (c) {
        case NL_HK_COMBO_SHIFT_MENU:      return "shift_menu";
        case NL_HK_COMBO_CTRL_SHIFT_MENU: return "ctrl_shift_menu";
        case NL_HK_COMBO_CTRL_MENU:
        default:                          return "ctrl_menu";
    }
}

static inline nl_hk_combo_t nl_str_to_hk_combo(const char *s) {
    if (!s) return NL_HK_COMBO_CTRL_MENU;
    while (*s == ' ' || *s == '\t') s++;
    if (strncmp(s, "shift_menu", 10) == 0 || *s == '1') {
        return NL_HK_COMBO_SHIFT_MENU;
    }
    if (strncmp(s, "ctrl_shift_menu", 15) == 0 || *s == '2') {
        return NL_HK_COMBO_CTRL_SHIFT_MENU;
    }
    return NL_HK_COMBO_CTRL_MENU;
}

static inline const char *nl_hk_combo_label(nl_hk_combo_t c) {
    switch (c) {
        case NL_HK_COMBO_SHIFT_MENU:      return "Shift + Menu";
        case NL_HK_COMBO_CTRL_SHIFT_MENU: return "Ctrl + Shift + Menu";
        case NL_HK_COMBO_CTRL_MENU:
        default:                          return "Ctrl + Menu";
    }
}

static inline void nl_bg_copy_str(char *dst, size_t max_len, const char *src) {
    if (!dst || max_len == 0U) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0U;
    while (i + 1U < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static inline void nl_bg_strip_slash(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 1U && (s[n - 1U] == '/' || s[n - 1U] == '\\')) {
        s[--n] = '\0';
    }
}

static inline void nl_bg_join_path(char *dst, size_t max_len, const char *base, const char *suffix) {
    nl_bg_copy_str(dst, max_len, base);
    size_t pos = strlen(dst);
    if (!suffix) return;
    size_t j = 0U;
    while (pos + 1U < max_len && suffix[j] != '\0') {
        dst[pos++] = suffix[j++];
    }
    dst[pos] = '\0';
}

static inline void nl_bg_trim_crlf(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0U && (s[n - 1U] == '\n' || s[n - 1U] == '\r' || s[n - 1U] == ' ' || s[n - 1U] == '\t')) {
        s[--n] = '\0';
    }
}

static inline int nl_bg_parse_flag_val(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    return (*s >= '1' && *s <= '9') ? 1 : 0;
}

static inline bool nl_bg_file_exists_any(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static inline bool nl_bg_read_guard_state(const char *guard_path, nl_boot_guard_state_t *out) {
    if (!out) return false;
    out->pending = 0;
    out->recovery = 0;
    out->disabled = 0;
    nl_bg_copy_str(out->reason, sizeof(out->reason), "none");

    if (!guard_path || !guard_path[0]) return false;
    FILE *fp = fopen(guard_path, "r");
    if (!fp) return false;

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        nl_bg_trim_crlf(line);
        if (strncmp(line, "pending=", 8) == 0) {
            out->pending = nl_bg_parse_flag_val(line + 8);
        } else if (strncmp(line, "recovery=", 9) == 0) {
            out->recovery = nl_bg_parse_flag_val(line + 9);
        } else if (strncmp(line, "disabled=", 9) == 0) {
            out->disabled = nl_bg_parse_flag_val(line + 9);
        } else if (strncmp(line, "reason=", 7) == 0 && line[7] != '\0') {
            nl_bg_copy_str(out->reason, sizeof(out->reason), line + 7);
        }
    }
    fclose(fp);
    return true;
}

static inline bool nl_bg_write_guard_state(
    const char *guard_path,
    int pending,
    int recovery,
    int disabled,
    const char *reason
) {
    if (!guard_path || !guard_path[0]) return false;
    FILE *fp = fopen(guard_path, "w");
    if (!fp) return false;

    const char *rstr = (reason && reason[0]) ? reason : "none";
    fprintf(fp, "pending=%d\nrecovery=%d\ndisabled=%d\nreason=%s\n",
            pending ? 1 : 0,
            recovery ? 1 : 0,
            disabled ? 1 : 0,
            rstr);
    fflush(fp);
    fclose(fp);
    return true;
}

/* Reads autostart= and launcher_path= from nlauncher.cfg.tns without altering file */
static inline void nl_bg_read_launcher_cfg(
    const char *cfg_path,
    bool *out_autostart,
    char *out_target_path,
    size_t target_max
) {
    if (out_autostart) *out_autostart = false;
    if (!cfg_path || !cfg_path[0]) return;

    FILE *fp = fopen(cfg_path, "r");
    if (!fp) return;

    char line[320];
    while (fgets(line, sizeof(line), fp)) {
        nl_bg_trim_crlf(line);
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "autostart=", 10) == 0) {
            if (out_autostart) *out_autostart = (nl_bg_parse_flag_val(p + 10) != 0);
        } else if (strncmp(p, "launcher_path=", 14) == 0 && p[14] != '\0') {
            if (out_target_path && target_max > 0U) {
                nl_bg_copy_str(out_target_path, target_max, p + 14);
            }
        }
    }
    fclose(fp);
}

/* Rewrites ONLY autostart=1 -> autostart=0 in cfg_path, preserving 100% of other lines
 * (theme, accent, layout, sort, favorites, play counts, play time, timestamps, shortcuts). */
static inline bool nl_bg_disable_autostart_in_cfg(const char *cfg_path) {
    if (!cfg_path || !cfg_path[0]) return false;

    FILE *rfp = fopen(cfg_path, "rb");
    if (!rfp) {
        FILE *nfp = fopen(cfg_path, "w");
        if (!nfp) return false;
        fprintf(nfp, "autostart=0\n");
        fflush(nfp);
        fclose(nfp);
        return true;
    }

    if (fseek(rfp, 0, SEEK_END) != 0) {
        fclose(rfp);
        return false;
    }
    long fsz = ftell(rfp);
    if (fsz < 0 || fsz > 131072L || fseek(rfp, 0, SEEK_SET) != 0) {
        fclose(rfp);
        return false;
    }

    size_t sz = (size_t)fsz;
    char *buf = (char *)malloc(sz + 1U);
    if (!buf) {
        fclose(rfp);
        return false;
    }
    size_t nread = (sz > 0U) ? fread(buf, 1U, sz, rfp) : 0U;
    fclose(rfp);
    buf[nread] = '\0';

    /* Output buffer: existing content + possible "autostart=0\n" safety margin */
    char *out = (char *)malloc(nread + 32U);
    if (!out) {
        free(buf);
        return false;
    }

    size_t out_pos = 0U;
    size_t pos = 0U;
    bool replaced_key = false;

    while (pos < nread) {
        size_t line_start = pos;
        while (pos < nread && buf[pos] != '\n') pos++;
        size_t line_end = pos;
        bool had_nl = (pos < nread && buf[pos] == '\n');
        if (had_nl) pos++;

        size_t trim_end = line_end;
        while (trim_end > line_start && buf[trim_end - 1U] == '\r') {
            trim_end--;
        }
        size_t trim_start = line_start;
        while (trim_start < trim_end && (buf[trim_start] == ' ' || buf[trim_start] == '\t')) {
            trim_start++;
        }

        if (trim_end - trim_start >= 10U &&
            strncmp(buf + trim_start, "autostart=", 10U) == 0) {
            static const char k_off[] = "autostart=0";
            memcpy(out + out_pos, k_off, sizeof(k_off) - 1U);
            out_pos += sizeof(k_off) - 1U;
            replaced_key = true;
        } else {
            size_t copy_len = trim_end - line_start;
            if (copy_len > 0U) {
                memcpy(out + out_pos, buf + line_start, copy_len);
                out_pos += copy_len;
            }
        }
        if (had_nl || pos < nread) {
            out[out_pos++] = '\n';
        }
    }

    if (!replaced_key) {
        if (out_pos > 0U && out[out_pos - 1U] != '\n') {
            out[out_pos++] = '\n';
        }
        static const char k_off_nl[] = "autostart=0\n";
        memcpy(out + out_pos, k_off_nl, sizeof(k_off_nl) - 1U);
        out_pos += sizeof(k_off_nl) - 1U;
    }

    free(buf);

    FILE *wfp = fopen(cfg_path, "wb");
    if (!wfp) {
        free(out);
        return false;
    }
    size_t written = (out_pos > 0U) ? fwrite(out, 1U, out_pos, wfp) : 0U;
    fflush(wfp);
    fclose(wfp);
    free(out);
    return (written == out_pos);
}

/* Remove all known N-Launcher startup hooks cleanly without directory scan overhead */
static inline void nl_bg_remove_startup_hooks(const char *startup_dir) {
    if (!startup_dir || !startup_dir[0]) return;
    static const char *const k_hook_names[12] = {
        "/nl_boot.tns",
        "/NL_BOOT.TNS",
        "/nl_boot_stub.tns",
        "/NL_BOOT_STUB.TNS",
        "/nl_hotkey.tns",
        "/NL_HOTKEY.TNS",
        "/n-launcher.tns",
        "/N-Launcher.tns",
        "/nlauncher.tns",
        "/NLauncher.tns",
        "/nlauncher_boot.tns",
        "/NLAUNCHER_BOOT.TNS"
    };
    char rm_path[256];
    for (size_t i = 0U; i < sizeof(k_hook_names) / sizeof(k_hook_names[0]); i++) {
        nl_bg_join_path(rm_path, sizeof(rm_path), startup_dir, k_hook_names[i]);
        (void)unlink(rm_path);
    }
}

/* Rewrites global_hotkey=1 -> global_hotkey=0 in cfg_path, preserving 100% of other lines */
static inline bool nl_bg_disable_hotkey_in_cfg(const char *cfg_path) {
    if (!cfg_path || !cfg_path[0]) return false;

    FILE *rfp = fopen(cfg_path, "rb");
    if (!rfp) return false;

    if (fseek(rfp, 0, SEEK_END) != 0) {
        fclose(rfp);
        return false;
    }
    long fsz = ftell(rfp);
    if (fsz < 0 || fsz > 131072L || fseek(rfp, 0, SEEK_SET) != 0) {
        fclose(rfp);
        return false;
    }

    size_t sz = (size_t)fsz;
    char *buf = (char *)malloc(sz + 1U);
    if (!buf) {
        fclose(rfp);
        return false;
    }
    size_t nread = (sz > 0U) ? fread(buf, 1U, sz, rfp) : 0U;
    fclose(rfp);
    buf[nread] = '\0';

    char *out = (char *)malloc(nread + 32U);
    if (!out) {
        free(buf);
        return false;
    }

    size_t out_pos = 0U;
    size_t pos = 0U;
    bool replaced_key = false;

    while (pos < nread) {
        size_t line_start = pos;
        while (pos < nread && buf[pos] != '\n') pos++;
        size_t line_end = pos;
        bool had_nl = (pos < nread && buf[pos] == '\n');
        if (had_nl) pos++;

        size_t trim_end = line_end;
        while (trim_end > line_start && buf[trim_end - 1U] == '\r') {
            trim_end--;
        }
        size_t trim_start = line_start;
        while (trim_start < trim_end && (buf[trim_start] == ' ' || buf[trim_start] == '\t')) {
            trim_start++;
        }

        if (trim_end - trim_start >= 14U &&
            strncmp(buf + trim_start, "global_hotkey=", 14U) == 0) {
            static const char k_off[] = "global_hotkey=0";
            memcpy(out + out_pos, k_off, sizeof(k_off) - 1U);
            out_pos += sizeof(k_off) - 1U;
            replaced_key = true;
        } else {
            size_t copy_len = trim_end - line_start;
            if (copy_len > 0U) {
                memcpy(out + out_pos, buf + line_start, copy_len);
                out_pos += copy_len;
            }
        }
        if (had_nl || pos < nread) {
            out[out_pos++] = '\n';
        }
    }

    if (!replaced_key) {
        if (out_pos > 0U && out[out_pos - 1U] != '\n') {
            out[out_pos++] = '\n';
        }
        static const char k_off_nl[] = "global_hotkey=0\n";
        memcpy(out + out_pos, k_off_nl, sizeof(k_off_nl) - 1U);
        out_pos += sizeof(k_off_nl) - 1U;
    }

    free(buf);

    FILE *wfp = fopen(cfg_path, "wb");
    if (!wfp) {
        free(out);
        return false;
    }
    size_t written = (out_pos > 0U) ? fwrite(out, 1U, out_pos, wfp) : 0U;
    fflush(wfp);
    fclose(wfp);
    free(out);
    return (written == out_pos);
}

/* Rewrites global_hotkey=0 -> global_hotkey=1 (or creates/appends global_hotkey=1) in cfg_path */
static inline bool nl_bg_enable_hotkey_in_cfg(const char *cfg_path) {
    if (!cfg_path || !cfg_path[0]) return false;

    FILE *rfp = fopen(cfg_path, "rb");
    if (!rfp) {
        FILE *nfp = fopen(cfg_path, "wb");
        if (!nfp) return false;
        fprintf(nfp, "global_hotkey=1\n");
        fflush(nfp);
        fclose(nfp);
        return true;
    }

    if (fseek(rfp, 0, SEEK_END) != 0) {
        fclose(rfp);
        return false;
    }
    long fsz = ftell(rfp);
    if (fsz < 0 || fsz > 131072L || fseek(rfp, 0, SEEK_SET) != 0) {
        fclose(rfp);
        return false;
    }

    size_t sz = (size_t)fsz;
    char *buf = (char *)malloc(sz + 1U);
    if (!buf) {
        fclose(rfp);
        return false;
    }
    size_t nread = (sz > 0U) ? fread(buf, 1U, sz, rfp) : 0U;
    fclose(rfp);
    buf[nread] = '\0';

    char *out = (char *)malloc(nread + 32U);
    if (!out) {
        free(buf);
        return false;
    }

    size_t out_pos = 0U;
    size_t pos = 0U;
    bool replaced_key = false;

    while (pos < nread) {
        size_t line_start = pos;
        while (pos < nread && buf[pos] != '\n') pos++;
        size_t line_end = pos;
        bool had_nl = (pos < nread && buf[pos] == '\n');
        if (had_nl) pos++;

        size_t trim_end = line_end;
        while (trim_end > line_start && buf[trim_end - 1U] == '\r') {
            trim_end--;
        }
        size_t trim_start = line_start;
        while (trim_start < trim_end && (buf[trim_start] == ' ' || buf[trim_start] == '\t')) {
            trim_start++;
        }

        if (trim_end - trim_start >= 14U &&
            strncmp(buf + trim_start, "global_hotkey=", 14U) == 0) {
            static const char k_on[] = "global_hotkey=1";
            memcpy(out + out_pos, k_on, sizeof(k_on) - 1U);
            out_pos += sizeof(k_on) - 1U;
            replaced_key = true;
        } else {
            size_t copy_len = trim_end - line_start;
            if (copy_len > 0U) {
                memcpy(out + out_pos, buf + line_start, copy_len);
                out_pos += copy_len;
            }
        }
        if (had_nl || pos < nread) {
            out[out_pos++] = '\n';
        }
    }

    if (!replaced_key) {
        if (out_pos > 0U && out[out_pos - 1U] != '\n') {
            out[out_pos++] = '\n';
        }
        static const char k_on_nl[] = "global_hotkey=1\n";
        memcpy(out + out_pos, k_on_nl, sizeof(k_on_nl) - 1U);
        out_pos += sizeof(k_on_nl) - 1U;
    }

    free(buf);

    FILE *wfp = fopen(cfg_path, "wb");
    if (!wfp) {
        free(out);
        return false;
    }
    size_t written = (out_pos > 0U) ? fwrite(out, 1U, out_pos, wfp) : 0U;
    fflush(wfp);
    fclose(wfp);
    free(out);
    return (written == out_pos);
}

/* Reads global_hotkey=, hotkey_combo=, and launcher_path= from nlauncher.cfg.tns without altering file */
static inline void nl_bg_read_hotkey_cfg_ex(
    const char *cfg_path,
    bool *out_hotkey,
    nl_hk_combo_t *out_combo,
    char *out_target_path,
    size_t target_max
) {
    if (out_hotkey) *out_hotkey = false;
    if (out_combo) *out_combo = NL_HK_COMBO_CTRL_MENU;
    if (!cfg_path || !cfg_path[0]) return;

    FILE *fp = fopen(cfg_path, "r");
    if (!fp) return;

    char line[320];
    while (fgets(line, sizeof(line), fp)) {
        nl_bg_trim_crlf(line);
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "global_hotkey=", 14) == 0) {
            if (out_hotkey) *out_hotkey = (nl_bg_parse_flag_val(p + 14) != 0);
        } else if (strncmp(p, "hotkey_combo=", 13) == 0) {
            if (out_combo) *out_combo = nl_str_to_hk_combo(p + 13);
        } else if (strncmp(p, "launcher_path=", 14) == 0 && p[14] != '\0') {
            if (out_target_path && target_max > 0U) {
                nl_bg_copy_str(out_target_path, target_max, p + 14);
            }
        }
    }
    fclose(fp);
}

static inline void nl_bg_read_hotkey_cfg(
    const char *cfg_path,
    bool *out_hotkey,
    char *out_target_path,
    size_t target_max
) {
    nl_bg_read_hotkey_cfg_ex(cfg_path, out_hotkey, NULL, out_target_path, target_max);
}

/* Manage active session lock (/documents/ndless/nl_active.lock.tns) to prevent global hotkey
 * from firing while N-Launcher or any child game is already running. */
static inline bool nl_bg_set_active_lock(const char *docs_root_in, bool active) {
    char docs_root[160];
    nl_bg_copy_str(docs_root, sizeof(docs_root), (docs_root_in && docs_root_in[0]) ? docs_root_in : "/documents");
    nl_bg_strip_slash(docs_root);

    char lock_path[256];
    nl_bg_join_path(lock_path, sizeof(lock_path), docs_root, "/ndless/nl_active.lock.tns");
    if (!active) {
        (void)unlink(lock_path);
        return true;
    }
    FILE *fp = fopen(lock_path, "wb");
    if (!fp) return false;
    fputs("active=1\n", fp);
    fflush(fp);
    fclose(fp);
    return true;
}

static inline bool nl_bg_is_active_lock_present(const char *docs_root_in) {
    char docs_root[160];
    nl_bg_copy_str(docs_root, sizeof(docs_root), (docs_root_in && docs_root_in[0]) ? docs_root_in : "/documents");
    nl_bg_strip_slash(docs_root);

    char lock_path[256];
    nl_bg_join_path(lock_path, sizeof(lock_path), docs_root, "/ndless/nl_active.lock.tns");
    return nl_bg_file_exists_any(lock_path);
}

/* Validates that an ARM32 instruction word does not use PC (r15) or branch offsets,
 * ensuring it is 100% safe to relocate in a HOOK_INSTALL 2-word prologue trampoline. */
static inline bool nl_arm_instr_is_pc_free(uint32_t ins) {
    if (ins == 0U || ins == 0xFFFFFFFFU || ins == 0xE51FF004U) return false;
    /* Reject unconditional 0xF... extension space */
    if (((ins >> 28) & 0xFU) == 0xFU) return false;
    uint32_t top3 = (ins >> 25) & 0x7U;
    /* Reject B / BLbranches (0b101) and SWI/coprocessor (0b110, 0b111) */
    if (top3 == 5U || top3 == 6U || top3 == 7U) return false;
    /* Block data transfer (STM/LDM, top3 == 4): check Rn != 15 and PC bit (bit 15) == 0 */
    if (top3 == 4U) {
        if (((ins >> 16) & 0xFU) == 15U) return false;
        if ((ins & 0x8000U) != 0U) return false;
        return true;
    }
    /* Data processing or single data transfer: check Rn (19:16) != 15 and Rd (15:12) != 15 */
    if (((ins >> 16) & 0xFU) == 15U) return false;
    if (((ins >> 12) & 0xFU) == 15U) return false;
    /* Reject BX / BLX register branches */
    if ((ins & 0x0FFFFFF0U) == 0x012FFF10U || (ins & 0x0FFFFFF0U) == 0x012FFF30U) return false;
    /* If register operand (top3 == 0 or top3 == 3), check Rm (3:0) != 15 */
    if ((top3 == 0U || top3 == 3U) && ((ins & 0xFU) == 15U)) return false;
    return true;
}

/* Detects ARM32 PC-relative immediate word load: ldr<cond> Rd, [pc, #+-imm12] (Rd != pc).
 * Very common in TI-OS 6.2.0 function prologues (e.g. loading global/BSS pointer at +0 or +4). */
static inline bool nl_arm_is_pc_rel_ldr(uint32_t ins) {
    if (((ins >> 28) & 0xFU) == 0xFU) return false;
    if ((ins & 0x0F7F0000U) != 0x051F0000U) return false;
    uint32_t rd = (ins >> 12) & 0xFU;
    return (rd != 15U);
}

static inline bool nl_arm_instr_is_relocatable(uint32_t ins) {
    return nl_arm_instr_is_pc_free(ins) || nl_arm_is_pc_rel_ldr(ins);
}

/* Builds a 6-word ARM32 trampoline relocating 2 prologue words from orig_site:
 *   [0] relocated w0 (if PC-rel ldr -> rewritten to [pc, #8] -> slot [4])
 *   [1] relocated w1 (if PC-rel ldr -> rewritten to [pc, #8] -> slot [5])
 *   [2] 0xE51FF004 (ldr pc, [pc, #-4])
 *   [3] orig_site + 8
 *   [4] literal pool word for w0 (if PC-rel ldr)
 *   [5] literal pool word for w1 (if PC-rel ldr)
 * If literal_reader is NULL, reads directly from (const uint32_t *)target_addr on hardware. */
typedef uint32_t (*nl_arm_read_u32_fn)(uintptr_t addr, void *ctx);

static inline bool nl_arm_relocate_prologue(
    uintptr_t orig_site,
    uint32_t w0,
    uint32_t w1,
    nl_arm_read_u32_fn reader,
    void *reader_ctx,
    uint32_t out_trampoline[6]
) {
    if (!out_trampoline || (orig_site & 3U) != 0U) return false;
    if (!nl_arm_instr_is_relocatable(w0) || !nl_arm_instr_is_relocatable(w1)) return false;

    uint32_t src[2] = { w0, w1 };
    out_trampoline[4] = 0U;
    out_trampoline[5] = 0U;

    for (unsigned i = 0U; i < 2U; i++) {
        uint32_t ins = src[i];
        if (nl_arm_instr_is_pc_free(ins)) {
            out_trampoline[i] = ins;
        } else if (nl_arm_is_pc_rel_ldr(ins)) {
            uintptr_t orig_pc = orig_site + (uintptr_t)(i * 4U) + 8U;
            uint32_t imm12 = ins & 0xFFFU;
            uintptr_t lit_addr = (ins & (1U << 23)) ? (orig_pc + imm12) : (orig_pc - imm12);
            if ((lit_addr & 3U) != 0U) return false;
            uint32_t lit_val = reader
                ? reader(lit_addr, reader_ctx)
                : (*(const volatile uint32_t *)lit_addr);
            /* Rewrite to ldr<cond> Rd, [pc, #8]:
             *  - at slot 0 (PC = tramp+8)  -> tramp+16 = &out_trampoline[4]
             *  - at slot 1 (PC = tramp+12) -> tramp+20 = &out_trampoline[5] */
            out_trampoline[i] = (ins & 0xFF7FF000U) | 0x00800008U;
            out_trampoline[4U + i] = lit_val;
        } else {
            return false;
        }
    }

    out_trampoline[2] = 0xE51FF004U; /* ldr pc, [pc, #-4] */
    out_trampoline[3] = (uint32_t)(orig_site + 8U);
    return true;
}

static inline uint32_t nl_hk_decode_imm12(uint32_t instr) {
    uint32_t rot = ((instr >> 8) & 0xFU) * 2U;
    uint32_t imm = instr & 0xFFU;
    if (rot == 0U) return imm;
    return (imm >> rot) | (imm << (32U - rot));
}

static inline const uintptr_t *nl_hk_decode_pc_rel_slot(const uint32_t *instr_ptr, uint32_t instr) {
    uintptr_t pc_val = (uintptr_t)instr_ptr + 8U;
    uint32_t off = instr & 0xFFFU;
    uintptr_t addr = (instr & (1U << 23)) ? (pc_val + off) : (pc_val - off);
    return (const uintptr_t *)addr;
}

/* Parse Ndless ints_swi_handler (identified by "NEXT" magic 8 bytes before entry)
 * to extract both sc_ext_table (offset 0x200000) and sc_addrs_ptr (offset 0x400000). */
static inline bool nl_hotkey_parse_swi_handler(
    const uint32_t *swi_handler,
    uintptr_t **out_sc_ext,
    uintptr_t **out_os_sc
) {
    if (out_sc_ext) *out_sc_ext = NULL;
    if (out_os_sc)  *out_os_sc = NULL;
    if (!swi_handler || (((uintptr_t)swi_handler) & 3U) != 0U) return false;
    if (*(swi_handler - 2) != 0x4E455854U) return false; /* "NEXT" */

    uintptr_t *ext_found = NULL;
    uintptr_t *os_found = NULL;

    for (unsigned i = 0U; i < 63U; i++) {
        uint32_t w0 = swi_handler[i];
        uint32_t w1 = swi_handler[i + 1U];

        /* Match: cmp r1, #0x200000 (__SYSCALLS_ISEXT) + ldreq r2, [pc, #+-imm] */
        if (!ext_found &&
            (w0 & 0xFFFFF000U) == 0xE3510000U &&
            nl_hk_decode_imm12(w0) == 0x200000U &&
            (w1 & 0xFF7FF000U) == 0x051F2000U) {
            const uintptr_t *pool = nl_hk_decode_pc_rel_slot(&swi_handler[i + 1U], w1);
            ext_found = (uintptr_t *)(*pool);
        }

        /* Match: cmp r1, #0x400000 (__SYSCALLS_ISEMU) + ldreq r2, =emu + ldrne r2, sc_addrs_ptr */
        if (!os_found && i < 62U &&
            (w0 & 0xFFFFF000U) == 0xE3510000U &&
            nl_hk_decode_imm12(w0) == 0x400000U) {
            uint32_t w2 = swi_handler[i + 2U];
            if ((w2 & 0xFF7FF000U) == 0x151F2000U) {
                const uintptr_t *pool = nl_hk_decode_pc_rel_slot(&swi_handler[i + 2U], w2);
                os_found = (uintptr_t *)(*pool);
            }
        }

        /* Fallback match: ldrne r2, [pc, #+-imm] immediately followed by ldr r0, [r2, r0, lsl #2] */
        if (!os_found && w1 == 0xE7920100U && (w0 & 0xFF7FF000U) == 0x151F2000U) {
            const uintptr_t *pool = nl_hk_decode_pc_rel_slot(&swi_handler[i], w0);
            os_found = (uintptr_t *)(*pool);
        }

        if (ext_found && os_found) break;
    }

    if (out_sc_ext) *out_sc_ext = ext_found;
    if (out_os_sc)  *out_os_sc  = os_found;
    return (os_found != NULL);
}

static inline bool nl_hk_ptr_in_ram_range(uintptr_t addr, uintptr_t min_addr, uintptr_t max_addr) {
    if (min_addr == 0U && max_addr == 0U) {
        return (addr != 0U && (addr & 3U) == 0U);
    }
    return (addr >= min_addr && addr < max_addr && (addr & 3U) == 0U);
}

/* Inspect a hooked function address (fn_addr or fn_addr+4) for N-Launcher's "NLHK" resident control block. */
static inline nl_hk_ctrl_t *nl_hotkey_find_live_ctrl_at(
    uintptr_t fn_addr,
    uintptr_t ram_min,
    uintptr_t ram_max,
    bool *out_third_party_hook
) {
    if (out_third_party_hook) *out_third_party_hook = false;
    if (!nl_hk_ptr_in_ram_range(fn_addr, ram_min, ram_max)) return NULL;

    const volatile uint32_t *w = (const volatile uint32_t *)fn_addr;
    for (unsigned off = 0U; off <= 1U; off++) {
        if (w[off] == 0xE51FF004U) {
            uintptr_t handler = (uintptr_t)w[off + 1U];
            if (nl_hk_ptr_in_ram_range(handler, ram_min, ram_max)) {
                const volatile uint32_t *hdr = (const volatile uint32_t *)(handler - 8U);
                if (hdr[1] == NL_HK_CTRL_MAGIC) {
                    nl_hk_ctrl_t *ctrl = (nl_hk_ctrl_t *)(uintptr_t)hdr[0];
                    if (nl_hk_ptr_in_ram_range((uintptr_t)ctrl, ram_min, ram_max) &&
                        ctrl->magic == NL_HK_CTRL_MAGIC &&
                        ctrl->version == NL_HK_CTRL_VERSION) {
                        return ctrl;
                    }
                }
            }
            if (out_third_party_hook) *out_third_party_hook = true;
            return NULL;
        }
    }
    return NULL;
}

static inline bool nl_hk_combo_matches(nl_hk_combo_t combo, bool ctrl_pressed, bool shift_pressed, bool menu_pressed) {
    if (!menu_pressed) return false;
    switch (combo) {
        case NL_HK_COMBO_SHIFT_MENU:
            return shift_pressed && !ctrl_pressed;
        case NL_HK_COMBO_CTRL_SHIFT_MENU:
            return ctrl_pressed && shift_pressed;
        case NL_HK_COMBO_CTRL_MENU:
        default:
            return ctrl_pressed && !shift_pressed;
    }
}

/* Sticky + Simultaneous TI-Nspire Modifier Evaluator:
 * TI-Nspire users routinely tap Shift or Ctrl, release it, then tap Menu (calculator sticky modifier),
 * OR hold Shift+Menu / Ctrl+Menu simultaneously. Any other key cancels the sticky latch immediately. */
#define NL_HK_STICKY_MAX_POLLS 12

typedef struct {
    int  ctrl_sticky;
    int  shift_sticky;
    bool prev_ctrl;
    bool prev_shift;
} nl_hk_sticky_state_t;

static inline void nl_hk_sticky_reset(nl_hk_sticky_state_t *st) {
    if (!st) return;
    st->ctrl_sticky = 0;
    st->shift_sticky = 0;
    st->prev_ctrl = false;
    st->prev_shift = false;
}

static inline bool nl_hk_sticky_eval(
    nl_hk_sticky_state_t *st,
    nl_hk_combo_t combo,
    bool ctrl_down,
    bool shift_down,
    bool menu_down,
    bool other_key_down
) {
    if (!st) {
        return nl_hk_combo_matches(combo, ctrl_down, shift_down, menu_down);
    }

    if (ctrl_down) {
        st->ctrl_sticky = NL_HK_STICKY_MAX_POLLS;
    } else if (st->prev_ctrl) {
        st->ctrl_sticky = NL_HK_STICKY_MAX_POLLS;
    } else if (st->ctrl_sticky > 0) {
        st->ctrl_sticky--;
    }

    if (shift_down) {
        st->shift_sticky = NL_HK_STICKY_MAX_POLLS;
    } else if (st->prev_shift) {
        st->shift_sticky = NL_HK_STICKY_MAX_POLLS;
    } else if (st->shift_sticky > 0) {
        st->shift_sticky--;
    }

    if (other_key_down) {
        st->ctrl_sticky = 0;
        st->shift_sticky = 0;
    }

    st->prev_ctrl = ctrl_down;
    st->prev_shift = shift_down;

    if (!menu_down) {
        return false;
    }

    /* Direct simultaneous match always wins */
    if (nl_hk_combo_matches(combo, ctrl_down, shift_down, true)) {
        st->ctrl_sticky = 0;
        st->shift_sticky = 0;
        return true;
    }

    /* Sticky modifier match (e.g. Shift released -> Menu pressed) */
    bool eff_ctrl  = ctrl_down  || (st->ctrl_sticky > 0);
    bool eff_shift = shift_down || (st->shift_sticky > 0);
    if (nl_hk_combo_matches(combo, eff_ctrl, eff_shift, true)) {
        st->ctrl_sticky = 0;
        st->shift_sticky = 0;
        return true;
    }
    return false;
}

typedef bool (*nl_stub_file_ok_fn)(const char *path);
typedef int  (*nl_stub_exec_fn)(const char *path, int argc, char *argv[]);

/* Evaluates whether the global TI-OS hotkey combo is permitted to launch N-Launcher */
static inline bool nl_hotkey_should_trigger_ex(
    nl_hk_combo_t combo,
    bool ctrl_pressed,
    bool shift_pressed,
    bool menu_pressed,
    bool esc_or_on_pressed,
    bool is_startup,
    bool in_progress_lock,
    const char *docs_root_in,
    nl_stub_file_ok_fn file_ok_fn,
    char *out_target_path,
    size_t target_max
) {
    if (out_target_path && target_max > 0U) {
        out_target_path[0] = '\0';
    }
    if (!nl_hk_combo_matches(combo, ctrl_pressed, shift_pressed, menu_pressed)) return false;
    if (esc_or_on_pressed || is_startup || in_progress_lock) return false;

    char docs_root[160];
    nl_bg_copy_str(docs_root, sizeof(docs_root), (docs_root_in && docs_root_in[0]) ? docs_root_in : "/documents");
    nl_bg_strip_slash(docs_root);

    /* Never trigger while N-Launcher or a child game is active */
    if (nl_bg_is_active_lock_present(docs_root)) {
        return false;
    }

    char cfg_path[256];
    char fallback_cfg[256];
    char guard_path[256];
    char target_path[256];

    nl_bg_join_path(cfg_path, sizeof(cfg_path), docs_root, "/ndless/nlauncher.cfg.tns");
    nl_bg_join_path(fallback_cfg, sizeof(fallback_cfg), docs_root, "/nlauncher.cfg.tns");
    nl_bg_join_path(guard_path, sizeof(guard_path), docs_root, "/ndless/nl_boot_guard.cfg.tns");
    nl_bg_join_path(target_path, sizeof(target_path), docs_root, "/ndless/N-Launcher.tns");

    /* Never trigger if boot-guard has an unstabilized boot or recovery state */
    nl_boot_guard_state_t gs;
    if (nl_bg_read_guard_state(guard_path, &gs)) {
        if (gs.pending != 0 || gs.recovery != 0) {
            return false;
        }
    }

    bool hk_enabled = false;
    nl_hk_combo_t cfg_combo = NL_HK_COMBO_CTRL_MENU;
    nl_bg_read_hotkey_cfg_ex(cfg_path, &hk_enabled, &cfg_combo, target_path, sizeof(target_path));
    if (!hk_enabled && !nl_bg_file_exists_any(cfg_path)) {
        nl_bg_read_hotkey_cfg_ex(fallback_cfg, &hk_enabled, &cfg_combo, target_path, sizeof(target_path));
    }
    if (!hk_enabled) {
        return false;
    }

    if (!file_ok_fn || !file_ok_fn(target_path)) {
        char alt_path[256];
        nl_bg_join_path(alt_path, sizeof(alt_path), docs_root, "/N-Launcher.tns");
        if (!file_ok_fn || !file_ok_fn(alt_path)) {
            return false;
        }
        nl_bg_copy_str(target_path, sizeof(target_path), alt_path);
    }

    if (out_target_path && target_max > 0U) {
        nl_bg_copy_str(out_target_path, target_max, target_path);
    }
    return true;
}

static inline bool nl_hotkey_should_trigger(
    bool ctrl_pressed,
    bool menu_pressed,
    bool esc_or_on_pressed,
    bool is_startup,
    bool in_progress_lock,
    const char *docs_root_in,
    nl_stub_file_ok_fn file_ok_fn,
    char *out_target_path,
    size_t target_max
) {
    return nl_hotkey_should_trigger_ex(
        NL_HK_COMBO_CTRL_MENU,
        ctrl_pressed,
        false,
        menu_pressed,
        esc_or_on_pressed,
        is_startup,
        in_progress_lock,
        docs_root_in,
        file_ok_fn,
        out_target_path,
        target_max
    );
}

/* Auto-disable autostart & hotkey safely across config, guard state & startup directory while preserving all user data */
static inline void nl_bg_trigger_recovery_disable(
    const char *primary_cfg,
    const char *fallback_cfg,
    const char *guard_path,
    const char *startup_dir,
    const char *reason
) {
    if (primary_cfg && primary_cfg[0]) {
        (void)nl_bg_disable_autostart_in_cfg(primary_cfg);
        (void)nl_bg_disable_hotkey_in_cfg(primary_cfg);
    }
    if (fallback_cfg && fallback_cfg[0] &&
        (!primary_cfg || strcmp(fallback_cfg, primary_cfg) != 0) &&
        nl_bg_file_exists_any(fallback_cfg)) {
        (void)nl_bg_disable_autostart_in_cfg(fallback_cfg);
        (void)nl_bg_disable_hotkey_in_cfg(fallback_cfg);
    }
    (void)nl_bg_write_guard_state(guard_path, 0, 1, 1, reason);
    nl_bg_remove_startup_hooks(startup_dir);
}

/* Deterministic Autostart Bootstrapper Flow (shared verbatim between nl_boot_stub.c & test_runner.c) */
static inline nl_stub_outcome_t nl_boot_stub_execute(
    bool is_startup,
    bool recovery_key_held,
    const char *docs_root_in,
    nl_stub_file_ok_fn file_ok_fn,
    nl_stub_exec_fn exec_fn,
    char *out_launched_path,
    size_t out_launched_max
) {
    if (out_launched_path && out_launched_max > 0U) {
        out_launched_path[0] = '\0';
    }

    if (!is_startup) {
        return NL_STUB_SKIP_NOT_STARTUP;
    }

    char docs_root[160];
    nl_bg_copy_str(docs_root, sizeof(docs_root), (docs_root_in && docs_root_in[0]) ? docs_root_in : "/documents");
    nl_bg_strip_slash(docs_root);

    /* Clear any stale session lock at cold boot */
    (void)nl_bg_set_active_lock(docs_root, false);

    char cfg_path[256];
    char guard_path[256];
    char startup_dir[256];
    char target_path[256];
    char fallback_cfg_path[256];

    nl_bg_join_path(cfg_path, sizeof(cfg_path), docs_root, "/ndless/nlauncher.cfg.tns");
    nl_bg_join_path(guard_path, sizeof(guard_path), docs_root, "/ndless/nl_boot_guard.cfg.tns");
    nl_bg_join_path(startup_dir, sizeof(startup_dir), docs_root, "/ndless/startup");
    nl_bg_join_path(target_path, sizeof(target_path), docs_root, "/ndless/N-Launcher.tns");
    nl_bg_join_path(fallback_cfg_path, sizeof(fallback_cfg_path), docs_root, "/nlauncher.cfg.tns");

    bool autostart_enabled = false;
    nl_bg_read_launcher_cfg(cfg_path, &autostart_enabled, target_path, sizeof(target_path));
    if (!autostart_enabled && !nl_bg_file_exists_any(cfg_path)) {
        nl_bg_read_launcher_cfg(fallback_cfg_path, &autostart_enabled, target_path, sizeof(target_path));
    }

    /* Check recovery key first: even if autostart was enabled, holding ESC/ON cleanly aborts & disables */
    if (recovery_key_held) {
        if (autostart_enabled) {
            nl_bg_trigger_recovery_disable(cfg_path, fallback_cfg_path, guard_path, startup_dir, "manual_key");
        }
        return NL_STUB_ABORT_RECOVERY_KEY;
    }

    if (!autostart_enabled) {
        return NL_STUB_SKIP_DISABLED;
    }

    /* Check crash-loop guard BEFORE running target */
    nl_boot_guard_state_t g;
    nl_bg_read_guard_state(guard_path, &g);
    if (g.pending != 0) {
        /* Previous startup crashed before main-loop stability: disable autostart permanently until user re-enables */
        nl_bg_trigger_recovery_disable(cfg_path, fallback_cfg_path, guard_path, startup_dir, "crash_detected");
        return NL_STUB_ABORT_CRASH_GUARD;
    }

    if (!file_ok_fn || !file_ok_fn(target_path)) {
        char alt_path[256];
        nl_bg_join_path(alt_path, sizeof(alt_path), docs_root, "/N-Launcher.tns");
        if (!file_ok_fn || !file_ok_fn(alt_path)) {
            nl_bg_trigger_recovery_disable(cfg_path, fallback_cfg_path, guard_path, startup_dir, "missing_target");
            return NL_STUB_ABORT_MISSING_TARGET;
        }
        nl_bg_copy_str(target_path, sizeof(target_path), alt_path);
    }

    /* Record pending launch attempt on flash BEFORE calling nl_exec() */
    (void)nl_bg_write_guard_state(guard_path, 1, 0, 0, "booting");

    if (out_launched_path && out_launched_max > 0U) {
        nl_bg_copy_str(out_launched_path, out_launched_max, target_path);
    }

    int exec_rc = exec_fn ? exec_fn(target_path, 0, NULL) : 0xDEAD;
    if (exec_rc == 0xDEAD || exec_rc == 0xBEEF || exec_rc < 0) {
        nl_bg_trigger_recovery_disable(cfg_path, fallback_cfg_path, guard_path, startup_dir, "exec_failed");
        return NL_STUB_ABORT_EXEC_FAILED;
    }

    /* Clean return from N-Launcher requires exec_rc == 0 AND pending cleared to 0
     * (either after NL_BOOT_STABILIZE_FRAMES, before game launch, or at clean ESC/PowerOff exit).
     * If pending != 0 or exec_rc != 0, treat it as an unstabilized failure and trigger recovery. */
    nl_boot_guard_state_t after;
    nl_bg_read_guard_state(guard_path, &after);
    if (exec_rc != 0 || after.pending != 0) {
        nl_bg_trigger_recovery_disable(cfg_path, fallback_cfg_path, guard_path, startup_dir, "crash_detected");
        return NL_STUB_ABORT_CRASH_GUARD;
    }
    return NL_STUB_OK_CLEAN;
}

#ifdef __cplusplus
}
#endif

#endif /* BOOT_GUARD_CORE_H */
