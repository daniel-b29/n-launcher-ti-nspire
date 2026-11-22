/****************************************************************************
 * N-Launcher — diag.c
 * Centralized Diagnostic Registry, 12-Stage Persistent Boot Forensics,
 * Non-Destructive Corrupt File Backups (.bak.tns) & OOM Recovery
 ****************************************************************************/

#include "nlauncher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

static void diag_safe_copy(char *dst, size_t max_len, const char *src) {
    if (!dst || max_len == 0U) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= max_len) n = max_len - 1U;
    if (n > 0U) memcpy(dst, src, n);
    dst[n] = '\0';
}

static void diag_ensure_parent(const char *filepath) {
    if (!filepath || filepath[0] == '\0') return;
    char tmp[NL_MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", filepath);
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            (void)mkdir(tmp, 0777);
            *p = saved;
        }
    }
}

const char *nl_diag_code_str(nl_diag_code_t code) {
    switch (code) {
        case NL_DIAG_E102_ICON_MISSING:       return "E102";
        case NL_DIAG_E103_ICON_CORRUPT:       return "E103";
        case NL_DIAG_E104_SCREENSHOT_MISSING: return "E104";
        case NL_DIAG_E201_GAME_MALFORMED:     return "E201";
        case NL_DIAG_E301_CONFIG_INVALID:     return "E301";
        case NL_DIAG_E401_DIR_MISSING:        return "E401";
        case NL_DIAG_E501_ASSET_INVALID:      return "E501";
        case NL_DIAG_E601_PROFILE_CORRUPT:    return "E601";
        case NL_DIAG_E701_HOTKEY_FAILED:      return "E701";
        case NL_DIAG_E801_LOW_MEMORY:         return "E801";
        case NL_DIAG_E901_PREV_CRASH:         return "E901";
        default:                              return "E000";
    }
}

const char *nl_diag_stage_label(nl_boot_stage_t stage) {
    switch (stage) {
        case NL_STAGE_01_ENTRY:    return "1. Entree N Launcher";
        case NL_STAGE_02_NDLESS:   return "2. Init Ndless";
        case NL_STAGE_03_MEMORY:   return "3. Init memoire";
        case NL_STAGE_04_DIRS:     return "4. Verif dossiers";
        case NL_STAGE_05_CONFIG:   return "5. Configs";
        case NL_STAGE_06_PROFILES: return "6. Profils";
        case NL_STAGE_07_ICONS:    return "7. Icones";
        case NL_STAGE_08_GAMES:    return "8. Jeux";
        case NL_STAGE_09_GFX:      return "9. Init graphique";
        case NL_STAGE_10_SPLASH:   return "10. Animation d'ouverture";
        case NL_STAGE_11_LIBRARY:  return "11. Chargement bibliotheque";
        case NL_STAGE_12_READY:    return "12. Fonctionnement normal";
        default:                   return "Etape inconnue";
    }
}

static void diag_defaults_for_code(
    nl_diag_code_t code,
    const char **out_cat,
    const char **out_title,
    const char **out_reason,
    const char **out_impact,
    const char **out_recovery
) {
    switch (code) {
        case NL_DIAG_E102_ICON_MISSING:
            *out_cat      = "Icones";
            *out_title    = "Icone introuvable";
            *out_reason   = "Fichier icone absent";
            *out_impact   = "Le jeu reste disponible et lancable";
            *out_recovery = "Icone par defaut utilisee";
            break;
        case NL_DIAG_E103_ICON_CORRUPT:
            *out_cat      = "Icones";
            *out_title    = "Icone invalide/corrompue";
            *out_reason   = "En-tete ou pixels invalides";
            *out_impact   = "Le jeu reste disponible et lancable";
            *out_recovery = "Icone ignoree + icone par defaut";
            break;
        case NL_DIAG_E104_SCREENSHOT_MISSING:
            *out_cat      = "Captures";
            *out_title    = "Capture absente";
            *out_reason   = "Fichier .scr.tns introuvable";
            *out_impact   = "Apercu de capture absent";
            *out_recovery = "Animation de lancement par defaut";
            break;
        case NL_DIAG_E201_GAME_MALFORMED:
            *out_cat      = "Jeux";
            *out_title    = "Jeu mal configure/illisible";
            *out_reason   = "Fichier .tns corrompu ou illisible";
            *out_impact   = "Jeu exclu sans bloquer les autres";
            *out_recovery = "Jeu isole et scan poursuivi";
            break;
        case NL_DIAG_E301_CONFIG_INVALID:
            *out_cat      = "Configuration";
            *out_title    = "Configuration illisible";
            *out_reason   = "Fichier .cfg corrompu ou incomplet";
            *out_impact   = "Reglages precedents non charges";
            *out_recovery = "Sauvegarde .bak.tns + valeurs par defaut";
            break;
        case NL_DIAG_E401_DIR_MISSING:
            *out_cat      = "Dossiers";
            *out_title    = "Dossier manquant";
            *out_reason   = "Repertoire absent sur flash";
            *out_impact   = "Risque d'echec E/S si non recree";
            *out_recovery = "Dossier recree automatiquement";
            break;
        case NL_DIAG_E501_ASSET_INVALID:
            *out_cat      = "Ressources";
            *out_title    = "Ressource graphique invalide";
            *out_reason   = "Donnees graphiques invalides";
            *out_impact   = "Ressource defectueuse ignoree";
            *out_recovery = "Interface conservee fonctionnelle";
            break;
        case NL_DIAG_E601_PROFILE_CORRUPT:
            *out_cat      = "Profils";
            *out_title    = "Profil corrompu";
            *out_reason   = "Syntaxe ou structure invalide";
            *out_impact   = "Original preserve, jamais ecrase";
            *out_recovery = "Copie .bak.tns + chargement partiel";
            break;
        case NL_DIAG_E701_HOTKEY_FAILED:
            *out_cat      = "Raccourci TI-OS";
            *out_title    = "Hook TI-OS non installe";
            *out_reason   = "Module nl_hotkey non actif en RAM";
            *out_impact   = "Raccourci global desactive sans plantage";
            *out_recovery = "Lancer manuellement ou verifier Ndless";
            break;
        case NL_DIAG_E801_LOW_MEMORY:
            *out_cat      = "Memoire";
            *out_title    = "Memoire insuffisante";
            *out_reason   = "Allocation memoire echouee";
            *out_impact   = "Mode allege sans caches visuels";
            *out_recovery = "Caches non critiques liberes";
            break;
        case NL_DIAG_E901_PREV_CRASH:
        default:
            *out_cat      = "Demarrage";
            *out_title    = "Arret anormal precedent";
            *out_reason   = "Session precedente non cloturee";
            *out_impact   = "Derniere etape enregistree avant arret";
            *out_recovery = "Journal conserve dans Diagnostics";
            break;
    }
}

void nl_diag_init(nl_state_t *state, const char *docs_root) {
    if (!state) return;
    memset(&state->diag, 0, sizeof(state->diag));
    const char *root = (docs_root && docs_root[0] != '\0') ? docs_root : "/documents";
    snprintf(state->sys.stage_path, sizeof(state->sys.stage_path),
             "%.*s/ndless/nlauncher.stage.tns", (int)(sizeof(state->sys.stage_path) - 32U), root);
    snprintf(state->sys.diag_log_path, sizeof(state->sys.diag_log_path),
             "%.*s/ndless/nlauncher.diag.log.tns", (int)(sizeof(state->sys.diag_log_path) - 34U), root);
    state->diag.current_stage = NL_STAGE_01_ENTRY;
    state->diag.stage_reached[NL_STAGE_01_ENTRY] = true;
}

void nl_diag_read_prev_stage(nl_state_t *state) {
    if (!state || state->sys.stage_path[0] == '\0') return;
    FILE *fp = fopen(state->sys.stage_path, "r");
    if (!fp) return;

    int prev_stage = 0;
    int clean_exit = 1;
    char prev_mode[24] = "";
    char prev_os[32] = "";
    char line[160];

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
            line[--len] = '\0';
        }
        if (strncmp(line, "stage=", 6) == 0) {
            prev_stage = atoi(line + 6);
        } else if (strncmp(line, "clean_exit=", 11) == 0) {
            clean_exit = atoi(line + 11);
        } else if (strncmp(line, "ndless_mode=", 12) == 0) {
            diag_safe_copy(prev_mode, sizeof(prev_mode), line + 12);
        } else if (strncmp(line, "os=", 3) == 0) {
            diag_safe_copy(prev_os, sizeof(prev_os), line + 3);
        }
    }
    fclose(fp);

    if (clean_exit == 0 && prev_stage >= 1 && prev_stage <= 12) {
        state->sys.prior_incomplete_boot = true;
        state->sys.last_crash_stage = (nl_boot_stage_t)prev_stage;
        diag_safe_copy(state->sys.last_crash_mode, sizeof(state->sys.last_crash_mode),
                       prev_mode[0] ? prev_mode : "Inconnu");

        char reason_buf[48];
        snprintf(reason_buf, sizeof(reason_buf), "Arret a: %.*s",
                 37, nl_diag_stage_label((nl_boot_stage_t)prev_stage));

        char impact_buf[56];
        snprintf(impact_buf, sizeof(impact_buf), "Mode: %.18s | OS: %.22s",
                 prev_mode[0] ? prev_mode : "Ndless",
                 prev_os[0] ? prev_os : "TI-OS");

        nl_diag_report(
            state,
            NL_DIAG_E901_PREV_CRASH,
            "nlauncher.stage.tns",
            state->sys.stage_path,
            "Reprise apres arret",
            reason_buf,
            impact_buf,
            "Trace des 12 etapes preservee"
        );
    }
}

void nl_disable_watchdog_safe(void) {
#ifdef _TINSPIRE
    /* Exact Ndless SP805 watchdog disable sequence (TI-OS may re-arm watchdog during heavy NAND I/O) */
    *(volatile uint32_t *)0x90060C00U = 0x1ACCE551U;
    *(volatile uint32_t *)0x90060008U = 0U;
    *(volatile uint32_t *)0x90060C00U = 0U;
#endif
}

void nl_diag_mark_stage_ram(nl_state_t *state, nl_boot_stage_t stage) {
    if (!state) return;
    if ((int)stage >= 1 && (int)stage <= 12) {
        state->diag.current_stage = stage;
        state->diag.stage_reached[(int)stage] = true;
    }
}

void nl_diag_record_stage(nl_state_t *state, nl_boot_stage_t stage, bool clean_exit) {
    if (!state) return;
    nl_diag_mark_stage_ram(state, stage);
    if (state->sys.stage_path[0] == '\0') return;
    diag_ensure_parent(state->sys.stage_path);

    FILE *fp = fopen(state->sys.stage_path, "w");
    if (!fp) {
        state->diag.log_write_failed = true;
        return;
    }

    const char *mode_str = state->sys.has_ndless
        ? (state->sys.ndless_persistent ? "Persistant" : "Classique")
        : "Aucun";
    const char *hw_str = state->sys.hw_is_cx2 ? "TI-Nspire CX II" : "TI-Nspire CX";

    fprintf(fp, "build=%s\n", NL_BUILD_STAMP_STR);
    fprintf(fp, "stage=%d\n", (int)stage);
    fprintf(fp, "stage_label=%s\n", nl_diag_stage_label(stage));
    fprintf(fp, "clean_exit=%d\n", clean_exit ? 1 : 0);
    fprintf(fp, "hw=%s\n", hw_str);
    fprintf(fp, "os=%s\n", state->sys.os_version[0] ? state->sys.os_version : "TI-OS");
    fprintf(fp, "ndless_rev=%lu\n", (unsigned long)state->sys.ndless_rev);
    fprintf(fp, "ndless_mode=%s\n", mode_str);
    fflush(fp);
    fclose(fp);
}

static void diag_append_log_file(const nl_state_t *state, const nl_diag_entry_t *e) {
    if (!state || !e || state->sys.diag_log_path[0] == '\0') return;
    diag_ensure_parent(state->sys.diag_log_path);

    struct stat st;
    if (stat(state->sys.diag_log_path, &st) == 0 && (size_t)st.st_size >= NL_DIAG_LOG_MAX_BYTES) {
        /* Keep bounded size without dropping latest records: rotate current log to .old */
        char old_path[NL_MAX_PATH_LEN];
        snprintf(old_path, sizeof(old_path), "%.*s.old",
                 (int)(sizeof(old_path) - 6U), state->sys.diag_log_path);
        (void)unlink(old_path);
        (void)rename(state->sys.diag_log_path, old_path);
    }

    FILE *fp = fopen(state->sys.diag_log_path, "a");
    if (!fp) return;

    const char *mode_str = state->sys.has_ndless
        ? (state->sys.ndless_persistent ? "Persistant" : "Classique")
        : "Aucun";

    fprintf(fp, "[%s] %s | %s | build=%s | hw=%s | os=%s | ndless=r%lu(%s) | stage=%s\n",
            nl_diag_code_str(e->code),
            e->category,
            e->title,
            NL_BUILD_STAMP_STR,
            state->sys.hw_is_cx2 ? "CX-II" : "CX",
            state->sys.os_version[0] ? state->sys.os_version : "TI-OS",
            (unsigned long)state->sys.ndless_rev,
            mode_str,
            nl_diag_stage_label(e->stage));
    fprintf(fp, "  cible=%s | chemin=%s | op=%s | raison=%s | impact=%s | solution=%s\n",
            e->target_name[0] ? e->target_name : "-",
            e->path[0] ? e->path : "-",
            e->operation[0] ? e->operation : "-",
            e->reason,
            e->impact,
            e->recovery);
    fflush(fp);
    fclose(fp);
}

void nl_diag_report(
    nl_state_t *state,
    nl_diag_code_t code,
    const char *target_name,
    const char *path,
    const char *operation,
    const char *reason,
    const char *impact,
    const char *recovery
) {
    if (!state || code == NL_DIAG_NONE) return;

    const char *def_cat = NULL, *def_title = NULL, *def_reason = NULL, *def_impact = NULL, *def_rec = NULL;
    diag_defaults_for_code(code, &def_cat, &def_title, &def_reason, &def_impact, &def_rec);

    state->diag.total_events++;
    const char *safe_path = path ? path : "";

    /* Deduplicate identical (code, path) errors cleanly via counter increment */
    for (int i = 0; i < state->diag.count; i++) {
        nl_diag_entry_t *ex = &state->diag.entries[i];
        if (ex->code == code && strcmp(ex->path, safe_path) == 0) {
            if (ex->count < 65000U) ex->count++;
            ex->stage = state->diag.current_stage;
            return;
        }
    }

    if (state->diag.count >= NL_DIAG_MAX_ENTRIES) {
        /* Compact oldest low-priority icon/screenshot notice if table full, never lose crash/profile/oom */
        int evict_idx = -1;
        for (int i = 0; i < state->diag.count; i++) {
            nl_diag_code_t c = state->diag.entries[i].code;
            if (c == NL_DIAG_E102_ICON_MISSING || c == NL_DIAG_E104_SCREENSHOT_MISSING) {
                evict_idx = i;
                break;
            }
        }
        if (evict_idx < 0) return;
        if (evict_idx + 1 < state->diag.count) {
            memmove(&state->diag.entries[evict_idx],
                    &state->diag.entries[evict_idx + 1],
                    (size_t)(state->diag.count - 1 - evict_idx) * sizeof(nl_diag_entry_t));
        }
        state->diag.count--;
    }

    nl_diag_entry_t *e = &state->diag.entries[state->diag.count++];
    memset(e, 0, sizeof(*e));
    e->code  = code;
    e->stage = (state->diag.current_stage >= 1 && state->diag.current_stage <= 12)
        ? state->diag.current_stage
        : NL_STAGE_01_ENTRY;
    e->count = 1U;
    e->tick  = state->diag.total_events;

    diag_safe_copy(e->category, sizeof(e->category), def_cat);
    diag_safe_copy(e->title, sizeof(e->title), def_title);
    diag_safe_copy(e->target_name, sizeof(e->target_name), target_name ? target_name : "");
    diag_safe_copy(e->path, sizeof(e->path), safe_path);
    diag_safe_copy(e->operation, sizeof(e->operation), operation ? operation : "Verification");
    diag_safe_copy(e->reason, sizeof(e->reason), (reason && reason[0]) ? reason : def_reason);
    diag_safe_copy(e->impact, sizeof(e->impact), (impact && impact[0]) ? impact : def_impact);
    diag_safe_copy(e->recovery, sizeof(e->recovery), (recovery && recovery[0]) ? recovery : def_rec);

    if (!state->diag.defer_flash_log) {
        diag_append_log_file(state, e);
    }
}

bool nl_diag_backup_corrupt_file(const char *src_path, char *out_bak_path, size_t bak_max) {
    if (!src_path || src_path[0] == '\0') return false;

    char bak_path[NL_MAX_PATH_LEN];
    size_t slen = strlen(src_path);
    if (slen > 4U && strcmp(src_path + slen - 4U, ".tns") == 0) {
        snprintf(bak_path, sizeof(bak_path), "%.*s.bak.tns", (int)(slen - 4U), src_path);
    } else {
        snprintf(bak_path, sizeof(bak_path), "%.*s.bak.tns", (int)(sizeof(bak_path) - 10U), src_path);
    }

    if (out_bak_path && bak_max > 0U) {
        snprintf(out_bak_path, bak_max, "%s", bak_path);
    }

    /* Do not overwrite an older preserved .bak.tns if one already exists */
    struct stat bst;
    if (stat(bak_path, &bst) == 0 && bst.st_size > 0) {
        return true;
    }

    FILE *src = fopen(src_path, "rb");
    if (!src) return false;
    FILE *dst = fopen(bak_path, "wb");
    if (!dst) {
        fclose(src);
        return false;
    }

    uint8_t buf[256];
    size_t nr;
    bool write_ok = true;
    while ((nr = fread(buf, 1U, sizeof(buf), src)) > 0U) {
        if (fwrite(buf, 1U, nr, dst) != nr) {
            write_ok = false;
            break;
        }
    }
    fflush(dst);
    fclose(dst);
    fclose(src);
    if (!write_ok) {
        (void)unlink(bak_path);
        return false;
    }
    return true;
}

bool nl_diag_clear_logs(nl_state_t *state) {
    if (!state) return false;

    if (state->sys.diag_log_path[0] != '\0') {
        (void)unlink(state->sys.diag_log_path);
        char old_path[NL_MAX_PATH_LEN];
        snprintf(old_path, sizeof(old_path), "%.*s.old",
                 (int)(sizeof(old_path) - 6U), state->sys.diag_log_path);
        (void)unlink(old_path);
    }

    state->diag.count = 0;
    state->diag.total_events = 0U;
    state->diag.log_write_failed = false;
    memset(state->diag.entries, 0, sizeof(state->diag.entries));

    state->sys.prior_incomplete_boot = false;
    state->sys.last_crash_stage = (nl_boot_stage_t)0;
    state->sys.last_crash_mode[0] = '\0';

    if (state->sys.stage_path[0] != '\0') {
        nl_diag_record_stage(state, state->diag.current_stage ? state->diag.current_stage : NL_STAGE_12_READY, true);
    }
    return true;
}

void nl_recover_memory_on_oom(nl_state_t *state, const char *context_op, const char *target_path) {
    if (!state) return;
    state->sys.oom_recoveries++;
    /* Free non-critical gameplay screenshot buffers & renderer caches so core launcher stays alive */
    nl_free_screenshots(state);
    nl_renderer_cleanup();
    nl_diag_report(
        state,
        NL_DIAG_E801_LOW_MEMORY,
        target_path ? target_path : "Tas memoire",
        target_path ? target_path : "RAM",
        context_op ? context_op : "Allocation memoire",
        "Memoire disponible insuffisante",
        "Captures (.scr.tns) liberees de la RAM",
        "Allocation annulee + caches liberes"
    );
}
