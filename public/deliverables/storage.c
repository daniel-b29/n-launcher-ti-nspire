/****************************************************************************
 * N-Launcher — storage.c
 * Persistent Configuration, Stats, Shortcuts & Ndless Autostart Management
 * Fault-tolerant against missing, truncated, or corrupted config files
 ****************************************************************************/

#ifdef __arm__
#include <os.h>
#include <syscall-list.h>
#endif

#include "nlauncher.h"
#include "boot_guard_core.h"
#include "hotkey_stub_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int str_icmp_s(const char *a, const char *b) {
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

static char *trim_ws(char *s) {
    if (!s) return (char *)"";
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0U && (s[len - 1U] == ' ' || s[len - 1U] == '\t' || s[len - 1U] == '\n' || s[len - 1U] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static void ensure_parent_dirs(const char *filepath) {
    if (!filepath) return;
    char buf[NL_MAX_PATH_LEN];
    size_t len = strlen(filepath);
    if (len == 0U || len >= sizeof(buf)) return;
    memcpy(buf, filepath, len + 1U);

    for (size_t i = 1U; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = '\0';
            (void)mkdir(buf, 0777);
            buf[i] = saved;
        }
    }
}

static void ensure_dir_recursive(const char *dir_path) {
    if (!dir_path || dir_path[0] == '\0') return;
    char probe[NL_MAX_PATH_LEN + 4U];
    snprintf(probe, sizeof(probe), "%.*s/x", (int)(sizeof(probe) - 4U), dir_path);
    ensure_parent_dirs(probe);
}

/* ============================================================================
 * IEEE 802.3 CRC32 & Persistent Binary Library + Pre-Decoded Icon Cache
 * (/documents/ndless/nlauncher.cache.tns)
 * ============================================================================ */

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
    static const uint32_t k_crc_nibble[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
    };
    const uint8_t *p = (const uint8_t *)data;
    if (!p) return crc;
    for (size_t i = 0U; i < len; i++) {
        crc = (crc >> 4) ^ k_crc_nibble[(crc ^ (p[i] >> 0)) & 0x0FU];
        crc = (crc >> 4) ^ k_crc_nibble[(crc ^ (p[i] >> 4)) & 0x0FU];
    }
    return crc;
}

uint32_t nl_crc32_compute(const void *data, size_t len) {
    return ~crc32_update(0xFFFFFFFFU, data, len);
}

static void build_cache_game_rec(const nl_game_t *g, nl_cache_game_rec_t *rec) {
    memset(rec, 0, sizeof(*rec));
    snprintf(rec->title, sizeof(rec->title), "%s", g->title);
    snprintf(rec->author, sizeof(rec->author), "%s", g->author);
    snprintf(rec->path, sizeof(rec->path), "%s", g->path);
    snprintf(rec->filename, sizeof(rec->filename), "%s", g->filename);
    rec->size_bytes    = g->size_bytes;
    rec->bin_format    = (uint32_t)g->bin_format;
    rec->exec_version  = g->exec_version;
    rec->uses_lcd_blit = g->uses_lcd_blit ? 1U : 0U;
    rec->supports_hww  = g->supports_hww ? 1U : 0U;
    rec->accent_color  = g->accent_color;
    rec->added_order   = g->added_order;

    if (g->icon_source != NL_ICON_NONE &&
        g->icon_pixels != NULL &&
        g->icon_w > 0U && g->icon_w <= NL_ICON_MAX_DIM &&
        g->icon_h > 0U && g->icon_h <= NL_ICON_MAX_DIM) {
        rec->icon_source    = (uint32_t)g->icon_source;
        rec->icon_w         = g->icon_w;
        rec->icon_h         = g->icon_h;
        rec->has_icon_alpha = (g->icon_alpha != NULL) ? 1U : 0U;
    } else {
        rec->icon_source    = (uint32_t)NL_ICON_NONE;
        rec->icon_w         = 0U;
        rec->icon_h         = 0U;
        rec->has_icon_alpha = 0U;
    }
}

static void build_tmp_path(const char *path, char *out_tmp, size_t max_len) {
    size_t len = strlen(path);
    if (len > 4U && str_icmp_s(path + len - 4U, ".tns") == 0) {
        snprintf(out_tmp, max_len, "%.*s.tmp.tns", (int)(len - 4U), path);
    } else {
        snprintf(out_tmp, max_len, "%.*s.tmp.tns",
                 (int)(max_len > 10U ? max_len - 10U : 0U), path);
    }
}

static bool commit_tmp_file_atomic(const char *tmp_path, const char *final_path) {
    if (!tmp_path || !final_path) return false;
    /* Verify temp file exists and is non-empty BEFORE touching an existing valid final_path */
    struct stat tst;
    if (stat(tmp_path, &tst) != 0 || !S_ISREG(tst.st_mode) || tst.st_size <= 0) {
        return false;
    }
    /* POSIX/TI-Nspire FAT rename may fail if destination exists -> remove then rename */
    (void)unlink(final_path);
    if (rename(tmp_path, final_path) == 0) {
        return true;
    }
    /* Fallback copy if rename failed across filesystems */
    FILE *src = fopen(tmp_path, "rb");
    if (!src) return false;
    FILE *dst = fopen(final_path, "wb");
    if (!dst) {
        fclose(src);
        return false;
    }
    uint8_t buf[256];
    size_t nr;
    bool ok = true;
    while ((nr = fread(buf, 1U, sizeof(buf), src)) > 0U) {
        if (fwrite(buf, 1U, nr, dst) != nr) {
            ok = false;
            break;
        }
    }
    if (fflush(dst) != 0 || ferror(dst) != 0) ok = false;
    fclose(dst);
    fclose(src);
    (void)unlink(tmp_path);
    return ok;
}

/* ============================================================================
 * Icon-Independent Persistent Library & Folders Index
 * (/documents/ndless/nlauncher.lib.tns)
 * ============================================================================ */

static void sanitize_pipe_field(const char *src, char *dst, size_t max_len) {
    if (!dst || max_len == 0U) return;
    size_t o = 0U;
    if (src) {
        for (size_t i = 0U; src[i] != '\0' && o + 1U < max_len; i++) {
            char c = src[i];
            if (c == '|' || c == '\n' || c == '\r') c = ' ';
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
}

typedef struct {
    char tmp_path[NL_MAX_PATH_LEN];
    char line[512];
    char s_path[NL_MAX_PATH_LEN];
    char s_file[NL_MAX_NAME_LEN];
    char s_title[NL_MAX_NAME_LEN];
    char s_auth[NL_MAX_AUTHOR_LEN];
} nl_lib_save_scratch_t;

bool nl_save_library_index(const nl_state_t *state, const char *lib_path) {
    if (!state || !lib_path || lib_path[0] == '\0') return false;
    if (state->total_games < 0 || state->total_games > NL_MAX_GAMES) return false;

    ensure_parent_dirs(lib_path);

    nl_lib_save_scratch_t *sc = (nl_lib_save_scratch_t *)calloc(1U, sizeof(nl_lib_save_scratch_t));
    if (!sc) return false;

    build_tmp_path(lib_path, sc->tmp_path, sizeof(sc->tmp_path));

    FILE *fp = fopen(sc->tmp_path, "wb");
    if (!fp) {
        free(sc);
        return false;
    }

    uint32_t crc = 0xFFFFFFFFU;
    int n = snprintf(sc->line, sizeof(sc->line), "# N-Launcher Library Index v%u\nversion=%u\n",
                     (unsigned)NL_LIB_VERSION, (unsigned)NL_LIB_VERSION);
    if (n <= 0 || fwrite(sc->line, 1U, (size_t)n, fp) != (size_t)n) {
        fclose(fp); (void)unlink(sc->tmp_path); free(sc); return false;
    }
    crc = crc32_update(crc, sc->line, (size_t)n);

    if (state->sys.docs_root[0] != '\0') {
        n = snprintf(sc->line, sizeof(sc->line), "docs_root=%s\n", state->sys.docs_root);
        if (n <= 0 || fwrite(sc->line, 1U, (size_t)n, fp) != (size_t)n) {
            fclose(fp); (void)unlink(sc->tmp_path); free(sc); return false;
        }
        crc = crc32_update(crc, sc->line, (size_t)n);
    }

    for (int f = 0; f < state->folders.count && f < NL_MAX_GAME_FOLDERS; f++) {
        if (state->folders.paths[f][0] == '\0') continue;
        n = snprintf(sc->line, sizeof(sc->line), "folder=%s\n", state->folders.paths[f]);
        if (n <= 0 || fwrite(sc->line, 1U, (size_t)n, fp) != (size_t)n) {
            fclose(fp); (void)unlink(sc->tmp_path); free(sc); return false;
        }
        crc = crc32_update(crc, sc->line, (size_t)n);
    }

    for (int i = 0; i < state->total_games; i++) {
        const nl_game_t *g = &state->games[i];
        sanitize_pipe_field(g->path, sc->s_path, sizeof(sc->s_path));
        sanitize_pipe_field(g->filename, sc->s_file, sizeof(sc->s_file));
        sanitize_pipe_field(g->title, sc->s_title, sizeof(sc->s_title));
        sanitize_pipe_field(g->author, sc->s_auth, sizeof(sc->s_auth));

        n = snprintf(sc->line, sizeof(sc->line), "game=%s|%s|%s|%s|%u|%u|%u|%u|%u|%u|%u\n",
                     sc->s_path, sc->s_file, sc->s_title, sc->s_auth,
                     (unsigned)g->size_bytes,
                     (unsigned)g->bin_format,
                     (unsigned)g->exec_version,
                     g->uses_lcd_blit ? 1U : 0U,
                     g->supports_hww ? 1U : 0U,
                     (unsigned)g->accent_color,
                     (unsigned)g->added_order);
        if (n <= 0 || n >= (int)sizeof(sc->line) || fwrite(sc->line, 1U, (size_t)n, fp) != (size_t)n) {
            fclose(fp); (void)unlink(sc->tmp_path); free(sc); return false;
        }
        crc = crc32_update(crc, sc->line, (size_t)n);
    }

    uint32_t final_crc = ~crc;
    n = snprintf(sc->line, sizeof(sc->line), "crc32=%08X\n", (unsigned)final_crc);
    if (n <= 0 || fwrite(sc->line, 1U, (size_t)n, fp) != (size_t)n) {
        fclose(fp); (void)unlink(sc->tmp_path); free(sc); return false;
    }

    if (fflush(fp) != 0 || ferror(fp) != 0 || fclose(fp) != 0) {
        (void)unlink(sc->tmp_path);
        free(sc);
        return false;
    }

    bool committed = commit_tmp_file_atomic(sc->tmp_path, lib_path);
    free(sc);
    return committed;
}

bool nl_load_library_index(nl_state_t *state, const char *lib_path) {
    if (!state || !lib_path || lib_path[0] == '\0') return false;
    FILE *fp = fopen(lib_path, "rb");
    if (!fp) return false;

    nl_game_t *temp_games = (nl_game_t *)calloc(NL_MAX_GAMES, sizeof(nl_game_t));
    nl_folder_list_t *temp_folders = (nl_folder_list_t *)calloc(1U, sizeof(nl_folder_list_t));
    char *raw_line = (char *)malloc(512U);
    if (!temp_games || !temp_folders || !raw_line) {
        free(raw_line);
        free(temp_games);
        free(temp_folders);
        fclose(fp);
        return false;
    }

    int temp_count = 0;
    uint32_t running_crc = 0xFFFFFFFFU;
    uint32_t file_crc = 0U;
    bool have_crc = false;
    bool have_version = false;

    while (fgets(raw_line, 512, fp)) {
        if (strncmp(raw_line, "crc32=", 6) == 0) {
            file_crc = (uint32_t)strtoul(raw_line + 6, NULL, 16);
            have_crc = true;
            break;
        }
        size_t rlen = strlen(raw_line);
        running_crc = crc32_update(running_crc, raw_line, rlen);

        while (rlen > 0U && (raw_line[rlen - 1U] == '\n' || raw_line[rlen - 1U] == '\r')) {
            raw_line[--rlen] = '\0';
        }
        if (raw_line[0] == '#' || raw_line[0] == '\0') continue;

        char *eq = strchr(raw_line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = trim_ws(raw_line);
        char *val = trim_ws(eq + 1);

        if (strcmp(key, "version") == 0) {
            unsigned v = (unsigned)strtoul(val, NULL, 10);
            if (v >= 1U && v <= NL_LIB_VERSION) have_version = true;
        } else if (strcmp(key, "folder") == 0) {
            if (val[0] != '\0') (void)nl_folder_add(temp_folders, val);
        } else if (strcmp(key, "game") == 0 && temp_count < NL_MAX_GAMES) {
            char *parts[11];
            memset(parts, 0, sizeof(parts));
            int pc = 0;
            char *cur = val;
            parts[pc++] = cur;
            while (pc < 11) {
                char *sep = strchr(cur, '|');
                if (!sep) break;
                *sep = '\0';
                cur = sep + 1;
                parts[pc++] = cur;
            }
            if (pc < 6 || parts[0][0] == '\0' || parts[1][0] == '\0') continue;

            nl_game_t *g = &temp_games[temp_count];
            memset(g, 0, sizeof(*g));
            snprintf(g->path, sizeof(g->path), "%s", parts[0]);
            snprintf(g->filename, sizeof(g->filename), "%s", parts[1]);
            snprintf(g->title, sizeof(g->title), "%s", parts[2][0] ? parts[2] : parts[1]);
            snprintf(g->author, sizeof(g->author), "%s", parts[3][0] ? parts[3] : "Ndless");
            g->size_bytes    = (uint32_t)strtoul(parts[4], NULL, 10);
            g->bin_format    = (nl_bin_format_t)strtoul(parts[5], NULL, 10);
            g->exec_version  = (pc >= 7) ? (uint32_t)strtoul(parts[6], NULL, 10) : 1U;
            g->uses_lcd_blit = (pc >= 8) ? (atoi(parts[7]) != 0) : true;
            g->supports_hww  = (pc >= 9) ? (atoi(parts[8]) != 0) : false;
            g->accent_color  = (pc >= 10) ? (uint16_t)strtoul(parts[9], NULL, 10) : RGB565(56, 189, 248);
            g->added_order   = (pc >= 11) ? (uint32_t)strtoul(parts[10], NULL, 10) : (uint32_t)(temp_count + 1);
            g->icon_source   = NL_ICON_NONE;
            g->icon_pixels   = NULL;
            g->icon_alpha    = NULL;
            g->scr_pixels    = NULL;
            temp_count++;
        }
    }
    fclose(fp);

    uint32_t computed_crc = ~running_crc;
    free(raw_line);
    if (!have_version || !have_crc || computed_crc != file_crc) {
        free(temp_games);
        free(temp_folders);
        char bak_path[NL_MAX_PATH_LEN];
        (void)nl_diag_backup_corrupt_file(lib_path, bak_path, sizeof(bak_path));
        nl_diag_report(
            state,
            NL_DIAG_E301_CONFIG_INVALID,
            "nlauncher.lib.tns",
            lib_path,
            "Lecture index bibliotheque",
            "Index corrompu ou CRC invalide",
            "Rescan automatique lance pour reconstruire",
            "Copie .bak.tns creee + reconstruction"
        );
        return false;
    }

    /* Commit loaded folders & icon-independent games safely without freeing profiles */
    for (int f = 0; f < temp_folders->count; f++) {
        (void)nl_folder_add(&state->folders, temp_folders->paths[f]);
    }

    nl_free_media_buffers(state);
    memset(state->games, 0, sizeof(state->games));
    if (temp_count > 0) {
        memcpy(state->games, temp_games, (size_t)temp_count * sizeof(nl_game_t));
    }
    state->total_games = temp_count;

    free(temp_games);
    free(temp_folders);

    nl_rebuild_filter(state);
    return true;
}

bool nl_save_library_cache(const nl_state_t *state, const char *cache_path) {
    if (!state || !cache_path || cache_path[0] == '\0') return false;
    if (state->total_games <= 0 || state->total_games > NL_MAX_GAMES) return false;

    ensure_parent_dirs(cache_path);

    /* Pass 1: Compute exact payload size and IEEE 802.3 CRC32 with zero heap allocation */
    uint32_t payload_bytes = 0U;
    uint32_t crc_state = 0xFFFFFFFFU;

    for (int i = 0; i < state->total_games; i++) {
        const nl_game_t *g = &state->games[i];
        nl_cache_game_rec_t rec;
        build_cache_game_rec(g, &rec);

        crc_state = crc32_update(crc_state, &rec, sizeof(rec));
        payload_bytes += (uint32_t)sizeof(rec);

        if (rec.icon_source != (uint32_t)NL_ICON_NONE && rec.icon_w > 0U && rec.icon_h > 0U) {
            size_t px_count = (size_t)rec.icon_w * (size_t)rec.icon_h;
            size_t px_bytes = px_count * sizeof(uint16_t);
            crc_state = crc32_update(crc_state, g->icon_pixels, px_bytes);
            payload_bytes += (uint32_t)px_bytes;

            if (rec.has_icon_alpha && g->icon_alpha) {
                crc_state = crc32_update(crc_state, g->icon_alpha, px_count);
                payload_bytes += (uint32_t)px_count;
            }
        }
    }

    nl_cache_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic         = NL_CACHE_MAGIC;
    hdr.version       = NL_CACHE_VERSION;
    hdr.total_games   = (uint32_t)state->total_games;
    hdr.payload_bytes = payload_bytes;
    hdr.payload_crc32 = ~crc_state;

    char tmp_path[NL_MAX_PATH_LEN];
    build_tmp_path(cache_path, tmp_path, sizeof(tmp_path));

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) return false;

    if (fwrite(&hdr, 1U, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        (void)unlink(tmp_path);
        return false;
    }

    for (int i = 0; i < state->total_games; i++) {
        const nl_game_t *g = &state->games[i];
        nl_cache_game_rec_t rec;
        build_cache_game_rec(g, &rec);

        if (fwrite(&rec, 1U, sizeof(rec), fp) != sizeof(rec)) {
            fclose(fp);
            (void)unlink(tmp_path);
            return false;
        }

        if (rec.icon_source != (uint32_t)NL_ICON_NONE && rec.icon_w > 0U && rec.icon_h > 0U) {
            size_t px_count = (size_t)rec.icon_w * (size_t)rec.icon_h;
            size_t px_bytes = px_count * sizeof(uint16_t);
            if (fwrite(g->icon_pixels, 1U, px_bytes, fp) != px_bytes) {
                fclose(fp);
                (void)unlink(tmp_path);
                return false;
            }
            if (rec.has_icon_alpha && g->icon_alpha) {
                if (fwrite(g->icon_alpha, 1U, px_count, fp) != px_count) {
                    fclose(fp);
                    (void)unlink(tmp_path);
                    return false;
                }
            }
        }
    }

    if (fflush(fp) != 0 || ferror(fp) != 0 || fclose(fp) != 0) {
        (void)unlink(tmp_path);
        return false;
    }
    return commit_tmp_file_atomic(tmp_path, cache_path);
}

static void reset_library_entries(nl_state_t *state) {
    if (!state) return;
    /* NEVER free profiles when resetting library entries! */
    nl_free_media_buffers(state);
    state->total_games = 0;
    state->filtered_count = 0;
}

bool nl_load_library_cache(nl_state_t *state, const char *cache_path) {
    if (!state || !cache_path || cache_path[0] == '\0') return false;

    struct stat st;
    if (stat(cache_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        state->sys.cache_valid = false;
        return false;
    }
    if ((size_t)st.st_size < sizeof(nl_cache_header_t)) {
        state->sys.cache_valid = false;
        return false;
    }

    FILE *fp = fopen(cache_path, "rb");
    if (!fp) {
        state->sys.cache_valid = false;
        return false;
    }

    nl_cache_header_t hdr;
    if (fread(&hdr, 1U, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        state->sys.cache_valid = false;
        return false;
    }

    if (hdr.magic != NL_CACHE_MAGIC ||
        hdr.version != NL_CACHE_VERSION ||
        hdr.total_games == 0U ||
        hdr.total_games > (uint32_t)NL_MAX_GAMES ||
        (size_t)st.st_size != sizeof(nl_cache_header_t) + (size_t)hdr.payload_bytes) {
        fclose(fp);
        state->sys.cache_valid = false;
        return false;
    }

    /* Pass 1: Verify IEEE 802.3 CRC32 over the entire payload before mutating state */
    uint32_t crc_state = 0xFFFFFFFFU;
    uint32_t rem = hdr.payload_bytes;
    uint8_t chunk[256];
    while (rem > 0U) {
        size_t to_read = (rem > sizeof(chunk)) ? sizeof(chunk) : (size_t)rem;
        if (fread(chunk, 1U, to_read, fp) != to_read) {
            fclose(fp);
            state->sys.cache_valid = false;
            return false;
        }
        crc_state = crc32_update(crc_state, chunk, to_read);
        rem -= (uint32_t)to_read;
    }

    uint32_t computed_crc = ~crc_state;
    if (computed_crc != hdr.payload_crc32) {
        fclose(fp);
        state->sys.cache_valid = false;
        return false;
    }

    if (fseek(fp, (long)sizeof(nl_cache_header_t), SEEK_SET) != 0) {
        fclose(fp);
        state->sys.cache_valid = false;
        return false;
    }

    /* Clear any existing games before populating from verified cache */
    reset_library_entries(state);

    for (uint32_t i = 0U; i < hdr.total_games; i++) {
        nl_cache_game_rec_t rec;
        if (fread(&rec, 1U, sizeof(rec), fp) != sizeof(rec)) {
            reset_library_entries(state);
            fclose(fp);
            state->sys.cache_valid = false;
            return false;
        }

        rec.title[NL_MAX_NAME_LEN - 1] = '\0';
        rec.author[NL_MAX_AUTHOR_LEN - 1] = '\0';
        rec.path[NL_MAX_PATH_LEN - 1] = '\0';
        rec.filename[NL_MAX_NAME_LEN - 1] = '\0';

        /* Verify that the cached .tns executable still exists on flash with matching size */
        struct stat gst;
        if (rec.path[0] == '\0' ||
            stat(rec.path, &gst) != 0 ||
            !S_ISREG(gst.st_mode) ||
            (uint32_t)gst.st_size != rec.size_bytes) {
            reset_library_entries(state);
            fclose(fp);
            state->sys.cache_valid = false;
            return false;
        }

        if (rec.icon_w > NL_ICON_MAX_DIM || rec.icon_h > NL_ICON_MAX_DIM) {
            reset_library_entries(state);
            fclose(fp);
            state->sys.cache_valid = false;
            return false;
        }

        nl_game_t *g = &state->games[i];
        memset(g, 0, sizeof(*g));
        snprintf(g->title, sizeof(g->title), "%s", rec.title);
        snprintf(g->author, sizeof(g->author), "%s", rec.author);
        snprintf(g->path, sizeof(g->path), "%s", rec.path);
        snprintf(g->filename, sizeof(g->filename), "%s", rec.filename);
        g->size_bytes    = rec.size_bytes;
        g->bin_format    = (nl_bin_format_t)rec.bin_format;
        g->exec_version  = rec.exec_version;
        g->uses_lcd_blit = (rec.uses_lcd_blit != 0U);
        g->supports_hww  = (rec.supports_hww != 0U);
        g->accent_color  = rec.accent_color;
        g->added_order   = rec.added_order;
        g->icon_source   = NL_ICON_NONE;
        g->icon_w        = 0U;
        g->icon_h        = 0U;
        g->icon_pixels   = NULL;
        g->icon_alpha    = NULL;

        if (rec.icon_source != (uint32_t)NL_ICON_NONE && rec.icon_w > 0U && rec.icon_h > 0U) {
            size_t px_count = (size_t)rec.icon_w * (size_t)rec.icon_h;
            size_t px_bytes = px_count * sizeof(uint16_t);
            uint16_t *px = (uint16_t *)malloc(px_bytes);
            if (!px) {
                nl_recover_memory_on_oom(state, "Lecture icone cache", cache_path);
                reset_library_entries(state);
                fclose(fp);
                state->sys.cache_valid = false;
                return false;
            }
            if (fread(px, 1U, px_bytes, fp) != px_bytes) {
                free(px);
                reset_library_entries(state);
                fclose(fp);
                state->sys.cache_valid = false;
                return false;
            }

            uint8_t *alpha = NULL;
            if (rec.has_icon_alpha) {
                alpha = (uint8_t *)malloc(px_count);
                if (!alpha) {
                    free(px);
                    nl_recover_memory_on_oom(state, "Lecture alpha cache", cache_path);
                    reset_library_entries(state);
                    fclose(fp);
                    state->sys.cache_valid = false;
                    return false;
                }
                if (fread(alpha, 1U, px_count, fp) != px_count) {
                    free(alpha);
                    free(px);
                    reset_library_entries(state);
                    fclose(fp);
                    state->sys.cache_valid = false;
                    return false;
                }
            }

            g->icon_source = (nl_icon_source_t)rec.icon_source;
            g->icon_w      = rec.icon_w;
            g->icon_h      = rec.icon_h;
            g->icon_pixels = px;
            g->icon_alpha  = alpha;
        }

        state->total_games = (int)(i + 1U);
    }

    fclose(fp);
    state->sys.cache_valid = true;
    state->sys.cache_crc32 = hdr.payload_crc32;
    nl_rebuild_filter(state);
    return true;
}

/* #R20 Fast Warm-Boot Pre-Decoded Icon Hydration:
 * Hydrates already-decoded RGB565 (+ optional alpha) custom icons from nlauncher.cache.tns
 * directly into existing state->games[] entries loaded from nlauncher.lib.tns.
 * Pumps progress_cb frequently so the startup splash animation stays fluid at 60 FPS. */
int nl_hydrate_icons_from_cache(
    nl_state_t *state,
    const char *cache_path,
    nl_progress_cb_t progress_cb,
    void *progress_ctx
) {
    if (!state || !cache_path || cache_path[0] == '\0' || state->total_games <= 0) return 0;

    FILE *fp = fopen(cache_path, "rb");
    if (!fp) return 0;

    nl_cache_header_t hdr;
    if (fread(&hdr, 1U, sizeof(hdr), fp) != sizeof(hdr) ||
        hdr.magic != NL_CACHE_MAGIC ||
        hdr.version != NL_CACHE_VERSION ||
        hdr.total_games == 0U ||
        hdr.total_games > NL_MAX_GAMES ||
        hdr.payload_bytes == 0U ||
        hdr.payload_bytes > (4U * 1024U * 1024U)) {
        fclose(fp);
        return 0;
    }

    /* Pass 1: Verify CRC32 in small 2 KB chunks while pumping splash animation */
    uint32_t crc_state = 0xFFFFFFFFU;
    uint32_t rem = hdr.payload_bytes;
    uint8_t chunk[2048];
    uint32_t chunk_idx = 0U;
    while (rem > 0U) {
        if (state->scan.abort_requested || state->scan.cancel_requested) {
            fclose(fp);
            return 0;
        }
        size_t to_read = (rem > sizeof(chunk)) ? sizeof(chunk) : (size_t)rem;
        if (fread(chunk, 1U, to_read, fp) != to_read) {
            fclose(fp);
            return 0;
        }
        crc_state = crc32_update(crc_state, chunk, to_read);
        rem -= (uint32_t)to_read;
        chunk_idx++;
        if (progress_cb && (chunk_idx & 7U) == 0U) {
            progress_cb(progress_ctx);
        }
    }

    if ((~crc_state) != hdr.payload_crc32) {
        fclose(fp);
        return 0;
    }

    if (fseek(fp, (long)sizeof(nl_cache_header_t), SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    uint32_t total_ram = 0U;
    for (int g = 0; g < state->total_games; g++) {
        total_ram += nl_game_icon_ram_bytes(&state->games[g]);
    }

    int hydrated = 0;
    for (uint32_t i = 0U; i < hdr.total_games; i++) {
        if (state->scan.abort_requested || state->scan.cancel_requested) break;
        nl_cache_game_rec_t rec;
        if (fread(&rec, 1U, sizeof(rec), fp) != sizeof(rec)) break;

        rec.path[NL_MAX_PATH_LEN - 1] = '\0';
        rec.filename[NL_MAX_NAME_LEN - 1] = '\0';

        bool has_blob = (rec.icon_source != (uint32_t)NL_ICON_NONE &&
                         rec.icon_w > 0U && rec.icon_h > 0U &&
                         rec.icon_w <= NL_ICON_MAX_DIM && rec.icon_h <= NL_ICON_MAX_DIM);
        size_t px_count = has_blob ? ((size_t)rec.icon_w * (size_t)rec.icon_h) : 0U;
        size_t px_bytes = px_count * sizeof(uint16_t);
        size_t al_bytes = (has_blob && rec.has_icon_alpha) ? px_count : 0U;
        size_t skip_bytes = px_bytes + al_bytes;

        /* Find matching game in current library index */
        nl_game_t *match = NULL;
        if (has_blob && rec.path[0] != '\0') {
            for (int g = 0; g < state->total_games; g++) {
                if (state->games[g].icon_pixels == NULL &&
                    state->games[g].size_bytes == rec.size_bytes &&
                    str_icmp_s(state->games[g].path, rec.path) == 0) {
                    match = &state->games[g];
                    break;
                }
            }
        }

        if (match && (total_ram + (uint32_t)skip_bytes) <= NL_ICON_RAM_BUDGET) {
            uint16_t *px = (uint16_t *)malloc(px_bytes);
            uint8_t  *al = (al_bytes > 0U) ? (uint8_t *)malloc(al_bytes) : NULL;
            if (px && (al_bytes == 0U || al != NULL)) {
                bool rd_ok = (fread(px, 1U, px_bytes, fp) == px_bytes);
                if (rd_ok && al_bytes > 0U) {
                    rd_ok = (fread(al, 1U, al_bytes, fp) == al_bytes);
                }
                if (rd_ok) {
                    match->icon_source = (nl_icon_source_t)rec.icon_source;
                    match->icon_w      = rec.icon_w;
                    match->icon_h      = rec.icon_h;
                    match->icon_pixels = px;
                    match->icon_alpha  = al;
                    total_ram += (uint32_t)skip_bytes;
                    hydrated++;
                } else {
                    free(px);
                    free(al);
                    break;
                }
            } else {
                free(px);
                free(al);
                if (skip_bytes > 0U && fseek(fp, (long)skip_bytes, SEEK_CUR) != 0) break;
            }
        } else if (skip_bytes > 0U) {
            if (fseek(fp, (long)skip_bytes, SEEK_CUR) != 0) break;
        }

        if (progress_cb && ((i & 1U) == 0U || match != NULL)) {
            progress_cb(progress_ctx);
        }
    }

    fclose(fp);
    state->icon_ram_bytes = total_ram;
    state->sys.cache_valid = true;
    state->sys.cache_crc32 = hdr.payload_crc32;
    return hydrated;
}

static const char *display_mode_to_str(nl_display_mode_t mode) {
    if (mode == NL_DISPLAY_LIGHT) return "light";
    if (mode == NL_DISPLAY_GRAY)  return "gray";
    return "dark";
}

static const char *accent_color_to_str(nl_accent_color_t accent) {
    switch (accent) {
        case NL_ACCENT_RED:    return "red";
        case NL_ACCENT_GREEN:  return "green";
        case NL_ACCENT_YELLOW: return "yellow";
        case NL_ACCENT_BLUE:
        default:               return "blue";
    }
}

static const char *layout_to_str(nl_layout_t layout) {
    switch (layout) {
        case NL_LAYOUT_1X1: return "1x1";
        case NL_LAYOUT_2X2: return "2x2";
        case NL_LAYOUT_3X3: return "3x3";
        case NL_LAYOUT_4X4: return "4x4";
        case NL_LAYOUT_3X2:
        default:            return "3x2";
    }
}

static nl_layout_t str_to_layout(const char *val) {
    if (str_icmp_s(val, "1x1") == 0) return NL_LAYOUT_1X1;
    if (str_icmp_s(val, "2x2") == 0) return NL_LAYOUT_2X2;
    if (str_icmp_s(val, "3x3") == 0) return NL_LAYOUT_3X3;
    if (str_icmp_s(val, "4x4") == 0) return NL_LAYOUT_4X4;
    return NL_LAYOUT_3X2;
}

static const char *scroll_dir_to_str(nl_scroll_dir_t dir) {
    return (dir == NL_SCROLL_HORIZONTAL) ? "horizontal" : "vertical";
}

static nl_scroll_dir_t str_to_scroll_dir(const char *val) {
    if (str_icmp_s(val, "horizontal") == 0 || strcmp(val, "1") == 0) {
        return NL_SCROLL_HORIZONTAL;
    }
    return NL_SCROLL_VERTICAL;
}

static const char *sort_to_str(nl_sort_order_t sort) {
    switch (sort) {
        case NL_SORT_ALPHA_DESC:    return "alpha_desc";
        case NL_SORT_RECENT_ADD:    return "recent_add";
        case NL_SORT_OLDEST_ADD:    return "oldest_add";
        case NL_SORT_LAST_USED:     return "last_used";
        case NL_SORT_MOST_LAUNCHED: return "most_launched";
        case NL_SORT_PLAY_TIME:     return "play_time";
        case NL_SORT_ALPHA_ASC:
        default:                    return "alpha_asc";
    }
}

static nl_sort_order_t str_to_sort(const char *val) {
    if (str_icmp_s(val, "alpha_desc") == 0)    return NL_SORT_ALPHA_DESC;
    if (str_icmp_s(val, "recent_add") == 0)    return NL_SORT_RECENT_ADD;
    if (str_icmp_s(val, "oldest_add") == 0)    return NL_SORT_OLDEST_ADD;
    if (str_icmp_s(val, "last_used") == 0)     return NL_SORT_LAST_USED;
    if (str_icmp_s(val, "most_launched") == 0) return NL_SORT_MOST_LAUNCHED;
    if (str_icmp_s(val, "play_time") == 0)     return NL_SORT_PLAY_TIME;
    return NL_SORT_ALPHA_ASC;
}

bool nl_save_config(const nl_state_t *state, const char *cfg_path) {
    if (!state || !cfg_path || cfg_path[0] == '\0') return false;
    ensure_parent_dirs(cfg_path);

    char tmp_path[NL_MAX_PATH_LEN];
    build_tmp_path(cfg_path, tmp_path, sizeof(tmp_path));

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) return false;

    fprintf(fp, "# N-Launcher Configuration v3.1\n");
    fprintf(fp, "display_mode=%s\n", display_mode_to_str(state->display_mode));
    fprintf(fp, "accent_color=%s\n", accent_color_to_str(state->accent_color));
    fprintf(fp, "global_hotkey=%d\n", state->settings.global_hotkey ? 1 : 0);
    fprintf(fp, "hotkey_combo=%s\n", nl_hk_combo_to_str((nl_hk_combo_t)state->settings.hotkey_combo));
    fprintf(fp, "layout=%s\n", layout_to_str(state->settings.layout));
    fprintf(fp, "scroll_dir=%s\n", scroll_dir_to_str(state->settings.scroll_dir));
    fprintf(fp, "profile_lock_enabled=%d\n", state->settings.profile_lock_enabled ? 1 : 0);
    fprintf(fp, "show_names=%d\n", state->settings.show_names ? 1 : 0);
    fprintf(fp, "show_shortcuts=%d\n", state->settings.show_shortcuts ? 1 : 0);
    fprintf(fp, "touchpad_mode=%s\n",
            (state->settings.touchpad_mode == NL_TP_MODE_MOUSE) ? "mouse" : "touchpad");
    fprintf(fp, "touchpad_nav=%d\n", state->settings.touchpad_nav ? 1 : 0);
    fprintf(fp, "touchpad_sensitivity=%d\n", state->settings.touchpad_sensitivity);
    fprintf(fp, "cursor_speed=%d\n", state->settings.cursor_speed);
    fprintf(fp, "touchpad_cursor_show=%d\n", state->settings.touchpad_cursor_show ? 1 : 0);
    fprintf(fp, "touchpad_cursor_style=%s\n", nl_cursor_style_to_str(state->settings.touchpad_cursor_style));
    fprintf(fp, "cursor_size=%s\n", nl_cursor_size_to_str(state->settings.cursor_size));
    fprintf(fp, "tap_to_click=%d\n", state->settings.tap_to_click ? 1 : 0);
    fprintf(fp, "sort_order=%s\n", sort_to_str(state->settings.sort_order));
    fprintf(fp, "app_open_anim=%s\n", nl_app_open_anim_to_str(state->settings.app_open_anim));
    fprintf(fp, "app_close_anim=%s\n", nl_app_close_anim_to_str(state->settings.app_close_anim));
    fprintf(fp, "launch_anim=%s\n", nl_launch_anim_to_str(state->settings.launch_anim_mode));
    fprintf(fp, "return_anim=%s\n", nl_return_anim_to_str(state->settings.return_anim_mode));
    fprintf(fp, "play_counter=%u\n", (unsigned)state->play_counter);
    if (state->sys.self_path[0] != '\0') {
        fprintf(fp, "launcher_path=%s\n", state->sys.self_path);
    }

    /* Secondary backup of saved/discovered game folders (completely independent of icons) */
    for (int f = 0; f < state->folders.count && f < NL_MAX_GAME_FOLDERS; f++) {
        if (state->folders.paths[f][0] != '\0') {
            fprintf(fp, "game_folder=%s\n", state->folders.paths[f]);
        }
    }

    for (int i = 0; i < state->total_games; i++) {
        const nl_game_t *g = &state->games[i];
        if (g->is_favorite ||
            g->last_played_rank > 0U ||
            g->launch_count > 0U ||
            g->play_time_sec > 0U ||
            g->last_played_ts > 0U ||
            g->shortcut_key != 0) {
            char sc = nl_shortcut_is_assignable(g->shortcut_key) ? g->shortcut_key : '-';
            fprintf(fp, "game=%s|%d|%u|%u|%u|%u|%c\n",
                    g->filename,
                    g->is_favorite ? 1 : 0,
                    (unsigned)g->last_played_rank,
                    (unsigned)g->launch_count,
                    (unsigned)g->play_time_sec,
                    (unsigned)g->last_played_ts,
                    sc);
        }
    }

    if (fflush(fp) != 0 || ferror(fp) != 0 || fclose(fp) != 0) {
        (void)unlink(tmp_path);
        return false;
    }
    return commit_tmp_file_atomic(tmp_path, cfg_path);
}

bool nl_load_config(nl_state_t *state, const char *cfg_path) {
    if (!state || !cfg_path || cfg_path[0] == '\0') return false;
    FILE *fp = fopen(cfg_path, "r");
    if (!fp) return false;

    char line[384];
    int valid_keys = 0;
    int malformed_lines = 0;
    bool has_nonempty = false;

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
            line[--len] = '\0';
        }
        if (line[0] == '#' || line[0] == '\0') continue;
        has_nonempty = true;

        char *eq = strchr(line, '=');
        if (!eq) {
            malformed_lines++;
            continue;
        }
        *eq = '\0';
        const char *key = trim_ws(line);
        char *val = trim_ws(eq + 1);
        if (key[0] == '\0') {
            malformed_lines++;
            continue;
        }
        valid_keys++;

        if (strcmp(key, "display_mode") == 0) {
            if (str_icmp_s(val, "light") == 0) {
                state->display_mode = NL_DISPLAY_LIGHT;
            } else if (str_icmp_s(val, "gray") == 0) {
                state->display_mode = NL_DISPLAY_GRAY;
            } else if (str_icmp_s(val, "dark") == 0) {
                state->display_mode = NL_DISPLAY_DARK;
            }
        } else if (strcmp(key, "accent_color") == 0) {
            if (str_icmp_s(val, "blue") == 0) {
                state->accent_color = NL_ACCENT_BLUE;
            } else if (str_icmp_s(val, "red") == 0) {
                state->accent_color = NL_ACCENT_RED;
            } else if (str_icmp_s(val, "green") == 0) {
                state->accent_color = NL_ACCENT_GREEN;
            } else if (str_icmp_s(val, "yellow") == 0) {
                state->accent_color = NL_ACCENT_YELLOW;
            }
        } else if (strcmp(key, "autostart") == 0 || strcmp(key, "show_battery") == 0) {
            /* Legacy keys intentionally retired */
        } else if (strcmp(key, "global_hotkey") == 0) {
            state->settings.global_hotkey = (atoi(val) != 0);
        } else if (strcmp(key, "hotkey_combo") == 0) {
            state->settings.hotkey_combo = (int)nl_str_to_hk_combo(val);
        } else if (strcmp(key, "layout") == 0) {
            state->settings.layout = str_to_layout(val);
        } else if (strcmp(key, "scroll_dir") == 0) {
            state->settings.scroll_dir = str_to_scroll_dir(val);
        } else if (strcmp(key, "profile_lock_enabled") == 0) {
            state->settings.profile_lock_enabled = (atoi(val) != 0);
        } else if (strcmp(key, "show_names") == 0) {
            state->settings.show_names = (atoi(val) != 0);
        } else if (strcmp(key, "show_shortcuts") == 0) {
            state->settings.show_shortcuts = (atoi(val) != 0);
        } else if (strcmp(key, "touchpad_mode") == 0) {
            if (str_icmp_s(val, "mouse") == 0 || strcmp(val, "1") == 0) {
                state->settings.touchpad_mode = NL_TP_MODE_MOUSE;
            } else {
                state->settings.touchpad_mode = NL_TP_MODE_TOUCHPAD;
            }
        } else if (strcmp(key, "touchpad_nav") == 0) {
            state->settings.touchpad_nav = (atoi(val) != 0);
        } else if (strcmp(key, "touchpad_sensitivity") == 0) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NL_TP_SENS_COUNT) s = NL_TP_SENS_COUNT - 1;
            state->settings.touchpad_sensitivity = s;
        } else if (strcmp(key, "cursor_speed") == 0) {
            int sp = atoi(val);
            if (sp < 0) sp = 0;
            if (sp >= NL_CURSOR_SPEED_COUNT) sp = NL_CURSOR_SPEED_COUNT - 1;
            state->settings.cursor_speed = sp;
        } else if (strcmp(key, "touchpad_cursor_show") == 0) {
            state->settings.touchpad_cursor_show = (atoi(val) != 0);
        } else if (strcmp(key, "touchpad_cursor_style") == 0) {
            state->settings.touchpad_cursor_style = nl_str_to_cursor_style(val);
        } else if (strcmp(key, "cursor_size") == 0) {
            state->settings.cursor_size = nl_str_to_cursor_size(val);
        } else if (strcmp(key, "tap_to_click") == 0) {
            state->settings.tap_to_click = (atoi(val) != 0);
        } else if (strcmp(key, "sort_order") == 0) {
            state->settings.sort_order = str_to_sort(val);
        } else if (strcmp(key, "app_open_anim") == 0) {
            state->settings.app_open_anim = nl_str_to_app_open_anim(val);
        } else if (strcmp(key, "app_close_anim") == 0) {
            state->settings.app_close_anim = nl_str_to_app_close_anim(val);
        } else if (strcmp(key, "launch_anim") == 0) {
            state->settings.launch_anim_mode = nl_str_to_launch_anim(val);
        } else if (strcmp(key, "return_anim") == 0) {
            state->settings.return_anim_mode = nl_str_to_return_anim(val);
        } else if (strcmp(key, "play_counter") == 0) {
            unsigned long pc = strtoul(val, NULL, 10);
            if (pc <= 10000000UL) {
                state->play_counter = (uint32_t)pc;
            }
        } else if (strcmp(key, "game_folder") == 0) {
            if (val[0] != '\0') {
                (void)nl_folder_add(&state->folders, val);
            }
        } else if (strcmp(key, "game") == 0) {
            char *parts[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
            int p_count = 0;
            char *cur = val;
            parts[p_count++] = cur;
            while (p_count < 7) {
                char *sep = strchr(cur, '|');
                if (!sep) break;
                *sep = '\0';
                cur = sep + 1;
                parts[p_count++] = cur;
            }
            if (p_count < 3 || parts[0][0] == '\0') {
                malformed_lines++;
                continue;
            }

            const char *fname = parts[0];
            bool fav = (atoi(parts[1]) != 0);
            uint32_t rank = (uint32_t)strtoul(parts[2], NULL, 10);
            uint32_t launches = (p_count >= 4) ? (uint32_t)strtoul(parts[3], NULL, 10) : 0U;
            uint32_t play_sec = (p_count >= 5) ? (uint32_t)strtoul(parts[4], NULL, 10) : 0U;
            uint32_t last_ts  = (p_count >= 6) ? (uint32_t)strtoul(parts[5], NULL, 10) : 0U;
            char sc = 0;
            if (p_count >= 7 && parts[6][0] != '\0' && parts[6][0] != '-') {
                char upper = (char)toupper((unsigned char)parts[6][0]);
                if (nl_shortcut_is_assignable(upper)) {
                    sc = upper;
                }
            }

            if (launches == 0U && rank > 0U) launches = 1U;

            for (int i = 0; i < state->total_games; i++) {
                if (str_icmp_s(state->games[i].filename, fname) == 0) {
                    state->games[i].is_favorite      = fav;
                    state->games[i].last_played_rank = rank;
                    state->games[i].launch_count     = launches;
                    state->games[i].play_time_sec    = play_sec;
                    state->games[i].last_played_ts   = last_ts;
                    if (sc != 0 && nl_shortcut_find_owner(state, sc, i) < 0) {
                        state->games[i].shortcut_key = sc;
                    }
                }
            }
        }
    }

    fclose(fp);

    if (has_nonempty && (valid_keys == 0 || malformed_lines > 0)) {
        char bak_path[NL_MAX_PATH_LEN];
        (void)nl_diag_backup_corrupt_file(cfg_path, bak_path, sizeof(bak_path));
        nl_diag_report(
            state,
            NL_DIAG_E301_CONFIG_INVALID,
            "nlauncher.cfg.tns",
            cfg_path,
            "Lecture configuration",
            (valid_keys == 0) ? "Fichier illisible ou corrompu" : "Entrees malformees detectees",
            (valid_keys == 0) ? "Configuration par defaut chargee" : "Valeurs saines conservees",
            "Sauvegarde .bak.tns creee"
        );
        if (valid_keys == 0) {
            return false;
        }
    }

    state->anim.switch_hotkey_fp    = state->settings.global_hotkey ? 256 : 0;
    state->anim.switch_shownames_fp = state->settings.show_names ? 256 : 0;
    state->anim.switch_shortcuts_fp = state->settings.show_shortcuts ? 256 : 0;
    state->anim.switch_touchpad_fp       = state->settings.touchpad_nav ? 256 : 0;
    state->anim.switch_tp_mode_fp        = (state->settings.touchpad_mode == NL_TP_MODE_MOUSE) ? 256 : 0;
    state->anim.switch_tp_cursor_fp      = state->settings.touchpad_cursor_show ? 256 : 0;
    state->anim.switch_tap_click_fp      = state->settings.tap_to_click ? 256 : 0;
    state->anim.switch_prof_lock_fp      = state->settings.profile_lock_enabled ? 256 : 0;
    state->sui.tp_style_cursor           = (state->settings.touchpad_cursor_style >= NL_CURSOR_STYLE_ARROW &&
                                            state->settings.touchpad_cursor_style < NL_CURSOR_STYLE_COUNT)
                                            ? ((int)state->settings.touchpad_cursor_style - 1)
                                            : 0;
    state->sui.tp_size_cursor            = ((unsigned)state->settings.cursor_size < (unsigned)NL_CURSOR_SIZE_COUNT)
                                            ? (int)state->settings.cursor_size
                                            : (int)NL_CURSOR_SIZE_DEFAULT;
    state->anim.tp_slider_pos_fp         = state->settings.touchpad_sensitivity * 256;
    state->anim.tp_slider_vel_fp         = 0;
    state->anim.tp_detent_pulse_fp       = 0;
    state->anim.tp_notch_last            = state->settings.touchpad_sensitivity;
    state->anim.tp_speed_slider_pos_fp   = state->settings.cursor_speed * 256;
    state->anim.tp_speed_slider_vel_fp   = 0;
    state->anim.tp_speed_detent_pulse_fp = 0;
    state->anim.tp_speed_notch_last      = state->settings.cursor_speed;
    state->anim.layout_from              = state->settings.layout;
    state->anim.layout_trans_fp          = 256;
    nl_rebuild_filter(state);
    nl_mouse_update_hover(state);
    return true;
}

bool nl_reset_settings_only(nl_state_t *state, const char *cfg_path) {
    if (!state) return false;

    /* Clean up any legacy autostart & disable global hotkey startup stub without touching N-Launcher.tns or games */
    if (state->sys.docs_root[0] != '\0') {
        char diag[64];
        nl_cleanup_legacy_autostart(state->sys.docs_root);
        (void)nl_hotkey_set(state, false, diag, sizeof(diag));
    } else {
        state->settings.global_hotkey = false;
    }

    /* Reset ONLY configuration preferences to factory defaults */
    state->display_mode          = NL_DISPLAY_DARK;
    state->accent_color          = NL_ACCENT_BLUE;
    state->settings.global_hotkey = false;
    state->settings.hotkey_combo  = (int)NL_HK_COMBO_CTRL_MENU;
    state->settings.layout        = NL_LAYOUT_3X2;
    state->settings.scroll_dir    = NL_SCROLL_VERTICAL;
    state->settings.profile_lock_enabled = false;
    state->settings.show_names      = false;
    state->settings.show_shortcuts  = true;
    state->settings.touchpad_mode         = NL_TP_MODE_TOUCHPAD;
    state->settings.touchpad_nav          = false;
    state->settings.touchpad_sensitivity  = NL_TP_SENS_DEFAULT;
    state->settings.cursor_speed          = NL_CURSOR_SPEED_DEFAULT;
    state->settings.touchpad_cursor_show  = false;
    state->settings.touchpad_cursor_style = NL_CURSOR_STYLE_ARROW;
    state->settings.cursor_size           = NL_CURSOR_SIZE_DEFAULT;
    state->settings.tap_to_click          = false;
    state->settings.sort_order      = NL_SORT_ALPHA_ASC;
    state->settings.app_open_anim   = NL_OPEN_ANIM_CURRENT;
    state->settings.app_close_anim  = NL_CLOSE_ANIM_LIQUID;
    state->settings.launch_anim_mode = NL_LAUNCH_ANIM_CURRENT;
    state->settings.return_anim_mode = NL_RETURN_ANIM_LIQUID;
    state->anim.switch_hotkey_fp    = 0;
    state->anim.switch_shownames_fp = 0;
    state->anim.switch_shortcuts_fp = 256;
    state->anim.switch_touchpad_fp  = 0;
    state->anim.switch_tp_mode_fp   = 0;
    state->anim.switch_tp_cursor_fp = 0;
    state->anim.switch_tap_click_fp = 0;
    state->anim.switch_prof_lock_fp = 0;
    state->sui.tp_tab               = 0;
    state->sui.tp_scroll            = 0;
    state->sui.tp_custom_row        = 0;
    state->sui.tp_style_cursor      = 0;
    state->sui.tp_size_cursor       = (int)NL_CURSOR_SIZE_DEFAULT;
    state->sui.hotkey_cursor        = 0;
    state->sui.ctrl_mouse_active    = false;
    state->sui.hover_valid          = false;
    state->anim.tp_slider_pos_fp         = NL_TP_SENS_DEFAULT * 256;
    state->anim.tp_slider_vel_fp         = 0;
    state->anim.tp_detent_pulse_fp       = 0;
    state->anim.tp_notch_last            = NL_TP_SENS_DEFAULT;
    state->anim.tp_speed_slider_pos_fp   = NL_CURSOR_SPEED_DEFAULT * 256;
    state->anim.tp_speed_slider_vel_fp   = 0;
    state->anim.tp_speed_detent_pulse_fp = 0;
    state->anim.tp_speed_notch_last      = NL_CURSOR_SPEED_DEFAULT;
    state->anim.layout_from              = NL_LAYOUT_3X2;
    state->anim.layout_trans_fp          = 256;

    /* Clear launcher custom shortcut bindings while keeping games, covers, favorites, stats & saves untouched */
    for (int i = 0; i < state->total_games; i++) {
        state->games[i].shortcut_key = 0;
    }

    nl_rebuild_filter(state);
    if (state->profiles.initialized) {
        nl_profile_snapshot_active(state);
        state->sui.profiles_dirty = true;
    }

    if (cfg_path && cfg_path[0] != '\0') {
        return nl_save_config(state, cfg_path);
    }
    return true;
}

bool nl_check_ndless_persistent_at(const char *phoenix_dir) {
    if (!phoenix_dir || phoenix_dir[0] == '\0') return false;
    char data_path[NL_MAX_PATH_LEN];
    char tns_path[NL_MAX_PATH_LEN];
    snprintf(data_path, sizeof(data_path), "%.*s/currentdoc.data",
             (int)(sizeof(data_path) - 20U), phoenix_dir);
    snprintf(tns_path, sizeof(tns_path), "%.*s/currentdoc.tns",
             (int)(sizeof(tns_path) - 20U), phoenix_dir);

    struct stat st_data, st_tns;
    if (stat(data_path, &st_data) != 0 || !S_ISREG(st_data.st_mode) || st_data.st_size < 4) {
        return false;
    }
    if (stat(tns_path, &st_tns) != 0 || !S_ISREG(st_tns.st_mode) || st_tns.st_size < 16) {
        return false;
    }

    FILE *fp = fopen(data_path, "rb");
    if (!fp) return false;
    uint8_t b[4] = {0, 0, 0, 0};
    size_t n = fread(b, 1, 4, fp);
    fclose(fp);
    if (n != 4U) return false;
    uint32_t word0 = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return (word0 == 1U);
}

static void clean_startup_duplicates(const char *startup_dir, const char *keep_file) {
    DIR *dp = opendir(startup_dir);
    if (!dp) return;
    struct dirent *ep;
    while ((ep = readdir(dp)) != NULL) {
        const char *name = ep->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (keep_file && str_icmp_s(name, keep_file) == 0) continue;

        /* Only remove duplicates clearly belonging to N-Launcher autostart */
        if (str_icmp_s(name, "nl_boot.tns") == 0 ||
            str_icmp_s(name, "nl_boot_stub.tns") == 0 ||
            str_icmp_s(name, "n-launcher.tns") == 0 ||
            str_icmp_s(name, "nlauncher.tns") == 0 ||
            str_icmp_s(name, "nlauncher_boot.tns") == 0) {
            char rm_path[NL_MAX_PATH_LEN];
            snprintf(rm_path, sizeof(rm_path), "%.*s/%.*s",
                     180, startup_dir, 64, name);
            (void)unlink(rm_path);
        }
    }
    closedir(dp);
}

void nl_cleanup_legacy_autostart(const char *docs_root) {
    const char *root = (docs_root && docs_root[0] != '\0') ? docs_root : "/documents";
    char startup_dir[NL_MAX_PATH_LEN];
    char stub_path[NL_MAX_PATH_LEN];
    char guard_path[NL_MAX_PATH_LEN];

    snprintf(startup_dir, sizeof(startup_dir), "%.*s/ndless/startup",
             (int)(sizeof(startup_dir) - 20U), root);
    snprintf(stub_path, sizeof(stub_path), "%.*s/nl_boot.tns",
             (int)(sizeof(stub_path) - 16U), startup_dir);
    snprintf(guard_path, sizeof(guard_path), "%.*s/ndless/nl_boot_guard.cfg.tns",
             (int)(sizeof(guard_path) - 32U), root);

    clean_startup_duplicates(startup_dir, NULL);
    (void)unlink(stub_path);
    (void)unlink(guard_path);
}

/* ============================================================================
 * Global TI-OS Hotkey Module Management (nl_hotkey.tns + Live "NLHK" RAM Hook)
 * ============================================================================ */

#ifndef __arm__
static bool g_host_hk_ram_resident = false;
static nl_hk_ctrl_t g_host_hk_ctrl = {
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
    1U,
    0U,
    "/documents",
    "/documents/ndless/N-Launcher.tns"
};
#endif

static nl_hk_ctrl_t *probe_ram_hk_ctrl(void) {
#ifdef __arm__
    volatile uintptr_t vec_addr = 0x28U;
    const uint32_t *swi_handler = *(const uint32_t * volatile *)vec_addr;
    uintptr_t *sc_ext = NULL;
    uintptr_t *os_sc = NULL;
    if (!nl_hotkey_parse_swi_handler(swi_handler, &sc_ext, &os_sc) || !os_sc) {
        return NULL;
    }
    uintptr_t fn_get_event = ((unsigned)e_get_event <= 512U) ? os_sc[e_get_event] : 0U;
    bool third_party = false;
    nl_hk_ctrl_t *ctrl = nl_hotkey_find_live_ctrl_at(fn_get_event, 0x10000000U, 0x18000000U, &third_party);
    if (!ctrl && !third_party) {
        uintptr_t fn_gc_finish = ((unsigned)e_gui_gc_finish <= 512U) ? os_sc[e_gui_gc_finish] : 0U;
        if (fn_gc_finish != 0U) {
            ctrl = nl_hotkey_find_live_ctrl_at(fn_gc_finish, 0x10000000U, 0x18000000U, &third_party);
        }
    }
    return ctrl;
#else
    if (!g_host_hk_ram_resident) return NULL;
    return &g_host_hk_ctrl;
#endif
}

bool nl_hotkey_is_installed_in_ram(bool *out_enabled, int *out_combo, uint32_t *out_launch_count) {
    if (out_enabled)      *out_enabled = false;
    if (out_combo)        *out_combo = (int)NL_HK_COMBO_CTRL_MENU;
    if (out_launch_count) *out_launch_count = 0U;

    nl_hk_ctrl_t *ctrl = probe_ram_hk_ctrl();
    if (!ctrl) return false;

    if (out_enabled)      *out_enabled = (ctrl->enabled != 0U);
    if (out_combo)        *out_combo = (int)ctrl->combo;
    if (out_launch_count) *out_launch_count = ctrl->launch_count;
    return true;
}

void nl_hotkey_set_ram_busy(bool busy) {
    nl_hk_ctrl_t *ctrl = probe_ram_hk_ctrl();
    if (ctrl) {
        ctrl->in_progress = busy ? 1U : 0U;
    }
}

uint32_t nl_hotkey_get_ram_hook_mode(void) {
    nl_hk_ctrl_t *ctrl = probe_ram_hk_ctrl();
    return ctrl ? ctrl->hook_mode : 0U;
}

void nl_hotkey_sync_live_combo(nl_state_t *state) {
    if (!state) return;
    nl_hk_ctrl_t *ctrl = probe_ram_hk_ctrl();
    if (ctrl) {
        ctrl->combo = (uint32_t)state->settings.hotkey_combo;
    }
    const char *combo_lbl = nl_hk_combo_label((nl_hk_combo_t)state->settings.hotkey_combo);
    if (state->sys.hotkey_ram_active) {
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "RAM + startup : %s", combo_lbl);
    } else if (state->sys.hotkey_active) {
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "Disque startup : %s", combo_lbl);
    }
}

bool nl_hotkey_verify_active(const char *docs_root, const nl_sys_env_t *env) {
    if (!docs_root || docs_root[0] == '\0' || !env) return false;
    if (!env->has_ndless) return false;

    char stub_path[NL_MAX_PATH_LEN];
    snprintf(stub_path, sizeof(stub_path), "%.*s/ndless/startup/nl_hotkey.tns",
             (int)(sizeof(stub_path) - 34U), docs_root);

    struct stat st;
    if (stat(stub_path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if ((size_t)st.st_size != G_HOTKEY_STUB_TNS_LEN) return false;

    if (env->self_path[0] != '\0' && !nl_file_exists(env->self_path)) {
        return false;
    }
    return true;
}

#ifdef __arm__
static void storage_wait_keys_released_safe(void) {
    unsigned loops_per_ms = (nl_hwsubtype() == 2) ? 38000U : 22000U;
    unsigned clean = 0U;
    for (unsigned i = 0U; i < 240U; i++) {
        if (!any_key_pressed() && !on_key_pressed()) {
            if (++clean >= 4U) return;
        } else {
            clean = 0U;
        }
        unsigned total = 10U * loops_per_ms;
        for (volatile unsigned k = 0U; k < total; k++) {
            __asm__ volatile("" ::: "memory");
        }
    }
}
#endif

void nl_hotkey_reconcile(nl_state_t *state) {
    if (!state) return;
    bool disk_ok = nl_hotkey_verify_active(state->sys.docs_root, &state->sys);
    bool ram_enabled = false;
    bool ram_installed = nl_hotkey_is_installed_in_ram(&ram_enabled, NULL, NULL);

    if (state->settings.global_hotkey && disk_ok) {
        if (ram_installed && !ram_enabled) {
            nl_hk_ctrl_t *ctrl = probe_ram_hk_ctrl();
            if (ctrl) {
                ctrl->combo = (uint32_t)state->settings.hotkey_combo;
                if (state->sys.self_path[0] != '\0') {
                    nl_bg_copy_str(ctrl->target_path, sizeof(ctrl->target_path), state->sys.self_path);
                }
                ctrl->enabled = 1U;
                ram_enabled = true;
            }
        }
#ifdef __arm__
        else if (!ram_installed && state->sys.has_ndless) {
            static bool s_boot_ram_load_tried = false;
            if (!s_boot_ram_load_tried) {
                s_boot_ram_load_tried = true;
                char cfg_path[NL_MAX_PATH_LEN];
                char stub_path[NL_MAX_PATH_LEN];
                const char *root = state->sys.docs_root[0] ? state->sys.docs_root : "/documents";
                snprintf(cfg_path, sizeof(cfg_path), "%.*s/ndless/nlauncher.cfg.tns",
                         (int)(sizeof(cfg_path) - 28U), root);
                snprintf(stub_path, sizeof(stub_path), "%.*s/ndless/startup/nl_hotkey.tns",
                         (int)(sizeof(stub_path) - 34U), root);
                (void)nl_save_config(state, cfg_path);
                storage_wait_keys_released_safe();
                char *hk_args[] = { "from_nl", NULL };
                (void)nl_exec(stub_path, 1, hk_args);
                ram_installed = nl_hotkey_is_installed_in_ram(&ram_enabled, NULL, NULL);
            }
        }
#endif
    } else if (!state->settings.global_hotkey && ram_installed && ram_enabled) {
        nl_hk_ctrl_t *ctrl = probe_ram_hk_ctrl();
        if (ctrl) {
            ctrl->enabled = 0U;
            ram_enabled = false;
        }
    }

    state->sys.hotkey_active = disk_ok;
    state->sys.hotkey_ram_active = (ram_installed && ram_enabled);
    if (state->settings.global_hotkey && !disk_ok && !state->sys.hotkey_ram_active) {
        state->settings.global_hotkey = false;
        state->anim.switch_hotkey_fp = 0;
    }
    const char *combo_lbl = nl_hk_combo_label((nl_hk_combo_t)state->settings.hotkey_combo);
    if (state->sys.hotkey_ram_active) {
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "RAM + startup : %s", combo_lbl);
    } else if (state->sys.hotkey_active) {
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "Startup seul (hook RAM absent)");
    } else {
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "Raccourci TI-OS desactive");
    }
}

bool nl_hotkey_set(nl_state_t *state, bool enable, char *out_diag, size_t diag_max) {
    if (!state) return false;
    const char *docs_root = state->sys.docs_root[0] ? state->sys.docs_root : "/documents";
    const char *combo_lbl = nl_hk_combo_label((nl_hk_combo_t)state->settings.hotkey_combo);

    char startup_dir[NL_MAX_PATH_LEN];
    char stub_path[NL_MAX_PATH_LEN];
    char cfg_path[NL_MAX_PATH_LEN];

    snprintf(startup_dir, sizeof(startup_dir), "%.*s/ndless/startup",
             (int)(sizeof(startup_dir) - 20U), docs_root);
    snprintf(stub_path, sizeof(stub_path), "%.*s/nl_hotkey.tns",
             (int)(sizeof(stub_path) - 18U), startup_dir);
    snprintf(cfg_path, sizeof(cfg_path), "%.*s/ndless/nlauncher.cfg.tns",
             (int)(sizeof(cfg_path) - 28U), docs_root);

    if (!enable) {
        (void)unlink(stub_path);
        nl_hk_ctrl_t *ctrl = probe_ram_hk_ctrl();
        if (ctrl) {
            ctrl->enabled = 0U;
        }
#ifndef __arm__
        g_host_hk_ram_resident = false;
#endif
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        state->sys.hotkey_ram_active = false;
        if (out_diag && diag_max > 0U) {
            snprintf(out_diag, diag_max, "Raccourci TI-OS desactive");
        }
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Desactive");
        return true;
    }

    if (!state->sys.has_ndless) {
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        state->sys.hotkey_ram_active = false;
        if (out_diag && diag_max > 0U) snprintf(out_diag, diag_max, "Ndless non detecte");
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Ndless absent");
        return false;
    }

    if (state->sys.self_path[0] == '\0' || !nl_file_exists(state->sys.self_path)) {
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        state->sys.hotkey_ram_active = false;
        if (out_diag && diag_max > 0U) {
            snprintf(out_diag, diag_max, "Binaire N-Launcher.tns introuvable");
        }
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Executable absent");
        return false;
    }

    ensure_dir_recursive(startup_dir);

    FILE *fp = fopen(stub_path, "wb");
    if (!fp) {
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        state->sys.hotkey_ram_active = false;
        if (out_diag && diag_max > 0U) {
            snprintf(out_diag, diag_max, "Erreur ecriture ndless/startup");
        }
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Erreur I/O startup");
        return false;
    }

    size_t written = fwrite(G_HOTKEY_STUB_TNS, 1U, G_HOTKEY_STUB_TNS_LEN, fp);
    fflush(fp);
    fclose(fp);

    if (written != G_HOTKEY_STUB_TNS_LEN || !nl_hotkey_verify_active(docs_root, &state->sys)) {
        (void)unlink(stub_path);
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        state->sys.hotkey_ram_active = false;
        if (out_diag && diag_max > 0U) {
            snprintf(out_diag, diag_max, "Verification nl_hotkey.tns echouee");
        }
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Echec verif");
        return false;
    }

    /* Persist global_hotkey=1 & hotkey_combo BEFORE executing nl_hotkey.tns live */
    state->settings.global_hotkey = true;
    (void)nl_save_config(state, cfg_path);

    nl_hk_ctrl_t *existing = probe_ram_hk_ctrl();
    if (existing) {
        existing->combo = (uint32_t)state->settings.hotkey_combo;
        nl_bg_copy_str(existing->docs_root, sizeof(existing->docs_root), docs_root);
        nl_bg_copy_str(existing->target_path, sizeof(existing->target_path), state->sys.self_path);
        existing->enabled = 1U;
    } else {
#ifdef __arm__
        storage_wait_keys_released_safe();
        char *hk_args[] = { "from_nl", NULL };
        (void)nl_exec(stub_path, 1, hk_args);
#else
        g_host_hk_ram_resident = true;
        g_host_hk_ctrl.enabled = 1U;
        g_host_hk_ctrl.combo = (uint32_t)state->settings.hotkey_combo;
        nl_bg_copy_str(g_host_hk_ctrl.docs_root, sizeof(g_host_hk_ctrl.docs_root), docs_root);
        nl_bg_copy_str(g_host_hk_ctrl.target_path, sizeof(g_host_hk_ctrl.target_path), state->sys.self_path);
#endif
    }

    bool ram_enabled = false;
    bool ram_ok = nl_hotkey_is_installed_in_ram(&ram_enabled, NULL, NULL) && ram_enabled;

    if (!ram_ok) {
        /* Honest failure report: live hook could not attach to get_event on this OS */
        (void)unlink(stub_path);
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        state->sys.hotkey_ram_active = false;
        (void)nl_save_config(state, cfg_path);
        if (out_diag && diag_max > 0U) {
            snprintf(out_diag, diag_max, "Echec hook RAM (E701)");
        }
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "Hook get_event impossible en RAM");
        nl_diag_report(
            state,
            NL_DIAG_E701_HOTKEY_FAILED,
            "nl_hotkey.tns",
            stub_path,
            "Installation hook TI-OS",
            "Hook get_event non installe en RAM",
            "Raccourci TI-OS desactive par securite",
            "Verifier version Ndless / OS compatible"
        );
        return false;
    }

    state->settings.global_hotkey = true;
    state->sys.hotkey_active = true;
    state->sys.hotkey_ram_active = true;
    if (out_diag && diag_max > 0U) {
        snprintf(out_diag, diag_max, "Actif en RAM + startup : %s", combo_lbl);
    }
    snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
             "RAM + startup : %s", combo_lbl);
    return true;
}

const char *nl_resolve_os_version_string(uint32_t sig_0x10000020) {
    switch (sig_0x10000020) {
        case 0x1040E4D0U: return "5.2.0.771 (CX II)";
        case 0x1040EAE0U: return "5.2.0.771 (CX II-T)";
        case 0x1040F3B0U: return "5.2.0.771 (CAS II)";
        case 0x10416CC0U: return "5.3.0.564 (CX II)";
        case 0x10417460U: return "5.3.0.564 (CX II-T)";
        case 0x10417DA0U: return "5.3.0.564 (CAS II)";
        case 0x10429E10U: return "6.2.0.333 (CX II)";
        case 0x1042A580U: return "6.2.0.333 (CX II-T)";
        case 0x1042AE10U: return "6.2.0.333 (CAS II)";
        case 0x10429EC0U: return "6.4.0.74 (CX II)";
        case 0x1042A600U: return "6.4.0.74 (CX II-T)";
        case 0x1042AEE0U: return "6.4.0.74 (CAS II)";
        case 0x103B1860U: return "4.5.0.1180 (CX)";
        case 0x103B1E60U: return "4.5.0.1180 (CAS CX)";
        case 0x103B27D0U: return "4.5.1.12 (CX)";
        case 0x103B2EF0U: return "4.5.1.12 (CAS CX)";
        case 0x103B3020U: return "4.5.3.14 (CX)";
        case 0x103B3740U: return "4.5.3.14 (CAS CX)";
        case 0x103B3A10U: return "4.5.4.48 (CX)";
        case 0x103B4130U: return "4.5.4.48 (CAS CX)";
        case 0x103B18E0U: return "4.5.5 (CX)";
        case 0x103B1F80U: return "4.5.5 (CAS CX)";
        default:          return NULL;
    }
}
