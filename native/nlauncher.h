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
#define NL_BUILD_STAMP_STR      "Build 2026-11-17 #R8"

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
#define NL_ICON_MAX_DIM     128
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
    NL_ICON_ICO      = 3  /* Genuine Windows .ico custom icon (.ico.tns) */
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
    int view_bottom;
    int pagination_y;
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

/* UI Screens (§1 - §15) */
typedef enum {
    NL_UI_LIBRARY                = 0,
    NL_UI_SETTINGS_MAIN          = 1,
    NL_UI_SETTINGS_AUTOSTART     = 2,
    NL_UI_SETTINGS_DISPLAY       = 3,
    NL_UI_SETTINGS_TOUCHPAD      = 4,
    NL_UI_SETTINGS_SHORTCUTS     = 5,
    NL_UI_SETTINGS_KEY_HELP      = 6,
    NL_UI_SETTINGS_SORT          = 7,
    NL_UI_SETTINGS_STATS         = 8,
    NL_UI_SETTINGS_RESET         = 9,
    NL_UI_SETTINGS_INFO          = 10,
    NL_UI_SETTINGS_CREDITS       = 11,
    NL_UI_SETTINGS_CAPTURE_BATCH = 12,
    NL_UI_SETTINGS_ICONS         = 13,
    NL_UI_SETTINGS_HOTKEY        = 14,
    NL_UI_SETTINGS_PROFILES      = 15
} nl_ui_screen_t;

/* Settings Main Menu Entries (14 entries; Autostart completely removed) */
typedef enum {
    NL_SET_MENU_DISPLAY       = 0,
    NL_SET_MENU_TOUCHPAD      = 1,
    NL_SET_MENU_SHORTCUTS     = 2,
    NL_SET_MENU_KEY_HELP      = 3,
    NL_SET_MENU_SORT          = 4,
    NL_SET_MENU_STATS         = 5,
    NL_SET_MENU_CAPTURE_BATCH = 6,
    NL_SET_MENU_ICONS         = 7,
    NL_SET_MENU_REFRESH_LIB   = 8,
    NL_SET_MENU_HOTKEY        = 9,
    NL_SET_MENU_PROFILES      = 10,
    NL_SET_MENU_RESET         = 11,
    NL_SET_MENU_INFO          = 12,
    NL_SET_MENU_CREDITS       = 13,
    NL_SET_MENU_COUNT         = 14
} nl_settings_menu_idx_t;

typedef enum {
    NL_BAT_UNAVAILABLE = 0,
    NL_BAT_LOW         = 1,
    NL_BAT_MEDIUM      = 2,
    NL_BAT_HIGH        = 3
} nl_battery_level_t;

#define NL_BATTERY_REFRESH_INTERVAL_SEC 3600U
#define NL_DISPLAY_OPT_COUNT            4     /* 0=Layout, 1=Names, 2=Shortcuts, 3=Power Off Calculator */
#define NL_BAT_IND_X                    262
#define NL_BAT_IND_Y                    7
#define NL_BAT_IND_W                    24
#define NL_BAT_IND_H                    11

/* Persistent Binary Library & Pre-Decoded Icon Cache (/documents/ndless/nlauncher.cache.tns) */
#define NL_CACHE_MAGIC   0x48434C4EU /* "NLCH" in little-endian */
#define NL_CACHE_VERSION 4U

typedef struct {
    uint32_t magic;            /* NL_CACHE_MAGIC ("NLCH") */
    uint32_t version;          /* NL_CACHE_VERSION (2) */
    uint32_t total_games;      /* 0..NL_MAX_GAMES */
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
    NL_HOVER_SET_ESC,
    NL_HOVER_SET_POWER,
    NL_HOVER_SET_ROW,
    NL_HOVER_SET_TAB,
    NL_HOVER_SET_SLIDER_NOTCH,
    NL_HOVER_SET_STYLE_CARD,
    NL_HOVER_MODAL_BTN,
    NL_HOVER_AVATAR_CAT,
    NL_HOVER_AVATAR_CELL,
    NL_HOVER_SC_KEY_CELL,
    NL_HOVER_ICON_CARD,
    NL_HOVER_PROF_ROW,
    NL_HOVER_PROF_DETAIL_ROW,
    NL_HOVER_PROF_AV_TAB,
    NL_HOVER_PROF_AV_CELL
} nl_hover_kind_t;

#define NL_CURSOR_TRAIL_LEN             5
#define NL_CURSOR_IDLE_FADE_START_TICKS 75
#define NL_CURSOR_IDLE_MIN_OPACITY_FP   112

typedef struct {
    bool               autostart;
    bool               global_hotkey;
    nl_layout_t        layout;
    bool               show_names;
    bool               show_shortcuts;
    bool               show_battery;
    nl_touchpad_mode_t touchpad_mode;         /* NL_TP_MODE_TOUCHPAD or NL_TP_MODE_MOUSE */
    bool               touchpad_nav;
    int                touchpad_sensitivity;  /* 0..4 (5 mechanical detents, default 2 = 3/5) */
    int                cursor_speed;          /* 0..4 (independent mouse pointer speed, default 2) */
    bool               touchpad_cursor_show;  /* Legacy / profile sync flag */
    nl_cursor_style_t  touchpad_cursor_style; /* Selected cursor style (ARROW / CIRCLE / POINTER) */
    nl_sort_order_t    sort_order;
} nl_settings_t;

/* Runtime System / Autostart & Battery Verification Context */
typedef struct {
    char               docs_root[NL_MAX_PATH_LEN];
    char               self_path[NL_MAX_PATH_LEN];
    char               os_version[32];
    uint32_t           ndless_rev;
    bool               has_ndless;
    bool               hw_is_cx2;
    bool               ndless_persistent;
    bool               autostart_active;
    bool               hotkey_active;
    bool               cache_valid;
    bool               cache_loaded_at_boot;
    uint32_t           cache_crc32;
    bool               boot_recovery_triggered;
    char               boot_recovery_reason[32];
    char               autostart_diag[64];
    char               hotkey_diag[64];
    nl_battery_level_t battery_level;
    uint32_t           battery_last_rtc;
    uint32_t           battery_read_count;
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
    uint16_t        *icon_pixels;      /* Heap-allocated icon_w * icon_h RGB565 icon */
    uint8_t         *icon_alpha;       /* Optional alpha channel (0..255) for 32bpp RGBA .ico */
    bool             has_screenshot;   /* Separate real gameplay capture (.scr.tns) */
    uint16_t        *scr_pixels;       /* Heap-allocated 94x68 RGB565 screenshot */
} nl_game_t;

/* Smooth Console Animation Controller */
typedef struct {
    bool     initialized;
    int      splash_tick;                   /* 0..24: brief startup splash sequence */
    int      splash_max;
    int      scroll_y_fp;                   /* Vertical scroll Y in 1/16 px fixed-point */
    int      target_scroll_y_fp;            /* Target Y = current_page * page_stride << 4 */
    int      dot_pos_fp;                    /* Smooth active pagination dot X offset (1/16 px) */
    int      target_dot_fp;
    int      card_scale_fp[NL_MAX_GAMES];   /* Per-game scale in 8.8 FP (256=1.00x, 280=1.09x) */
    int      card_dx_fp[NL_MAX_GAMES];      /* Post-return sort glide X offset in 1/16 px (0 = settled) */
    int      card_dy_fp[NL_MAX_GAMES];      /* Post-return sort glide Y offset in 1/16 px (0 = settled) */
    bool     launch_holding;                /* True while LANCER held down before release */
    int      launch_shrink_fp;              /* 8.8 FP scale during hold (280 -> 232) */
    bool     launch_anim_active;
    int      launch_tick;                   /* 0..14: launch cover zoom + crossfade */
    int      launch_max_ticks;
    bool     ink_active;                    /* Organic ink diffusion transition for DOC/MENU */
    int      ink_tick;
    int      ink_max;
    uint32_t ink_seq;
    int      switch_autostart_fp;           /* 0..256 smooth animated toggle knob */
    int      switch_hotkey_fp;              /* 0..256 smooth animated toggle knob for global hotkey */
    int      switch_shownames_fp;           /* 0..256 smooth animated toggle knob */
    int      switch_shortcuts_fp;           /* 0..256 smooth animated toggle knob & layout transition */
    int      switch_battery_fp;             /* 0..256 smooth animated toggle knob for battery indicator */
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
    int      qp_glow_x_fp;                  /* 1/16 px smooth X tracking for multicolor focus ring */
    int      qp_glow_y_fp;                  /* 1/16 px smooth Y tracking for multicolor focus ring */
    int      qp_trail_x_fp;                 /* 1/16 px lagging luminous wake behind focus ring */
    int      qp_sel_bounce_fp;              /* 256..0 spring rebound impulse on avatar selection */
    int      qp_plus_focus_fp;              /* 0..256 '+' button focus & rotation progress */
    int      qp_slot_scale_fp[9];           /* Per-slot scale in 8.8 FP (256=1.00x, 302=1.18x) */
    uint16_t qp_glow_phase;                 /* 0..255 continuous luminous multicolor ring phase */
    bool     prof_switch_active;            /* True during 4-stage profile switch transition */
    int      prof_switch_tick;              /* 0..prof_switch_max */
    int      prof_switch_max;               /* Total ticks (22) for water-drop + ghost zoom + rebound + fade */
    int      prof_switch_avatar_id;         /* Target profile avatar ID */
    int      prof_switch_origin_x;          /* Dynamic origin X of selected profile icon */
    int      prof_switch_origin_y;          /* Dynamic origin Y of selected profile icon */
    int      prof_switch_origin_d;          /* Dynamic origin diameter of selected profile icon */
    bool     prof_plus_active;              /* True during '+' button sequential ghost zoom transition */
    int      prof_plus_tick;                /* 0..prof_plus_max */
    int      prof_plus_max;                 /* Total ticks (16) for '+' water-drop + ghost zoom + rebound */
    int      prof_plus_origin_x;            /* Dynamic origin X of '+' button */
    int      prof_plus_origin_y;            /* Dynamic origin Y of '+' button */
    bool     prof_birth_active;             /* True during new user creation confirmation animation */
    int      prof_birth_tick;               /* 0..prof_birth_max */
    int      prof_birth_max;                /* Total ticks (20) for avatar confirmation wave + arrival */
    int      prof_birth_avatar_id;          /* Newly created profile avatar ID */
    int      prof_birth_slot;               /* Newly created profile slot index in selector */
    int      prof_cat_trans_fp;             /* 0..256 smooth 4-category avatar transition */
    int      prof_cat_dir;                  /* -1 / +1 category transition direction */
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
    nl_layout_t            layout;
    bool                   show_names;
    bool                   show_shortcuts;
    bool                   touchpad_cursor_show;
    nl_cursor_style_t      touchpad_cursor_style;
    nl_sort_order_t        sort_order;
    nl_category_t          active_category;
    int                    selected_pos;
    uint32_t               play_counter;
    int                    game_data_count;
    nl_profile_game_data_t game_data[NL_MAX_PROFILE_GAMES];
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

    /* Display submenu cursor (0=Layout, 1=Names, 2=Shortcuts, 3=Show Battery, 4=Power Off Calculator) */
    int            display_cursor;
    int            display_scroll;

    /* Touchpad submenu cursor (Tab 0: 0=Mode, 1=SlideNav, 2=SlideSens, 3=MouseSpeed, 4=StyleEntry; Tab 1: style selector) */
    int            tp_cursor;
    int            tp_tab;                  /* 0 = Navigation tactile, 1 = Apparence du curseur */
    int            tp_style_cursor;         /* 0 = Fleche, 1 = Cercle, 2 = Pointeur */
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

    /* Quick Profile Selector overlay (Library only; zero edit/delete controls) */
    bool           qp_open;
    int            qp_cursor;               /* 0..profiles.count (count = '+' creation slot) */

    /* Dedicated Settings « Profils utilisateurs » management state */
    nl_prof_mode_t prof_mode;
    int            prof_cursor;             /* 0 = '+ Creer un profil', 1..count = profile */
    int            prof_scroll;
    int            prof_detail_idx;
    int            prof_detail_cursor;      /* 0=Nom, 1=Icone, 2=Activer, 3=Supprimer */
    char           prof_edit_name[NL_PROFILE_NAME_LEN];
    int            prof_edit_len;
    int            prof_avatar_cat;         /* 0..NL_AVATAR_CAT_COUNT-1 */
    int            prof_avatar_cursor;      /* Index inside active avatar category */
    bool           prof_delete_yes;         /* false = Annuler (safe default) */
    bool           prof_from_quick;         /* Entered creation flow from Quick Selector '+' */
    bool           profiles_dirty;

    /* Hardware action signals */
    bool           autostart_toggle_req;
    bool           autostart_target_val;
    bool           hotkey_toggle_req;
    bool           hotkey_target_val;
    bool           rescan_req;
    bool           battery_refresh_req;
    bool           poweroff_req;
    bool           config_dirty;
} nl_settings_ui_t;

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

    bool              should_exit;
    bool              launch_requested;
    int               launch_game_idx;
    int               last_exec_status;
    uint32_t          play_counter;

    char              covers_dir[NL_MAX_PATH_LEN];
    char              icons_dir[NL_MAX_PATH_LEN];
    char              profiles_path[NL_MAX_PATH_LEN];
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
const char *nl_sort_order_label(nl_sort_order_t sort);
const char *nl_battery_level_label(nl_battery_level_t level);
void nl_get_layout_metrics(nl_layout_t layout, bool show_names, nl_layout_metrics_t *out);
void nl_get_layout_metrics_ex(nl_layout_t layout, bool show_names, bool show_shortcuts, nl_layout_metrics_t *out);
void nl_get_layout_metrics_for_count(nl_layout_t layout, bool show_names, bool show_shortcuts, int item_count, nl_layout_metrics_t *out);
void nl_compute_grid_slot_pos(const nl_layout_metrics_t *m, int filtered_count, int f_idx, int *out_x, int *out_y);
uint16_t nl_rgb565_lerp(uint16_t c0, uint16_t c1, unsigned alpha_0_to_32);
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

/* covers.c — Real Game Cover Loading (.bmp.tns/.ico.tns), VRAM Capture & Optical TI-OS Battery Analysis */
void nl_load_all_icons(nl_state_t *state);
void nl_load_all_icons_ex(nl_state_t *state, nl_progress_cb_t progress_cb, void *progress_ctx);
bool nl_load_game_icon(nl_game_t *game, const char *covers_dir);
bool nl_save_captured_icon(const nl_game_t *game, const char *covers_dir);
bool nl_delete_captured_icon(nl_game_t *game, const char *covers_dir);
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
nl_battery_level_t nl_detect_battery_from_vram(
    const void *vram_base,
    bool rotated_240x320,
    nl_pixel_format_t fmt,
    const uint16_t *hw_palette256
);
void nl_stamp_tios_battery_icon(
    void *vram_base,
    bool rotated_240x320,
    nl_battery_level_t level,
    bool use_red_alert
);
bool nl_check_periodic_battery_due(
    const nl_state_t *state,
    uint32_t current_rtc
);

/* storage.c — Persistent Config, Cache, Stats, Shortcuts & Hotkey Control */
uint32_t nl_crc32_compute(const void *data, size_t len);
bool nl_save_library_cache(const nl_state_t *state, const char *cache_path);
bool nl_load_library_cache(nl_state_t *state, const char *cache_path);
bool nl_save_config(const nl_state_t *state, const char *cfg_path);
bool nl_load_config(nl_state_t *state, const char *cfg_path);
bool nl_reset_settings_only(nl_state_t *state, const char *cfg_path);
void nl_cleanup_legacy_autostart(const char *docs_root);
bool nl_autostart_set(nl_state_t *state, bool enable, char *out_diag, size_t diag_max);
bool nl_autostart_verify_active(const char *docs_root, const nl_sys_env_t *env);
void nl_autostart_reconcile(nl_state_t *state);
bool nl_hotkey_set(nl_state_t *state, bool enable, char *out_diag, size_t diag_max);
bool nl_hotkey_verify_active(const char *docs_root, const nl_sys_env_t *env);
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

/* Touchpad Physical Center Click Guard (launches selected game independently of slide nav) */
#define NL_TP_CLICK_SLIDE_CANCEL   140 /* Normalized motion cancelling click-to-launch */
#define NL_TP_CLICK_RELEASE_TICKS  2
#define NL_TP_CLICK_COOLDOWN_TICKS 12

typedef struct {
    bool     in_contact;
    uint16_t down_x;
    uint16_t down_y;
    bool     moved_since_down;
    bool     wait_release;
    int      release_streak;
    int      cooldown_ticks;
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
void nl_show_toast(nl_state_t *state, const char *msg, int duration_ticks);
void nl_toggle_favorite(nl_state_t *state);
void nl_record_launch(nl_state_t *state, int game_idx);
void nl_record_play_session(nl_state_t *state, int game_idx, uint32_t start_rtc, uint32_t end_rtc);
void nl_batch_capture_init(nl_state_t *state);
bool nl_batch_capture_advance(nl_state_t *state, bool captured_ok, bool was_skipped, nl_cap_status_t st);
void nl_batch_capture_cancel(nl_state_t *state);
const char *nl_cursor_style_to_str(nl_cursor_style_t style);
nl_cursor_style_t nl_str_to_cursor_style(const char *s);
const char *nl_cursor_style_label(nl_cursor_style_t style);
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
void nl_profile_snapshot_active(nl_state_t *state);
void nl_profile_apply_active(nl_state_t *state);
const nl_profile_t *nl_profile_get_active(const nl_state_t *state);
bool nl_profile_switch_to(nl_state_t *state, int target_idx);
bool nl_profile_step(nl_state_t *state, int delta);
int  nl_profile_create(nl_state_t *state, const char *name, int avatar_id);
bool nl_profile_rename(nl_state_t *state, int prof_idx, const char *new_name);
bool nl_profile_set_avatar(nl_state_t *state, int prof_idx, int avatar_id);
bool nl_profile_delete(nl_state_t *state, int prof_idx);
bool nl_save_profiles(nl_state_t *state, const char *profiles_path);
bool nl_load_profiles(nl_state_t *state, const char *profiles_path);
void nl_sync_active_cat_pill_targets(nl_state_t *state, bool snap);

/* renderer.c — Library Grid, Top Gear, Settings Views & Smooth Animations */
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

#ifdef __cplusplus
}
#endif

#endif /* NLAUNCHER_H */
