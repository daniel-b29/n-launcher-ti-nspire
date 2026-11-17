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

typedef struct {
    int  pending;
    int  recovery;
    int  disabled;
    char reason[32];
} nl_boot_guard_state_t;

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

/* Reads global_hotkey= and launcher_path= from nlauncher.cfg.tns without altering file */
static inline void nl_bg_read_hotkey_cfg(
    const char *cfg_path,
    bool *out_hotkey,
    char *out_target_path,
    size_t target_max
) {
    if (out_hotkey) *out_hotkey = false;
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
        } else if (strncmp(p, "launcher_path=", 14) == 0 && p[14] != '\0') {
            if (out_target_path && target_max > 0U) {
                nl_bg_copy_str(out_target_path, target_max, p + 14);
            }
        }
    }
    fclose(fp);
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

typedef bool (*nl_stub_file_ok_fn)(const char *path);
typedef int  (*nl_stub_exec_fn)(const char *path, int argc, char *argv[]);

/* Evaluates whether the global TI-OS hotkey (Ctrl + Menu) is permitted to launch N-Launcher */
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
    if (out_target_path && target_max > 0U) {
        out_target_path[0] = '\0';
    }
    if (!ctrl_pressed || !menu_pressed) return false;
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
    nl_bg_read_hotkey_cfg(cfg_path, &hk_enabled, target_path, sizeof(target_path));
    if (!hk_enabled && !nl_bg_file_exists_any(cfg_path)) {
        nl_bg_read_hotkey_cfg(fallback_cfg, &hk_enabled, target_path, sizeof(target_path));
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
