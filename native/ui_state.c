/****************************************************************************
 * N-Launcher — ui_state.c
 * Strict 3-Category Library State (TOUS, FAV, REC), Dynamic Layouts (1x1..4x4),
 * 7 Sort Orders, Custom Shortcuts Registry, Live Search, Stats, Safe Reset &
 * Complete Settings Menu Controller
 ****************************************************************************/

#include "nlauncher.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Centralized 3x4 Theme Matrix: (DARK, LIGHT, GRAY) x (BLUE, RED, GREEN, YELLOW) */
static const nl_theme_t G_THEMES[NL_DISPLAY_COUNT][NL_ACCENT_COUNT] = {
    /* NL_DISPLAY_DARK */
    {
        /* BLUE */
        {
            RGB565(13, 17, 23),    /* bg_main */
            RGB565(20, 25, 35),    /* bg_top_bar */
            RGB565(16, 20, 28),    /* bg_bottom_bar */
            RGB565(25, 32, 44),    /* card_bg */
            RGB565(34, 43, 58),    /* card_bg_sel */
            RGB565(46, 56, 72),    /* border_subtle */
            RGB565(78, 88, 108),   /* dot_inactive */
            RGB565(240, 244, 250), /* text_primary */
            RGB565(136, 150, 172), /* text_secondary */
            RGB565(56, 189, 248),  /* accent_primary */
            RGB565(125, 211, 252), /* accent_secondary */
            RGB565(10, 16, 26),    /* text_on_accent */
            RGB565(250, 204, 21),  /* badge_fav */
            RGB565(34, 43, 58),    /* pill_bg */
            false                  /* is_light */
        },
        /* RED */
        {
            RGB565(15, 16, 21),
            RGB565(24, 22, 29),
            RGB565(19, 18, 24),
            RGB565(30, 28, 37),
            RGB565(41, 37, 49),
            RGB565(56, 50, 66),
            RGB565(92, 84, 102),
            RGB565(245, 243, 247),
            RGB565(156, 148, 166),
            RGB565(239, 68, 68),
            RGB565(248, 113, 113),
            RGB565(255, 255, 255),
            RGB565(250, 204, 21),
            RGB565(39, 35, 47),
            false
        },
        /* GREEN */
        {
            RGB565(13, 18, 17),
            RGB565(19, 27, 25),
            RGB565(15, 22, 20),
            RGB565(24, 35, 32),
            RGB565(32, 47, 42),
            RGB565(45, 65, 58),
            RGB565(76, 100, 92),
            RGB565(240, 247, 244),
            RGB565(138, 164, 155),
            RGB565(34, 197, 94),
            RGB565(74, 222, 128),
            RGB565(10, 24, 15),
            RGB565(250, 204, 21),
            RGB565(31, 45, 40),
            false
        },
        /* YELLOW */
        {
            RGB565(16, 16, 18),
            RGB565(25, 24, 26),
            RGB565(20, 19, 21),
            RGB565(32, 31, 34),
            RGB565(44, 42, 46),
            RGB565(62, 59, 64),
            RGB565(98, 94, 102),
            RGB565(248, 246, 240),
            RGB565(164, 158, 144),
            RGB565(250, 204, 21),
            RGB565(253, 224, 71),
            RGB565(24, 20, 6),
            RGB565(250, 204, 21),
            RGB565(42, 40, 44),
            false
        }
    },
    /* NL_DISPLAY_LIGHT */
    {
        /* BLUE */
        {
            RGB565(244, 246, 250),
            RGB565(255, 255, 255),
            RGB565(237, 241, 247),
            RGB565(227, 232, 240),
            RGB565(214, 222, 234),
            RGB565(203, 213, 225),
            RGB565(148, 163, 184),
            RGB565(15, 23, 42),
            RGB565(71, 85, 105),
            RGB565(2, 132, 199),
            RGB565(3, 105, 161),
            RGB565(255, 255, 255),
            RGB565(234, 179, 8),
            RGB565(226, 232, 240),
            true
        },
        /* RED */
        {
            RGB565(249, 245, 246),
            RGB565(255, 255, 255),
            RGB565(243, 237, 239),
            RGB565(236, 227, 230),
            RGB565(226, 213, 217),
            RGB565(214, 200, 205),
            RGB565(162, 148, 154),
            RGB565(24, 16, 20),
            RGB565(92, 74, 82),
            RGB565(220, 38, 38),
            RGB565(185, 28, 28),
            RGB565(255, 255, 255),
            RGB565(234, 179, 8),
            RGB565(234, 224, 227),
            true
        },
        /* GREEN */
        {
            RGB565(243, 248, 245),
            RGB565(255, 255, 255),
            RGB565(235, 243, 239),
            RGB565(224, 235, 229),
            RGB565(210, 226, 217),
            RGB565(196, 216, 205),
            RGB565(142, 168, 154),
            RGB565(15, 30, 22),
            RGB565(70, 96, 82),
            RGB565(22, 163, 74),
            RGB565(21, 128, 61),
            RGB565(255, 255, 255),
            RGB565(234, 179, 8),
            RGB565(224, 235, 229),
            true
        },
        /* YELLOW */
        {
            RGB565(249, 247, 242),
            RGB565(255, 255, 255),
            RGB565(243, 240, 232),
            RGB565(236, 231, 220),
            RGB565(226, 219, 205),
            RGB565(212, 204, 188),
            RGB565(162, 154, 138),
            RGB565(26, 22, 14),
            RGB565(94, 86, 68),
            RGB565(234, 179, 8),
            RGB565(161, 98, 7),
            RGB565(24, 20, 6),
            RGB565(234, 179, 8),
            RGB565(235, 229, 216),
            true
        }
    },
    /* NL_DISPLAY_GRAY (Mode Gris neutre graphite/ardoise) */
    {
        /* BLUE */
        {
            RGB565(64, 70, 80),
            RGB565(50, 55, 64),
            RGB565(54, 59, 68),
            RGB565(82, 89, 101),
            RGB565(98, 106, 120),
            RGB565(114, 123, 138),
            RGB565(138, 146, 160),
            RGB565(245, 247, 250),
            RGB565(200, 206, 216),
            RGB565(56, 189, 248),
            RGB565(125, 211, 252),
            RGB565(10, 16, 26),
            RGB565(250, 204, 21),
            RGB565(76, 82, 94),
            false
        },
        /* RED */
        {
            RGB565(66, 68, 76),
            RGB565(52, 53, 60),
            RGB565(56, 57, 64),
            RGB565(84, 86, 96),
            RGB565(100, 102, 114),
            RGB565(118, 120, 132),
            RGB565(142, 144, 156),
            RGB565(246, 245, 248),
            RGB565(204, 200, 210),
            RGB565(239, 68, 68),
            RGB565(252, 165, 165),
            RGB565(255, 255, 255),
            RGB565(250, 204, 21),
            RGB565(78, 80, 90),
            false
        },
        /* GREEN */
        {
            RGB565(62, 70, 74),
            RGB565(49, 56, 60),
            RGB565(53, 60, 64),
            RGB565(80, 90, 95),
            RGB565(96, 108, 114),
            RGB565(112, 126, 132),
            RGB565(136, 150, 156),
            RGB565(244, 248, 246),
            RGB565(198, 210, 205),
            RGB565(34, 197, 94),
            RGB565(134, 239, 172),
            RGB565(10, 24, 15),
            RGB565(250, 204, 21),
            RGB565(74, 84, 88),
            false
        },
        /* YELLOW */
        {
            RGB565(68, 70, 76),
            RGB565(54, 56, 61),
            RGB565(58, 60, 65),
            RGB565(86, 89, 96),
            RGB565(102, 105, 113),
            RGB565(120, 123, 132),
            RGB565(144, 147, 156),
            RGB565(248, 247, 242),
            RGB565(208, 205, 196),
            RGB565(250, 204, 21),
            RGB565(253, 224, 71),
            RGB565(24, 20, 6),
            RGB565(250, 204, 21),
            RGB565(80, 82, 90),
            false
        }
    }
};

static const char *const G_ASSIGNABLE_KEY_STR = "ABCDEFGHIJKLMNOPQRSTUVWXYZ13";

static int str_icmp_u(const char *a, const char *b) {
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

static bool str_icontains_u(const char *hay, const char *needle) {
    if (!hay || !needle) return false;
    if (needle[0] == '\0') return true;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p != '\0'; p++) {
        size_t i = 0U;
        while (i < nlen && p[i] != '\0' &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

const nl_theme_t *nl_get_theme(nl_display_mode_t mode, nl_accent_color_t accent) {
    unsigned m = ((unsigned)mode < (unsigned)NL_DISPLAY_COUNT) ? (unsigned)mode : (unsigned)NL_DISPLAY_DARK;
    unsigned a = ((unsigned)accent < (unsigned)NL_ACCENT_COUNT) ? (unsigned)accent : (unsigned)NL_ACCENT_BLUE;
    return &G_THEMES[m][a];
}

const char *nl_category_label(nl_category_t cat) {
    switch (cat) {
        case NL_CAT_ALL:       return "TOUS";
        case NL_CAT_FAVORITES: return "FAV";
        case NL_CAT_RECENT:    return "REC";
        default:               return "TOUS";
    }
}

const char *nl_layout_label(nl_layout_t layout) {
    switch (layout) {
        case NL_LAYOUT_1X1: return "1 x 1";
        case NL_LAYOUT_2X2: return "2 x 2";
        case NL_LAYOUT_3X3: return "3 x 3";
        case NL_LAYOUT_4X4: return "4 x 4";
        case NL_LAYOUT_3X2:
        default:            return "3 x 2";
    }
}

const char *nl_sort_order_label(nl_sort_order_t sort) {
    switch (sort) {
        case NL_SORT_ALPHA_DESC:    return "Ordre alphabetique (Z -> A)";
        case NL_SORT_RECENT_ADD:    return "Ajoutes recemment";
        case NL_SORT_OLDEST_ADD:    return "Ajoutes le plus tot";
        case NL_SORT_LAST_USED:     return "Joues recemment";
        case NL_SORT_MOST_LAUNCHED: return "Les plus lances";
        case NL_SORT_PLAY_TIME:     return "Temps de jeu cumule";
        case NL_SORT_ALPHA_ASC:
        default:                    return "Ordre alphabetique (A -> Z)";
    }
}

const char *nl_battery_level_label(nl_battery_level_t level) {
    switch (level) {
        case NL_BAT_HIGH:   return "Eleve";
        case NL_BAT_MEDIUM: return "Moyen";
        case NL_BAT_LOW:    return "Faible";
        case NL_BAT_UNAVAILABLE:
        default:            return "Indisponible";
    }
}


void nl_get_layout_metrics_for_count(nl_layout_t layout, bool show_names, bool show_shortcuts, int item_count, nl_layout_metrics_t *out) {
    if (!out) return;
    out->view_bottom  = show_shortcuts ? NL_VIEW_BOTTOM : NL_VIEW_BOTTOM_NO_SC;
    out->pagination_y = show_shortcuts ? NL_PAGINATION_Y : NL_PAGINATION_Y_NO_SC;

    switch (layout) {
        case NL_LAYOUT_1X1:
            out->cols = 1;
            out->rows = 1;
            if (show_shortcuts) {
                out->card_w  = show_names ? 152 : 164;
                out->card_h  = out->card_w;
                out->top_y   = 33;
                out->gap_y   = 12;
                out->radius  = show_names ? 17 : 18;
            } else {
                out->card_w  = show_names ? 166 : 178;
                out->card_h  = out->card_w;
                out->top_y   = 35;
                out->gap_y   = 14;
                out->radius  = show_names ? 18 : 19;
            }
            out->margin_x = (NL_SCREEN_W - out->card_w) / 2;
            out->gap_x    = 0;
            out->label_h  = show_names ? 12 : 0;
            break;

        case NL_LAYOUT_2X2:
            out->cols = 2;
            out->rows = 2;
            if (show_shortcuts) {
                out->card_w  = show_names ? 76 : 85;
                out->card_h  = out->card_w;
                out->gap_x   = 30;
                out->top_y   = 28;
                out->gap_y   = show_names ? 3 : 5;
                out->radius  = show_names ? 10 : 11;
            } else {
                out->card_w  = show_names ? 84 : 93;
                out->card_h  = out->card_w;
                out->gap_x   = 28;
                out->top_y   = 28;
                out->gap_y   = show_names ? 4 : 6;
                out->radius  = show_names ? 11 : 12;
            }
            out->margin_x = (NL_SCREEN_W - (2 * out->card_w + out->gap_x)) / 2;
            out->label_h  = show_names ? 10 : 0;
            break;

        case NL_LAYOUT_3X3:
            out->cols = 3;
            out->rows = 3;
            if (show_shortcuts) {
                out->card_w  = show_names ? 48 : 56;
                out->card_h  = out->card_w;
                out->gap_x   = show_names ? 24 : 22;
                out->top_y   = 28;
                out->gap_y   = show_names ? 2 : 4;
                out->radius  = show_names ? 7 : 8;
            } else {
                out->card_w  = show_names ? 53 : 61;
                out->card_h  = out->card_w;
                out->gap_x   = show_names ? 24 : 22;
                out->top_y   = 28;
                out->gap_y   = show_names ? 3 : 5;
                out->radius  = show_names ? 8 : 9;
            }
            out->margin_x = (NL_SCREEN_W - (3 * out->card_w + 2 * out->gap_x)) / 2;
            out->label_h  = show_names ? 9 : 0;
            break;

        case NL_LAYOUT_4X4:
            out->cols = 4;
            out->rows = 4;
            if (show_shortcuts) {
                out->card_w  = show_names ? 34 : 42;
                out->card_h  = out->card_w;
                out->gap_x   = show_names ? 24 : 20;
                out->top_y   = 28;
                out->gap_y   = 3;
                out->radius  = 6;
            } else {
                out->card_w  = show_names ? 39 : 46;
                out->card_h  = out->card_w;
                out->gap_x   = show_names ? 22 : 20;
                out->top_y   = 28;
                out->gap_y   = show_names ? 2 : 3;
                out->radius  = show_names ? 6 : 7;
            }
            out->margin_x = (NL_SCREEN_W - (4 * out->card_w + 3 * out->gap_x)) / 2;
            out->label_h  = show_names ? 8 : 0;
            break;

        case NL_LAYOUT_3X2:
        default:
            out->cols = NL_GRID_COLS;
            out->rows = NL_GRID_ROWS;
            if (show_shortcuts) {
                if (!show_names) {
                    out->card_w   = NL_GRID_CARD_W;   /* 84 */
                    out->card_h   = NL_GRID_CARD_H;   /* 84 */
                    out->radius   = NL_COVER_RADIUS;  /* 11 */
                    out->margin_x = NL_GRID_MARGIN_X; /* 18 */
                    out->gap_x    = NL_GRID_GAP_X;    /* 16 */
                    out->top_y    = NL_GRID_TOP_Y;    /* 29 */
                    out->gap_y    = NL_GRID_GAP_Y;    /* 6  */
                    out->label_h  = 0;
                } else {
                    out->card_w   = 76;
                    out->card_h   = 76;
                    out->radius   = 10;
                    out->gap_x    = 22;
                    out->margin_x = (NL_SCREEN_W - (3 * 76 + 2 * 22)) / 2; /* 24 */
                    out->top_y    = 29;
                    out->gap_y    = 3;
                    out->label_h  = 10;
                }
            } else {
                if (!show_names) {
                    out->card_w   = 90;
                    out->card_h   = 90;
                    out->radius   = 12;
                    out->gap_x    = 14;
                    out->margin_x = (NL_SCREEN_W - (3 * 90 + 2 * 14)) / 2; /* 11 */
                    out->top_y    = 29;
                    out->gap_y    = 8;
                    out->label_h  = 0;
                } else {
                    out->card_w   = 82;
                    out->card_h   = 82;
                    out->radius   = 11;
                    out->gap_x    = 18;
                    out->margin_x = (NL_SCREEN_W - (3 * 82 + 2 * 18)) / 2; /* 19 */
                    out->top_y    = 29;
                    out->gap_y    = 6;
                    out->label_h  = 10;
                }
            }
            break;
    }

    out->per_page = out->cols * out->rows;

    /* Adaptive single-page vertical & horizontal optimization when active rows < max rows */
    if (item_count > 0 && item_count <= out->per_page) {
        int vis_rows = (item_count + out->cols - 1) / out->cols;
        if (vis_rows >= 1 && vis_rows < out->rows) {
            int avail_h = out->view_bottom - NL_VIEW_TOP;
            if (layout == NL_LAYOUT_4X4) {
                if (vis_rows == 3) {
                    if (show_shortcuts) {
                        out->card_w = show_names ? 49 : 56;
                        out->gap_x  = show_names ? 18 : 14;
                        out->gap_y  = show_names ? 3 : 4;
                        out->radius = show_names ? 7 : 8;
                    } else {
                        out->card_w = show_names ? 53 : 60;
                        out->gap_x  = show_names ? 16 : 14;
                        out->gap_y  = show_names ? 4 : 6;
                        out->radius = show_names ? 8 : 9;
                    }
                } else if (vis_rows == 2) {
                    if (show_shortcuts) {
                        out->card_w = show_names ? 60 : 64;
                        out->gap_x  = show_names ? 14 : 12;
                        out->gap_y  = show_names ? 10 : 14;
                        out->radius = show_names ? 8 : 9;
                    } else {
                        out->card_w = show_names ? 62 : 66;
                        out->gap_x  = show_names ? 14 : 12;
                        out->gap_y  = show_names ? 12 : 16;
                        out->radius = 9;
                    }
                } else {
                    out->card_w = show_names ? 64 : 68;
                    out->gap_x  = show_names ? 12 : 10;
                    out->gap_y  = 8;
                    out->radius = show_names ? 9 : 10;
                }
                out->card_h   = out->card_w;
                out->margin_x = (NL_SCREEN_W - (4 * out->card_w + 3 * out->gap_x)) / 2;
            } else if (layout == NL_LAYOUT_3X3) {
                if (vis_rows == 2) {
                    if (show_shortcuts) {
                        out->card_w = show_names ? 74 : 80;
                        out->gap_x  = show_names ? 20 : 18;
                        out->gap_y  = show_names ? 6 : 8;
                        out->radius = show_names ? 10 : 11;
                    } else {
                        out->card_w = show_names ? 80 : 86;
                        out->gap_x  = show_names ? 18 : 16;
                        out->gap_y  = show_names ? 8 : 10;
                        out->radius = 11;
                    }
                } else {
                    if (show_shortcuts) {
                        out->card_w = show_names ? 84 : 88;
                        out->gap_x  = show_names ? 16 : 14;
                        out->gap_y  = 8;
                        out->radius = show_names ? 11 : 12;
                    } else {
                        out->card_w = show_names ? 88 : 92;
                        out->gap_x  = show_names ? 14 : 12;
                        out->gap_y  = 8;
                        out->radius = 12;
                    }
                }
                out->card_h   = out->card_w;
                out->margin_x = (NL_SCREEN_W - (3 * out->card_w + 2 * out->gap_x)) / 2;
            } else if (layout == NL_LAYOUT_3X2) {
                /* vis_rows == 1 (1..3 games) */
                if (show_shortcuts) {
                    out->card_w = show_names ? 84 : 88;
                    out->gap_x  = show_names ? 16 : 14;
                    out->gap_y  = 8;
                    out->radius = show_names ? 11 : 12;
                } else {
                    out->card_w = show_names ? 88 : 92;
                    out->gap_x  = show_names ? 14 : 12;
                    out->gap_y  = 8;
                    out->radius = 12;
                }
                out->card_h   = out->card_w;
                out->margin_x = (NL_SCREEN_W - (3 * out->card_w + 2 * out->gap_x)) / 2;
            } else if (layout == NL_LAYOUT_2X2) {
                /* vis_rows == 1 (1..2 games) */
                if (show_shortcuts) {
                    out->card_w = show_names ? 108 : 116;
                    out->gap_x  = show_names ? 26 : 24;
                    out->gap_y  = 8;
                    out->radius = show_names ? 13 : 14;
                } else {
                    out->card_w = show_names ? 116 : 124;
                    out->gap_x  = show_names ? 26 : 24;
                    out->gap_y  = 8;
                    out->radius = show_names ? 14 : 15;
                }
                out->card_h   = out->card_w;
                out->margin_x = (NL_SCREEN_W - (2 * out->card_w + out->gap_x)) / 2;
            }
            int content_h = vis_rows * (out->card_h + out->label_h) + (vis_rows - 1) * out->gap_y;
            int centered_top = NL_VIEW_TOP + (avail_h - content_h) / 2;
            if (centered_top < NL_VIEW_TOP + 2) centered_top = NL_VIEW_TOP + 2;
            out->top_y = centered_top;
        }
    }

    out->row_stride_y = out->card_h + out->label_h + out->gap_y;
    out->page_stride_y = out->row_stride_y * out->rows;
}

void nl_get_layout_metrics_ex(nl_layout_t layout, bool show_names, bool show_shortcuts, nl_layout_metrics_t *out) {
    nl_get_layout_metrics_for_count(layout, show_names, show_shortcuts, 0, out);
}

void nl_get_layout_metrics(nl_layout_t layout, bool show_names, nl_layout_metrics_t *out) {
    nl_get_layout_metrics_for_count(layout, show_names, true, 0, out);
}

void nl_compute_grid_slot_pos(const nl_layout_metrics_t *m, int filtered_count, int f_idx, int *out_x, int *out_y) {
    if (!m || !out_x || !out_y) return;
    int cols = (m->cols > 0) ? m->cols : 1;
    int per_page = (m->per_page > 0) ? m->per_page : cols;
    int page = (f_idx >= 0) ? (f_idx / per_page) : 0;
    int slot = (f_idx >= 0) ? (f_idx % per_page) : 0;
    int row_in_page = slot / cols;
    int col = slot % cols;

    int global_row = (f_idx >= 0) ? (f_idx / cols) : 0;
    int row_start = global_row * cols;
    int items_in_row = filtered_count - row_start;
    if (items_in_row > cols) items_in_row = cols;
    if (items_in_row < 1) items_in_row = 1;

    if (items_in_row < cols) {
        int row_w = items_in_row * m->card_w + (items_in_row - 1) * m->gap_x;
        int row_margin_x = (NL_SCREEN_W - row_w) / 2;
        *out_x = row_margin_x + col * (m->card_w + m->gap_x);
    } else {
        *out_x = m->margin_x + col * (m->card_w + m->gap_x);
    }

    int page_top_y = m->top_y;
    if (filtered_count > per_page) {
        int page_items = filtered_count - page * per_page;
        if (page_items > per_page) page_items = per_page;
        if (page_items < 1) page_items = 1;
        int rows_on_page = (page_items + cols - 1) / cols;
        if (rows_on_page < m->rows) {
            int avail_h = m->view_bottom - NL_VIEW_TOP;
            int content_h = rows_on_page * (m->card_h + m->label_h) + (rows_on_page - 1) * m->gap_y;
            int centered_y = NL_VIEW_TOP + (avail_h - content_h) / 2;
            if (centered_y > page_top_y) {
                page_top_y = centered_y;
            }
        }
    }

    *out_y = page_top_y + page * m->page_stride_y + row_in_page * m->row_stride_y;
}

void nl_format_duration(uint32_t seconds, char *out_buf, size_t max_len) {
    if (!out_buf || max_len == 0U) return;
    if (seconds == 0U) {
        snprintf(out_buf, max_len, "0s");
    } else if (seconds < 60U) {
        snprintf(out_buf, max_len, "%lus", (unsigned long)seconds);
    } else if (seconds < 3600U) {
        uint32_t m = seconds / 60U;
        uint32_t s = seconds % 60U;
        snprintf(out_buf, max_len, "%lum %02lus", (unsigned long)m, (unsigned long)s);
    } else {
        uint32_t h = seconds / 3600U;
        uint32_t m = (seconds % 3600U) / 60U;
        snprintf(out_buf, max_len, "%luh %02lum", (unsigned long)h, (unsigned long)m);
    }
}

void nl_format_timestamp(uint32_t epoch_sec, char *out_buf, size_t max_len) {
    if (!out_buf || max_len == 0U) return;
    if (epoch_sec < 946684800U) {
        snprintf(out_buf, max_len, "--");
        return;
    }
    uint32_t sod = epoch_sec % 86400U;
    uint32_t hh = sod / 3600U;
    uint32_t mm = (sod % 3600U) / 60U;

    /* Civil date from Unix days (Howard Hinnant integer algorithm) */
    int32_t z = (int32_t)(epoch_sec / 86400U) + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int32_t y = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    uint32_t mp = (5U * doy + 2U) / 153U;
    uint32_t d = doy - (153U * mp + 2U) / 5U + 1U;
    uint32_t m = mp + (mp < 10U ? 3U : (uint32_t)(-9));
    y += (m <= 2U) ? 1 : 0;

    snprintf(out_buf, max_len, "%02u/%02u/%02d %02u:%02u",
             (unsigned)d, (unsigned)m, (int)(y % 100),
             (unsigned)hh, (unsigned)mm);
}

bool nl_shortcut_is_reserved(char ch) {
    char u = (char)toupper((unsigned char)ch);
    switch (u) {
        case '0':
        case '2':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '.':
        case '-':
        case '=':
        case '*':
        case '+':
        case '\x1B':
        case '\n':
        case '\r':
        case '\t':
            return true;
        default:
            return false;
    }
}

bool nl_shortcut_is_assignable(char ch) {
    char u = (char)toupper((unsigned char)ch);
    if (nl_shortcut_is_reserved(u)) return false;
    for (const char *p = G_ASSIGNABLE_KEY_STR; *p != '\0'; p++) {
        if (*p == u) return true;
    }
    return false;
}

int nl_shortcut_find_owner(const nl_state_t *state, char ch, int exclude_game_idx) {
    if (!state || ch == 0) return -1;
    char u = (char)toupper((unsigned char)ch);
    for (int i = 0; i < state->total_games; i++) {
        if (i == exclude_game_idx) continue;
        if ((char)toupper((unsigned char)state->games[i].shortcut_key) == u) {
            return i;
        }
    }
    return -1;
}

int nl_get_assignable_keys(const char **out_chars) {
    if (out_chars) *out_chars = G_ASSIGNABLE_KEY_STR;
    return (int)strlen(G_ASSIGNABLE_KEY_STR);
}

int nl_get_shortcut_defs(const nl_state_t *state, nl_shortcut_def_t *out_defs, int max_defs) {
    static const struct {
        const char *k;
        const char *act;
    } k_base[] = {
        { "FLECHES",   "Naviguer / Deplacer" },
        { "ENTER / 5", "Lancer le jeu selectionne" },
        { "ESC",       "Retour / Quitter" },
        { "TAB",       "Categorie TOUS/FAV/REC" },
        { ".",         "Selecteur rapide profil" },
        { "0 / -",     "Profil prec. / suiv." },
        { "A-Z / 1-9", "Recherche directe accueil" },
        { "VAR / *",   "Ajouter/Retirer favori" },
        { "TRIG / =",  "Page prec. / suiv." },
        { "DEL",       "Actualiser bibliotheque" },
        { "DOC",       "Mode Sombre/Clair/Gris" },
        { "MENU",      "Changer couleur accent" },
        { "SAVE",      "Ouvrir Parametres" },
        { "DEL (Param.)", "Eteindre la calculatrice" }
    };

    if (!out_defs || max_defs <= 0) return 0;
    int count = 0;
    int n_base = (int)(sizeof(k_base) / sizeof(k_base[0]));
    for (int i = 0; i < n_base && count < max_defs; i++) {
        snprintf(out_defs[count].key_label, sizeof(out_defs[count].key_label), "%s", k_base[i].k);
        snprintf(out_defs[count].action_label, sizeof(out_defs[count].action_label), "%s", k_base[i].act);
        count++;
    }

    if (state) {
        if (count < max_defs) {
            snprintf(out_defs[count].key_label, sizeof(out_defs[count].key_label), "TACTILE");
            snprintf(out_defs[count].action_label, sizeof(out_defs[count].action_label),
                     state->settings.touchpad_nav ? "Clic: Lancer / Glisser: Nav" : "Clic centre: Lancer jeu");
            count++;
        }
        if (count < max_defs) {
            snprintf(out_defs[count].key_label, sizeof(out_defs[count].key_label), "CTRL+MENU");
            snprintf(out_defs[count].action_label, sizeof(out_defs[count].action_label),
                     state->settings.global_hotkey ? "Ouvrir depuis TI-OS (ON)" : "Ouvrir depuis TI-OS (OFF)");
            count++;
        }
        for (int i = 0; i < state->total_games && count < max_defs; i++) {
            char sc = state->games[i].shortcut_key;
            if (nl_shortcut_is_assignable(sc)) {
                snprintf(out_defs[count].key_label, sizeof(out_defs[count].key_label), "TOUCHE %c", sc);
                snprintf(out_defs[count].action_label, sizeof(out_defs[count].action_label),
                         "Lancer %.*s", 19, state->games[i].title);
                count++;
            }
        }
    }
    return count;
}

void nl_touchpad_tracker_reset(nl_touchpad_tracker_t *tracker) {
    if (!tracker) return;
    memset(tracker, 0, sizeof(*tracker));
}

int nl_touchpad_threshold_for_level(int sensitivity_level) {
    static const int k_thresholds[NL_TP_SENS_COUNT] = { 300, 255, 215, 175, 138 };
    if (sensitivity_level < 0) sensitivity_level = 0;
    if (sensitivity_level >= NL_TP_SENS_COUNT) sensitivity_level = NL_TP_SENS_COUNT - 1;
    return k_thresholds[sensitivity_level];
}

nl_key_t nl_touchpad_process_sample_ex(
    nl_touchpad_tracker_t *tracker,
    bool touchpad_nav_enabled,
    bool contact,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h,
    int sensitivity_level
) {
    if (!tracker) return NL_KEY_NONE;

    if (!touchpad_nav_enabled || !contact) {
        tracker->in_contact = false;
        tracker->acc_x = 0;
        tracker->acc_y = 0;
        if (tracker->cooldown_ticks > 0) {
            tracker->cooldown_ticks--;
        }
        return NL_KEY_NONE;
    }

    /* First frame of finger contact: anchor position without emitting movement */
    if (!tracker->in_contact) {
        tracker->in_contact = true;
        tracker->last_x = raw_x;
        tracker->last_y = raw_y;
        tracker->acc_x = 0;
        tracker->acc_y = 0;
        return NL_KEY_NONE;
    }

    int32_t w = (pad_w >= 200U) ? (int32_t)pad_w : 2328;
    int32_t h = (pad_h >= 200U) ? (int32_t)pad_h : 1691;
    const int32_t thresh = (int32_t)nl_touchpad_threshold_for_level(sensitivity_level);

    /* Normalize delta to 1024 units across pad width/height.
     * Note: On TI-Nspire hardware, raw_y=0 is bottom and raw_y=height is top,
     * so (last_y - raw_y) > 0 corresponds to downward movement (+Y on screen). */
    int32_t dx_norm = ((int32_t)raw_x - (int32_t)tracker->last_x) * 1024 / w;
    int32_t dy_norm = ((int32_t)tracker->last_y - (int32_t)raw_y) * 1024 / h;

    tracker->last_x = raw_x;
    tracker->last_y = raw_y;

    /* Reject physical finger-reposition jumps (> 45% of touchpad span in 16 ms) */
    if (abs(dx_norm) > 460 || abs(dy_norm) > 460) {
        tracker->acc_x = 0;
        tracker->acc_y = 0;
        return NL_KEY_NONE;
    }

    /* Deadzone filter against stationary finger micro-jitter */
    if (abs(dx_norm) < 4 && abs(dy_norm) < 4) {
        tracker->acc_x = (tracker->acc_x * 7) / 8;
        tracker->acc_y = (tracker->acc_y * 7) / 8;
    } else {
        /* Instant accumulator reset on intentional direction reversal (zero reversal lag) */
        if ((dx_norm > 8 && tracker->acc_x < 0) || (dx_norm < -8 && tracker->acc_x > 0)) {
            tracker->acc_x = 0;
        }
        if ((dy_norm > 8 && tracker->acc_y < 0) || (dy_norm < -8 && tracker->acc_y > 0)) {
            tracker->acc_y = 0;
        }
        tracker->acc_x += dx_norm;
        tracker->acc_y += dy_norm;
    }

    /* Controlled repeat cadence: prevent accidental multi-game jumps */
    if (tracker->cooldown_ticks > 0) {
        tracker->cooldown_ticks--;
        if (tracker->acc_x > thresh) tracker->acc_x = thresh;
        if (tracker->acc_x < -thresh) tracker->acc_x = -thresh;
        if (tracker->acc_y > thresh) tracker->acc_y = thresh;
        if (tracker->acc_y < -thresh) tracker->acc_y = -thresh;
        return NL_KEY_NONE;
    }

    int32_t ax = abs(tracker->acc_x);
    int32_t ay = abs(tracker->acc_y);

    if (ax >= thresh || ay >= thresh) {
        tracker->cooldown_ticks = 4;
        if (ax >= ay) {
            nl_key_t out = (tracker->acc_x > 0) ? NL_KEY_RIGHT : NL_KEY_LEFT;
            tracker->acc_x -= (tracker->acc_x > 0) ? thresh : -thresh;
            if (tracker->acc_x > 90) tracker->acc_x = 90;
            if (tracker->acc_x < -90) tracker->acc_x = -90;
            tracker->acc_y /= 2;
            return out;
        } else {
            nl_key_t out = (tracker->acc_y > 0) ? NL_KEY_DOWN : NL_KEY_UP;
            tracker->acc_y -= (tracker->acc_y > 0) ? thresh : -thresh;
            if (tracker->acc_y > 90) tracker->acc_y = 90;
            if (tracker->acc_y < -90) tracker->acc_y = -90;
            tracker->acc_x /= 2;
            return out;
        }
    }

    return NL_KEY_NONE;
}

nl_key_t nl_touchpad_process_sample(
    nl_touchpad_tracker_t *tracker,
    bool touchpad_nav_enabled,
    bool contact,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h
) {
    return nl_touchpad_process_sample_ex(
        tracker, touchpad_nav_enabled, contact, raw_x, raw_y, pad_w, pad_h, NL_TP_SENS_DEFAULT
    );
}

void nl_tp_click_reset(nl_tp_click_guard_t *guard) {
    if (!guard) return;
    memset(guard, 0, sizeof(*guard));
}

bool nl_tp_click_poll(
    nl_tp_click_guard_t *guard,
    bool contact,
    bool pressed,
    bool center_click,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h
) {
    if (!guard) return false;

    if (guard->cooldown_ticks > 0) {
        guard->cooldown_ticks--;
    }

    if (guard->wait_release) {
        if (!pressed && !contact) {
            guard->release_streak++;
            if (guard->release_streak >= NL_TP_CLICK_RELEASE_TICKS) {
                guard->wait_release = false;
                guard->release_streak = 0;
            }
        } else {
            guard->release_streak = 0;
        }
        return false;
    }

    if (contact || pressed) {
        if (!guard->in_contact) {
            guard->in_contact = true;
            guard->down_x = raw_x;
            guard->down_y = raw_y;
            guard->moved_since_down = false;
        } else if (contact && !pressed) {
            int32_t w = (pad_w >= 200U) ? (int32_t)pad_w : 2328;
            int32_t h = (pad_h >= 200U) ? (int32_t)pad_h : 1691;
            int32_t dx_norm = ((int32_t)raw_x - (int32_t)guard->down_x) * 1024 / w;
            int32_t dy_norm = ((int32_t)raw_y - (int32_t)guard->down_y) * 1024 / h;
            if (abs(dx_norm) > NL_TP_CLICK_SLIDE_CANCEL || abs(dy_norm) > NL_TP_CLICK_SLIDE_CANCEL) {
                guard->moved_since_down = true;
            }
        }
    } else {
        guard->in_contact = false;
        guard->moved_since_down = false;
    }

    if (pressed && center_click && !guard->moved_since_down && guard->cooldown_ticks == 0) {
        guard->wait_release = true;
        guard->release_streak = 0;
        guard->cooldown_ticks = NL_TP_CLICK_COOLDOWN_TICKS;
        guard->in_contact = false;
        guard->moved_since_down = false;
        return true;
    }

    return false;
}

void nl_save_shortcut_reset(nl_save_shortcut_t *sc) {
    if (!sc) return;
    memset(sc, 0, sizeof(*sc));
}

void nl_save_shortcut_clear_latch(nl_save_shortcut_t *sc) {
    if (!sc) return;
    sc->ctrl_was_down = false;
    sc->ctrl_latch_ticks = 0;
}

bool nl_save_shortcut_poll(
    nl_save_shortcut_t *sc,
    bool direct_key_down,
    bool ctrl_down,
    bool s_down,
    bool other_key_down
) {
    if (!sc) return false;

    if (sc->cooldown_ticks > 0) {
        sc->cooldown_ticks--;
    }

    if (sc->wait_full_release) {
        if (!direct_key_down && !ctrl_down && !s_down) {
            sc->release_streak++;
            if (sc->release_streak >= NL_SETTINGS_RELEASE_TICKS) {
                sc->wait_full_release = false;
                sc->release_streak = 0;
            }
        } else {
            sc->release_streak = 0;
        }
        return false;
    }

    if (direct_key_down && sc->cooldown_ticks == 0) {
        sc->ctrl_was_down = false;
        sc->ctrl_latch_ticks = 0;
        sc->wait_full_release = true;
        sc->release_streak = 0;
        sc->cooldown_ticks = NL_SETTINGS_COOLDOWN_TICKS;
        return true;
    }

    if (other_key_down) {
        sc->ctrl_was_down = ctrl_down;
        sc->ctrl_latch_ticks = 0;
        return false;
    }

    if (ctrl_down) {
        sc->ctrl_was_down = true;
        sc->ctrl_latch_ticks = NL_CTRL_LATCH_TICKS;
    } else {
        if (sc->ctrl_was_down) {
            sc->ctrl_was_down = false;
            sc->ctrl_latch_ticks = NL_CTRL_LATCH_TICKS;
        } else if (sc->ctrl_latch_ticks > 0) {
            sc->ctrl_latch_ticks--;
        }
    }

    if (s_down && (ctrl_down || sc->ctrl_latch_ticks > 0) && sc->cooldown_ticks == 0) {
        sc->ctrl_was_down = false;
        sc->ctrl_latch_ticks = 0;
        sc->wait_full_release = true;
        sc->release_streak = 0;
        sc->cooldown_ticks = NL_SETTINGS_COOLDOWN_TICKS;
        return true;
    }

    return false;
}

void nl_init_state(nl_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->active_category = NL_CAT_ALL;
    state->display_mode = NL_DISPLAY_DARK;
    state->accent_color = NL_ACCENT_BLUE;
    state->settings.autostart = false;
    state->settings.global_hotkey = false;
    state->settings.layout = NL_LAYOUT_3X2;
    state->settings.show_names = false;
    state->settings.show_shortcuts = true;
    state->settings.show_battery = false;
    state->settings.touchpad_mode = NL_TP_MODE_TOUCHPAD;
    state->settings.touchpad_nav = false;
    state->settings.touchpad_sensitivity = NL_TP_SENS_DEFAULT;
    state->settings.cursor_speed = NL_CURSOR_SPEED_DEFAULT;
    state->settings.touchpad_cursor_show = false;
    state->settings.touchpad_cursor_style = NL_CURSOR_STYLE_ARROW;
    state->settings.sort_order = NL_SORT_ALPHA_ASC;
    state->sui.screen = NL_UI_LIBRARY;
    state->sui.tp_cursor = 0;
    state->sui.tp_tab = 0;
    state->sui.tp_style_cursor = 0;
    state->sui.ctrl_mouse_active = false;
    state->sui.hover_kind = NL_HOVER_NONE;
    state->sui.hover_index = 0;
    state->sui.hover_sub = 0;
    state->sui.hover_valid = false;
    state->sui.cap_last_game_idx = -1;
    state->launch_game_idx = -1;
    state->total_pages = 1;
    state->anim.splash_max = 20;
    state->anim.splash_tick = 20;
    state->anim.launch_max_ticks = 30;
    state->anim.launch_shrink_fp = NL_SCALE_SELECTED;
    state->anim.ink_max = NL_INK_TRANS_TICKS;
    state->anim.switch_hotkey_fp = 0;
    state->anim.switch_shortcuts_fp = 256;
    state->anim.switch_battery_fp = 0;
    state->anim.switch_touchpad_fp = 0;
    state->anim.switch_tp_mode_fp = 0;
    state->anim.tp_slider_pos_fp = NL_TP_SENS_DEFAULT * 256;
    state->anim.tp_slider_vel_fp = 0;
    state->anim.tp_detent_pulse_fp = 0;
    state->anim.tp_notch_last = NL_TP_SENS_DEFAULT;
    state->anim.tp_speed_slider_pos_fp = NL_CURSOR_SPEED_DEFAULT * 256;
    state->anim.tp_speed_slider_vel_fp = 0;
    state->anim.tp_speed_detent_pulse_fp = 0;
    state->anim.tp_speed_notch_last = NL_CURSOR_SPEED_DEFAULT;
    state->anim.set_open_fp = 0;
    state->anim.set_last_screen = NL_UI_SETTINGS_MAIN;
    state->anim.set_prev_screen = NL_UI_LIBRARY;
    state->anim.set_sec_fp = 256;
    state->anim.set_sec_dir = 1;
    state->anim.set_sel_y_fp = 32 << 4;
    state->anim.set_target_sel_y_fp = 32 << 4;
    state->anim.set_sel_glow_fp = 256;
    state->anim.set_prev_cursor = -1;
    state->anim.set_list_scroll_fp = 0;
    state->anim.set_target_scroll_fp = 0;
    state->anim.modal_fp = 0;
    state->anim.modal_btn_fp = 0;
    state->anim.layout_from = NL_LAYOUT_3X2;
    state->anim.layout_trans_fp = 256;
    state->anim.val_pulse_fp = 0;
    state->anim.val_pulse_dir = 0;
    state->anim.search_bar_fp = 0;
    state->anim.cat_ind_x_fp = 34 << 4;
    state->anim.cat_ind_w_fp = 36 << 4;
    state->anim.cat_trans_fp = 256;
    state->anim.cat_trans_dir = 1;
    state->anim.cat_prev = NL_CAT_ALL;
    state->anim.prof_cat_trans_fp = 256;
    state->anim.prof_cat_dir = 1;
    state->anim.qp_open_fp = 0;
    state->anim.switch_tp_cursor_fp = 0;
    state->anim.tp_tab_trans_fp = 256;
    state->anim.tp_style_glow_fp = 256;
    state->anim.cur_initialized = false;
    state->anim.mouse_x_fp = 160 << 4;
    state->anim.mouse_y_fp = 116 << 4;
    state->anim.mouse_vx_fp = 0;
    state->anim.mouse_vy_fp = 0;
    state->anim.mouse_last_contact = false;
    state->anim.cur_x_fp = 160 << 4;
    state->anim.cur_y_fp = 116 << 4;
    state->anim.cur_target_x_fp = 160 << 4;
    state->anim.cur_target_y_fp = 116 << 4;
    state->anim.cur_opacity_fp = 0;
    state->anim.toast_max_ticks = 75;
    snprintf(state->sys.os_version, sizeof(state->sys.os_version), "TI-Nspire OS");
    for (int i = 0; i < NL_MAX_GAMES; i++) {
        state->anim.card_scale_fp[i] = NL_SCALE_NORMAL;
    }
    nl_profiles_init_default(state);
}

void nl_sync_active_cat_pill_targets(nl_state_t *state, bool snap) {
    if (!state) return;
    int tgt_cx = (state->active_category == NL_CAT_ALL) ? (34 << 4)
               : (state->active_category == NL_CAT_FAVORITES) ? (76 << 4)
               : (112 << 4);
    int tgt_cw = (state->active_category == NL_CAT_ALL) ? (36 << 4) : (30 << 4);
    if (snap) {
        state->anim.cat_ind_x_fp = tgt_cx;
        state->anim.cat_ind_w_fp = tgt_cw;
    }
}

void nl_show_toast(nl_state_t *state, const char *msg, int duration_ticks) {
    if (!state || !msg) return;
    size_t i = 0U;
    while (i + 1U < sizeof(state->anim.toast_text) && msg[i] != '\0') {
        state->anim.toast_text[i] = msg[i];
        i++;
    }
    state->anim.toast_text[i] = '\0';
    state->anim.toast_ticks = duration_ticks > 0 ? duration_ticks : 75;
    state->anim.toast_max_ticks = state->anim.toast_ticks;
}

static void sync_scroll_targets(nl_state_t *state, bool snap) {
    if (!state) return;
    nl_layout_metrics_t m;
    nl_get_layout_metrics_for_count(state->settings.layout, state->settings.show_names, state->settings.show_shortcuts, state->filtered_count, &m);

    state->anim.target_scroll_y_fp = (state->current_page * m.page_stride_y) << 4;
    state->anim.target_dot_fp = state->current_page << 4;

    if (!state->anim.initialized || snap) {
        state->anim.scroll_y_fp = state->anim.target_scroll_y_fp;
        state->anim.dot_pos_fp = state->anim.target_dot_fp;
        if (state->filtered_count > 0 && state->selected_pos < state->filtered_count) {
            int sel_game = state->filtered_indices[state->selected_pos];
            state->anim.card_scale_fp[sel_game] = NL_SCALE_SELECTED;
        }
        state->anim.initialized = true;
    }
}

static int compare_filtered_pair(const nl_state_t *state, int idx_a, int idx_b) {
    const nl_game_t *a = &state->games[idx_a];
    const nl_game_t *b = &state->games[idx_b];

    if (state->active_category == NL_CAT_RECENT) {
        if (b->last_played_rank != a->last_played_rank) {
            return (b->last_played_rank > a->last_played_rank) ? 1 : -1;
        }
        return str_icmp_u(a->title, b->title);
    }

    switch (state->settings.sort_order) {
        case NL_SORT_ALPHA_DESC:
            return str_icmp_u(b->title, a->title);

        case NL_SORT_RECENT_ADD:
            if (b->added_order != a->added_order) {
                return (b->added_order > a->added_order) ? 1 : -1;
            }
            return str_icmp_u(a->title, b->title);

        case NL_SORT_OLDEST_ADD:
            if (a->added_order != b->added_order) {
                return (a->added_order > b->added_order) ? 1 : -1;
            }
            return str_icmp_u(a->title, b->title);

        case NL_SORT_LAST_USED:
            if (b->last_played_rank != a->last_played_rank) {
                return (b->last_played_rank > a->last_played_rank) ? 1 : -1;
            }
            if (b->last_played_ts != a->last_played_ts) {
                return (b->last_played_ts > a->last_played_ts) ? 1 : -1;
            }
            return str_icmp_u(a->title, b->title);

        case NL_SORT_MOST_LAUNCHED:
            if (b->launch_count != a->launch_count) {
                return (b->launch_count > a->launch_count) ? 1 : -1;
            }
            if (b->last_played_rank != a->last_played_rank) {
                return (b->last_played_rank > a->last_played_rank) ? 1 : -1;
            }
            return str_icmp_u(a->title, b->title);

        case NL_SORT_PLAY_TIME:
            if (b->play_time_sec != a->play_time_sec) {
                return (b->play_time_sec > a->play_time_sec) ? 1 : -1;
            }
            if (b->launch_count != a->launch_count) {
                return (b->launch_count > a->launch_count) ? 1 : -1;
            }
            return str_icmp_u(a->title, b->title);

        case NL_SORT_ALPHA_ASC:
        default:
            return str_icmp_u(a->title, b->title);
    }
}

void nl_rebuild_filter(nl_state_t *state) {
    if (!state) return;

    int old_slot[NL_MAX_GAMES];
    bool track_return_glide = (state->launch_game_idx >= 0 &&
                               state->launch_game_idx < state->total_games &&
                               state->filtered_count > 0);
    for (int i = 0; i < NL_MAX_GAMES; i++) {
        old_slot[i] = -1;
    }
    if (track_return_glide) {
        for (int f = 0; f < state->filtered_count; f++) {
            int g_idx = state->filtered_indices[f];
            if (g_idx >= 0 && g_idx < NL_MAX_GAMES) {
                old_slot[g_idx] = f;
            }
        }
    }

    state->filtered_count = 0;

    for (int i = 0; i < state->total_games; i++) {
        const nl_game_t *g = &state->games[i];
        bool match = false;
        switch (state->active_category) {
            case NL_CAT_ALL:
                match = true;
                break;
            case NL_CAT_FAVORITES:
                match = g->is_favorite;
                break;
            case NL_CAT_RECENT:
                match = (g->last_played_rank > 0U);
                break;
            default:
                match = true;
                break;
        }
        if (match && state->sui.search_active && state->sui.search_len > 0) {
            if (!str_icontains_u(g->title, state->sui.search_query) &&
                !str_icontains_u(g->filename, state->sui.search_query)) {
                match = false;
            }
        }
        if (match) {
            state->filtered_indices[state->filtered_count++] = i;
        }
    }

    if (state->filtered_count > 1) {
        for (int a = 0; a < state->filtered_count - 1; a++) {
            for (int b = a + 1; b < state->filtered_count; b++) {
                if (compare_filtered_pair(state, state->filtered_indices[a], state->filtered_indices[b]) > 0) {
                    int tmp = state->filtered_indices[a];
                    state->filtered_indices[a] = state->filtered_indices[b];
                    state->filtered_indices[b] = tmp;
                }
            }
        }
    }

    nl_layout_metrics_t m;
    nl_get_layout_metrics_for_count(state->settings.layout, state->settings.show_names, state->settings.show_shortcuts, state->filtered_count, &m);

    for (int i = 0; i < NL_MAX_GAMES; i++) {
        state->anim.card_dx_fp[i] = 0;
        state->anim.card_dy_fp[i] = 0;
    }

    if (track_return_glide && state->filtered_count > 0) {
        for (int f = 0; f < state->filtered_count; f++) {
            int g_idx = state->filtered_indices[f];
            if (g_idx == state->launch_game_idx) {
                state->selected_pos = f;
            }
            if (state->anim.initialized && g_idx >= 0 && g_idx < NL_MAX_GAMES) {
                int old_f = old_slot[g_idx];
                if (old_f >= 0 && old_f != f) {
                    int old_x = 0, old_y = 0, new_x = 0, new_y = 0;
                    nl_compute_grid_slot_pos(&m, state->filtered_count, old_f, &old_x, &old_y);
                    nl_compute_grid_slot_pos(&m, state->filtered_count, f, &new_x, &new_y);
                    int dx_px = old_x - new_x;
                    int dy_px = old_y - new_y;
                    state->anim.card_dx_fp[g_idx] = dx_px << 4;
                    state->anim.card_dy_fp[g_idx] = dy_px << 4;
                }
            }
        }
    }

    if (state->filtered_count <= 0) {
        state->selected_pos = 0;
        state->current_page = 0;
        state->total_pages = 1;
    } else {
        if (state->selected_pos >= state->filtered_count) {
            state->selected_pos = state->filtered_count - 1;
        }
        if (state->selected_pos < 0) {
            state->selected_pos = 0;
        }
        state->total_pages = (state->filtered_count + m.per_page - 1) / m.per_page;
        state->current_page = state->selected_pos / m.per_page;
    }

    if (state->sui.screen == NL_UI_LIBRARY) {
        state->anim.switch_shownames_fp = state->settings.show_names ? 256 : 0;
        state->anim.layout_from = state->settings.layout;
        state->anim.layout_trans_fp = 256;
    }

    sync_scroll_targets(state, !state->anim.initialized);
}

static void settings_sync_cursor_targets(nl_state_t *state, bool snap, int prev_cursor) {
    if (!state) return;
    const nl_settings_ui_t *sui = &state->sui;
    int row_y0 = 32;
    int stride = 30;
    int cur = 0;
    int scr = 0;

    switch (sui->screen) {
        case NL_UI_SETTINGS_MAIN:
            row_y0 = 32;
            stride = 30;
            cur = sui->menu_cursor;
            scr = sui->menu_scroll;
            break;
        case NL_UI_SETTINGS_DISPLAY:
            row_y0 = 30;
            stride = 31;
            cur = sui->display_cursor;
            scr = sui->display_scroll;
            break;
        case NL_UI_SETTINGS_TOUCHPAD:
            row_y0 = 47;
            stride = 38;
            cur = sui->tp_cursor;
            scr = 0;
            break;
        case NL_UI_SETTINGS_SHORTCUTS:
            row_y0 = 32;
            stride = 30;
            cur = sui->sc_game_cursor;
            scr = sui->sc_game_scroll;
            break;
        case NL_UI_SETTINGS_KEY_HELP:
            row_y0 = 33;
            stride = 26;
            cur = sui->sub_scroll;
            scr = sui->sub_scroll;
            break;
        case NL_UI_SETTINGS_SORT:
            row_y0 = 32;
            stride = 30;
            cur = sui->sub_cursor;
            scr = sui->sub_scroll;
            break;
        case NL_UI_SETTINGS_STATS:
            row_y0 = 33;
            stride = 36;
            cur = sui->sub_cursor;
            scr = sui->sub_scroll;
            break;
        case NL_UI_SETTINGS_PROFILES:
            row_y0 = 32;
            stride = 30;
            cur = (sui->prof_mode == NL_PROF_MODE_LIST) ? sui->prof_cursor
                : ((sui->prof_mode == NL_PROF_MODE_DETAIL) ? sui->prof_detail_cursor : 0);
            scr = (sui->prof_mode == NL_PROF_MODE_LIST) ? sui->prof_scroll : 0;
            break;
        default:
            row_y0 = 34;
            stride = 30;
            cur = 0;
            scr = 0;
            break;
    }

    int target_y = row_y0 + cur * stride;
    state->anim.set_target_sel_y_fp = target_y << 4;
    state->anim.set_target_scroll_fp = (scr * stride) << 4;

    if (snap) {
        state->anim.set_sel_y_fp = state->anim.set_target_sel_y_fp;
        state->anim.set_list_scroll_fp = state->anim.set_target_scroll_fp;
        state->anim.set_sel_glow_fp = 256;
        state->anim.set_prev_cursor = -1;
    } else {
        state->anim.set_prev_cursor = prev_cursor;
        state->anim.set_sel_glow_fp = 56;
    }
}

static void settings_transition_screen(nl_state_t *state, nl_ui_screen_t next_screen, int dir) {
    if (!state) return;
    state->anim.set_prev_screen = state->sui.screen;
    state->sui.screen = next_screen;
    state->anim.set_last_screen = next_screen;
    state->anim.set_sec_dir = (dir >= 0) ? 1 : -1;
    state->anim.set_sec_fp = 32;
    if (next_screen == NL_UI_SETTINGS_RESET) {
        state->anim.modal_fp = 32;
        state->anim.modal_btn_fp = state->sui.reset_confirm_yes ? 256 : 0;
    } else if (next_screen == NL_UI_SETTINGS_PROFILES && state->sui.prof_mode == NL_PROF_MODE_CONFIRM_DELETE) {
        state->anim.modal_fp = 32;
        state->anim.modal_btn_fp = state->sui.prof_delete_yes ? 256 : 0;
    } else {
        state->anim.modal_fp = 0;
        state->anim.modal_btn_fp = 0;
    }
    settings_sync_cursor_targets(state, true, -1);
}

static void settings_close_to_library(nl_state_t *state) {
    if (!state) return;
    state->anim.set_prev_screen = state->sui.screen;
    state->sui.screen = NL_UI_LIBRARY;
    state->anim.set_last_screen = NL_UI_LIBRARY;
    state->sui.sc_picking_key = false;
    state->sui.sc_conflict_prompt = false;
    state->sui.prof_from_quick = false;
    state->sui.prof_mode = NL_PROF_MODE_LIST;
    state->anim.modal_fp = 0;
    if (state->anim.set_open_fp <= 48) {
        state->anim.set_open_fp = 0;
    } else {
        state->anim.set_open_fp = (state->anim.set_open_fp > 208) ? 208 : (state->anim.set_open_fp - 32);
    }
}

void nl_toggle_favorite(nl_state_t *state) {
    if (!state || state->filtered_count <= 0) return;
    int game_idx = state->filtered_indices[state->selected_pos];
    bool next_fav = !state->games[game_idx].is_favorite;
    state->games[game_idx].is_favorite = next_fav;
    nl_show_toast(state, next_fav ? "AJOUTE AUX FAVORIS" : "RETIRE DES FAVORIS", 50);
    if (state->active_category == NL_CAT_FAVORITES) {
        nl_rebuild_filter(state);
    }
    state->sui.config_dirty = true;
    state->sui.profiles_dirty = true;
}

void nl_batch_capture_init(nl_state_t *state) {
    if (!state) return;
    nl_settings_ui_t *sui = &state->sui;
    sui->cap_active = false;
    sui->cap_semi_auto = false;
    sui->cap_finished = false;
    sui->cap_total = 0;
    sui->cap_pos = 0;
    sui->cap_captured = 0;
    sui->cap_skipped = 0;
    sui->cap_failed = 0;
    sui->cap_already = 0;
    sui->cap_last_game_idx = -1;
    sui->cap_last_status = NL_CAP_IDLE;
    sui->cap_step_req = false;

    for (int i = 0; i < state->total_games; i++) {
        const nl_game_t *g = &state->games[i];
        if (g->has_screenshot && g->scr_pixels != NULL) {
            sui->cap_already++;
        } else if (sui->cap_total < NL_MAX_GAMES) {
            sui->cap_queue[sui->cap_total++] = i;
        }
    }

    if (sui->cap_total > 0) {
        sui->cap_active = true;
        sui->cap_step_req = true;
    } else {
        sui->cap_finished = true;
    }
}

bool nl_batch_capture_advance(nl_state_t *state, bool captured_ok, bool was_skipped, nl_cap_status_t st) {
    if (!state) return false;
    nl_settings_ui_t *sui = &state->sui;
    if (!sui->cap_active || sui->cap_pos >= sui->cap_total) {
        sui->cap_active = false;
        sui->cap_finished = true;
        sui->cap_step_req = false;
        return false;
    }

    sui->cap_last_game_idx = sui->cap_queue[sui->cap_pos];
    sui->cap_last_status = st;

    if (captured_ok) {
        sui->cap_captured++;
        sui->cap_semi_auto = false;
        sui->cap_pos++;
    } else if (was_skipped) {
        sui->cap_skipped++;
        sui->cap_semi_auto = false;
        sui->cap_pos++;
    } else {
        sui->cap_failed++;
        sui->cap_semi_auto = true;
        sui->cap_step_req = false;
        return true;
    }

    if (sui->cap_pos >= sui->cap_total) {
        sui->cap_active = false;
        sui->cap_finished = true;
        sui->cap_step_req = false;
        return false;
    }

    sui->cap_step_req = true;
    return true;
}

void nl_batch_capture_cancel(nl_state_t *state) {
    if (!state) return;
    state->sui.cap_active = false;
    state->sui.cap_semi_auto = false;
    state->sui.cap_step_req = false;
    state->sui.cap_cancelled = true;
}

void nl_record_launch(nl_state_t *state, int game_idx) {
    if (!state || game_idx < 0 || game_idx >= state->total_games) return;
    state->play_counter++;
    state->games[game_idx].last_played_rank = state->play_counter;
    state->games[game_idx].launch_count++;
}

void nl_record_play_session(nl_state_t *state, int game_idx, uint32_t start_rtc, uint32_t end_rtc) {
    if (!state || game_idx < 0 || game_idx >= state->total_games) return;
    nl_game_t *g = &state->games[game_idx];
    if (end_rtc >= 946684800U) {
        g->last_played_ts = end_rtc;
    }
    if (end_rtc >= start_rtc) {
        uint32_t delta = end_rtc - start_rtc;
        /* Guard against corrupted/uninitialized RTC jump (> 12h in one session) */
        if (delta <= 43200U) {
            g->play_time_sec += delta;
        }
    }
    state->sui.config_dirty = true;
}

static void on_selection_changed(nl_state_t *state) {
    nl_layout_metrics_t m;
    nl_get_layout_metrics_for_count(state->settings.layout, state->settings.show_names, state->settings.show_shortcuts, state->filtered_count, &m);
    state->current_page = state->selected_pos / m.per_page;
    sync_scroll_targets(state, false);
}

static void trigger_game_launch(nl_state_t *state, int game_idx) {
    if (!state || game_idx < 0 || game_idx >= state->total_games) return;
    state->anim.ink_active = false;
    state->anim.set_open_fp = 0;
    /* Freeze all card slot offsets so zero library items move during launch */
    for (int i = 0; i < NL_MAX_GAMES; i++) {
        state->anim.card_dx_fp[i] = 0;
        state->anim.card_dy_fp[i] = 0;
    }
    if (state->filtered_count > 0 &&
        (state->selected_pos < 0 ||
         state->selected_pos >= state->filtered_count ||
         state->filtered_indices[state->selected_pos] != game_idx)) {
        for (int fi = 0; fi < state->filtered_count; fi++) {
            if (state->filtered_indices[fi] == game_idx) {
                state->selected_pos = fi;
                on_selection_changed(state);
                break;
            }
        }
    }
    state->launch_game_idx = game_idx;
    state->launch_requested = true;
    nl_record_launch(state, game_idx);
}

static void adjust_list_scroll(int cursor, int *scroll, int visible_rows) {
    if (cursor < *scroll) {
        *scroll = cursor;
    } else if (cursor >= *scroll + visible_rows) {
        *scroll = cursor - visible_rows + 1;
    }
    if (*scroll < 0) *scroll = 0;
}

static void assign_shortcut_key_to_game(nl_state_t *state, int game_idx, char key_char, bool force_replace) {
    if (!state || game_idx < 0 || game_idx >= state->total_games) return;
    char u = (char)toupper((unsigned char)key_char);
    if (nl_shortcut_is_reserved(u)) {
        nl_show_toast(state, "TOUCHE SYSTEME RESERVEE", 60);
        return;
    }
    if (!nl_shortcut_is_assignable(u)) {
        nl_show_toast(state, "TOUCHE NON SUPPORTEE", 60);
        return;
    }
    int owner = nl_shortcut_find_owner(state, u, game_idx);
    if (owner >= 0 && !force_replace) {
        state->sui.sc_conflict_prompt = true;
        state->sui.sc_conflict_key = u;
        state->sui.sc_conflict_owner_idx = owner;
        state->sui.sc_conflict_confirm_yes = false;
        state->anim.modal_fp = 32;
        state->anim.modal_btn_fp = 0;
        return;
    }
    if (owner >= 0) {
        state->games[owner].shortcut_key = 0;
    }
    state->games[game_idx].shortcut_key = u;
    state->sui.sc_picking_key = false;
    state->sui.sc_conflict_prompt = false;
    state->anim.modal_fp = 0;
    state->sui.config_dirty = true;
    char msg[NL_MAX_TOAST_LEN];
    snprintf(msg, sizeof(msg), "RACCOURCI [%c] ENREGISTRE", u);
    nl_show_toast(state, msg, 55);
}

static void execute_autostart_toggle(nl_state_t *state) {
    bool target = !state->settings.autostart;
    if (state->sys.docs_root[0] != '\0') {
        char diag[64];
        nl_autostart_set(state, target, diag, sizeof(diag));
        nl_show_toast(state, diag, 70);
    } else {
        /* Host/unit mode without filesystem path: record request */
        state->sui.autostart_toggle_req = true;
        state->sui.autostart_target_val = target;
        if (!state->sys.has_ndless || !state->sys.ndless_persistent) {
            state->settings.autostart = false;
            nl_show_toast(state, "Ndless persistant requis", 70);
        } else {
            state->settings.autostart = target;
            state->sys.autostart_active = target;
            nl_show_toast(state, target ? "Lancement auto : ON" : "Lancement auto : OFF", 55);
        }
    }
    state->sui.config_dirty = true;
}

static void execute_hotkey_toggle(nl_state_t *state) {
    bool target = !state->settings.global_hotkey;
    if (state->sys.docs_root[0] != '\0') {
        char diag[64];
        nl_hotkey_set(state, target, diag, sizeof(diag));
        nl_show_toast(state, diag, 70);
    } else {
        state->sui.hotkey_toggle_req = true;
        state->sui.hotkey_target_val = target;
        if (!state->sys.has_ndless) {
            state->settings.global_hotkey = false;
            nl_show_toast(state, "Ndless requis pour Ctrl+Menu", 70);
        } else {
            state->settings.global_hotkey = target;
            state->sys.hotkey_active = target;
            nl_show_toast(state, target ? "Raccourci TI-OS (Ctrl+Menu) : ON" : "Raccourci TI-OS : OFF", 55);
        }
    }
    state->sui.config_dirty = true;
}

static void request_calculator_poweroff(nl_state_t *state) {
    if (!state) return;
    state->sui.poweroff_req = true;
    state->sui.config_dirty = true;
    state->anim.val_pulse_fp = 256;
    state->anim.val_pulse_dir = 1;
    nl_show_toast(state, "EXTINCTION CALCULATRICE...", 70);
}

static int compute_qp_slot_center_x(const nl_state_t *state, int slot) {
    int pcount = (state->profiles.count > 0) ? state->profiles.count : 1;
    int total_slots = pcount + 1;
    const int vis_max = 6;
    const int diam = 34;
    const int stride = 44;
    int cur = state->sui.qp_cursor;
    if (cur < 0) cur = 0;
    if (cur >= total_slots) cur = total_slots - 1;
    int shown = (total_slots < vis_max) ? total_slots : vis_max;
    int win_start = 0;
    if (total_slots > vis_max) {
        win_start = cur - (vis_max / 2);
        if (win_start < 0) win_start = 0;
        if (win_start > total_slots - vis_max) win_start = total_slots - vis_max;
    }
    int strip_w = shown * diam + (shown - 1) * (stride - diam);
    int start_x = 160 - strip_w / 2;
    int rel = slot - win_start;
    if (rel < 0) rel = 0;
    if (rel >= shown) rel = shown - 1;
    return start_x + rel * stride + diam / 2;
}

static int clamp_cursor_style_to_card(nl_cursor_style_t style) {
    if (style == NL_CURSOR_STYLE_CIRCLE) return 1;
    if (style == NL_CURSOR_STYLE_POINTER) return 2;
    return 0;
}

static nl_cursor_style_t card_to_cursor_style(int card_idx) {
    if (card_idx == 1) return NL_CURSOR_STYLE_CIRCLE;
    if (card_idx == 2) return NL_CURSOR_STYLE_POINTER;
    return NL_CURSOR_STYLE_ARROW;
}

const char *nl_cursor_style_to_str(nl_cursor_style_t style) {
    switch (style) {
        case NL_CURSOR_STYLE_NONE:    return "none";
        case NL_CURSOR_STYLE_CIRCLE:  return "circle";
        case NL_CURSOR_STYLE_POINTER: return "pointer";
        case NL_CURSOR_STYLE_ARROW:
        default:                      return "arrow";
    }
}

nl_cursor_style_t nl_str_to_cursor_style(const char *s) {
    if (!s) return NL_CURSOR_STYLE_ARROW;
    if (str_icmp_u(s, "none") == 0 || str_icmp_u(s, "aucun") == 0 || strcmp(s, "0") == 0) {
        return NL_CURSOR_STYLE_NONE;
    }
    if (str_icmp_u(s, "circle") == 0 || str_icmp_u(s, "cercle") == 0 || strcmp(s, "2") == 0) {
        return NL_CURSOR_STYLE_CIRCLE;
    }
    if (str_icmp_u(s, "pointer") == 0 || str_icmp_u(s, "pointeur") == 0 ||
        str_icmp_u(s, "hand") == 0 || strcmp(s, "3") == 0) {
        return NL_CURSOR_STYLE_POINTER;
    }
    return NL_CURSOR_STYLE_ARROW;
}

const char *nl_cursor_style_label(nl_cursor_style_t style) {
    switch (style) {
        case NL_CURSOR_STYLE_NONE:    return "Aucun curseur";
        case NL_CURSOR_STYLE_CIRCLE:  return "Cercle";
        case NL_CURSOR_STYLE_POINTER: return "Pointeur";
        case NL_CURSOR_STYLE_ARROW:
        default:                      return "Fleche";
    }
}

const char *nl_cursor_speed_label(int speed_level) {
    static const char *const k_speed_labels[NL_CURSOR_SPEED_COUNT] = {
        "1/5 Lente",
        "2/5 Douce",
        "3/5 Normale",
        "4/5 Rapide",
        "5/5 Tres rap."
    };
    if (speed_level < 0) speed_level = 0;
    if (speed_level >= NL_CURSOR_SPEED_COUNT) speed_level = NL_CURSOR_SPEED_COUNT - 1;
    return k_speed_labels[speed_level];
}

bool nl_mouse_is_active(const nl_state_t *state) {
    if (!state) return false;
    return (state->settings.touchpad_mode == NL_TP_MODE_MOUSE) || state->sui.ctrl_mouse_active;
}

bool nl_cursor_is_visible(const nl_state_t *state) {
    return nl_mouse_is_active(state);
}

void nl_cursor_compute_target(const nl_state_t *state, int *out_x, int *out_y) {
    int tx = 160;
    int ty = 116;
    if (!state) {
        if (out_x) *out_x = tx;
        if (out_y) *out_y = ty;
        return;
    }

    if (nl_mouse_is_active(state)) {
        tx = (state->anim.mouse_x_fp + 8) >> 4;
        ty = (state->anim.mouse_y_fp + 8) >> 4;
        if (tx < 0) tx = 0;
        if (tx >= NL_SCREEN_W) tx = NL_SCREEN_W - 1;
        if (ty < 0) ty = 0;
        if (ty >= NL_SCREEN_H) ty = NL_SCREEN_H - 1;
        if (out_x) *out_x = tx;
        if (out_y) *out_y = ty;
        return;
    }

    if (state->sui.screen == NL_UI_LIBRARY) {
        if (state->sui.qp_open) {
            tx = compute_qp_slot_center_x(state, state->sui.qp_cursor);
            ty = 114;
        } else if (state->filtered_count > 0) {
            int sel = state->selected_pos;
            if (sel < 0) sel = 0;
            if (sel >= state->filtered_count) sel = state->filtered_count - 1;

            nl_layout_metrics_t lm;
            nl_get_layout_metrics_for_count(
                state->settings.layout,
                state->settings.show_names,
                state->settings.show_shortcuts,
                state->filtered_count,
                &lm
            );
            int per_page = (lm.per_page > 0) ? lm.per_page : 1;
            int page_idx = sel / per_page;
            int idx_in_page = sel % per_page;
            int page_start = page_idx * per_page;
            int page_items = state->filtered_count - page_start;
            if (page_items > per_page) page_items = per_page;

            int cx = 0, cy = 0;
            nl_compute_grid_slot_pos(&lm, page_items, idx_in_page, &cx, &cy);
            tx = cx + lm.card_w / 2;
            ty = cy + lm.card_h / 2;
        }
    } else if (state->sui.screen == NL_UI_SETTINGS_TOUCHPAD) {
        if (state->sui.tp_tab == 1) {
            int sc = state->sui.tp_style_cursor;
            if (sc < 0) sc = 0;
            if (sc > 2) sc = 2;
            tx = 14 + sc * 101 + 45;
            ty = 108;
        } else {
            int r = state->sui.tp_cursor;
            if (r == 0) {
                tx = 240;
                ty = 61;
            } else if (r == 1) {
                tx = 266;
                ty = 90;
            } else if (r == 2) {
                int sens = state->settings.touchpad_sensitivity;
                if (sens < 0) sens = 0;
                if (sens >= NL_TP_SENS_COUNT) sens = NL_TP_SENS_COUNT - 1;
                tx = 34 + sens * 63;
                ty = 131;
            } else if (r == 3) {
                int cspd = state->settings.cursor_speed;
                if (cspd < 0) cspd = 0;
                if (cspd >= NL_CURSOR_SPEED_COUNT) cspd = NL_CURSOR_SPEED_COUNT - 1;
                tx = 34 + cspd * 63;
                ty = 171;
            } else {
                tx = 250;
                ty = 201;
            }
        }
    } else if (state->sui.screen == NL_UI_SETTINGS_PROFILES) {
        if (state->sui.prof_mode == NL_PROF_MODE_CREATE_AVATAR ||
            state->sui.prof_mode == NL_PROF_MODE_EDIT_AVATAR) {
            int cols = (state->sui.prof_avatar_cat == 0) ? 8 : 5;
            int col = state->sui.prof_avatar_cursor % cols;
            int row = state->sui.prof_avatar_cursor / cols;
            tx = 44 + col * 38;
            ty = 82 + row * 54;
        } else {
            tx = 236;
            ty = (state->anim.set_target_sel_y_fp >> 4) + 14;
        }
    } else {
        tx = 236;
        ty = (state->anim.set_target_sel_y_fp >> 4) + 14;
    }

    int nudge_x = (state->anim.cur_nudge_x_fp + 8) >> 4;
    int nudge_y = (state->anim.cur_nudge_y_fp + 8) >> 4;
    tx += nudge_x;
    ty += nudge_y;

    if (tx < 10) tx = 10;
    if (tx > NL_SCREEN_W - 10) tx = NL_SCREEN_W - 10;
    if (ty < 28) ty = 28;
    if (ty > NL_SCREEN_H - 10) ty = NL_SCREEN_H - 10;

    if (out_x) *out_x = tx;
    if (out_y) *out_y = ty;
}

void nl_cursor_wake(nl_state_t *state) {
    if (!state) return;
    int tx = 160, ty = 116;
    nl_cursor_compute_target(state, &tx, &ty);
    if (nl_mouse_is_active(state)) {
        state->anim.cur_target_x_fp = state->anim.mouse_x_fp;
        state->anim.cur_target_y_fp = state->anim.mouse_y_fp;
    } else {
        state->anim.cur_target_x_fp = tx << 4;
        state->anim.cur_target_y_fp = ty << 4;
    }
    state->anim.cur_idle_ticks = 0;
    if (!state->anim.cur_initialized) {
        state->anim.cur_initialized = true;
        state->anim.cur_x_fp = state->anim.cur_target_x_fp;
        state->anim.cur_y_fp = state->anim.cur_target_y_fp;
        state->anim.cur_vx_fp = 0;
        state->anim.cur_vy_fp = 0;
        for (int i = 0; i < NL_CURSOR_TRAIL_LEN; i++) {
            state->anim.cur_trail_x_fp[i] = state->anim.cur_x_fp;
            state->anim.cur_trail_y_fp[i] = state->anim.cur_y_fp;
            state->anim.cur_trail_alpha[i] = 0;
        }
    }
    if (nl_cursor_is_visible(state)) {
        state->anim.cur_opacity_fp = 256;
    }
}

void nl_cursor_apply_touchpad_nudge(
    nl_state_t *state,
    const nl_touchpad_tracker_t *tracker,
    bool contact
) {
    if (!state) return;
    if (nl_mouse_is_active(state)) {
        state->anim.cur_nudge_x_fp = 0;
        state->anim.cur_nudge_y_fp = 0;
        return;
    }
    if (contact && tracker && tracker->in_contact && state->settings.touchpad_nav) {
        int thresh = nl_touchpad_threshold_for_level(state->settings.touchpad_sensitivity);
        if (thresh < 50) thresh = 50;
        int nx = (int)((tracker->acc_x * 14 * 16) / thresh);
        int ny = (int)((tracker->acc_y * 14 * 16) / thresh);
        if (nx > 14 * 16) nx = 14 * 16;
        if (nx < -14 * 16) nx = -14 * 16;
        if (ny > 14 * 16) ny = 14 * 16;
        if (ny < -14 * 16) ny = -14 * 16;
        state->anim.cur_nudge_x_fp = nx;
        state->anim.cur_nudge_y_fp = ny;
        if (abs(tracker->acc_x) > 8 || abs(tracker->acc_y) > 8) {
            nl_cursor_wake(state);
        }
    } else {
        state->anim.cur_nudge_x_fp = (state->anim.cur_nudge_x_fp * 5) / 8;
        state->anim.cur_nudge_y_fp = (state->anim.cur_nudge_y_fp * 5) / 8;
        if (abs(state->anim.cur_nudge_x_fp) <= 2) state->anim.cur_nudge_x_fp = 0;
        if (abs(state->anim.cur_nudge_y_fp) <= 2) state->anim.cur_nudge_y_fp = 0;
    }
}

static void record_mouse_trail_step(nl_state_t *state, int prev_x_fp, int prev_y_fp, int moved_fp) {
    for (int i = NL_CURSOR_TRAIL_LEN - 1; i > 0; i--) {
        state->anim.cur_trail_x_fp[i] = state->anim.cur_trail_x_fp[i - 1];
        state->anim.cur_trail_y_fp[i] = state->anim.cur_trail_y_fp[i - 1];
        state->anim.cur_trail_alpha[i] = (state->anim.cur_trail_alpha[i - 1] * 17) / 24;
    }
    state->anim.cur_trail_x_fp[0] = prev_x_fp;
    state->anim.cur_trail_y_fp[0] = prev_y_fp;
    int spawn_alpha = (moved_fp >= 10) ? (108 + moved_fp / 2) : 0;
    if (spawn_alpha > 210) spawn_alpha = 210;
    state->anim.cur_trail_alpha[0] = (uint8_t)spawn_alpha;
}

bool nl_mouse_feed_touchpad(
    nl_state_t *state,
    bool contact,
    uint16_t raw_x,
    uint16_t raw_y,
    uint16_t pad_w,
    uint16_t pad_h
) {
    static const int k_speed_scales[NL_CURSOR_SPEED_COUNT] = { 8, 12, 17, 23, 30 };
    if (!state || !nl_mouse_is_active(state)) return false;

    if (!contact) {
        state->anim.mouse_last_contact = false;
        return false;
    }

    if (!state->anim.mouse_last_contact) {
        state->anim.mouse_last_contact = true;
        state->anim.mouse_last_raw_x = raw_x;
        state->anim.mouse_last_raw_y = raw_y;
        state->anim.mouse_vx_fp = 0;
        state->anim.mouse_vy_fp = 0;
        nl_cursor_wake(state);
        nl_mouse_update_hover(state);
        return false;
    }

    int32_t w = (pad_w >= 200U) ? (int32_t)pad_w : 2328;
    int32_t h = (pad_h >= 200U) ? (int32_t)pad_h : 1691;
    int32_t dx_norm = ((int32_t)raw_x - (int32_t)state->anim.mouse_last_raw_x) * 1024 / w;
    int32_t dy_norm = ((int32_t)state->anim.mouse_last_raw_y - (int32_t)raw_y) * 1024 / h;

    state->anim.mouse_last_raw_x = raw_x;
    state->anim.mouse_last_raw_y = raw_y;

    if (abs(dx_norm) > 460 || abs(dy_norm) > 460) {
        state->anim.mouse_vx_fp = 0;
        state->anim.mouse_vy_fp = 0;
        return false;
    }

    if (abs(dx_norm) < 3 && abs(dy_norm) < 3) {
        state->anim.mouse_vx_fp = 0;
        state->anim.mouse_vy_fp = 0;
        return false;
    }

    int spd = state->settings.cursor_speed;
    if (spd < 0) spd = 0;
    if (spd >= NL_CURSOR_SPEED_COUNT) spd = NL_CURSOR_SPEED_COUNT - 1;
    int scale = k_speed_scales[spd];

    int dx_fp = (int)((dx_norm * scale * 16) / 64);
    int dy_fp = (int)((dy_norm * scale * 12) / 64);
    if (dx_fp == 0 && abs(dx_norm) >= 3) dx_fp = (dx_norm > 0) ? 4 : -4;
    if (dy_fp == 0 && abs(dy_norm) >= 3) dy_fp = (dy_norm > 0) ? 4 : -4;

    int prev_x_fp = state->anim.mouse_x_fp;
    int prev_y_fp = state->anim.mouse_y_fp;
    int nx_fp = prev_x_fp + dx_fp;
    int ny_fp = prev_y_fp + dy_fp;

    const int max_x_fp = (NL_SCREEN_W - 1) << 4;
    const int max_y_fp = (NL_SCREEN_H - 1) << 4;
    if (nx_fp < 0) nx_fp = 0;
    if (nx_fp > max_x_fp) nx_fp = max_x_fp;
    if (ny_fp < 0) ny_fp = 0;
    if (ny_fp > max_y_fp) ny_fp = max_y_fp;

    state->anim.mouse_x_fp = nx_fp;
    state->anim.mouse_y_fp = ny_fp;
    state->anim.mouse_vx_fp = (dx_fp * 2) / 5;
    state->anim.mouse_vy_fp = (dy_fp * 2) / 5;
    state->anim.cur_target_x_fp = nx_fp;
    state->anim.cur_target_y_fp = ny_fp;
    state->anim.cur_x_fp = nx_fp;
    state->anim.cur_y_fp = ny_fp;
    state->anim.cur_vx_fp = 0;
    state->anim.cur_vy_fp = 0;
    state->anim.cur_initialized = true;
    state->anim.cur_idle_ticks = 0;
    state->anim.cur_opacity_fp = 256;

    int moved_fp = abs(nx_fp - prev_x_fp) + abs(ny_fp - prev_y_fp);
    if (moved_fp > 0) {
        record_mouse_trail_step(state, prev_x_fp, prev_y_fp, moved_fp);
    }

    nl_mouse_update_hover(state);
    return (moved_fp > 0);
}

void nl_mouse_move_to(nl_state_t *state, int x_px, int y_px) {
    if (!state) return;
    if (x_px < 0) x_px = 0;
    if (x_px >= NL_SCREEN_W) x_px = NL_SCREEN_W - 1;
    if (y_px < 0) y_px = 0;
    if (y_px >= NL_SCREEN_H) y_px = NL_SCREEN_H - 1;

    int prev_x_fp = state->anim.mouse_x_fp;
    int prev_y_fp = state->anim.mouse_y_fp;
    int nx_fp = x_px << 4;
    int ny_fp = y_px << 4;

    state->anim.mouse_x_fp = nx_fp;
    state->anim.mouse_y_fp = ny_fp;
    state->anim.mouse_vx_fp = 0;
    state->anim.mouse_vy_fp = 0;
    state->anim.cur_target_x_fp = nx_fp;
    state->anim.cur_target_y_fp = ny_fp;
    state->anim.cur_x_fp = nx_fp;
    state->anim.cur_y_fp = ny_fp;
    state->anim.cur_vx_fp = 0;
    state->anim.cur_vy_fp = 0;
    state->anim.cur_initialized = true;
    state->anim.cur_idle_ticks = 0;
    if (nl_mouse_is_active(state)) {
        state->anim.cur_opacity_fp = 256;
    }

    int moved_fp = abs(nx_fp - prev_x_fp) + abs(ny_fp - prev_y_fp);
    if (moved_fp >= 16) {
        record_mouse_trail_step(state, prev_x_fp, prev_y_fp, moved_fp);
    }
    nl_mouse_update_hover(state);
}

static void compute_pagination_row_metrics(
    int total_pages,
    int *out_start_x,
    int *out_end_x,
    int *out_dot_w,
    int *out_gap,
    int *out_active_extra
) {
    if (total_pages < 1) total_pages = 1;
    if (total_pages <= NL_PAGINATION_MAX_DOTS) {
        const int dot_d = 8;
        const int cap_w = 18;
        const int gap = 6;
        int total_w = (total_pages - 1) * (dot_d + gap) + cap_w;
        int start_x = (NL_SCREEN_W - total_w) / 2;
        *out_start_x = start_x;
        *out_end_x = start_x + total_w;
        *out_dot_w = dot_d;
        *out_gap = gap;
        *out_active_extra = cap_w - dot_d;
    } else {
        const int max_row_w = 280;
        int active_extra = (total_pages <= 36) ? 6 : ((total_pages <= 64) ? 4 : 2);
        int gap = (total_pages <= 32) ? 4 : ((total_pages <= 56) ? 3 : ((total_pages <= 84) ? 2 : 1));
        int avail = max_row_w - (total_pages - 1) * gap - active_extra;
        int dash_w = avail / total_pages;
        if (dash_w < 1) dash_w = 1;
        if (dash_w > 10) dash_w = 10;
        int total_w = total_pages * dash_w + (total_pages - 1) * gap + active_extra;
        if (total_w > max_row_w && gap > 1) {
            gap--;
            total_w = total_pages * dash_w + (total_pages - 1) * gap + active_extra;
        }
        int start_x = (NL_SCREEN_W - total_w) / 2;
        *out_start_x = start_x;
        *out_end_x = start_x + total_w;
        *out_dot_w = dash_w;
        *out_gap = gap;
        *out_active_extra = active_extra;
    }
}

static const char *settings_screen_title_for_chrome(const nl_state_t *state, nl_ui_screen_t scr) {
    switch (scr) {
        case NL_UI_SETTINGS_MAIN:          return "PARAMETRES";
        case NL_UI_SETTINGS_AUTOSTART:     return "DEMARRAGE AUTOMATIQUE";
        case NL_UI_SETTINGS_HOTKEY:        return "RACCOURCI GLOBAL TI-OS";
        case NL_UI_SETTINGS_DISPLAY:       return "AFFICHAGE";
        case NL_UI_SETTINGS_TOUCHPAD:      return (state->sui.tp_tab == 1) ? "PERSONNALISER LE CURSEUR" : "NAVIGATION TACTILE";
        case NL_UI_SETTINGS_SHORTCUTS:     return "TOUCHES DE LANCEMENT";
        case NL_UI_SETTINGS_KEY_HELP:      return "COMMANDES DU LAUNCHER";
        case NL_UI_SETTINGS_SORT:          return "TRI DE LA BIBLIOTHEQUE";
        case NL_UI_SETTINGS_STATS:         return "STATISTIQUES DE JEU";
        case NL_UI_SETTINGS_CAPTURE_BATCH: return "GENERER CAPTURES D'ECRAN";
        case NL_UI_SETTINGS_ICONS:         return "GESTION DES ICONES";
        case NL_UI_SETTINGS_RESET:         return "REINITIALISER LES REGLAGES";
        case NL_UI_SETTINGS_INFO:          return "INFORMATIONS SYSTEME";
        case NL_UI_SETTINGS_CREDITS:       return "A PROPOS ET CREDITS";
        case NL_UI_SETTINGS_PROFILES:
            if (state->sui.prof_mode == NL_PROF_MODE_DETAIL || state->sui.prof_mode == NL_PROF_MODE_CONFIRM_DELETE) {
                return "GESTION DU PROFIL";
            }
            if (state->sui.prof_mode == NL_PROF_MODE_EDIT_NAME) return "MODIFIER LE NOM";
            if (state->sui.prof_mode == NL_PROF_MODE_CREATE_NAME) return "NOUVEAU PROFIL (1/2)";
            if (state->sui.prof_mode == NL_PROF_MODE_CREATE_AVATAR) return "NOUVEAU PROFIL (2/2)";
            if (state->sui.prof_mode == NL_PROF_MODE_EDIT_AVATAR) return "CHOISIR UNE ICONE";
            return "PROFILS UTILISATEURS";
        default:                           return "PARAMETRES";
    }
}

void nl_mouse_update_hover(nl_state_t *state) {
    if (!state) return;
    nl_settings_ui_t *sui = &state->sui;
    sui->hover_valid = false;
    sui->hover_kind = NL_HOVER_NONE;
    sui->hover_index = 0;
    sui->hover_sub = 0;

    if (!nl_mouse_is_active(state) || state->anim.launch_anim_active) {
        return;
    }

    int mx = (state->anim.mouse_x_fp + 8) >> 4;
    int my = (state->anim.mouse_y_fp + 8) >> 4;
    if (mx < 0) mx = 0;
    if (mx >= NL_SCREEN_W) mx = NL_SCREEN_W - 1;
    if (my < 0) my = 0;
    if (my >= NL_SCREEN_H) my = NL_SCREEN_H - 1;

    if (sui->screen == NL_UI_LIBRARY) {
        if (sui->qp_open) {
            int pcount = (state->profiles.count > 0) ? state->profiles.count : 1;
            int total_slots = pcount + 1;
            const int vis_max = 6;
            const int diam = 34;
            const int stride = 44;
            int cur = sui->qp_cursor;
            if (cur < 0) cur = 0;
            if (cur >= total_slots) cur = total_slots - 1;
            int shown = (total_slots < vis_max) ? total_slots : vis_max;
            int win_start = 0;
            if (total_slots > vis_max) {
                win_start = cur - (vis_max / 2);
                if (win_start < 0) win_start = 0;
                if (win_start > total_slots - vis_max) win_start = total_slots - vis_max;
            }
            int strip_w = shown * diam + (shown - 1) * (stride - diam);
            int start_x = 160 - strip_w / 2;

            if (win_start > 0 && mx >= 12 && mx <= 26 && my >= 102 && my <= 126) {
                sui->hover_kind = NL_HOVER_QP_PREV;
            } else if (win_start + shown < total_slots && mx >= 294 && mx <= 308 && my >= 102 && my <= 126) {
                sui->hover_kind = NL_HOVER_QP_NEXT;
            } else if (mx < 10 || mx >= 310 || my < 52 || my >= 188) {
                sui->hover_kind = NL_HOVER_QP_OUTSIDE;
            } else {
                for (int v = 0; v < shown; v++) {
                    int scx = start_x + v * stride + diam / 2;
                    if (mx >= scx - 19 && mx <= scx + 19 && my >= 90 && my <= 140) {
                        sui->hover_kind = NL_HOVER_QP_SLOT;
                        sui->hover_index = win_start + v;
                        break;
                    }
                }
            }
            sui->hover_valid = (sui->hover_kind != NL_HOVER_NONE);
            return;
        }

        if (my >= 2 && my <= 24) {
            if (mx >= 8 && mx <= 27) {
                sui->hover_kind = NL_HOVER_PROFILE_BADGE;
            } else if (mx >= 34 && mx < 70) {
                sui->hover_kind = NL_HOVER_CATEGORY;
                sui->hover_index = NL_CAT_ALL;
            } else if (mx >= 76 && mx < 106) {
                sui->hover_kind = NL_HOVER_CATEGORY;
                sui->hover_index = NL_CAT_FAVORITES;
            } else if (mx >= 112 && mx < 142) {
                sui->hover_kind = NL_HOVER_CATEGORY;
                sui->hover_index = NL_CAT_RECENT;
            } else if (mx >= 148 && mx <= 164) {
                sui->hover_kind = NL_HOVER_FAV_BTN;
            } else if (mx >= 293 && mx <= 313) {
                sui->hover_kind = NL_HOVER_SETTINGS_BTN;
            }
            sui->hover_valid = (sui->hover_kind != NL_HOVER_NONE);
            return;
        }

        if (sui->search_active && my >= 27 && my <= 51) {
            if (mx >= 278 && mx <= 310) {
                sui->hover_kind = NL_HOVER_SEARCH_CLOSE;
            } else if (mx >= 10 && mx < 278) {
                sui->hover_kind = NL_HOVER_SEARCH_BAR;
            }
            sui->hover_valid = (sui->hover_kind != NL_HOVER_NONE);
            return;
        }

        nl_layout_metrics_t lm;
        nl_get_layout_metrics_for_count(
            state->settings.layout,
            state->settings.show_names,
            state->settings.show_shortcuts,
            state->filtered_count,
            &lm
        );

        if (state->total_pages > 1 && my >= lm.pagination_y - 4 && my <= lm.pagination_y + 12) {
            int p_sx = 0, p_ex = 0, p_dw = 8, p_gap = 6, p_extra = 10;
            compute_pagination_row_metrics(state->total_pages, &p_sx, &p_ex, &p_dw, &p_gap, &p_extra);
            int left_x0 = (p_sx >= 19) ? (p_sx - 18) : 2;
            int right_x1 = (p_ex + 18 <= NL_SCREEN_W - 2) ? (p_ex + 18) : (NL_SCREEN_W - 2);
            if (mx >= left_x0 && mx < p_sx - 1) {
                sui->hover_kind = NL_HOVER_PAGE_PREV;
            } else if (mx > p_ex + 1 && mx <= right_x1) {
                sui->hover_kind = NL_HOVER_PAGE_NEXT;
            } else if (mx >= p_sx && mx <= p_ex) {
                int cx = p_sx;
                for (int pi = 0; pi < state->total_pages; pi++) {
                    int pw = p_dw + ((pi == state->current_page) ? p_extra : 0);
                    if (mx >= cx - 1 && mx <= cx + pw + 1) {
                        sui->hover_kind = NL_HOVER_PAGE_DOT;
                        sui->hover_index = pi;
                        break;
                    }
                    cx += pw + p_gap;
                }
            }
            if (sui->hover_kind != NL_HOVER_NONE) {
                sui->hover_valid = true;
                return;
            }
        }

        int search_fp = state->anim.search_bar_fp;
        if (sui->search_active && search_fp == 0) search_fp = 256;
        else if (!sui->search_active && search_fp == 256) search_fp = 0;
        int search_h = (28 * search_fp + 128) >> 8;
        int grid_clip_top = NL_VIEW_TOP + search_h;
        if (state->filtered_count > 0 && my >= grid_clip_top && my < lm.view_bottom) {
            int scroll_y = state->anim.scroll_y_fp >> 4;
            int s_diff = state->anim.target_scroll_y_fp - state->anim.scroll_y_fp;
            if (s_diff < 0) s_diff = -s_diff;
            bool v_settled = (s_diff <= 16);

            for (int f_idx = 0; f_idx < state->filtered_count; f_idx++) {
                int row = f_idx / lm.cols;
                int slot_x = 0, slot_y = 0;
                nl_compute_grid_slot_pos(&lm, state->filtered_count, f_idx, &slot_x, &slot_y);
                int by = slot_y + search_h - scroll_y;
                if (v_settled && search_h == 0 && by < NL_VIEW_TOP && row == state->current_page * lm.rows) {
                    by = NL_VIEW_TOP + 1;
                }
                if (by + lm.card_h + lm.label_h <= grid_clip_top || by >= lm.view_bottom) continue;
                if (mx >= slot_x && mx < slot_x + lm.card_w &&
                    my >= by && my < by + lm.card_h + lm.label_h) {
                    sui->hover_kind = NL_HOVER_GAME_CARD;
                    sui->hover_index = f_idx;
                    sui->hover_valid = true;
                    return;
                }
            }
        }
        return;
    }

    /* Settings Screens Top Bar Chrome (ESC Ret. & DEL power button) */
    if (my >= 2 && my <= 24) {
        if (mx >= 248 && mx <= 312) {
            sui->hover_kind = NL_HOVER_SET_ESC;
            sui->hover_valid = true;
            return;
        }
        if (sui->screen != NL_UI_SETTINGS_SHORTCUTS &&
            sui->screen != NL_UI_SETTINGS_ICONS &&
            sui->screen != NL_UI_SETTINGS_PROFILES) {
            const char *t = settings_screen_title_for_chrome(state, sui->screen);
            int t_len = (int)strlen(t);
            if (t_len > 26) t_len = 26;
            int pwr_x = 32 + t_len * 6 + 6;
            if (pwr_x + 17 <= 246) {
                int pwr_end = (pwr_x + 42 <= 246) ? (pwr_x + 42) : (pwr_x + 18);
                if (mx >= pwr_x && mx <= pwr_end) {
                    sui->hover_kind = NL_HOVER_SET_POWER;
                    sui->hover_valid = true;
                    return;
                }
            }
        }
        return;
    }

    /* Modal Overlays Take Priority Over Background Rows */
    if (sui->screen == NL_UI_SETTINGS_RESET) {
        if (my >= 166 && my <= 192) {
            if (mx >= 42 && mx <= 150) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 0;
                sui->hover_valid = true;
            } else if (mx >= 170 && mx <= 278) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 1;
                sui->hover_valid = true;
            }
        }
        return;
    }
    if (sui->screen == NL_UI_SETTINGS_ICONS && sui->icon_confirm_modal) {
        if (my >= 144 && my <= 168) {
            if (mx >= 46 && mx <= 148) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 0;
                sui->hover_valid = true;
            } else if (mx >= 172 && mx <= 274) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 1;
                sui->hover_valid = true;
            }
        }
        return;
    }
    if (sui->screen == NL_UI_SETTINGS_SHORTCUTS && sui->sc_conflict_prompt) {
        if (my >= 138 && my <= 162) {
            if (mx >= 54 && mx <= 146) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 0;
                sui->hover_valid = true;
            } else if (mx >= 174 && mx <= 266) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 1;
                sui->hover_valid = true;
            }
        }
        return;
    }
    if (sui->screen == NL_UI_SETTINGS_SHORTCUTS && sui->sc_picking_key) {
        const char *keys_str = NULL;
        int n_keys = nl_get_assignable_keys(&keys_str);
        int total_opts = n_keys + 1;
        for (int k = 0; k < total_opts; k++) {
            int r = k / 8;
            int c = k % 8;
            int kx = 34 + c * 31;
            int ky = 72 + r * 26;
            int kw = (k == 0) ? 28 : 26;
            if (mx >= kx && mx < kx + kw && my >= ky && my < ky + 20) {
                sui->hover_kind = NL_HOVER_SC_KEY_CELL;
                sui->hover_index = k;
                sui->hover_valid = true;
                return;
            }
        }
        return;
    }
    if (sui->screen == NL_UI_SETTINGS_PROFILES && sui->prof_mode == NL_PROF_MODE_CONFIRM_DELETE) {
        if (my >= 148 && my <= 172) {
            if (mx >= 42 && mx <= 146) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 0;
                sui->hover_valid = true;
            } else if (mx >= 174 && mx <= 278) {
                sui->hover_kind = NL_HOVER_MODAL_BTN;
                sui->hover_index = 1;
                sui->hover_valid = true;
            }
        }
        return;
    }

    int list_scroll_px = (state->anim.set_target_scroll_fp == state->anim.set_list_scroll_fp ||
                          sui->screen == state->anim.set_last_screen)
        ? (state->anim.set_list_scroll_fp >> 4)
        : 0;

    switch (sui->screen) {
        case NL_UI_SETTINGS_MAIN:
            if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
                int spx = (state->anim.set_last_screen == NL_UI_SETTINGS_MAIN)
                    ? list_scroll_px : (sui->menu_scroll * 30);
                for (int idx = 0; idx < NL_SET_MENU_COUNT; idx++) {
                    int ry = 32 + idx * 30 - spx;
                    if (ry + 28 <= 28 || ry >= 218) continue;
                    if (my >= ry && my < ry + 28) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = idx;
                        sui->hover_valid = true;
                        return;
                    }
                }
            }
            break;

        case NL_UI_SETTINGS_HOTKEY:
            if (mx >= 14 && mx < 306 && my >= 34 && my < 88) {
                sui->hover_kind = NL_HOVER_SET_ROW;
                sui->hover_index = 0;
                sui->hover_valid = true;
            }
            break;

        case NL_UI_SETTINGS_DISPLAY:
            if (mx >= 12 && mx < 304 && my >= 28 && my < 122) {
                int spx = (state->anim.set_last_screen == NL_UI_SETTINGS_DISPLAY)
                    ? list_scroll_px : (sui->display_scroll * 31);
                for (int r = 0; r < NL_DISPLAY_OPT_COUNT; r++) {
                    int ry = 30 + r * 31 - spx;
                    if (ry + 28 <= 28 || ry >= 122) continue;
                    if (my >= ry && my < ry + 28) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = r;
                        sui->hover_valid = true;
                        return;
                    }
                }
            }
            break;

        case NL_UI_SETTINGS_TOUCHPAD:
            if (my >= 28 && my <= 44) {
                if (mx >= 15 && mx <= 153) {
                    sui->hover_kind = NL_HOVER_SET_TAB;
                    sui->hover_index = 0;
                    sui->hover_valid = true;
                    return;
                }
                if (mx >= 155 && mx <= 305) {
                    sui->hover_kind = NL_HOVER_SET_TAB;
                    sui->hover_index = 1;
                    sui->hover_valid = true;
                    return;
                }
            }
            if (sui->tp_tab == 1) {
                if (my >= 50 && my <= 166) {
                    for (int c = 0; c < 3; c++) {
                        int cx = 14 + c * 101;
                        if (mx >= cx && mx < cx + 90) {
                            sui->hover_kind = NL_HOVER_SET_STYLE_CARD;
                            sui->hover_index = c;
                            sui->hover_valid = true;
                            return;
                        }
                    }
                }
            } else {
                if (mx >= 14 && mx < 306) {
                    if (my >= 47 && my < 75) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = 0;
                        sui->hover_valid = true;
                    } else if (my >= 77 && my < 104) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = 1;
                        sui->hover_valid = true;
                    } else if (my >= 106 && my < 144) {
                        if (my >= 122 && mx >= 22 && mx <= 298) {
                            int best_n = 0, best_d = 9999;
                            for (int n = 0; n < NL_TP_SENS_COUNT; n++) {
                                int d = abs(mx - (34 + n * 63));
                                if (d < best_d) { best_d = d; best_n = n; }
                            }
                            sui->hover_kind = NL_HOVER_SET_SLIDER_NOTCH;
                            sui->hover_index = 2;
                            sui->hover_sub = best_n;
                        } else {
                            sui->hover_kind = NL_HOVER_SET_ROW;
                            sui->hover_index = 2;
                        }
                        sui->hover_valid = true;
                    } else if (my >= 146 && my < 184) {
                        if (my >= 162 && mx >= 22 && mx <= 298) {
                            int best_n = 0, best_d = 9999;
                            for (int n = 0; n < NL_CURSOR_SPEED_COUNT; n++) {
                                int d = abs(mx - (34 + n * 63));
                                if (d < best_d) { best_d = d; best_n = n; }
                            }
                            sui->hover_kind = NL_HOVER_SET_SLIDER_NOTCH;
                            sui->hover_index = 3;
                            sui->hover_sub = best_n;
                        } else {
                            sui->hover_kind = NL_HOVER_SET_ROW;
                            sui->hover_index = 3;
                        }
                        sui->hover_valid = true;
                    } else if (my >= 186 && my <= 217) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = 4;
                        sui->hover_valid = true;
                    }
                }
            }
            break;

        case NL_UI_SETTINGS_SHORTCUTS:
            if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
                int spx = (state->anim.set_last_screen == NL_UI_SETTINGS_SHORTCUTS)
                    ? list_scroll_px : (sui->sc_game_scroll * 30);
                for (int idx = 0; idx < state->total_games; idx++) {
                    int ry = 32 + idx * 30 - spx;
                    if (ry + 28 <= 28 || ry >= 218) continue;
                    if (my >= ry && my < ry + 28) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = idx;
                        sui->hover_valid = true;
                        return;
                    }
                }
            }
            break;

        case NL_UI_SETTINGS_SORT:
            if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
                int spx = (state->anim.set_last_screen == NL_UI_SETTINGS_SORT)
                    ? list_scroll_px : (sui->sub_scroll * 30);
                for (int idx = 0; idx < NL_SORT_COUNT; idx++) {
                    int ry = 32 + idx * 30 - spx;
                    if (ry + 28 <= 28 || ry >= 218) continue;
                    if (my >= ry && my < ry + 28) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = idx;
                        sui->hover_valid = true;
                        return;
                    }
                }
            }
            break;

        case NL_UI_SETTINGS_STATS:
            if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
                int spx = (state->anim.set_last_screen == NL_UI_SETTINGS_STATS)
                    ? list_scroll_px : (sui->sub_scroll * 36);
                for (int idx = 0; idx < state->total_games; idx++) {
                    int ry = 33 + idx * 36 - spx;
                    if (ry + 33 <= 28 || ry >= 218) continue;
                    if (my >= ry && my < ry + 33) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = idx;
                        sui->hover_valid = true;
                        return;
                    }
                }
            }
            break;

        case NL_UI_SETTINGS_CAPTURE_BATCH:
            if (sui->cap_active && sui->cap_total > 0) {
                if (my >= 174 && my <= 198) {
                    if (mx >= 30 && mx <= 154) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = 0;
                        sui->hover_valid = true;
                    } else if (mx >= 166 && mx <= 290) {
                        sui->hover_kind = NL_HOVER_SET_ROW;
                        sui->hover_index = 1;
                        sui->hover_valid = true;
                    }
                }
            } else {
                if (mx >= 96 && mx <= 224 && my >= 168 && my <= 198) {
                    sui->hover_kind = NL_HOVER_SET_ROW;
                    sui->hover_index = 0;
                    sui->hover_valid = true;
                }
            }
            break;

        case NL_UI_SETTINGS_ICONS:
            if (state->total_games > 0 && my >= 28 && my < 216) {
                for (int idx = 0; idx < state->total_games; idx++) {
                    int r = idx / 3;
                    int c = idx % 3;
                    int cx = 32 + c * 90;
                    int cy = 33 + (r - sui->icon_scroll) * 90;
                    if (cy + 86 <= 28 || cy >= 216) continue;
                    if (mx >= cx && mx < cx + 68 && my >= cy && my < cy + 84) {
                        sui->hover_kind = NL_HOVER_ICON_CARD;
                        sui->hover_index = idx;
                        sui->hover_valid = true;
                        return;
                    }
                }
            }
            break;

        case NL_UI_SETTINGS_PROFILES:
            if (sui->prof_mode == NL_PROF_MODE_LIST) {
                int total_rows = state->profiles.count + 1;
                if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
                    int spx = (state->anim.set_last_screen == NL_UI_SETTINGS_PROFILES)
                        ? list_scroll_px : (sui->prof_scroll * 30);
                    for (int row = 0; row < total_rows; row++) {
                        int ry = 32 + row * 30 - spx;
                        if (ry + 28 <= 28 || ry >= 218) continue;
                        if (my >= ry && my < ry + 28) {
                            sui->hover_kind = NL_HOVER_PROF_ROW;
                            sui->hover_index = row;
                            sui->hover_valid = true;
                            return;
                        }
                    }
                }
            } else if (sui->prof_mode == NL_PROF_MODE_DETAIL) {
                if (mx >= 13 && mx < 307) {
                    for (int r = 0; r < 4; r++) {
                        int ry = 83 + r * 32;
                        if (my >= ry && my < ry + 28) {
                            sui->hover_kind = NL_HOVER_PROF_DETAIL_ROW;
                            sui->hover_index = r;
                            sui->hover_valid = true;
                            return;
                        }
                    }
                }
            } else if (sui->prof_mode == NL_PROF_MODE_EDIT_AVATAR ||
                       sui->prof_mode == NL_PROF_MODE_CREATE_AVATAR) {
                if (my >= 30 && my <= 46) {
                    for (int c = 0; c < NL_AVATAR_CAT_COUNT; c++) {
                        int tx = 11 + c * 75;
                        if (mx >= tx && mx < tx + 72) {
                            sui->hover_kind = NL_HOVER_PROF_AV_TAB;
                            sui->hover_index = c;
                            sui->hover_valid = true;
                            return;
                        }
                    }
                }
                int cat = sui->prof_avatar_cat;
                if (cat < 0 || cat >= NL_AVATAR_CAT_COUNT) cat = 0;
                int cstart = 0, ccnt = 0;
                nl_avatar_category_range(cat, &cstart, &ccnt);
                int cols = (cat == 0) ? 8 : 5;
                int diam = (cat == 0) ? 31 : 36;
                int dx_step = (cat == 0) ? 37 : 52;
                int dy_step = (cat == 0) ? 54 : 58;
                int grid_w = (cols - 1) * dx_step + diam;
                int start_x = (NL_SCREEN_W - grid_w) / 2;
                const int start_y = 58;
                if (my >= 48 && my < 198) {
                    for (int i = 0; i < ccnt; i++) {
                        int r = i / cols;
                        int col_idx = i % cols;
                        int ax = start_x + col_idx * dx_step;
                        int ay = start_y + r * dy_step;
                        if (mx >= ax - 2 && mx < ax + diam + 2 &&
                            my >= ay - 2 && my < ay + diam + 2) {
                            sui->hover_kind = NL_HOVER_PROF_AV_CELL;
                            sui->hover_index = i;
                            sui->hover_valid = true;
                            return;
                        }
                    }
                }
            }
            break;

        default:
            break;
    }
}

static void trigger_profile_switch_anim(
    nl_state_t *state,
    int avatar_id,
    int origin_x,
    int origin_y,
    int origin_d
) {
    state->anim.prof_switch_active = true;
    state->anim.prof_switch_tick = 0;
    state->anim.prof_switch_max = 16;
    state->anim.prof_switch_avatar_id = avatar_id;
    state->anim.prof_switch_origin_x = origin_x;
    state->anim.prof_switch_origin_y = origin_y;
    state->anim.prof_switch_origin_d = (origin_d >= 12) ? origin_d : 36;
}

static bool begin_profile_create_avatar_flow(nl_state_t *state, bool from_quick, int orig_x, int orig_y) {
    int first_free = nl_avatar_first_free(state);
    if (state->profiles.count >= NL_MAX_PROFILES || first_free < 0) {
        nl_show_toast(state, "43/43 icones deja utilisees", 65);
        return false;
    }
    int cat = nl_avatar_category_of(first_free);
    int cstart = 0, ccnt = 0;
    nl_avatar_category_range(cat, &cstart, &ccnt);
    state->sui.prof_edit_name[0] = '\0';
    state->sui.prof_edit_len = 0;
    state->sui.prof_from_quick = from_quick;
    state->sui.prof_avatar_cat = cat;
    state->sui.prof_avatar_cursor = first_free - cstart;
    if (state->sui.prof_avatar_cursor < 0 || state->sui.prof_avatar_cursor >= ccnt) {
        state->sui.prof_avatar_cursor = 0;
    }
    state->anim.prof_cat_trans_fp = 256;
    state->sui.prof_mode = NL_PROF_MODE_CREATE_AVATAR;

    if (from_quick) {
        state->sui.qp_open = false;
        state->anim.qp_open_fp = 0;
        state->sui.screen = NL_UI_SETTINGS_PROFILES;
        state->anim.set_last_screen = NL_UI_SETTINGS_PROFILES;
        state->anim.set_prev_screen = NL_UI_LIBRARY;
        state->anim.set_sec_fp = 256;
        state->anim.set_open_fp = 256;
        state->anim.prof_plus_active = true;
        state->anim.prof_plus_tick = 0;
        state->anim.prof_plus_max = 14;
        state->anim.prof_plus_origin_x = orig_x;
        state->anim.prof_plus_origin_y = orig_y;
    }
    return true;
}

static bool handle_settings_key(nl_state_t *state, nl_key_t key) {
    nl_settings_ui_t *sui = &state->sui;

    if (key == NL_KEY_DOC) {
        state->display_mode = (nl_display_mode_t)(((unsigned)state->display_mode + 1U) % NL_DISPLAY_COUNT);
        state->anim.ink_active = true;
        state->anim.ink_tick = 0;
        state->anim.ink_max = NL_INK_TRANS_TICKS;
        state->anim.ink_seq++;
        sui->config_dirty = true;
        return true;
    }
    if (key == NL_KEY_MENU) {
        state->accent_color = (nl_accent_color_t)(((unsigned)state->accent_color + 1U) % NL_ACCENT_COUNT);
        state->anim.ink_active = true;
        state->anim.ink_tick = 0;
        state->anim.ink_max = NL_INK_TRANS_TICKS;
        state->anim.ink_seq++;
        sui->config_dirty = true;
        return true;
    }
    if (key == NL_KEY_SETTINGS) {
        settings_close_to_library(state);
        return true;
    }
    if (key == NL_KEY_RESCAN &&
        sui->screen != NL_UI_SETTINGS_SHORTCUTS &&
        sui->screen != NL_UI_SETTINGS_ICONS &&
        sui->screen != NL_UI_SETTINGS_PROFILES) {
        request_calculator_poweroff(state);
        return true;
    }

    switch (sui->screen) {
        case NL_UI_SETTINGS_MAIN:
            if (key == NL_KEY_ESC) {
                settings_close_to_library(state);
                return true;
            }
            if (key == NL_KEY_UP) {
                int prev = sui->menu_cursor;
                sui->menu_cursor = (sui->menu_cursor > 0) ? (sui->menu_cursor - 1) : (NL_SET_MENU_COUNT - 1);
                adjust_list_scroll(sui->menu_cursor, &sui->menu_scroll, 6);
                settings_sync_cursor_targets(state, false, prev);
                return true;
            }
            if (key == NL_KEY_DOWN) {
                int prev = sui->menu_cursor;
                sui->menu_cursor = (sui->menu_cursor + 1) % NL_SET_MENU_COUNT;
                adjust_list_scroll(sui->menu_cursor, &sui->menu_scroll, 6);
                settings_sync_cursor_targets(state, false, prev);
                return true;
            }
            if (key == NL_KEY_EXE || key == NL_KEY_RIGHT) {
                nl_ui_screen_t next_scr = NL_UI_SETTINGS_MAIN;
                switch ((nl_settings_menu_idx_t)sui->menu_cursor) {
                    case NL_SET_MENU_DISPLAY:
                        next_scr = NL_UI_SETTINGS_DISPLAY;
                        sui->display_cursor = 0;
                        sui->display_scroll = 0;
                        break;
                    case NL_SET_MENU_TOUCHPAD:
                        next_scr = NL_UI_SETTINGS_TOUCHPAD;
                        sui->tp_cursor = 0;
                        sui->tp_tab = 0;
                        sui->tp_style_cursor = clamp_cursor_style_to_card(state->settings.touchpad_cursor_style);
                        state->anim.tp_tab_trans_fp = 256;
                        state->anim.tp_style_glow_fp = 256;
                        state->anim.tp_slider_pos_fp = state->settings.touchpad_sensitivity * 256;
                        state->anim.tp_slider_vel_fp = 0;
                        state->anim.tp_notch_last = state->settings.touchpad_sensitivity;
                        state->anim.tp_speed_slider_pos_fp = state->settings.cursor_speed * 256;
                        state->anim.tp_speed_slider_vel_fp = 0;
                        state->anim.tp_speed_notch_last = state->settings.cursor_speed;
                        break;
                    case NL_SET_MENU_SHORTCUTS:
                        next_scr = NL_UI_SETTINGS_SHORTCUTS;
                        sui->sc_game_cursor = 0;
                        sui->sc_game_scroll = 0;
                        sui->sc_picking_key = false;
                        sui->sc_conflict_prompt = false;
                        break;
                    case NL_SET_MENU_KEY_HELP:
                        next_scr = NL_UI_SETTINGS_KEY_HELP;
                        sui->sub_scroll = 0;
                        break;
                    case NL_SET_MENU_SORT:
                        next_scr = NL_UI_SETTINGS_SORT;
                        sui->sub_cursor = (int)state->settings.sort_order;
                        sui->sub_scroll = 0;
                        adjust_list_scroll(sui->sub_cursor, &sui->sub_scroll, 6);
                        break;
                    case NL_SET_MENU_STATS:
                        next_scr = NL_UI_SETTINGS_STATS;
                        sui->sub_cursor = 0;
                        sui->sub_scroll = 0;
                        break;
                    case NL_SET_MENU_CAPTURE_BATCH:
                        next_scr = NL_UI_SETTINGS_CAPTURE_BATCH;
                        nl_batch_capture_init(state);
                        break;
                    case NL_SET_MENU_ICONS:
                        next_scr = NL_UI_SETTINGS_ICONS;
                        sui->icon_cursor = 0;
                        sui->icon_scroll = 0;
                        sui->icon_confirm_modal = false;
                        sui->icon_confirm_yes = false;
                        break;
                    case NL_SET_MENU_REFRESH_LIB:
                        sui->rescan_req = true;
                        state->anim.val_pulse_fp = 256;
                        state->anim.val_pulse_dir = 1;
                        nl_rebuild_filter(state);
                        nl_show_toast(state, "BIBLIOTHEQUE ACTUALISEE", 55);
                        return true;
                    case NL_SET_MENU_HOTKEY:
                        next_scr = NL_UI_SETTINGS_HOTKEY;
                        break;
                    case NL_SET_MENU_PROFILES:
                        next_scr = NL_UI_SETTINGS_PROFILES;
                        sui->prof_mode = NL_PROF_MODE_LIST;
                        sui->prof_cursor = (state->profiles.active_index >= 0) ? (state->profiles.active_index + 1) : 0;
                        sui->prof_scroll = 0;
                        adjust_list_scroll(sui->prof_cursor, &sui->prof_scroll, 6);
                        sui->prof_from_quick = false;
                        break;
                    case NL_SET_MENU_RESET:
                        next_scr = NL_UI_SETTINGS_RESET;
                        sui->reset_confirm_yes = false; /* Always safe Cancel by default */
                        sui->reset_performed = false;
                        break;
                    case NL_SET_MENU_INFO:
                        next_scr = NL_UI_SETTINGS_INFO;
                        break;
                    case NL_SET_MENU_CREDITS:
                        next_scr = NL_UI_SETTINGS_CREDITS;
                        break;
                    default:
                        break;
                }
                settings_transition_screen(state, next_scr, 1);
                return true;
            }
            break;

        case NL_UI_SETTINGS_AUTOSTART:
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (key == NL_KEY_EXE || key == NL_KEY_LEFT || key == NL_KEY_RIGHT) {
                execute_autostart_toggle(state);
                return true;
            }
            break;

        case NL_UI_SETTINGS_HOTKEY:
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (key == NL_KEY_EXE || key == NL_KEY_LEFT || key == NL_KEY_RIGHT) {
                execute_hotkey_toggle(state);
                return true;
            }
            break;

        case NL_UI_SETTINGS_DISPLAY:
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (key == NL_KEY_UP) {
                int prev = sui->display_cursor;
                sui->display_cursor = (sui->display_cursor > 0) ? (sui->display_cursor - 1) : (NL_DISPLAY_OPT_COUNT - 1);
                adjust_list_scroll(sui->display_cursor, &sui->display_scroll, 3);
                settings_sync_cursor_targets(state, false, prev);
                return true;
            }
            if (key == NL_KEY_DOWN) {
                int prev = sui->display_cursor;
                sui->display_cursor = (sui->display_cursor + 1) % NL_DISPLAY_OPT_COUNT;
                adjust_list_scroll(sui->display_cursor, &sui->display_scroll, 3);
                settings_sync_cursor_targets(state, false, prev);
                return true;
            }
            if (key == NL_KEY_LEFT || key == NL_KEY_RIGHT || key == NL_KEY_EXE) {
                if (sui->display_cursor == 0) {
                    int delta = (key == NL_KEY_LEFT) ? (NL_LAYOUT_COUNT - 1) : 1;
                    state->anim.layout_from = state->settings.layout;
                    state->settings.layout = (nl_layout_t)(((int)state->settings.layout + delta) % NL_LAYOUT_COUNT);
                    state->anim.layout_trans_fp = 0;
                    state->anim.val_pulse_fp = 256;
                    state->anim.val_pulse_dir = (key == NL_KEY_LEFT) ? -1 : 1;
                    nl_rebuild_filter(state);
                    sui->config_dirty = true;
                } else if (sui->display_cursor == 1) {
                    state->settings.show_names = !state->settings.show_names;
                    nl_rebuild_filter(state);
                    sui->config_dirty = true;
                } else if (sui->display_cursor == 2) {
                    state->settings.show_shortcuts = !state->settings.show_shortcuts;
                    nl_rebuild_filter(state);
                    sui->config_dirty = true;
                } else if (sui->display_cursor == 3 && key == NL_KEY_EXE) {
                    request_calculator_poweroff(state);
                }
                return true;
            }
            break;

        case NL_UI_SETTINGS_TOUCHPAD:
            if (key == NL_KEY_TAB) {
                sui->tp_tab = (sui->tp_tab == 0) ? 1 : 0;
                sui->tp_style_cursor = clamp_cursor_style_to_card(state->settings.touchpad_cursor_style);
                state->anim.tp_tab_trans_fp = 32;
                state->anim.tp_style_glow_fp = 64;
                nl_cursor_wake(state);
                return true;
            }
            if (sui->tp_tab == 1) {
                /* Tab 1: « Apparence du curseur » (0=Fleche, 1=Cercle, 2=Pointeur) */
                if (key == NL_KEY_ESC) {
                    sui->tp_tab = 0;
                    state->anim.tp_tab_trans_fp = 32;
                    nl_cursor_wake(state);
                    return true;
                }
                if (key == NL_KEY_LEFT || key == NL_KEY_UP || key == NL_KEY_RIGHT || key == NL_KEY_DOWN) {
                    int delta = (key == NL_KEY_RIGHT || key == NL_KEY_DOWN) ? 1 : -1;
                    int next_c = (sui->tp_style_cursor + delta + 3) % 3;
                    sui->tp_style_cursor = next_c;
                    state->settings.touchpad_cursor_style = card_to_cursor_style(next_c);
                    state->anim.tp_style_glow_fp = 32;
                    state->anim.val_pulse_fp = 256;
                    state->anim.val_pulse_dir = delta;
                    sui->config_dirty = true;
                    nl_cursor_wake(state);
                    char msg[40];
                    snprintf(msg, sizeof(msg), "Style : %s", nl_cursor_style_label(state->settings.touchpad_cursor_style));
                    nl_show_toast(state, msg, 50);
                    return true;
                }
                if (key == NL_KEY_EXE) {
                    state->settings.touchpad_cursor_style = card_to_cursor_style(sui->tp_style_cursor);
                    state->anim.tp_style_glow_fp = 32;
                    state->anim.val_pulse_fp = 256;
                    sui->config_dirty = true;
                    nl_cursor_wake(state);
                    char msg[40];
                    snprintf(msg, sizeof(msg), "Curseur : %s", nl_cursor_style_label(state->settings.touchpad_cursor_style));
                    nl_show_toast(state, msg, 55);
                    return true;
                }
                break;
            }

            /* Tab 0: « Navigation tactile » (5 rows: 0=Mode, 1=Glissement, 2=Sensibilite pave, 3=Vitesse souris, 4=Apparence curseur) */
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (key == NL_KEY_UP || key == NL_KEY_DOWN) {
                int prev = sui->tp_cursor;
                if (key == NL_KEY_DOWN) {
                    sui->tp_cursor = (sui->tp_cursor + 1) % 5;
                } else {
                    sui->tp_cursor = (sui->tp_cursor + 4) % 5;
                }
                settings_sync_cursor_targets(state, false, prev);
                nl_cursor_wake(state);
                return true;
            }
            if (sui->tp_cursor == 0) {
                if (key == NL_KEY_EXE || key == NL_KEY_LEFT || key == NL_KEY_RIGHT) {
                    state->settings.touchpad_mode = (state->settings.touchpad_mode == NL_TP_MODE_MOUSE)
                        ? NL_TP_MODE_TOUCHPAD
                        : NL_TP_MODE_MOUSE;
                    bool is_mouse = (state->settings.touchpad_mode == NL_TP_MODE_MOUSE);
                    state->settings.touchpad_cursor_show = is_mouse;
                    if (is_mouse) {
                        if (state->settings.touchpad_cursor_style == NL_CURSOR_STYLE_NONE ||
                            state->settings.touchpad_cursor_style >= NL_CURSOR_STYLE_COUNT) {
                            state->settings.touchpad_cursor_style = NL_CURSOR_STYLE_ARROW;
                        }
                        sui->ctrl_mouse_active = false;
                        nl_mouse_update_hover(state);
                    } else {
                        state->anim.mouse_vx_fp = 0;
                        state->anim.mouse_vy_fp = 0;
                        state->anim.mouse_last_contact = false;
                        if (!sui->ctrl_mouse_active) {
                            state->anim.cur_opacity_fp = 0;
                            sui->hover_valid = false;
                            sui->hover_kind = NL_HOVER_NONE;
                        }
                    }
                    state->anim.val_pulse_fp = 256;
                    state->anim.val_pulse_dir = is_mouse ? 1 : -1;
                    sui->config_dirty = true;
                    nl_cursor_wake(state);
                    nl_show_toast(state,
                                  is_mouse ? "Mode Souris : ACTIF" : "Mode Pave tactile : ACTIF",
                                  55);
                    return true;
                }
            } else if (sui->tp_cursor == 1) {
                if (key == NL_KEY_EXE || key == NL_KEY_LEFT || key == NL_KEY_RIGHT) {
                    state->settings.touchpad_nav = !state->settings.touchpad_nav;
                    state->anim.val_pulse_fp = 256;
                    state->anim.val_pulse_dir = state->settings.touchpad_nav ? 1 : -1;
                    sui->config_dirty = true;
                    nl_cursor_wake(state);
                    nl_show_toast(state,
                                  state->settings.touchpad_nav ? "Glissement au pave : ON" : "Glissement au pave : OFF",
                                  55);
                    return true;
                }
            } else if (sui->tp_cursor == 2) {
                if (key == NL_KEY_LEFT || key == NL_KEY_RIGHT) {
                    int prev_sens = state->settings.touchpad_sensitivity;
                    int next_sens = prev_sens + ((key == NL_KEY_RIGHT) ? 1 : -1);
                    if (next_sens < 0) next_sens = 0;
                    if (next_sens >= NL_TP_SENS_COUNT) next_sens = NL_TP_SENS_COUNT - 1;
                    if (next_sens != prev_sens) {
                        state->settings.touchpad_sensitivity = next_sens;
                        state->anim.val_pulse_fp = 256;
                        state->anim.val_pulse_dir = (next_sens > prev_sens) ? 1 : -1;
                        sui->config_dirty = true;
                        nl_cursor_wake(state);
                        char msg[36];
                        snprintf(msg, sizeof(msg), "Sensibilite pave : %d/5", next_sens + 1);
                        nl_show_toast(state, msg, 45);
                    } else {
                        state->anim.tp_detent_pulse_fp = 192;
                    }
                    return true;
                }
                if (key == NL_KEY_EXE) {
                    state->anim.tp_detent_pulse_fp = 256;
                    return true;
                }
            } else if (sui->tp_cursor == 3) {
                if (key == NL_KEY_LEFT || key == NL_KEY_RIGHT) {
                    int prev_spd = state->settings.cursor_speed;
                    int next_spd = prev_spd + ((key == NL_KEY_RIGHT) ? 1 : -1);
                    if (next_spd < 0) next_spd = 0;
                    if (next_spd >= NL_CURSOR_SPEED_COUNT) next_spd = NL_CURSOR_SPEED_COUNT - 1;
                    if (next_spd != prev_spd) {
                        state->settings.cursor_speed = next_spd;
                        state->anim.val_pulse_fp = 256;
                        state->anim.val_pulse_dir = (next_spd > prev_spd) ? 1 : -1;
                        sui->config_dirty = true;
                        nl_cursor_wake(state);
                        char msg[36];
                        snprintf(msg, sizeof(msg), "Vitesse souris : %d/5", next_spd + 1);
                        nl_show_toast(state, msg, 45);
                    } else {
                        state->anim.tp_speed_detent_pulse_fp = 192;
                    }
                    return true;
                }
                if (key == NL_KEY_EXE) {
                    state->anim.tp_speed_detent_pulse_fp = 256;
                    return true;
                }
            } else if (sui->tp_cursor == 4) {
                if (key == NL_KEY_EXE || key == NL_KEY_RIGHT) {
                    sui->tp_tab = 1;
                    sui->tp_style_cursor = clamp_cursor_style_to_card(state->settings.touchpad_cursor_style);
                    state->anim.tp_tab_trans_fp = 32;
                    state->anim.tp_style_glow_fp = 64;
                    nl_cursor_wake(state);
                    return true;
                }
            }
            break;

        case NL_UI_SETTINGS_SHORTCUTS:
            if (sui->sc_conflict_prompt) {
                if (key == NL_KEY_ESC) {
                    sui->sc_conflict_prompt = false;
                    state->anim.modal_fp = 32;
                    return true;
                }
                if (key == NL_KEY_LEFT || key == NL_KEY_RIGHT || key == NL_KEY_UP || key == NL_KEY_DOWN) {
                    sui->sc_conflict_confirm_yes = !sui->sc_conflict_confirm_yes;
                    return true;
                }
                if (key == NL_KEY_EXE) {
                    if (sui->sc_conflict_confirm_yes) {
                        assign_shortcut_key_to_game(state, sui->sc_game_cursor, sui->sc_conflict_key, true);
                    } else {
                        sui->sc_conflict_prompt = false;
                        state->anim.modal_fp = 32;
                    }
                    return true;
                }
                return false;
            }
            if (sui->sc_picking_key) {
                const char *keys_str = NULL;
                int n_keys = nl_get_assignable_keys(&keys_str);
                int total_opts = n_keys + 1; /* slot 0 = Aucun */
                if (key == NL_KEY_ESC) {
                    sui->sc_picking_key = false;
                    return true;
                }
                if (key == NL_KEY_LEFT || key == NL_KEY_UP) {
                    sui->sc_key_cursor = (sui->sc_key_cursor > 0) ? (sui->sc_key_cursor - 1) : (total_opts - 1);
                    return true;
                }
                if (key == NL_KEY_RIGHT || key == NL_KEY_DOWN) {
                    sui->sc_key_cursor = (sui->sc_key_cursor + 1) % total_opts;
                    return true;
                }
                if (key == NL_KEY_BACKSPACE) {
                    if (sui->sc_game_cursor >= 0 && sui->sc_game_cursor < state->total_games) {
                        state->games[sui->sc_game_cursor].shortcut_key = 0;
                        sui->sc_picking_key = false;
                        sui->config_dirty = true;
                        nl_show_toast(state, "RACCOURCI SUPPRIME", 50);
                    }
                    return true;
                }
                if (key == NL_KEY_EXE) {
                    if (sui->sc_game_cursor >= 0 && sui->sc_game_cursor < state->total_games) {
                        if (sui->sc_key_cursor == 0) {
                            state->games[sui->sc_game_cursor].shortcut_key = 0;
                            sui->sc_picking_key = false;
                            sui->config_dirty = true;
                            nl_show_toast(state, "RACCOURCI SUPPRIME", 50);
                        } else {
                            char chosen = keys_str[sui->sc_key_cursor - 1];
                            assign_shortcut_key_to_game(state, sui->sc_game_cursor, chosen, false);
                        }
                    }
                    return true;
                }
                return false;
            }
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (state->total_games > 0) {
                if (key == NL_KEY_UP) {
                    int prev = sui->sc_game_cursor;
                    sui->sc_game_cursor = (sui->sc_game_cursor > 0) ? (sui->sc_game_cursor - 1) : (state->total_games - 1);
                    adjust_list_scroll(sui->sc_game_cursor, &sui->sc_game_scroll, 6);
                    settings_sync_cursor_targets(state, false, prev);
                    return true;
                }
                if (key == NL_KEY_DOWN) {
                    int prev = sui->sc_game_cursor;
                    sui->sc_game_cursor = (sui->sc_game_cursor + 1) % state->total_games;
                    adjust_list_scroll(sui->sc_game_cursor, &sui->sc_game_scroll, 6);
                    settings_sync_cursor_targets(state, false, prev);
                    return true;
                }
                if (key == NL_KEY_BACKSPACE || key == NL_KEY_RESCAN) {
                    if (state->games[sui->sc_game_cursor].shortcut_key != 0) {
                        state->games[sui->sc_game_cursor].shortcut_key = 0;
                        sui->config_dirty = true;
                        nl_show_toast(state, "RACCOURCI SUPPRIME", 50);
                        return true;
                    }
                }
                if (key == NL_KEY_EXE) {
                    const char *keys_str = NULL;
                    int n_keys = nl_get_assignable_keys(&keys_str);
                    sui->sc_picking_key = true;
                    state->anim.modal_fp = 32;
                    sui->sc_key_cursor = 1;
                    char cur_k = state->games[sui->sc_game_cursor].shortcut_key;
                    if (cur_k != 0) {
                        for (int i = 0; i < n_keys; i++) {
                            if (keys_str[i] == cur_k) {
                                sui->sc_key_cursor = i + 1;
                                break;
                            }
                        }
                    }
                    int kr = sui->sc_key_cursor / 8;
                    int kc = sui->sc_key_cursor % 8;
                    state->anim.sc_key_x_fp = (34 + kc * 31) << 4;
                    state->anim.sc_key_y_fp = (72 + kr * 26) << 4;
                    state->anim.sc_key_w_fp = ((sui->sc_key_cursor == 0) ? 28 : 26) << 4;
                    return true;
                }
            }
            break;

        case NL_UI_SETTINGS_KEY_HELP: {
            nl_shortcut_def_t defs[40];
            int total_defs = nl_get_shortcut_defs(state, defs, 40);
            int max_scroll = (total_defs > 7) ? (total_defs - 7) : 0;
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (key == NL_KEY_UP && sui->sub_scroll > 0) {
                sui->sub_scroll--;
                settings_sync_cursor_targets(state, false, -1);
                return true;
            }
            if (key == NL_KEY_DOWN && sui->sub_scroll < max_scroll) {
                sui->sub_scroll++;
                settings_sync_cursor_targets(state, false, -1);
                return true;
            }
            break;
        }

        case NL_UI_SETTINGS_SORT:
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (key == NL_KEY_UP) {
                int prev = sui->sub_cursor;
                sui->sub_cursor = (sui->sub_cursor > 0) ? (sui->sub_cursor - 1) : (NL_SORT_COUNT - 1);
                adjust_list_scroll(sui->sub_cursor, &sui->sub_scroll, 6);
                settings_sync_cursor_targets(state, false, prev);
                return true;
            }
            if (key == NL_KEY_DOWN) {
                int prev = sui->sub_cursor;
                sui->sub_cursor = (sui->sub_cursor + 1) % NL_SORT_COUNT;
                adjust_list_scroll(sui->sub_cursor, &sui->sub_scroll, 6);
                settings_sync_cursor_targets(state, false, prev);
                return true;
            }
            if (key == NL_KEY_EXE) {
                state->settings.sort_order = (nl_sort_order_t)sui->sub_cursor;
                state->selected_pos = 0;
                nl_rebuild_filter(state);
                sync_scroll_targets(state, true);
                sui->config_dirty = true;
                state->anim.val_pulse_fp = 256;
                state->anim.val_pulse_dir = 1;
                nl_show_toast(state, nl_sort_order_label(state->settings.sort_order), 50);
                return true;
            }
            break;

        case NL_UI_SETTINGS_STATS:
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (state->total_games > 0) {
                if (key == NL_KEY_UP) {
                    int prev = sui->sub_cursor;
                    sui->sub_cursor = (sui->sub_cursor > 0) ? (sui->sub_cursor - 1) : (state->total_games - 1);
                    adjust_list_scroll(sui->sub_cursor, &sui->sub_scroll, 5);
                    settings_sync_cursor_targets(state, false, prev);
                    return true;
                }
                if (key == NL_KEY_DOWN) {
                    int prev = sui->sub_cursor;
                    sui->sub_cursor = (sui->sub_cursor + 1) % state->total_games;
                    adjust_list_scroll(sui->sub_cursor, &sui->sub_scroll, 5);
                    settings_sync_cursor_targets(state, false, prev);
                    return true;
                }
            }
            break;

        case NL_UI_SETTINGS_RESET:
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (key == NL_KEY_LEFT || key == NL_KEY_RIGHT || key == NL_KEY_UP || key == NL_KEY_DOWN) {
                sui->reset_confirm_yes = !sui->reset_confirm_yes;
                return true;
            }
            if (key == NL_KEY_EXE) {
                if (sui->reset_confirm_yes) {
                    nl_reset_settings_only(state, NULL);
                    sui->reset_performed = true;
                    sui->config_dirty = true;
                    settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                    nl_show_toast(state, "PARAMETRES REINITIALISES", 65);
                } else {
                    settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                }
                return true;
            }
            break;

        case NL_UI_SETTINGS_CAPTURE_BATCH:
            if (key == NL_KEY_ESC) {
                bool had_saved = (sui->cap_captured > 0);
                nl_batch_capture_cancel(state);
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                if (had_saved) {
                    nl_show_toast(state, "CAPTURES TERMINEES CONSERVEES", 60);
                }
                return true;
            }
            if (sui->cap_finished) {
                if (key == NL_KEY_EXE) {
                    settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                    return true;
                }
            } else if (sui->cap_active) {
                if (key == NL_KEY_RIGHT || key == NL_KEY_DOWN) {
                    nl_batch_capture_advance(state, false, true, NL_CAP_IDLE);
                    return true;
                }
                if (key == NL_KEY_EXE && sui->cap_semi_auto) {
                    sui->cap_semi_auto = false;
                    sui->cap_step_req = true;
                    return true;
                }
            }
            break;

        case NL_UI_SETTINGS_ICONS:
            if (sui->icon_confirm_modal) {
                if (key == NL_KEY_ESC) {
                    sui->icon_confirm_modal = false;
                    sui->icon_confirm_yes = false;
                    return true;
                }
                if (key == NL_KEY_LEFT || key == NL_KEY_RIGHT || key == NL_KEY_UP || key == NL_KEY_DOWN) {
                    sui->icon_confirm_yes = !sui->icon_confirm_yes;
                    return true;
                }
                if (key == NL_KEY_EXE) {
                    if (sui->icon_confirm_yes && sui->icon_cursor >= 0 && sui->icon_cursor < state->total_games) {
                        nl_delete_custom_icon(&state->games[sui->icon_cursor], state->covers_dir);
                        nl_show_toast(state, "ICONE SUPPRIMEE", 55);
                    }
                    sui->icon_confirm_modal = false;
                    sui->icon_confirm_yes = false;
                    return true;
                }
                return false;
            }
            if (key == NL_KEY_ESC) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            if (state->total_games > 0) {
                if (key == NL_KEY_LEFT) {
                    sui->icon_cursor = (sui->icon_cursor > 0) ? (sui->icon_cursor - 1) : (state->total_games - 1);
                } else if (key == NL_KEY_RIGHT) {
                    sui->icon_cursor = (sui->icon_cursor + 1 < state->total_games) ? (sui->icon_cursor + 1) : 0;
                } else if (key == NL_KEY_UP) {
                    if (sui->icon_cursor >= 3) {
                        sui->icon_cursor -= 3;
                    } else {
                        int last_row_col = ((state->total_games - 1) / 3) * 3 + sui->icon_cursor;
                        sui->icon_cursor = (last_row_col < state->total_games) ? last_row_col : (state->total_games - 1);
                    }
                } else if (key == NL_KEY_DOWN) {
                    if (sui->icon_cursor + 3 < state->total_games) {
                        sui->icon_cursor += 3;
                    } else {
                        sui->icon_cursor %= 3;
                    }
                } else if (key == NL_KEY_EXE || key == NL_KEY_BACKSPACE || key == NL_KEY_RESCAN) {
                    if (state->games[sui->icon_cursor].icon_source != NL_ICON_NONE) {
                        sui->icon_confirm_modal = true;
                        sui->icon_confirm_yes = false;
                        state->anim.modal_fp = 32;
                        state->anim.modal_btn_fp = 0;
                    } else {
                        nl_show_toast(state, "Aucune icone personnalisee", 50);
                    }
                    return true;
                } else {
                    return false;
                }

                int row_cur = sui->icon_cursor / 3;
                if (row_cur < sui->icon_scroll) {
                    sui->icon_scroll = row_cur;
                } else if (row_cur >= sui->icon_scroll + 2) {
                    sui->icon_scroll = row_cur - 1;
                }
                if (sui->icon_scroll < 0) sui->icon_scroll = 0;
                return true;
            }
            break;

        case NL_UI_SETTINGS_INFO:
        case NL_UI_SETTINGS_CREDITS:
            if (key == NL_KEY_ESC || key == NL_KEY_EXE) {
                settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                return true;
            }
            break;

        case NL_UI_SETTINGS_PROFILES: {
            int pcount = state->profiles.count;
            switch (sui->prof_mode) {
                case NL_PROF_MODE_LIST: {
                    int total_rows = pcount + 1; /* row 0 = '+ Creer un profil' */
                    if (key == NL_KEY_ESC) {
                        settings_transition_screen(state, NL_UI_SETTINGS_MAIN, -1);
                        return true;
                    }
                    if (key == NL_KEY_UP) {
                        int prev = sui->prof_cursor;
                        sui->prof_cursor = (sui->prof_cursor > 0) ? (sui->prof_cursor - 1) : (total_rows - 1);
                        adjust_list_scroll(sui->prof_cursor, &sui->prof_scroll, 6);
                        settings_sync_cursor_targets(state, false, prev);
                        return true;
                    }
                    if (key == NL_KEY_DOWN) {
                        int prev = sui->prof_cursor;
                        sui->prof_cursor = (sui->prof_cursor + 1) % total_rows;
                        adjust_list_scroll(sui->prof_cursor, &sui->prof_scroll, 6);
                        settings_sync_cursor_targets(state, false, prev);
                        return true;
                    }
                    if (key == NL_KEY_EXE || key == NL_KEY_RIGHT) {
                        if (sui->prof_cursor == 0) {
                            (void)begin_profile_create_avatar_flow(state, false, 160, 56);
                            return true;
                        } else {
                            int target = sui->prof_cursor - 1;
                            if (target >= 0 && target < pcount) {
                                sui->prof_detail_idx = target;
                                sui->prof_detail_cursor = 0;
                                sui->prof_mode = NL_PROF_MODE_DETAIL;
                                settings_sync_cursor_targets(state, true, -1);
                                return true;
                            }
                        }
                    }
                    break;
                }

                case NL_PROF_MODE_DETAIL: {
                    if (sui->prof_detail_idx < 0 || sui->prof_detail_idx >= pcount) {
                        sui->prof_mode = NL_PROF_MODE_LIST;
                        return true;
                    }
                    const nl_profile_t *dp = &state->profiles.items[sui->prof_detail_idx];
                    if (key == NL_KEY_ESC || key == NL_KEY_LEFT) {
                        sui->prof_mode = NL_PROF_MODE_LIST;
                        settings_sync_cursor_targets(state, true, -1);
                        return true;
                    }
                    if (key == NL_KEY_UP) {
                        int prev = sui->prof_detail_cursor;
                        sui->prof_detail_cursor = (sui->prof_detail_cursor > 0) ? (sui->prof_detail_cursor - 1) : 3;
                        settings_sync_cursor_targets(state, false, prev);
                        return true;
                    }
                    if (key == NL_KEY_DOWN) {
                        int prev = sui->prof_detail_cursor;
                        sui->prof_detail_cursor = (sui->prof_detail_cursor + 1) % 4;
                        settings_sync_cursor_targets(state, false, prev);
                        return true;
                    }
                    if (key == NL_KEY_EXE || key == NL_KEY_RIGHT) {
                        if (sui->prof_detail_cursor == 0) {
                            snprintf(sui->prof_edit_name, sizeof(sui->prof_edit_name), "%s", dp->name);
                            sui->prof_edit_len = (int)strlen(sui->prof_edit_name);
                            sui->prof_mode = NL_PROF_MODE_EDIT_NAME;
                            return true;
                        } else if (sui->prof_detail_cursor == 1) {
                            int cat = nl_avatar_category_of(dp->avatar_id);
                            int cstart = 0, ccnt = 0;
                            nl_avatar_category_range(cat, &cstart, &ccnt);
                            sui->prof_avatar_cat = cat;
                            sui->prof_avatar_cursor = dp->avatar_id - cstart;
                            if (sui->prof_avatar_cursor < 0 || sui->prof_avatar_cursor >= ccnt) {
                                sui->prof_avatar_cursor = 0;
                            }
                            state->anim.prof_cat_trans_fp = 256;
                            sui->prof_mode = NL_PROF_MODE_EDIT_AVATAR;
                            return true;
                        } else if (sui->prof_detail_cursor == 2) {
                            if (sui->prof_detail_idx == state->profiles.active_index) {
                                nl_show_toast(state, "Profil deja actif", 45);
                            } else {
                                int av_id = dp->avatar_id;
                                nl_profile_switch_to(state, sui->prof_detail_idx);
                                trigger_profile_switch_anim(state, av_id, 37, 54, 34);
                            }
                            return true;
                        } else if (sui->prof_detail_cursor == 3) {
                            if (dp->is_primary || state->profiles.count <= 1) {
                                nl_show_toast(state, "Profil principal protege", 60);
                            } else {
                                sui->prof_delete_yes = false;
                                sui->prof_mode = NL_PROF_MODE_CONFIRM_DELETE;
                                state->anim.modal_fp = 32;
                                state->anim.modal_btn_fp = 0;
                            }
                            return true;
                        }
                    }
                    break;
                }

                case NL_PROF_MODE_EDIT_NAME:
                case NL_PROF_MODE_CREATE_NAME: {
                    bool is_create = (sui->prof_mode == NL_PROF_MODE_CREATE_NAME);
                    if (key == NL_KEY_ESC) {
                        if (is_create) {
                            if (sui->prof_from_quick) {
                                sui->prof_from_quick = false;
                                settings_close_to_library(state);
                            } else {
                                sui->prof_mode = NL_PROF_MODE_LIST;
                                settings_sync_cursor_targets(state, true, -1);
                            }
                        } else {
                            sui->prof_mode = NL_PROF_MODE_DETAIL;
                            settings_sync_cursor_targets(state, true, -1);
                        }
                        return true;
                    }
                    if (key == NL_KEY_BACKSPACE || key == NL_KEY_RESCAN) {
                        if (sui->prof_edit_len > 0) {
                            sui->prof_edit_name[--sui->prof_edit_len] = '\0';
                        }
                        return true;
                    }
                    if (key == NL_KEY_SPACE) {
                        if (sui->prof_edit_len + 1 < NL_PROFILE_NAME_LEN) {
                            sui->prof_edit_name[sui->prof_edit_len++] = ' ';
                            sui->prof_edit_name[sui->prof_edit_len] = '\0';
                        }
                        return true;
                    }
                    if (key == NL_KEY_EXE) {
                        /* Check non-empty trimmed name */
                        char tmp[NL_PROFILE_NAME_LEN];
                        snprintf(tmp, sizeof(tmp), "%s", sui->prof_edit_name);
                        int start = 0;
                        while (tmp[start] == ' ') start++;
                        if (tmp[start] == '\0') {
                            nl_show_toast(state, "Entrez un nom valide", 50);
                            return true;
                        }
                        if (is_create) {
                            int first_free = nl_avatar_first_free(state);
                            if (first_free < 0) {
                                nl_show_toast(state, "43/43 icones deja utilisees", 65);
                                return true;
                            }
                            int cat = nl_avatar_category_of(first_free);
                            int cstart = 0, ccnt = 0;
                            nl_avatar_category_range(cat, &cstart, &ccnt);
                            sui->prof_avatar_cat = cat;
                            sui->prof_avatar_cursor = first_free - cstart;
                            state->anim.prof_cat_trans_fp = 256;
                            sui->prof_mode = NL_PROF_MODE_CREATE_AVATAR;
                            return true;
                        } else {
                            if (nl_profile_rename(state, sui->prof_detail_idx, sui->prof_edit_name)) {
                                nl_show_toast(state, "Nom mis a jour", 50);
                                sui->prof_mode = NL_PROF_MODE_DETAIL;
                                settings_sync_cursor_targets(state, true, -1);
                            } else {
                                nl_show_toast(state, "Nom invalide", 50);
                            }
                            return true;
                        }
                    }
                    break;
                }

                case NL_PROF_MODE_EDIT_AVATAR:
                case NL_PROF_MODE_CREATE_AVATAR: {
                    bool is_create = (sui->prof_mode == NL_PROF_MODE_CREATE_AVATAR);
                    int cat = sui->prof_avatar_cat;
                    if (cat < 0 || cat >= NL_AVATAR_CAT_COUNT) cat = 0;
                    int cstart = 0, ccnt = 0;
                    nl_avatar_category_range(cat, &cstart, &ccnt);
                    int cols = (cat == 0) ? 8 : 5;

                    if (key == NL_KEY_ESC) {
                        if (is_create) {
                            if (sui->prof_from_quick) {
                                sui->prof_from_quick = false;
                                sui->prof_mode = NL_PROF_MODE_LIST;
                                settings_close_to_library(state);
                                state->sui.qp_open = true;
                                state->anim.qp_open_fp = 256;
                            } else {
                                sui->prof_mode = NL_PROF_MODE_LIST;
                                settings_sync_cursor_targets(state, true, -1);
                            }
                        } else {
                            sui->prof_mode = NL_PROF_MODE_DETAIL;
                            settings_sync_cursor_targets(state, true, -1);
                        }
                        return true;
                    }
                    if (key == NL_KEY_TAB || key == NL_KEY_PAGE_NEXT) {
                        sui->prof_avatar_cat = (cat + 1) % NL_AVATAR_CAT_COUNT;
                        sui->prof_avatar_cursor = 0;
                        state->anim.prof_cat_dir = 1;
                        state->anim.prof_cat_trans_fp = 48;
                        return true;
                    }
                    if (key == NL_KEY_PAGE_PREV) {
                        sui->prof_avatar_cat = (cat + NL_AVATAR_CAT_COUNT - 1) % NL_AVATAR_CAT_COUNT;
                        sui->prof_avatar_cursor = 0;
                        state->anim.prof_cat_dir = -1;
                        state->anim.prof_cat_trans_fp = 48;
                        return true;
                    }
                    if (key == NL_KEY_LEFT) {
                        if (sui->prof_avatar_cursor > 0) {
                            sui->prof_avatar_cursor--;
                        } else {
                            sui->prof_avatar_cat = (cat + NL_AVATAR_CAT_COUNT - 1) % NL_AVATAR_CAT_COUNT;
                            int nc_start = 0, nc_cnt = 1;
                            nl_avatar_category_range(sui->prof_avatar_cat, &nc_start, &nc_cnt);
                            sui->prof_avatar_cursor = nc_cnt - 1;
                            state->anim.prof_cat_dir = -1;
                            state->anim.prof_cat_trans_fp = 48;
                        }
                        return true;
                    }
                    if (key == NL_KEY_RIGHT) {
                        if (sui->prof_avatar_cursor + 1 < ccnt) {
                            sui->prof_avatar_cursor++;
                        } else {
                            sui->prof_avatar_cat = (cat + 1) % NL_AVATAR_CAT_COUNT;
                            sui->prof_avatar_cursor = 0;
                            state->anim.prof_cat_dir = 1;
                            state->anim.prof_cat_trans_fp = 48;
                        }
                        return true;
                    }
                    if (key == NL_KEY_UP) {
                        if (sui->prof_avatar_cursor >= cols) {
                            sui->prof_avatar_cursor -= cols;
                        } else {
                            sui->prof_avatar_cat = (cat + NL_AVATAR_CAT_COUNT - 1) % NL_AVATAR_CAT_COUNT;
                            sui->prof_avatar_cursor = 0;
                            state->anim.prof_cat_dir = -1;
                            state->anim.prof_cat_trans_fp = 48;
                        }
                        return true;
                    }
                    if (key == NL_KEY_DOWN) {
                        if (sui->prof_avatar_cursor + cols < ccnt) {
                            sui->prof_avatar_cursor += cols;
                        } else if ((sui->prof_avatar_cursor / cols) < ((ccnt - 1) / cols)) {
                            sui->prof_avatar_cursor = ccnt - 1;
                        } else {
                            sui->prof_avatar_cat = (cat + 1) % NL_AVATAR_CAT_COUNT;
                            sui->prof_avatar_cursor = 0;
                            state->anim.prof_cat_dir = 1;
                            state->anim.prof_cat_trans_fp = 48;
                        }
                        return true;
                    }
                    if (key == NL_KEY_EXE) {
                        int avatar_id = cstart + sui->prof_avatar_cursor;
                        int excl = is_create ? -1 : sui->prof_detail_idx;
                        if (nl_avatar_find_owner(state, avatar_id, excl) >= 0) {
                            nl_show_toast(state, "Icone deja utilisee", 55);
                            return true;
                        }
                        if (is_create) {
                            int new_idx = nl_profile_create(state, sui->prof_edit_name, avatar_id);
                            if (new_idx >= 0) {
                                char msg[NL_MAX_TOAST_LEN];
                                snprintf(msg, sizeof(msg), "Profil %.*s cree", 24, state->profiles.items[new_idx].name);
                                nl_show_toast(state, msg, 60);
                                state->anim.prof_birth_active = true;
                                state->anim.prof_birth_tick = 0;
                                state->anim.prof_birth_max = 16;
                                state->anim.prof_birth_avatar_id = avatar_id;
                                state->anim.prof_birth_slot = new_idx;
                                if (sui->prof_from_quick) {
                                    sui->prof_from_quick = false;
                                    sui->prof_mode = NL_PROF_MODE_LIST;
                                    settings_close_to_library(state);
                                    state->sui.qp_open = true;
                                    state->sui.qp_cursor = new_idx;
                                    state->anim.qp_open_fp = 256;
                                    state->anim.qp_sel_bounce_fp = 256;
                                } else {
                                    sui->prof_mode = NL_PROF_MODE_LIST;
                                    sui->prof_cursor = new_idx + 1;
                                    adjust_list_scroll(sui->prof_cursor, &sui->prof_scroll, 6);
                                    settings_sync_cursor_targets(state, true, -1);
                                }
                            } else {
                                nl_show_toast(state, "Impossible de creer", 55);
                            }
                        } else {
                            if (nl_profile_set_avatar(state, sui->prof_detail_idx, avatar_id)) {
                                nl_show_toast(state, "Icone mise a jour", 50);
                                sui->prof_mode = NL_PROF_MODE_DETAIL;
                                settings_sync_cursor_targets(state, true, -1);
                            } else {
                                nl_show_toast(state, "Icone deja utilisee", 55);
                            }
                        }
                        return true;
                    }
                    break;
                }

                case NL_PROF_MODE_CONFIRM_DELETE: {
                    if (key == NL_KEY_ESC) {
                        sui->prof_mode = NL_PROF_MODE_DETAIL;
                        state->anim.modal_fp = 0;
                        settings_sync_cursor_targets(state, true, -1);
                        return true;
                    }
                    if (key == NL_KEY_LEFT || key == NL_KEY_RIGHT ||
                        key == NL_KEY_UP   || key == NL_KEY_DOWN) {
                        sui->prof_delete_yes = !sui->prof_delete_yes;
                        return true;
                    }
                    if (key == NL_KEY_EXE) {
                        if (sui->prof_delete_yes) {
                            if (nl_profile_delete(state, sui->prof_detail_idx)) {
                                nl_show_toast(state, "Profil supprime", 55);
                                sui->prof_cursor = (state->profiles.active_index >= 0) ? (state->profiles.active_index + 1) : 1;
                                sui->prof_scroll = 0;
                                adjust_list_scroll(sui->prof_cursor, &sui->prof_scroll, 6);
                                sui->prof_mode = NL_PROF_MODE_LIST;
                            } else {
                                nl_show_toast(state, "Suppression refusee", 55);
                                sui->prof_mode = NL_PROF_MODE_DETAIL;
                            }
                        } else {
                            sui->prof_mode = NL_PROF_MODE_DETAIL;
                        }
                        state->anim.modal_fp = 0;
                        settings_sync_cursor_targets(state, true, -1);
                        return true;
                    }
                    break;
                }
            }
            break;
        }

        default:
            break;
    }

    return false;
}

bool nl_handle_char_input(nl_state_t *state, char ch) {
    if (!state || ch == 0) return false;
    if (state->anim.launch_anim_active) return false;

    if (state->sui.screen == NL_UI_SETTINGS_CAPTURE_BATCH && state->sui.cap_active) {
        char u = (char)toupper((unsigned char)ch);
        if (u == 'S') {
            nl_batch_capture_advance(state, false, true, NL_CAP_IDLE);
            return true;
        }
    }

    if (state->sui.screen == NL_UI_SETTINGS_SHORTCUTS &&
        state->sui.sc_picking_key &&
        !state->sui.sc_conflict_prompt) {
        assign_shortcut_key_to_game(state, state->sui.sc_game_cursor, ch, false);
        return true;
    }

    if (state->sui.screen == NL_UI_SETTINGS_PROFILES &&
        (state->sui.prof_mode == NL_PROF_MODE_EDIT_NAME ||
         state->sui.prof_mode == NL_PROF_MODE_CREATE_NAME ||
         state->sui.prof_mode == NL_PROF_MODE_CREATE_AVATAR)) {
        if (ch == '\b' || ch == 0x7F) {
            return nl_handle_key(state, NL_KEY_BACKSPACE);
        }
        unsigned char uc = (unsigned char)ch;
        if ((isalnum(uc) || uc == ' ' || uc == '-' || uc == '_') &&
            state->sui.prof_edit_len + 1 < NL_PROFILE_NAME_LEN) {
            state->sui.prof_edit_name[state->sui.prof_edit_len++] = (char)uc;
            state->sui.prof_edit_name[state->sui.prof_edit_len] = '\0';
            return true;
        }
        return false;
    }

    if (state->sui.screen == NL_UI_LIBRARY) {
        if (state->sui.qp_open) {
            if (ch == '.') {
                return nl_handle_key(state, NL_KEY_PROFILE_QUICK);
            }
            if (ch == '0') {
                return nl_handle_key(state, NL_KEY_PROFILE_PREV);
            }
            if (ch == '-') {
                return nl_handle_key(state, NL_KEY_PROFILE_NEXT);
            }
            return false;
        }
        if (ch == '5') {
            return nl_handle_key(state, NL_KEY_EXE);
        }
        if (state->sui.search_active) {
            if (ch == '\b' || ch == 0x7F) {
                return nl_handle_key(state, NL_KEY_BACKSPACE);
            }
            unsigned char uc = (unsigned char)ch;
            if ((isalnum(uc) || uc == ' ' || uc == '-' || uc == '_') &&
                state->sui.search_len + 1 < NL_MAX_SEARCH_LEN) {
                state->sui.search_query[state->sui.search_len++] = (char)uc;
                state->sui.search_query[state->sui.search_len] = '\0';
                state->selected_pos = 0;
                nl_rebuild_filter(state);
                sync_scroll_targets(state, true);
                return true;
            }
            return false;
        }

        if (ch == '.') {
            return nl_handle_key(state, NL_KEY_PROFILE_QUICK);
        }
        if (ch == '0') {
            return nl_handle_key(state, NL_KEY_PROFILE_PREV);
        }
        if (ch == '-') {
            return nl_handle_key(state, NL_KEY_PROFILE_NEXT);
        }

        char u = (char)toupper((unsigned char)ch);
        if (nl_shortcut_is_assignable(u)) {
            int game_idx = nl_shortcut_find_owner(state, u, -1);
            if (game_idx >= 0) {
                trigger_game_launch(state, game_idx);
                return true;
            }
        }
        unsigned char uc = (unsigned char)ch;
        if (isalnum(uc) || uc == '_') {
            state->sui.search_active = true;
            state->sui.search_query[0] = (char)uc;
            state->sui.search_query[1] = '\0';
            state->sui.search_len = 1;
            state->selected_pos = 0;
            nl_rebuild_filter(state);
            sync_scroll_targets(state, true);
            return true;
        }
    }

    return false;
}

static bool handle_key_inner(nl_state_t *state, nl_key_t key) {
    if (!state || key == NL_KEY_NONE) return false;

    /* Prevent double-triggering launch while launch animation runs */
    if (state->anim.launch_anim_active) {
        return false;
    }

    if (state->sui.screen != NL_UI_LIBRARY) {
        return handle_settings_key(state, key);
    }

    /* Quick Profile Selector modal overlay on Library (STRICTLY browse/switch/'+' only — NO edit/delete) */
    if (state->sui.qp_open) {
        int total_slots = (state->profiles.count > 0 ? state->profiles.count : 1) + 1;
        switch (key) {
            case NL_KEY_ESC:
            case NL_KEY_PROFILE_QUICK:
                state->sui.qp_open = false;
                return true;
            case NL_KEY_LEFT:
                state->sui.qp_cursor = (state->sui.qp_cursor > 0) ? (state->sui.qp_cursor - 1) : (total_slots - 1);
                state->anim.qp_sel_bounce_fp = 256;
                return true;
            case NL_KEY_RIGHT:
                state->sui.qp_cursor = (state->sui.qp_cursor + 1) % total_slots;
                state->anim.qp_sel_bounce_fp = 256;
                return true;
            case NL_KEY_PROFILE_PREV:
                if (nl_profile_step(state, -1)) {
                    state->sui.qp_cursor = state->profiles.active_index;
                    state->anim.qp_sel_bounce_fp = 256;
                    return true;
                }
                return false;
            case NL_KEY_PROFILE_NEXT:
                if (nl_profile_step(state, +1)) {
                    state->sui.qp_cursor = state->profiles.active_index;
                    state->anim.qp_sel_bounce_fp = 256;
                    return true;
                }
                return false;
            case NL_KEY_EXE:
                if (state->sui.qp_cursor < state->profiles.count) {
                    int sel_slot = state->sui.qp_cursor;
                    int orig_x = compute_qp_slot_center_x(state, sel_slot);
                    int av_id = state->profiles.items[sel_slot].avatar_id;
                    if (sel_slot != state->profiles.active_index) {
                        nl_profile_switch_to(state, sel_slot);
                    }
                    state->sui.qp_open = false;
                    state->anim.qp_open_fp = 0;
                    trigger_profile_switch_anim(state, av_id, orig_x, 114, 38);
                    return true;
                } else {
                    int plus_x = compute_qp_slot_center_x(state, state->sui.qp_cursor);
                    (void)begin_profile_create_avatar_flow(state, true, plus_x, 114);
                    return true;
                }
            case NL_KEY_DOC:
                state->display_mode = (nl_display_mode_t)(((unsigned)state->display_mode + 1U) % NL_DISPLAY_COUNT);
                state->anim.ink_active = true;
                state->anim.ink_tick = 0;
                state->anim.ink_max = NL_INK_TRANS_TICKS;
                state->anim.ink_seq++;
                return true;
            case NL_KEY_MENU:
                state->accent_color = (nl_accent_color_t)(((unsigned)state->accent_color + 1U) % NL_ACCENT_COUNT);
                state->anim.ink_active = true;
                state->anim.ink_tick = 0;
                state->anim.ink_max = NL_INK_TRANS_TICKS;
                state->anim.ink_seq++;
                return true;
            default:
                return false;
        }
    }

    nl_layout_metrics_t m;
    nl_get_layout_metrics_for_count(state->settings.layout, state->settings.show_names, state->settings.show_shortcuts, state->filtered_count, &m);

    switch (key) {
        case NL_KEY_PROFILE_QUICK:
            if (!state->sui.search_active) {
                state->sui.qp_open = true;
                state->sui.qp_cursor = (state->profiles.active_index >= 0 &&
                                        state->profiles.active_index < state->profiles.count)
                    ? state->profiles.active_index : 0;
                state->anim.qp_open_fp = 24;
                state->anim.qp_glow_x_fp = 0;
                state->anim.qp_glow_y_fp = 0;
                state->anim.qp_trail_x_fp = 0;
                state->anim.qp_sel_bounce_fp = 256;
                return true;
            }
            break;

        case NL_KEY_PROFILE_PREV:
            if (!state->sui.search_active) {
                if (nl_profile_step(state, -1)) {
                    const nl_profile_t *ap = nl_profile_get_active(state);
                    trigger_profile_switch_anim(state, ap ? ap->avatar_id : 0, 17, 12, 18);
                    return true;
                }
            }
            break;

        case NL_KEY_PROFILE_NEXT:
            if (!state->sui.search_active) {
                if (nl_profile_step(state, +1)) {
                    const nl_profile_t *ap = nl_profile_get_active(state);
                    trigger_profile_switch_anim(state, ap ? ap->avatar_id : 0, 17, 12, 18);
                    return true;
                }
            }
            break;

        case NL_KEY_SETTINGS:
            state->sui.screen = NL_UI_SETTINGS_MAIN;
            state->sui.menu_cursor = 0;
            state->sui.menu_scroll = 0;
            state->anim.set_last_screen = NL_UI_SETTINGS_MAIN;
            state->anim.set_prev_screen = NL_UI_LIBRARY;
            state->anim.set_sec_fp = 256;
            state->anim.set_open_fp = (state->anim.set_open_fp > 48) ? state->anim.set_open_fp : 48;
            settings_sync_cursor_targets(state, true, -1);
            return true;

        case NL_KEY_LEFT:
            if (state->filtered_count > 0) {
                if (state->selected_pos > 0) {
                    state->selected_pos--;
                } else {
                    state->selected_pos = state->filtered_count - 1;
                }
                on_selection_changed(state);
                return true;
            }
            break;

        case NL_KEY_RIGHT:
            if (state->filtered_count > 0) {
                if (state->selected_pos + 1 < state->filtered_count) {
                    state->selected_pos++;
                } else {
                    state->selected_pos = 0;
                }
                on_selection_changed(state);
                return true;
            }
            break;

        case NL_KEY_UP:
            if (state->filtered_count > 0) {
                if (state->selected_pos >= m.cols) {
                    state->selected_pos -= m.cols;
                    on_selection_changed(state);
                    return true;
                }
            }
            break;

        case NL_KEY_DOWN:
            if (state->filtered_count > 0) {
                if (state->selected_pos + m.cols < state->filtered_count) {
                    state->selected_pos += m.cols;
                } else {
                    int row_cur = state->selected_pos / m.cols;
                    int last_row = (state->filtered_count - 1) / m.cols;
                    if (row_cur < last_row) {
                        state->selected_pos = state->filtered_count - 1;
                    } else {
                        break;
                    }
                }
                on_selection_changed(state);
                return true;
            }
            break;

        case NL_KEY_PAGE_PREV:
            if (state->filtered_count > 0 && state->total_pages > 1) {
                int slot = state->selected_pos % m.per_page;
                int new_page = (state->current_page > 0) ? (state->current_page - 1) : (state->total_pages - 1);
                int new_pos = new_page * m.per_page + slot;
                if (new_pos >= state->filtered_count) new_pos = state->filtered_count - 1;
                state->selected_pos = new_pos;
                on_selection_changed(state);
                return true;
            }
            break;

        case NL_KEY_PAGE_NEXT:
            if (state->filtered_count > 0 && state->total_pages > 1) {
                int slot = state->selected_pos % m.per_page;
                int new_page = (state->current_page + 1 < state->total_pages) ? (state->current_page + 1) : 0;
                int new_pos = new_page * m.per_page + slot;
                if (new_pos >= state->filtered_count) new_pos = state->filtered_count - 1;
                state->selected_pos = new_pos;
                on_selection_changed(state);
                return true;
            }
            break;

        case NL_KEY_TAB:
            state->anim.cat_prev = state->active_category;
            state->active_category = (nl_category_t)((state->active_category + 1) % NL_CAT_COUNT);
            state->anim.cat_trans_dir = 1;
            state->anim.cat_trans_fp = 48;
            state->selected_pos = 0;
            nl_rebuild_filter(state);
            state->anim.scroll_y_fp = 0;
            sync_scroll_targets(state, true);
            return true;

        case NL_KEY_BACKSPACE:
            if (state->sui.search_active) {
                if (state->sui.search_len > 0) {
                    state->sui.search_query[--state->sui.search_len] = '\0';
                    state->selected_pos = 0;
                    nl_rebuild_filter(state);
                    sync_scroll_targets(state, true);
                }
                return true;
            }
            break;

        case NL_KEY_SPACE:
            if (state->sui.search_active && state->sui.search_len + 1 < NL_MAX_SEARCH_LEN) {
                state->sui.search_query[state->sui.search_len++] = ' ';
                state->sui.search_query[state->sui.search_len] = '\0';
                state->selected_pos = 0;
                nl_rebuild_filter(state);
                sync_scroll_targets(state, true);
                return true;
            }
            break;

        case NL_KEY_FAV:
            nl_toggle_favorite(state);
            return true;

        case NL_KEY_EXE:
            if (state->filtered_count > 0) {
                trigger_game_launch(state, state->filtered_indices[state->selected_pos]);
                return true;
            }
            break;

        case NL_KEY_ESC:
            if (state->sui.search_active) {
                state->sui.search_active = false;
                state->sui.search_query[0] = '\0';
                state->sui.search_len = 0;
                state->selected_pos = 0;
                nl_rebuild_filter(state);
                sync_scroll_targets(state, true);
                return true;
            }
            if (state->active_category != NL_CAT_ALL) {
                state->anim.cat_prev = state->active_category;
                state->active_category = NL_CAT_ALL;
                state->anim.cat_trans_dir = -1;
                state->anim.cat_trans_fp = 48;
                state->selected_pos = 0;
                nl_rebuild_filter(state);
                sync_scroll_targets(state, true);
                return true;
            }
            state->anim.ink_active = false;
            state->should_exit = true;
            return true;

        case NL_KEY_DOC:
            state->display_mode = (nl_display_mode_t)(((unsigned)state->display_mode + 1U) % NL_DISPLAY_COUNT);
            state->anim.ink_active = true;
            state->anim.ink_tick = 0;
            state->anim.ink_max = NL_INK_TRANS_TICKS;
            state->anim.ink_seq++;
            return true;

        case NL_KEY_MENU:
            state->accent_color = (nl_accent_color_t)(((unsigned)state->accent_color + 1U) % NL_ACCENT_COUNT);
            state->anim.ink_active = true;
            state->anim.ink_tick = 0;
            state->anim.ink_max = NL_INK_TRANS_TICKS;
            state->anim.ink_seq++;
            return true;

        case NL_KEY_RESCAN:
            nl_rebuild_filter(state);
            sync_scroll_targets(state, true);
            nl_show_toast(state, "BIBLIOTHEQUE ACTUALISEE", 90);
            return true;

        default:
            break;
    }

    return false;
}

bool nl_handle_key(nl_state_t *state, nl_key_t key) {
    bool handled = handle_key_inner(state, key);
    if (handled && state) {
        nl_mouse_update_hover(state);
        nl_cursor_wake(state);
    }
    return handled;
}

bool nl_mouse_click(nl_state_t *state) {
    if (!state || state->anim.launch_anim_active) return false;
    nl_mouse_update_hover(state);
    nl_settings_ui_t *sui = &state->sui;
    if (!sui->hover_valid || sui->hover_kind == NL_HOVER_NONE) {
        return false;
    }

    bool ok = false;
    nl_hover_kind_t hk = sui->hover_kind;
    int hi = sui->hover_index;
    int hs = sui->hover_sub;

    switch (hk) {
        case NL_HOVER_PROFILE_BADGE:
            ok = handle_key_inner(state, NL_KEY_PROFILE_QUICK);
            break;

        case NL_HOVER_CATEGORY:
            if (hi >= 0 && hi < NL_CAT_COUNT && (nl_category_t)hi != state->active_category) {
                state->anim.cat_prev = state->active_category;
                state->anim.cat_trans_dir = ((nl_category_t)hi > state->active_category) ? 1 : -1;
                state->active_category = (nl_category_t)hi;
                state->anim.cat_trans_fp = 48;
                state->selected_pos = 0;
                nl_rebuild_filter(state);
                state->anim.scroll_y_fp = 0;
                sync_scroll_targets(state, true);
                ok = true;
            }
            break;

        case NL_HOVER_FAV_BTN:
            if (state->filtered_count > 0) {
                nl_toggle_favorite(state);
                ok = true;
            }
            break;

        case NL_HOVER_SETTINGS_BTN:
            ok = handle_key_inner(state, NL_KEY_SETTINGS);
            break;

        case NL_HOVER_SEARCH_CLOSE:
            if (sui->search_active) {
                ok = handle_key_inner(state, NL_KEY_ESC);
            }
            break;

        case NL_HOVER_SEARCH_BAR:
            ok = true;
            break;

        case NL_HOVER_PAGE_PREV:
            ok = handle_key_inner(state, NL_KEY_PAGE_PREV);
            break;

        case NL_HOVER_PAGE_NEXT:
            ok = handle_key_inner(state, NL_KEY_PAGE_NEXT);
            break;

        case NL_HOVER_PAGE_DOT:
            if (state->filtered_count > 0 && state->total_pages > 1 && hi >= 0 && hi < state->total_pages) {
                nl_layout_metrics_t lm;
                nl_get_layout_metrics_for_count(
                    state->settings.layout, state->settings.show_names,
                    state->settings.show_shortcuts, state->filtered_count, &lm
                );
                int new_pos = hi * lm.per_page;
                if (new_pos >= state->filtered_count) new_pos = state->filtered_count - 1;
                state->selected_pos = new_pos;
                on_selection_changed(state);
                ok = true;
            }
            break;

        case NL_HOVER_GAME_CARD:
            if (hi >= 0 && hi < state->filtered_count) {
                state->selected_pos = hi;
                on_selection_changed(state);
                trigger_game_launch(state, state->filtered_indices[state->selected_pos]);
                ok = true;
            }
            break;

        case NL_HOVER_QP_PREV:
            if (sui->qp_open) {
                ok = handle_key_inner(state, NL_KEY_LEFT);
            }
            break;

        case NL_HOVER_QP_NEXT:
            if (sui->qp_open) {
                ok = handle_key_inner(state, NL_KEY_RIGHT);
            }
            break;

        case NL_HOVER_QP_OUTSIDE:
            if (sui->qp_open) {
                sui->qp_open = false;
                ok = true;
            }
            break;

        case NL_HOVER_QP_SLOT:
            if (sui->qp_open) {
                int pcount = (state->profiles.count > 0) ? state->profiles.count : 1;
                int total_slots = pcount + 1;
                if (hi >= 0 && hi < total_slots) {
                    sui->qp_cursor = hi;
                    ok = handle_key_inner(state, NL_KEY_EXE);
                }
            }
            break;

        case NL_HOVER_SET_ESC:
            ok = handle_key_inner(state, NL_KEY_ESC);
            break;

        case NL_HOVER_SET_POWER:
            if (sui->screen != NL_UI_LIBRARY &&
                sui->screen != NL_UI_SETTINGS_SHORTCUTS &&
                sui->screen != NL_UI_SETTINGS_ICONS &&
                sui->screen != NL_UI_SETTINGS_PROFILES) {
                request_calculator_poweroff(state);
                ok = true;
            }
            break;

        case NL_HOVER_SET_TAB:
            if (sui->screen == NL_UI_SETTINGS_TOUCHPAD && (hi == 0 || hi == 1) && sui->tp_tab != hi) {
                sui->tp_tab = hi;
                sui->tp_style_cursor = clamp_cursor_style_to_card(state->settings.touchpad_cursor_style);
                state->anim.tp_tab_trans_fp = 32;
                state->anim.tp_style_glow_fp = 64;
                ok = true;
            }
            break;

        case NL_HOVER_SET_SLIDER_NOTCH:
            if (sui->screen == NL_UI_SETTINGS_TOUCHPAD && sui->tp_tab == 0) {
                int prev_c = sui->tp_cursor;
                if (hi == 2 && hs >= 0 && hs < NL_TP_SENS_COUNT) {
                    sui->tp_cursor = 2;
                    settings_sync_cursor_targets(state, false, prev_c);
                    state->settings.touchpad_sensitivity = hs;
                    state->anim.tp_detent_pulse_fp = 256;
                    state->anim.val_pulse_fp = 256;
                    sui->config_dirty = true;
                    char msg[36];
                    snprintf(msg, sizeof(msg), "Sensibilite pave : %d/5", hs + 1);
                    nl_show_toast(state, msg, 45);
                    ok = true;
                } else if (hi == 3 && hs >= 0 && hs < NL_CURSOR_SPEED_COUNT) {
                    sui->tp_cursor = 3;
                    settings_sync_cursor_targets(state, false, prev_c);
                    state->settings.cursor_speed = hs;
                    state->anim.tp_speed_detent_pulse_fp = 256;
                    state->anim.val_pulse_fp = 256;
                    sui->config_dirty = true;
                    char msg[36];
                    snprintf(msg, sizeof(msg), "Vitesse souris : %d/5", hs + 1);
                    nl_show_toast(state, msg, 45);
                    ok = true;
                }
            }
            break;

        case NL_HOVER_SET_STYLE_CARD:
            if (sui->screen == NL_UI_SETTINGS_TOUCHPAD && sui->tp_tab == 1 && hi >= 0 && hi < 3) {
                sui->tp_style_cursor = hi;
                ok = handle_key_inner(state, NL_KEY_EXE);
            }
            break;

        case NL_HOVER_SET_ROW:
            if (sui->screen == NL_UI_SETTINGS_MAIN && hi >= 0 && hi < NL_SET_MENU_COUNT) {
                sui->menu_cursor = hi;
                adjust_list_scroll(sui->menu_cursor, &sui->menu_scroll, 6);
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_HOTKEY) {
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_DISPLAY && hi >= 0 && hi < NL_DISPLAY_OPT_COUNT) {
                sui->display_cursor = hi;
                adjust_list_scroll(sui->display_cursor, &sui->display_scroll, 3);
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_TOUCHPAD && sui->tp_tab == 0 && hi >= 0 && hi < 5) {
                int prev_c = sui->tp_cursor;
                sui->tp_cursor = hi;
                settings_sync_cursor_targets(state, false, prev_c);
                if (hi == 2) {
                    int next_s = (state->settings.touchpad_sensitivity + 1) % NL_TP_SENS_COUNT;
                    state->settings.touchpad_sensitivity = next_s;
                    state->anim.tp_detent_pulse_fp = 256;
                    sui->config_dirty = true;
                    char msg[36];
                    snprintf(msg, sizeof(msg), "Sensibilite pave : %d/5", next_s + 1);
                    nl_show_toast(state, msg, 45);
                    ok = true;
                } else if (hi == 3) {
                    int next_v = (state->settings.cursor_speed + 1) % NL_CURSOR_SPEED_COUNT;
                    state->settings.cursor_speed = next_v;
                    state->anim.tp_speed_detent_pulse_fp = 256;
                    sui->config_dirty = true;
                    char msg[36];
                    snprintf(msg, sizeof(msg), "Vitesse souris : %d/5", next_v + 1);
                    nl_show_toast(state, msg, 45);
                    ok = true;
                } else {
                    ok = handle_key_inner(state, NL_KEY_EXE);
                }
            } else if (sui->screen == NL_UI_SETTINGS_SHORTCUTS && hi >= 0 && hi < state->total_games) {
                sui->sc_game_cursor = hi;
                adjust_list_scroll(sui->sc_game_cursor, &sui->sc_game_scroll, 6);
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_SORT && hi >= 0 && hi < NL_SORT_COUNT) {
                sui->sub_cursor = hi;
                adjust_list_scroll(sui->sub_cursor, &sui->sub_scroll, 6);
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_STATS && hi >= 0 && hi < state->total_games) {
                int prev_c = sui->sub_cursor;
                sui->sub_cursor = hi;
                adjust_list_scroll(sui->sub_cursor, &sui->sub_scroll, 5);
                settings_sync_cursor_targets(state, false, prev_c);
                ok = true;
            } else if (sui->screen == NL_UI_SETTINGS_CAPTURE_BATCH) {
                if (sui->cap_active && sui->cap_total > 0) {
                    ok = handle_key_inner(state, (hi == 0) ? NL_KEY_RIGHT : NL_KEY_ESC);
                } else {
                    ok = handle_key_inner(state, NL_KEY_EXE);
                }
            }
            break;

        case NL_HOVER_SC_KEY_CELL:
            if (sui->screen == NL_UI_SETTINGS_SHORTCUTS && sui->sc_picking_key) {
                sui->sc_key_cursor = hi;
                ok = handle_key_inner(state, NL_KEY_EXE);
            }
            break;

        case NL_HOVER_ICON_CARD:
            if (sui->screen == NL_UI_SETTINGS_ICONS && hi >= 0 && hi < state->total_games) {
                sui->icon_cursor = hi;
                ok = handle_key_inner(state, NL_KEY_EXE);
            }
            break;

        case NL_HOVER_PROF_ROW:
            if (sui->screen == NL_UI_SETTINGS_PROFILES && sui->prof_mode == NL_PROF_MODE_LIST) {
                sui->prof_cursor = hi;
                adjust_list_scroll(sui->prof_cursor, &sui->prof_scroll, 6);
                ok = handle_key_inner(state, NL_KEY_EXE);
            }
            break;

        case NL_HOVER_PROF_DETAIL_ROW:
            if (sui->screen == NL_UI_SETTINGS_PROFILES && sui->prof_mode == NL_PROF_MODE_DETAIL && hi >= 0 && hi < 4) {
                sui->prof_detail_cursor = hi;
                ok = handle_key_inner(state, NL_KEY_EXE);
            }
            break;

        case NL_HOVER_PROF_AV_TAB:
            if (sui->screen == NL_UI_SETTINGS_PROFILES &&
                (sui->prof_mode == NL_PROF_MODE_EDIT_AVATAR || sui->prof_mode == NL_PROF_MODE_CREATE_AVATAR) &&
                hi >= 0 && hi < NL_AVATAR_CAT_COUNT) {
                state->anim.prof_cat_dir = (hi >= sui->prof_avatar_cat) ? 1 : -1;
                sui->prof_avatar_cat = hi;
                sui->prof_avatar_cursor = 0;
                state->anim.prof_cat_trans_fp = 48;
                ok = true;
            }
            break;

        case NL_HOVER_PROF_AV_CELL:
            if (sui->screen == NL_UI_SETTINGS_PROFILES &&
                (sui->prof_mode == NL_PROF_MODE_EDIT_AVATAR || sui->prof_mode == NL_PROF_MODE_CREATE_AVATAR)) {
                sui->prof_avatar_cursor = hi;
                ok = handle_key_inner(state, NL_KEY_EXE);
            }
            break;

        case NL_HOVER_MODAL_BTN:
            if (sui->screen == NL_UI_SETTINGS_RESET) {
                sui->reset_confirm_yes = (hi == 1);
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_ICONS && sui->icon_confirm_modal) {
                sui->icon_confirm_yes = (hi == 1);
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_SHORTCUTS && sui->sc_conflict_prompt) {
                sui->sc_conflict_confirm_yes = (hi == 1);
                ok = handle_key_inner(state, NL_KEY_EXE);
            } else if (sui->screen == NL_UI_SETTINGS_PROFILES && sui->prof_mode == NL_PROF_MODE_CONFIRM_DELETE) {
                sui->prof_delete_yes = (hi == 1);
                ok = handle_key_inner(state, NL_KEY_EXE);
            }
            break;

        default:
            break;
    }

    if (ok) {
        nl_mouse_update_hover(state);
        nl_cursor_wake(state);
    }
    return ok;
}
