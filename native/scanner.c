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
    "nlauncher.lib.tns",
    "nlauncher.stage.tns",
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
    if (str_icontains(base, ".lib.tns")) return true;
    if (str_icontains(base, ".stage.tns")) return true;
    if (str_icontains(base, ".lock.tns")) return true;
    if (str_icontains(base, ".ico.tns")) return true;
    if (str_icontains(base, ".scr.tns")) return true;
    if (str_icontains(base, ".bmp.tns")) return true;
    if (str_icontains(base, ".png.tns")) return true;
    if (str_icontains(base, ".lua.tns")) return true;
    if (str_icontains(base, ".tmp.tns")) return true;
    if (str_icontains(base, ".bak.tns")) return true;
    if (str_icontains(base, ".log.tns")) return true;
    if (str_icontains(base, ".sav.tns")) return true;
    if (str_icontains(base, ".wad.tns")) return true;
    if (str_icontains(base, ".pak.tns")) return true;
    if (str_icontains(base, ".gb.tns")) return true;
    if (str_icontains(base, ".gbc.tns")) return true;
    if (str_icontains(base, ".gba.tns")) return true;
    if (str_icontains(base, ".nes.tns")) return true;
    if (str_icontains(base, ".rom.tns")) return true;
    if (str_icontains(base, ".smc.tns")) return true;
    if (str_icontains(base, ".sfc.tns")) return true;
    if (str_icontains(base, ".z64.tns")) return true;
    if (str_icontains(base, ".n64.tns")) return true;
    if (str_icontains(base, ".nds.tns")) return true;
    if (str_icontains(base, ".sms.tns")) return true;
    if (str_icontains(base, ".gg.tns")) return true;
    if (str_icontains(base, ".pce.tns")) return true;
    if (str_icontains(base, ".bin.tns")) return true;
    if (str_icontains(base, ".dat.tns")) return true;
    if (str_icontains(base, ".txt.tns")) return true;
    if (str_icontains(base, ".ini.tns")) return true;
    if (str_icontains(base, ".zip.tns")) return true;
    if (str_icontains(base, ".srm.tns")) return true;
    if (str_icontains(base, ".state.tns")) return true;
    if (str_icontains(base, ".raw.tns")) return true;
    if (str_icontains(base, ".map.tns")) return true;
    if (str_icontains(base, ".lvl.tns")) return true;
    if (str_icontains(base, ".nlic.tns")) return true;
    if (str_icontains(base, ".csv.tns")) return true;

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

static nl_bin_format_t inspect_tns_file_with_buf(
    const char *filepath,
    const char *self_path,
    nl_exec_info_t *out_info,
    uint8_t *reusable_buf,
    size_t reusable_cap,
    uint32_t known_file_size
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

    uint32_t fsize = known_file_size;
    if (fsize == 0U) {
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            return NL_BIN_INVALID;
        }
        long sz_l = ftell(fp);
        if (sz_l < 16L || fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp);
            return NL_BIN_INVALID;
        }
        fsize = (uint32_t)sz_l;
    } else if (fsize < 16U) {
        fclose(fp);
        return NL_BIN_INVALID;
    }

    /* Stage 1: Ultra-fast 64-byte magic probe (avoids reading 4-16 KB from non-game TI docs/data) */
    uint8_t probe[64];
    size_t probe_want = (fsize < sizeof(probe)) ? (size_t)fsize : sizeof(probe);
    size_t probe_got = fread(probe, 1U, probe_want, fp);
    if (probe_got < 16U) {
        fclose(fp);
        return NL_BIN_INVALID;
    }

    uint32_t magic = read_u32_le(probe);
    bool needs_full_hdr = (magic == NL_MAGIC_ZEHN || magic == NL_MAGIC_PRG);
    if (!needs_full_hdr) {
        fclose(fp);
        return nl_inspect_tns_buffer(probe, probe_got, fsize, filepath, self_path, out_info);
    }

    /* Stage 2: Read up to 4096 bytes for Zehn/PRG flags + metadata (4x faster on NAND flash) */
    uint8_t *heap_hdr = NULL;
    uint8_t *header_buf = reusable_buf;
    size_t max_buf = reusable_cap;
    if (!header_buf || max_buf < 512U) {
        heap_hdr = (uint8_t *)malloc(4096U);
        if (heap_hdr) {
            header_buf = heap_hdr;
            max_buf = 4096U;
        } else {
            fclose(fp);
            return nl_inspect_tns_buffer(probe, probe_got, fsize, filepath, self_path, out_info);
        }
    }
    if (max_buf > 4096U) max_buf = 4096U;

    memcpy(header_buf, probe, probe_got);
    size_t total_read = probe_got;
    size_t target_read = ((size_t)fsize < max_buf) ? (size_t)fsize : max_buf;
    if (target_read > probe_got) {
        size_t extra = fread(header_buf + probe_got, 1U, target_read - probe_got, fp);
        total_read += extra;
    }
    fclose(fp);

    nl_bin_format_t res = nl_inspect_tns_buffer(header_buf, total_read, fsize, filepath, self_path, out_info);
    free(heap_hdr);
    return res;
}

nl_bin_format_t nl_inspect_tns_file(
    const char *filepath,
    const char *self_path,
    nl_exec_info_t *out_info
) {
    return inspect_tns_file_with_buf(filepath, self_path, out_info, NULL, 0U, 0U);
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

static void normalize_dir_path(const char *src, char *dst, size_t max_len) {
    if (!dst || max_len == 0U) return;
    dst[0] = '\0';
    if (!src || src[0] == '\0') return;
    safe_strncpy(dst, src, max_len);
    size_t len = strlen(dst);
    while (len > 1U && (dst[len - 1U] == '/' || dst[len - 1U] == '\\')) {
        dst[--len] = '\0';
    }
}

static void extract_parent_dir(const char *filepath, char *out_dir, size_t max_len) {
    if (!out_dir || max_len == 0U) return;
    out_dir[0] = '\0';
    if (!filepath || filepath[0] == '\0') return;
    safe_strncpy(out_dir, filepath, max_len);
    char *slash = strrchr(out_dir, '/');
    char *bslash = strrchr(out_dir, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash && slash != out_dir) {
        *slash = '\0';
    } else if (slash == out_dir) {
        out_dir[1] = '\0';
    } else {
        out_dir[0] = '\0';
    }
}

bool nl_folder_add(nl_folder_list_t *list, const char *dir_path) {
    if (!list || !dir_path || dir_path[0] == '\0') return false;
    char norm[NL_MAX_PATH_LEN];
    normalize_dir_path(dir_path, norm, sizeof(norm));
    if (norm[0] == '\0') return false;

    for (int i = 0; i < list->count; i++) {
        if (str_icmp(list->paths[i], norm) == 0) {
            return true; /* Already present */
        }
    }
    if (list->count >= NL_MAX_GAME_FOLDERS) {
        return false;
    }
    safe_strncpy(list->paths[list->count], norm, NL_MAX_PATH_LEN);
    list->count++;
    return true;
}

void nl_folder_seed_defaults(nl_folder_list_t *list, const char *docs_root) {
    if (!list) return;
    const char *root = (docs_root && docs_root[0] != '\0') ? docs_root : "/documents";
    char buf[NL_MAX_PATH_LEN];

    snprintf(buf, sizeof(buf), "%.*s/games", (int)(sizeof(buf) - 8U), root);
    (void)nl_folder_add(list, buf);
    snprintf(buf, sizeof(buf), "%.*s/ndless", (int)(sizeof(buf) - 9U), root);
    (void)nl_folder_add(list, buf);
    snprintf(buf, sizeof(buf), "%.*s/roms", (int)(sizeof(buf) - 7U), root);
    (void)nl_folder_add(list, buf);
    (void)nl_folder_add(list, root);
}

#define NL_SCAN_QUEUE_CAP      64
#define NL_SCAN_DIR_SNAP_MAX   512
#define NL_SCAN_DIR_ARENA_CAP  24576U

typedef struct {
    char path[NL_MAX_PATH_LEN];
    int  depth;
} nl_dir_queue_item_t;

typedef struct {
    nl_game_t           *games;
    int                 *total_games;
    nl_folder_list_t    *discovered_folders;
    nl_state_t          *diag_state;
    nl_scan_stats_t     *stats;
    const char          *self_path;
    nl_progress_cb_t     legacy_cb;
    void                *legacy_ctx;
    nl_scan_progress_cb_t scan_cb;
    void                *scan_ctx;
    uint8_t             *read_buf;
    size_t               read_buf_cap;
    nl_dir_queue_item_t *queue;
    char                *dir_arena;
    uint16_t             dir_offs[NL_SCAN_DIR_SNAP_MAX];
    char                 visited[NL_SCAN_QUEUE_CAP][NL_MAX_PATH_LEN];
    int                  visited_count;
    /* Heap-resident scratch buffers: keeps scan_dir_iterative ARM stack < 200 B! */
    char                 scratch_norm_root[NL_MAX_PATH_LEN];
    char                 scratch_full_path[NL_MAX_PATH_LEN];
    char                 scratch_parent_dir[NL_MAX_PATH_LEN];
    nl_exec_info_t       scratch_info;
} nl_iter_scan_ctx_t;

static bool iter_already_visited(const nl_iter_scan_ctx_t *ctx, const char *norm_dir) {
    for (int i = 0; i < ctx->visited_count; i++) {
        if (str_icmp(ctx->visited[i], norm_dir) == 0) return true;
    }
    return false;
}

static void iter_mark_visited(nl_iter_scan_ctx_t *ctx, const char *norm_dir) {
    if (ctx->visited_count < NL_SCAN_QUEUE_CAP) {
        safe_strncpy(ctx->visited[ctx->visited_count], norm_dir, NL_MAX_PATH_LEN);
        ctx->visited_count++;
    }
}

static int register_game_into_array(
    nl_game_t *games,
    int *total_games,
    nl_state_t *diag_state,
    const char *filepath,
    const nl_exec_info_t *info,
    uint32_t mtime_order
) {
    if (!games || !total_games || !filepath || !info) return -1;
    if (info->format != NL_BIN_ZEHN && info->format != NL_BIN_PRG && info->format != NL_BIN_BFLT) {
        return -1;
    }
    if (*total_games >= NL_MAX_GAMES) {
        return -1;
    }

    const char *base = path_basename(filepath);
    char candidate_title[NL_MAX_NAME_LEN];
    if (info->exec_name[0] != '\0') {
        safe_strncpy(candidate_title, info->exec_name, sizeof(candidate_title));
    } else {
        prettify_title_from_filename(base, candidate_title, sizeof(candidate_title));
    }

    for (int i = 0; i < *total_games; i++) {
        if (str_icmp(games[i].path, filepath) == 0) {
            if (diag_state) diag_state->rejected_dups++;
            return -1;
        }
        if (str_icmp(games[i].filename, base) == 0 &&
            games[i].size_bytes == info->file_size) {
            if (diag_state) diag_state->rejected_dups++;
            return -1;
        }
    }

    nl_game_t *g = &games[*total_games];
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
    g->added_order   = (mtime_order > 0U) ? mtime_order : (uint32_t)(*total_games + 1);

    int idx = *total_games;
    (*total_games)++;
    return idx;
}

static bool is_optional_default_folder(const char *dir_path, const char *docs_root) {
    if (!dir_path || !docs_root) return false;
    char norm_d[NL_MAX_PATH_LEN];
    char norm_r[NL_MAX_PATH_LEN];
    normalize_dir_path(dir_path, norm_d, sizeof(norm_d));
    normalize_dir_path(docs_root, norm_r, sizeof(norm_r));
    if (str_icmp(norm_d, norm_r) == 0) return true;
    size_t rlen = strlen(norm_r);
    if (strlen(norm_d) > rlen && norm_d[rlen] == '/') {
        const char *sub = norm_d + rlen + 1;
        if (str_icmp(sub, "games") == 0 ||
            str_icmp(sub, "roms") == 0 ||
            str_icmp(sub, "ndless") == 0) {
            return true;
        }
    }
    return false;
}

/* Two-phase Nucleus-safe directory scanner:
 * Phase 1 snapshots all entry names from opendir()/readdir() and IMMEDIATELY calls closedir()
 * BEFORE any stat(), fopen(), diag report, or UI callback executes!
 * This eliminates Nucleus FATFS re-entrancy / DSTAT corruption crashes on TI-OS 6.2.0. */
static void scan_dir_iterative(
    nl_iter_scan_ctx_t *ctx,
    const char *root_dir,
    int max_depth,
    bool report_missing_root
) {
    if (!ctx || !root_dir || root_dir[0] == '\0' || !ctx->queue) return;
    if (ctx->stats && ctx->stats->abort_requested) return;

    normalize_dir_path(root_dir, ctx->scratch_norm_root, sizeof(ctx->scratch_norm_root));
    if (ctx->scratch_norm_root[0] == '\0' || iter_already_visited(ctx, ctx->scratch_norm_root)) {
        return;
    }

    int q_head = 0;
    int q_tail = 0;
    safe_strncpy(ctx->queue[q_tail].path, ctx->scratch_norm_root, NL_MAX_PATH_LEN);
    ctx->queue[q_tail].depth = 0;
    q_tail++;
    iter_mark_visited(ctx, ctx->scratch_norm_root);

    while (q_head < q_tail && *ctx->total_games < NL_MAX_GAMES) {
        if (ctx->stats && ctx->stats->abort_requested) break;
        nl_disable_watchdog_safe();

        const char *dir_path = ctx->queue[q_head].path;
        int depth = ctx->queue[q_head].depth;
        q_head++;

        if (ctx->stats) {
            safe_strncpy(ctx->stats->current_dir, dir_path, sizeof(ctx->stats->current_dir));
        }

        /* Phase 1: Snapshot directory names and CLOSE DIR handle immediately */
        DIR *dp = opendir(dir_path);
        if (!dp) {
            if (report_missing_root && depth == 0) {
                if (ctx->stats) {
                    ctx->stats->errors_count++;
                    snprintf(ctx->stats->last_error, sizeof(ctx->stats->last_error),
                             "Dossier absent: %.44s", path_basename(dir_path));
                }
                if (ctx->diag_state) {
                    nl_diag_report(
                        ctx->diag_state,
                        NL_DIAG_E401_DIR_MISSING,
                        path_basename(dir_path),
                        dir_path,
                        "Verifier dossier de jeux",
                        "Dossier de jeux introuvable",
                        "Autres dossiers toujours scannes",
                        "Dossier ignore sans erreur fatale"
                    );
                }
            }
            continue;
        }

        int entry_count = 0;
        size_t arena_used = 0U;
        struct dirent *ep;
        while ((ep = readdir(dp)) != NULL) {
            const char *dname = ep->d_name;
            if (!dname || dname[0] == '\0' || dname[0] == '.') continue;
            if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0) continue;
            if (!ctx->dir_arena || entry_count >= NL_SCAN_DIR_SNAP_MAX) break;

            size_t nlen = strlen(dname);
            if (nlen == 0U || nlen >= 200U) continue;
            if (arena_used + nlen + 1U > NL_SCAN_DIR_ARENA_CAP) break;

            ctx->dir_offs[entry_count++] = (uint16_t)arena_used;
            memcpy(ctx->dir_arena + arena_used, dname, nlen + 1U);
            arena_used += nlen + 1U;
        }
        closedir(dp);
        dp = NULL;

        if (ctx->stats) {
            ctx->stats->dirs_scanned++;
            if (ctx->scan_cb) ctx->scan_cb(ctx->stats, ctx->scan_ctx);
            if (ctx->stats->abort_requested) break;
        }

        /* Phase 2: Process snapshotted entries with ZERO open DIR handles! */
        size_t dlen = strlen(dir_path);
        bool has_slash = (dlen > 0U && dir_path[dlen - 1U] == '/');

        for (int e_i = 0; e_i < entry_count; e_i++) {
            if (*ctx->total_games >= NL_MAX_GAMES) break;
            if (ctx->stats && ctx->stats->abort_requested) break;
            if ((e_i & 7) == 0) nl_disable_watchdog_safe();
            if (ctx->legacy_cb) ctx->legacy_cb(ctx->legacy_ctx);

            const char *dname = ctx->dir_arena + ctx->dir_offs[e_i];
            size_t nlen = strlen(dname);
            if (dlen + (has_slash ? 0U : 1U) + nlen + 1U >= sizeof(ctx->scratch_full_path)) {
                if (ctx->stats) ctx->stats->files_ignored++;
                continue;
            }
            memcpy(ctx->scratch_full_path, dir_path, dlen);
            size_t pos = dlen;
            if (!has_slash) {
                ctx->scratch_full_path[pos++] = '/';
            }
            memcpy(ctx->scratch_full_path + pos, dname, nlen + 1U);

            /* Fast-reject known system/config/ROM .tns files BEFORE calling stat() or fopen() */
            bool is_tns = has_tns_extension(dname);
            if (is_tns && nl_is_system_or_self(ctx->scratch_full_path, ctx->self_path)) {
                if (ctx->diag_state) ctx->diag_state->rejected_sys++;
                if (ctx->stats) {
                    ctx->stats->files_examined++;
                    ctx->stats->files_ignored++;
                    if (ctx->scan_cb) ctx->scan_cb(ctx->stats, ctx->scan_ctx);
                }
                continue;
            }

            struct stat st;
            if (stat(ctx->scratch_full_path, &st) != 0) {
                if (ctx->stats) ctx->stats->files_ignored++;
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                if (str_icmp(dname, "System") == 0 ||
                    str_icmp(dname, "lost+found") == 0 ||
                    str_icmp(dname, "covers") == 0 ||
                    str_icmp(dname, "icons") == 0 ||
                    str_icmp(dname, "startup") == 0) {
                    continue;
                }
                if (depth < max_depth && q_tail < NL_SCAN_QUEUE_CAP &&
                    !iter_already_visited(ctx, ctx->scratch_full_path)) {
                    safe_strncpy(ctx->queue[q_tail].path, ctx->scratch_full_path, NL_MAX_PATH_LEN);
                    ctx->queue[q_tail].depth = depth + 1;
                    q_tail++;
                    iter_mark_visited(ctx, ctx->scratch_full_path);
                }
            } else if (S_ISREG(st.st_mode)) {
                if (ctx->stats) ctx->stats->files_examined++;
                if (!is_tns) {
                    if (ctx->stats) {
                        ctx->stats->files_ignored++;
                        if (ctx->scan_cb) ctx->scan_cb(ctx->stats, ctx->scan_ctx);
                    }
                    continue;
                }
                if (ctx->diag_state) ctx->diag_state->scanned_files++;

                uint32_t known_sz = (st.st_size > 0 && st.st_size < 0x7FFFFFFF)
                    ? (uint32_t)st.st_size : 0U;
                nl_bin_format_t fmt = inspect_tns_file_with_buf(
                    ctx->scratch_full_path,
                    ctx->self_path,
                    &ctx->scratch_info,
                    ctx->read_buf,
                    ctx->read_buf_cap,
                    known_sz
                );
                if (fmt == NL_BIN_ZEHN || fmt == NL_BIN_PRG || fmt == NL_BIN_BFLT) {
                    uint32_t mtime_order = 0U;
                    if (st.st_mtim.tv_sec >= 946684800L) {
                        mtime_order = (uint32_t)st.st_mtim.tv_sec;
                    }
                    int added_idx = register_game_into_array(
                        ctx->games, ctx->total_games, ctx->diag_state,
                        ctx->scratch_full_path, &ctx->scratch_info, mtime_order);
                    if (added_idx >= 0) {
                        if (ctx->stats) ctx->stats->games_found = *ctx->total_games;
                        if (ctx->discovered_folders) {
                            extract_parent_dir(ctx->scratch_full_path,
                                               ctx->scratch_parent_dir,
                                               sizeof(ctx->scratch_parent_dir));
                            if (ctx->scratch_parent_dir[0] != '\0') {
                                (void)nl_folder_add(ctx->discovered_folders, ctx->scratch_parent_dir);
                            }
                        }
                    } else if (ctx->stats) {
                        ctx->stats->files_ignored++;
                    }
                } else if (fmt == NL_BIN_TI_DOC) {
                    if (ctx->diag_state) ctx->diag_state->rejected_docs++;
                    if (ctx->stats) ctx->stats->files_ignored++;
                } else if (fmt == NL_BIN_SYSTEM) {
                    if (ctx->diag_state) ctx->diag_state->rejected_sys++;
                    if (ctx->stats) ctx->stats->files_ignored++;
                } else {
                    if (ctx->stats) {
                        ctx->stats->files_ignored++;
                        ctx->stats->errors_count++;
                        snprintf(ctx->stats->last_error, sizeof(ctx->stats->last_error),
                                 "Invalide: %.48s", dname);
                    }
                    if (ctx->diag_state) {
                        nl_diag_report(
                            ctx->diag_state,
                            NL_DIAG_E201_GAME_MALFORMED,
                            dname,
                            ctx->scratch_full_path,
                            "Analyser executable .tns",
                            "En-tete .tns invalide ou illisible",
                            "Jeu exclu sans bloquer les autres",
                            "Jeu isole et scan poursuivi"
                        );
                    }
                }

                if (ctx->legacy_cb) ctx->legacy_cb(ctx->legacy_ctx);
                /* Pump SP804-paced splash/UI animation on EVERY file so animation stays 60 FPS! */
                if (ctx->stats && ctx->scan_cb) {
                    ctx->scan_cb(ctx->stats, ctx->scan_ctx);
                }
            }
        }
    }
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

    nl_iter_scan_ctx_t *ctx = (nl_iter_scan_ctx_t *)calloc(1U, sizeof(nl_iter_scan_ctx_t));
    if (!ctx) return 0;
    ctx->queue = (nl_dir_queue_item_t *)calloc(NL_SCAN_QUEUE_CAP, sizeof(nl_dir_queue_item_t));
    ctx->dir_arena = (char *)malloc(NL_SCAN_DIR_ARENA_CAP);
    ctx->read_buf = (uint8_t *)malloc(4096U);
    if (!ctx->queue || !ctx->dir_arena) {
        free(ctx->read_buf);
        free(ctx->dir_arena);
        free(ctx->queue);
        free(ctx);
        return 0;
    }

    ctx->games              = state->games;
    ctx->total_games        = &state->total_games;
    ctx->discovered_folders = &state->folders;
    ctx->diag_state         = state;
    ctx->stats              = NULL;
    ctx->self_path          = self_path;
    ctx->legacy_cb          = progress_cb;
    ctx->legacy_ctx         = progress_ctx;
    ctx->read_buf_cap       = ctx->read_buf ? 4096U : 0U;

    int before = state->total_games;
    bool prev_defer = state->diag.defer_flash_log;
    state->diag.defer_flash_log = true;
    scan_dir_iterative(ctx, root_dir, max_depth, false);
    state->diag.defer_flash_log = prev_defer;

    free(ctx->read_buf);
    free(ctx->dir_arena);
    free(ctx->queue);
    free(ctx);

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

static void clamp_ui_cursors_after_library_change(nl_state_t *state) {
    if (!state) return;
    nl_rebuild_filter(state);
    if (state->total_games <= 0) {
        state->selected_pos = 0;
        state->current_page = 0;
        state->sui.icon_cursor = 0;
        state->sui.icon_scroll = 0;
        state->sui.sc_game_cursor = 0;
        state->sui.sc_game_scroll = 0;
    } else {
        if (state->sui.icon_cursor >= state->total_games) {
            state->sui.icon_cursor = state->total_games - 1;
        }
        if (state->sui.icon_cursor < 0) state->sui.icon_cursor = 0;
        if (state->sui.sc_game_cursor >= state->total_games) {
            state->sui.sc_game_cursor = state->total_games - 1;
        }
        if (state->sui.sc_game_cursor < 0) state->sui.sc_game_cursor = 0;
    }
    nl_mouse_update_hover(state);
}

bool nl_transactional_scan(
    nl_state_t *state,
    const char *docs_root,
    const char *self_path,
    bool is_startup,
    bool force_allow_empty,
    nl_scan_progress_cb_t progress_cb,
    void *progress_ctx
) {
    if (!state) return false;
    const char *root = (docs_root && docs_root[0] != '\0')
        ? docs_root
        : (state->sys.docs_root[0] != '\0' ? state->sys.docs_root : "/documents");

    /* Prevent re-entrant concurrent scans */
    if (state->scan.active) {
        return false;
    }

    memset(&state->scan, 0, sizeof(state->scan));
    state->scan.active = true;
    state->scan.is_startup = is_startup;
    state->scan.prev_games_count = state->total_games;
    safe_strncpy(state->scan.current_dir, root, sizeof(state->scan.current_dir));

    /* Allocate ALL temporary scan state on heap; existing state->games[] stays 100% untouched! */
    nl_game_t *temp_games = (nl_game_t *)calloc(NL_MAX_GAMES, sizeof(nl_game_t));
    nl_folder_list_t *temp_folders = (nl_folder_list_t *)calloc(1U, sizeof(nl_folder_list_t));
    nl_iter_scan_ctx_t *ctx = (nl_iter_scan_ctx_t *)calloc(1U, sizeof(nl_iter_scan_ctx_t));
    nl_dir_queue_item_t *queue = (nl_dir_queue_item_t *)calloc(NL_SCAN_QUEUE_CAP, sizeof(nl_dir_queue_item_t));
    char *dir_arena = (char *)malloc(NL_SCAN_DIR_ARENA_CAP);
    uint8_t *read_buf = (uint8_t *)malloc(4096U);

    if (!temp_games || !temp_folders || !ctx || !queue || !dir_arena) {
        free(temp_games);
        free(temp_folders);
        free(ctx);
        free(queue);
        free(dir_arena);
        free(read_buf);
        state->scan.active = false;
        state->scan.finished = true;
        state->scan.cancelled_or_error = true;
        state->scan.errors_count = 1;
        snprintf(state->scan.last_error, sizeof(state->scan.last_error), "Memoire insuffisante");
        nl_recover_memory_on_oom(state, "Scan bibliotheque", root);
        return false;
    }

    /* Preserve previously registered game folders in temp_folders */
    for (int f = 0; f < state->folders.count && f < NL_MAX_GAME_FOLDERS; f++) {
        (void)nl_folder_add(temp_folders, state->folders.paths[f]);
    }

    int temp_total = 0;
    ctx->games              = temp_games;
    ctx->total_games        = &temp_total;
    ctx->discovered_folders = temp_folders;
    ctx->diag_state         = state;
    ctx->stats              = &state->scan;
    ctx->self_path          = self_path;
    ctx->scan_cb            = progress_cb;
    ctx->scan_ctx           = progress_ctx;
    ctx->read_buf           = read_buf;
    ctx->read_buf_cap       = read_buf ? 4096U : 0U;
    ctx->queue              = queue;
    ctx->dir_arena          = dir_arena;

    /* Suppress per-file NAND Flash diag log writes during scan (RAM diag entries still recorded!) */
    bool prev_defer_flash = state->diag.defer_flash_log;
    state->diag.defer_flash_log = true;

    if (progress_cb) progress_cb(&state->scan, progress_ctx);

    /* 1. Verify and scan every explicitly saved folder first (never report E401 on optional defaults) */
    for (int f = 0; f < state->folders.count && f < NL_MAX_GAME_FOLDERS; f++) {
        if (state->scan.abort_requested) break;
        if (state->folders.paths[f][0] != '\0') {
            bool report_missing = !is_optional_default_folder(state->folders.paths[f], root);
            scan_dir_iterative(ctx, state->folders.paths[f], 3, report_missing);
        }
    }

    /* 2. Scan standard priority game directories + full documents root */
    if (!state->scan.abort_requested) {
        char sub_path[NL_MAX_PATH_LEN];
        snprintf(sub_path, sizeof(sub_path), "%.*s/games", (int)(sizeof(sub_path) - 8U), root);
        scan_dir_iterative(ctx, sub_path, 3, false);

        if (!state->scan.abort_requested) {
            snprintf(sub_path, sizeof(sub_path), "%.*s/ndless", (int)(sizeof(sub_path) - 9U), root);
            scan_dir_iterative(ctx, sub_path, 3, false);
        }
        if (!state->scan.abort_requested) {
            snprintf(sub_path, sizeof(sub_path), "%.*s/roms", (int)(sizeof(sub_path) - 7U), root);
            scan_dir_iterative(ctx, sub_path, 3, false);
        }
        if (!state->scan.abort_requested) {
            scan_dir_iterative(ctx, root, 4, false);
        }
    }

    state->diag.defer_flash_log = prev_defer_flash;

    free(read_buf);
    free(dir_arena);
    free(queue);
    free(ctx);

    /* Clean early-abort path (e.g. ESC/ON pressed during startup splash): leave existing library untouched! */
    if (state->scan.abort_requested) {
        free(temp_games);
        free(temp_folders);
        state->scan.active = false;
        state->scan.finished = false;
        state->scan.cancelled_or_error = true;
        return false;
    }

    /* Sort temporary discovered games alphabetically before committing */
    if (temp_total > 1) {
        qsort(temp_games, (size_t)temp_total, sizeof(nl_game_t), compare_games_alpha);
    }

    state->scan.games_found = temp_total;

    /* Transactional Empty-Library Safety Guard:
     * If games existed previously and the new scan returned 0 games, NEVER silently erase
     * the existing library! Hold previous games intact and request explicit confirmation. */
    if (state->total_games > 0 && temp_total == 0 && !force_allow_empty) {
        free(temp_games);
        free(temp_folders);
        state->scan.active = false;
        state->scan.finished = true;
        state->scan.empty_confirm_pending = true;
        state->scan.empty_confirm_replace = false; /* Safe default: Conserver */
        state->scan.games_kept = state->total_games;
        if (progress_cb) progress_cb(&state->scan, progress_ctx);
        return true;
    }

    /* Reconcile metadata & existing icon/screenshot buffers from previous library into temp_games */
    bool *old_transferred = (bool *)calloc((size_t)(state->total_games > 0 ? state->total_games : 1), sizeof(bool));
    int kept = 0;
    int added = 0;
    for (int n = 0; n < temp_total; n++) {
        nl_game_t *ng = &temp_games[n];
        int match = -1;
        for (int o = 0; o < state->total_games; o++) {
            if (old_transferred && old_transferred[o]) continue;
            if (str_icmp(state->games[o].path, ng->path) == 0 ||
                str_icmp(state->games[o].filename, ng->filename) == 0) {
                match = o;
                break;
            }
        }
        if (match >= 0) {
            nl_game_t *og = &state->games[match];
            ng->is_favorite      = og->is_favorite;
            ng->last_played_rank = og->last_played_rank;
            ng->launch_count     = og->launch_count;
            ng->play_time_sec    = og->play_time_sec;
            ng->last_played_ts   = og->last_played_ts;
            ng->shortcut_key     = og->shortcut_key;
            /* Transfer already-allocated media pointers cleanly (zero leaks, zero double-free) */
            ng->icon_source      = og->icon_source;
            ng->icon_w           = og->icon_w;
            ng->icon_h           = og->icon_h;
            ng->icon_pixels      = og->icon_pixels;
            ng->icon_alpha       = og->icon_alpha;
            ng->has_screenshot   = og->has_screenshot;
            ng->scr_pixels       = og->scr_pixels;
            og->icon_pixels      = NULL;
            og->icon_alpha       = NULL;
            og->scr_pixels       = NULL;
            if (old_transferred) old_transferred[match] = true;
            kept++;
        } else {
            added++;
        }
    }

    /* Free media buffers ONLY for games that were removed; NEVER touch profiles! */
    int removed = 0;
    for (int o = 0; o < state->total_games; o++) {
        if (!old_transferred || !old_transferred[o]) {
            removed++;
            free(state->games[o].icon_pixels);
            free(state->games[o].icon_alpha);
            free(state->games[o].scr_pixels);
            state->games[o].icon_pixels = NULL;
            state->games[o].icon_alpha  = NULL;
            state->games[o].scr_pixels  = NULL;
        }
    }
    free(old_transferred);

    /* Commit verified temporary library atomically into state->games */
    memset(state->games, 0, sizeof(state->games));
    if (temp_total > 0) {
        memcpy(state->games, temp_games, (size_t)temp_total * sizeof(nl_game_t));
    }
    state->total_games = temp_total;

    /* Ensure default folders + all discovered folders are remembered */
    nl_folder_seed_defaults(temp_folders, root);
    state->folders = *temp_folders;

    free(temp_games);
    free(temp_folders);

    /* Re-apply active profile stats if profiles are initialized (never wipes game_data!) */
    if (state->profiles.initialized) {
        nl_profile_apply_active(state);
    }

    clamp_ui_cursors_after_library_change(state);

    state->scan.games_added   = added;
    state->scan.games_removed = removed;
    state->scan.games_kept    = kept;
    state->scan.active        = false;
    state->scan.finished      = true;

    if (progress_cb) progress_cb(&state->scan, progress_ctx);
    return true;
}

void nl_rescan_apply_empty_confirm(nl_state_t *state, bool replace_with_empty) {
    if (!state || !state->scan.empty_confirm_pending) return;
    state->scan.empty_confirm_pending = false;
    state->scan.empty_confirm_replace = false;
    state->scan.commit_empty_req = replace_with_empty;
    state->scan.finished = true;

    if (replace_with_empty) {
        int prev = state->total_games;
        nl_free_media_buffers(state);
        memset(state->games, 0, sizeof(state->games));
        state->total_games = 0;
        state->scan.games_found   = 0;
        state->scan.games_added   = 0;
        state->scan.games_kept    = 0;
        state->scan.games_removed = prev;
        clamp_ui_cursors_after_library_change(state);
    }
}

/* ============================================================================
 * Non-Blocking Incremental Transactional Scan Job (Build #R14)
 * Executes in small per-frame batches inside the 60 FPS main loop so keyboard,
 * mouse cursor, CTRL nav toggle, ESC cancel, and the visible "Annuler" button
 * remain 100% responsive at every instant.
 * ============================================================================ */

typedef enum {
    NL_SJOB_SCAN_DIRS = 0,
    NL_SJOB_PREP_ICONS = 1,
    NL_SJOB_LOAD_ICONS = 2,
    NL_SJOB_DONE = 3
} nl_sjob_phase_t;

typedef struct {
    char path[NL_MAX_PATH_LEN];
    int  max_depth;
    bool report_missing;
} nl_sjob_root_t;

struct nl_scan_job {
    nl_sjob_phase_t      phase;
    bool                 done;
    bool                 cancelled;
    bool                 prev_defer_flash;
    char                 docs_root[NL_MAX_PATH_LEN];
    char                 self_path[NL_MAX_PATH_LEN];

    nl_game_t           *temp_games;
    int                  temp_total;
    nl_folder_list_t    *temp_folders;
    nl_iter_scan_ctx_t  *ctx;
    nl_dir_queue_item_t *queue;
    char                *dir_arena;
    uint8_t             *read_buf;

    nl_sjob_root_t       roots[NL_MAX_GAME_FOLDERS + 4];
    int                  root_count;
    int                  root_idx;
    int                  cur_max_depth;
    bool                 cur_report_missing;
    int                  q_head;
    int                  q_tail;
    char                 cur_dir[NL_MAX_PATH_LEN];
    int                  cur_depth;
    int                  entry_count;
    int                  entry_idx;

    int                  old_match[NL_MAX_GAMES];
    bool                 reuse_old_icon[NL_MAX_GAMES];
    nl_icon_batch_t     *icon_batch;
    char                *icon_cand;
    int                  icon_idx;
    uint32_t             icon_ram_total;
    int                  icons_loaded;
    int                  icons_fallback;
    bool                 budget_warned;
};

static void sjob_free_temp_owned_icons(nl_scan_job_t *job) {
    if (!job || !job->temp_games) return;
    for (int i = 0; i < job->temp_total && i < NL_MAX_GAMES; i++) {
        if (!job->reuse_old_icon[i]) {
            free(job->temp_games[i].icon_pixels);
            free(job->temp_games[i].icon_alpha);
            job->temp_games[i].icon_pixels = NULL;
            job->temp_games[i].icon_alpha  = NULL;
        }
        /* temp_games never allocates scr_pixels before commit */
    }
}

static void sjob_release_resources(nl_scan_job_t *job) {
    if (!job) return;
    if (job->icon_batch) {
        nl_icon_batch_destroy(job->icon_batch);
        job->icon_batch = NULL;
    }
    free(job->icon_cand);    job->icon_cand = NULL;
    free(job->read_buf);     job->read_buf = NULL;
    free(job->dir_arena);    job->dir_arena = NULL;
    free(job->queue);        job->queue = NULL;
    free(job->ctx);          job->ctx = NULL;
    free(job->temp_folders); job->temp_folders = NULL;
    free(job->temp_games);   job->temp_games = NULL;
}

static void sjob_add_root(nl_scan_job_t *job, const char *dir_path, int max_depth, bool report_missing) {
    if (!job || !dir_path || dir_path[0] == '\0') return;
    if (job->root_count >= (int)(sizeof(job->roots) / sizeof(job->roots[0]))) return;
    char norm[NL_MAX_PATH_LEN];
    normalize_dir_path(dir_path, norm, sizeof(norm));
    if (norm[0] == '\0') return;
    for (int i = 0; i < job->root_count; i++) {
        if (str_icmp(job->roots[i].path, norm) == 0) return;
    }
    safe_strncpy(job->roots[job->root_count].path, norm, sizeof(job->roots[0].path));
    job->roots[job->root_count].max_depth = max_depth;
    job->roots[job->root_count].report_missing = report_missing;
    job->root_count++;
}

nl_scan_job_t *nl_scan_job_start(nl_state_t *state, const char *docs_root, const char *self_path) {
    if (!state || state->scan.active) return NULL;
    const char *root = (docs_root && docs_root[0] != '\0')
        ? docs_root
        : (state->sys.docs_root[0] != '\0' ? state->sys.docs_root : "/documents");

    nl_scan_job_t *job = (nl_scan_job_t *)calloc(1U, sizeof(nl_scan_job_t));
    if (!job) return NULL;

    job->temp_games   = (nl_game_t *)calloc(NL_MAX_GAMES, sizeof(nl_game_t));
    job->temp_folders = (nl_folder_list_t *)calloc(1U, sizeof(nl_folder_list_t));
    job->ctx          = (nl_iter_scan_ctx_t *)calloc(1U, sizeof(nl_iter_scan_ctx_t));
    job->queue        = (nl_dir_queue_item_t *)calloc(NL_SCAN_QUEUE_CAP, sizeof(nl_dir_queue_item_t));
    job->dir_arena    = (char *)malloc(NL_SCAN_DIR_ARENA_CAP);
    job->read_buf     = (uint8_t *)malloc(4096U);

    if (!job->temp_games || !job->temp_folders || !job->ctx || !job->queue || !job->dir_arena) {
        sjob_release_resources(job);
        free(job);
        state->scan.active = false;
        state->scan.finished = true;
        state->scan.cancelled_or_error = true;
        state->scan.errors_count = 1;
        snprintf(state->scan.last_error, sizeof(state->scan.last_error), "Memoire insuffisante");
        nl_recover_memory_on_oom(state, "Scan bibliotheque", root);
        return NULL;
    }

    safe_strncpy(job->docs_root, root, sizeof(job->docs_root));
    safe_strncpy(job->self_path, self_path ? self_path : "", sizeof(job->self_path));

    memset(&state->scan, 0, sizeof(state->scan));
    state->scan.active = true;
    state->scan.is_startup = false;
    state->scan.prev_games_count = state->total_games;
    state->scan.progress_pct = 2;
    snprintf(state->scan.phase_label, sizeof(state->scan.phase_label), "Analyse des dossiers...");
    safe_strncpy(state->scan.current_dir, root, sizeof(state->scan.current_dir));

    /* Preserve previously memorized game folders */
    for (int f = 0; f < state->folders.count && f < NL_MAX_GAME_FOLDERS; f++) {
        (void)nl_folder_add(job->temp_folders, state->folders.paths[f]);
        if (state->folders.paths[f][0] != '\0') {
            bool rep_miss = !is_optional_default_folder(state->folders.paths[f], root);
            sjob_add_root(job, state->folders.paths[f], 3, rep_miss);
        }
    }

    char sub_path[NL_MAX_PATH_LEN];
    snprintf(sub_path, sizeof(sub_path), "%.*s/games", (int)(sizeof(sub_path) - 8U), root);
    sjob_add_root(job, sub_path, 3, false);
    snprintf(sub_path, sizeof(sub_path), "%.*s/ndless", (int)(sizeof(sub_path) - 9U), root);
    sjob_add_root(job, sub_path, 3, false);
    snprintf(sub_path, sizeof(sub_path), "%.*s/roms", (int)(sizeof(sub_path) - 7U), root);
    sjob_add_root(job, sub_path, 3, false);
    sjob_add_root(job, root, 4, false);

    job->ctx->games              = job->temp_games;
    job->ctx->total_games        = &job->temp_total;
    job->ctx->discovered_folders = job->temp_folders;
    job->ctx->diag_state         = state;
    job->ctx->stats              = &state->scan;
    job->ctx->self_path          = job->self_path;
    job->ctx->read_buf           = job->read_buf;
    job->ctx->read_buf_cap       = job->read_buf ? 4096U : 0U;
    job->ctx->queue              = job->queue;
    job->ctx->dir_arena          = job->dir_arena;

    job->prev_defer_flash        = state->diag.defer_flash_log;
    state->diag.defer_flash_log  = true;
    job->phase                   = NL_SJOB_SCAN_DIRS;

    for (int i = 0; i < NL_MAX_GAMES; i++) {
        job->old_match[i] = -1;
        job->reuse_old_icon[i] = false;
    }

    return job;
}

static bool sjob_pop_next_dir(nl_scan_job_t *job, nl_state_t *state) {
    nl_iter_scan_ctx_t *ctx = job->ctx;
    while (job->temp_total < NL_MAX_GAMES) {
        if (job->q_head >= job->q_tail) {
            if (job->root_idx >= job->root_count) {
                return false;
            }
            const nl_sjob_root_t *rt = &job->roots[job->root_idx++];
            job->cur_max_depth = rt->max_depth;
            job->cur_report_missing = rt->report_missing;
            job->q_head = 0;
            job->q_tail = 0;
            if (iter_already_visited(ctx, rt->path)) {
                continue;
            }
            safe_strncpy(job->queue[job->q_tail].path, rt->path, NL_MAX_PATH_LEN);
            job->queue[job->q_tail].depth = 0;
            job->q_tail++;
            iter_mark_visited(ctx, rt->path);
        }

        const char *dir_path = job->queue[job->q_head].path;
        int depth = job->queue[job->q_head].depth;
        job->q_head++;

        safe_strncpy(job->cur_dir, dir_path, sizeof(job->cur_dir));
        job->cur_depth = depth;
        safe_strncpy(state->scan.current_dir, dir_path, sizeof(state->scan.current_dir));

        /* Two-phase Nucleus-safe snapshot: opendir -> snapshot names -> IMMEDIATELY closedir */
        DIR *dp = opendir(dir_path);
        if (!dp) {
            if (job->cur_report_missing && depth == 0) {
                state->scan.errors_count++;
                snprintf(state->scan.last_error, sizeof(state->scan.last_error),
                         "Dossier absent: %.44s", path_basename(dir_path));
                nl_diag_report(
                    state,
                    NL_DIAG_E401_DIR_MISSING,
                    path_basename(dir_path),
                    dir_path,
                    "Verifier dossier de jeux",
                    "Dossier de jeux introuvable",
                    "Autres dossiers toujours scannes",
                    "Dossier ignore sans erreur fatale"
                );
            }
            continue;
        }

        int entry_count = 0;
        size_t arena_used = 0U;
        struct dirent *ep;
        while ((ep = readdir(dp)) != NULL) {
            const char *dname = ep->d_name;
            if (!dname || dname[0] == '\0' || dname[0] == '.') continue;
            if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0) continue;
            if (entry_count >= NL_SCAN_DIR_SNAP_MAX) break;

            size_t nlen = strlen(dname);
            if (nlen == 0U || nlen >= 200U) continue;
            if (arena_used + nlen + 1U > NL_SCAN_DIR_ARENA_CAP) break;

            ctx->dir_offs[entry_count++] = (uint16_t)arena_used;
            memcpy(ctx->dir_arena + arena_used, dname, nlen + 1U);
            arena_used += nlen + 1U;
        }
        closedir(dp);
        dp = NULL;

        state->scan.dirs_scanned++;
        job->entry_count = entry_count;
        job->entry_idx = 0;
        return true;
    }
    return false;
}

static void sjob_update_dir_progress(nl_scan_job_t *job, nl_state_t *state) {
    int total_roots = (job->root_count > 0) ? job->root_count : 1;
    int done_roots = (job->root_idx > 0) ? (job->root_idx - 1) : 0;
    int root_base = (done_roots * 76) / total_roots;
    int root_span = 76 / total_roots;
    if (root_span < 4) root_span = 4;
    int sub_pct = 0;
    if (job->q_tail > 0) {
        int q_done = (job->q_head > 0) ? (job->q_head - 1) : 0;
        sub_pct = (q_done * root_span) / job->q_tail;
        if (job->entry_count > 0) {
            sub_pct += (job->entry_idx * root_span) / (job->q_tail * job->entry_count);
        }
    }
    int pct = 2 + root_base + sub_pct;
    if (pct < state->scan.progress_pct) pct = state->scan.progress_pct;
    if (pct > 78) pct = 78;
    state->scan.progress_pct = pct;
}

static void sjob_commit_finished(nl_scan_job_t *job, nl_state_t *state) {
    /* Transfer matched metadata + reused icons & screenshots atomically from old library */
    bool *old_transferred = (bool *)calloc((size_t)(state->total_games > 0 ? state->total_games : 1), sizeof(bool));
    int kept = 0;
    int added = 0;

    for (int n = 0; n < job->temp_total; n++) {
        nl_game_t *ng = &job->temp_games[n];
        int match = job->old_match[n];
        if (match >= 0 && match < state->total_games) {
            nl_game_t *og = &state->games[match];
            ng->is_favorite      = og->is_favorite;
            ng->last_played_rank = og->last_played_rank;
            ng->launch_count     = og->launch_count;
            ng->play_time_sec    = og->play_time_sec;
            ng->last_played_ts   = og->last_played_ts;
            ng->shortcut_key     = og->shortcut_key;
            if (job->reuse_old_icon[n]) {
                ng->icon_source  = og->icon_source;
                ng->icon_w       = og->icon_w;
                ng->icon_h       = og->icon_h;
                ng->icon_low_res = og->icon_low_res;
                ng->icon_pixels  = og->icon_pixels;
                ng->icon_alpha   = og->icon_alpha;
                og->icon_pixels  = NULL;
                og->icon_alpha   = NULL;
            }
            /* Always preserve loaded screenshot buffer if present */
            ng->has_screenshot = og->has_screenshot;
            ng->scr_pixels     = og->scr_pixels;
            og->scr_pixels     = NULL;
            if (old_transferred) old_transferred[match] = true;
            kept++;
        } else {
            added++;
        }
    }

    /* Free buffers belonging to removed games or replaced/deleted icons of kept games */
    int removed = 0;
    for (int o = 0; o < state->total_games; o++) {
        if (!old_transferred || !old_transferred[o]) {
            removed++;
        }
        free(state->games[o].icon_pixels);
        free(state->games[o].icon_alpha);
        free(state->games[o].scr_pixels);
        state->games[o].icon_pixels = NULL;
        state->games[o].icon_alpha  = NULL;
        state->games[o].scr_pixels  = NULL;
    }
    free(old_transferred);

    memset(state->games, 0, sizeof(state->games));
    if (job->temp_total > 0) {
        memcpy(state->games, job->temp_games, (size_t)job->temp_total * sizeof(nl_game_t));
    }
    state->total_games = job->temp_total;
    /* Prevent sjob_release_resources from double-freeing transferred buffers */
    memset(job->temp_games, 0, sizeof(nl_game_t) * (size_t)NL_MAX_GAMES);

    nl_folder_seed_defaults(job->temp_folders, job->docs_root);
    state->folders = *job->temp_folders;

    state->icon_ram_bytes      = job->icon_ram_total;
    state->scan.icons_loaded   = job->icons_loaded;
    state->scan.icons_fallback = job->icons_fallback;
    state->scan.games_found    = job->temp_total;
    state->scan.games_added    = added;
    state->scan.games_removed  = removed;
    state->scan.games_kept     = kept;
    state->scan.progress_pct   = 100;
    snprintf(state->scan.phase_label, sizeof(state->scan.phase_label), "Termine");

    if (state->profiles.initialized) {
        nl_profile_apply_active(state);
    }
    clamp_ui_cursors_after_library_change(state);

    state->diag.defer_flash_log = job->prev_defer_flash;
    state->scan.active   = false;
    state->scan.finished = true;
    job->phase = NL_SJOB_DONE;
    job->done  = true;
    sjob_release_resources(job);
}

void nl_scan_job_cancel(nl_scan_job_t *job, nl_state_t *state) {
    if (!job || job->done) return;
    sjob_free_temp_owned_icons(job);
    sjob_release_resources(job);
    job->phase     = NL_SJOB_DONE;
    job->done      = true;
    job->cancelled = true;

    if (state) {
        state->diag.defer_flash_log   = job->prev_defer_flash;
        state->scan.active            = false;
        state->scan.finished          = false;
        state->scan.cancelled_or_error = true;
        state->scan.cancel_requested  = false;
        state->scan.abort_requested   = false;
        snprintf(state->scan.phase_label, sizeof(state->scan.phase_label), "Annule");
        snprintf(state->scan.last_error, sizeof(state->scan.last_error),
                 "Scan annule - bibliotheque conservee");
    }
}

bool nl_scan_job_step(nl_scan_job_t *job, nl_state_t *state, int max_items) {
    if (!job || !state || job->done) return false;
    if (max_items < 1) max_items = 1;
    nl_disable_watchdog_safe();

    if (state->scan.cancel_requested || state->scan.abort_requested) {
        nl_scan_job_cancel(job, state);
        return false;
    }

    if (job->phase == NL_SJOB_SCAN_DIRS) {
        if (job->entry_idx >= job->entry_count) {
            if (!sjob_pop_next_dir(job, state)) {
                job->phase = NL_SJOB_PREP_ICONS;
                state->scan.progress_pct = 78;
                snprintf(state->scan.phase_label, sizeof(state->scan.phase_label), "Verification index...");
                return true;
            }
            sjob_update_dir_progress(job, state);
            return true;
        }

        nl_iter_scan_ctx_t *ctx = job->ctx;
        size_t dlen = strlen(job->cur_dir);
        bool has_slash = (dlen > 0U && job->cur_dir[dlen - 1U] == '/');

        int processed = 0;
        while (processed < max_items && job->entry_idx < job->entry_count && job->temp_total < NL_MAX_GAMES) {
            if (state->scan.cancel_requested || state->scan.abort_requested) {
                nl_scan_job_cancel(job, state);
                return false;
            }

            const char *dname = ctx->dir_arena + ctx->dir_offs[job->entry_idx++];
            processed++;

            size_t nlen = strlen(dname);
            if (dlen + (has_slash ? 0U : 1U) + nlen + 1U >= sizeof(ctx->scratch_full_path)) {
                state->scan.files_ignored++;
                continue;
            }
            memcpy(ctx->scratch_full_path, job->cur_dir, dlen);
            size_t pos = dlen;
            if (!has_slash) ctx->scratch_full_path[pos++] = '/';
            memcpy(ctx->scratch_full_path + pos, dname, nlen + 1U);

            bool is_tns = has_tns_extension(dname);
            if (is_tns && nl_is_system_or_self(ctx->scratch_full_path, ctx->self_path)) {
                state->rejected_sys++;
                state->scan.files_examined++;
                state->scan.files_ignored++;
                continue;
            }

            struct stat st;
            if (stat(ctx->scratch_full_path, &st) != 0) {
                state->scan.files_ignored++;
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                if (str_icmp(dname, "System") == 0 ||
                    str_icmp(dname, "lost+found") == 0 ||
                    str_icmp(dname, "covers") == 0 ||
                    str_icmp(dname, "icons") == 0 ||
                    str_icmp(dname, "startup") == 0) {
                    continue;
                }
                if (job->cur_depth < job->cur_max_depth && job->q_tail < NL_SCAN_QUEUE_CAP &&
                    !iter_already_visited(ctx, ctx->scratch_full_path)) {
                    safe_strncpy(job->queue[job->q_tail].path, ctx->scratch_full_path, NL_MAX_PATH_LEN);
                    job->queue[job->q_tail].depth = job->cur_depth + 1;
                    job->q_tail++;
                    iter_mark_visited(ctx, ctx->scratch_full_path);
                }
            } else if (S_ISREG(st.st_mode)) {
                state->scan.files_examined++;
                if (!is_tns) {
                    state->scan.files_ignored++;
                    continue;
                }
                state->scanned_files++;

                uint32_t known_sz = (st.st_size > 0 && st.st_size < 0x7FFFFFFF)
                    ? (uint32_t)st.st_size : 0U;
                nl_bin_format_t fmt = inspect_tns_file_with_buf(
                    ctx->scratch_full_path,
                    ctx->self_path,
                    &ctx->scratch_info,
                    ctx->read_buf,
                    ctx->read_buf_cap,
                    known_sz
                );
                if (fmt == NL_BIN_ZEHN || fmt == NL_BIN_PRG || fmt == NL_BIN_BFLT) {
                    uint32_t mtime_order = 0U;
                    if (st.st_mtim.tv_sec >= 946684800L) {
                        mtime_order = (uint32_t)st.st_mtim.tv_sec;
                    }
                    int added_idx = register_game_into_array(
                        job->temp_games, &job->temp_total, state,
                        ctx->scratch_full_path, &ctx->scratch_info, mtime_order);
                    if (added_idx >= 0) {
                        state->scan.games_found = job->temp_total;
                        extract_parent_dir(ctx->scratch_full_path,
                                           ctx->scratch_parent_dir,
                                           sizeof(ctx->scratch_parent_dir));
                        if (ctx->scratch_parent_dir[0] != '\0') {
                            (void)nl_folder_add(job->temp_folders, ctx->scratch_parent_dir);
                        }
                    } else {
                        state->scan.files_ignored++;
                    }
                } else if (fmt == NL_BIN_TI_DOC) {
                    state->rejected_docs++;
                    state->scan.files_ignored++;
                } else if (fmt == NL_BIN_SYSTEM) {
                    state->rejected_sys++;
                    state->scan.files_ignored++;
                } else {
                    state->scan.files_ignored++;
                    state->scan.errors_count++;
                    snprintf(state->scan.last_error, sizeof(state->scan.last_error),
                             "Invalide: %.48s", dname);
                    nl_diag_report(
                        state,
                        NL_DIAG_E201_GAME_MALFORMED,
                        dname,
                        ctx->scratch_full_path,
                        "Analyser executable .tns",
                        "En-tete .tns invalide ou illisible",
                        "Jeu exclu sans bloquer les autres",
                        "Jeu isole et scan poursuivi"
                    );
                }
            }
        }

        sjob_update_dir_progress(job, state);
        return true;
    }

    if (job->phase == NL_SJOB_PREP_ICONS) {
        /* Release BFS scan memory before loading icons */
        free(job->read_buf);  job->read_buf = NULL;
        free(job->dir_arena); job->dir_arena = NULL;
        free(job->queue);     job->queue = NULL;
        free(job->ctx);       job->ctx = NULL;

        if (job->temp_total > 1) {
            qsort(job->temp_games, (size_t)job->temp_total, sizeof(nl_game_t), compare_games_alpha);
        }
        state->scan.games_found = job->temp_total;

        /* Transactional Empty-Library Safety Guard */
        if (state->total_games > 0 && job->temp_total == 0) {
            sjob_release_resources(job);
            state->diag.defer_flash_log        = job->prev_defer_flash;
            state->scan.active                 = false;
            state->scan.finished               = true;
            state->scan.empty_confirm_pending  = true;
            state->scan.empty_confirm_replace  = false;
            state->scan.games_kept             = state->total_games;
            state->scan.progress_pct           = 100;
            job->phase = NL_SJOB_DONE;
            job->done  = true;
            return false;
        }

        /* Map discovered games against existing library so existing icons can be reused safely */
        bool old_claimed[NL_MAX_GAMES];
        memset(old_claimed, 0, sizeof(old_claimed));
        for (int n = 0; n < job->temp_total; n++) {
            job->old_match[n] = -1;
            job->reuse_old_icon[n] = false;
            for (int o = 0; o < state->total_games; o++) {
                if (old_claimed[o]) continue;
                if (str_icmp(state->games[o].path, job->temp_games[n].path) == 0 ||
                    str_icmp(state->games[o].filename, job->temp_games[n].filename) == 0) {
                    job->old_match[n] = o;
                    old_claimed[o] = true;
                    break;
                }
            }
        }

        job->icon_batch = nl_icon_batch_start(state->covers_dir);
        job->icon_cand  = (char *)malloc(NL_MAX_PATH_LEN);
        job->icon_idx   = 0;
        job->phase      = NL_SJOB_LOAD_ICONS;
        snprintf(state->scan.phase_label, sizeof(state->scan.phase_label), "Chargement des icones...");
        state->scan.progress_pct = 80;
        return true;
    }

    if (job->phase == NL_SJOB_LOAD_ICONS) {
        int icon_batch_lim = (max_items > 2) ? 2 : max_items;
        int done_now = 0;
        while (done_now < icon_batch_lim && job->icon_idx < job->temp_total) {
            if (state->scan.cancel_requested || state->scan.abort_requested) {
                nl_scan_job_cancel(job, state);
                return false;
            }
            int i = job->icon_idx++;
            done_now++;

            nl_game_t *ng = &job->temp_games[i];
            bool has_file = (job->icon_batch && job->icon_cand)
                ? nl_icon_batch_find_file(job->icon_batch, ng, job->icon_cand, NL_MAX_PATH_LEN)
                : false;

            if (!has_file) {
                ng->icon_source = NL_ICON_NONE;
                job->icons_fallback++;
            } else {
                int om = job->old_match[i];
                if (om >= 0 && om < state->total_games &&
                    state->games[om].icon_source != NL_ICON_NONE &&
                    state->games[om].icon_pixels != NULL) {
                    /* Reuse already-decoded icon from existing library with zero extra RAM */
                    job->reuse_old_icon[i] = true;
                    job->icon_ram_total += nl_game_icon_ram_bytes(&state->games[om]);
                    job->icons_loaded++;
                } else if (job->icon_ram_total >= NL_ICON_RAM_BUDGET) {
                    ng->icon_source = NL_ICON_NONE;
                    job->icons_fallback++;
                    if (!job->budget_warned) {
                        job->budget_warned = true;
                        nl_diag_report(
                            state,
                            NL_DIAG_E801_LOW_MEMORY,
                            ng->title[0] ? ng->title : ng->filename,
                            job->icon_cand ? job->icon_cand : state->icons_dir,
                            "Budget RAM icones",
                            "Limite memoire icones atteinte",
                            "Jeux restants affichent l'icone generique",
                            "Tous les jeux restent disponibles et lancables"
                        );
                    }
                } else {
                    if (job->icon_cand && nl_icon_batch_decode_file(job->icon_cand, ng)) {
                        job->icon_ram_total += nl_game_icon_ram_bytes(ng);
                        job->icons_loaded++;
                    } else {
                        ng->icon_source = NL_ICON_NONE;
                        job->icons_fallback++;
                        nl_diag_report(
                            state,
                            NL_DIAG_E103_ICON_CORRUPT,
                            ng->title[0] ? ng->title : ng->filename,
                            job->icon_cand ? job->icon_cand : state->icons_dir,
                            "Decoder icone du jeu",
                            "Format .png/.ico/.bmp invalide ou corrompu",
                            "Jeu toujours disponible et lancable",
                            "Icone ignoree + icone par defaut"
                        );
                    }
                }
            }
        }

        state->scan.icons_loaded   = job->icons_loaded;
        state->scan.icons_fallback = job->icons_fallback;
        if (job->temp_total > 0) {
            state->scan.progress_pct = 80 + (job->icon_idx * 19) / job->temp_total;
        } else {
            state->scan.progress_pct = 99;
        }

        if (job->icon_idx >= job->temp_total) {
            sjob_commit_finished(job, state);
            return false;
        }
        return true;
    }

    return false;
}

bool nl_scan_job_is_done(const nl_scan_job_t *job) {
    return (!job || job->done);
}

bool nl_scan_job_was_cancelled(const nl_scan_job_t *job) {
    return (job && job->cancelled);
}

void nl_scan_job_destroy(nl_scan_job_t *job) {
    if (!job) return;
    if (!job->done) {
        sjob_free_temp_owned_icons(job);
    }
    sjob_release_resources(job);
    free(job);
}
