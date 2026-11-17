/****************************************************************************
 * N-Launcher — storage.c
 * Persistent Configuration, Stats, Shortcuts & Ndless Autostart Management
 * Fault-tolerant against missing, truncated, or corrupted config files
 ****************************************************************************/

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

bool nl_save_library_cache(const nl_state_t *state, const char *cache_path) {
    if (!state || !cache_path || cache_path[0] == '\0') return false;
    if (state->total_games < 0 || state->total_games > NL_MAX_GAMES) return false;

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

    FILE *fp = fopen(cache_path, "wb");
    if (!fp) return false;

    if (fwrite(&hdr, 1U, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        (void)unlink(cache_path);
        return false;
    }

    for (int i = 0; i < state->total_games; i++) {
        const nl_game_t *g = &state->games[i];
        nl_cache_game_rec_t rec;
        build_cache_game_rec(g, &rec);

        if (fwrite(&rec, 1U, sizeof(rec), fp) != sizeof(rec)) {
            fclose(fp);
            (void)unlink(cache_path);
            return false;
        }

        if (rec.icon_source != (uint32_t)NL_ICON_NONE && rec.icon_w > 0U && rec.icon_h > 0U) {
            size_t px_count = (size_t)rec.icon_w * (size_t)rec.icon_h;
            size_t px_bytes = px_count * sizeof(uint16_t);
            if (fwrite(g->icon_pixels, 1U, px_bytes, fp) != px_bytes) {
                fclose(fp);
                (void)unlink(cache_path);
                return false;
            }
            if (rec.has_icon_alpha && g->icon_alpha) {
                if (fwrite(g->icon_alpha, 1U, px_count, fp) != px_count) {
                    fclose(fp);
                    (void)unlink(cache_path);
                    return false;
                }
            }
        }
    }

    fflush(fp);
    fclose(fp);
    return true;
}

static void reset_library_entries(nl_state_t *state) {
    if (!state) return;
    nl_free_icons(state);
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
        hdr.total_games > (uint32_t)NL_MAX_GAMES ||
        (size_t)st.st_size != sizeof(nl_cache_header_t) + (size_t)hdr.payload_bytes) {
        fclose(fp);
        state->sys.cache_valid = false;
        return false;
    }

    /* Pass 1: Verify IEEE 802.3 CRC32 over the entire payload before mutating state */
    uint32_t crc_state = 0xFFFFFFFFU;
    uint32_t rem = hdr.payload_bytes;
    uint8_t chunk[1024];
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

    FILE *fp = fopen(cfg_path, "w");
    if (!fp) return false;

    fprintf(fp, "# N-Launcher Configuration v3.1\n");
    fprintf(fp, "display_mode=%s\n", display_mode_to_str(state->display_mode));
    fprintf(fp, "accent_color=%s\n", accent_color_to_str(state->accent_color));
    fprintf(fp, "global_hotkey=%d\n", state->settings.global_hotkey ? 1 : 0);
    fprintf(fp, "layout=%s\n", layout_to_str(state->settings.layout));
    fprintf(fp, "show_names=%d\n", state->settings.show_names ? 1 : 0);
    fprintf(fp, "show_shortcuts=%d\n", state->settings.show_shortcuts ? 1 : 0);
    fprintf(fp, "touchpad_nav=%d\n", state->settings.touchpad_nav ? 1 : 0);
    fprintf(fp, "touchpad_sensitivity=%d\n", state->settings.touchpad_sensitivity);
    fprintf(fp, "touchpad_cursor_show=%d\n", state->settings.touchpad_cursor_show ? 1 : 0);
    fprintf(fp, "touchpad_cursor_style=%s\n", nl_cursor_style_to_str(state->settings.touchpad_cursor_style));
    fprintf(fp, "sort_order=%s\n", sort_to_str(state->settings.sort_order));
    fprintf(fp, "play_counter=%u\n", (unsigned)state->play_counter);
    if (state->sys.self_path[0] != '\0') {
        fprintf(fp, "launcher_path=%s\n", state->sys.self_path);
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

    fflush(fp);
    fclose(fp);
    return true;
}

bool nl_load_config(nl_state_t *state, const char *cfg_path) {
    if (!state || !cfg_path || cfg_path[0] == '\0') return false;
    FILE *fp = fopen(cfg_path, "r");
    if (!fp) return false;

    char line[384];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
            line[--len] = '\0';
        }
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = trim_ws(line);
        char *val = trim_ws(eq + 1);

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
        } else if (strcmp(key, "autostart") == 0) {
            state->settings.autostart = false;
        } else if (strcmp(key, "global_hotkey") == 0) {
            state->settings.global_hotkey = (atoi(val) != 0);
        } else if (strcmp(key, "layout") == 0) {
            state->settings.layout = str_to_layout(val);
        } else if (strcmp(key, "show_names") == 0) {
            state->settings.show_names = (atoi(val) != 0);
        } else if (strcmp(key, "show_shortcuts") == 0) {
            state->settings.show_shortcuts = (atoi(val) != 0);
        } else if (strcmp(key, "show_battery") == 0) {
            state->settings.show_battery = false;
        } else if (strcmp(key, "touchpad_nav") == 0) {
            state->settings.touchpad_nav = (atoi(val) != 0);
        } else if (strcmp(key, "touchpad_sensitivity") == 0) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NL_TP_SENS_COUNT) s = NL_TP_SENS_COUNT - 1;
            state->settings.touchpad_sensitivity = s;
        } else if (strcmp(key, "touchpad_cursor_show") == 0) {
            state->settings.touchpad_cursor_show = (atoi(val) != 0);
        } else if (strcmp(key, "touchpad_cursor_style") == 0) {
            state->settings.touchpad_cursor_style = nl_str_to_cursor_style(val);
        } else if (strcmp(key, "sort_order") == 0) {
            state->settings.sort_order = str_to_sort(val);
        } else if (strcmp(key, "play_counter") == 0) {
            unsigned long pc = strtoul(val, NULL, 10);
            if (pc <= 10000000UL) {
                state->play_counter = (uint32_t)pc;
            }
        } else if (strcmp(key, "game") == 0) {
            /* Format:
             * v3:   filename|fav|rank
             * v3.1: filename|fav|rank|launches|play_sec|last_ts|shortcut */
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
            if (p_count < 3 || parts[0][0] == '\0') continue;

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
    state->settings.autostart       = false;
    state->settings.show_battery    = false;
    state->anim.switch_autostart_fp = 0;
    state->anim.switch_hotkey_fp    = state->settings.global_hotkey ? 256 : 0;
    state->anim.switch_shownames_fp = state->settings.show_names ? 256 : 0;
    state->anim.switch_shortcuts_fp = state->settings.show_shortcuts ? 256 : 0;
    state->anim.switch_battery_fp   = 0;
    state->anim.switch_touchpad_fp  = state->settings.touchpad_nav ? 256 : 0;
    state->anim.switch_tp_cursor_fp = state->settings.touchpad_cursor_show ? 256 : 0;
    state->sui.tp_style_cursor      = (int)state->settings.touchpad_cursor_style;
    state->anim.tp_slider_pos_fp    = state->settings.touchpad_sensitivity * 256;
    state->anim.tp_slider_vel_fp    = 0;
    state->anim.tp_detent_pulse_fp  = 0;
    state->anim.tp_notch_last       = state->settings.touchpad_sensitivity;
    state->anim.layout_from         = state->settings.layout;
    state->anim.layout_trans_fp     = 256;
    nl_rebuild_filter(state);
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
        state->settings.autostart = false;
        state->settings.global_hotkey = false;
    }

    /* Reset ONLY configuration preferences to factory defaults */
    state->display_mode          = NL_DISPLAY_DARK;
    state->accent_color          = NL_ACCENT_BLUE;
    state->settings.autostart    = false;
    state->settings.global_hotkey = false;
    state->settings.layout          = NL_LAYOUT_3X2;
    state->settings.show_names      = false;
    state->settings.show_shortcuts  = true;
    state->settings.show_battery    = false;
    state->settings.touchpad_nav    = false;
    state->settings.touchpad_sensitivity  = NL_TP_SENS_DEFAULT;
    state->settings.touchpad_cursor_show  = false;
    state->settings.touchpad_cursor_style = NL_CURSOR_STYLE_ARROW;
    state->settings.sort_order      = NL_SORT_ALPHA_ASC;
    state->anim.switch_autostart_fp = 0;
    state->anim.switch_hotkey_fp    = 0;
    state->anim.switch_shownames_fp = 0;
    state->anim.switch_shortcuts_fp = 256;
    state->anim.switch_battery_fp   = 0;
    state->anim.switch_touchpad_fp  = 0;
    state->anim.switch_tp_cursor_fp = 0;
    state->sui.tp_tab               = 0;
    state->sui.tp_style_cursor      = (int)NL_CURSOR_STYLE_ARROW;
    state->anim.tp_slider_pos_fp    = NL_TP_SENS_DEFAULT * 256;
    state->anim.tp_slider_vel_fp    = 0;
    state->anim.tp_detent_pulse_fp  = 0;
    state->anim.tp_notch_last       = NL_TP_SENS_DEFAULT;
    state->anim.layout_from         = NL_LAYOUT_3X2;
    state->anim.layout_trans_fp     = 256;

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

bool nl_autostart_verify_active(const char *docs_root, const nl_sys_env_t *env) {
    (void)docs_root;
    (void)env;
    return false;
}

void nl_autostart_reconcile(nl_state_t *state) {
    if (!state) return;
    if (state->sys.docs_root[0] != '\0') {
        nl_cleanup_legacy_autostart(state->sys.docs_root);
    }
    state->settings.autostart = false;
    state->sys.autostart_active = false;
    state->anim.switch_autostart_fp = 0;
}

bool nl_autostart_set(nl_state_t *state, bool enable, char *out_diag, size_t diag_max) {
    (void)enable;
    if (!state) return false;
    nl_cleanup_legacy_autostart(state->sys.docs_root);
    state->settings.autostart = false;
    state->sys.autostart_active = false;
    state->sys.boot_recovery_triggered = false;
    state->sys.boot_recovery_reason[0] = '\0';
    state->anim.switch_autostart_fp = 0;
    if (out_diag && diag_max > 0U) {
        snprintf(out_diag, diag_max, "Lancement auto supprime");
    }
    snprintf(state->sys.autostart_diag, sizeof(state->sys.autostart_diag), "Desactive");
    return false;
}

/* ============================================================================
 * Experimental Global TI-OS Hotkey Module Management (nl_hotkey.tns)
 * ============================================================================ */

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

void nl_hotkey_reconcile(nl_state_t *state) {
    if (!state) return;
    bool ok = nl_hotkey_verify_active(state->sys.docs_root, &state->sys);
    state->sys.hotkey_active = ok;
    if (state->settings.global_hotkey && !ok) {
        state->settings.global_hotkey = false;
        state->anim.switch_hotkey_fp = 0;
    }
}

bool nl_hotkey_set(nl_state_t *state, bool enable, char *out_diag, size_t diag_max) {
    if (!state) return false;
    const char *docs_root = state->sys.docs_root;

    char startup_dir[NL_MAX_PATH_LEN];
    char stub_path[NL_MAX_PATH_LEN];

    snprintf(startup_dir, sizeof(startup_dir), "%.*s/ndless/startup",
             (int)(sizeof(startup_dir) - 20U), docs_root[0] ? docs_root : "/documents");
    snprintf(stub_path, sizeof(stub_path), "%.*s/nl_hotkey.tns",
             (int)(sizeof(stub_path) - 18U), startup_dir);

    if (!enable) {
        (void)unlink(stub_path);
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        if (out_diag && diag_max > 0U) {
            snprintf(out_diag, diag_max, "Raccourci global TI-OS desactive");
        }
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Desactive");
        return true;
    }

    if (!state->sys.has_ndless) {
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
        if (out_diag && diag_max > 0U) snprintf(out_diag, diag_max, "Ndless non detecte");
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Ndless absent");
        return false;
    }

    if (state->sys.self_path[0] == '\0' || !nl_file_exists(state->sys.self_path)) {
        state->settings.global_hotkey = false;
        state->sys.hotkey_active = false;
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
        if (out_diag && diag_max > 0U) {
            snprintf(out_diag, diag_max, "Verification nl_hotkey.tns echouee");
        }
        snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
                 "%s", out_diag ? out_diag : "Echec verif");
        return false;
    }

    state->settings.global_hotkey = true;
    state->sys.hotkey_active = true;
    if (out_diag && diag_max > 0U) {
        snprintf(out_diag, diag_max, "Actif: Ctrl+Menu (ndless/startup/nl_hotkey.tns)");
    }
    snprintf(state->sys.hotkey_diag, sizeof(state->sys.hotkey_diag),
             "%s", out_diag ? out_diag : "Actif (Ctrl+Menu)");
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
