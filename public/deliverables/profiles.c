/****************************************************************************
 * N-Launcher — profiles.c
 * Multi-User Profile Management, 4-Category Exclusive Avatars (43 icons),
 * Per-Profile Game State Isolation, Legacy Migration & Persistent Storage
 ****************************************************************************/

#include "nlauncher.h"
#include "avatars_data.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define nl_mkdir_p(p) _mkdir(p)
#else
#define nl_mkdir_p(p) mkdir((p), 0777)
#endif

static int str_icmp_p(const char *a, const char *b) {
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

static char *trim_ws_p(char *s) {
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    size_t len = strlen(s);
    while (len > 0U && (s[len - 1U] == ' ' || s[len - 1U] == '\t' ||
                        s[len - 1U] == '\r' || s[len - 1U] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

static void ensure_parent_dirs_p(const char *filepath) {
    char tmp[NL_MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", filepath);
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            nl_mkdir_p(tmp);
            *p = saved;
        }
    }
}

static const char *layout_to_str_p(nl_layout_t l) {
    switch (l) {
        case NL_LAYOUT_1X1: return "1x1";
        case NL_LAYOUT_2X2: return "2x2";
        case NL_LAYOUT_3X3: return "3x3";
        case NL_LAYOUT_4X4: return "4x4";
        case NL_LAYOUT_3X2:
        default:            return "3x2";
    }
}

static nl_layout_t str_to_layout_p(const char *s) {
    if (str_icmp_p(s, "1x1") == 0) return NL_LAYOUT_1X1;
    if (str_icmp_p(s, "2x2") == 0) return NL_LAYOUT_2X2;
    if (str_icmp_p(s, "3x3") == 0) return NL_LAYOUT_3X3;
    if (str_icmp_p(s, "4x4") == 0) return NL_LAYOUT_4X4;
    return NL_LAYOUT_3X2;
}

static const char *sort_to_str_p(nl_sort_order_t s) {
    switch (s) {
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

static nl_sort_order_t str_to_sort_p(const char *s) {
    if (str_icmp_p(s, "alpha_desc") == 0)    return NL_SORT_ALPHA_DESC;
    if (str_icmp_p(s, "recent_add") == 0)    return NL_SORT_RECENT_ADD;
    if (str_icmp_p(s, "oldest_add") == 0)    return NL_SORT_OLDEST_ADD;
    if (str_icmp_p(s, "last_used") == 0)     return NL_SORT_LAST_USED;
    if (str_icmp_p(s, "most_launched") == 0) return NL_SORT_MOST_LAUNCHED;
    if (str_icmp_p(s, "play_time") == 0)     return NL_SORT_PLAY_TIME;
    return NL_SORT_ALPHA_ASC;
}

static const char *cat_to_str_p(nl_category_t c) {
    switch (c) {
        case NL_CAT_FAVORITES: return "FAVORITES";
        case NL_CAT_RECENT:    return "RECENT";
        case NL_CAT_ALL:
        default:               return "ALL";
    }
}

static nl_category_t str_to_cat_p(const char *s) {
    if (str_icmp_p(s, "FAVORITES") == 0) return NL_CAT_FAVORITES;
    if (str_icmp_p(s, "RECENT") == 0)    return NL_CAT_RECENT;
    return NL_CAT_ALL;
}

/* =========================================================================
 * Official 43 Circular Avatar Registry Across 4 Distinct Categories
 * ========================================================================= */

const uint16_t *nl_avatar_get_pixels(int avatar_id) {
    if (avatar_id < 0 || avatar_id >= NL_AVATAR_COUNT) return NULL;
    return G_AVATARS_RGB565[avatar_id];
}

const char *nl_avatar_category_label(int cat_idx) {
    switch (cat_idx) {
        case 0:  return "Jeux video";
        case 1:  return "Fortnite";
        case 2:  return "Heroiques";
        case 3:  return "Rabbins";
        default: return "Jeux video";
    }
}

void nl_avatar_category_range(int cat_idx, int *out_start, int *out_count) {
    static const int k_starts[NL_AVATAR_CAT_COUNT] = { 0, 16, 25, 34 };
    static const int k_counts[NL_AVATAR_CAT_COUNT] = { 16, 9, 9, 9 };
    int idx = (cat_idx >= 0 && cat_idx < NL_AVATAR_CAT_COUNT) ? cat_idx : 0;
    if (out_start) *out_start = k_starts[idx];
    if (out_count) *out_count = k_counts[idx];
}

int nl_avatar_category_of(int avatar_id) {
    if (avatar_id < 16) return 0;
    if (avatar_id < 25) return 1;
    if (avatar_id < 34) return 2;
    return 3;
}

int nl_avatar_find_owner(const nl_state_t *state, int avatar_id, int exclude_profile_idx) {
    if (!state || avatar_id < 0 || avatar_id >= NL_AVATAR_COUNT) return -1;
    for (int i = 0; i < state->profiles.count; i++) {
        if (i == exclude_profile_idx) continue;
        if (state->profiles.items[i].avatar_id == avatar_id) {
            return i;
        }
    }
    return -1;
}

int nl_avatar_first_free(const nl_state_t *state) {
    if (!state) return -1;
    for (int id = 0; id < NL_AVATAR_COUNT; id++) {
        if (nl_avatar_find_owner(state, id, -1) < 0) {
            return id;
        }
    }
    return -1;
}

/* =========================================================================
 * Profile Isolation Snapshot & Apply
 * ========================================================================= */

const nl_profile_t *nl_profile_get_active(const nl_state_t *state) {
    if (!state || state->profiles.count <= 0) return NULL;
    int idx = state->profiles.active_index;
    if (idx < 0 || idx >= state->profiles.count) idx = 0;
    return &state->profiles.items[idx];
}

void nl_profile_snapshot_active(nl_state_t *state) {
    if (!state || !state->profiles.initialized || state->profiles.count <= 0) return;
    int idx = state->profiles.active_index;
    if (idx < 0 || idx >= state->profiles.count) return;

    nl_profile_t *p = &state->profiles.items[idx];
    p->layout          = state->settings.layout;
    p->show_names      = state->settings.show_names;
    p->show_shortcuts  = state->settings.show_shortcuts;
    p->sort_order      = state->settings.sort_order;
    p->active_category = state->active_category;
    p->selected_pos    = state->selected_pos;
    p->play_counter    = state->play_counter;

    for (int g = 0; g < state->total_games; g++) {
        const nl_game_t *gm = &state->games[g];
        bool has_data = gm->is_favorite ||
                        gm->last_played_rank > 0U ||
                        gm->launch_count > 0U ||
                        gm->play_time_sec > 0U ||
                        gm->last_played_ts > 0U ||
                        gm->shortcut_key != 0;

        int existing = -1;
        for (int k = 0; k < p->game_data_count; k++) {
            if (str_icmp_p(p->game_data[k].filename, gm->filename) == 0) {
                existing = k;
                break;
            }
        }

        if (existing >= 0) {
            p->game_data[existing].is_favorite      = gm->is_favorite;
            p->game_data[existing].last_played_rank = gm->last_played_rank;
            p->game_data[existing].launch_count     = gm->launch_count;
            p->game_data[existing].play_time_sec    = gm->play_time_sec;
            p->game_data[existing].last_played_ts   = gm->last_played_ts;
            p->game_data[existing].shortcut_key     = gm->shortcut_key;
        } else if (has_data && p->game_data_count < NL_MAX_PROFILE_GAMES) {
            nl_profile_game_data_t *rec = &p->game_data[p->game_data_count++];
            memset(rec, 0, sizeof(*rec));
            snprintf(rec->filename, sizeof(rec->filename), "%s", gm->filename);
            rec->is_favorite      = gm->is_favorite;
            rec->last_played_rank = gm->last_played_rank;
            rec->launch_count     = gm->launch_count;
            rec->play_time_sec    = gm->play_time_sec;
            rec->last_played_ts   = gm->last_played_ts;
            rec->shortcut_key     = gm->shortcut_key;
        }
    }
}

void nl_profile_apply_active(nl_state_t *state) {
    if (!state || state->profiles.count <= 0) return;
    int idx = state->profiles.active_index;
    if (idx < 0 || idx >= state->profiles.count) {
        idx = 0;
        state->profiles.active_index = 0;
    }

    const nl_profile_t *p = &state->profiles.items[idx];
    state->settings.layout         = p->layout;
    state->settings.show_names     = p->show_names;
    state->settings.show_shortcuts = p->show_shortcuts;
    state->settings.sort_order     = p->sort_order;
    state->active_category         = p->active_category;
    state->play_counter            = p->play_counter;

    for (int g = 0; g < state->total_games; g++) {
        nl_game_t *gm = &state->games[g];
        gm->is_favorite      = false;
        gm->last_played_rank = 0U;
        gm->launch_count     = 0U;
        gm->play_time_sec    = 0U;
        gm->last_played_ts   = 0U;
        gm->shortcut_key     = 0;

        for (int k = 0; k < p->game_data_count; k++) {
            const nl_profile_game_data_t *rec = &p->game_data[k];
            if (str_icmp_p(gm->filename, rec->filename) == 0) {
                gm->is_favorite      = rec->is_favorite;
                gm->last_played_rank = rec->last_played_rank;
                gm->launch_count     = rec->launch_count;
                gm->play_time_sec    = rec->play_time_sec;
                gm->last_played_ts   = rec->last_played_ts;
                if (rec->shortcut_key != 0 && nl_shortcut_is_assignable(rec->shortcut_key) &&
                    nl_shortcut_find_owner(state, rec->shortcut_key, g) < 0) {
                    gm->shortcut_key = rec->shortcut_key;
                }
                break;
            }
        }
    }

    state->sui.search_active = false;
    state->sui.search_query[0] = '\0';
    state->sui.search_len = 0;

    state->selected_pos = p->selected_pos;
    nl_rebuild_filter(state);

    /* Ensure restored layout/switches/category pill match profile immediately */
    state->anim.switch_shownames_fp = state->settings.show_names ? 256 : 0;
    state->anim.switch_shortcuts_fp = state->settings.show_shortcuts ? 256 : 0;
    state->anim.layout_from = state->settings.layout;
    state->anim.layout_trans_fp = 256;
    nl_sync_active_cat_pill_targets(state, true);
}

void nl_profiles_init_default(nl_state_t *state) {
    if (!state) return;
    memset(&state->profiles, 0, sizeof(state->profiles));
    state->profiles.count = 1;
    state->profiles.active_index = 0;
    state->profiles.next_profile_id = 2U;
    state->profiles.initialized = true;

    nl_profile_t *p = &state->profiles.items[0];
    p->id = 1U;
    snprintf(p->name, sizeof(p->name), "Profil 1");
    p->avatar_id = 0;
    p->is_primary = true;
    nl_profile_snapshot_active(state);
}

/* =========================================================================
 * Profile Lifecycle Operations
 * ========================================================================= */

bool nl_profile_switch_to(nl_state_t *state, int target_idx) {
    if (!state || target_idx < 0 || target_idx >= state->profiles.count) return false;
    if (!state->profiles.initialized) nl_profiles_init_default(state);
    if (target_idx == state->profiles.active_index) return false;

    nl_profile_snapshot_active(state);
    state->profiles.active_index = target_idx;
    nl_profile_apply_active(state);

    state->sui.profiles_dirty = true;
    state->sui.config_dirty = true;

    char msg[NL_MAX_TOAST_LEN];
    snprintf(msg, sizeof(msg), "Profil : %.*s", 26, state->profiles.items[target_idx].name);
    nl_show_toast(state, msg, 55);
    return true;
}

bool nl_profile_step(nl_state_t *state, int delta) {
    if (!state) return false;
    if (!state->profiles.initialized) nl_profiles_init_default(state);
    int next = state->profiles.active_index + delta;
    /* Strict non-wrapping boundary behavior */
    if (next < 0 || next >= state->profiles.count || next == state->profiles.active_index) {
        return false;
    }
    return nl_profile_switch_to(state, next);
}

static void clean_profile_name(const char *raw, char *out, size_t max_len, int fallback_num) {
    char tmp[NL_PROFILE_NAME_LEN];
    size_t j = 0U;
    if (raw) {
        const char *p = raw;
        while (*p == ' ' || *p == '\t') p++;
        while (*p != '\0' && j + 1U < NL_PROFILE_NAME_LEN) {
            unsigned char ch = (unsigned char)*p;
            if (isalnum(ch) || ch == ' ' || ch == '-' || ch == '_') {
                tmp[j++] = (char)ch;
            }
            p++;
        }
        while (j > 0U && (tmp[j - 1U] == ' ' || tmp[j - 1U] == '\t')) j--;
    }
    tmp[j] = '\0';
    if (tmp[0] == '\0') {
        snprintf(out, max_len, "Profil %d", fallback_num);
    } else {
        snprintf(out, max_len, "%s", tmp);
    }
}

int nl_profile_create(nl_state_t *state, const char *name, int avatar_id) {
    if (!state) return -1;
    if (!state->profiles.initialized) nl_profiles_init_default(state);
    if (state->profiles.count >= NL_MAX_PROFILES) return -1;
    if (avatar_id < 0 || avatar_id >= NL_AVATAR_COUNT) return -1;
    if (nl_avatar_find_owner(state, avatar_id, -1) >= 0) return -1;

    nl_profile_snapshot_active(state);

    int new_idx = state->profiles.count;
    nl_profile_t *np = &state->profiles.items[new_idx];
    memset(np, 0, sizeof(*np));
    np->id = state->profiles.next_profile_id++;
    if (np->id == 0U) np->id = state->profiles.next_profile_id++;
    clean_profile_name(name, np->name, sizeof(np->name), new_idx + 1);
    np->avatar_id = avatar_id;
    np->is_primary = false;
    np->layout = NL_LAYOUT_3X2;
    np->show_names = false;
    np->show_shortcuts = false;
    np->sort_order = NL_SORT_ALPHA_ASC;
    np->active_category = NL_CAT_ALL;
    np->selected_pos = 0;
    np->play_counter = 0U;
    np->game_data_count = 0;

    state->profiles.count++;
    state->profiles.active_index = new_idx;
    nl_profile_apply_active(state);

    state->sui.profiles_dirty = true;
    state->sui.config_dirty = true;
    return new_idx;
}

bool nl_profile_rename(nl_state_t *state, int prof_idx, const char *new_name) {
    if (!state || !new_name || prof_idx < 0 || prof_idx >= state->profiles.count) return false;

    char cleaned[NL_PROFILE_NAME_LEN];
    size_t j = 0U;
    const char *p = new_name;
    while (*p == ' ' || *p == '\t') p++;
    while (*p != '\0' && j + 1U < NL_PROFILE_NAME_LEN) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch) || ch == ' ' || ch == '-' || ch == '_') {
            cleaned[j++] = (char)ch;
        }
        p++;
    }
    while (j > 0U && (cleaned[j - 1U] == ' ' || cleaned[j - 1U] == '\t')) j--;
    cleaned[j] = '\0';
    if (cleaned[0] == '\0') return false;

    snprintf(state->profiles.items[prof_idx].name, NL_PROFILE_NAME_LEN, "%s", cleaned);
    state->sui.profiles_dirty = true;
    return true;
}

bool nl_profile_set_avatar(nl_state_t *state, int prof_idx, int avatar_id) {
    if (!state || prof_idx < 0 || prof_idx >= state->profiles.count) return false;
    if (avatar_id < 0 || avatar_id >= NL_AVATAR_COUNT) return false;
    if (nl_avatar_find_owner(state, avatar_id, prof_idx) >= 0) return false;

    state->profiles.items[prof_idx].avatar_id = avatar_id;
    state->sui.profiles_dirty = true;
    return true;
}

bool nl_profile_delete(nl_state_t *state, int prof_idx) {
    if (!state || !state->profiles.initialized) return false;
    if (state->profiles.count <= 1) return false;
    if (prof_idx < 0 || prof_idx >= state->profiles.count) return false;
    if (state->profiles.items[prof_idx].is_primary) return false;

    nl_profile_snapshot_active(state);

    uint32_t old_active_id = state->profiles.items[state->profiles.active_index].id;
    bool deleted_active = (prof_idx == state->profiles.active_index);

    if (prof_idx + 1 < state->profiles.count) {
        memmove(&state->profiles.items[prof_idx],
                &state->profiles.items[prof_idx + 1],
                (size_t)(state->profiles.count - 1 - prof_idx) * sizeof(nl_profile_t));
    }
    memset(&state->profiles.items[state->profiles.count - 1], 0, sizeof(nl_profile_t));
    state->profiles.count--;

    if (deleted_active) {
        int fallback = 0;
        for (int i = 0; i < state->profiles.count; i++) {
            if (state->profiles.items[i].is_primary) {
                fallback = i;
                break;
            }
        }
        state->profiles.active_index = fallback;
        nl_profile_apply_active(state);
    } else {
        state->profiles.active_index = 0;
        for (int i = 0; i < state->profiles.count; i++) {
            if (state->profiles.items[i].id == old_active_id) {
                state->profiles.active_index = i;
                break;
            }
        }
    }

    state->sui.profiles_dirty = true;
    state->sui.config_dirty = true;
    return true;
}

/* =========================================================================
 * Persistent Multi-Profile File Storage (/documents/ndless/nlauncher.profiles.tns)
 * ========================================================================= */

bool nl_save_profiles(nl_state_t *state, const char *profiles_path) {
    if (!state || !profiles_path || profiles_path[0] == '\0') return false;
    if (!state->profiles.initialized) {
        nl_profiles_init_default(state);
    } else {
        nl_profile_snapshot_active(state);
    }

    ensure_parent_dirs_p(profiles_path);
    FILE *fp = fopen(profiles_path, "w");
    if (!fp) return false;

    uint32_t active_id = state->profiles.items[state->profiles.active_index].id;
    fprintf(fp, "# N-Launcher Profiles v1\n");
    fprintf(fp, "active_profile_id=%u\n", (unsigned)active_id);
    fprintf(fp, "next_profile_id=%u\n", (unsigned)state->profiles.next_profile_id);

    for (int i = 0; i < state->profiles.count; i++) {
        const nl_profile_t *p = &state->profiles.items[i];
        fprintf(fp, "[profile]\n");
        fprintf(fp, "id=%u\n", (unsigned)p->id);
        fprintf(fp, "name=%s\n", p->name);
        fprintf(fp, "avatar_id=%d\n", p->avatar_id);
        fprintf(fp, "is_primary=%d\n", p->is_primary ? 1 : 0);
        fprintf(fp, "layout=%s\n", layout_to_str_p(p->layout));
        fprintf(fp, "show_names=%d\n", p->show_names ? 1 : 0);
        fprintf(fp, "show_shortcuts=%d\n", p->show_shortcuts ? 1 : 0);
        fprintf(fp, "sort_order=%s\n", sort_to_str_p(p->sort_order));
        fprintf(fp, "active_category=%s\n", cat_to_str_p(p->active_category));
        fprintf(fp, "selected_pos=%d\n", p->selected_pos);
        fprintf(fp, "play_counter=%u\n", (unsigned)p->play_counter);

        for (int g = 0; g < p->game_data_count; g++) {
            const nl_profile_game_data_t *rec = &p->game_data[g];
            if (rec->filename[0] == '\0') continue;
            if (rec->is_favorite ||
                rec->last_played_rank > 0U ||
                rec->launch_count > 0U ||
                rec->play_time_sec > 0U ||
                rec->last_played_ts > 0U ||
                rec->shortcut_key != 0) {
                char sc = nl_shortcut_is_assignable(rec->shortcut_key) ? rec->shortcut_key : '-';
                fprintf(fp, "game=%s|%d|%u|%u|%u|%u|%c\n",
                        rec->filename,
                        rec->is_favorite ? 1 : 0,
                        (unsigned)rec->last_played_rank,
                        (unsigned)rec->launch_count,
                        (unsigned)rec->play_time_sec,
                        (unsigned)rec->last_played_ts,
                        sc);
            }
        }
    }

    fflush(fp);
    fclose(fp);
    return true;
}

bool nl_load_profiles(nl_state_t *state, const char *profiles_path) {
    if (!state || !profiles_path || profiles_path[0] == '\0') return false;
    FILE *fp = fopen(profiles_path, "r");
    if (!fp) return false;

    char line[384];
    bool has_profile_sec = false;
    while (fgets(line, sizeof(line), fp)) {
        char *s = trim_ws_p(line);
        if (strcmp(s, "[profile]") == 0) {
            has_profile_sec = true;
            break;
        }
    }
    if (!has_profile_sec) {
        fclose(fp);
        return false;
    }
    rewind(fp);

    nl_profile_sys_t *loaded = &state->profiles;
    memset(loaded, 0, sizeof(*loaded));
    loaded->next_profile_id = 2U;

    uint32_t target_active_id = 0U;
    nl_profile_t *cur = NULL;

    while (fgets(line, sizeof(line), fp)) {
        char *s = trim_ws_p(line);
        if (s[0] == '\0' || s[0] == '#') continue;

        if (strcmp(s, "[profile]") == 0) {
            if (loaded->count < NL_MAX_PROFILES) {
                cur = &loaded->items[loaded->count++];
                memset(cur, 0, sizeof(*cur));
                cur->id = loaded->next_profile_id;
                cur->avatar_id = 0;
                cur->layout = NL_LAYOUT_3X2;
                cur->show_shortcuts = true;
                cur->sort_order = NL_SORT_ALPHA_ASC;
                cur->active_category = NL_CAT_ALL;
            } else {
                cur = NULL;
            }
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = trim_ws_p(s);
        char *val = trim_ws_p(eq + 1);

        if (!cur) {
            if (strcmp(key, "active_profile_id") == 0) {
                target_active_id = (uint32_t)strtoul(val, NULL, 10);
            } else if (strcmp(key, "next_profile_id") == 0) {
                uint32_t npid = (uint32_t)strtoul(val, NULL, 10);
                if (npid >= 2U) loaded->next_profile_id = npid;
            }
            continue;
        }

        if (strcmp(key, "id") == 0) {
            uint32_t id = (uint32_t)strtoul(val, NULL, 10);
            if (id > 0U) {
                cur->id = id;
                if (id >= loaded->next_profile_id) loaded->next_profile_id = id + 1U;
            }
        } else if (strcmp(key, "name") == 0) {
            clean_profile_name(val, cur->name, sizeof(cur->name), loaded->count);
        } else if (strcmp(key, "avatar_id") == 0) {
            int aid = atoi(val);
            if (aid < 0 || aid >= NL_AVATAR_COUNT) aid = 0;
            cur->avatar_id = aid;
        } else if (strcmp(key, "is_primary") == 0) {
            cur->is_primary = (atoi(val) != 0);
        } else if (strcmp(key, "layout") == 0) {
            cur->layout = str_to_layout_p(val);
        } else if (strcmp(key, "show_names") == 0) {
            cur->show_names = (atoi(val) != 0);
        } else if (strcmp(key, "show_shortcuts") == 0) {
            cur->show_shortcuts = (atoi(val) != 0);
        } else if (strcmp(key, "sort_order") == 0) {
            cur->sort_order = str_to_sort_p(val);
        } else if (strcmp(key, "active_category") == 0) {
            cur->active_category = str_to_cat_p(val);
        } else if (strcmp(key, "selected_pos") == 0) {
            int sp = atoi(val);
            cur->selected_pos = (sp >= 0) ? sp : 0;
        } else if (strcmp(key, "play_counter") == 0) {
            cur->play_counter = (uint32_t)strtoul(val, NULL, 10);
        } else if (strcmp(key, "game") == 0 && cur->game_data_count < NL_MAX_PROFILE_GAMES) {
            char *parts[7] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };
            int p_count = 0;
            char *p_cur = val;
            parts[p_count++] = p_cur;
            while (p_count < 7) {
                char *sep = strchr(p_cur, '|');
                if (!sep) break;
                *sep = '\0';
                p_cur = sep + 1;
                parts[p_count++] = p_cur;
            }
            if (p_count >= 3 && parts[0][0] != '\0') {
                nl_profile_game_data_t *rec = &cur->game_data[cur->game_data_count++];
                memset(rec, 0, sizeof(*rec));
                snprintf(rec->filename, sizeof(rec->filename), "%s", parts[0]);
                rec->is_favorite      = (atoi(parts[1]) != 0);
                rec->last_played_rank = (uint32_t)strtoul(parts[2], NULL, 10);
                rec->launch_count     = (p_count >= 4) ? (uint32_t)strtoul(parts[3], NULL, 10) : 0U;
                rec->play_time_sec    = (p_count >= 5) ? (uint32_t)strtoul(parts[4], NULL, 10) : 0U;
                rec->last_played_ts   = (p_count >= 6) ? (uint32_t)strtoul(parts[5], NULL, 10) : 0U;
                if (p_count >= 7 && parts[6][0] != '\0' && parts[6][0] != '-') {
                    char upper = (char)toupper((unsigned char)parts[6][0]);
                    if (nl_shortcut_is_assignable(upper)) {
                        rec->shortcut_key = upper;
                    }
                }
            }
        }
    }
    fclose(fp);

    /* Validate primary profile & avatar exclusivity across loaded profiles */
    bool has_primary = false;
    bool used_avatars[NL_AVATAR_COUNT];
    memset(used_avatars, 0, sizeof(used_avatars));

    for (int i = 0; i < loaded->count; i++) {
        if (loaded->items[i].name[0] == '\0') {
            snprintf(loaded->items[i].name, NL_PROFILE_NAME_LEN, "Profil %d", i + 1);
        }
        if (loaded->items[i].is_primary) {
            if (!has_primary) has_primary = true;
            else loaded->items[i].is_primary = false;
        }
        int aid = loaded->items[i].avatar_id;
        if (aid < 0 || aid >= NL_AVATAR_COUNT || used_avatars[aid]) {
            int alt = 0;
            while (alt < NL_AVATAR_COUNT && used_avatars[alt]) alt++;
            aid = (alt < NL_AVATAR_COUNT) ? alt : 0;
            loaded->items[i].avatar_id = aid;
        }
        used_avatars[aid] = true;
    }
    if (!has_primary) {
        loaded->items[0].is_primary = true;
    }

    loaded->active_index = 0;
    for (int i = 0; i < loaded->count; i++) {
        if (loaded->items[i].id == target_active_id) {
            loaded->active_index = i;
            break;
        }
    }
    loaded->initialized = true;

    nl_profile_apply_active(state);
    return true;
}
