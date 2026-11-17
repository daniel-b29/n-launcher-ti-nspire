/****************************************************************************
 * N-Launcher — scanner.c
 * Binary Header Inspector (PRG\0, Zehn v1, bFLT vs TI-Nspire ZIP/XML)
 * Multi-Directory Recursive Scanner & System / Self Exclusion
 ****************************************************************************/

#include "nlauncher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

static void safe_strncpy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_icmp(const char *a, const char *b) {
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

static bool str_icontains(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return false;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static const char *path_basename(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && slash + 1 > base) base = slash + 1;
    if (bslash && bslash + 1 > base) base = bslash + 1;
    return base;
}

static bool has_tns_extension(const char *filename) {
    if (!filename) return false;
    size_t len = strlen(filename);
    if (len < 5) return false;
    return str_icmp(filename + len - 4, ".tns") == 0;
}

void nl_extract_game_stem(const char *filename, char *out_stem, size_t max_len) {
    const char *base = path_basename(filename);
    safe_strncpy(out_stem, base, max_len);
    size_t len = strlen(out_stem);
    if (len > 4 && str_icmp(out_stem + len - 4, ".tns") == 0) {
        out_stem[len - 4] = '\0';
        len -= 4;
    }
    if (len > 4 && str_icmp(out_stem + len - 4, ".prg") == 0) {
        out_stem[len - 4] = '\0';
    }
}

void nl_compute_monogram(const char *title, char out_mono[3]) {
    out_mono[0] = 'G';
    out_mono[1] = 'M';
    out_mono[2] = '\0';
    if (!title || !title[0]) return;

    char first = '\0', second = '\0';
    for (const char *p = title; *p; p++) {
        if (isalnum((unsigned char)*p)) {
            if (!first) {
                first = (char)toupper((unsigned char)*p);
            } else {
                second = (char)toupper((unsigned char)*p);
                break;
            }
        }
    }
    for (const char *p = title + 1; *p; p++) {
        if ((*p == ' ' || *p == '-') && isalnum((unsigned char)p[1])) {
            second = (char)toupper((unsigned char)p[1]);
            break;
        }
    }
    if (first) out_mono[0] = first;
    out_mono[1] = second ? second : first;
}

static void prettify_title_from_filename(const char *filename, char *out_title, size_t max_len) {
    nl_extract_game_stem(filename, out_title, max_len);
    size_t len = strlen(out_title);
    for (size_t i = 0; i < len; i++) {
        if (out_title[i] == '_') out_title[i] = ' ';
    }
    if (out_title[0] >= 'a' && out_title[0] <= 'z') {
        out_title[0] = (char)(out_title[0] - 'a' + 'A');
    }
}

bool nl_file_exists(const char *filepath) {
    if (!filepath || filepath[0] == '\0') return false;
    struct stat st;
    if (stat(filepath, &st) != 0) return false;
    return S_ISREG(st.st_mode) && st.st_size >= 16;
}

static const char *G_SYSTEM_TNS_BLACKLIST[] = {
    "ndless_resources.tns",
    "ndless_installer.tns",
    "ndless_installer_4.5.tns",
    "ndless_installer_5.2.tns",
    "ndless_installer_5.3.tns",
    "ndless_installer_5.4.tns",
    "zehn_loader.tns",
    "zehn_loader_compressed.tns",
    "nlauncher.cfg.tns",
    "nlauncher.profiles.tns",
    "nlauncher.cache.tns",
    "ndless.cfg.tns",
    "startup.tns",
    "nl_boot.tns",
    "nl_boot_stub.tns",
    "nl_hotkey.tns",
    "n-launcher.tns",
    "nlauncher.tns",
    "n_launcher.tns",
    NULL
};

bool nl_is_system_or_self(const char *filepath, const char *self_path) {
    if (!filepath) return true;
    const char *base = path_basename(filepath);

    if (self_path && self_path[0] != '\0') {
        if (str_icmp(filepath, self_path) == 0) return true;
        const char *self_base = path_basename(self_path);
        if (self_base[0] != '\0' && str_icmp(base, self_base) == 0) return true;
    }

    for (int i = 0; G_SYSTEM_TNS_BLACKLIST[i] != NULL; i++) {
        if (str_icmp(base, G_SYSTEM_TNS_BLACKLIST[i]) == 0) {
            return true;
        }
    }

    if (base[0] == '.') return true;
    if (str_icontains(base, "ndless_installer")) return true;
    if (str_icontains(base, "ndless_resources")) return true;
    if (str_icontains(base, ".cfg.tns")) return true;
    if (str_icontains(base, ".profiles.tns")) return true;
    if (str_icontains(base, ".cache.tns")) return true;
    if (str_icontains(base, ".lock.tns")) return true;
    if (str_icontains(base, ".ico.tns")) return true;
    if (str_icontains(base, ".scr.tns")) return true;
    if (str_icontains(base, ".bmp.tns")) return true;
    if (str_icontains(base, ".png.tns")) return true;
    if (str_icontains(base, ".lua.tns")) return true;
    if (str_icontains(base, ".tmp.tns")) return true;
    if (str_icontains(base, ".log.tns")) return true;
    if (str_icontains(base, ".sav.tns")) return true;
    if (str_icontains(base, ".wad.tns")) return true;
    if (str_icontains(base, ".pak.tns")) return true;
    if (str_icontains(base, ".gb.tns")) return true;
    if (str_icontains(base, ".gbc.tns")) return true;
    if (str_icontains(base, ".gba.tns")) return true;
    if (str_icontains(base, ".nes.tns")) return true;
    if (str_icontains(base, ".rom.tns")) return true;

    return false;
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static bool parse_zehn_at(
    const uint8_t *buf,
    size_t buf_len,
    size_t z_off,
    uint32_t total_file_size,
    nl_exec_info_t *out_info
) {
    if (z_off + 32 > buf_len) return false;

    const uint8_t *zh = buf + z_off;
    uint32_t sig         = read_u32_le(zh + 0);
    uint32_t version     = read_u32_le(zh + 4);
    uint32_t zehn_fsize  = read_u32_le(zh + 8);
    uint32_t reloc_count = read_u32_le(zh + 12);
    uint32_t flag_count  = read_u32_le(zh + 16);
    uint32_t extra_size  = read_u32_le(zh + 20);
    uint32_t alloc_size  = read_u32_le(zh + 24);

    if (sig != NL_MAGIC_ZEHN || version != 1) return false;
    if (zehn_fsize == 0 || (total_file_size > 0 && zehn_fsize > total_file_size)) return false;
    if (reloc_count > 65536 || flag_count > 256 || extra_size > 65536) return false;

    out_info->zehn_offset  = (uint32_t)z_off;
    out_info->zehn_version = version;
    out_info->reloc_count  = reloc_count;
    out_info->alloc_size   = alloc_size;

    size_t flag_table_off = z_off + 32 + (size_t)reloc_count * 4;
    size_t extra_data_off = flag_table_off + (size_t)flag_count * 4;

    if (flag_table_off + (size_t)flag_count * 4 <= buf_len) {
        for (uint32_t i = 0; i < flag_count; i++) {
            uint32_t raw_flag = read_u32_le(buf + flag_table_off + i * 4);
            uint8_t flag_type = (uint8_t)(raw_flag & 0xFFU);
            uint32_t flag_data = (raw_flag >> 8) & 0xFFFFFFU;

            switch (flag_type) {
                case 0:
                    out_info->ndless_min = flag_data;
                    break;
                case 8:
                    if (extra_data_off + flag_data < buf_len && flag_data < extra_size) {
                        const char *str = (const char *)(buf + extra_data_off + flag_data);
                        size_t max_avail = buf_len - (extra_data_off + flag_data);
                        size_t slen = 0;
                        while (slen < max_avail && slen < NL_MAX_NAME_LEN - 1 && str[slen] != '\0') {
                            slen++;
                        }
                        if (slen > 0 && slen < max_avail && str[slen] == '\0') {
                            safe_strncpy(out_info->exec_name, str, NL_MAX_NAME_LEN);
                        }
                    }
                    break;
                case 9:
                    if (extra_data_off + flag_data < buf_len && flag_data < extra_size) {
                        const char *str = (const char *)(buf + extra_data_off + flag_data);
                        size_t max_avail = buf_len - (extra_data_off + flag_data);
                        size_t slen = 0;
                        while (slen < max_avail && slen < NL_MAX_AUTHOR_LEN - 1 && str[slen] != '\0') {
                            slen++;
                        }
                        if (slen > 0 && slen < max_avail && str[slen] == '\0') {
                            safe_strncpy(out_info->exec_author, str, NL_MAX_AUTHOR_LEN);
                        }
                    }
                    break;
                case 10:
                    out_info->exec_version = flag_data;
                    break;
                case 12:
                    out_info->supports_hww = (flag_data != 0);
                    break;
                case 13:
                    out_info->uses_lcd_blit = (flag_data != 0);
                    break;
                default:
                    break;
            }
        }
    }

    return true;
}

nl_bin_format_t nl_inspect_tns_buffer(
    const uint8_t *buf,
    size_t buf_len,
    uint32_t total_file_size,
    const char *filename,
    const char *self_path,
    nl_exec_info_t *out_info
) {
    nl_exec_info_t local_info;
    if (!out_info) out_info = &local_info;
    memset(out_info, 0, sizeof(*out_info));
    out_info->file_size = total_file_size;

    if (!buf || buf_len < 16 || !filename) {
        out_info->format = NL_BIN_INVALID;
        return NL_BIN_INVALID;
    }

    if (!has_tns_extension(filename)) {
        out_info->format = NL_BIN_INVALID;
        return NL_BIN_INVALID;
    }

    if (nl_is_system_or_self(filename, self_path)) {
        out_info->format = NL_BIN_SYSTEM;
        return NL_BIN_SYSTEM;
    }

    uint32_t magic = read_u32_le(buf);

    if (magic == NL_MAGIC_PKZIP ||
        memcmp(buf, "*TIM", 4) == 0 ||
        memcmp(buf, "<?xm", 4) == 0 ||
        memcmp(buf, "\x0F\x0E", 2) == 0) {
        out_info->format = NL_BIN_TI_DOC;
        return NL_BIN_TI_DOC;
    }

    if (magic == NL_MAGIC_ZEHN) {
        if (parse_zehn_at(buf, buf_len, 0, total_file_size, out_info)) {
            out_info->format = NL_BIN_ZEHN;
            return NL_BIN_ZEHN;
        }
        out_info->format = NL_BIN_INVALID;
        return NL_BIN_INVALID;
    }

    if (magic == NL_MAGIC_PRG) {
        size_t scan_limit = buf_len > 2048 ? 2048 : buf_len;
        for (size_t off = 4; off + 32 <= scan_limit; off += 4) {
            if (read_u32_le(buf + off) == NL_MAGIC_ZEHN) {
                if (parse_zehn_at(buf, buf_len, off, total_file_size, out_info)) {
                    out_info->format = NL_BIN_ZEHN;
                    return NL_BIN_ZEHN;
                }
            }
        }
        out_info->format = NL_BIN_PRG;
        out_info->alloc_size = total_file_size;
        return NL_BIN_PRG;
    }

    if (magic == NL_MAGIC_BFLT && buf_len >= 64) {
        uint32_t rev = read_u32_be(buf + 4);
        if (rev == 4) {
            out_info->format = NL_BIN_BFLT;
            out_info->alloc_size = total_file_size;
            return NL_BIN_BFLT;
        }
    }

    out_info->format = NL_BIN_INVALID;
    return NL_BIN_INVALID;
}

nl_bin_format_t nl_inspect_tns_file(
    const char *filepath,
    const char *self_path,
    nl_exec_info_t *out_info
) {
    if (!filepath) return NL_BIN_INVALID;
    if (nl_is_system_or_self(filepath, self_path)) {
        if (out_info) {
            memset(out_info, 0, sizeof(*out_info));
            out_info->format = NL_BIN_SYSTEM;
        }
        return NL_BIN_SYSTEM;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NL_BIN_INVALID;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NL_BIN_INVALID;
    }
    long fsize = ftell(fp);
    if (fsize < 16) {
        fclose(fp);
        return NL_BIN_INVALID;
    }
    fseek(fp, 0, SEEK_SET);

    uint8_t *header_buf = (uint8_t *)malloc(16384U);
    if (!header_buf) {
        fclose(fp);
        return NL_BIN_INVALID;
    }
    size_t to_read = (size_t)fsize < 16384U ? (size_t)fsize : 16384U;
    size_t nread = fread(header_buf, 1, to_read, fp);
    fclose(fp);

    nl_bin_format_t res = nl_inspect_tns_buffer(header_buf, nread, (uint32_t)fsize, filepath, self_path, out_info);
    free(header_buf);
    return res;
}

static uint16_t pick_accent_color(const char *title) {
    uint32_t h = 2166136261U;
    for (const char *p = title; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619U;
    }
    static const uint16_t palette[6] = {
        RGB565(56, 189, 248),
        RGB565(52, 211, 153),
        RGB565(251, 146, 60),
        RGB565(167, 139, 250),
        RGB565(244, 114, 182),
        RGB565(250, 204, 21)
    };
    return palette[h % 6];
}

int nl_register_game(nl_state_t *state, const char *filepath, const nl_exec_info_t *info) {
    if (!state || !filepath || !info) return -1;
    if (info->format != NL_BIN_ZEHN && info->format != NL_BIN_PRG && info->format != NL_BIN_BFLT) {
        return -1;
    }
    if (state->total_games >= NL_MAX_GAMES) {
        return -1;
    }

    const char *base = path_basename(filepath);
    char candidate_title[NL_MAX_NAME_LEN];
    if (info->exec_name[0] != '\0') {
        safe_strncpy(candidate_title, info->exec_name, sizeof(candidate_title));
    } else {
        prettify_title_from_filename(base, candidate_title, sizeof(candidate_title));
    }

    for (int i = 0; i < state->total_games; i++) {
        if (str_icmp(state->games[i].path, filepath) == 0) {
            state->rejected_dups++;
            return -1;
        }
        if (str_icmp(state->games[i].filename, base) == 0 &&
            state->games[i].size_bytes == info->file_size) {
            state->rejected_dups++;
            return -1;
        }
    }

    nl_game_t *g = &state->games[state->total_games];
    memset(g, 0, sizeof(*g));
    safe_strncpy(g->title, candidate_title, sizeof(g->title));
    safe_strncpy(g->filename, base, sizeof(g->filename));
    safe_strncpy(g->path, filepath, sizeof(g->path));
    if (info->exec_author[0] != '\0') {
        safe_strncpy(g->author, info->exec_author, sizeof(g->author));
    } else {
        safe_strncpy(g->author, "Ndless", sizeof(g->author));
    }

    g->size_bytes    = info->file_size;
    g->bin_format    = info->format;
    g->exec_version  = info->exec_version > 0 ? info->exec_version : 1;
    g->uses_lcd_blit = info->uses_lcd_blit;
    g->supports_hww  = info->supports_hww;
    g->accent_color  = pick_accent_color(g->title);
    if (g->added_order == 0U) {
        g->added_order = (uint32_t)(state->total_games + 1);
    }

    int idx = state->total_games;
    state->total_games++;
    return idx;
}

static int compare_games_alpha(const void *a, const void *b) {
    const nl_game_t *ga = (const nl_game_t *)a;
    const nl_game_t *gb = (const nl_game_t *)b;
    return str_icmp(ga->title, gb->title);
}

static void scan_dir_recursive(
    nl_state_t *state,
    const char *dir_path,
    const char *self_path,
    int depth,
    int max_depth,
    nl_progress_cb_t progress_cb,
    void *progress_ctx
) {
    if (depth > max_depth || state->total_games >= NL_MAX_GAMES) return;

    DIR *dp = opendir(dir_path);
    if (!dp) return;

    struct dirent *ep;
    while ((ep = readdir(dp)) != NULL) {
        if (progress_cb) progress_cb(progress_ctx);

        const char *dname = ep->d_name;
        if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0) continue;
        if (dname[0] == '.') continue;

        char full_path[NL_MAX_PATH_LEN];
        size_t dlen = strlen(dir_path);
        size_t nlen = strlen(dname);
        bool has_slash = (dlen > 0 && dir_path[dlen - 1] == '/');
        if (dlen + (has_slash ? 0 : 1) + nlen + 1 >= sizeof(full_path)) {
            continue;
        }
        memcpy(full_path, dir_path, dlen);
        size_t pos = dlen;
        if (!has_slash) {
            full_path[pos++] = '/';
        }
        memcpy(full_path + pos, dname, nlen + 1);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (str_icmp(dname, "System") == 0 ||
                str_icmp(dname, "lost+found") == 0 ||
                str_icmp(dname, "covers") == 0 ||
                str_icmp(dname, "startup") == 0) {
                continue;
            }
            scan_dir_recursive(state, full_path, self_path, depth + 1, max_depth, progress_cb, progress_ctx);
        } else if (S_ISREG(st.st_mode)) {
            if (!has_tns_extension(dname)) continue;
            state->scanned_files++;

            nl_exec_info_t info;
            nl_bin_format_t fmt = nl_inspect_tns_file(full_path, self_path, &info);
            if (fmt == NL_BIN_ZEHN || fmt == NL_BIN_PRG || fmt == NL_BIN_BFLT) {
                int added_idx = nl_register_game(state, full_path, &info);
                if (added_idx >= 0) {
                    if (st.st_mtim.tv_sec >= 946684800L) {
                        state->games[added_idx].added_order = (uint32_t)st.st_mtim.tv_sec;
                    }
                }
            } else if (fmt == NL_BIN_TI_DOC) {
                state->rejected_docs++;
            } else if (fmt == NL_BIN_SYSTEM) {
                state->rejected_sys++;
            }
            if (progress_cb) progress_cb(progress_ctx);
        }
    }

    closedir(dp);
}

int nl_scan_directory_ex(
    nl_state_t *state,
    const char *root_dir,
    const char *self_path,
    int max_depth,
    nl_progress_cb_t progress_cb,
    void *progress_ctx
) {
    if (!state || !root_dir) return 0;

    int before = state->total_games;
    scan_dir_recursive(state, root_dir, self_path, 0, max_depth, progress_cb, progress_ctx);

    if (state->total_games > 1) {
        qsort(state->games, (size_t)state->total_games, sizeof(nl_game_t), compare_games_alpha);
    }

    nl_rebuild_filter(state);
    if (progress_cb) progress_cb(progress_ctx);
    return state->total_games - before;
}

int nl_scan_directory(nl_state_t *state, const char *root_dir, const char *self_path, int max_depth) {
    return nl_scan_directory_ex(state, root_dir, self_path, max_depth, NULL, NULL);
}
