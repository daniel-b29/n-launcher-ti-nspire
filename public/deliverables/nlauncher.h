/****************************************************************************
 * N-Launcher — Console Game Library Launcher for TI-Nspire CX II-T (Ndless)
 * Target: ARM926EJ-S / TI-Nspire CX II-T (320x240 RGB565 via lcd_blit)
 * Architecture: Modular C11 (scanner, covers, storage, ui_state, animation,
 *               renderer, main_ndless)
 ****************************************************************************/

#ifndef NLAUNCHER_H
#define NLAUNCHER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NL_LAUNCHER_VERSION_STR "3.1"
#define NL_BUILD_STAMP_STR      "Build 2026-11-18 #R23"

/* Hardware Screen Constants (TI-Nspire CX / CX II-T) */
#define NL_SCREEN_W         320
#define NL_SCREEN_H         240
#define NL_FB_PIXELS        (NL_SCREEN_W * NL_SCREEN_H)
#define NL_FB_BYTES         (NL_FB_PIXELS * sizeof(uint16_t))

/* Gameplay Screenshot Dimensions (.scr.tns, 94 x 68 RGB565) & Custom Icon Max Dimension */
#define NL_COVER_W          94
#define NL_COVER_H          68
#define NL_COVER_PIXELS     (NL_COVER_W * NL_COVER_H)
#define NL_COVER_BYTES      (NL_COVER_PIXELS * sizeof(uint16_t))
#define NL_ICON_MAX_DIM     84          /* 84x84 native 3x2 card match (78 RGBA icons <= 1.57 MB < 2 MB budget) */
#define NL_ICON_RAM_BUDGET  (2048U * 1024U)
#define NL_COVER_RADIUS     11

/* Legacy Icon Dimensions (88 x 48 NLIC v1 backward compatibility) */
#define NL_ICON_V1_W        88
#define NL_ICON_V1_H        48
#define NL_ICON_MAGIC       0x43494C4EU /* "NLIC" little-endian */
#define NL_VRAM_SENTINEL    0x0821U     /* Sentinel RGB(8,8,8) before nl_exec */

/* Default 3x2 Icon-Centric Grid & Vertical Pagination Geometry (320 x 240) */
#define NL_TOP_BAR_H        25
#define NL_VIEW_TOP         26
#define NL_VIEW_BOTTOM      205         /* Bottom clip edge above pagination */
#define NL_GRID_COLS        3
#define NL_GRID_ROWS        2
#define NL_GAMES_PER_PAGE   (NL_GRID_COLS * NL_GRID_ROWS)
#define NL_GRID_CARD_W      84
#define NL_GRID_CARD_H      84
#define NL_GRID_MARGIN_X    18
#define NL_GRID_GAP_X       16
#define NL_GRID_TOP_Y       29          /* Row 0 Y inside un-scrolled library */
#define NL_GRID_GAP_Y       6
#define NL_ROW_STRIDE_Y     (NL_GRID_CARD_H + NL_GRID_GAP_Y) /* 90 px */
#define NL_PAGE_STRIDE_Y    (NL_ROW_STRIDE_Y * NL_GRID_ROWS)  /* 180 px */
#define NL_PAGINATION_Y     209
#define NL_PAGINATION_MAX_DOTS 19       /* Switch to compact horizontal dashes above 19 pages */
#define NL_BOTTOM_BAR_Y     221
#define NL_BOTTOM_BAR_H     19
#define NL_VIEW_BOTTOM_NO_SC 222        /* Expanded bottom clip when bottom bar hidden */
#define NL_PAGINATION_Y_NO_SC 226       /* Repositioned pagination Y when bottom bar hidden */

/* Top-bar round Settings gear button geometry */
#define NL_GEAR_BTN_X       295
#define NL_GEAR_BTN_Y       5
#define NL_GEAR_BTN_D       15

/* Storage Limits */
#define NL_MAX_GAMES        128
#define NL_MAX_NAME_LEN     48
#define NL_MAX_AUTHOR_LEN   32
#define NL_MAX_PATH_LEN     256
#define NL_MAX_TOAST_LEN    40
#define NL_MAX_SEARCH_LEN   24

/* Multi-User Profile Limits & Official Avatar Set */
#define NL_MAX_PROFILES      43
#define NL_MAX_PROFILE_GAMES 64
#define NL_PROFILE_NAME_LEN  20
#define NL_AVATAR_COUNT      43
#define NL_AVATAR_CAT_COUNT  4
#define NL_AVATAR_SRC_DIM    40

/* Fixed-point scale constants (8.8 fixed point: 256 = 1.00x) */
#define NL_SCALE_NORMAL     256
#define NL_SCALE_SELECTED   280         /* ~1.094x subtle smooth console enlargement */
#define NL_INK_TRANS_TICKS  18

/* Ndless Executable Magic Signatures (Little-Endian uint32_t) */
#define NL_MAGIC_PRG        0x00475250U /* "PRG\0" - Classic Ndless or zehn_loader wrapper */
#define NL_MAGIC_ZEHN       0x6e68655aU /* "Zehn"  - Native Zehn v1 relocatable binary */
#define NL_MAGIC_BFLT       0x544c4662U /* "bFLT"  - Binary Flat format */
#define NL_MAGIC_PKZIP      0x04034b50U /* "PK\x03\x04" - Standard TI-Nspire Document */

/* RGB565 Helper Macro */
#define RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                (((uint16_t)(g) & 0xFCU) << 3) | \
                (((uint16_t)(b) & 0xF8U) >> 3)))

/* Executable Binary Format Type */
typedef enum {
    NL_BIN_INVALID = 0,
    NL_BIN_TI_DOC  = 1,
    NL_BIN_SYSTEM  = 2,
    NL_BIN_PRG     = 3,
    NL_BIN_ZEHN    = 4,
    NL_BIN_BFLT    = 5
} nl_bin_format_t;

/* Strict 3 Categories: TOUS, FAV, REC */
typedef enum {
    NL_CAT_ALL       = 0,
    NL_CAT_FAVORITES = 1,
    NL_CAT_RECENT    = 2,
    NL_CAT_COUNT     = 3
} nl_category_t;

/* Game Cover Source */
typedef enum {
    NL_ICON_NONE     = 0, /* Clean fallback emblem (zero technical labels) */
    NL_ICON_CAPTURED = 1, /* Validated NLIC custom icon (.ico.tns) */
    NL_ICON_BMP      = 2, /* External BMP cover image (.bmp.tns) */
    NL_ICON_ICO      = 3, /* Genuine Windows .ico custom icon (.ico.tns) */
    NL_ICON_PNG      = 4  /* Direct native PNG custom icon (.png.tns / .png) */
} nl_icon_source_t;

/* Hardware / Framebuffer Pixel Format for Cover Capture */
typedef enum {
    NL_PIX_RGB565 = 0,
    NL_PIX_RGB555 = 1,
    NL_PIX_8BPP   = 2,
    NL_PIX_4BPP   = 3
} nl_pixel_format_t;

/* Internal Cover Capture Diagnostic Status */
typedef enum {
    NL_CAP_IDLE                 = 0,
    NL_CAP_SKIPPED_EXISTING     = 1,
    NL_CAP_CAPTURED_BLIT        = 2,
    NL_CAP_CAPTURED_VRAM        = 3,
    NL_CAP_CAPTURED_STAGED      = 4,
    NL_CAP_FAIL_NO_FRAMES       = 5,
    NL_CAP_FAIL_BLANK_OR_BLACK  = 6,
    NL_CAP_FAIL_ESC_HELD        = 7,
    NL_CAP_FAIL_SAVE_IO         = 8,
    NL_CAP_FAIL_EXEC_ERROR      = 9
} nl_cap_status_t;

typedef struct {
    char            game_filename[NL_MAX_NAME_LEN];
    nl_cap_status_t status;
    uint32_t        blit_frames_seen;
    uint32_t        vram_probes_seen;
    uint32_t        frames_rejected_blank;
    uint32_t        frames_rejected_esc;
    uint32_t        frames_staged;
    int             best_scene_score;
    int             last_scr_type;
    bool            hook_blit_installed;
    bool            hook_os_installed;
    bool            save_ok;
} nl_cap_diag_t;

/* Independent Display Mode & Accent Color Settings */
typedef enum {
    NL_DISPLAY_DARK  = 0,
    NL_DISPLAY_LIGHT = 1,
    NL_DISPLAY_GRAY  = 2,
    NL_DISPLAY_COUNT = 3
} nl_display_mode_t;

typedef enum {
    NL_ACCENT_BLUE   = 0,
    NL_ACCENT_RED    = 1,
    NL_ACCENT_GREEN  = 2,
    NL_ACCENT_YELLOW = 3,
    NL_ACCENT_COUNT  = 4
} nl_accent_color_t;

/* Grid Layout Options (§3) */
typedef enum {
    NL_LAYOUT_1X1   = 0,
    NL_LAYOUT_2X2   = 1,
    NL_LAYOUT_3X2   = 2, /* Default */
    NL_LAYOUT_3X3   = 3,
    NL_LAYOUT_4X4   = 4,
    NL_LAYOUT_COUNT = 5
} nl_layout_t;

/* Page Navigation Scroll Direction (Parametres -> Affichage) */
typedef enum {
    NL_SCROLL_VERTICAL   = 0, /* Vertical — Haut / Bas (default) */
    NL_SCROLL_HORIZONTAL = 1, /* Horizontal — Gauche / Droite avec apercu pages adjacentes */
    NL_SCROLL_DIR_COUNT  = 2
} nl_scroll_dir_t;

/* 4 Independent Customizable Animation Categories (Parametres -> Animations) */
typedef enum {
    NL_OPEN_ANIM_CURRENT = 0, /* Actuelle : logo N-Launcher + fondu fluide */
    NL_OPEN_ANIM_LIQUID  = 1, /* Liquide organique : 4 propagations adaptees au theme */
    NL_OPEN_ANIM_ZOOM    = 2, /* Zoom fluide : apparition rapide centree */
    NL_OPEN_ANIM_INSTANT = 3, /* Instantane : acces direct sans delai */
    NL_OPEN_ANIM_COUNT   = 4
} nl_app_open_anim_t;

typedef enum {
    NL_CLOSE_ANIM_LIQUID  = 0, /* Liquide organique : 4 propagations rapides de fermeture */
    NL_CLOSE_ANIM_FADE    = 1, /* Fondu fluide : disparition douce rapide */
    NL_CLOSE_ANIM_ZOOM    = 2, /* Zoom arriere : repli centre rapide */
    NL_CLOSE_ANIM_INSTANT = 3, /* Instantane : sortie immediate vers TI-OS */
    NL_CLOSE_ANIM_COUNT   = 4
} nl_app_close_anim_t;

typedef enum {
    NL_LAUNCH_ANIM_CURRENT     = 0, /* Animation 1: Actuelle (capture d'ecran + 3 ondes lumineuses) */
    NL_LAUNCH_ANIM_THEME_DROP  = 1, /* Animation 2: Theme Drop (4 propagations liquides organiques adaptees au theme) */
    NL_LAUNCH_ANIM_SPINNER     = 2, /* Animation 3: Spinner (spinner rotatif sur l'icone, sans gouttes/halos) */
    NL_LAUNCH_ANIM_LOADING_BAR = 3, /* Animation 4: Loading Bar (recentrage + barre de chargement reelle) */
    NL_LAUNCH_ANIM_COUNT       = 4
} nl_launch_anim_t;

typedef enum {
    NL_RETURN_ANIM_LIQUID  = 0, /* Liquide inverse : 4 propagations organiques non-bloquantes */
    NL_RETURN_ANIM_ZOOM    = 1, /* Zoom inverse : retour rapide vers la carte du jeu */
    NL_RETURN_ANIM_FADE    = 2, /* Fondu rapide : levee immediate du voile du theme */
    NL_RETURN_ANIM_INSTANT = 3, /* Instantane : affichage direct sans transition */
    NL_RETURN_ANIM_COUNT   = 4
} nl_return_anim_t;

#define NL_ANIM_CAT_COUNT 4 /* 0=Ouverture, 1=Fermeture, 2=Lancement jeux, 3=Retour jeux */

typedef struct {
    int cols;
    int rows;
    int per_page;
    int card_w;
    int card_h;
    int radius;
    int margin_x;
    int gap_x;
    int top_y;
    int gap_y;
    int label_h;
    int row_stride_y;
    int page_stride_y;
    int page_stride_x;
    int view_bottom;
    int pagination_y;
    bool is_horizontal;
} nl_layout_metrics_t;

/* Game Sort Order (§6) */
typedef enum {
    NL_SORT_ALPHA_ASC     = 0, /* A -> Z (default) */
    NL_SORT_ALPHA_DESC    = 1, /* Z -> A */
    NL_SORT_RECENT_ADD    = 2, /* Plus recents */
    NL_SORT_OLDEST_ADD    = 3, /* Plus anciens */
    NL_SORT_LAST_USED     = 4, /* Derniere utilisation */
    NL_SORT_MOST_LAUNCHED = 5, /* Plus lances */
    NL_SORT_PLAY_TIME     = 6, /* Temps de jeu */
    NL_SORT_COUNT         = 7
} nl_sort_order_t;

/* UI Screens (§1 - §16) */
typedef enum {
    NL_UI_LIBRARY                = 0,
    NL_UI_SETTINGS_MAIN          = 1,
    NL_UI_SETTINGS_DISPLAY       = 2,
    NL_UI_SETTINGS_TOUCHPAD      = 3,
    NL_UI_SETTINGS_SHORTCUTS     = 4,
    NL_UI_SETTINGS_KEY_HELP      = 5,
    NL_UI_SETTINGS_SORT          = 6,
    NL_UI_SETTINGS_STATS         = 7,
    NL_UI_SETTINGS_RESET         = 8,
    NL_UI_SETTINGS_INFO          = 9,
    NL_UI_SETTINGS_CREDITS       = 10,
    NL_UI_SETTINGS_CAPTURE_BATCH = 11,
    NL_UI_SETTINGS_ICONS         = 12,
    NL_UI_SETTINGS_HOTKEY        = 13,
    NL_UI_SETTINGS_PROFILES      = 14,
    NL_UI_SETTINGS_DIAG          = 15,
    NL_UI_SETTINGS_RESCAN        = 16,
    NL_UI_SETTINGS_ANIMATIONS    = 17
} nl_ui_screen_t;

/* Settings Main Menu Entries (16 entries; Autostart & Battery removed; Animations & Diagnostics included) */
typedef enum {
    NL_SET_MENU_DISPLAY       = 0,
    NL_SET_MENU_ANIMATIONS    = 1,
    NL_SET_MENU_TOUCHPAD      = 2,
    NL_SET_MENU_SHORTCUTS     = 3,
    NL_SET_MENU_KEY_HELP      = 4,
    NL_SET_MENU_SORT          = 5,
    NL_SET_MENU_STATS         = 6,
    NL_SET_MENU_CAPTURE_BATCH = 7,
    NL_SET_MENU_ICONS         = 8,
    NL_SET_MENU_REFRESH_LIB   = 9,
    NL_SET_MENU_HOTKEY        = 10,
    NL_SET_MENU_PROFILES      = 11,
    NL_SET_MENU_DIAG          = 12,
    NL_SET_MENU_RESET         = 13,
    NL_SET_MENU_INFO          = 14,
    NL_SET_MENU_CREDITS       = 15,
    NL_SET_MENU_COUNT         = 16
} nl_settings_menu_idx_t;

#define NL_DISPLAY_OPT_COUNT            5     /* 0=Layout, 1=ScrollDir, 2=Names, 3=Shortcuts, 4=Power Off Calculator */

/* 12-Stage Persistent Boot & Runtime Forensics */
typedef enum {
    NL_STAGE_01_ENTRY   = 1,
    NL_STAGE_02_NDLESS  = 2,
    NL_STAGE_03_MEMORY  = 3,
    NL_STAGE_04_DIRS    = 4,
    NL_STAGE_05_CONFIG  = 5,
    NL_STAGE_06_PROFILES= 6,
    NL_STAGE_07_ICONS   = 7,
    NL_STAGE_08_GAMES   = 8,
    NL_STAGE_09_GFX     = 9,
    NL_STAGE_10_SPLASH  = 10,
    NL_STAGE_11_LIBRARY = 11,
    NL_STAGE_12_READY   = 12
} nl_boot_stage_t;

/* Centralized Diagnostic Error Codes */
typedef enum {
    NL_DIAG_NONE = 0,
    NL_DIAG_E102_ICON_MISSING       = 102,
    NL_DIAG_E103_ICON_CORRUPT       = 103,
    NL_DIAG_E104_SCREENSHOT_MISSING = 104,
    NL_DIAG_E201_GAME_MALFORMED     = 201,
    NL_DIAG_E301_CONFIG_INVALID     = 301,
    NL_DIAG_E401_DIR_MISSING        = 401,
    NL_DIAG_E501_ASSET_INVALID      = 501,
    NL_DIAG_E601_PROFILE_CORRUPT    = 601,
    NL_DIAG_E701_HOTKEY_FAILED      = 701,
    NL_DIAG_E801_LOW_MEMORY         = 801,
    NL_DIAG_E901_PREV_CRASH         = 901
} nl_diag_code_t;

#define NL_DIAG_MAX_ENTRIES   24
#define NL_DIAG_LOG_MAX_BYTES 24576U

typedef struct {
    nl_diag_code_t  code;
    char            category[20];
    char            title[40];
    char            target_name[48];
    char            path[128];
    char            operation[32];
    char            reason[48];
    char            impact[56];
    char            recovery[56];
    nl_boot_stage_t stage;
    uint16_t        count;
    uint32_t        tick;
} nl_diag_entry_t;

typedef struct {
    nl_diag_entry_t entries[NL_DIAG_MAX_ENTRIES];
    int             count;
    uint32_t        total_events;
    nl_boot_stage_t current_stage;
    bool            stage_reached[13];
    bool            log_write_failed;
    bool            defer_flash_log;
} nl_diag_store_t;

/* Persistent Icon-Independent Library & Folders Index (/documents/ndless/nlauncher.lib.tns) */
#define NL_LIB_VERSION       1U
#define NL_MAX_GAME_FOLDERS  16

typedef struct {
    char paths[NL_MAX_GAME_FOLDERS][NL_MAX_PATH_LEN];
    int  count;
} nl_folder_list_t;

typedef struct {
    bool active;
    bool finished;
    bool is_startup;
    bool empty_confirm_pending;   /* Previous library had games & new scan found 0 -> ask user */
    bool empty_confirm_replace;   /* false = Conserver l'ancienne bibliotheque (safe default) */
    bool commit_empty_req;        /* Confirmed replacement with empty library */
    bool cancelled_or_error;
    bool abort_requested;         /* Early ESC/ON abort during startup or rescan */
    bool cancel_requested;        /* Interactive user cancel request (ESC or Annuler button) */
    int  progress_pct;            /* 0..100 smooth scan + icon loading progress */
    char phase_label[32];         /* Current non-blocking scan phase label */
    char current_dir[NL_MAX_PATH_LEN];
    int  dirs_scanned;
    int  files_examined;
    int  games_found;
    int  games_added;
    int  games_removed;
    int  games_kept;
    int  files_ignored;
    int  errors_count;
    int  icons_loaded;
    int  icons_fallback;
    int  prev_games_count;
    char last_error[64];
} nl_scan_stats_t;

typedef void (*nl_scan_progress_cb_t)(const nl_scan_stats_t *stats, void *ctx);

/* Persistent Binary Library & Pre-Decoded Icon Cache (/documents/ndless/nlauncher.cache.tns) */
#define NL_CACHE_MAGIC   0x48434C4EU /* "NLCH" in little-endian */
#define NL_CACHE_VERSION 5U

typedef struct {
    uint32_t magic;            /* NL_CACHE_MAGIC ("NLCH") */
    uint32_t version;          /* NL_CACHE_VERSION (5) */
    uint32_t total_games;      /* 1..NL_MAX_GAMES */
    uint32_t payload_bytes;    /* Total bytes after this header */
    uint32_t payload_crc32;    /* IEEE 802.3 CRC32 over all bytes after header */
} nl_cache_header_t;

typedef struct {
    char     title[NL_MAX_NAME_LEN];
    char     author[NL_MAX_AUTHOR_LEN];
    char     path[NL_MAX_PATH_LEN];
    char     filename[NL_MAX_NAME_LEN];
    uint32_t size_bytes;
    uint32_t bin_format;
    uint32_t exec_version;
    uint8_t  uses_lcd_blit;
    uint8_t  supports_hww;
    uint16_t accent_color;
    uint32_t added_order;
    uint32_t icon_source;      /* nl_icon_source_t */
    uint16_t icon_w;           /* 0..NL_ICON_MAX_DIM */
    uint16_t icon_h;           /* 0..NL_ICON_MAX_DIM */
    uint8_t  has_icon_alpha;   /* 1 if icon_alpha buffer follows icon_pixels */
    uint8_t  reserved[3];
} nl_cache_game_rec_t;

/* Persistent Settings Structure */
#define NL_TP_SENS_COUNT        5
#define NL_TP_SENS_DEFAULT      2
#define NL_CURSOR_SPEED_COUNT   5
#define NL_CURSOR_SPEED_DEFAULT 2

/* Navigation Modes in Parametres -> Navigation tactile */
typedef enum {
    NL_TP_MODE_TOUCHPAD = 0, /* Mode 1: Pave tactile sans curseur graphique */
    NL_TP_MODE_MOUSE    = 1  /* Mode 2: Mode souris complet avec curseur graphique 2D */
} nl_touchpad_mode_t;

/* Customizable Touchpad Graphical Cursor Styles */
typedef enum {
    NL_CURSOR_STYLE_NONE    = 0, /* Aucun curseur (compatibilite / hors mode souris) */
    NL_CURSOR_STYLE_ARROW   = 1, /* Fleche : icons8-select-cursor-24.png (pointe de la fleche) */
    NL_CURSOR_STYLE_CIRCLE  = 2, /* Cercle : icons8-select-48.png (centre du cercle) */
    NL_CURSOR_STYLE_POINTER = 3, /* Pointeur : Dark_Blue_Gradient_Animated_Cursor--pointer (index) */
    NL_CURSOR_STYLE_COUNT   = 4
} nl_cursor_style_t;

/* Independent Proportional Cursor Sizes (usable with every cursor model) */
typedef enum {
    NL_CURSOR_SIZE_SMALL  = 0, /* Petite  : discret et compact */
    NL_CURSOR_SIZE_MEDIUM = 1, /* Moyenne : taille equilibree */
    NL_CURSOR_SIZE_LARGE  = 2, /* Grande  : taille originale 28px */
    NL_CURSOR_SIZE_COUNT  = 3
} nl_cursor_size_t;

#define NL_CURSOR_SIZE_DEFAULT NL_CURSOR_SIZE_MEDIUM

/* Interactive Hover Target Kinds for Mouse Mode */
typedef enum {
    NL_HOVER_NONE = 0,
    NL_HOVER_PROFILE_BADGE,
    NL_HOVER_CATEGORY,
    NL_HOVER_FAV_BTN,
    NL_HOVER_SETTINGS_BTN,
    NL_HOVER_GAME_CARD,
    NL_HOVER_CARD_FAV,
    NL_HOVER_PAGE_PREV,
    NL_HOVER_PAGE_NEXT,
    NL_HOVER_PAGE_DOT,
    NL_HOVER_SEARCH_BAR,
    NL_HOVER_SEARCH_CLOSE,
    NL_HOVER_QP_SLOT,
    NL_HOVER_QP_PREV,
    NL_HOVER_QP_NEXT,
    NL_HOVER_QP_OUTSIDE,
    NL_HOVER_QP_PICK_TAB,
    NL_HOVER_QP_PICK_CELL,
    NL_HOVER_QP_PICK_CLOSE,
    NL_HOVER_SET_ESC,
    NL_HOVER_SET_POWER,
    NL_HOVER_SET_ROW,
    NL_HOVER_SET_TAB,
    NL_HOVER_SET_SLIDER_NOTCH,
    NL_HOVER_SET_STYLE_CARD,
    NL_HOVER_SET_SIZE_CARD,
    NL_HOVER_MODAL_BTN,
    NL_HOVER_AVATAR_CAT,
    NL_HOVER_AVATAR_CELL,
    NL_HOVER_SC_KEY_CELL,
    NL_HOVER_ICON_CARD,
    NL_HOVER_PROF_ROW,
    NL_HOVER_PROF_DETAIL_ROW,
    NL_HOVER_PROF_AV_TAB,
    NL_HOVER_PROF_AV_CELL,
    NL_HOVER_DIAG_TAB,
    NL_HOVER_DIAG_ROW,
    NL_HOVER_DIAG_CLEAR_BTN,
    NL_HOVER_DIAG_DETAIL_CLOSE
} nl_hover_kind_t;

#define NL_CURSOR_TRAIL_LEN             5
#define NL_CURSOR_IDLE_FADE_START_TICKS 75
#define NL_CURSOR_IDLE_MIN_OPACITY_FP   112

typedef struct {
    bool               global_hotkey;
    int                hotkey_combo;          /* 0=Ctrl+Menu, 1=Shift+Menu, 2=Ctrl+Shift+Menu */
    nl_layout_t        layout;
    nl_scroll_dir_t    scroll_dir;            /* NL_SCROLL_VERTICAL or NL_SCROLL_HORIZONTAL */
    bool               show_names;
    bool               show_shortcuts;
    bool               profile_lock_enabled;  /* Global toggle for padlock profile protection */
    nl_touchpad_mode_t touchpad_mode;         /* NL_TP_MODE_TOUCHPAD or NL_TP_MODE_MOUSE */
    bool               touchpad_nav;
    int                touchpad_sensitivity;  /* 0..4 (5 mechanical detents, default 2 = 3/5) */
    int                cursor_speed;          /* 0..4 (independent mouse pointer speed, default 2) */
    bool               touchpad_cursor_show;  /* Legacy / profile sync flag */
    nl_cursor_style_t  touchpad_cursor_style; /* Selected cursor style (ARROW / CIRCLE / POINTER) */
    nl_cursor_size_t   cursor_size;           /* Independent cursor size (SMALL / MEDIUM / LARGE) */
    bool               tap_to_click;          /* Independent tap-to-click toggle (physical click always works) */
    nl_sort_order_t     sort_order;
    nl_app_open_anim_t  app_open_anim;        /* 0=Actuelle, 1=Liquide, 2=Zoom, 3=Instantane */
    nl_app_close_anim_t app_close_anim;       /* 0=Liquide, 1=Fondu, 2=Zoom, 3=Instantane */
    nl_launch_anim_t    launch_anim_mode;     /* 0=Actuelle, 1=Theme Drop, 2=Spinner, 3=Loading Bar */
    nl_return_anim_t    return_anim_mode;     /* 0=Liquide Inverse, 1=Zoom Inverse, 2=Fondu, 3=Instantane */
} nl_settings_t;

/* Runtime System, Boot Forensics & Memory Verification Context */
typedef struct {
    char               docs_root[NL_MAX_PATH_LEN];
    char               self_path[NL_MAX_PATH_LEN];
    char               stage_path[NL_MAX_PATH_LEN];
    char               diag_log_path[NL_MAX_PATH_LEN];
    char               os_version[32];
    uint32_t           ndless_rev;
    bool               has_ndless;
    bool               hw_is_cx2;
    bool               ndless_persistent;
    bool               hotkey_active;
    bool               hotkey_ram_active;     /* Verified "NLHK" hook currently resident in RAM */
    bool               cache_valid;
    bool               cache_loaded_at_boot;
    uint32_t           cache_crc32;
    bool               prior_incomplete_boot;
    nl_boot_stage_t    last_crash_stage;
    char               last_crash_mode[24];
    uint32_t           oom_recoveries;
    char               hotkey_diag[64];
} nl_sys_env_t;

/* Central Dynamic Keyboard Shortcut Definition (§5) */
typedef struct {
    char key_label[12];
    char action_label[28];
} nl_shortcut_def_t;

/* Virtual Navigation Keys */
typedef enum {
    NL_KEY_NONE = 0,
    NL_KEY_LEFT,
    NL_KEY_RIGHT,
    NL_KEY_UP,
    NL_KEY_DOWN,
    NL_KEY_EXE,       /* Launch selected game / Confirm */
    NL_KEY_ESC,       /* Back / Return to TOUS or cleanly exit launcher */
    NL_KEY_TAB,       /* Cycle TOUS -> FAV -> REC */
    NL_KEY_FAV,       /* Toggle favorite (VAR / *) */
    NL_KEY_RESCAN,    /* Refresh library (DEL) */
    NL_KEY_PAGE_PREV, /* Vertical page up (TRIG / 7) */
    NL_KEY_PAGE_NEXT, /* Vertical page down (= / 9) */
    NL_KEY_DOC,       /* Direct toggle Light/Dark mode (DOCK/DOC key) */
    NL_KEY_MENU,      /* Direct cycle Blue->Red->Green->Yellow (MENU key) */
    NL_KEY_SETTINGS,      /* Open Settings Menu (Scratchpad/Save key, Ctrl+S, or Gear button) */
    NL_KEY_BACKSPACE,     /* Erase character in Search / Delete shortcut / Profile name */
    NL_KEY_SPACE,         /* Space in Search / Profile name */
    NL_KEY_PROFILE_QUICK, /* Open/close Quick Profile Selector (.) */
    NL_KEY_PROFILE_PREV,  /* Direct switch to previous profile (0, no wrap) */
    NL_KEY_PROFILE_NEXT   /* Direct switch to next profile (-, no wrap) */
} nl_key_t;

/* Centralized Combined Palette (2 modes x 4 accents = 8 combinations) */
typedef struct {
    uint16_t bg_main;
    uint16_t bg_top_bar;
    uint16_t bg_bottom_bar;
    uint16_t card_bg;
    uint16_t card_bg_sel;
    uint16_t border_subtle;
    uint16_t dot_inactive;
    uint16_t text_primary;
    uint16_t text_secondary;
    uint16_t accent_primary;
    uint16_t accent_secondary;
    uint16_t text_on_accent;
    uint16_t badge_fav;
    uint16_t pill_bg;
    bool     is_light;
} nl_theme_t;

/* Parsed Zehn / Executable Header Inspection Result */
typedef struct {
    nl_bin_format_t format;
    uint32_t        file_size;
    uint32_t        zehn_offset;
    uint32_t        zehn_version;
    uint32_t        reloc_count;
    uint32_t        alloc_size;
    uint32_t        ndless_min;
    bool            uses_lcd_blit;
    bool            supports_hww;
    char            exec_name[NL_MAX_NAME_LEN];
    char            exec_author[NL_MAX_AUTHOR_LEN];
    uint32_t        exec_version;
} nl_exec_info_t;

/* Game Library Entry */
typedef struct {
    char             title[NL_MAX_NAME_LEN];
    char             author[NL_MAX_AUTHOR_LEN];
    char             path[NL_MAX_PATH_LEN];
    char             filename[NL_MAX_NAME_LEN];
    uint32_t         size_bytes;
    nl_bin_format_t  bin_format;
    uint32_t         exec_version;
    bool             uses_lcd_blit;
    bool             supports_hww;
    bool             is_favorite;
    uint32_t         last_played_rank;
    uint32_t         launch_count;
    uint32_t         play_time_sec;
    uint32_t         last_played_ts;   /* Unix epoch seconds (CX II-T RTC) */
    uint32_t         added_order;      /* mtime or scan discovery index */
    char             shortcut_key;     /* 0 = none, 'A'..'Z' or '0','1','3','5' */
    uint16_t         accent_color;
    nl_icon_source_t icon_source;
    uint16_t         icon_w;           /* Natural/downsampled icon width (<= NL_ICON_MAX_DIM) */
    uint16_t         icon_h;           /* Natural/downsampled icon height (<= NL_ICON_MAX_DIM) */
    bool             icon_low_res;     /* True if source image was < 32x32 (preserved crisp, never upscaled) */
    uint16_t        *icon_pixels;      /* Heap-allocated icon_w * icon_h RGB565 icon */
    uint8_t         *icon_alpha;       /* Optional alpha channel (0..255) for RGBA .png/.ico */
    bool             has_screenshot;   /* Separate real gameplay capture (.scr.tns) */
    uint16_t        *scr_pixels;       /* Heap-allocated 94x68 RGB565 screenshot */
} nl_game_t;

/* Smooth Console Animation Controller */
typedef struct {
    bool     initialized;
    int      splash_tick;                   /* 0..24: brief startup splash sequence */
    int      splash_max;
    int      scroll_y_fp;                   /* Vertical scroll Y in 1/16 px fixed-point */
    int      target_scroll_y_fp;            /* Target Y = current_page * page_stride_y << 4 */
    int      scroll_start_y_fp;             /* Anchor scroll Y when a new page transition starts */
    int      scroll_last_target_fp;         /* Last seen target_scroll_y_fp for seamless retargeting */
    int      scroll_x_fp;                   /* Horizontal scroll X in 1/16 px fixed-point */
    int      target_scroll_x_fp;            /* Target X = current_page * page_stride_x << 4 */
    int      scroll_start_x_fp;             /* Anchor scroll X when a new page transition starts */
    int      scroll_last_target_x_fp;       /* Last seen target_scroll_x_fp for seamless retargeting */
    int      scroll_anim_tick;              /* 0..scroll_anim_max smooth cubic page scroll tick */
    int      scroll_anim_max;               /* Duration in 60Hz frames (~14-16 frames) */
    int      dot_pos_fp;                    /* Smooth active pagination dot X offset (1/16 px) */
    int      dot_start_fp;                  /* Anchor pagination dot offset for synchronized glide */
    int      target_dot_fp;
    int      card_scale_fp[NL_MAX_GAMES];   /* Per-game scale in 8.8 FP (256=1.00x, 280=1.09x) */
    int      card_dx_fp[NL_MAX_GAMES];      /* Post-return sort glide X offset in 1/16 px (0 = settled) */
    int      card_dy_fp[NL_MAX_GAMES];      /* Post-return sort glide Y offset in 1/16 px (0 = settled) */
    bool     launch_holding;                /* True while LANCER held down before release */
    int      launch_shrink_fp;              /* 8.8 FP scale during hold (280 -> 232) */
    bool     launch_anim_active;
    int      launch_tick;                   /* 0..max: launch cover zoom + crossfade */
    int      launch_max_ticks;
    int      launch_load_pct;               /* 0..100 real pre-launch stage progress (Loading Bar mode) */
    int      launch_load_stage;             /* 0..4 real pre-launch operation index */
    bool     ink_active;                    /* Organic ink diffusion transition for DOC/MENU */
    int      ink_tick;
    int      ink_max;
    uint32_t ink_seq;
    int      switch_hotkey_fp;              /* 0..256 smooth animated toggle knob for global hotkey */
    int      switch_shownames_fp;           /* 0..256 smooth animated toggle knob */
    int      switch_shortcuts_fp;           /* 0..256 smooth animated toggle knob & layout transition */
    int      switch_touchpad_fp;            /* 0..256 smooth animated toggle knob for touchpad nav */
    int      tp_slider_pos_fp;              /* 0..1024 continuous position across 5 detents (0,256,512,768,1024) */
    int      tp_slider_vel_fp;              /* Fixed-point velocity for subtle detent catch & settle */
    int      tp_detent_pulse_fp;            /* 0..256 subtle visual validation pulse when latching onto a notch */
    int      tp_notch_last;                 /* Last latched notch index (0..4) */
    int      set_open_fp;                   /* 0..256 menu open/close fade + vertical slide */
    nl_ui_screen_t set_last_screen;         /* Last active settings screen for close animation */
    nl_ui_screen_t set_prev_screen;         /* Outgoing settings section for section transition */
    int      set_sec_fp;                    /* 0..256 section transition progress */
    int      set_sec_dir;                   /* +1 entering submenu, -1 returning to main settings */
    int      set_sel_y_fp;                  /* Smooth selection highlight Y in 1/16 px */
    int      set_target_sel_y_fp;           /* Target selection highlight Y in 1/16 px */
    int      set_sel_glow_fp;               /* 0..256 progressive selection highlight blend */
    int      set_prev_cursor;               /* Previous cursor index for smooth deselection fade */
    int      set_list_scroll_fp;            /* Smooth settings list scroll Y in 1/16 px */
    int      set_target_scroll_fp;          /* Target settings list scroll Y in 1/16 px */
    int      modal_fp;                      /* 0..256 confirmation modal / picker fade + slide */
    int      modal_btn_fp;                  /* 0..256 confirmation button highlight interpolation */
    nl_layout_t layout_from;                /* Previous layout for smooth grid & preview transition */
    int      layout_trans_fp;               /* 0..256 layout transition progress */
    int      val_pulse_fp;                  /* 0..256 value change visual feedback pulse */
    int      val_pulse_dir;                 /* -1 left, +1 right value change nudge */
    int      search_bar_fp;                 /* 0..256 inline library search bar reveal */
    int      cat_ind_x_fp;                  /* Sliding category pill X in 1/16 px */
    int      cat_ind_w_fp;                  /* Sliding category pill width in 1/16 px */
    int      cat_trans_fp;                  /* 0..256 category content slide/fade transition */
    int      cat_trans_dir;                 /* +1 / -1 category switch direction */
    nl_category_t cat_prev;                 /* Outgoing category during TAB switch */
    int      sc_key_x_fp;                   /* Key picker interpolated cell X in 1/16 px */
    int      sc_key_y_fp;                   /* Key picker interpolated cell Y in 1/16 px */
    int      sc_key_w_fp;                   /* Key picker interpolated cell width in 1/16 px */
    int      qp_open_fp;                    /* 0..256 Quick Profile Selector overlay open/close */
    bool     qp_opening;                    /* True when opening (elastic bloom) vs closing (crisp ease-in) */
    int      qp_glow_x_fp;                  /* 1/16 px elastic spring X tracking for multicolor focus ring */
    int      qp_glow_vx_fp;                 /* 1/16 px elastic spring velocity X for focus ring */
    int      qp_glow_y_fp;                  /* 1/16 px smooth Y tracking for multicolor focus ring */
    int      qp_trail_x_fp;                 /* 1/16 px lagging luminous wake behind focus ring */
    int      qp_sel_bounce_fp;              /* 256..0 spring rebound impulse on avatar selection */
    int      qp_plus_focus_fp;              /* 0..256 '+' button focus & rotation progress */
    int      qp_slot_scale_fp[NL_MAX_PROFILES + 1]; /* Per-slot scale in 8.8 FP (256=1.00x, 302=1.18x) */
    uint16_t qp_glow_phase;                 /* 0..255 continuous luminous multicolor ring phase */
    bool     prof_switch_active;            /* True during Identity Morph & Prism Pulse profile switch */
    int      prof_switch_tick;              /* 0..prof_switch_max */
    int      prof_switch_max;               /* Total ticks (20) for depth swap + prism pulse + corner dock */
    int      prof_switch_avatar_id;         /* Incoming target profile avatar ID */
    int      prof_switch_from_avatar_id;    /* Outgoing previous profile avatar ID for depth swap */
    int      prof_switch_origin_x;          /* Dynamic origin X of selected profile icon */
    int      prof_switch_origin_y;          /* Dynamic origin Y of selected profile icon */
    int      prof_switch_origin_d;          /* Dynamic origin diameter of selected profile icon */
    bool     prof_plus_active;              /* True during strictly centered '+' Elastic Morph & Aperture Bloom */
    int      prof_plus_tick;                /* 0..prof_plus_max */
    int      prof_plus_max;                 /* Total ticks (14) for centered press-rebound + morph to picker */
    int      prof_plus_origin_x;            /* Exact center X of '+' button (strictly anchored, zero drift) */
    int      prof_plus_origin_y;            /* Exact center Y of '+' button */
    bool     prof_birth_active;             /* True during new user creation confirmation animation */
    int      prof_birth_tick;               /* 0..prof_birth_max */
    int      prof_birth_max;                /* Total ticks (20) for avatar confirmation wave + arrival */
    int      prof_birth_avatar_id;          /* Newly created profile avatar ID */
    int      prof_birth_slot;               /* Newly created profile slot index in selector */
    int      prof_cat_trans_fp;             /* 0..256 smooth 4-category avatar transition */
    int      prof_cat_dir;                  /* -1 / +1 category transition direction */
    int      qp_pick_fp;                    /* 0..256 compact inline avatar picker modal open/close */
    int      switch_prof_lock_fp;           /* 0..256 smooth animated toggle for profile lock protection */
    int      switch_tp_cursor_fp;           /* 0..256 smooth animated toggle knob for cursor visibility */
    int      switch_tp_mode_fp;             /* 0..256 smooth mode selector (0=Pave, 256=Souris) */
    int      tp_speed_slider_pos_fp;        /* 0..1024 continuous position across 5 mouse speed detents */
    int      tp_speed_slider_vel_fp;        /* Fixed-point velocity for mouse speed detent settle */
    int      tp_speed_detent_pulse_fp;      /* 0..256 pulse when latching onto speed notch */
    int      tp_speed_notch_last;           /* Last latched mouse speed notch (0..4) */
    int      tp_tab_trans_fp;               /* 0..256 smooth tab transition in SETTINGS_TOUCHPAD */
    int      tp_style_glow_fp;              /* 0..256 animated selection blend in Apparence du curseur */
    uint16_t tp_style_halo_phase;           /* 0..255 continuous luminous halo phase in cursor tab */
    uint16_t cur_style_phase;               /* 0..255 continuous gradient animation phase for Pointeur cursor */
    bool     cur_initialized;               /* True once graphical cursor position is anchored */
    int      cur_x_fp;                      /* 1/16 px damped graphical cursor X */
    int      cur_y_fp;                      /* 1/16 px damped graphical cursor Y */
    int      cur_vx_fp;                     /* 1/16 px graphical cursor velocity X */
    int      cur_vy_fp;                     /* 1/16 px graphical cursor velocity Y */
    int      cur_target_x_fp;               /* 1/16 px target graphical cursor X */
    int      cur_target_y_fp;               /* 1/16 px target graphical cursor Y */
    int      cur_nudge_x_fp;                /* 1/16 px live touchpad finger displacement nudge X */
    int      cur_nudge_y_fp;                /* 1/16 px live touchpad finger displacement nudge Y */
    int      mouse_x_fp;                    /* 1/16 px authoritative free 2D mouse pointer X [0..319] */
    int      mouse_y_fp;                    /* 1/16 px authoritative free 2D mouse pointer Y [0..239] */
    int      mouse_vx_fp;                   /* 1/16 px smooth deceleration inertia velocity X */
    int      mouse_vy_fp;                   /* 1/16 px smooth deceleration inertia velocity Y */
    bool     mouse_last_contact;            /* True while finger is actively touching pad in Mouse Mode */
    uint16_t mouse_last_raw_x;              /* Previous touchpad raw X for delta tracking */
    uint16_t mouse_last_raw_y;              /* Previous touchpad raw Y for delta tracking */
    int      cur_trail_x_fp[NL_CURSOR_TRAIL_LEN];
    int      cur_trail_y_fp[NL_CURSOR_TRAIL_LEN];
    int      cur_trail_alpha[NL_CURSOR_TRAIL_LEN]; /* 0..256 fading opacity of each trail echo */
    int      cur_idle_ticks;                /* Ticks since last movement/input for inactivity fade */
    int      cur_opacity_fp;                /* 0..256 overall cursor visibility opacity */
    int      cur_click_pulse_fp;            /* 256..0 subtle visual click/tap confirmation pulse */
    int      switch_tap_click_fp;           /* 0..256 smooth animated toggle knob for tap-to-click */
    bool     return_drop_active;            /* #R22 Non-blocking return-from-game organic liquid reveal */
    int      return_drop_tick;              /* 0..return_drop_max */
    int      return_drop_max;               /* Total ticks (22) for 4-layer organic liquid reveal */
    int      return_drop_game_idx;          /* Returned game index (-1 = screen center) */
    int      toast_ticks;
    int      toast_max_ticks;
    char     toast_text[NL_MAX_TOAST_LEN];
} nl_anim_t;

/* Multi-User Profile Management Sub-Modes (Settings only) */
typedef enum {
    NL_PROF_MODE_LIST = 0,
    NL_PROF_MODE_DETAIL,
    NL_PROF_MODE_EDIT_NAME,
    NL_PROF_MODE_EDIT_AVATAR,
    NL_PROF_MODE_CONFIRM_DELETE,
    NL_PROF_MODE_CREATE_NAME,
    NL_PROF_MODE_CREATE_AVATAR
} nl_prof_mode_t;

typedef struct {
    char     filename[NL_MAX_NAME_LEN];
    bool     is_favorite;
    uint32_t last_played_rank;
    uint32_t launch_count;
    uint32_t play_time_sec;
    uint32_t last_played_ts;
    char     shortcut_key;
} nl_profile_game_data_t;

typedef struct {
    uint32_t               id;
    char                   name[NL_PROFILE_NAME_LEN];
    int                    avatar_id;
    bool                   is_primary;
    bool                   is_locked;         /* Padlock protection against accidental edit/delete */
    nl_layout_t            layout;
    nl_scroll_dir_t        scroll_dir;
    bool                   show_names;
    bool                   show_shortcuts;
    bool                   touchpad_cursor_show;
    nl_cursor_style_t      touchpad_cursor_style;
    nl_cursor_size_t       cursor_size;
    int                    cursor_speed;
    bool                   tap_to_click;
    nl_sort_order_t        sort_order;
    nl_app_open_anim_t     app_open_anim;
    nl_app_close_anim_t    app_close_anim;
    nl_launch_anim_t       launch_anim_mode;
    nl_return_anim_t       return_anim_mode;
    nl_category_t          active_category;
    int                    selected_pos;
    uint32_t               play_counter;
    int                    game_data_count;
    int                    game_data_cap;
    nl_profile_game_data_t *game_data;
} nl_profile_t;

typedef struct {
    int          count;
    int          active_index;
    uint32_t     next_profile_id;
    bool         initialized;
    nl_profile_t items[NL_MAX_PROFILES];
} nl_profile_sys_t;

/* Settings & Inline Search Interaction State */
typedef struct {
    nl_ui_screen_t screen;
    int            menu_cursor;             /* 0..NL_SET_MENU_COUNT-1 */
    int            menu_scroll;
    int            sub_cursor;
    int            sub_scroll;

    /* Display submenu cursor (0=Layout, 1=ScrollDir, 2=Names, 3=Shortcuts, 4=Power Off Calculator) */
    int            display_cursor;
    int            display_scroll;

    /* Animations submenu cursor (0..NL_LAUNCH_ANIM_COUNT-1) */
    int            anim_cursor;

    /* Touchpad submenu cursor (Tab 0: 0=Mode, 1=SlideNav, 2=SlideSens, 3=MouseSpeed, 4=TapClick, 5=CursorSize, 6=Customize) */
    int            tp_cursor;
    int            tp_scroll;
    int            tp_tab;                  /* 0 = Navigation tactile, 1 = Personnaliser le curseur */
    int            tp_custom_row;           /* 0 = Modele du curseur, 1 = Taille du curseur (in Tab 1) */
    int            tp_style_cursor;         /* 0 = Fleche, 1 = Cercle, 2 = Pointeur */
    int            tp_size_cursor;          /* 0 = Petite, 1 = Moyenne, 2 = Grande */
    bool           ctrl_mouse_active;       /* True while CTRL held temporarily activates Mouse Mode in Mode 1 */
    bool           hover_valid;             /* True when mouse pointer hovers an interactive element */
    nl_hover_kind_t hover_kind;             /* Kind of UI element under pointer */
    int            hover_index;             /* Primary index of hovered item */
    int            hover_sub;               /* Secondary index/sub-control of hovered item */

    /* Game Launch Shortcuts submenu */
    int            sc_game_cursor;
    int            sc_game_scroll;
    bool           sc_picking_key;
    int            sc_key_cursor;           /* Cursor in assignable key palette */
    bool           sc_conflict_prompt;
    char           sc_conflict_key;
    int            sc_conflict_owner_idx;
    bool           sc_conflict_confirm_yes;

    /* Direct Inline Library Search (activated directly from Library typing) */
    bool           search_active;
    char           search_query[NL_MAX_SEARCH_LEN];
    int            search_len;

    /* Reset confirmation modal */
    bool           reset_confirm_yes;       /* false = Annuler (default safe focus) */
    bool           reset_performed;

    /* Batch automatic screenshot generation (Générer captures d'écran) */
    bool           cap_active;
    bool           cap_semi_auto;
    bool           cap_finished;
    bool           cap_cancelled;
    int            cap_queue[NL_MAX_GAMES];
    int            cap_total;
    int            cap_pos;
    int            cap_captured;
    int            cap_skipped;
    int            cap_failed;
    int            cap_already;
    int            cap_last_game_idx;
    nl_cap_status_t cap_last_status;
    bool           cap_step_req;

    /* Custom icon manager (Gestion des icônes) */
    int            icon_cursor;
    int            icon_scroll;
    bool           icon_confirm_modal;
    bool           icon_confirm_yes;        /* false = Annuler (safe default) */

    /* Quick Profile Selector overlay (Library only; minimalist avatar-only + compact '+' avatar picker) */
    bool           qp_open;
    int            qp_cursor;               /* 0..profiles.count (count = '+' creation slot) */
    bool           qp_pick_open;            /* Compact floating avatar picker modal launched by '+' */
    bool           qp_pick_pending;         /* Waiting for controlled '+' dissolve before opening compact picker */
    int            qp_pick_cat;             /* 0..NL_AVATAR_CAT_COUNT-1 inside compact picker */
    int            qp_pick_cursor;          /* Cursor inside active category of compact picker */

    /* Dedicated Settings « Profils utilisateurs » management state */
    nl_prof_mode_t prof_mode;
    int            prof_cursor;             /* 0 = Lock toggle, 1 = '+ Creer un profil', 2..count+1 = profile */
    int            prof_scroll;
    int            prof_detail_idx;
    int            prof_detail_cursor;      /* 0=Nom, 1=Icone, 2=Activer, 3=Verrouiller, 4=Supprimer */
    char           prof_edit_name[NL_PROFILE_NAME_LEN];
    int            prof_edit_len;
    int            prof_avatar_cat;         /* 0..NL_AVATAR_CAT_COUNT-1 */
    int            prof_avatar_cursor;      /* Index inside active avatar category */
    bool           prof_delete_yes;         /* false = Annuler (safe default) */
    bool           prof_from_quick;         /* Entered creation flow from Quick Selector '+' */
    bool           profiles_dirty;

    /* Diagnostics & Journaux screen state */
    int            diag_tab;                /* 0 = Erreurs & Resume, 1 = Etapes boot & Systeme */
    int            diag_cursor;
    int            diag_scroll;
    bool           diag_detail_open;
    bool           diag_confirm_modal;
    bool           diag_confirm_yes;        /* false = Annuler (safe default) */
    bool           diag_clear_req;

    /* Hardware action signals & Hotkey UI focus */
    int            hotkey_cursor;           /* 0=Activer ON/OFF, 1=Combinaison */
    bool           hotkey_toggle_req;
    bool           hotkey_target_val;
    bool           rescan_req;
    bool           poweroff_req;
    bool           config_dirty;
    bool           consume_exe_release;     /* R16: Latched when any modal consumes ENTER/EXE or closes */
} nl_settings_ui_t;

/* Real-Time Graphics & Frame-Pacing Performance Telemetry (SP804 32,768 Hz on hardware) */
typedef struct {
    bool     hw_timer_active;
    uint32_t frame_count;
    uint32_t total_dt_us;
    uint32_t min_dt_us;
    uint32_t max_dt_us;
    uint32_t last_dt_us;
    uint32_t total_jitter_us;
    uint32_t max_jitter_us;
    uint32_t total_render_us;
    uint32_t max_render_us;
    uint32_t fps_drop_count;        /* Frames exceeding 20,000 us (< 50 FPS) */
    uint32_t stutter_count;         /* Frames exceeding 25,000 us (> 1.5x 60 FPS target) */
    uint32_t open_render_us;        /* Boot/splash transition peak render time */
    uint32_t menu_render_us;        /* Settings open/section transition peak render time */
    uint32_t launch_render_us;      /* Game launch zoom+ripples peak render time */
    uint32_t profile_render_us;     /* Profile switch transition peak render time */
    uint32_t cursor_render_us;      /* Mouse cursor + trail peak render time */
    uint32_t physical_clicks;       /* Count of verified hardware physical clicks */
    uint32_t tap_clicks;            /* Count of verified hardware tap-to-click activations */
    uint32_t return_restore_us;     /* #R23 Microseconds from nl_exec() return to Frame 1 LCD blit */
    uint32_t return_count;          /* #R23 Number of completed game returns in session */
    uint32_t open_total_ms;         /* #R23 Total boot-to-library latency (ms) */
    uint32_t close_total_ms;        /* #R23 Total shutdown-to-TI-OS latency (ms) */
} nl_perf_stats_t;

/* Complete Launcher State */
typedef struct {
    nl_game_t     games[NL_MAX_GAMES];
    int           total_games;

    int           filtered_indices[NL_MAX_GAMES];
    int           filtered_count;

    int           selected_pos;
    int           current_page;
    int           total_pages;

    nl_category_t     active_category;
    nl_display_mode_t display_mode;
    nl_accent_color_t accent_color;
    nl_settings_t     settings;
    nl_settings_ui_t  sui;
    nl_sys_env_t      sys;
    nl_profile_sys_t  profiles;
    nl_diag_store_t   diag;
    nl_perf_stats_t   perf;

    bool              should_exit;
    bool              launch_requested;
    int               launch_game_idx;
    int               last_exec_status;
    uint32_t          play_counter;

    char              covers_dir[NL_MAX_PATH_LEN];
    char              icons_dir[NL_MAX_PATH_LEN];
    char              profiles_path[NL_MAX_PATH_LEN];
    char              lib_path[NL_MAX_PATH_LEN];
    nl_folder_list_t  folders;
    nl_scan_stats_t   scan;
    uint32_t          icon_ram_bytes;
    nl_anim_t         anim;

    /* Internal diagnostics (not shown in main console UI) */
    int               scanned_files;
    int               rejected_docs;
    int               rejected_sys;
    int               rejected_dups;
    nl_cap_diag_t     cap_diag;
} nl_state_t;

/* Theme, Layout & Utility Helpers */
const nl_theme_t *nl_get_theme(nl_display_mode_t mode, nl_accent_color_t accent);
const char *nl_category_label(nl_category_t cat);
const char *nl_layout_label(nl_layout_t layout);
const char *nl_scroll_dir_label(nl_scroll_dir_t dir);
const char *nl_sort_order_label(nl_sort_order_t sort);
const char *nl_app_open_anim_label(nl_app_open_anim_t mode);
const char *nl_app_open_anim_short_label(nl_app_open_anim_t mode);
const char *nl_app_open_anim_desc(nl_app_open_anim_t mode);
const char *nl_app_open_anim_to_str(nl_app_open_anim_t mode);
nl_app_open_anim_t nl_str_to_app_open_anim(const char *val);
const char *nl_app_close_anim_label(nl_app_close_anim_t mode);
const char *nl_app_close_anim_short_label(nl_app_close_anim_t mode);
const char *nl_app_close_anim_desc(nl_app_close_anim_t mode);
const char *nl_app_close_anim_to_str(nl_app_close_anim_t mode);
nl_app_close_anim_t nl_str_to_app_close_anim(const char *val);
const char *nl_launch_anim_label(nl_launch_anim_t mode);
const char *nl_launch_anim_short_label(nl_launch_anim_t mode);
const char *nl_launch_anim_desc(nl_launch_anim_t mode);
const char *nl_launch_anim_to_str(nl_launch_anim_t mode);
nl_launch_anim_t nl_str_to_launch_anim(const char *val);
const char *nl_return_anim_label(nl_return_anim_t mode);
const char *nl_return_anim_short_label(nl_return_anim_t mode);
const char *nl_return_anim_desc(nl_return_anim_t mode);
const char *nl_return_anim_to_str(nl_return_anim_t mode);
nl_return_anim_t nl_str_to_return_anim(const char *val);
void nl_get_layout_metrics(nl_layout_t layout, bool show_names, nl_layout_metrics_t *out);
void nl_get_layout_metrics_ex(nl_layout_t layout, bool show_names, bool show_shortcuts, nl_layout_metrics_t *out);
void nl_get_layout_metrics_for_count(nl_layout_t layout, bool show_names, bool show_shortcuts, int item_count, nl_layout_metrics_t *out);
void nl_get_layout_metrics_full(nl_layout_t layout, nl_scroll_dir_t scroll_dir, bool show_names, bool show_shortcuts, int item_count, nl_layout_metrics_t *out);
void nl_compute_grid_slot_pos(const nl_layout_metrics_t *m, int filtered_count, int f_idx, int *out_x, int *out_y);
uint16_t nl_rgb565_lerp(uint16_t c0, uint16_t c1, unsigned alpha_0_to_32);
static inline uint16_t nl_rgb565_lerp_fast(uint16_t c0, uint16_t c1, unsigned a) {
    if (a == 0U || c0 == c1) return c0;
    if (a >= 32U) return c1;
    uint32_t s0 = ((uint32_t)c0 | ((uint32_t)c0 << 16)) & 0x07E0F81FU;
    uint32_t s1 = ((uint32_t)c1 | ((uint32_t)c1 << 16)) & 0x07E0F81FU;
    uint32_t d  = ((((s1 - s0) * a) >> 5) + s0) & 0x07E0F81FU;
    return (uint16_t)(d | (d >> 16));
}
void nl_extract_game_stem(const char *filename, char *out_stem, size_t max_len);
void nl_compute_monogram(const char *title, char out_mono[3]);
void nl_format_duration(uint32_t seconds, char *out_buf, size_t max_len);
void nl_format_timestamp(uint32_t epoch_sec, char *out_buf, size_t max_len);

/* Custom Shortcut Key Registry & Conflict Validation */
bool nl_shortcut_is_reserved(char ch);
bool nl_shortcut_is_assignable(char ch);
int  nl_shortcut_find_owner(const nl_state_t *state, char ch, int exclude_game_idx);
int  nl_get_assignable_keys(const char **out_chars);
int  nl_get_shortcut_defs(const nl_state_t *state, nl_shortcut_def_t *out_defs, int max_defs);

/* Cooperative Boot Progress Callback */
typedef void (*nl_progress_cb_t)(void *ctx);

/* scanner.c — Binary Inspection, Filtering & Multi-Directory Scan */
nl_bin_format_t nl_inspect_tns_buffer(
    const uint8_t *buf,
    size_t buf_len,
    uint32_t total_file_size,
    const char *filename,
    const char *self_path,
    nl_exec_info_t *out_info
);
nl_bin_format_t nl_inspect_tns_file(
    const char *filepath,
    const char *self_path,
    nl_exec_info_t *out_info
);
bool nl_is_system_or_self(const char *filepath, const char *self_path);
int  nl_scan_directory(nl_state_t *state, const char *root_dir, const char *self_path, int max_depth);
int  nl_scan_directory_ex(
    nl_state_t *state,
    const char *root_dir,
    const char *self_path,
    int max_depth,
    nl_progress_cb_t progress_cb,
    void *progress_ctx
);
int  nl_register_game(nl_state_t *state, const char *filepath, const nl_exec_info_t *info);
bool nl_file_exists(const char *filepath);
bool nl_folder_add(nl_folder_list_t *list, const char *dir_path);
void nl_folder_seed_defaults(nl_folder_list_t *list, const char *docs_root);
bool nl_transactional_scan(
    nl_state_t *state,
    const char *docs_root,
    const char *self_path,
    bool is_startup,
    bool force_allow_empty,
    nl_scan_progress_cb_t progress_cb,
    void *progress_ctx
);
void nl_rescan_apply_empty_confirm(nl_state_t *state, bool replace_with_empty);

/* Non-blocking incremental transactional scan job (drives interactive 60 FPS rescan + ESC/Annuler cancel) */
typedef struct nl_scan_job nl_scan_job_t;
nl_scan_job_t *nl_scan_job_start(nl_state_t *state, const char *docs_root, const char *self_path);
bool           nl_scan_job_step(nl_scan_job_t *job, nl_state_t *state, int max_items);
bool           nl_scan_job_is_done(const nl_scan_job_t *job);
bool           nl_scan_job_was_cancelled(const nl_scan_job_t *job);
void           nl_scan_job_cancel(nl_scan_job_t *job, nl_state_t *state);
void           nl_scan_job_destroy(nl_scan_job_t *job);

/* covers.c — Real Game Cover Loading (.png.tns/.ico.tns/.bmp.tns), VRAM Capture & Incremental Icon Batch */
typedef struct nl_icon_batch nl_icon_batch_t;
nl_icon_batch_t *nl_icon_batch_start(const char *covers_dir);
bool             nl_icon_batch_find_file(nl_icon_batch_t *batch, const nl_game_t *game, char *out_path, size_t max_len);
bool             nl_icon_batch_decode_file(const char *cand_path, nl_game_t *game);
uint32_t         nl_game_icon_ram_bytes(const nl_game_t *game);
void             nl_icon_batch_destroy(nl_icon_batch_t *batch);

void nl_set_icon_decode_progress_cb(nl_progress_cb_t cb, void *ctx);
void nl_load_all_icons(nl_state_t *state);
void nl_load_all_icons_ex(nl_state_t *state, nl_progress_cb_t progress_cb, void *progress_ctx);
bool nl_load_game_icon(nl_game_t *game, const char *covers_dir);
bool nl_save_captured_icon(const nl_game_t *game, const char *covers_dir);
bool nl_delete_captured_icon(nl_game_t *game, const char *covers_dir);
void nl_free_game_icons_only(nl_state_t *state);
void nl_free_media_buffers(nl_state_t *state);
void nl_free_icons(nl_state_t *state);
int  nl_evaluate_frame_score(
    const void *frame_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256
);
bool nl_capture_frame_to_icon_ex(
    nl_game_t *game,
    const void *frame_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256,
    bool stage_only,
    int *out_score
);
bool nl_capture_frame_to_icon(
    nl_game_t *game,
    const void *frame_base,
    bool rotated_240x320,
    bool is_8bpp,
    const uint16_t *hw_palette256
);
bool nl_capture_vram_to_icon(
    nl_game_t *game,
    const void *vram_base,
    bool rotated_240x320,
    const uint16_t *hw_palette256
);
bool nl_screenshot_is_valid_buffer(const uint16_t *pixels);
bool nl_game_has_valid_screenshot_on_disk(const nl_game_t *game, const char *covers_dir);
void nl_probe_all_screenshots(nl_state_t *state);
void nl_probe_all_screenshots_ex(nl_state_t *state, nl_progress_cb_t progress_cb, void *progress_ctx);
bool nl_load_game_screenshot(nl_game_t *game, const char *covers_dir);
void nl_load_all_screenshots(nl_state_t *state);
bool nl_save_captured_screenshot(const nl_game_t *game, const char *covers_dir);
bool nl_capture_frame_to_screenshot_ex(
    nl_game_t *game,
    const void *frame_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256,
    bool stage_only,
    int *out_score
);
bool nl_capture_vram_to_screenshot(
    nl_game_t *game,
    const void *vram_base,
    bool rotated_240x320,
    const uint16_t *hw_palette256
);
void nl_free_screenshots(nl_state_t *state);
bool nl_delete_custom_icon(nl_game_t *game, const char *covers_dir);

/* diag.c — Centralized Grouped Diagnostics, 12-Stage Boot Forensics, Backups & OOM Recovery */
const char *nl_diag_code_str(nl_diag_code_t code);
const char *nl_diag_stage_label(nl_boot_stage_t stage);
void nl_diag_init(nl_state_t *state, const char *docs_root);
void nl_diag_read_prev_stage(nl_state_t *state);
void nl_diag_mark_stage_ram(nl_state_t *state, nl_boot_stage_t stage);
void nl_diag_record_stage(nl_state_t *state, nl_boot_stage_t stage, bool clean_exit);
void nl_disable_watchdog_safe(void);
void nl_diag_report(
    nl_state_t *state,
    nl_diag_code_t code,
    const char *target_name,
    const char *path,
    const char *operation,
    const char *reason,
    const char *impact,
    const char *recovery
);
bool nl_diag_backup_corrupt_file(const char *src_path, char *out_bak_path, size_t bak_max);
bool nl_diag_clear_logs(nl_state_t *state);
void nl_recover_memory_on_oom(nl_state_t *state, const char *context_op, const char *target_path);

/* covers.c context-aware diagnostic helpers */
bool nl_load_game_icon_diag(nl_state_t *state, nl_game_t *game, const char *covers_dir);
bool nl_load_game_screenshot_diag(nl_state_t *state, nl_game_t *game, const char *covers_dir);

/* storage.c — Persistent Config, Independent Library Index, Cache, Stats, Shortcuts & Hotkey Control */
uint32_t nl_crc32_compute(const void *data, size_t len);
bool nl_save_library_index(const nl_state_t *state, const char *lib_path);
bool nl_load_library_index(nl_state_t *state, const char *lib_path);
bool nl_save_library_cache(const nl_state_t *state, const char *cache_path);
bool nl_load_library_cache(nl_state_t *state, const char *cache_path);
int  nl_hydrate_icons_from_cache(nl_state_t *state, const char *cache_path, nl_progress_cb_t progress_cb, void *progress_ctx);
bool nl_save_config(const nl_state_t *state, const char *cfg_path);
bool nl_load_config(nl_state_t *state, const char *cfg_path);
bool nl_reset_settings_only(nl_state_t *state, const char *cfg_path);
void nl_cleanup_legacy_autostart(const char *docs_root);
bool nl_hotkey_set(nl_state_t *state, bool enable, char *out_diag, size_t diag_max);
bool nl_hotkey_verify_active(const char *docs_root, const nl_sys_env_t *env);
bool nl_hotkey_is_installed_in_ram(bool *out_enabled, int *out_combo, uint32_t *out_launch_count);
void nl_hotkey_set_ram_busy(bool busy);
uint32_t nl_hotkey_get_ram_hook_mode(void);
void nl_hotkey_sync_live_combo(nl_state_t *state);
void nl_hotkey_reconcile(nl_state_t *state);
const char *nl_resolve_os_version_string(uint32_t sig_0x10000020);
bool nl_check_ndless_persistent_at(const char *phoenix_dir);

/* Touchpad Sliding Motion Tracker (Pure 32-bit Integer Math, Zero Heap Allocation) */
#define NL_TP_STEP_THRESHOLD 215            /* ~21% of touchpad span (1024 normalized units) */

typedef struct {
    bool     in_contact;
    uint16_t last_x;
    uint16_t last_y;
    int32_t  acc_x;
    int32_t  acc_y;
    int      cooldown_ticks;
} nl_touchpad_tracker_t;

void nl_touchpad_tracker_reset(nl_touchpad_tracker_t *tracker);
int nl_touchpad_threshold_for_level(int sensitivity_level);
nl_key_t nl_touchpad_process_sample_ex(
    nl_touchpad_tracker_t *tracker,
    bool touchpad_nav_enabled,
    bool contact,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h,
    int sensitivity_level
);
nl_key_t nl_touchpad_process_sample(
    nl_touchpad_tracker_t *tracker,
    bool touchpad_nav_enabled,
    bool contact,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h
);

/* Touchpad Physical Click + Tap-to-Click Guard */
#define NL_TP_CLICK_SLIDE_CANCEL        140 /* Normalized motion cancelling Mode 1 click-to-launch */
#define NL_TP_TAP_SLIDE_CANCEL          150 /* Forgiving normalized motion threshold distinguishing tap vs slide */
#define NL_TP_TAP_MAX_TICKS              26 /* Max tap duration (~433 ms at 60 FPS) */
#define NL_TP_CLICK_RELEASE_TICKS         2
#define NL_TP_CLICK_COOLDOWN_TICKS       10 /* ~165 ms anti-double-click guard */
#define NL_TP_TAP_COOLDOWN_TICKS         14 /* ~233 ms anti-double-tap guard */
#define NL_TP_SCAN_ERR_RESET_THRESHOLD    5 /* Consecutive touchpad_scan errors before resetting state */

typedef enum {
    NL_TP_ACT_NONE           = 0,
    NL_TP_ACT_PHYSICAL_CLICK = 1,
    NL_TP_ACT_TAP_CLICK      = 2
} nl_tp_click_action_t;

typedef struct {
    bool     in_contact;
    uint16_t down_x;
    uint16_t down_y;
    bool     moved_since_down;
    bool     wait_release;
    int      release_streak;
    int      cooldown_ticks;
    /* Mouse Mode physical-click & tap-to-click independent state */
    bool     prev_pressed;
    bool     tap_tracking;
    bool     tap_in_center;
    bool     tap_moved;
    bool     tap_had_press;
    uint16_t tap_down_x;
    uint16_t tap_down_y;
    int      tap_ticks;
    int      tap_max_delta;
    int      recent_move_ticks;
    int      scan_err_streak;
} nl_tp_click_guard_t;

void nl_tp_click_reset(nl_tp_click_guard_t *guard);
bool nl_tp_click_poll(
    nl_tp_click_guard_t *guard,
    bool contact,
    bool pressed,
    bool center_click,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h
);
nl_tp_click_action_t nl_mouse_tp_poll(
    nl_tp_click_guard_t *guard,
    bool contact,
    bool pressed,
    bool center_click,
    bool hover_active,
    bool tap_to_click_enabled,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h
);

/* Settings Shortcut (Physical Scratchpad/Save key OR Ctrl+S / sticky Ctrl -> S) with Bounce & Cooldown Guard */
#define NL_CTRL_LATCH_TICKS        90 /* ~1.5s sticky CTRL window after release */
#define NL_SETTINGS_COOLDOWN_TICKS 18 /* ~300ms guard against instant open+close toggle */
#define NL_SETTINGS_RELEASE_TICKS  3  /* Require 3 clean release frames before re-arming */

typedef struct {
    bool ctrl_was_down;
    int  ctrl_latch_ticks;
    bool wait_full_release;
    int  release_streak;
    int  cooldown_ticks;
} nl_save_shortcut_t;

void nl_save_shortcut_reset(nl_save_shortcut_t *sc);
void nl_save_shortcut_clear_latch(nl_save_shortcut_t *sc);
bool nl_save_shortcut_poll(
    nl_save_shortcut_t *sc,
    bool direct_key_down,
    bool ctrl_down,
    bool s_down,
    bool other_key_down
);

/* ui_state.c & animation.c — State, Grid Navigation, Sorting, Inline Search & Settings */
void nl_init_state(nl_state_t *state);
void nl_rebuild_filter(nl_state_t *state);
bool nl_handle_key(nl_state_t *state, nl_key_t key);
bool nl_handle_char_input(nl_state_t *state, char ch);
bool nl_anim_step(nl_state_t *state);
void nl_return_drop_start(nl_state_t *state, int game_idx);
void nl_show_toast(nl_state_t *state, const char *msg, int duration_ticks);
void nl_toggle_favorite(nl_state_t *state);
void nl_record_launch(nl_state_t *state, int game_idx);
void nl_record_play_session(nl_state_t *state, int game_idx, uint32_t start_rtc, uint32_t end_rtc);
bool nl_return_needs_filter_rebuild(const nl_state_t *state);
void nl_batch_capture_init(nl_state_t *state);
bool nl_batch_capture_advance(nl_state_t *state, bool captured_ok, bool was_skipped, nl_cap_status_t st);
void nl_batch_capture_cancel(nl_state_t *state);
const char *nl_cursor_style_to_str(nl_cursor_style_t style);
nl_cursor_style_t nl_str_to_cursor_style(const char *s);
const char *nl_cursor_style_label(nl_cursor_style_t style);
const char *nl_cursor_size_to_str(nl_cursor_size_t size);
nl_cursor_size_t nl_str_to_cursor_size(const char *s);
const char *nl_cursor_size_label(nl_cursor_size_t size);
void nl_cursor_get_size_metrics(
    nl_cursor_style_t style,
    nl_cursor_size_t size,
    int *out_w,
    int *out_h,
    int *out_hx,
    int *out_hy
);
const char *nl_cursor_speed_label(int speed_level);
bool nl_mouse_is_active(const nl_state_t *state);
bool nl_cursor_is_visible(const nl_state_t *state);
void nl_cursor_compute_target(const nl_state_t *state, int *out_x, int *out_y);
void nl_cursor_wake(nl_state_t *state);
void nl_cursor_apply_touchpad_nudge(nl_state_t *state, const nl_touchpad_tracker_t *tracker, bool contact);
bool nl_mouse_feed_touchpad(
    nl_state_t *state,
    bool contact,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h
);
void nl_mouse_move_to(nl_state_t *state, int x_px, int y_px);
void nl_mouse_update_hover(nl_state_t *state);
bool nl_mouse_click(nl_state_t *state);

/* profiles.c — Multi-User Profile Management, 4-Category Exclusive Avatars & Isolation */
const uint16_t *nl_avatar_get_pixels(int avatar_id);
const char *nl_avatar_category_label(int cat_idx);
void nl_avatar_category_range(int cat_idx, int *out_start, int *out_count);
int  nl_avatar_category_of(int avatar_id);
int  nl_avatar_find_owner(const nl_state_t *state, int avatar_id, int exclude_profile_idx);
int  nl_avatar_first_free(const nl_state_t *state);
void nl_profiles_init_default(nl_state_t *state);
void nl_profiles_free(nl_state_t *state);
void nl_profile_snapshot_active(nl_state_t *state);
void nl_profile_apply_active(nl_state_t *state);
const nl_profile_t *nl_profile_get_active(const nl_state_t *state);
bool nl_profile_switch_to(nl_state_t *state, int target_idx);
bool nl_profile_step(nl_state_t *state, int delta);
int  nl_profile_create(nl_state_t *state, const char *name, int avatar_id);
bool nl_profile_rename(nl_state_t *state, int prof_idx, const char *new_name);
bool nl_profile_set_avatar(nl_state_t *state, int prof_idx, int avatar_id);
bool nl_profile_delete(nl_state_t *state, int prof_idx);
bool nl_profile_is_protected(const nl_state_t *state, int prof_idx);
bool nl_profile_toggle_lock(nl_state_t *state, int prof_idx);
bool nl_save_profiles(nl_state_t *state, const char *profiles_path);
bool nl_load_profiles(nl_state_t *state, const char *profiles_path);
void nl_sync_active_cat_pill_targets(nl_state_t *state, bool snap);

/* renderer.c — Library Grid, Top Gear, Settings Views & Smooth Animations */
void nl_renderer_cleanup(void);
void nl_renderer_prepare_for_exec(const nl_state_t *state);
int  nl_renderer_cached_sprite_count(void);
void nl_render_frame(const nl_state_t *state, uint16_t *fb);
void nl_render_splash(const nl_state_t *state, uint16_t *fb, int step, int max_steps);
void nl_render_splash_anim(const nl_state_t *state, uint16_t *fb, uint32_t elapsed_ms, bool show_loader);
void nl_render_boot_transition(
    const nl_state_t *state,
    uint16_t *fb,
    const uint16_t *last_splash_fb,
    const uint16_t *lib_fb,
    int step,
    int max_steps
);
void nl_render_ink_transition(
    uint16_t *fb,
    const uint16_t *old_fb,
    const uint16_t *new_fb,
    uint32_t seed,
    int step,
    int max_steps
);
void nl_render_launch_zoom(const nl_state_t *state, uint16_t *fb, int step, int max_steps);
void nl_render_app_close_anim(
    const nl_state_t *state,
    uint16_t *fb,
    const uint16_t *base_fb,
    int step,
    int max_steps
);
bool nl_renderer_has_static_fb_cached(void);
int      nl_liquid_eval_radius(int angle_256, int r_px, int irreg_256, uint32_t seed, int phase_256);
uint16_t nl_liquid_theme_color_rgb565(nl_display_mode_t dm, nl_accent_color_t accent, int layer_0_to_3);

#ifdef __cplusplus
}
#endif

#endif /* NLAUNCHER_H */
