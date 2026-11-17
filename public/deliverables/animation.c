/****************************************************************************
 * N-Launcher — animation.c
 * Interruptible Console Animations (Pure Integer Fixed-Point Math):
 *  - Startup splash progression
 *  - Smooth vertical page scrolling (scroll_y_fp -> target_scroll_y_fp)
 *  - Per-card scale-up selection (1.00x <-> 1.094x, zero frames/corners)
 *  - Smooth active pagination dot glide
 *  - Toast slide-down timer
 ****************************************************************************/

#include "nlauncher.h"
#include <stdlib.h>

bool nl_anim_step(nl_state_t *state) {
    if (!state) return false;
    bool dirty = false;

    /* 1. Startup splash progression */
    if (state->anim.splash_tick < state->anim.splash_max) {
        state->anim.splash_tick++;
        dirty = true;
    }

    /* 2. Smooth vertical page scroll ease-out (1/16 px fixed-point) */
    int s_diff = state->anim.target_scroll_y_fp - state->anim.scroll_y_fp;
    if (s_diff != 0) {
        int step = (s_diff * 5) / 16;
        if (step == 0) step = (s_diff > 0) ? 1 : -1;
        if (abs(s_diff) <= 4) {
            state->anim.scroll_y_fp = state->anim.target_scroll_y_fp;
        } else {
            state->anim.scroll_y_fp += step;
        }
        dirty = true;
    }

    /* 3. Smooth pagination dot interpolation */
    int d_diff = state->anim.target_dot_fp - state->anim.dot_pos_fp;
    if (d_diff != 0) {
        int step = (d_diff * 6) / 16;
        if (step == 0) step = (d_diff > 0) ? 1 : -1;
        if (abs(d_diff) <= 1) {
            state->anim.dot_pos_fp = state->anim.target_dot_fp;
        } else {
            state->anim.dot_pos_fp += step;
        }
        dirty = true;
    }

    /* 4. Per-card selection scale-up/scale-down (immediately interruptible on fast input) */
    int active_game_idx = -1;
    if (state->filtered_count > 0 && state->selected_pos >= 0 && state->selected_pos < state->filtered_count) {
        active_game_idx = state->filtered_indices[state->selected_pos];
    }

    for (int i = 0; i < state->total_games; i++) {
        int target = (i == active_game_idx) ? NL_SCALE_SELECTED : NL_SCALE_NORMAL;
        int cur = state->anim.card_scale_fp[i];
        int diff = target - cur;
        if (diff != 0) {
            int step = (diff * 7) / 16;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            if (abs(diff) <= 1) {
                state->anim.card_scale_fp[i] = target;
            } else {
                state->anim.card_scale_fp[i] = cur + step;
            }
            dirty = true;
        }

        /* Smooth post-return sort slot glide (card_dx_fp / card_dy_fp -> 0) */
        int dx = state->anim.card_dx_fp[i];
        if (dx != 0) {
            int step_x = (dx * 5) / 16;
            if (step_x == 0) step_x = (dx > 0) ? 1 : -1;
            if (abs(dx) <= 4) {
                state->anim.card_dx_fp[i] = 0;
            } else {
                state->anim.card_dx_fp[i] = dx - step_x;
            }
            dirty = true;
        }
        int dy = state->anim.card_dy_fp[i];
        if (dy != 0) {
            int step_y = (dy * 5) / 16;
            if (step_y == 0) step_y = (dy > 0) ? 1 : -1;
            if (abs(dy) <= 4) {
                state->anim.card_dy_fp[i] = 0;
            } else {
                state->anim.card_dy_fp[i] = dy - step_y;
            }
            dirty = true;
        }
    }

    /* 4b. Press-shrink feedback while LANCER is held down before release */
    {
        int tgt_shrink = state->anim.launch_holding ? 232 : NL_SCALE_SELECTED;
        int cur_shrink = state->anim.launch_shrink_fp;
        if (cur_shrink <= 0) cur_shrink = NL_SCALE_SELECTED;
        int diff_sh = tgt_shrink - cur_shrink;
        if (diff_sh != 0) {
            int step = (diff_sh * 9) / 16;
            if (step == 0) step = (diff_sh > 0) ? 1 : -1;
            if (abs(diff_sh) <= 2) {
                state->anim.launch_shrink_fp = tgt_shrink;
            } else {
                state->anim.launch_shrink_fp = cur_shrink + step;
            }
            dirty = true;
        } else {
            state->anim.launch_shrink_fp = cur_shrink;
        }
    }

    /* 5. Organic theme/accent ink diffusion progression */
    if (state->anim.ink_active) {
        if (state->anim.ink_tick < state->anim.ink_max) {
            state->anim.ink_tick++;
            dirty = true;
        }
        if (state->anim.ink_tick >= state->anim.ink_max) {
            state->anim.ink_active = false;
        }
    }

    /* 5b. Smooth ON/OFF toggle switch knob & color interpolation (0..256 FP) */
    {
        state->anim.switch_autostart_fp = 0;
        state->anim.switch_battery_fp = 0;

        int tgt_hk = state->settings.global_hotkey ? 256 : 0;
        int diff_hk = tgt_hk - state->anim.switch_hotkey_fp;
        if (diff_hk != 0) {
            int step = (diff_hk * 7) / 16;
            if (step == 0) step = (diff_hk > 0) ? 1 : -1;
            if (abs(diff_hk) <= 4) {
                state->anim.switch_hotkey_fp = tgt_hk;
            } else {
                state->anim.switch_hotkey_fp += step;
            }
            dirty = true;
        }

        int tgt_names = state->settings.show_names ? 256 : 0;
        int diff_names = tgt_names - state->anim.switch_shownames_fp;
        if (diff_names != 0) {
            int step = (diff_names * 7) / 16;
            if (step == 0) step = (diff_names > 0) ? 1 : -1;
            if (abs(diff_names) <= 4) {
                state->anim.switch_shownames_fp = tgt_names;
            } else {
                state->anim.switch_shownames_fp += step;
            }
            dirty = true;
        }

        int tgt_sc = state->settings.show_shortcuts ? 256 : 0;
        int diff_sc = tgt_sc - state->anim.switch_shortcuts_fp;
        if (diff_sc != 0) {
            int step = (diff_sc * 7) / 16;
            if (step == 0) step = (diff_sc > 0) ? 1 : -1;
            if (abs(diff_sc) <= 4) {
                state->anim.switch_shortcuts_fp = tgt_sc;
            } else {
                state->anim.switch_shortcuts_fp += step;
            }
            dirty = true;
        }

        int tgt_tp = state->settings.touchpad_nav ? 256 : 0;
        int diff_tp = tgt_tp - state->anim.switch_touchpad_fp;
        if (diff_tp != 0) {
            int step = (diff_tp * 7) / 16;
            if (step == 0) step = (diff_tp > 0) ? 1 : -1;
            if (abs(diff_tp) <= 4) {
                state->anim.switch_touchpad_fp = tgt_tp;
            } else {
                state->anim.switch_touchpad_fp += step;
            }
            dirty = true;
        }

        int tgt_tpc = state->settings.touchpad_cursor_show ? 256 : 0;
        int diff_tpc = tgt_tpc - state->anim.switch_tp_cursor_fp;
        if (diff_tpc != 0) {
            int step = (diff_tpc * 7) / 16;
            if (step == 0) step = (diff_tpc > 0) ? 1 : -1;
            if (abs(diff_tpc) <= 4) {
                state->anim.switch_tp_cursor_fp = tgt_tpc;
            } else {
                state->anim.switch_tp_cursor_fp += step;
            }
            dirty = true;
        }

        if (state->anim.tp_tab_trans_fp < 256) {
            int dtab = 256 - state->anim.tp_tab_trans_fp;
            int step = (dtab * 8) / 16;
            if (step < 1) step = 1;
            state->anim.tp_tab_trans_fp = (dtab <= 4) ? 256 : (state->anim.tp_tab_trans_fp + step);
            dirty = true;
        }

        if (state->anim.tp_style_glow_fp < 256) {
            int dsg = 256 - state->anim.tp_style_glow_fp;
            int step = (dsg * 7) / 16;
            if (step < 1) step = 1;
            state->anim.tp_style_glow_fp = (dsg <= 4) ? 256 : (state->anim.tp_style_glow_fp + step);
            dirty = true;
        }

        if (state->sui.screen == NL_UI_SETTINGS_TOUCHPAD && state->sui.tp_tab == 1) {
            state->anim.tp_style_halo_phase = (uint16_t)((state->anim.tp_style_halo_phase + 6U) & 255U);
        }

        /* 5-Detent Touchpad Sensitivity Slider (mechanical catch-and-latch animation) */
        int sens = state->settings.touchpad_sensitivity;
        if (sens < 0) sens = 0;
        if (sens >= NL_TP_SENS_COUNT) sens = NL_TP_SENS_COUNT - 1;
        int tgt_pos = sens * 256;
        int diff_pos = tgt_pos - state->anim.tp_slider_pos_fp;
        if (diff_pos != 0 || state->anim.tp_slider_vel_fp != 0) {
            int force = (diff_pos * 9) / 32;
            int vel = (state->anim.tp_slider_vel_fp * 7) / 16 + force;
            if (vel == 0 && diff_pos != 0) vel = (diff_pos > 0) ? 4 : -4;
            int next_pos = state->anim.tp_slider_pos_fp + vel;

            if (abs(tgt_pos - next_pos) <= 10 && abs(vel) <= 18) {
                next_pos = tgt_pos;
                vel = 0;
            }
            if (next_pos < -16) { next_pos = -16; vel = 0; }
            if (next_pos > 1040) { next_pos = 1040; vel = 0; }

            state->anim.tp_slider_pos_fp = next_pos;
            state->anim.tp_slider_vel_fp = vel;

            if (sens != state->anim.tp_notch_last && abs(tgt_pos - next_pos) <= 44) {
                state->anim.tp_notch_last = sens;
                state->anim.tp_detent_pulse_fp = 256;
            }
            dirty = true;
        }
        if (state->anim.tp_detent_pulse_fp > 0) {
            state->anim.tp_detent_pulse_fp -= 32;
            if (state->anim.tp_detent_pulse_fp < 0) state->anim.tp_detent_pulse_fp = 0;
            dirty = true;
        }
    }

    /* 5c. Settings Menu open/close (A & G), section transitions (B), option selection (C),
     *     value modification transitions (E) & confirmation modals (F) */
    {
        int tgt_open = (state->sui.screen != NL_UI_LIBRARY) ? 256 : 0;
        int diff_open = tgt_open - state->anim.set_open_fp;
        if (diff_open != 0) {
            int step = (diff_open * 7) / 16;
            if (step == 0) step = (diff_open > 0) ? 1 : -1;
            if (abs(diff_open) <= 4) {
                state->anim.set_open_fp = tgt_open;
            } else {
                state->anim.set_open_fp += step;
            }
            dirty = true;
        }

        if (state->anim.set_sec_fp < 256) {
            int diff_sec = 256 - state->anim.set_sec_fp;
            int step = (diff_sec * 7) / 16;
            if (step < 4) step = 4;
            if (diff_sec <= 4) {
                state->anim.set_sec_fp = 256;
            } else {
                state->anim.set_sec_fp += step;
                if (state->anim.set_sec_fp > 256) state->anim.set_sec_fp = 256;
            }
            dirty = true;
        }

        int diff_sel_y = state->anim.set_target_sel_y_fp - state->anim.set_sel_y_fp;
        if (diff_sel_y != 0) {
            int step = (diff_sel_y * 7) / 16;
            if (step == 0) step = (diff_sel_y > 0) ? 1 : -1;
            if (abs(diff_sel_y) <= 3) {
                state->anim.set_sel_y_fp = state->anim.set_target_sel_y_fp;
            } else {
                state->anim.set_sel_y_fp += step;
            }
            dirty = true;
        }

        int diff_list = state->anim.set_target_scroll_fp - state->anim.set_list_scroll_fp;
        if (diff_list != 0) {
            int step = (diff_list * 7) / 16;
            if (step == 0) step = (diff_list > 0) ? 1 : -1;
            if (abs(diff_list) <= 3) {
                state->anim.set_list_scroll_fp = state->anim.set_target_scroll_fp;
            } else {
                state->anim.set_list_scroll_fp += step;
            }
            dirty = true;
        }

        if (state->anim.set_sel_glow_fp < 256) {
            int diff_glow = 256 - state->anim.set_sel_glow_fp;
            int step = (diff_glow * 7) / 16;
            if (step < 4) step = 4;
            if (diff_glow <= 4) {
                state->anim.set_sel_glow_fp = 256;
            } else {
                state->anim.set_sel_glow_fp += step;
                if (state->anim.set_sel_glow_fp > 256) state->anim.set_sel_glow_fp = 256;
            }
            dirty = true;
        }

        bool modal_active = (state->sui.screen == NL_UI_SETTINGS_RESET) ||
                            (state->sui.screen == NL_UI_SETTINGS_SHORTCUTS &&
                             (state->sui.sc_picking_key || state->sui.sc_conflict_prompt)) ||
                            (state->sui.screen == NL_UI_SETTINGS_ICONS &&
                             state->sui.icon_confirm_modal) ||
                            (state->sui.screen == NL_UI_SETTINGS_PROFILES &&
                             state->sui.prof_mode == NL_PROF_MODE_CONFIRM_DELETE);
        int tgt_modal = modal_active ? 256 : 0;
        int diff_modal = tgt_modal - state->anim.modal_fp;
        if (diff_modal != 0) {
            int step = (diff_modal * 8) / 16;
            if (step == 0) step = (diff_modal > 0) ? 1 : -1;
            if (abs(diff_modal) <= 4) {
                state->anim.modal_fp = tgt_modal;
            } else {
                state->anim.modal_fp += step;
            }
            dirty = true;
        }

        bool confirm_yes = (state->sui.screen == NL_UI_SETTINGS_RESET)
            ? state->sui.reset_confirm_yes
            : ((state->sui.screen == NL_UI_SETTINGS_ICONS)
                ? state->sui.icon_confirm_yes
                : ((state->sui.screen == NL_UI_SETTINGS_PROFILES)
                    ? state->sui.prof_delete_yes
                    : state->sui.sc_conflict_confirm_yes));
        int tgt_btn = confirm_yes ? 256 : 0;
        int diff_btn = tgt_btn - state->anim.modal_btn_fp;
        if (diff_btn != 0) {
            int step = (diff_btn * 8) / 16;
            if (step == 0) step = (diff_btn > 0) ? 1 : -1;
            if (abs(diff_btn) <= 4) {
                state->anim.modal_btn_fp = tgt_btn;
            } else {
                state->anim.modal_btn_fp += step;
            }
            dirty = true;
        }

        if (state->anim.layout_trans_fp < 256) {
            int diff_lay = 256 - state->anim.layout_trans_fp;
            int step = (diff_lay * 7) / 16;
            if (step < 4) step = 4;
            if (diff_lay <= 4) {
                state->anim.layout_trans_fp = 256;
            } else {
                state->anim.layout_trans_fp += step;
                if (state->anim.layout_trans_fp > 256) state->anim.layout_trans_fp = 256;
            }
            dirty = true;
        }

        if (state->anim.prof_cat_trans_fp < 256) {
            int diff_pc = 256 - state->anim.prof_cat_trans_fp;
            int step = (diff_pc * 7) / 16;
            if (step < 5) step = 5;
            if (diff_pc <= 4) {
                state->anim.prof_cat_trans_fp = 256;
            } else {
                state->anim.prof_cat_trans_fp += step;
                if (state->anim.prof_cat_trans_fp > 256) state->anim.prof_cat_trans_fp = 256;
            }
            dirty = true;
        }

        if (state->anim.val_pulse_fp > 0) {
            state->anim.val_pulse_fp -= 44;
            if (state->anim.val_pulse_fp < 0) state->anim.val_pulse_fp = 0;
            dirty = true;
        }

        if (state->sui.screen == NL_UI_SETTINGS_SHORTCUTS && state->sui.sc_picking_key) {
            int k = state->sui.sc_key_cursor;
            int r = k / 8;
            int c = k % 8;
            int tgt_kx = (34 + c * 31) << 4;
            int tgt_ky = (72 + r * 26) << 4;
            int tgt_kw = ((k == 0) ? 28 : 26) << 4;

            int dx = tgt_kx - state->anim.sc_key_x_fp;
            if (dx != 0) {
                int step = (dx * 8) / 16;
                if (step == 0) step = (dx > 0) ? 1 : -1;
                state->anim.sc_key_x_fp = (abs(dx) <= 3) ? tgt_kx : (state->anim.sc_key_x_fp + step);
                dirty = true;
            }
            int dy = tgt_ky - state->anim.sc_key_y_fp;
            if (dy != 0) {
                int step = (dy * 8) / 16;
                if (step == 0) step = (dy > 0) ? 1 : -1;
                state->anim.sc_key_y_fp = (abs(dy) <= 3) ? tgt_ky : (state->anim.sc_key_y_fp + step);
                dirty = true;
            }
            int dw = tgt_kw - state->anim.sc_key_w_fp;
            if (dw != 0) {
                int step = (dw * 8) / 16;
                if (step == 0) step = (dw > 0) ? 1 : -1;
                state->anim.sc_key_w_fp = (abs(dw) <= 3) ? tgt_kw : (state->anim.sc_key_w_fp + step);
                dirty = true;
            }
        }
    }

    /* 5d. Library Inline Search Bar reveal/collapse + Category Sliding Capsule */
    {
        int tgt_search = state->sui.search_active ? 256 : 0;
        int diff_search = tgt_search - state->anim.search_bar_fp;
        if (diff_search != 0) {
            int step = (diff_search * 7) / 16;
            if (step == 0) step = (diff_search > 0) ? 1 : -1;
            if (abs(diff_search) <= 4) {
                state->anim.search_bar_fp = tgt_search;
            } else {
                state->anim.search_bar_fp += step;
            }
            dirty = true;
        }

        int tgt_cx = (state->active_category == NL_CAT_ALL) ? (34 << 4)
                   : (state->active_category == NL_CAT_FAVORITES) ? (76 << 4)
                   : (112 << 4);
        int tgt_cw = (state->active_category == NL_CAT_ALL) ? (36 << 4) : (30 << 4);

        if (state->anim.cat_ind_w_fp <= 0) {
            state->anim.cat_ind_x_fp = tgt_cx;
            state->anim.cat_ind_w_fp = tgt_cw;
        } else {
            int dcx = tgt_cx - state->anim.cat_ind_x_fp;
            if (dcx != 0) {
                int step = (dcx * 7) / 16;
                if (step == 0) step = (dcx > 0) ? 1 : -1;
                state->anim.cat_ind_x_fp = (abs(dcx) <= 3) ? tgt_cx : (state->anim.cat_ind_x_fp + step);
                dirty = true;
            }
            int dcw = tgt_cw - state->anim.cat_ind_w_fp;
            if (dcw != 0) {
                int step = (dcw * 7) / 16;
                if (step == 0) step = (dcw > 0) ? 1 : -1;
                state->anim.cat_ind_w_fp = (abs(dcw) <= 3) ? tgt_cw : (state->anim.cat_ind_w_fp + step);
                dirty = true;
            }
        }

        if (state->anim.cat_trans_fp > 0 && state->anim.cat_trans_fp < 256) {
            int diff_ct = 256 - state->anim.cat_trans_fp;
            int step = (diff_ct * 7) / 16;
            if (step < 5) step = 5;
            if (diff_ct <= 4) {
                state->anim.cat_trans_fp = 256;
            } else {
                state->anim.cat_trans_fp += step;
                if (state->anim.cat_trans_fp > 256) state->anim.cat_trans_fp = 256;
            }
            dirty = true;
        }
    }

    /* 5e. Quick Profile Selector Overlay & Continuous Animated Multicolor Ring */
    {
        bool qp_vis = (state->sui.screen == NL_UI_LIBRARY) && state->sui.qp_open;
        int tgt_qp = qp_vis ? 256 : 0;
        int diff_qp = tgt_qp - state->anim.qp_open_fp;
        if (diff_qp != 0) {
            int step = (diff_qp * 8) / 16;
            if (step == 0) step = (diff_qp > 0) ? 1 : -1;
            if (abs(diff_qp) <= 4) {
                state->anim.qp_open_fp = tgt_qp;
            } else {
                state->anim.qp_open_fp += step;
            }
            dirty = true;
        }

        if (qp_vis || state->anim.qp_open_fp > 0) {
            state->anim.qp_glow_phase = (uint16_t)((state->anim.qp_glow_phase + 5U) & 255U);

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
            int rel = cur - win_start;
            int tgt_gx_fp = (start_x + rel * stride + diam / 2) << 4;
            int tgt_gy_fp = 114 << 4;

            if (state->anim.qp_glow_x_fp == 0 && state->anim.qp_glow_y_fp == 0) {
                state->anim.qp_glow_x_fp = tgt_gx_fp;
                state->anim.qp_glow_y_fp = tgt_gy_fp;
                state->anim.qp_trail_x_fp = tgt_gx_fp;
            } else {
                int dgx = tgt_gx_fp - state->anim.qp_glow_x_fp;
                if (dgx != 0) {
                    int step = (dgx * 9) / 16;
                    if (step == 0) step = (dgx > 0) ? 1 : -1;
                    state->anim.qp_glow_x_fp = (abs(dgx) <= 3) ? tgt_gx_fp : (state->anim.qp_glow_x_fp + step);
                }
                int dgy = tgt_gy_fp - state->anim.qp_glow_y_fp;
                if (dgy != 0) {
                    int step = (dgy * 9) / 16;
                    if (step == 0) step = (dgy > 0) ? 1 : -1;
                    state->anim.qp_glow_y_fp = (abs(dgy) <= 3) ? tgt_gy_fp : (state->anim.qp_glow_y_fp + step);
                }
                int dtx = state->anim.qp_glow_x_fp - state->anim.qp_trail_x_fp;
                if (dtx != 0) {
                    int tstep = (dtx * 5) / 16;
                    if (tstep == 0) tstep = (dtx > 0) ? 1 : -1;
                    state->anim.qp_trail_x_fp = (abs(dtx) <= 3) ? state->anim.qp_glow_x_fp : (state->anim.qp_trail_x_fp + tstep);
                }
            }

            if (state->anim.qp_sel_bounce_fp > 0) {
                state->anim.qp_sel_bounce_fp -= 38;
                if (state->anim.qp_sel_bounce_fp < 0) state->anim.qp_sel_bounce_fp = 0;
            }

            int tgt_plus = (cur == pcount) ? 256 : 0;
            int dplus = tgt_plus - state->anim.qp_plus_focus_fp;
            if (dplus != 0) {
                int pstep = (dplus * 7) / 16;
                if (pstep == 0) pstep = (dplus > 0) ? 1 : -1;
                state->anim.qp_plus_focus_fp = (abs(dplus) <= 4) ? tgt_plus : (state->anim.qp_plus_focus_fp + pstep);
            }

            for (int s = 0; s < 9; s++) {
                if (state->anim.qp_slot_scale_fp[s] == 0) state->anim.qp_slot_scale_fp[s] = 226;
                int tgt_sc = (s == cur) ? 272 : 226;
                int dsc = tgt_sc - state->anim.qp_slot_scale_fp[s];
                if (dsc != 0) {
                    int sstep = (dsc * 8) / 16;
                    if (sstep == 0) sstep = (dsc > 0) ? 1 : -1;
                    state->anim.qp_slot_scale_fp[s] = (abs(dsc) <= 2) ? tgt_sc : (state->anim.qp_slot_scale_fp[s] + sstep);
                }
            }
            dirty = true;
        }

        if (state->anim.prof_switch_active) {
            state->anim.qp_glow_phase = (uint16_t)((state->anim.qp_glow_phase + 7U) & 255U);
            state->anim.prof_switch_tick++;
            if (state->anim.prof_switch_tick >= state->anim.prof_switch_max) {
                state->anim.prof_switch_active = false;
                state->anim.prof_switch_tick = 0;
            }
            dirty = true;
        }

        if (state->anim.prof_plus_active) {
            state->anim.qp_glow_phase = (uint16_t)((state->anim.qp_glow_phase + 7U) & 255U);
            state->anim.prof_plus_tick++;
            if (state->anim.prof_plus_tick >= state->anim.prof_plus_max) {
                state->anim.prof_plus_active = false;
                state->anim.prof_plus_tick = 0;
            }
            dirty = true;
        }

        if (state->anim.prof_birth_active) {
            state->anim.qp_glow_phase = (uint16_t)((state->anim.qp_glow_phase + 7U) & 255U);
            state->anim.prof_birth_tick++;
            if (state->anim.prof_birth_tick >= state->anim.prof_birth_max) {
                state->anim.prof_birth_active = false;
                state->anim.prof_birth_tick = 0;
            }
            dirty = true;
        }
    }

    /* 5e. Customizable Touchpad Graphical Cursor: Damped Inertia, Luminous Trail & Idle Fade */
    {
        int tx = 160, ty = 116;
        nl_cursor_compute_target(state, &tx, &ty);
        state->anim.cur_target_x_fp = tx << 4;
        state->anim.cur_target_y_fp = ty << 4;

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

        if (!nl_cursor_is_visible(state)) {
            state->anim.cur_x_fp = state->anim.cur_target_x_fp;
            state->anim.cur_y_fp = state->anim.cur_target_y_fp;
            state->anim.cur_vx_fp = 0;
            state->anim.cur_vy_fp = 0;
            for (int i = 0; i < NL_CURSOR_TRAIL_LEN; i++) {
                state->anim.cur_trail_x_fp[i] = state->anim.cur_x_fp;
                state->anim.cur_trail_y_fp[i] = state->anim.cur_y_fp;
                state->anim.cur_trail_alpha[i] = 0;
            }
            if (state->anim.cur_opacity_fp > 0) {
                state->anim.cur_opacity_fp -= 48;
                if (state->anim.cur_opacity_fp < 0) state->anim.cur_opacity_fp = 0;
                dirty = true;
            }
        } else {
            int prev_x = state->anim.cur_x_fp;
            int prev_y = state->anim.cur_y_fp;
            int dx = state->anim.cur_target_x_fp - state->anim.cur_x_fp;
            int dy = state->anim.cur_target_y_fp - state->anim.cur_y_fp;

            if (dx != 0 || dy != 0 || state->anim.cur_vx_fp != 0 || state->anim.cur_vy_fp != 0) {
                int vx = (dx * 7) / 16 + (state->anim.cur_vx_fp * 4) / 16;
                if ((vx > 0 && vx > dx) || (vx < 0 && vx < dx)) {
                    vx = (dx * 3) / 4;
                }
                if (abs(dx) <= 3 && abs(vx) <= 3) {
                    state->anim.cur_x_fp = state->anim.cur_target_x_fp;
                    state->anim.cur_vx_fp = 0;
                } else {
                    if (vx == 0 && dx != 0) vx = (dx > 0) ? 1 : -1;
                    state->anim.cur_x_fp += vx;
                    state->anim.cur_vx_fp = vx;
                }

                int vy = (dy * 7) / 16 + (state->anim.cur_vy_fp * 4) / 16;
                if ((vy > 0 && vy > dy) || (vy < 0 && vy < dy)) {
                    vy = (dy * 3) / 4;
                }
                if (abs(dy) <= 3 && abs(vy) <= 3) {
                    state->anim.cur_y_fp = state->anim.cur_target_y_fp;
                    state->anim.cur_vy_fp = 0;
                } else {
                    if (vy == 0 && dy != 0) vy = (dy > 0) ? 1 : -1;
                    state->anim.cur_y_fp += vy;
                    state->anim.cur_vy_fp = vy;
                }

                int moved = abs(state->anim.cur_x_fp - prev_x) + abs(state->anim.cur_y_fp - prev_y);
                for (int i = NL_CURSOR_TRAIL_LEN - 1; i > 0; i--) {
                    state->anim.cur_trail_x_fp[i] = state->anim.cur_trail_x_fp[i - 1];
                    state->anim.cur_trail_y_fp[i] = state->anim.cur_trail_y_fp[i - 1];
                    state->anim.cur_trail_alpha[i] = (state->anim.cur_trail_alpha[i - 1] * 17) / 24;
                }
                state->anim.cur_trail_x_fp[0] = prev_x;
                state->anim.cur_trail_y_fp[0] = prev_y;
                int spawn_alpha = (moved >= 8) ? (110 + moved / 2) : 0;
                if (spawn_alpha > 210) spawn_alpha = 210;
                state->anim.cur_trail_alpha[0] = spawn_alpha;

                state->anim.cur_idle_ticks = 0;
                dirty = true;
            } else {
                for (int i = 0; i < NL_CURSOR_TRAIL_LEN; i++) {
                    if (state->anim.cur_trail_alpha[i] > 0) {
                        state->anim.cur_trail_alpha[i] = (state->anim.cur_trail_alpha[i] * 14) / 24 - 12;
                        if (state->anim.cur_trail_alpha[i] < 0) state->anim.cur_trail_alpha[i] = 0;
                        dirty = true;
                    }
                }
                if (state->anim.cur_idle_ticks < NL_CURSOR_IDLE_FADE_START_TICKS) {
                    state->anim.cur_idle_ticks++;
                }
            }

            int tgt_op = (state->anim.cur_idle_ticks >= NL_CURSOR_IDLE_FADE_START_TICKS)
                ? NL_CURSOR_IDLE_MIN_OPACITY_FP
                : 256;
            int dop = tgt_op - state->anim.cur_opacity_fp;
            if (dop != 0) {
                int step = (dop > 0) ? ((dop * 9) / 16) : ((dop * 3) / 16);
                if (step == 0) step = (dop > 0) ? 1 : -1;
                if (abs(dop) <= 4) {
                    state->anim.cur_opacity_fp = tgt_op;
                } else {
                    state->anim.cur_opacity_fp += step;
                }
                dirty = true;
            }
        }
    }

    /* 6. Toast notification countdown */
    if (state->anim.toast_ticks > 0) {
        state->anim.toast_ticks--;
        dirty = true;
    }

    return dirty;
}
