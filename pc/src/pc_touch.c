/* pc_touch.c - see pc_touch.h. Local pad only. The discs are drawn with a small shader,
 * not with a copied texture. */
#include "pc_touch.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_dedicated.h"
#include "pc_pause_menu.h"
#include "pc_settings_menu.h"
#include "pc_play_online_menu.h"
#include "pc_play_offline_menu.h"
#include "pc_crash.h"
#include "pc_typing.h"
#include "pc_chat.h"
#include "pc_bedroom.h"
#include "pc_cloud_storage.h"
#include "pc_dresser_ui.h"
#include "pc_missions.h"

extern int pc_creator_menu_active(void);
extern void pc_creator_menu_pointer(float fx, float fy);

#include "game.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "m_msg.h"
#include "m_field_info.h"
#include "m_name_table.h"
#include "m_actor.h"
#include "m_choice.h"
#include "m_common_data.h"
#include "m_private.h"
#include "m_submenu.h"
#include "m_submenu_ovl.h"
#include "m_tag_ovl.h"
#include "m_hand_ovl.h"
#include "m_mail.h"
#include "m_inventory_ovl.h"
#include "m_item_name.h"
#include "m_font.h"
#include "dataobject.h"
#include "audio.h"
#include "audio_defs.h"
#include "m_collision_bg.h"
#include <dolphin/pad.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TOUCH_STICK 80
#define TOUCH_ARRIVE 25.0f
#define HUD_OFF 0
#define HUD_TOWN 1
#define HUD_MENU 2
#define HUD_DIALOG 3
#define HUD_KEYS 4

enum { HIT_NONE = 0, HIT_PICTURE, HIT_STICK, HIT_NUB, HIT_ACTION, HIT_BACK, HIT_MENU, HIT_KEY, HIT_BAG, HIT_MAP, HIT_CLOSE, HIT_CONTEXT, HIT_CHOICE, HIT_SUB_TAB,
       HIT_SHEET_CLOSE, HIT_SHEET_BELLS, HIT_SHEET_TAB, HIT_SHEET_CARD, HIT_SHEET_ACT, HIT_SHEET_BODY, HIT_CAM,
       HIT_STORAGE, HIT_STOR_PREV, HIT_STOR_NEXT };

typedef struct {
    int active;            /* 1 if drawer is visible */
    int current_tab;       /* 0: Pockets, 1: Letters, 2: Fish, 3: Bugs, 4: Missions */
    int sel_slot;          /* -1 = none, 0..14 = pocket slot */
    int swap_mode;         /* 1 if waiting for second slot to swap */
    int swap_source;       /* source slot index */

    /* Sheet layout rect (in window screen coords) */
    float sheet_x0, sheet_y0, sheet_x1, sheet_y1;

    /* Header layout */
    float close_cx, close_cy, close_w, close_h;
    float bells_cx, bells_cy, bells_w, bells_h;

    /* Tabs layout (5 tabs) */
    float tab_cx[5], tab_cy[5], tab_w[5], tab_h[5];

    /* Card grid layout (15 slots: 5 cols x 3 rows) */
    float card_x0[15], card_y0[15], card_x1[15], card_y1[15];

    /* Detail banner layout */
    float detail_x0, detail_y0, detail_x1, detail_y1;

    /* Action bar buttons */
    int num_actions;
    const char* action_labels[4];
    int action_ids[4]; /* 10: Equip, 11: Unequip, 12: Wear, 13: Eat, 14: Plant, 15: Release, 16: Deposit, 2: Drop, 3: Swap, 4: Cancel/Close */
    float act_cx[4], act_cy[4], act_w[4], act_h[4];
} MobileSheet;

static MobileSheet s_sheet;
static int s_pending_drop = 0;

static void mobile_sheet_layout(void);
static int mobile_sheet_hit(float x, float y, int* key);
static void mobile_sheet_handle_card(int slot);
static void mobile_sheet_handle_act(int act_id);
static void mobile_sheet_handle_tab(int tab_id);
static void mobile_sheet_handle_bells(void);
static void mobile_sheet_draw(void);

static int s_pointer_was = 0;
static float s_down_x = 0.0f, s_down_y = 0.0f, s_last_x = 0.0f, s_last_y = 0.0f;
static int s_down_hit = HIT_NONE;
static int s_pulse_a = 0;
static int s_pulse_b = 0;
static int s_have_target = 0;
static int s_interact = 0;
static float s_target_x = 0.0f, s_target_z = 0.0f;
static float s_knob_x = 0.0f, s_knob_y = 0.0f;
static int s_title_valid = 0;
static int s_title_life = 0;
static float s_title_y = 0.0f;
static unsigned s_latch = 0;
static int s_latch_frames = 0;
static int s_gest = 0;
static int s_tap_ready = 0;
static float s_gest_x = 0, s_gest_y = 0, s_tap_x = 0, s_tap_y = 0;
static int s_hud = HUD_OFF;
static int s_logged = 0;
static int s_overlay_logged = 0;
static char s_action[16] = "A";
static float s_mark_x = 0, s_mark_y = 0, s_mark_t = 0;
static float s_best_dist = -1;
static int s_stall = 0;
static int s_slide = 0;
static int s_lay_w = 0, s_lay_h = 0;
static SDL_Haptic* s_haptic = NULL;
static int s_haptic_tried = 0;
static int s_running = 0;
static int s_drag_logged = 0;
static int s_tap_logged = 0;
static Uint32 s_last_tap_ms = 0;
static int s_swipe = 0;
static int s_choice_confirm = 0;
static char s_tap_kind[32] = "-";
static int s_drag_active = 0;
static float s_drag_ox = 0.0f, s_drag_oy = 0.0f;
static float s_drag_cx = 0.0f, s_drag_cy = 0.0f;
static Uint32 s_drag_t0 = 0;
static int s_drag_moved = 0;

static float s_cam_prev_dist = 0.0f;
static float s_cam_prev_mid_x = 0.0f, s_cam_prev_mid_y = 0.0f;
static float s_cam_accum_pinch = 0.0f;
static float s_cam_accum_dx = 0.0f, s_cam_accum_dy = 0.0f;
static int s_cam_two_finger_was = 0;
static int s_cam_pulse_x = 0, s_cam_pulse_y = 0;
static int s_cam_pulse_timer = 0;
static int s_cam_cooldown = 0;

enum { FINGERS = 4 };
typedef struct { SDL_FingerID id; float x, y; int down; } Finger;
static Finger s_fin[FINGERS];

typedef struct {
    int show_stick, show_nub, show_action, show_back, show_menu, show_keys, show_quick, show_cam;
    int show_close, show_context, choice_n;
    int show_sub_tabs, sub_tab_n;
    float sx, sy, sr, nx, ny, nr, ax, ay, ar, bx, by, br, mx, my, mr;
    float bag_x, bag_y, map_x, map_y, cam_x, cam_y, cam_r, qr;
    float close_x, close_y, close_r, ctx_x, ctx_y, ctx_r;
    float choice_x, choice_r, choice_y[6];
    int show_storage;
    float storage_x, storage_y;
    int show_stor_pages;
    float stor_prev_x, stor_prev_y, stor_next_x, stor_next_y, stor_page_r;
    float sub_tab_x[4], sub_tab_y[4], sub_tab_r;
    int sub_tab_id[4];
    int key_n;
    float kx[40], ky[40], kr;
    char kc[40];
} Hud;

int pc_touch_enabled(void) {
    if (g_pc_dedicated) return 0;
    if (g_pc_settings.touch_controls == 0) return 0;
    if (g_pc_settings.touch_controls == 1) return 1;
    return SDL_GetNumTouchDevices() > 0;
}

int pc_touch_finger_count(void) {
    int i, n = 0;
    for (i = 0; i < FINGERS; i++) if (s_fin[i].down) n++;
    return n;
}

int pc_touch_hud_mode(void) { return pc_touch_enabled() ? s_hud : HUD_OFF; }

void pc_touch_hud_state(char* buf, size_t n) {
    if (buf == NULL || n == 0) return;
    snprintf(buf, n, "sc=%d op=%d hand=%c stk=%d move=%d hap=%d vpad=%d tap=%s",
             g_pc_settings.touch_hud_scale, g_pc_settings.touch_hud_opacity,
             g_pc_settings.touch_left_hand ? 'L' : 'R',
             g_pc_settings.touch_stick, g_pc_settings.touch_move, g_pc_settings.touch_haptics,
             g_pc_settings.touch_virtual_pad, s_tap_kind);
}

void pc_touch_pulse_b(void) { s_pulse_b = 1; }

int pc_touch_take_title_tap(float* font_y) {
    if (!s_title_valid) return 0;
    s_title_valid = 0;
    if (font_y) *font_y = s_title_y;
    return 1;
}

static void finger_set(SDL_FingerID id, float x, float y, int down) {
    int i, free_slot = -1;
    for (i = 0; i < FINGERS; i++) {
        if (s_fin[i].down && s_fin[i].id == id) {
            s_fin[i].x = x;
            s_fin[i].y = y;
            s_fin[i].down = down;
            return;
        }
        if (!s_fin[i].down && free_slot < 0) free_slot = i;
    }
    if (down && free_slot >= 0) {
        s_fin[free_slot].id = id;
        s_fin[free_slot].x = x;
        s_fin[free_slot].y = y;
        s_fin[free_slot].down = 1;
    }
}

void pc_touch_note_event(const SDL_Event* e) {
    float x, y;
    if (e == NULL || g_pc_window_w < 2 || g_pc_window_h < 2) return;
    if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERUP || e->type == SDL_FINGERMOTION) {
        x = e->tfinger.x * (float)g_pc_window_w;
        y = e->tfinger.y * (float)g_pc_window_h;
        finger_set(e->tfinger.fingerId, x, y, e->type != SDL_FINGERUP);
        if (e->type == SDL_FINGERDOWN) {
            s_gest = 1;
            s_gest_x = x;
            s_gest_y = y;
        } else if (e->type == SDL_FINGERUP && s_gest) {
            /* A short tap's up often arrives in the same poll as its down, before PADRead. */
            s_gest = 0;
            s_tap_ready = 1;
            s_tap_x = s_gest_x;
            s_tap_y = s_gest_y;
            s_gest_x = x;
            s_gest_y = y;
        }
        if (s_logged < 6 && e->type != SDL_FINGERMOTION) {
            char line[96];
            s_logged++;
            snprintf(line, sizeof(line), "touch %s id=%lld x=%.3f y=%.3f",
                     e->type == SDL_FINGERDOWN ? "down" : "up",
                     (long long)e->tfinger.fingerId, e->tfinger.x, e->tfinger.y);
            pc_diag_milestone(line);
        }
        return;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT && s_logged < 6) {
        char line[96];
        s_logged++;
        snprintf(line, sizeof(line), "touch mouse down x=%d y=%d", e->button.x, e->button.y);
        pc_diag_milestone(line);
    }
}

static GAME_PLAY* get_game_play(void) {
    if (gamePT == NULL || gamePT->exec != play_main) return NULL;
    return (GAME_PLAY*)gamePT;
}

static int message_open(void) {
    if (get_game_play() == NULL) return 0;
    mMsg_Window_c* w = mMsg_Get_base_window_p();
    return w != NULL && mMsg_Check_main_hide(w) == FALSE;
}

static int submenu_open(void) {
    GAME_PLAY* play = get_game_play();
    if (play == NULL) return 0;
    return (play->submenu.open_flag ||
            play->submenu.mode >= mSM_MODE_PRERENDER_DONE ||
            play->submenu.process_status == mSM_PROCESS_PLAY ||
            play->submenu.menu_type != 0) &&
           play->submenu.overlay != NULL;
}

static int keys_up(void) {
#ifdef KEYBOARD_TYPING
    if (g_pc_editor_active && g_pc_settings.touch_keyboard) return 1;
#endif
    return pc_play_online_menu_text_active();
}

static int in_picture(float x, float y) {
    return x >= (float)g_pc_content_x && y >= (float)g_pc_content_y &&
           x < (float)(g_pc_content_x + g_pc_content_w) &&
           y < (float)(g_pc_content_y + g_pc_content_h);
}

static void to_font(float x, float y, float* fx, float* fy) {
    if (g_pc_layout_portrait) {
        float cw = (float)g_pc_content_w;
        float ch = cw * 3.0f / 4.0f;
        float cy = (float)g_pc_content_y + ((float)g_pc_content_h - ch) * 0.5f;
        *fx = (x - (float)g_pc_content_x) / cw * 320.0f;
        *fy = (y - cy) / ch * 240.0f;
    } else {
        *fx = (x - (float)g_pc_content_x) / (float)g_pc_content_w * 320.0f;
        *fy = (y - (float)g_pc_content_y) / (float)g_pc_content_h * 240.0f;
    }
}

static void place_keys(Hud* h, float top, float bottom, float left, float right) {
    static const char* rows[4] = { "qwertyuiop", "asdfghjkl", "zxcvbnm<", " +" };
    int r, c, n = 0;
    float band = bottom - top;
    float rh = band / 4.2f;
    if (rh < 28.0f) rh = 28.0f;
    h->kr = rh * 0.42f;
    if (h->kr > 26.0f) h->kr = 26.0f;
    if (h->kr < 16.0f) h->kr = 16.0f;
    for (r = 0; r < 4 && n < 40; r++) {
        int count = (int)strlen(rows[r]);
        float span = (float)count * h->kr * 2.15f;
        float x0 = left + ((right - left) - span) * 0.5f + h->kr;
        float y = top + rh * ((float)r + 0.55f);
        if (r == 3) {
            h->kx[n] = left + (right - left) * 0.38f;
            h->ky[n] = y;
            h->kc[n] = ' ';
            n++;
            h->kx[n] = left + (right - left) * 0.78f;
            h->ky[n] = y;
            h->kc[n] = '\n';
            n++;
            continue;
        }
        for (c = 0; c < count && n < 40; c++) {
            h->kx[n] = x0 + (float)c * h->kr * 2.15f;
            h->ky[n] = y;
            h->kc[n] = rows[r][c];
            n++;
        }
    }
    h->key_n = n;
}

static void hud_build(Hud* h) {
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float il = 0, it = 0, ir = 0, ib = 0;
    float rad;
    int town, menu, dialog;
    FILE* f = fopen("safe_insets.txt", "r");
    memset(h, 0, sizeof(*h));
    if (f != NULL) {
        int a = 0, b = 0, c = 0, d = 0;
        if (fscanf(f, "%d %d %d %d", &a, &b, &c, &d) == 4) {
            il = (float)a; it = (float)b; ir = (float)c; ib = (float)d;
        }
        fclose(f);
    }
    town = !g_pc_paused && !g_pc_title_main_menu_visible && !pc_settings_menu_active() &&
           !pc_play_online_menu_active() && !pc_play_offline_menu_active() && !pc_creator_menu_active() &&
           !message_open() && !submenu_open() && !keys_up();
    menu = g_pc_paused || g_pc_title_main_menu_visible || pc_settings_menu_active() ||
           pc_play_online_menu_active() || pc_play_offline_menu_active() || pc_creator_menu_active();
    dialog = !menu && (message_open() || submenu_open());
    if (keys_up()) s_hud = HUD_KEYS;
    else if (dialog) s_hud = HUD_DIALOG;
    else if (menu) s_hud = HUD_MENU;
    else if (town) s_hud = HUD_TOWN;
    else s_hud = HUD_TOWN;
    rad = 0.055f * (W < H ? W : H);
    if (rad < 36.0f) rad = 36.0f;
    if (rad > 68.0f) rad = 68.0f;
    {
        int sc = g_pc_settings.touch_hud_scale;
        if (sc < 70) sc = 70;
        if (sc > 140) sc = 140;
        rad *= (float)sc / 100.0f;
    }
    h->show_keys = keys_up();
    h->show_nub = 0;
    h->show_stick = 0;
    /* Pocket Camp controls: Action, Back, Menu, Bag, Map, Cam are available in town */
    h->show_action = !h->show_keys;
    h->show_back = !h->show_keys;
    h->show_menu = !h->show_keys;
    h->show_quick = !h->show_keys && (town || menu);
    h->show_cam = !h->show_keys && town;
    h->show_close = !h->show_keys && (menu || dialog);
    h->show_context = 0;

    if (g_pc_layout_portrait) {
        /* Portrait HUD: Action and Back in bottom-right; Pockets, Map & Cam in bottom-left; Menu in top-right */
        h->ar = rad * 1.20f;
        h->ax = W - ir - h->ar - 24.0f;
        h->ay = H - ib - h->ar - 28.0f;

        h->br = rad * 0.90f;
        h->bx = h->ax - h->ar - h->br - 22.0f;
        h->by = h->ay;

        h->qr = rad * 0.95f;
        h->bag_x = il + h->qr + 28.0f;
        h->bag_y = H - ib - h->qr - 32.0f;

        h->map_x = h->bag_x;
        h->map_y = h->bag_y - h->qr * 2.35f;

        h->cam_r = h->qr;
        h->cam_x = h->bag_x;
        h->cam_y = h->map_y - h->qr * 2.35f;

        h->mr = rad * 0.85f;
        h->mx = W - ir - h->mr - 24.0f;
        h->my = it + h->mr + 24.0f;

        h->close_r = rad * 0.85f;
        h->close_x = il + h->close_r + 24.0f;
        h->close_y = it + h->close_r + 24.0f;

        if (h->show_keys) {
            place_keys(h, H * 0.55f, H - ib - 16.0f, il + 8.0f, W - ir - 8.0f);
        }
    } else {
        /* Landscape HUD */
        h->ar = rad * 1.25f;
        h->ax = W - ir - h->ar - 24.0f;
        h->ay = H - ib - h->ar - 24.0f;

        h->br = rad * 0.90f;
        h->bx = h->ax - h->ar - h->br - 20.0f;
        h->by = h->ay;

        h->qr = rad * 0.85f;
        h->bag_x = il + h->qr + 24.0f;
        h->bag_y = H - ib - h->qr - 24.0f;

        h->map_x = h->bag_x;
        h->map_y = h->bag_y - h->qr * 2.3f;

        h->cam_r = h->qr;
        h->cam_x = h->bag_x;
        h->cam_y = h->map_y - h->qr * 2.3f;

        h->mr = rad * 0.80f;
        h->mx = W - ir - h->mr - 20.0f;
        h->my = it + h->mr + 20.0f;

        h->close_r = rad * 0.80f;
        h->close_x = il + h->close_r + 20.0f;
        h->close_y = it + h->close_r + 20.0f;

        if (h->show_keys) {
            place_keys(h, H - ib - rad * 3.4f, H - ib - 6.0f, W * 0.22f, W * 0.78f);
        }
    }

    h->choice_n = 0;
    if (get_game_play() != NULL) {
        mChoice_c* choice = mChoice_Get_base_window_p();
        int nchoice = 0;
        if (choice != NULL && mChoice_check_main_normal(choice)) nchoice = choice->data.choice_num;
        if (nchoice > 6) nchoice = 6;
        if (nchoice > 0 && dialog) {
            float band_top, band_bot, step;
            int i;
            if (g_pc_layout_portrait) {
                float cw = (float)g_pc_content_w;
                float ch_43 = cw * 3.0f / 4.0f;
                float cy_43 = (float)g_pc_content_y + ((float)g_pc_content_h - ch_43) * 0.5f;
                band_top = cy_43 + ch_43 * 0.70f;
                band_bot = H - ib - rad * 2.2f;
                if (band_bot < band_top + 60.0f) { band_top = cy_43 + ch_43 * 0.50f; band_bot = H - ib - 16.0f; }
            } else {
                band_top = H * 0.52f;
                band_bot = H - ib - rad * 2.8f;
                if (band_bot < band_top + 80.0f) { band_top = H * 0.45f; band_bot = H - ib - 20.0f; }
            }
            step = (band_bot - band_top) / (float)(nchoice + 1);
            h->choice_n = nchoice;
            h->choice_r = step * 0.38f;
            if (h->choice_r > rad * 0.85f) h->choice_r = rad * 0.85f;
            if (h->choice_r < 24.0f) h->choice_r = 24.0f;
            h->choice_x = W * 0.5f;
            for (i = 0; i < nchoice; i++) h->choice_y[i] = band_top + step * (float)(i + 1);
        }
    }

    h->show_sub_tabs = 0;
    if (get_game_play() != NULL && submenu_open() && !h->show_keys && h->choice_n == 0) {
        h->show_sub_tabs = 1;
        h->sub_tab_n = 4;
        h->sub_tab_id[0] = 0; /* Items */
        h->sub_tab_id[1] = 1; /* Letters */
        h->sub_tab_id[2] = 2; /* Fish */
        h->sub_tab_id[3] = 3; /* Bugs */
        if (g_pc_layout_portrait) {
            int bx, by, bw, bh;
            pc_layout_box(2, &bx, &by, &bw, &bh);
            float tab_y = (float)(by + bh) + 42.0f;
            if (tab_y > h->ay - rad * 1.25f) tab_y = h->ay - rad * 1.25f;
            h->sub_tab_r = rad * 0.78f;
            if (h->sub_tab_r > 42.0f) h->sub_tab_r = 42.0f;
            if (h->sub_tab_r < 24.0f) h->sub_tab_r = 24.0f;
            float band_w = W - il - ir;
            for (int i = 0; i < 4; i++) {
                h->sub_tab_x[i] = il + band_w * (0.16f + 0.226f * (float)i);
                h->sub_tab_y[i] = tab_y;
            }
        } else {
            h->sub_tab_r = rad * 0.65f;
            if (h->sub_tab_r > 34.0f) h->sub_tab_r = 34.0f;
            if (h->sub_tab_r < 20.0f) h->sub_tab_r = 20.0f;
            for (int i = 0; i < 4; i++) {
                h->sub_tab_x[i] = il + h->sub_tab_r + 20.0f + (float)i * (h->sub_tab_r * 2.3f);
                h->sub_tab_y[i] = H - ib - h->sub_tab_r - 20.0f;
            }
        }
    }

    if (g_pc_settings.touch_left_hand) {
        h->sx = W - h->sx; h->nx = W - h->nx; h->ax = W - h->ax;
        h->bx = W - h->bx; h->mx = W - h->mx;
        h->bag_x = W - h->bag_x; h->map_x = W - h->map_x; h->cam_x = W - h->cam_x;
        h->close_x = W - h->close_x;
        h->storage_x = W - h->storage_x;
        h->stor_prev_x = W - h->stor_prev_x;
        h->stor_next_x = W - h->stor_next_x;
        h->ctx_x = W - h->ctx_x;
        for (int i = 0; i < h->key_n; i++) h->kx[i] = W - h->kx[i];
        for (int i = 0; i < h->sub_tab_n; i++) h->sub_tab_x[i] = W - h->sub_tab_x[i];
    }
    if (!h->show_stick) { h->sx = -1000.0f; }

    h->show_storage = 0;
    if (!h->show_keys && (pc_bedroom_is_in_bedroom() || pc_bedroom_is_in_lounge()) && !submenu_open()) {
        h->show_storage = 1;
        if (g_pc_layout_portrait) {
            h->storage_x = h->bag_x;
            h->storage_y = h->cam_y - h->qr * 2.35f;
        } else {
            h->storage_x = h->bag_x + h->qr * 2.3f;
            h->storage_y = h->bag_y;
        }
    }
    h->show_stor_pages = 0;
    if (submenu_open() && pc_dresser_ui_active() && pc_cloud_storage_client_is_open()) {
        h->show_stor_pages = 1;
        float page_btn_r = 26.0f * ((float)H / 720.0f);
        if (page_btn_r < 20.0f) page_btn_r = 20.0f;
        h->stor_page_r = page_btn_r;
        h->stor_prev_x = W * 0.5f - 120.0f * ((float)H / 720.0f);
        h->stor_prev_y = H * 0.88f;
        h->stor_next_x = W * 0.5f + 120.0f * ((float)H / 720.0f);
        h->stor_next_y = H * 0.88f;
    }

    mobile_sheet_layout();
    if (s_sheet.active) {
        h->show_action = 0;
        h->show_back = 0;
        h->show_quick = 0;
        h->show_cam = 0;
        h->show_close = 0;
        h->show_sub_tabs = 0;
        h->show_context = 0;
        h->show_menu = 0;
    }
}

static int hit_disc(float x, float y, float cx, float cy, float r) {
    float dx = x - cx, dy = y - cy;
    r *= 1.15f; /* a little slop so a fold does not miss the disc */
    return dx * dx + dy * dy <= r * r;
}

static int classify(float x, float y, Hud* h, int* key) {
    int i;
    *key = -1;
    if (s_sheet.active) {
        int sh = mobile_sheet_hit(x, y, key);
        if (sh != HIT_NONE) return sh;
    }
    if (h->show_keys) {
        for (i = 0; i < h->key_n; i++) {
            float r = (h->kc[i] == ' ' || h->kc[i] == '\n') ? h->kr * 1.6f : h->kr;
            if (hit_disc(x, y, h->kx[i], h->ky[i], r)) { *key = i; return HIT_KEY; }
        }
    }
    if (h->choice_n > 0) {
        for (i = 0; i < h->choice_n; i++) {
            if (hit_disc(x, y, h->choice_x, h->choice_y[i], h->choice_r)) { *key = i; return HIT_CHOICE; }
        }
    }
    if (h->show_sub_tabs) {
        for (i = 0; i < h->sub_tab_n; i++) {
            if (hit_disc(x, y, h->sub_tab_x[i], h->sub_tab_y[i], h->sub_tab_r)) {
                *key = h->sub_tab_id[i];
                return HIT_SUB_TAB;
            }
        }
    }
    if (h->show_close && hit_disc(x, y, h->close_x, h->close_y, h->close_r)) return HIT_CLOSE;
    if (h->show_context && hit_disc(x, y, h->ctx_x, h->ctx_y, h->ctx_r)) return HIT_CONTEXT;
    if (h->show_storage && hit_disc(x, y, h->storage_x, h->storage_y, h->qr)) return HIT_STORAGE;
    if (h->show_stor_pages) {
        if (hit_disc(x, y, h->stor_prev_x, h->stor_prev_y, h->stor_page_r)) return HIT_STOR_PREV;
        if (hit_disc(x, y, h->stor_next_x, h->stor_next_y, h->stor_page_r)) return HIT_STOR_NEXT;
    }
    if (h->show_quick && hit_disc(x, y, h->bag_x, h->bag_y, h->qr)) return HIT_BAG;
    if (h->show_quick && hit_disc(x, y, h->map_x, h->map_y, h->qr)) return HIT_MAP;
    if (h->show_cam && hit_disc(x, y, h->cam_x, h->cam_y, h->cam_r)) return HIT_CAM;
    if (h->show_stick && hit_disc(x, y, h->sx, h->sy, h->sr)) return HIT_STICK;
    if (h->show_nub && hit_disc(x, y, h->nx, h->ny, h->nr)) return HIT_NUB;
    if (h->show_action && hit_disc(x, y, h->ax, h->ay, h->ar)) return HIT_ACTION;
    if (h->show_back && hit_disc(x, y, h->bx, h->by, h->br)) return HIT_BACK;
    if (h->show_menu && hit_disc(x, y, h->mx, h->my, h->mr)) return HIT_MENU;
    if (in_picture(x, y)) return HIT_PICTURE;
    return HIT_NONE;
}

static int can_steer(void) {
    GAME_PLAY* play = get_game_play();
    if (play == NULL || message_open() || g_pc_paused || g_pc_title_main_menu_visible) return 0;
    if (pc_settings_menu_active() || pc_play_online_menu_active() || pc_play_offline_menu_active() || pc_creator_menu_active()) return 0;
    if (play->submenu.menu_type != 0 || play->submenu.start_refuse) return 0;
    return 1;
}

static int steer_toward(float tx, float tz, signed char* stick_x, signed char* stick_y) {
    GAME_PLAY* play = get_game_play();
    PLAYER_ACTOR* player;
    float px, pz, dx, dz, dist, fx, fz, fl, rx, rz, sx, sy, mag, arrive_dist;
    if (!can_steer() || play == NULL) return 0;
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return 0;
    px = player->actor_class.world.position.x;
    pz = player->actor_class.world.position.z;
    dx = tx - px;
    dz = tz - pz;
    dist = sqrtf(dx * dx + dz * dz);
    arrive_dist = s_interact ? 36.0f : TOUCH_ARRIVE;
    if (s_best_dist < 0.0f || dist < s_best_dist - 0.2f) {
        s_best_dist = dist;
        s_stall = 0;
        if (s_slide != 0) s_slide = 0; /* moving again: straight at the tap */
    } else if (++s_stall == 45 || s_stall == 85) {
        s_slide = (s_stall == 45) ? 1 : -1; /* gentle detour around obstacle */
    } else if (s_stall == 120) {
        s_slide = 0; /* retry direct path */
    } else if (s_stall > 150) {
        s_have_target = 0;
        s_interact = 0;
        s_stall = 0;
        s_slide = 0;
        s_best_dist = -1.0f;
        return 0;
    }
    if (dist < arrive_dist) {
        s_have_target = 0;
        s_stall = 0;
        s_slide = 0;
        s_best_dist = -1.0f;
        if (s_interact) { s_pulse_a = 1; s_interact = 0; }
        return 0;
    }
    fx = play->camera.lookat.center.x - play->camera.lookat.eye.x;
    fz = play->camera.lookat.center.z - play->camera.lookat.eye.z;
    fl = sqrtf(fx * fx + fz * fz);
    if (fl < 1.0f) return 0;
    fx /= fl; fz /= fl;
    /* Camera right = cross(F, Up) */
    rx = -fz; rz = fx;
    dx /= dist; dz /= dist;
    sx = dx * rx + dz * rz;
    sy = dx * fx + dz * fz;
    mag = sqrtf(sx * sx + sy * sy);
    if (mag < 0.05f) return 0;
    sx = sx / mag * (float)TOUCH_STICK;
    sy = sy / mag * (float)TOUCH_STICK;
    if (s_slide != 0) {
        float ox = sx, oy = sy;
        sx = ox * 0.85f - oy * 0.35f * (float)s_slide;
        sy = oy * 0.85f + ox * 0.35f * (float)s_slide;
    }
    if (sx > 127.0f) sx = 127.0f;
    if (sx < -128.0f) sx = -128.0f;
    if (sy > 127.0f) sy = 127.0f;
    if (sy < -128.0f) sy = -128.0f;
    *stick_x = (signed char)sx;
    *stick_y = (signed char)sy;
    return 1;
}

static int tap_to_world(float wx, float wy, float* out_x, float* out_z) {
    GAME_PLAY* play = get_game_play();
    PLAYER_ACTOR* player;
    float fov, base_aspect, cur_aspect, nx, ny, base_tan_y, tan_x, tan_y;
    float fx, fy, fz, fl, rx, rz, rl, ux, uy, uz, ul, dx, dy, dz, dl, t, py;
    xyz_t test_pos;
    float bg_y, t2;

    if (!in_picture(wx, wy) || play == NULL) return 0;
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return 0;

    fov = play->camera.perspective.fov_y;
    if (fov < 5.0f || fov > 170.0f) fov = 45.0f;

    base_aspect = 4.0f / 3.0f;
    cur_aspect = (float)g_pc_content_w / (float)g_pc_content_h;
    base_tan_y = tanf(fov * 0.5f * 3.14159265f / 180.0f);

    if (cur_aspect < base_aspect - 0.001f) {
        /* Portrait / Vert+: horizontal FOV is fixed to 4:3 base, vertical FOV expands */
        tan_x = base_tan_y * base_aspect;
        tan_y = tan_x / cur_aspect;
    } else {
        /* Landscape / Hor+: vertical FOV is fixed, horizontal FOV expands */
        tan_y = base_tan_y;
        tan_x = base_tan_y * cur_aspect;
    }

    nx = ((wx - (float)g_pc_content_x) / (float)g_pc_content_w) * 2.0f - 1.0f;
    ny = 1.0f - ((wy - (float)g_pc_content_y) / (float)g_pc_content_h) * 2.0f;

    fx = play->camera.lookat.center.x - play->camera.lookat.eye.x;
    fy = play->camera.lookat.center.y - play->camera.lookat.eye.y;
    fz = play->camera.lookat.center.z - play->camera.lookat.eye.z;
    fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl < 1.0f) return 0;
    fx /= fl; fy /= fl; fz /= fl;

    /* Camera right = cross(F, Up) where Up is (0, 1, 0) */
    rx = -fz; rz = fx;
    rl = sqrtf(rx * rx + rz * rz);
    if (rl < 0.01f) return 0;
    rx /= rl; rz /= rl;

    /* Camera true up = cross(R, F) */
    ux = -rz * fy;
    uy = rz * fx - rx * fz;
    uz = rx * fy;
    ul = sqrtf(ux * ux + uy * uy + uz * uz);
    if (ul < 0.01f) return 0;
    ux /= ul; uy /= ul; uz /= ul;

    dx = fx + rx * (nx * tan_x) + ux * (ny * tan_y);
    dy = fy + uy * (ny * tan_y);
    dz = fz + rz * (nx * tan_x) + uz * (ny * tan_y);
    dl = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dl < 0.001f) return 0;
    dx /= dl; dy /= dl; dz /= dl;

    if (dy >= -0.01f) return 0; /* Ray pointing horizontal or up */

    py = player->actor_class.world.position.y;
    t = (py - play->camera.lookat.eye.y) / dy;
    if (t < 1.0f || t > 8000.0f) return 0;

    /* Refine intersection with terrain height from collision BG */
    test_pos.x = play->camera.lookat.eye.x + dx * t;
    test_pos.y = py;
    test_pos.z = play->camera.lookat.eye.z + dz * t;
    bg_y = mCoBG_GetBgY_OnlyCenter_FromWpos2(test_pos, 0.0f);
    if (fabsf(bg_y - py) > 1.0f && fabsf(bg_y - py) < 800.0f) {
        t2 = (bg_y - play->camera.lookat.eye.y) / dy;
        if (t2 >= 1.0f && t2 <= 8000.0f) {
            t = t2;
        }
    }

    *out_x = play->camera.lookat.eye.x + dx * t;
    *out_z = play->camera.lookat.eye.z + dz * t;
    return 1;
}

/* "npc", "item", or "ground". An NPC wins when one is standing on the point. */
static const char* ground_kind(float wx, float wz) {
    GAME_PLAY* play = get_game_play();
    PLAYER_ACTOR* player;
    xyz_t at;
    mActor_name_t* fg;
    int i;
    if (play == NULL) return "ground";
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return "ground";
    if (play->actor_info.list[ACTOR_PART_NPC].actor != NULL) {
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        for (i = 0; i < list->num_actors; i++) {
            ACTOR* a = &list->actor[i];
            float dx, dz;
            if (a == NULL) continue;
            dx = a->world.position.x - wx;
            dz = a->world.position.z - wz;
            if (dx * dx + dz * dz < 90.0f * 90.0f) return "npc";
        }
    }
    at.x = wx; at.y = player->actor_class.world.position.y; at.z = wz;
    fg = mFI_GetUnitFG(at);
    if (fg != NULL && *fg != EMPTY_NO) return "item";
    return "ground";
}

static void pc_touch_buzz(void) {
    if (!g_pc_settings.touch_haptics) return;
    if (!s_haptic_tried) {
        s_haptic_tried = 1;
        if (SDL_NumHaptics() > 0) {
            s_haptic = SDL_HapticOpen(0);
            if (s_haptic != NULL && SDL_HapticRumbleInit(s_haptic) != 0) {
                SDL_HapticClose(s_haptic);
                s_haptic = NULL;
            }
        }
    }
    if (s_haptic != NULL) SDL_HapticRumblePlay(s_haptic, 0.45f, 28);
}

static void pc_touch_cam_cycle(void) {
    GAME_PLAY* play = get_game_play();
    Camera2* cam;
    int next_dist = 1;
    if (play == NULL) return;
    cam = &play->camera;
    if (cam->indoor_distance_addition_idx == 1) {
        next_dist = 0; /* Normal -> Close */
    } else if (cam->indoor_distance_addition_idx == 0) {
        next_dist = 2; /* Close -> Wide */
    } else {
        next_dist = 1; /* Wide -> Normal */
    }
    cam->indoor_distance_addition_idx = next_dist;
    cam->indoor_direction_addition_idx = 1; /* reset angle to center */
    if (next_dist == 2) {
        sAdo_SysTrgStart(NA_SE_CAMERA_MOVE2);
    } else {
        sAdo_SysTrgStart(NA_SE_CAMERA_MOVE1);
    }
    pc_touch_buzz();
    pc_diag_milestone("cam cycle");
}

static void begin_move(float x, float y, int fresh) {
    float wx, wz, dist_from_player;
    const char* kind;
    GAME_PLAY* play;
    PLAYER_ACTOR* player;
    if (!g_pc_settings.touch_move || !can_steer()) return;
    if (!tap_to_world(x, y, &wx, &wz)) return;
    s_target_x = wx;
    s_target_z = wz;
    s_have_target = 1;
    s_stall = 0;
    s_slide = 0;
    s_best_dist = -1.0f;
    s_mark_x = x;
    s_mark_y = y;
    s_mark_t = 1.0f;
    kind = ground_kind(wx, wz);
    s_interact = (kind[0] != 'g');

    play = get_game_play();
    player = (play != NULL) ? get_player_actor_withoutCheck(play) : NULL;
    if (player != NULL) {
        dist_from_player = hypotf(wx - player->actor_class.world.position.x, wz - player->actor_class.world.position.z);
        if (dist_from_player > 260.0f) {
            s_running = 1;
        } else {
            s_running = 0;
        }
    }

    if (fresh) {
        Uint32 now = SDL_GetTicks();
        char line[32];
        if (s_last_tap_ms != 0 && now - s_last_tap_ms < 320u) s_running = 1;
        s_last_tap_ms = now;
        s_drag_logged = 0;
        s_tap_logged = 1;
        snprintf(s_tap_kind, sizeof(s_tap_kind), "%s", kind);
        snprintf(line, sizeof(line), "tap %s", kind);
        pc_diag_milestone(line);
        pc_touch_buzz();
    }
}

static int submenu_hand_is_empty(Submenu* submenu) {
    if (submenu == NULL || submenu->overlay == NULL || submenu->overlay->hand_ovl == NULL) return 1;
    mHD_Ovl_c* hand_ovl = submenu->overlay->hand_ovl;
    return (hand_ovl->info.item == EMPTY_NO && mMl_check_not_used_mail(&hand_ovl->info.mail));
}

static void refresh_action_label(void) {
    GAME_PLAY* play = get_game_play();
    PLAYER_ACTOR* player;
    xyz_t ahead;
    mActor_name_t* fg;
    int i;
    snprintf(s_action, sizeof(s_action), "A");
    if (play == NULL) return;
    if (message_open()) { snprintf(s_action, sizeof(s_action), "Next"); return; }
    if (submenu_open()) {
        Submenu* sm = &play->submenu;
        Submenu_Overlay_c* ov = sm->overlay;
        mTG_Ovl_c* tovl = (ov != NULL) ? ov->tag_ovl : NULL;
        mTG_tag_c* tag = (tovl != NULL && tovl->sel_tag_idx >= 0 && tovl->sel_tag_idx < mTG_TAG_NUM) ? &tovl->tags[tovl->sel_tag_idx] : NULL;
        if (!submenu_hand_is_empty(sm)) {
            snprintf(s_action, sizeof(s_action), "Drop");
            return;
        }
        if (tag != NULL && tag->type != mTG_TYPE_NONE) {
            snprintf(s_action, sizeof(s_action), "Select");
            return;
        }
        if (tag != NULL && tag->table == mTG_TABLE_ITEM) {
            snprintf(s_action, sizeof(s_action), "Open");
            return;
        }
        snprintf(s_action, sizeof(s_action), "OK");
        return;
    }
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return;
    ahead = player->actor_class.world.position;
    ahead.x += sinf(player->actor_class.shape_info.rotation.y * 3.14159265f / 32768.0f) * 50.0f;
    ahead.z += cosf(player->actor_class.shape_info.rotation.y * 3.14159265f / 32768.0f) * 50.0f;
    fg = mFI_GetUnitFG(ahead);
    if (fg != NULL && *fg != EMPTY_NO) {
        if (IS_ITEM_TREE(*fg)) { snprintf(s_action, sizeof(s_action), "Shake"); return; }
        if (ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_STRUCT || ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_WARP) {
            snprintf(s_action, sizeof(s_action), "Open"); return;
        }
        if (ITEM_IS_ITEM1(*fg)) { snprintf(s_action, sizeof(s_action), "Pick up"); return; }
        snprintf(s_action, sizeof(s_action), "Use");
        return;
    }
    if (play->actor_info.list[ACTOR_PART_NPC].actor != NULL) {
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        for (i = 0; i < list->num_actors; i++) {
            ACTOR* a = &list->actor[i];
            float dx, dz;
            if (a == NULL) continue;
            dx = a->world.position.x - player->actor_class.world.position.x;
            dz = a->world.position.z - player->actor_class.world.position.z;
            if (dx * dx + dz * dz < 140.0f * 140.0f) { snprintf(s_action, sizeof(s_action), "Talk"); return; }
        }
    }
}

static void type_char(char ch) {
#ifdef KEYBOARD_TYPING
    if (g_pc_editor_active) {
        g_pc_typing_mode = 1;
        if (ch == '<') pc_typing_queue_push(PC_TYPING_CMD_BACKSPACE);
        else if (ch == '\n') pc_typing_queue_push(PC_TYPING_CMD_ENTER);
        else if (ch == ' ' || (ch >= 33 && ch < 127)) pc_typing_queue_push((int)ch);
        return;
    }
#endif
    if (pc_play_online_menu_text_active()) {
        SDL_Event e;
        memset(&e, 0, sizeof(e));
        if (ch == '<' || ch == '\n') {
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = (ch == '<') ? SDLK_BACKSPACE : SDLK_RETURN;
            pc_play_online_menu_handle_text_event(&e);
        } else {
            e.type = SDL_TEXTINPUT;
            e.text.text[0] = ch;
            e.text.text[1] = 0;
            pc_play_online_menu_handle_text_event(&e);
        }
    }
}

static void pause_key(SDL_Keycode key) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    pc_pause_menu_handle_event(&e);
}

static void latch_button(unsigned bit, const char* name) {
    char line[64];
    if ((s_latch & bit) == 0) {
        snprintf(line, sizeof(line), "pad %s latched", name);
        pc_diag_milestone(line);
    }
    s_latch |= bit;
    if (s_latch_frames < 3) s_latch_frames = 3;
}

static void latch_consume(unsigned short* buttons) {
    extern u32 pc_frame_counter;
    static u32 s_latch_frame = 0xffffffffu;
    if (s_latch_frames <= 0 || buttons == NULL) return;
    *buttons = (unsigned short)(*buttons | s_latch);
    /* PADRead can run several times in one frame. Count a frame once. */
    if (s_latch_frame == pc_frame_counter) return;
    s_latch_frame = pc_frame_counter;
    s_latch_frames--;
    if (s_latch_frames == 0) {
        if (s_latch & PAD_BUTTON_A) pc_diag_milestone("pad A consumed");
        if (s_latch & PAD_BUTTON_B) pc_diag_milestone("pad B consumed");
        if (s_latch & PAD_BUTTON_X) pc_diag_milestone("pad X consumed");
        if (s_latch & PAD_BUTTON_Y) pc_diag_milestone("pad Y consumed");
        if (s_latch & PAD_BUTTON_START) pc_diag_milestone("pad Start consumed");
        s_latch = 0;
    }
}

static int choice_count_now(void);
static int choice_is_up(void);
static void note_tap(const char* kind);

static void handle_sub_tab_tap(int tab_id) {
    GAME_PLAY* play = get_game_play();
    Submenu* submenu;
    Submenu_Overlay_c* ovl;
    mTG_Ovl_c* tag_ovl;
    mTG_tag_c* tag;

    if (play == NULL) return;
    submenu = &play->submenu;
    ovl = submenu->overlay;
    if (ovl == NULL || ovl->tag_ovl == NULL) return;
    tag_ovl = ovl->tag_ovl;
    if (tag_ovl->sel_tag_idx < 0 || tag_ovl->sel_tag_idx >= mTG_TAG_NUM) return;
    tag = &tag_ovl->tags[tag_ovl->sel_tag_idx];

    if (tab_id == 0) { /* Items / Pockets */
        if (submenu->overlay->inventory_ovl != NULL &&
            submenu->overlay->inventory_ovl->page_order[0] != mIV_PAGE_INVENTORY) {
            tag->table = mTG_TABLE_WCHANGE;
            tag->tag_col = 0;
            tag->tag_row = mIV_PAGE_INVENTORY;
            s_pulse_a = 1;
        } else {
            tag->table = mTG_TABLE_ITEM;
            tag->tag_col = 0;
            tag->tag_row = 0;
            if (tag_ovl->init_tag_data_item_win_proc != NULL) {
                tag_ovl->init_tag_data_item_win_proc(submenu);
            }
        }
        note_tap("tab_items");
    } else if (tab_id == 1) { /* Letters / Mail */
        if (submenu->overlay->inventory_ovl != NULL &&
            submenu->overlay->inventory_ovl->page_order[0] != mIV_PAGE_INVENTORY) {
            tag->table = mTG_TABLE_WCHANGE;
            tag->tag_col = 0;
            tag->tag_row = mIV_PAGE_INVENTORY;
            s_pulse_a = 1;
        }
        tag->table = mTG_TABLE_MAIL;
        tag->tag_col = 0;
        tag->tag_row = 0;
        if (tag_ovl->init_tag_data_item_win_proc != NULL) {
            tag_ovl->init_tag_data_item_win_proc(submenu);
        }
        note_tap("tab_letters");
    } else if (tab_id == 2) { /* Fish */
        tag->table = mTG_TABLE_WCHANGE;
        tag->tag_col = 0;
        tag->tag_row = mIV_PAGE_FISH_COLLECTION;
        s_pulse_a = 1;
        note_tap("tab_fish");
    } else if (tab_id == 3) { /* Bugs */
        tag->table = mTG_TABLE_WCHANGE;
        tag->tag_col = 0;
        tag->tag_row = mIV_PAGE_INSECT_COLLECTION;
        s_pulse_a = 1;
        note_tap("tab_bugs");
    }
}

static void on_submenu_tap(float x, float y) {
    GAME_PLAY* play = get_game_play();
    Submenu* submenu;
    Submenu_Overlay_c* ovl;
    mTG_Ovl_c* tag_ovl;
    mTG_tag_c* tag;
    float cw, ch_43, center_x, center_y, gx, gy;
    int hand_empty;

    if (play == NULL) return;
    submenu = &play->submenu;
    ovl = submenu->overlay;
    if (ovl == NULL || ovl->tag_ovl == NULL) return;
    tag_ovl = ovl->tag_ovl;
    if (tag_ovl->sel_tag_idx < 0 || tag_ovl->sel_tag_idx >= mTG_TAG_NUM) {
        s_pulse_a = 1;
        return;
    }
    tag = &tag_ovl->tags[tag_ovl->sel_tag_idx];

    {
        int bx, by, bw, bh;
        pc_layout_box(2, &bx, &by, &bw, &bh);
        cw = (float)bw;
        ch_43 = (float)bh;
        center_x = (float)bx + cw * 0.5f;
        center_y = (float)by + ch_43 * 0.5f;
    }

    /* Convert touch point to GameCube 2D tag coordinate space:
     * X in [-160, 160], Y in [-120, 120] (GameCube Y is up) */
    gx = (x - center_x) / (cw * 0.5f) * 160.0f;
    gy = -(y - center_y) / (ch_43 * 0.5f) * 120.0f;
    hand_empty = submenu_hand_is_empty(submenu);

    /* 1. Item Action Popup Tag Menu (Wear, Drop, Grab, Quit, etc.) */
    if (tag->type != mTG_TYPE_NONE) {
        float fx, fy;
        float pos_y = 120.0f - (tag->base_pos[1] + tag->body_ofs[1] + tag->text_ofs[1]);
        float pos_x = (tag->base_pos[0] + tag->body_ofs[0] + 160.0f);
        int tapped_row;
        to_font(x, y, &fx, &fy);

        if (fx >= pos_x - 45.0f && fx <= pos_x + 95.0f) {
            tapped_row = (int)((fy - (pos_y - 6.0f)) / 15.0f);
            if (tapped_row >= 0 && tapped_row < 6) {
                tag->tag_row = tapped_row;
                s_pulse_a = 1;
                note_tap("tag_option");
                pc_touch_buzz();
                return;
            }
        }
        /* Tap outside popup cancels it */
        s_pulse_b = 1;
        note_tap("cancel_popup");
        pc_touch_buzz();
        return;
    }

    /* 2. Side Tabs on Inventory Rim */
    /* Right Top: Fish Collection (gx: 85..130, gy: 18..58) */
    if (gx >= 85.0f && gx <= 130.0f && gy >= 18.0f && gy <= 58.0f) {
        tag->table = mTG_TABLE_WCHANGE;
        tag->tag_col = 0;
        tag->tag_row = mIV_PAGE_FISH_COLLECTION;
        s_pulse_a = 1;
        note_tap("tab_fish");
        pc_touch_buzz();
        return;
    }
    /* Right Mid: Face / Designs Tab (gx: 85..130, gy: -18..18) */
    if (gx >= 85.0f && gx <= 130.0f && gy > -18.0f && gy < 18.0f) {
        tag->table = mTG_TABLE_WCHANGE;
        tag->tag_col = 0;
        tag->tag_row = 1;
        s_pulse_a = 1;
        note_tap("tab_face");
        pc_touch_buzz();
        return;
    }
    /* Right Bot: Insect Collection (gx: 85..130, gy: -58..-18) */
    if (gx >= 85.0f && gx <= 130.0f && gy >= -58.0f && gy <= -18.0f) {
        tag->table = mTG_TABLE_WCHANGE;
        tag->tag_col = 0;
        tag->tag_row = mIV_PAGE_INSECT_COLLECTION;
        s_pulse_a = 1;
        note_tap("tab_bugs");
        pc_touch_buzz();
        return;
    }
    /* Left Tab: Original Patterns / Needlework (gx: -130..-85, gy: -25..25) */
    if (gx >= -130.0f && gx <= -85.0f && gy >= -25.0f && gy <= 25.0f) {
        tag->table = mTG_TABLE_INVENTORY_WC_ORG;
        tag->tag_col = 0;
        tag->tag_row = 0;
        s_pulse_a = 1;
        note_tap("tab_designs");
        pc_touch_buzz();
        return;
    }

    /* 3. Fish & Insect Collection Page Grid (8 cols x 5 rows) */
    if (submenu->overlay->inventory_ovl != NULL &&
        submenu->overlay->inventory_ovl->page_order[0] != mIV_PAGE_INVENTORY) {
        int c = (int)roundf((gx - (-89.0f)) / 24.3f);
        int r = (int)roundf((42.0f - gy) / 24.0f);
        if (c >= 0 && c < 8 && r >= 0 && r < 5) {
            tag->table = mTG_TABLE_COLLECT;
            tag->tag_col = c;
            tag->tag_row = r;
            if (tag_ovl->init_tag_data_item_win_proc != NULL) {
                tag_ovl->init_tag_data_item_win_proc(submenu);
            }
            note_tap("collect_item");
            pc_touch_buzz();
            return;
        }
        return;
    }

    /* 4. Dresser / Gyroid storage (mSM_OVL_HANIWA) */
    if (submenu->menu_type == mSM_OVL_HANIWA) {
        if (gy >= 25.0f) {
            int c = (int)roundf((gx - (-106.0f)) / 22.0f);
            int r = (int)roundf((82.0f - gy) / 22.0f);
            if (c >= 0 && c < 9 && r >= 0 && r < 3) {
                int already = (tag->table == mTG_TABLE_HANIWA && tag->tag_col == c && tag->tag_row == r);
                tag->table = mTG_TABLE_HANIWA;
                tag->tag_col = c;
                tag->tag_row = r;
                if (!hand_empty || already) {
                    s_pulse_a = 1;
                } else if (tag_ovl->init_tag_data_item_win_proc != NULL) {
                    tag_ovl->init_tag_data_item_win_proc(submenu);
                }
                note_tap("dresser_slot");
                pc_touch_buzz();
                return;
            }
        }
    }

    /* 5. Player portrait: (-59, 63) */
    {
        float dx = gx - (-59.0f), dy = gy - 63.0f;
        if (dx * dx + dy * dy <= 32.0f * 32.0f) {
            int already = (tag->table == mTG_TABLE_PLAYER);
            tag->table = mTG_TABLE_PLAYER;
            tag->tag_col = 0;
            tag->tag_row = 0;
            if (already || !hand_empty) {
                s_pulse_a = 1;
            } else if (tag_ovl->init_tag_data_item_win_proc != NULL) {
                tag_ovl->init_tag_data_item_win_proc(submenu);
            }
            note_tap("player_portrait");
            pc_diag_milestone("tap player portrait");
            pc_touch_buzz();
            return;
        }
    }

    /* 6. Wallet: (-22, 10) */
    {
        float dx = gx - (-22.0f), dy = gy - 10.0f;
        if (dx * dx + dy * dy <= 28.0f * 28.0f) {
            int already = (tag->table == mTG_TABLE_MONEY);
            tag->table = mTG_TABLE_MONEY;
            tag->tag_col = 0;
            tag->tag_row = 0;
            if (already || !hand_empty) {
                s_pulse_a = 1;
            } else if (tag_ovl->init_tag_data_item_win_proc != NULL) {
                tag_ovl->init_tag_data_item_win_proc(submenu);
            }
            note_tap("wallet");
            pc_touch_buzz();
            return;
        }
    }

    /* 7. Pockets (5 cols x 3 rows) with generous distance-based snapping:
     * cols: {-77, -53, -29, -5, 19}, rows: {-10, -34, -58} */
    if (gx < 38.0f && gy < 12.0f && gy > -76.0f) {
        static const float item_cols[5] = { -77.0f, -53.0f, -29.0f, -5.0f, 19.0f };
        static const float item_rows[3] = { -10.0f, -34.0f, -58.0f };
        int best_c = 0, best_r = 0, c, r;
        float best_d2 = 1e9f;
        for (c = 0; c < 5; c++) {
            for (r = 0; r < 3; r++) {
                float d2 = (gx - item_cols[c]) * (gx - item_cols[c]) + (gy - item_rows[r]) * (gy - item_rows[r]);
                if (d2 < best_d2) { best_d2 = d2; best_c = c; best_r = r; }
            }
        }
        if (best_d2 <= 28.0f * 28.0f) {
            int already = (tag->table == mTG_TABLE_ITEM && tag->tag_col == best_c && tag->tag_row == best_r);
            tag->table = mTG_TABLE_ITEM;
            tag->tag_col = best_c;
            tag->tag_row = best_r;
            if (!hand_empty || already) {
                s_pulse_a = 1;
                note_tap(already ? "item_activate" : "drop_item");
            } else {
                if (tag_ovl->init_tag_data_item_win_proc != NULL) {
                    tag_ovl->init_tag_data_item_win_proc(submenu);
                }
                note_tap("item_inspect");
            }
            pc_touch_buzz();
            return;
        }
    }

    /* 8. Mail (2 cols x 5 rows) with generous distance-based snapping:
     * cols: {55, 79}, rows: {38, 14, -10, -34, -58} */
    if (gx >= 38.0f && gy > -76.0f && gy < 56.0f) {
        static const float mail_cols[2] = { 55.0f, 79.0f };
        static const float mail_rows[5] = { 38.0f, 14.0f, -10.0f, -34.0f, -58.0f };
        int best_c = 0, best_r = 0, c, r;
        float best_d2 = 1e9f;
        for (c = 0; c < 2; c++) {
            for (r = 0; r < 5; r++) {
                float d2 = (gx - mail_cols[c]) * (gx - mail_cols[c]) + (gy - mail_rows[r]) * (gy - mail_rows[r]);
                if (d2 < best_d2) { best_d2 = d2; best_c = c; best_r = r; }
            }
        }
        if (best_d2 <= 28.0f * 28.0f) {
            int already = (tag->table == mTG_TABLE_MAIL && tag->tag_col == best_c && tag->tag_row == best_r);
            tag->table = mTG_TABLE_MAIL;
            tag->tag_col = best_c;
            tag->tag_row = best_r;
            if (!hand_empty || already) {
                s_pulse_a = 1;
                note_tap(already ? "mail_activate" : "drop_mail");
            } else {
                if (tag_ovl->init_tag_data_item_win_proc != NULL) {
                    tag_ovl->init_tag_data_item_win_proc(submenu);
                }
                note_tap("mail_inspect");
            }
            pc_touch_buzz();
            return;
        }
    }
}

static void on_picture_release(float x, float y) {
    float fx, fy;
    if (!in_picture(x, y)) return;
    to_font(x, y, &fx, &fy);
    if (g_pc_paused) { pc_pause_menu_pointer(fx, fy); return; }
    if (pc_play_online_menu_active()) {
        if (!pc_play_online_menu_text_active()) pc_play_online_menu_pointer(fx, fy);
        return;
    }
    if (pc_play_offline_menu_active()) {
        pc_play_offline_menu_pointer(fx, fy);
        return;
    }
    if (pc_settings_menu_active()) { pc_settings_menu_pointer(fx, fy); return; }
    if (pc_creator_menu_active()) { pc_creator_menu_pointer(fx, fy); note_tap("creator"); return; }
    if (g_pc_title_main_menu_visible) {
        int i = (int)((fy - 126.0f) / 18.0f);
        if (i >= 0 && i < 5 && fx > 30.0f && fx < 290.0f) {
            char line[64];
            s_title_valid = 1;
            s_title_life = 3;
            s_title_y = 135.0f + (float)i * 18.0f;
            latch_button(PAD_BUTTON_A, "A");
            snprintf(line, sizeof(line), "touch title row %d", i);
            pc_diag_milestone(line);
            note_tap("title");
        } else {
            char line[64];
            snprintf(line, sizeof(line), "touch picture fy=%.0f fx=%.0f", fy, fx);
            pc_diag_milestone(line);
        }
        return;
    }
    if (choice_is_up()) {
        mChoice_c* c = mChoice_Get_base_window_p();
        if (c != NULL && c->data.choice_num > 1) {
            float cy = (c->center_y > 10.0f) ? c->center_y : 120.0f;
            float half_h = (float)c->data.choice_num * 14.0f;
            if (fy < cy - half_h * 0.2f) {
                c->selected_choice_idx = 0;
            } else if (fy > cy + half_h * 0.2f) {
                c->selected_choice_idx = c->data.choice_num - 1;
            } else {
                int idx = (int)((fy - (cy - half_h)) / (2.0f * half_h / (float)c->data.choice_num));
                if (idx >= 0 && idx < c->data.choice_num) c->selected_choice_idx = idx;
            }
        }
        s_pulse_a = 1;
        note_tap("choice");
        return;
    }
    if (submenu_open()) {
        on_submenu_tap(x, y);
        return;
    }
    s_pulse_a = 1;
    note_tap("text");
}

static int choice_count_now(void) {
    if (get_game_play() == NULL) return 0;
    mChoice_c* c = mChoice_Get_base_window_p();
    if (c == NULL || !mChoice_check_main_normal(c)) return 0;
    if (c->data.choice_num < 1) return 0;
    return c->data.choice_num;
}

/* True from the moment a choice window is on screen, including the appear
 * animation. A text-advance tap must not confirm the highlighted row. */
static int choice_is_up(void) {
    if (get_game_play() == NULL) return 0;
    mChoice_c* c = mChoice_Get_base_window_p();
    if (c == NULL || c->data.choice_num < 1) return 0;
    return c->main_index != mChoice_MAIN_HIDE;
}

static void note_tap(const char* kind) {
    if (kind == NULL) return;
    snprintf(s_tap_kind, sizeof(s_tap_kind), "%s", kind);
}

static void pick_choice(int i) {
    mChoice_c* c = mChoice_Get_base_window_p();
    char line[32];
    if (c == NULL || i < 0 || i >= c->data.choice_num) return;
    c->selected_choice_idx = i;
    s_choice_confirm = 1;
    s_pulse_a = 1;
    note_tap("choice");
    snprintf(line, sizeof(line), "tap choice %d", i + 1);
    pc_diag_milestone(line);
}

static int read_point(float* x, float* y, float* x2, float* y2, int* n) {
    int i, mx = 0, my = 0;
    Uint32 mouse;
    *n = 0;
    for (i = 0; i < FINGERS && *n < 2; i++) {
        if (!s_fin[i].down) continue;
        if (*n == 0) { *x = s_fin[i].x; *y = s_fin[i].y; }
        else { *x2 = s_fin[i].x; *y2 = s_fin[i].y; }
        (*n)++;
    }
    if (*n > 0) return 1;
    if (SDL_GetNumTouchDevices() > 0) {
        SDL_TouchID id = SDL_GetTouchDevice(0);
        int nf = SDL_GetNumTouchFingers(id);
        for (i = 0; i < nf && *n < 2; i++) {
            SDL_Finger* f = SDL_GetTouchFinger(id, i);
            if (f == NULL) continue;
            if (*n == 0) { *x = f->x * (float)g_pc_window_w; *y = f->y * (float)g_pc_window_h; }
            else { *x2 = f->x * (float)g_pc_window_w; *y2 = f->y * (float)g_pc_window_h; }
            (*n)++;
        }
    }
    mouse = SDL_GetMouseState(&mx, &my);
    if (*n == 0 && (mouse & SDL_BUTTON(SDL_BUTTON_LEFT))) {
        *x = (float)mx; *y = (float)my; *n = 1;
    }
    if (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) s_have_target = 0;
    return *n > 0;
}

void pc_touch_apply(unsigned short* buttons, signed char* stick_x, signed char* stick_y,
                    signed char* cstick_x, signed char* cstick_y,
                    int* wheel_hold, int* emote_hold, int* pointer_x, int* pointer_y) {
    Hud hud;
    float x = 0, y = 0, x2 = 0, y2 = 0;
    int n = 0, down, rising, hit, key = -1, tap_handled = 0;
    (void)wheel_hold; (void)emote_hold; (void)pointer_x; (void)pointer_y;
    if (s_title_life > 0 && --s_title_life == 0) s_title_valid = 0;
    refresh_action_label();
    if (pc_bedroom_claim_prompt_active()) {
        if (buttons != NULL) {
            if (*buttons & PAD_BUTTON_A) {
                *buttons &= ~PAD_BUTTON_A;
                pc_bedroom_confirm_claim();
                return;
            }
            if (*buttons & PAD_BUTTON_B) {
                *buttons &= ~PAD_BUTTON_B;
                pc_bedroom_cancel_claim();
                return;
            }
        }
    }
    if (!pc_touch_enabled() || buttons == NULL || g_pc_window_w < 2) return;
    hud_build(&hud);
    if (s_cam_pulse_timer > 0) {
        if (cstick_x) *cstick_x = (signed char)s_cam_pulse_x;
        if (cstick_y) *cstick_y = (signed char)s_cam_pulse_y;
        s_cam_pulse_timer--;
        if (s_cam_pulse_timer == 0) {
            s_cam_pulse_x = 0;
            s_cam_pulse_y = 0;
        }
    }
    if (s_cam_cooldown > 0) {
        s_cam_cooldown--;
    }
    if (s_pending_drop && submenu_open()) {
        GAME_PLAY* play = get_game_play();
        if (play != NULL && play->submenu.overlay != NULL && play->submenu.overlay->tag_ovl != NULL) {
            mTG_tag_c* tag = &play->submenu.overlay->tag_ovl->tags[0];
            if (tag->type != mTG_TYPE_NONE) {
                tag->tag_row = 1;
                s_pulse_a = 1;
                s_pending_drop = 0;
                pc_touch_buzz();
                note_tap("mobile_sheet_drop");
            }
        }
    }
    if (s_tap_ready) {
        float dx = s_gest_x - s_tap_x, dy = s_gest_y - s_tap_y;
        s_tap_ready = 0;
        tap_handled = 1;
        if (dx * dx + dy * dy < 64.0f * 64.0f) {
            if (pc_bedroom_claim_prompt_active()) {
                float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
                float ui = fmaxf(1.0f, H / 720.0f);
                float cw = fminf(380.0f * ui, W * 0.90f);
                float ch = 190.0f * ui;
                float cx = W * 0.5f, cy = H * 0.5f;
                float bh = 42.0f * ui;
                float bw = (cw - 40.0f * ui) * 0.46f;
                float btn_y = cy + 45.0f * ui;
                float claim_cx = cx - bw * 0.55f;
                float cancel_cx = cx + bw * 0.55f;
                if (fabsf(s_tap_x - claim_cx) <= bw * 0.5f && fabsf(s_tap_y - btn_y) <= bh * 0.5f) {
                    pc_touch_buzz();
                    pc_bedroom_confirm_claim();
                    return;
                }
                if (fabsf(s_tap_x - cancel_cx) <= bw * 0.5f && fabsf(s_tap_y - btn_y) <= bh * 0.5f) {
                    pc_touch_buzz();
                    pc_bedroom_cancel_claim();
                    return;
                }
                if (fabsf(s_tap_x - cx) > cw * 0.5f || fabsf(s_tap_y - cy) > ch * 0.5f) {
                    pc_touch_buzz();
                    pc_bedroom_cancel_claim();
                    return;
                }
                return;
            }
            int th = 0, tk = -1;
            th = classify(s_tap_x, s_tap_y, &hud, &tk);
            if (th == HIT_SHEET_CLOSE) {
                latch_button(PAD_BUTTON_B, "B");
                s_sheet.active = 0;
                s_sheet.swap_mode = 0;
                pc_touch_buzz();
                note_tap("sheet_close");
            } else if (th == HIT_SHEET_BELLS) {
                mobile_sheet_handle_bells();
            } else if (th == HIT_SHEET_TAB) {
                mobile_sheet_handle_tab(tk);
            } else if (th == HIT_SHEET_CARD) {
                mobile_sheet_handle_card(tk);
            } else if (th == HIT_SHEET_ACT) {
                mobile_sheet_handle_act(tk);
            } else if (th == HIT_SHEET_BODY) {
                /* Swallowed touch on sheet background */
            } else if (th == HIT_PICTURE) {
                if (can_steer()) {
                    if (!s_tap_logged) begin_move(s_tap_x, s_tap_y, 1);
                } else on_picture_release(s_tap_x, s_tap_y);
            }
            else if (th == HIT_NUB) {
                g_pc_settings.touch_stick = !g_pc_settings.touch_stick;
                pc_settings_save();
                pc_touch_buzz();
                pc_diag_milestone(g_pc_settings.touch_stick ? "touch stick shown" : "touch stick hidden");
            } else if (th == HIT_MENU) {
                pc_touch_buzz();
                pc_diag_milestone("touch menu");
                if (g_pc_title_main_menu_visible && !pc_settings_menu_active() && !pc_play_online_menu_active() && !pc_play_offline_menu_active())
                    latch_button(PAD_BUTTON_START, "Start");
                else if (message_open() || submenu_open() || pc_settings_menu_active() || pc_play_online_menu_active() || pc_play_offline_menu_active())
                    latch_button(PAD_BUTTON_B, "B");
                else if (g_pc_paused) pause_key(SDLK_ESCAPE);
                else pc_pause_menu_toggle();
            } else if (th == HIT_ACTION) {
                latch_button(PAD_BUTTON_A, "A");
                pc_touch_buzz();
                pc_diag_milestone("touch action");
            } else if (th == HIT_BACK || th == HIT_CLOSE) {
                latch_button(PAD_BUTTON_B, "B");
                pc_touch_buzz();
                pc_diag_milestone(th == HIT_CLOSE ? "touch close" : "touch back");
                note_tap(th == HIT_CLOSE ? "close" : "back");
                if (g_pc_paused) pause_key(SDLK_ESCAPE);
            } else if (th == HIT_CONTEXT) {
                latch_button(PAD_BUTTON_A, "A");
                pc_touch_buzz();
                pc_diag_milestone("touch context");
                note_tap("context");
            } else if (th == HIT_CHOICE) {
                pick_choice(tk);
                pc_touch_buzz();
            } else if (th == HIT_STORAGE) {
                pc_touch_buzz();
                pc_diag_milestone("touch storage");
                if (!submenu_open() && get_game_play() != NULL) {
                    pc_cloud_storage_client_open(NULL);
                    pc_dresser_ui_begin(NULL);
                    mSM_open_submenu(&get_game_play()->submenu, mSM_OVL_INVENTORY, mSM_IV_OPEN_HANIWA_ENTRUST, 0);
                }
            } else if (th == HIT_STOR_PREV) {
                pc_touch_buzz();
                pc_cloud_storage_client_prev_page();
                pc_dresser_ui_note_input();
                sAdo_SysTrgStart(NA_SE_CURSOL);
            } else if (th == HIT_STOR_NEXT) {
                pc_touch_buzz();
                pc_cloud_storage_client_next_page();
                pc_dresser_ui_note_input();
                sAdo_SysTrgStart(NA_SE_CURSOL);
            } else if (th == HIT_BAG) {
                latch_button(PAD_BUTTON_Y, "Y");
                pc_touch_buzz();
                pc_diag_milestone("touch inventory");
            } else if (th == HIT_MAP) {
                latch_button(PAD_BUTTON_X, "X");
                pc_touch_buzz();
                pc_diag_milestone("touch map");
            } else if (th == HIT_CAM) {
                pc_touch_cam_cycle();
            } else if (th == HIT_SUB_TAB) {
                handle_sub_tab_tap(tk);
                pc_touch_buzz();
            } else if (th == HIT_KEY) {
                int i;
                for (i = 0; i < hud.key_n; i++) {
                    float r = (hud.kc[i] == ' ' || hud.kc[i] == '\n') ? hud.kr * 1.6f : hud.kr;
                    if (hit_disc(s_tap_x, s_tap_y, hud.kx[i], hud.ky[i], r)) type_char(hud.kc[i]);
                }
            }
        }
    }
    if (s_pulse_a) { latch_button(PAD_BUTTON_A, "A"); s_pulse_a = 0; pc_touch_buzz(); }
    if (s_pulse_b) { latch_button(PAD_BUTTON_B, "B"); s_pulse_b = 0; pc_touch_buzz(); }
    latch_consume(buttons);
    down = read_point(&x, &y, &x2, &y2, &n);
    rising = down && !s_pointer_was;
    if (n >= 2 && g_pc_settings.touch_camera) {
        float cur_dist = hypotf(x2 - x, y2 - y);
        float mid_x = (x + x2) * 0.5f;
        float mid_y = (y + y2) * 0.5f;

        if (!s_cam_two_finger_was || s_cam_prev_dist <= 0.0f) {
            s_cam_two_finger_was = 1;
            s_cam_prev_dist = cur_dist;
            s_cam_prev_mid_x = mid_x;
            s_cam_prev_mid_y = mid_y;
            s_cam_accum_pinch = 0.0f;
            s_cam_accum_dx = 0.0f;
            s_cam_accum_dy = 0.0f;
        } else {
            float d_dist = cur_dist - s_cam_prev_dist;
            float d_mid_x = mid_x - s_cam_prev_mid_x;
            float d_mid_y = mid_y - s_cam_prev_mid_y;

            s_cam_prev_dist = cur_dist;
            s_cam_prev_mid_x = mid_x;
            s_cam_prev_mid_y = mid_y;

            s_cam_accum_pinch += d_dist;
            s_cam_accum_dx += d_mid_x;
            s_cam_accum_dy += d_mid_y;

            if (s_cam_cooldown == 0) {
                if (s_cam_accum_pinch > 35.0f) {
                    /* Pinch out (spread fingers) -> Zoom in */
                    s_cam_pulse_y = 80;
                    s_cam_pulse_x = 0;
                    s_cam_pulse_timer = 3;
                    s_cam_cooldown = 14;
                    s_cam_accum_pinch = 0.0f;
                    s_cam_accum_dx = 0.0f;
                    s_cam_accum_dy = 0.0f;
                    pc_touch_buzz();
                    pc_diag_milestone("pinch zoom in");
                } else if (s_cam_accum_pinch < -35.0f) {
                    /* Pinch in (contract fingers) -> Zoom out */
                    s_cam_pulse_y = -80;
                    s_cam_pulse_x = 0;
                    s_cam_pulse_timer = 3;
                    s_cam_cooldown = 14;
                    s_cam_accum_pinch = 0.0f;
                    s_cam_accum_dx = 0.0f;
                    s_cam_accum_dy = 0.0f;
                    pc_touch_buzz();
                    pc_diag_milestone("pinch zoom out");
                } else if (fabsf(s_cam_accum_dx) > 38.0f && fabsf(s_cam_accum_dx) > fabsf(s_cam_accum_dy) * 1.2f) {
                    /* Two-finger swipe horizontal -> Rotate camera */
                    s_cam_pulse_x = (s_cam_accum_dx > 0.0f) ? 80 : -80;
                    s_cam_pulse_y = 0;
                    s_cam_pulse_timer = 3;
                    s_cam_cooldown = 14;
                    s_cam_accum_pinch = 0.0f;
                    s_cam_accum_dx = 0.0f;
                    s_cam_accum_dy = 0.0f;
                    pc_touch_buzz();
                    pc_diag_milestone(s_cam_pulse_x > 0 ? "swipe rotate right" : "swipe rotate left");
                } else if (fabsf(s_cam_accum_dy) > 38.0f && fabsf(s_cam_accum_dy) > fabsf(s_cam_accum_dx) * 1.2f) {
                    /* Two-finger swipe vertical -> Zoom / tilt */
                    s_cam_pulse_y = (s_cam_accum_dy < 0.0f) ? 80 : -80;
                    s_cam_pulse_x = 0;
                    s_cam_pulse_timer = 3;
                    s_cam_cooldown = 14;
                    s_cam_accum_pinch = 0.0f;
                    s_cam_accum_dx = 0.0f;
                    s_cam_accum_dy = 0.0f;
                    pc_touch_buzz();
                    pc_diag_milestone(s_cam_pulse_y > 0 ? "swipe zoom in" : "swipe zoom out");
                }
            }
        }
        if (s_cam_pulse_timer > 0) {
            if (cstick_x) *cstick_x = (signed char)s_cam_pulse_x;
            if (cstick_y) *cstick_y = (signed char)s_cam_pulse_y;
        }
        s_have_target = 0;
        s_drag_active = 0;
        s_pointer_was = 1;
        latch_consume(buttons);
        return;
    } else {
        s_cam_two_finger_was = 0;
        s_cam_prev_dist = 0.0f;
    }
    if (!down) {
        if (s_drag_active) {
            float ddx = s_last_x - s_drag_ox, ddy = s_last_y - s_drag_oy;
            if (!tap_handled && (!s_drag_moved || (SDL_GetTicks() - s_drag_t0 < 250 && hypotf(ddx, ddy) < 22.0f))) {
                if (can_steer()) begin_move(s_down_x, s_down_y, 1);
                else on_picture_release(s_down_x, s_down_y);
            }
            s_drag_active = 0;
            s_drag_moved = 0;
        }
        if (!tap_handled && s_pointer_was && hypotf(s_last_x - s_down_x, s_last_y - s_down_y) < 36.0f) {
            if (s_down_hit == HIT_SHEET_CLOSE) {
                latch_button(PAD_BUTTON_B, "B");
                s_sheet.active = 0;
                s_sheet.swap_mode = 0;
                pc_touch_buzz();
                note_tap("sheet_close");
            } else if (s_down_hit == HIT_SHEET_BELLS) {
                mobile_sheet_handle_bells();
            } else if (s_down_hit == HIT_SHEET_TAB) {
                mobile_sheet_handle_tab(key);
            } else if (s_down_hit == HIT_SHEET_CARD) {
                mobile_sheet_handle_card(key);
            } else if (s_down_hit == HIT_SHEET_ACT) {
                mobile_sheet_handle_act(key);
            } else if (s_down_hit == HIT_PICTURE && !can_steer()) on_picture_release(s_down_x, s_down_y);
            else if (s_down_hit == HIT_SUB_TAB) {
                handle_sub_tab_tap(key);
                pc_touch_buzz();
            } else if (s_down_hit == HIT_NUB) {
                g_pc_settings.touch_stick = !g_pc_settings.touch_stick;
                pc_settings_save();
                pc_touch_buzz();
                pc_diag_milestone(g_pc_settings.touch_stick ? "touch stick shown" : "touch stick hidden");
            } else if (s_down_hit == HIT_MENU) {
                pc_touch_buzz();
                pc_diag_milestone("touch menu");
                if (message_open() || submenu_open() || pc_settings_menu_active() || pc_play_online_menu_active() || pc_play_offline_menu_active())
                    s_pulse_b = 1;
                else if (g_pc_paused) pause_key(SDLK_ESCAPE);
                else pc_pause_menu_toggle();
            } else if (s_down_hit == HIT_CAM) {
                float cdx = s_last_x - s_down_x;
                if (cdx > 25.0f) {
                    GAME_PLAY* play = get_game_play();
                    if (play != NULL) {
                        play->camera.indoor_direction_addition_idx = 2;
                        sAdo_SysTrgStart(NA_SE_CAMERA_MOVE1);
                        pc_touch_buzz();
                        pc_diag_milestone("cam rotate right");
                    }
                } else if (cdx < -25.0f) {
                    GAME_PLAY* play = get_game_play();
                    if (play != NULL) {
                        play->camera.indoor_direction_addition_idx = 0;
                        sAdo_SysTrgStart(NA_SE_CAMERA_MOVE1);
                        pc_touch_buzz();
                        pc_diag_milestone("cam rotate left");
                    }
                } else {
                    pc_touch_cam_cycle();
                }
            } else if (s_down_hit == HIT_KEY) {
                Hud now;
                hud_build(&now);
                if (s_down_hit == HIT_KEY && key >= 0) { /* key index stored below */ }
                (void)now;
            }
        }
        if (s_pointer_was && s_down_hit == HIT_KEY) {
            Hud now;
            int i, which = -1;
            hud_build(&now);
            for (i = 0; i < now.key_n; i++) {
                float r = (now.kc[i] == ' ' || now.kc[i] == '\n') ? now.kr * 1.6f : now.kr;
                if (hit_disc(s_down_x, s_down_y, now.kx[i], now.ky[i], r)) which = i;
            }
            if (which >= 0) type_char(now.kc[which]);
        }
        if (s_pointer_was && submenu_open() && !s_sheet.active) {
            float dx = s_last_x - s_down_x, dy = s_last_y - s_down_y;
            float dist = hypotf(dx, dy);
            if (dist > 45.0f) {
                if (fabsf(dx) > fabsf(dy)) {
                    s_swipe = (dx > 0.0f) ? 1 : -1;
                } else {
                    s_swipe = (dy < 0.0f) ? 2 : -2;
                }
                pc_touch_buzz();
                pc_diag_milestone("submenu swipe");
            }
        } else if (s_pointer_was && s_hud == HUD_MENU) {
            float dx = s_last_x - s_down_x, dy = s_last_y - s_down_y;
            if (fabsf(dy) > 56.0f && fabsf(dy) > fabsf(dx)) {
                s_swipe = (dy < 0.0f) ? 70 : -70;
                pc_diag_milestone("swipe");
            }
        }
        s_pointer_was = 0;
        s_down_hit = HIT_NONE;
        s_tap_logged = 0;
        s_knob_x = s_knob_y = 0.0f;
    } else {
        hit = classify(x, y, &hud, &key);
        if (g_pc_window_w != s_lay_w || g_pc_window_h != s_lay_h) {
            s_lay_w = g_pc_window_w;
            s_lay_h = g_pc_window_h;
            if (!rising) s_down_hit = hit; /* fold or unfold: the finger keeps whatever disc is under it now */
        }
        if (rising) {
            s_down_x = x; s_down_y = y; s_down_hit = hit;
            s_tap_logged = 0;
            if (hit == HIT_SHEET_CLOSE) {
                *buttons |= PAD_BUTTON_B;
                s_sheet.active = 0;
                s_sheet.swap_mode = 0;
                pc_touch_buzz();
                note_tap("sheet_close");
            } else if (hit == HIT_SHEET_BELLS) {
                mobile_sheet_handle_bells();
            } else if (hit == HIT_SHEET_TAB) {
                mobile_sheet_handle_tab(key);
            } else if (hit == HIT_SHEET_CARD) {
                mobile_sheet_handle_card(key);
            } else if (hit == HIT_SHEET_ACT) {
                mobile_sheet_handle_act(key);
            }
            if (hit == HIT_PICTURE && can_steer()) {
                s_drag_active = 1;
                s_drag_ox = x; s_drag_oy = y;
                s_drag_cx = x; s_drag_cy = y;
                s_drag_t0 = SDL_GetTicks();
                s_drag_moved = 0;
            } else if (hit != HIT_NONE && hit != HIT_PICTURE) {
                s_have_target = 0;
                s_drag_active = 0;
            }
            if (hit == HIT_SUB_TAB) {
                handle_sub_tab_tap(key);
                pc_touch_buzz();
            }
            if (hit == HIT_BAG) *buttons |= PAD_BUTTON_Y;
            if (hit == HIT_MAP) *buttons |= PAD_BUTTON_X;
            if (hit == HIT_ACTION || hit == HIT_CONTEXT) *buttons |= PAD_BUTTON_A;
            if ((hit == HIT_BACK || hit == HIT_CLOSE) && !g_pc_paused) *buttons |= PAD_BUTTON_B;
        } else if (s_down_hit == HIT_PICTURE && can_steer() && s_drag_active) {
            float dx = x - s_drag_ox;
            float dy = y - s_drag_oy;
            float dist = hypotf(dx, dy);
            float max_r = 75.0f * ((float)g_pc_settings.touch_hud_scale / 100.0f);
            if (max_r < 35.0f) max_r = 35.0f;
            s_drag_cx = x; s_drag_cy = y;
            if (dist > 12.0f) {
                float clamp_d = dist > max_r ? max_r : dist;
                float sx = (dx / dist) * (clamp_d / max_r) * 127.0f;
                float sy = (-dy / dist) * (clamp_d / max_r) * 127.0f;
                s_drag_moved = 1;
                s_have_target = 0;
                if (stick_x && stick_y && *stick_x == 0 && *stick_y == 0) {
                    *stick_x = (signed char)sx;
                    *stick_y = (signed char)sy;
                }
                if (dist > max_r * 0.68f && buttons != NULL) {
                    *buttons |= PAD_BUTTON_B; /* Long drag runs! */
                }
            }
        }
        s_last_x = x; s_last_y = y;
        if (choice_is_up() && s_pointer_was) {
            float dy = y - s_down_y;
            if (dy < -30.0f && stick_y && *stick_y == 0) {
                *stick_y = 80;
            } else if (dy > 30.0f && stick_y && *stick_y == 0) {
                *stick_y = -80;
            }
        }
        if (s_down_hit == HIT_ACTION || hit == HIT_ACTION || s_down_hit == HIT_CONTEXT || hit == HIT_CONTEXT) {
            *buttons |= PAD_BUTTON_A;
        } else if (s_down_hit == HIT_BACK || hit == HIT_BACK || s_down_hit == HIT_CLOSE || hit == HIT_CLOSE) {
            if (!g_pc_paused) *buttons |= PAD_BUTTON_B;
        } else if (s_down_hit == HIT_BAG || hit == HIT_BAG) {
            *buttons |= PAD_BUTTON_Y;
        } else if (s_down_hit == HIT_MAP || hit == HIT_MAP) {
            *buttons |= PAD_BUTTON_X;
        }
        s_pointer_was = 1;
    }
    latch_consume(buttons);
    if (s_swipe != 0) {
        if (s_swipe == 1 && stick_x != NULL && *stick_x == 0) *stick_x = 90;
        else if (s_swipe == -1 && stick_x != NULL && *stick_x == 0) *stick_x = -90;
        else if (s_swipe == 2 && stick_y != NULL && *stick_y == 0) *stick_y = 90;
        else if (s_swipe == -2 && stick_y != NULL && *stick_y == 0) *stick_y = -90;
        else if (s_swipe > 10 && stick_y != NULL && *stick_y == 0) *stick_y = (signed char)s_swipe;
        s_swipe = 0;
    }
    if (s_have_target && stick_x && stick_y && *stick_x == 0 && *stick_y == 0)
        (void)steer_toward(s_target_x, s_target_z, stick_x, stick_y);
    if (s_running && s_have_target && can_steer() && buttons != NULL) *buttons |= PAD_BUTTON_B;
    if (!s_have_target) s_running = 0;
    /* Choices are confirmed by Action button (A), screen tap, or bubble */
    s_choice_confirm = 0;
    (void)key;
}

void pc_touch_draw(struct game_s* game) { (void)game; }

/* Soft disc & authentic AC font texture. uv is -1..1 for circles, 0 for rects, >=2.0 for font glyphs. */
static GLuint s_prog = 0, s_vao = 0, s_vbo = 0, s_font_tex = 0;
static int s_gl_ready = 0, s_gl_failed = 0;

static void init_font_texture(void) {
    if (s_font_tex != 0 && glIsTexture(s_font_tex)) return;
    s_font_tex = 0;
    static u8 font_rgba[192 * 256 * 4];
    const u8* src = FONT_nes_tex_font1;
    int bw = 192 / 8; /* 24 blocks */
    int bh = 256 / 8; /* 32 blocks */
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x += 2) {
                    u8 val = *src++;
                    int px0 = bx * 8 + x;
                    int px1 = px0 + 1;
                    int py = by * 8 + y;
                    u8 i0 = (val >> 4) | (val & 0xF0);
                    u8 i1 = (val & 0x0F) | ((val & 0x0F) << 4);
                    int idx0 = (py * 192 + px0) * 4;
                    int idx1 = (py * 192 + px1) * 4;
                    font_rgba[idx0] = font_rgba[idx0 + 1] = font_rgba[idx0 + 2] = 255;
                    font_rgba[idx0 + 3] = i0;
                    font_rgba[idx1] = font_rgba[idx1 + 1] = font_rgba[idx1 + 2] = 255;
                    font_rgba[idx1 + 3] = i1;
                }
            }
        }
    }
    glGenTextures(1, &s_font_tex);
    glBindTexture(GL_TEXTURE_2D, s_font_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 192, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, font_rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static GLuint compile_one(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        pc_diag_milestone(log);
    }
    return s;
}

static void gl_init(void) {
#ifdef PC_GLES
    const char* vs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec4 aColor;\n"
        "layout(location=2) in vec2 aUv;\n"
        "out vec4 vColor;\n"
        "out vec2 vUv;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vColor = aColor;\n"
        "    vUv = aUv;\n"
        "}\n";
    const char* fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D uFont;\n"
        "in vec4 vColor;\n"
        "in vec2 vUv;\n"
        "out vec4 frag;\n"
        "void main() {\n"
        "    if (vUv.x >= 2.0) {\n"
        "        float a = texture(uFont, vec2(vUv.x - 2.0, vUv.y)).a;\n"
        "        if (a < 0.03) discard;\n"
        "        frag = vec4(vColor.rgb, vColor.a * a);\n"
        "    } else {\n"
        "        float d = length(vUv);\n"
        "        float a = smoothstep(1.0, 0.82, d);\n"
        "        if (a < 0.02) discard;\n"
        "        frag = vec4(vColor.rgb, vColor.a * a);\n"
        "    }\n"
        "}\n";
#else
    const char* vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec4 aColor;\n"
        "layout(location=2) in vec2 aUv;\n"
        "out vec4 vColor;\n"
        "out vec2 vUv;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vColor = aColor;\n"
        "    vUv = aUv;\n"
        "}\n";
    const char* fs =
        "#version 330 core\n"
        "uniform sampler2D uFont;\n"
        "in vec4 vColor;\n"
        "in vec2 vUv;\n"
        "out vec4 frag;\n"
        "void main() {\n"
        "    if (vUv.x >= 2.0) {\n"
        "        float a = texture(uFont, vec2(vUv.x - 2.0, vUv.y)).a;\n"
        "        if (a < 0.03) discard;\n"
        "        frag = vec4(vColor.rgb, vColor.a * a);\n"
        "    } else {\n"
        "        float d = length(vUv);\n"
        "        float a = smoothstep(1.0, 0.82, d);\n"
        "        if (a < 0.02) discard;\n"
        "        frag = vec4(vColor.rgb, vColor.a * a);\n"
        "    }\n"
        "}\n";
#endif
    GLuint v, f, p;
    GLint ok = 0;
    if (s_gl_ready || s_gl_failed) {
        init_font_texture();
        return;
    }
    v = compile_one(GL_VERTEX_SHADER, vs);
    f = compile_one(GL_FRAGMENT_SHADER, fs);
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        s_gl_failed = 1;
        glDeleteProgram(p);
        pc_diag_milestone("touch overlay shader failed");
        return;
    }
    s_prog = p;
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    init_font_texture();
    s_gl_ready = 1;
}

static float s_vert[8 * 32000];
static int s_nvert = 0;

static void vert(float x, float y, float u, float v, float r, float g, float b, float a) {
    float* p;
    if (s_nvert >= 32000) return;
    p = s_vert + s_nvert * 8;
    p[0] = x; p[1] = y; p[2] = r; p[3] = g; p[4] = b; p[5] = a; p[6] = u; p[7] = v;
    s_nvert++;
}

static float s_fade = 1.0f;

static void disc_raw(float cx, float cy, float rad, float r, float g, float b, float a) {
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float x0 = (cx - rad) / W * 2.0f - 1.0f;
    float x1 = (cx + rad) / W * 2.0f - 1.0f;
    float y0 = 1.0f - (cy + rad) / H * 2.0f;
    float y1 = 1.0f - (cy - rad) / H * 2.0f;
    vert(x0, y0, -1, -1, r, g, b, a);
    vert(x1, y0, 1, -1, r, g, b, a);
    vert(x1, y1, 1, 1, r, g, b, a);
    vert(x0, y0, -1, -1, r, g, b, a);
    vert(x1, y1, 1, 1, r, g, b, a);
    vert(x0, y1, -1, 1, r, g, b, a);
}

static void rect_raw(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float rx0 = x0 / W * 2.0f - 1.0f;
    float rx1 = x1 / W * 2.0f - 1.0f;
    float ry0 = 1.0f - y1 / H * 2.0f;
    float ry1 = 1.0f - y0 / H * 2.0f;
    vert(rx0, ry0, 0.0f, 0.0f, r, g, b, a);
    vert(rx1, ry0, 0.0f, 0.0f, r, g, b, a);
    vert(rx1, ry1, 0.0f, 0.0f, r, g, b, a);
    vert(rx0, ry0, 0.0f, 0.0f, r, g, b, a);
    vert(rx1, ry1, 0.0f, 0.0f, r, g, b, a);
    vert(rx0, ry1, 0.0f, 0.0f, r, g, b, a);
}

static void corner_quarter_raw(float cx, float cy, float rad, float qx, float qy, float r, float g, float b, float a) {
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float px0 = (qx < 0.0f) ? (cx - rad) : cx;
    float px1 = (qx < 0.0f) ? cx : (cx + rad);
    float py0 = (qy < 0.0f) ? (cy - rad) : cy;
    float py1 = (qy < 0.0f) ? cy : (cy + rad);
    float u0 = (qx < 0.0f) ? -1.0f : 0.0f;
    float u1 = (qx < 0.0f) ? 0.0f : 1.0f;
    float v0 = (qy < 0.0f) ? -1.0f : 0.0f;
    float v1 = (qy < 0.0f) ? 0.0f : 1.0f;
    float rx0 = px0 / W * 2.0f - 1.0f;
    float rx1 = px1 / W * 2.0f - 1.0f;
    float ry0 = 1.0f - py1 / H * 2.0f;
    float ry1 = 1.0f - py0 / H * 2.0f;
    vert(rx0, ry0, u0, v1, r, g, b, a);
    vert(rx1, ry0, u1, v1, r, g, b, a);
    vert(rx1, ry1, u1, v0, r, g, b, a);
    vert(rx0, ry0, u0, v1, r, g, b, a);
    vert(rx1, ry1, u1, v0, r, g, b, a);
    vert(rx0, ry1, u0, v0, r, g, b, a);
}

static void round_rect(float x0, float y0, float x1, float y1, float rad, float r, float g, float b, float a) {
    if (x1 <= x0 || y1 <= y0) return;
    if (rad * 2.0f > (x1 - x0)) rad = (x1 - x0) * 0.5f;
    if (rad * 2.0f > (y1 - y0)) rad = (y1 - y0) * 0.5f;
    if (rad < 2.0f) {
        rect_raw(x0, y0, x1, y1, r, g, b, a);
        return;
    }
    if (x1 - rad > x0 + rad) {
        rect_raw(x0 + rad, y0, x1 - rad, y1, r, g, b, a);
    }
    if (y1 - rad > y0 + rad) {
        rect_raw(x0, y0 + rad, x0 + rad, y1 - rad, r, g, b, a);
        rect_raw(x1 - rad, y0 + rad, x1, y1 - rad, r, g, b, a);
    }
    corner_quarter_raw(x0 + rad, y0 + rad, rad, -1.0f, -1.0f, r, g, b, a);
    corner_quarter_raw(x1 - rad, y0 + rad, rad,  1.0f, -1.0f, r, g, b, a);
    corner_quarter_raw(x0 + rad, y1 - rad, rad, -1.0f,  1.0f, r, g, b, a);
    corner_quarter_raw(x1 - rad, y1 - rad, rad,  1.0f,  1.0f, r, g, b, a);
}

static void pill(float cx, float cy, float w, float h, float r, float g, float b, float a) {
    round_rect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, h * 0.5f, r, g, b, a);
}

static void pill_button_shadow(float cx, float cy, float w, float h, float r, float g, float b, float a) {
    pill(cx + 1.5f, cy + 2.5f, w, h, 0.05f, 0.08f, 0.05f, 0.22f * a);
    pill(cx, cy, w, h, r, g, b, a);
    pill(cx, cy - h * 0.22f, w * 0.85f, h * 0.42f, 1.0f, 1.0f, 1.0f, 0.25f * a);
}

static void disc(float cx, float cy, float rad, float r, float g, float b, float a) {
    a *= s_fade;
    disc_raw(cx, cy, rad, r, g, b, a);
}

static void button(float cx, float cy, float rad, float r, float g, float b) {
    disc(cx + 2.0f, cy + 3.0f, rad, 0.05f, 0.06f, 0.05f, 0.28f);
    disc(cx, cy, rad, r, g, b, 0.88f);
    disc(cx, cy - rad * 0.18f, rad * 0.72f, 1.0f, 0.98f, 0.94f, 0.18f);
}

static u8 ascii_to_ac_char(char c) {
    if (c == '$') return CHAR_SYMBOL_MONEY;
    if (c == '*') return CHAR_SYMBOL_STAR;
    if (c == '/') return CHAR_FORWARD_SLASH;
    if (c == ';') return CHAR_SEMICOLON;
    if (c == '#') return CHAR_HASHTAG;
    if (c == '+') return CHAR_PLUS;
    return (u8)c;
}

static void to_title_case(const char* src, char* dst, int max_len) {
    int i = 0, j = 0;
    int capitalize_next = 1;
    if (dst == NULL || max_len <= 0) return;
    dst[0] = 0;
    if (src == NULL) return;
    while (src[i] != 0 && j < max_len - 1) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            if (!capitalize_next) c = c - 'A' + 'a';
            capitalize_next = 0;
        } else if (c >= 'a' && c <= 'z') {
            if (capitalize_next) c = c - 'a' + 'A';
            capitalize_next = 0;
        } else if (c == ' ' || c == '-' || c == '/' || c == '(' || c == '[' || c == '.') {
            capitalize_next = 1;
        } else {
            capitalize_next = 0;
        }
        dst[j++] = c;
        i++;
    }
    dst[j] = 0;
}

static void draw_text_ac(const char* s, float cx, float cy, float pix, float r, float g, float b, float a, int center, int max_chars) {
    if (s == NULL || s[0] == 0 || pix <= 0.001f) return;
    init_font_texture();
    if (s_font_tex == 0) return;

    float scale = pix * 0.54f;
    float tracking = 0.8f * scale;
    int n = 0;
    float total_w = 0.0f;
    while (s[n] != 0 && n < max_chars) {
        u8 ac = ascii_to_ac_char(s[n]);
        int cw = mFont_GetCodeWidth(ac, 1);
        if (cw <= 0) cw = 6;
        total_w += (float)cw * scale + tracking;
        n++;
    }
    if (n == 0) return;
    if (total_w > tracking) total_w -= tracking;

    float cur_x = center ? (cx - total_w * 0.5f) : cx;
    float gh = 16.0f * scale;
    float qy0 = cy - gh * 0.5f;
    float qy1 = cy + gh * 0.5f;
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float ry0 = 1.0f - qy1 / H * 2.0f;
    float ry1 = 1.0f - qy0 / H * 2.0f;

    for (int i = 0; i < n; i++) {
        u8 ac = ascii_to_ac_char(s[i]);
        int cw = mFont_GetCodeWidth(ac, 1);
        if (cw <= 0) cw = 6;
        float gw = (float)cw * scale;

        if (s[i] != ' ') {
            int row = ac >> 4;
            int col = ac & 0x0F;
            float u0 = ((float)(col * 12) + 0.5f) / 192.0f;
            float u1 = ((float)(col * 12 + cw) - 0.5f) / 192.0f;
            float v0 = ((float)(row * 16) + 0.5f) / 256.0f;
            float v1 = ((float)(row * 16 + 16) - 0.5f) / 256.0f;

            float qx0 = cur_x;
            float qx1 = cur_x + gw;
            float rx0 = qx0 / W * 2.0f - 1.0f;
            float rx1 = qx1 / W * 2.0f - 1.0f;

            vert(rx0, ry0, 2.0f + u0, v1, r, g, b, a);
            vert(rx1, ry0, 2.0f + u1, v1, r, g, b, a);
            vert(rx1, ry1, 2.0f + u1, v0, r, g, b, a);
            vert(rx0, ry0, 2.0f + u0, v1, r, g, b, a);
            vert(rx1, ry1, 2.0f + u1, v0, r, g, b, a);
            vert(rx0, ry1, 2.0f + u0, v0, r, g, b, a);
        }
        cur_x += gw + tracking;
    }
}

static void draw_str_raw(const char* s, float cx, float cy, float pix, float r, float g, float b, float a, int max_chars) {
    char title[64];
    to_title_case(s, title, sizeof(title));
    draw_text_ac(title, cx, cy, pix, r, g, b, a, 1, max_chars);
}

static void draw_str_left_raw(const char* s, float x, float cy, float pix, float r, float g, float b, float a, int max_chars) {
    char title[64];
    to_title_case(s, title, sizeof(title));
    draw_text_ac(title, x, cy, pix, r, g, b, a, 0, max_chars);
}

static void draw_word_raw(const char* s, float cx, float cy, float pix, float r, float g, float b, float a) {
    draw_str_raw(s, cx, cy, pix * 1.05f, r, g, b, a, 12);
}

static void glyph(char ch, float cx, float cy, float s) {
    char str[2];
    str[0] = ch;
    str[1] = 0;
    draw_text_ac(str, cx, cy, s * 2.3f, 0.16f, 0.18f, 0.15f, 0.95f, 1, 1);
}

static void draw_left(const char* s, float x, float cy, float pix) {
    draw_str_left_raw(s, x, cy, pix, 0.16f, 0.18f, 0.15f, 0.95f, 32);
}

static void get_item_name(mActor_name_t item, char* out, int max_len) {
    u8 raw[mIN_ITEM_NAME_LEN + 1];
    int i, len = 0;
    if (item == EMPTY_NO || item == 0) {
        snprintf(out, max_len, "Empty");
        return;
    }
    mIN_copy_name_str(raw, item);
    raw[mIN_ITEM_NAME_LEN] = 0;
    for (i = mIN_ITEM_NAME_LEN - 1; i >= 0; i--) {
        if (raw[i] == ' ' || raw[i] == 0) raw[i] = 0;
        else break;
    }
    for (i = 0; raw[i] != 0 && len < max_len - 1; i++) {
        u8 c = raw[i];
        if (c >= 32 && c <= 126) out[len++] = (char)c;
    }
    out[len] = 0;
    if (len == 0) snprintf(out, max_len, "Item");
}

static void get_item_category_color(mActor_name_t item, float* r, float* g, float* b) {
    if (item == EMPTY_NO || item == 0) {
        *r = 0.82f; *g = 0.82f; *b = 0.80f;
        return;
    }
    if (ITEM_IS_TOOL(item) || (item >= ITM_MY_ORG_UMBRELLA0 && item <= ITM_MY_ORG_UMBRELLA7)) {
        *r = 0.31f; *g = 0.73f; *b = 0.53f; /* Mint green */
    } else if (ITEM_IS_CLOTH(item) || item == RSV_CLOTH) {
        *r = 0.98f; *g = 0.49f; *b = 0.37f; /* Coral / Peach */
    } else if (ITEM_NAME_GET_CAT(item) == ITEM1_CAT_FRUIT || ITEM_NAME_GET_CAT(item) == ITEM1_CAT_KABU) {
        *r = 1.00f; *g = 0.64f; *b = 0.28f; /* Apricot / Orange */
    } else if (ITEM_NAME_GET_CAT(item) == ITEM1_CAT_FISH) {
        *r = 0.29f; *g = 0.56f; *b = 0.89f; /* Sky Blue */
    } else if (ITEM_IS_INSECT(item)) {
        *r = 0.18f; *g = 0.80f; *b = 0.44f; /* Emerald Green */
    } else if (item >= ITM_MONEY_START && item <= ITM_MONEY_END) {
        *r = 0.96f; *g = 0.65f; *b = 0.14f; /* Shiny Gold */
    } else {
        *r = 0.61f; *g = 0.35f; *b = 0.71f; /* Soft Lavender */
    }
}

static void get_clean_string(const u8* src, int max_src, char* dst, int max_dst) {
    int i, len = 0;
    if (dst == NULL || max_dst <= 0) return;
    dst[0] = 0;
    if (src == NULL) return;
    for (i = 0; i < max_src && len + 1 < max_dst; i++) {
        u8 c = src[i];
        if (c >= 32 && c <= 126) {
            dst[len++] = (char)c;
        } else if (c == 0) {
            break;
        }
    }
    while (len > 0 && dst[len - 1] == ' ') {
        len--;
    }
    dst[len] = 0;
}

static const char* get_item_category_name(mActor_name_t item) {
    if (item == EMPTY_NO || item == 0) return "EMPTY";
    if (ITEM_IS_TOOL(item) || (item >= ITM_MY_ORG_UMBRELLA0 && item <= ITM_MY_ORG_UMBRELLA7)) return "TOOL";
    if (ITEM_IS_CLOTH(item) || item == RSV_CLOTH) return "CLOTHES";
    if (ITEM_IS_FTR(item)) return "FURNITURE";
    if (ITEM_IS_CARPET(item)) return "CARPET";
    if (ITEM_IS_WALL(item)) return "WALLPAPER";
    if (ITEM_IS_FISH(item)) return "FISH";
    if (ITEM_IS_INSECT(item)) return "INSECT";
    if (ITEM_IS_PAPER(item)) return "PAPER";
    if (ITEM_NAME_GET_CAT(item) == ITEM1_CAT_FRUIT) return "FRUIT";
    if (ITEM_NAME_GET_CAT(item) == ITEM1_CAT_PLANT) return "PLANT";
    if (ITEM_NAME_GET_CAT(item) == ITEM1_CAT_KABU) return "TURNIP";
    if (item >= ITM_MONEY_START && item <= ITM_MONEY_END) return "BELLS";
    return "ITEM";
}

static void mobile_sheet_compute_dims(float W, float H, float* out_ui, float* out_sheet_y0,
                                      float* out_margin_x, float* out_gap_x, float* out_gap_y,
                                      float* out_card_w, float* out_card_h,
                                      float* out_header_h, float* out_tab_h,
                                      float* out_detail_h, float* out_act_h) {
    float ui = (W > 0.0f) ? (W / 1080.0f) : 1.0f;
    float margin_x, gap_x, gap_y, card_w, card_h;
    float header_h, tab_h, detail_h, act_h, fixed_h, sheet_content_h, max_sheet_h, sheet_y0;

    if (ui < 0.55f) ui = 0.55f;
    if (ui > 1.35f) ui = 1.35f;

    margin_x = 14.0f * ui;
    gap_x = 8.0f * ui;
    gap_y = 8.0f * ui;
    card_w = (W - 2.0f * margin_x - 4.0f * gap_x) / 5.0f;
    card_h = card_w * 0.70f;
    if (card_h < 84.0f * ui) card_h = 84.0f * ui;

    header_h = 48.0f * ui;
    tab_h = 48.0f * ui;
    detail_h = 74.0f * ui;
    act_h = 58.0f * ui;

    fixed_h = 26.0f * ui /* top handle zone */
            + header_h + 10.0f * ui
            + tab_h + 10.0f * ui
            + 2.0f * gap_y + 10.0f * ui
            + detail_h + 10.0f * ui
            + act_h + 18.0f * ui /* bottom margin */;

    max_sheet_h = H * 0.48f;
    if (fixed_h + 3.0f * card_h > max_sheet_h) {
        float avail_cards = (max_sheet_h - fixed_h) / 3.0f;
        if (avail_cards < 72.0f * ui) avail_cards = 72.0f * ui;
        card_h = avail_cards;
    }

    sheet_content_h = fixed_h + 3.0f * card_h;
    sheet_y0 = H - sheet_content_h;
    if (sheet_y0 < H * 0.44f) sheet_y0 = H * 0.44f;

    if (out_ui) *out_ui = ui;
    if (out_sheet_y0) *out_sheet_y0 = sheet_y0;
    if (out_margin_x) *out_margin_x = margin_x;
    if (out_gap_x) *out_gap_x = gap_x;
    if (out_gap_y) *out_gap_y = gap_y;
    if (out_card_w) *out_card_w = card_w;
    if (out_card_h) *out_card_h = card_h;
    if (out_header_h) *out_header_h = header_h;
    if (out_tab_h) *out_tab_h = tab_h;
    if (out_detail_h) *out_detail_h = detail_h;
    if (out_act_h) *out_act_h = act_h;
}

int pc_touch_sheet_top_y(void) {
    float sheet_y0;
    if (g_pc_window_w < 2 || g_pc_window_h < 2) return 0;
    if (!pc_touch_enabled() || !g_pc_layout_portrait || !submenu_open()) return 0;
    mobile_sheet_compute_dims((float)g_pc_window_w, (float)g_pc_window_h,
                              NULL, &sheet_y0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    return (int)sheet_y0;
}

static void mobile_sheet_layout(void) {
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float ui, sheet_y0, tab_h, header_h, act_h, detail_h;
    float margin_x, gap_x, gap_y, card_w, card_h, cur_y;
    int c, r, i;

    if (!g_pc_layout_portrait || pc_creator_menu_active()) {
        s_sheet.active = 0;
        s_sheet.swap_mode = 0;
        return;
    }
    {
        GAME_PLAY* play = get_game_play();
        if (!submenu_open() || play == NULL || play->submenu.overlay == NULL || play->submenu.overlay->inventory_ovl == NULL) {
            s_sheet.active = 0;
            s_sheet.swap_mode = 0;
            return;
        }
    }
    s_sheet.active = 1;
    if (s_sheet.sel_slot < 0 || s_sheet.sel_slot >= 15) {
        s_sheet.sel_slot = 0;
    }

    mobile_sheet_compute_dims(W, H, &ui, &sheet_y0, &margin_x, &gap_x, &gap_y,
                              &card_w, &card_h, &header_h, &tab_h, &detail_h, &act_h);

    s_sheet.sheet_x0 = 0.0f;
    s_sheet.sheet_y0 = sheet_y0;
    s_sheet.sheet_x1 = W;
    s_sheet.sheet_y1 = H;

    /* Lay out elements from top to bottom */
    cur_y = sheet_y0 + 26.0f * ui;

    /* Header */
    s_sheet.close_w = 98.0f * ui;
    s_sheet.close_h = header_h;
    s_sheet.close_cx = W - margin_x - s_sheet.close_w * 0.5f;
    s_sheet.close_cy = cur_y + header_h * 0.5f;

    s_sheet.bells_w = 126.0f * ui;
    s_sheet.bells_h = header_h;
    s_sheet.bells_cx = s_sheet.close_cx - s_sheet.close_w * 0.5f - 10.0f * ui - s_sheet.bells_w * 0.5f;
    s_sheet.bells_cy = cur_y + header_h * 0.5f;

    cur_y += header_h + 10.0f * ui;

    /* Category Tabs (5 tabs) */
    {
        float tab_w = (W - 2.0f * margin_x - 4.0f * gap_x) / 5.0f;
        float tab_cy = cur_y + tab_h * 0.5f;
        for (i = 0; i < 5; i++) {
            s_sheet.tab_cx[i] = margin_x + tab_w * 0.5f + (float)i * (tab_w + gap_x);
            s_sheet.tab_cy[i] = tab_cy;
            s_sheet.tab_w[i] = tab_w;
            s_sheet.tab_h[i] = tab_h;
        }
    }
    cur_y += tab_h + 10.0f * ui;

    /* 15 Pocket Cards Grid */
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 5; c++) {
            int slot = r * 5 + c;
            float x0 = margin_x + (float)c * (card_w + gap_x);
            float y0 = cur_y + (float)r * (card_h + gap_y);
            s_sheet.card_x0[slot] = x0;
            s_sheet.card_y0[slot] = y0;
            s_sheet.card_x1[slot] = x0 + card_w;
            s_sheet.card_y1[slot] = y0 + card_h;
        }
    }
    cur_y += 3.0f * card_h + 2.0f * gap_y + 10.0f * ui;

    /* Detail Banner */
    s_sheet.detail_x0 = margin_x;
    s_sheet.detail_x1 = W - margin_x;
    s_sheet.detail_y0 = cur_y;
    s_sheet.detail_y1 = cur_y + detail_h;
    cur_y += detail_h + 10.0f * ui;

    /* Action bar */
    {
        float act_cy = cur_y + act_h * 0.5f;
        float act_w, act_margin, act_gap, act_step;
        Private_c* priv = Now_Private;
        mActor_name_t item = EMPTY_NO;

        if (act_cy + act_h * 0.5f > H - 8.0f * ui) act_cy = H - 8.0f * ui - act_h * 0.5f;

        s_sheet.num_actions = 0;
        if (priv != NULL && s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
            item = priv->inventory.pockets[s_sheet.sel_slot];
        }

        if (s_sheet.current_tab != 0) {
            s_sheet.action_labels[0] = "BACK TO POCKETS";
            s_sheet.action_ids[0] = 5;
            s_sheet.num_actions = 1;
        } else if (s_sheet.swap_mode) {
            s_sheet.action_labels[0] = "SWAP HERE";
            s_sheet.action_ids[0] = 30;
            s_sheet.action_labels[1] = "CANCEL";
            s_sheet.action_ids[1] = 4;
            s_sheet.num_actions = 2;
        } else if (item != EMPTY_NO && item != 0) {
            if (ITEM_IS_TOOL(item) || (item >= ITM_MY_ORG_UMBRELLA0 && item <= ITM_MY_ORG_UMBRELLA7)) {
                if (priv != NULL && priv->equipment == item) {
                    s_sheet.action_labels[0] = "UNEQUIP";
                    s_sheet.action_ids[0] = 11;
                } else {
                    s_sheet.action_labels[0] = "EQUIP";
                    s_sheet.action_ids[0] = 10;
                }
            } else if (ITEM_IS_CLOTH(item) || item == RSV_CLOTH) {
                s_sheet.action_labels[0] = "WEAR";
                s_sheet.action_ids[0] = 12;
            } else if (ITEM_NAME_GET_CAT(item) == ITEM1_CAT_FRUIT || ITEM_NAME_GET_CAT(item) == ITEM1_CAT_KABU) {
                s_sheet.action_labels[0] = "EAT";
                s_sheet.action_ids[0] = 13;
            } else if (ITEM_NAME_GET_CAT(item) == ITEM1_CAT_FISH || ITEM_IS_INSECT(item)) {
                s_sheet.action_labels[0] = "RELEASE";
                s_sheet.action_ids[0] = 15;
            } else if (item >= ITM_MONEY_START && item <= ITM_MONEY_END) {
                s_sheet.action_labels[0] = "DEPOSIT";
                s_sheet.action_ids[0] = 16;
            } else {
                s_sheet.action_labels[0] = "SELECT";
                s_sheet.action_ids[0] = 10;
            }
            s_sheet.action_labels[1] = "DROP";
            s_sheet.action_ids[1] = 2;
            s_sheet.action_labels[2] = "SWAP";
            s_sheet.action_ids[2] = 3;
            s_sheet.num_actions = 3;
        } else {
            /* Empty slot: keep bar space for a muted hint without duplicate Close button */
            s_sheet.num_actions = 0;
            s_sheet.act_cy[0] = act_cy;
            s_sheet.act_h[0] = act_h;
        }

        if (s_sheet.num_actions > 0) {
            act_margin = margin_x;
            act_gap = 10.0f * ui;
            act_w = (W - 2.0f * act_margin - (float)(s_sheet.num_actions - 1) * act_gap) / (float)s_sheet.num_actions;
            act_step = act_w + act_gap;
            for (i = 0; i < s_sheet.num_actions; i++) {
                s_sheet.act_cx[i] = act_margin + act_w * 0.5f + (float)i * act_step;
                s_sheet.act_cy[i] = act_cy;
                s_sheet.act_w[i] = act_w;
                s_sheet.act_h[i] = act_h;
            }
        }
    }
}

static int mobile_sheet_hit(float x, float y, int* key) {
    int i;
    if (!s_sheet.active) return HIT_NONE;
    /* Let taps above the bottom sheet interact directly with the OG menu on top */
    if (y < s_sheet.sheet_y0) {
        return HIT_NONE;
    }
    if (x >= s_sheet.close_cx - s_sheet.close_w * 0.5f && x <= s_sheet.close_cx + s_sheet.close_w * 0.5f &&
        y >= s_sheet.close_cy - s_sheet.close_h * 0.5f && y <= s_sheet.close_cy + s_sheet.close_h * 0.5f) {
        return HIT_SHEET_CLOSE;
    }
    if (x >= s_sheet.bells_cx - s_sheet.bells_w * 0.5f && x <= s_sheet.bells_cx + s_sheet.bells_w * 0.5f &&
        y >= s_sheet.bells_cy - s_sheet.bells_h * 0.5f && y <= s_sheet.bells_cy + s_sheet.bells_h * 0.5f) {
        return HIT_SHEET_BELLS;
    }
    for (i = 0; i < 5; i++) {
        if (x >= s_sheet.tab_cx[i] - s_sheet.tab_w[i] * 0.5f && x <= s_sheet.tab_cx[i] + s_sheet.tab_w[i] * 0.5f &&
            y >= s_sheet.tab_cy[i] - s_sheet.tab_h[i] * 0.5f && y <= s_sheet.tab_cy[i] + s_sheet.tab_h[i] * 0.5f) {
            *key = i;
            return HIT_SHEET_TAB;
        }
    }
    for (i = 0; i < s_sheet.num_actions; i++) {
        if (x >= s_sheet.act_cx[i] - s_sheet.act_w[i] * 0.5f && x <= s_sheet.act_cx[i] + s_sheet.act_w[i] * 0.5f &&
            y >= s_sheet.act_cy[i] - s_sheet.act_h[i] * 0.5f && y <= s_sheet.act_cy[i] + s_sheet.act_h[i] * 0.5f) {
            *key = s_sheet.action_ids[i];
            return HIT_SHEET_ACT;
        }
    }
    if (s_sheet.current_tab == 0) {
        for (i = 0; i < 15; i++) {
            if (x >= s_sheet.card_x0[i] && x <= s_sheet.card_x1[i] &&
                y >= s_sheet.card_y0[i] && y <= s_sheet.card_y1[i]) {
                *key = i;
                return HIT_SHEET_CARD;
            }
        }
    }
    return HIT_SHEET_BODY;
}

static void mobile_sheet_handle_card(int slot) {
    Private_c* priv = Now_Private;
    if (priv == NULL || slot < 0 || slot >= 15) return;
    if (s_sheet.swap_mode) {
        int src = s_sheet.swap_source;
        int dst = slot;
        mActor_name_t t_item = priv->inventory.pockets[src];
        u32 src_cond = mPr_GET_ITEM_COND(priv->inventory.item_conditions, src);
        u32 dst_cond = mPr_GET_ITEM_COND(priv->inventory.item_conditions, dst);
        mActor_name_t dst_item = priv->inventory.pockets[dst];
        mPr_SetPossessionItem(priv, src, dst_item, dst_cond);
        mPr_SetPossessionItem(priv, dst, t_item, src_cond);
        s_sheet.swap_mode = 0;
        s_sheet.sel_slot = dst;
        sAdo_SysTrgStart(NA_SE_CURSOL);
        pc_touch_buzz();
        note_tap("sheet_card_swap");
        if (submenu_open()) {
            GAME_PLAY* play = get_game_play();
            if (play != NULL && play->submenu.overlay != NULL && play->submenu.overlay->tag_ovl != NULL &&
                play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
            }
        }
    } else {
        s_sheet.sel_slot = slot;
        sAdo_SysTrgStart(NA_SE_CURSOL);
        pc_touch_buzz();
        note_tap("sheet_card_select");
        if (submenu_open()) {
            GAME_PLAY* play = get_game_play();
            if (play != NULL && play->submenu.overlay != NULL && play->submenu.overlay->tag_ovl != NULL) {
                mTG_tag_c* tag = &play->submenu.overlay->tag_ovl->tags[0];
                tag->table = mTG_TABLE_ITEM;
                tag->tag_col = slot % 5;
                tag->tag_row = slot / 5;
                if (play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                    play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
                }
            }
        }
    }
}

static void mobile_sheet_handle_act(int act_id) {
    Private_c* priv = Now_Private;
    if (priv == NULL) return;
    if (act_id == 10) { /* EQUIP */
        if (s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
            mActor_name_t it = priv->inventory.pockets[s_sheet.sel_slot];
            mActor_name_t old_eq = priv->equipment;
            priv->inventory.pockets[s_sheet.sel_slot] = old_eq;
            priv->equipment = it;
            sAdo_SysTrgStart(NA_SE_5E);
            pc_touch_buzz();
            note_tap("sheet_equip");
            if (submenu_open()) {
                GAME_PLAY* play = get_game_play();
                if (play != NULL && play->submenu.overlay != NULL) {
                    play->submenu.overlay->segment.change_player_main_anime_idx = mIV_ANIM_CATCH;
                    if (play->submenu.overlay->tag_ovl != NULL && play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                        play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
                    }
                }
            }
        }
    } else if (act_id == 11) { /* UNEQUIP */
        int empty_slot = mPr_GetPossessionItemIdx(priv, EMPTY_NO);
        if (empty_slot >= 0) {
            priv->inventory.pockets[empty_slot] = priv->equipment;
            priv->equipment = EMPTY_NO;
            sAdo_SysTrgStart(NA_SE_CURSOL);
            pc_touch_buzz();
            note_tap("sheet_unequip");
            if (submenu_open()) {
                GAME_PLAY* play = get_game_play();
                if (play != NULL && play->submenu.overlay != NULL) {
                    play->submenu.overlay->segment.change_player_main_anime_idx = mIV_ANIM_WALK;
                    if (play->submenu.overlay->tag_ovl != NULL && play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                        play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
                    }
                }
            }
        }
    } else if (act_id == 12) { /* WEAR */
        if (s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
            mActor_name_t it = priv->inventory.pockets[s_sheet.sel_slot];
            mActor_name_t old_cloth = priv->cloth.item;
            priv->inventory.pockets[s_sheet.sel_slot] = old_cloth;
            mPlib_change_player_cloth_info_lv2(priv, it);
            mPlib_change_player_cloth((GAME*)get_game_play(), priv->cloth.idx);
            sAdo_SysTrgStart(NA_SE_WEAR);
            pc_touch_buzz();
            note_tap("sheet_wear");
            if (submenu_open()) {
                GAME_PLAY* play = get_game_play();
                if (play != NULL && play->submenu.overlay != NULL) {
                    play->submenu.overlay->segment.change_player_main_anime_idx = mIV_ANIM_CHANGE;
                    if (play->submenu.overlay->tag_ovl != NULL && play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                        play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
                    }
                }
            }
        }
    } else if (act_id == 13) { /* EAT */
        if (s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
            priv->inventory.pockets[s_sheet.sel_slot] = EMPTY_NO;
            sAdo_SysTrgStart(NA_SE_EAT);
            pc_touch_buzz();
            note_tap("sheet_eat");
            if (submenu_open()) {
                GAME_PLAY* play = get_game_play();
                if (play != NULL && play->submenu.overlay != NULL) {
                    play->submenu.overlay->segment.change_player_main_anime_idx = mIV_ANIM_EAT;
                    if (play->submenu.overlay->tag_ovl != NULL && play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                        play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
                    }
                }
            }
        }
    } else if (act_id == 15) { /* RELEASE (Fish/Insect) */
        if (s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
            priv->inventory.pockets[s_sheet.sel_slot] = EMPTY_NO;
            sAdo_SysTrgStart(NA_SE_5E);
            pc_touch_buzz();
            note_tap("sheet_release");
        }
    } else if (act_id == 16) { /* DEPOSIT (Bells bag -> wallet) */
        if (s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
            mActor_name_t item = priv->inventory.pockets[s_sheet.sel_slot];
            u32 amt = 0;
            if (item == ITM_MONEY_100) amt = 100;
            else if (item == ITM_MONEY_1000) amt = 1000;
            else if (item == ITM_MONEY_10000) amt = 10000;
            else if (item == ITM_MONEY_30000) amt = 30000;
            if (amt > 0) {
                priv->inventory.wallet += amt;
                if (priv->inventory.wallet > mPr_WALLET_MAX) priv->inventory.wallet = mPr_WALLET_MAX;
                priv->inventory.pockets[s_sheet.sel_slot] = EMPTY_NO;
                sAdo_SysTrgStart(MONO(NA_SE_REGISTER));
                pc_touch_buzz();
                note_tap("sheet_deposit");
                if (submenu_open()) {
                    GAME_PLAY* play = get_game_play();
                    if (play != NULL && play->submenu.overlay != NULL && play->submenu.overlay->inventory_ovl != NULL) {
                        play->submenu.overlay->inventory_ovl->disp_money = priv->inventory.wallet;
                    }
                }
            }
        }
    } else if (act_id == 2) { /* DROP */
        if (submenu_open() && s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
            GAME_PLAY* play = get_game_play();
            if (play != NULL && play->submenu.overlay != NULL && play->submenu.overlay->tag_ovl != NULL) {
                mTG_tag_c* tag = &play->submenu.overlay->tag_ovl->tags[0];
                tag->table = mTG_TABLE_ITEM;
                tag->tag_col = s_sheet.sel_slot % 5;
                tag->tag_row = s_sheet.sel_slot / 5;
                if (play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                    play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
                }
                s_pulse_a = 1;
                s_pending_drop = 1;
                pc_touch_buzz();
                note_tap("sheet_drop_start");
            }
        }
    } else if (act_id == 3) { /* SWAP */
        s_sheet.swap_mode = 1;
        s_sheet.swap_source = s_sheet.sel_slot;
        sAdo_SysTrgStart(NA_SE_CURSOL);
        pc_touch_buzz();
        note_tap("sheet_swap_mode");
    } else if (act_id == 30) { /* SWAP HERE */
        if (s_sheet.swap_mode && s_sheet.swap_source >= 0 && s_sheet.sel_slot >= 0) {
            int src = s_sheet.swap_source;
            int dst = s_sheet.sel_slot;
            mActor_name_t t_item = priv->inventory.pockets[src];
            u32 src_cond = mPr_GET_ITEM_COND(priv->inventory.item_conditions, src);
            u32 dst_cond = mPr_GET_ITEM_COND(priv->inventory.item_conditions, dst);
            mActor_name_t dst_item = priv->inventory.pockets[dst];
            mPr_SetPossessionItem(priv, src, dst_item, dst_cond);
            mPr_SetPossessionItem(priv, dst, t_item, src_cond);
            s_sheet.swap_mode = 0;
            s_sheet.sel_slot = dst;
            sAdo_SysTrgStart(NA_SE_CURSOL);
            pc_touch_buzz();
            note_tap("sheet_swap_done");
            if (submenu_open()) {
                GAME_PLAY* play = get_game_play();
                if (play != NULL && play->submenu.overlay != NULL && play->submenu.overlay->tag_ovl != NULL &&
                    play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                    play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
                }
            }
        }
    } else if (act_id == 4) { /* CLOSE / CANCEL */
        if (s_sheet.swap_mode) {
            s_sheet.swap_mode = 0;
            pc_touch_buzz();
            note_tap("sheet_swap_cancel");
        } else {
            s_pulse_b = 1;
            s_sheet.active = 0;
            s_sheet.swap_mode = 0;
            pc_touch_buzz();
            note_tap("sheet_close");
        }
    } else if (act_id == 5) { /* BACK TO POCKETS */
        mobile_sheet_handle_tab(0);
    }
}

static void mobile_sheet_handle_bells(void) {
    Private_c* priv = Now_Private;
    int empty_slot;
    u32 pull;
    mActor_name_t m_item;
    if (priv == NULL) return;
    empty_slot = mPr_GetPossessionItemIdx(priv, EMPTY_NO);
    if (empty_slot < 0 || priv->inventory.wallet < 100) {
        sAdo_SysTrgStart(NA_SE_33);
        return;
    }
    pull = 1000;
    if (priv->inventory.wallet >= 10000) pull = 10000;
    else if (priv->inventory.wallet < 1000) pull = 100;

    priv->inventory.wallet -= pull;
    m_item = (pull == 10000) ? ITM_MONEY_10000 : ((pull == 1000) ? ITM_MONEY_1000 : ITM_MONEY_100);
    mPr_SetPossessionItem(priv, empty_slot, m_item, mPr_ITEM_COND_NORMAL);
    s_sheet.sel_slot = empty_slot;
    sAdo_SysTrgStart(MONO(NA_SE_REGISTER));
    pc_touch_buzz();
    note_tap("sheet_pull_bells");
    if (submenu_open()) {
        GAME_PLAY* play = get_game_play();
        if (play != NULL && play->submenu.overlay != NULL) {
            if (play->submenu.overlay->inventory_ovl != NULL) {
                play->submenu.overlay->inventory_ovl->disp_money = priv->inventory.wallet;
            }
            if (play->submenu.overlay->tag_ovl != NULL && play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc != NULL) {
                play->submenu.overlay->tag_ovl->init_tag_data_item_win_proc(&play->submenu);
            }
        }
    }
}

static void mobile_sheet_handle_tab(int tab_id) {
    s_sheet.current_tab = tab_id;
    s_sheet.swap_mode = 0;
    if (tab_id < 4) {
        handle_sub_tab_tap(tab_id);
    }
    sAdo_SysTrgStart(NA_SE_CURSOL);
    pc_touch_buzz();
}

static void mobile_sheet_draw(void) {
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float ui, margin_x;
    Private_c* priv = Now_Private;
    char buf[64];
    int i;
    if (!s_sheet.active || priv == NULL) return;

    mobile_sheet_compute_dims(W, H, &ui, NULL, &margin_x, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    /* Subtle drop shadow right above the top edge of the bottom sheet (do not dim the OG menu) */
    rect_raw(0.0f, s_sheet.sheet_y0 - 10.0f * ui, W, s_sheet.sheet_y0, 0.02f, 0.03f, 0.02f, 0.22f);

    /* Sheet body: rounded top, creamy warm parchment */
    round_rect(0.0f, s_sheet.sheet_y0, W, H + 40.0f * ui, 26.0f * ui, 0.96f, 0.95f, 0.92f, 0.99f);

    /* Mobile drawer top handle pill */
    pill(W * 0.5f, s_sheet.sheet_y0 + 11.0f * ui, 52.0f * ui, 5.0f * ui, 0.68f, 0.66f, 0.62f, 0.85f);

    /* Header: Player Name & Land (preserve exact case, clean ASCII without garbage) */
    {
        char pname[16], lname[16];
        get_clean_string(priv->player_ID.player_name, 8, pname, sizeof(pname));
        get_clean_string(priv->player_ID.land_name, 8, lname, sizeof(lname));
        if (pname[0] == 0) snprintf(pname, sizeof(pname), "Player");
        if (lname[0] != 0) {
            snprintf(buf, sizeof(buf), "%s * %s", pname, lname);
        } else {
            snprintf(buf, sizeof(buf), "%s", pname);
        }
        draw_text_ac(buf, margin_x + 4.0f * ui, s_sheet.close_cy, 3.3f * ui, 0.22f, 0.26f, 0.24f, 1.0f, 0, 22);
    }

    /* Bells button */
    pill_button_shadow(s_sheet.bells_cx, s_sheet.bells_cy, s_sheet.bells_w, s_sheet.bells_h, 0.98f, 0.82f, 0.20f, 1.0f);
    snprintf(buf, sizeof(buf), "$ %u", (unsigned int)priv->inventory.wallet);
    draw_str_raw(buf, s_sheet.bells_cx, s_sheet.bells_cy, 3.0f * ui, 0.18f, 0.16f, 0.05f, 1.0f, 14);

    /* Close button */
    pill_button_shadow(s_sheet.close_cx, s_sheet.close_cy, s_sheet.close_w, s_sheet.close_h, 0.92f, 0.40f, 0.35f, 1.0f);
    draw_str_raw("CLOSE", s_sheet.close_cx, s_sheet.close_cy, 3.0f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 6);

    /* Category Tabs */
    {
        static const char* tab_names[5] = { "POCKETS", "LETTERS", "FISH", "BUGS", "MISSIONS" };
        for (i = 0; i < 5; i++) {
            int active = (s_sheet.current_tab == i);
            float cx = s_sheet.tab_cx[i], cy = s_sheet.tab_cy[i];
            float tw = s_sheet.tab_w[i], th = s_sheet.tab_h[i];
            if (active) {
                pill_button_shadow(cx, cy, tw, th, 0.28f, 0.70f, 0.46f, 1.0f);
                draw_str_raw(tab_names[i], cx, cy, 2.7f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 9);
            } else {
                pill_button_shadow(cx, cy, tw, th, 0.89f, 0.87f, 0.83f, 0.95f);
                draw_str_raw(tab_names[i], cx, cy, 2.5f * ui, 0.42f, 0.40f, 0.38f, 1.0f, 9);
            }
        }
    }

    /* Tab Content */
    if (s_sheet.current_tab == 0) {
        /* Pockets Grid */
        for (i = 0; i < 15; i++) {
            float x0 = s_sheet.card_x0[i], y0 = s_sheet.card_y0[i];
            float x1 = s_sheet.card_x1[i], y1 = s_sheet.card_y1[i];
            float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            mActor_name_t item = priv->inventory.pockets[i];
            float cr, cg, cb;
            char iname[32];

            /* Selection / Swap highlight ring */
            if (s_sheet.swap_mode && s_sheet.swap_source == i) {
                round_rect(x0 - 4.0f * ui, y0 - 4.0f * ui, x1 + 4.0f * ui, y1 + 4.0f * ui, 16.0f * ui, 0.98f, 0.60f, 0.12f, 0.95f);
            } else if (s_sheet.sel_slot == i) {
                round_rect(x0 - 4.0f * ui, y0 - 4.0f * ui, x1 + 4.0f * ui, y1 + 4.0f * ui, 16.0f * ui, 0.20f, 0.55f, 0.98f, 0.95f);
            }

            if (item == EMPTY_NO || item == 0) {
                /* Empty card */
                round_rect(x0, y0, x1, y1, 13.0f * ui, 0.92f, 0.91f, 0.88f, 0.75f);
                draw_str_raw("EMPTY", cx, cy, 2.7f * ui, 0.60f, 0.58f, 0.55f, 0.90f, 6);
            } else {
                int badged = (priv->equipment == item || priv->cloth.item == item);
                float text_cy = badged ? (cy - 4.0f * ui) : (cy + 3.0f * ui);

                /* Filled card */
                round_rect(x0 + 1.5f * ui, y0 + 2.0f * ui, x1 + 1.5f * ui, y1 + 2.0f * ui, 13.0f * ui, 0.05f, 0.08f, 0.05f, 0.14f);
                round_rect(x0, y0, x1, y1, 13.0f * ui, 1.0f, 1.0f, 1.0f, 0.99f);

                /* Category top bar */
                get_item_category_color(item, &cr, &cg, &cb);
                round_rect(x0 + 5.0f * ui, y0 + 5.0f * ui, x1 - 5.0f * ui, y0 + 13.0f * ui, 4.0f * ui, cr, cg, cb, 0.95f);

                /* Item name */
                get_item_name(item, iname, sizeof(iname));
                {
                    char* sp = strchr(iname, ' ');
                    if (sp != NULL && (sp - iname) <= 12 && strlen(sp + 1) <= 12) {
                        char w1[16], w2[16];
                        int l1 = (int)(sp - iname);
                        memcpy(w1, iname, l1);
                        w1[l1] = 0;
                        snprintf(w2, sizeof(w2), "%s", sp + 1);
                        draw_str_raw(w1, cx, text_cy - 12.0f * ui, 2.85f * ui, 0.16f, 0.18f, 0.16f, 1.0f, 12);
                        draw_str_raw(w2, cx, text_cy + 12.0f * ui, 2.85f * ui, 0.16f, 0.18f, 0.16f, 1.0f, 12);
                    } else {
                        draw_str_raw(iname, cx, text_cy, 2.95f * ui, 0.16f, 0.18f, 0.16f, 1.0f, 13);
                    }
                }

                /* Badges */
                if (priv->equipment == item) {
                    pill_button_shadow(cx, y1 - 14.0f * ui, 64.0f * ui, 20.0f * ui, 0.28f, 0.75f, 0.45f, 1.0f);
                    draw_str_raw("HELD", cx, y1 - 14.0f * ui, 2.0f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 4);
                } else if (priv->cloth.item == item) {
                    pill_button_shadow(cx, y1 - 14.0f * ui, 64.0f * ui, 20.0f * ui, 0.96f, 0.52f, 0.30f, 1.0f);
                    draw_str_raw("WORN", cx, y1 - 14.0f * ui, 2.0f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 4);
                }
            }
        }

        /* Detail Banner */
        {
            float dx0 = s_sheet.detail_x0, dy0 = s_sheet.detail_y0;
            float dx1 = s_sheet.detail_x1, dy1 = s_sheet.detail_y1;
            float dcx = (dx0 + dx1) * 0.5f, dcy = (dy0 + dy1) * 0.5f;

            if (s_sheet.swap_mode) {
                round_rect(dx0 + 1.5f * ui, dy0 + 2.0f * ui, dx1 + 1.5f * ui, dy1 + 2.0f * ui, 14.0f * ui, 0.05f, 0.08f, 0.05f, 0.15f);
                round_rect(dx0, dy0, dx1, dy1, 14.0f * ui, 0.98f, 0.78f, 0.20f, 0.98f);
                draw_str_raw("SWAP MODE ACTIVE", dcx, dcy - 12.0f * ui, 3.2f * ui, 0.18f, 0.16f, 0.05f, 1.0f, 20);
                draw_str_raw("TAP A SECOND SLOT TO SWAP ITEMS", dcx, dcy + 13.0f * ui, 2.5f * ui, 0.25f, 0.20f, 0.08f, 1.0f, 32);
            } else {
                mActor_name_t sel_item = EMPTY_NO;
                if (s_sheet.sel_slot >= 0 && s_sheet.sel_slot < 15) {
                    sel_item = priv->inventory.pockets[s_sheet.sel_slot];
                }
                round_rect(dx0 + 1.5f * ui, dy0 + 2.0f * ui, dx1 + 1.5f * ui, dy1 + 2.0f * ui, 14.0f * ui, 0.05f, 0.08f, 0.05f, 0.12f);
                round_rect(dx0, dy0, dx1, dy1, 14.0f * ui, 1.0f, 1.0f, 1.0f, 0.98f);

                if (sel_item == EMPTY_NO || sel_item == 0) {
                    pill(dx0 + 58.0f * ui, dcy, 84.0f * ui, 30.0f * ui, 0.78f, 0.78f, 0.78f, 1.0f);
                    snprintf(buf, sizeof(buf), "SLOT %d", s_sheet.sel_slot + 1);
                    draw_str_raw(buf, dx0 + 58.0f * ui, dcy, 2.4f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 8);
                    draw_str_left_raw("EMPTY POCKET SLOT", dx0 + 114.0f * ui, dcy, 3.1f * ui, 0.50f, 0.50f, 0.50f, 1.0f, 20);
                } else {
                    float cr, cg, cb;
                    char iname[32];
                    get_item_category_color(sel_item, &cr, &cg, &cb);
                    pill(dx0 + 62.0f * ui, dcy, 94.0f * ui, 30.0f * ui, cr, cg, cb, 1.0f);
                    draw_str_raw(get_item_category_name(sel_item), dx0 + 62.0f * ui, dcy, 2.4f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 10);
                    get_item_name(sel_item, iname, sizeof(iname));
                    draw_str_left_raw(iname, dx0 + 122.0f * ui, dcy, 3.3f * ui, 0.16f, 0.18f, 0.16f, 1.0f, 18);

                    if (priv->equipment == sel_item) {
                        pill_button_shadow(dx1 - 58.0f * ui, dcy, 92.0f * ui, 28.0f * ui, 0.28f, 0.75f, 0.45f, 1.0f);
                        draw_str_raw("EQUIPPED", dx1 - 58.0f * ui, dcy, 2.2f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 9);
                    } else if (priv->cloth.item == sel_item) {
                        pill_button_shadow(dx1 - 58.0f * ui, dcy, 92.0f * ui, 28.0f * ui, 0.96f, 0.52f, 0.30f, 1.0f);
                        draw_str_raw("WORN", dx1 - 58.0f * ui, dcy, 2.2f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 5);
                    }
                }
            }
        }
    } else if (s_sheet.current_tab == 1) {
        /* Letters Tab (10 letter slots) */
        for (i = 0; i < 10; i++) {
            int c = i % 2, r = i / 2;
            float gap_x = 10.0f * ui, gap_y = 8.0f * ui;
            float grid_top = s_sheet.card_y0[0];
            float grid_bot = s_sheet.detail_y1;
            float lw = (W - 2.0f * margin_x - gap_x) / 2.0f;
            float lh = (grid_bot - grid_top - 4.0f * gap_y) / 5.0f;
            float x0 = margin_x + (float)c * (lw + gap_x);
            float y0 = grid_top + (float)r * (lh + gap_y);
            float x1 = x0 + lw, y1 = y0 + lh;
            float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            Mail_c* m = &priv->mail[i];

            round_rect(x0 + 1.5f * ui, y0 + 2.0f * ui, x1 + 1.5f * ui, y1 + 2.0f * ui, 12.0f * ui, 0.05f, 0.08f, 0.05f, 0.14f);
            round_rect(x0, y0, x1, y1, 12.0f * ui, 1.0f, 1.0f, 1.0f, 0.98f);
            round_rect(x0 + 4.0f * ui, y0 + 4.0f * ui, x1 - 4.0f * ui, y0 + 11.0f * ui, 3.0f * ui, 0.95f, 0.50f, 0.45f, 0.95f);

            char sname[16];
            get_clean_string(m->header.sender.personalID.player_name, 8, sname, sizeof(sname));
            if (sname[0] != 0) {
                snprintf(buf, sizeof(buf), "From: %s", sname);
                draw_text_ac(buf, cx, cy + 2.0f * ui, 2.7f * ui, 0.16f, 0.18f, 0.16f, 1.0f, 1, 18);
                if (m->present != EMPTY_NO) {
                    pill_button_shadow(x1 - 34.0f * ui, cy + 2.0f * ui, 54.0f * ui, 22.0f * ui, 0.96f, 0.78f, 0.18f, 1.0f);
                    draw_str_raw("GIFT", x1 - 34.0f * ui, cy + 2.0f * ui, 2.1f * ui, 0.20f, 0.18f, 0.05f, 1.0f, 4);
                }
            } else {
                draw_str_raw("EMPTY LETTER", cx, cy + 2.0f * ui, 2.5f * ui, 0.60f, 0.58f, 0.55f, 0.85f, 14);
            }
        }
    } else if (s_sheet.current_tab == 4) {
        /* Daily Villager Missions Tab */
        const PCMissionState* ms = pc_missions_get_state();
        float grid_top = s_sheet.card_y0[0];
        float grid_bot = s_sheet.detail_y1;
        int n = (int)ms->count;
        if (n > PC_MISSIONS_MAX) n = PC_MISSIONS_MAX;
        if (n <= 0) {
            float cx = W * 0.5f, cy = (grid_top + grid_bot) * 0.5f;
            draw_str_raw("NO MISSIONS TODAY", cx, cy, 3.2f * ui, 0.45f, 0.45f, 0.45f, 1.0f, 20);
        } else {
            float gap = 8.0f * ui;
            float row_h = (grid_bot - grid_top - (float)(n - 1) * gap) / (float)n;
            for (i = 0; i < n; i++) {
                const PCMissionDef* d = &ms->defs[i];
                int cur = 0, tgt = 1;
                int ready = pc_missions_get_progress(i, &cur, &tgt);
                int done = (ms->completed_mask & (1u << i)) != 0;
                float y0 = grid_top + (float)i * (row_h + gap);
                float y1 = y0 + row_h;
                float cy = (y0 + y1) * 0.5f;
                char line[64], badge[24];

                round_rect(margin_x, y0, W - margin_x, y1, 12.0f * ui,
                           done ? 0.90f : (ready ? 0.92f : 1.0f),
                           done ? 0.92f : (ready ? 0.98f : 1.0f),
                           done ? 0.89f : (ready ? 0.92f : 1.0f), 0.98f);

                snprintf(line, sizeof(line), "%s: %s (%uB)",
                         d->villager_name, d->title, (unsigned)d->reward_bells);
                draw_text_ac(line, margin_x + 14.0f * ui, cy, 2.7f * ui,
                             done ? 0.48f : 0.18f, done ? 0.50f : 0.22f, done ? 0.48f : 0.20f, 1.0f, 0, 42);

                if (done) {
                    snprintf(badge, sizeof(badge), "DONE");
                    pill_button_shadow(W - margin_x - 54.0f * ui, cy, 88.0f * ui, row_h * 0.68f, 0.68f, 0.72f, 0.68f, 1.0f);
                    draw_str_raw(badge, W - margin_x - 54.0f * ui, cy, 2.4f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 10);
                } else if (ready) {
                    snprintf(badge, sizeof(badge), "DELIVER!");
                    pill_button_shadow(W - margin_x - 54.0f * ui, cy, 88.0f * ui, row_h * 0.68f, 0.22f, 0.72f, 0.42f, 1.0f);
                    draw_str_raw(badge, W - margin_x - 54.0f * ui, cy, 2.4f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 10);
                } else {
                    snprintf(badge, sizeof(badge), "%d/%d", cur, tgt);
                    pill_button_shadow(W - margin_x - 54.0f * ui, cy, 88.0f * ui, row_h * 0.68f, 0.96f, 0.80f, 0.24f, 1.0f);
                    draw_str_raw(badge, W - margin_x - 54.0f * ui, cy, 2.5f * ui, 0.18f, 0.16f, 0.06f, 1.0f, 10);
                }
            }
        }
    } else {
        /* Fish / Bugs Tab Info */
        float cx = W * 0.5f, cy = (s_sheet.card_y0[0] + s_sheet.detail_y1) * 0.5f;
        draw_str_raw(s_sheet.current_tab == 2 ? "FISH COLLECTION" : "INSECT COLLECTION", cx, cy - 22.0f * ui, 3.5f * ui, 0.20f, 0.24f, 0.20f, 1.0f, 20);
        draw_str_raw("BROWSE COLLECTION IN OG MENU ABOVE", cx, cy + 22.0f * ui, 2.7f * ui, 0.45f, 0.45f, 0.45f, 1.0f, 36);
    }

    /* Action Bar Buttons */
    if (s_sheet.num_actions == 0 && s_sheet.current_tab == 0) {
        float cy = s_sheet.act_cy[0], ah = s_sheet.act_h[0];
        round_rect(margin_x, cy - ah * 0.5f, W - margin_x, cy + ah * 0.5f, ah * 0.5f, 0.90f, 0.89f, 0.85f, 0.90f);
        draw_str_raw("TAP A FILLED ITEM FOR ACTIONS", W * 0.5f, cy, 2.8f * ui, 0.50f, 0.48f, 0.45f, 1.0f, 32);
    } else {
        for (i = 0; i < s_sheet.num_actions; i++) {
            float cx = s_sheet.act_cx[i], cy = s_sheet.act_cy[i];
            float aw = s_sheet.act_w[i], ah = s_sheet.act_h[i];
            int aid = s_sheet.action_ids[i];
            float br = 0.32f, bg = 0.68f, bb = 0.48f, tr = 1.0f, tg = 1.0f, tb = 1.0f;

            if (aid == 10 || aid == 12 || aid == 13 || aid == 15 || aid == 16 || aid == 30) {
                br = 0.20f; bg = 0.72f; bb = 0.42f;
            } else if (aid == 2) {
                br = 0.90f; bg = 0.35f; bb = 0.32f;
            } else if (aid == 3) {
                br = 0.96f; bg = 0.68f; bb = 0.18f;
                tr = tg = tb = 0.15f;
            } else if (aid == 11) {
                br = 0.45f; bg = 0.55f; bb = 0.62f;
            } else {
                br = 0.52f; bg = 0.55f; bb = 0.58f;
            }

            pill_button_shadow(cx, cy, aw, ah, br, bg, bb, 1.0f);
            draw_str_raw(s_sheet.action_labels[i], cx, cy, 3.1f * ui, tr, tg, tb, 1.0f, 16);
        }
    }
}

static void info_line(char* buf, size_t n) {
    time_t now = time(NULL);
    struct tm tm;
    Private_c* priv;
    int filled = 0, i;
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    priv = Now_Private;
    if (priv != NULL && get_game_play() != NULL) {
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT && i < 15; i++) {
            if (priv->inventory.pockets[i] != EMPTY_NO) filled++;
        }
        snprintf(buf, n, "%02d:%02d  %u  pocket %d", tm.tm_hour, tm.tm_min, (unsigned int)priv->inventory.wallet, filled);
    } else {
        snprintf(buf, n, "%02d:%02d  solo", tm.tm_hour, tm.tm_min);
    }
}

static void draw_word(const char* s, float cx, float cy, float pix) {
    draw_word_raw(s, cx, cy, pix, 0.16f, 0.18f, 0.15f, 0.95f);
}

static const char* action_word(void) {
    if (choice_is_up()) return "OK";
    if (message_open()) return "Next";
    if (strcmp(s_action, "Pick up") == 0) return "Pick";
    if (strcmp(s_action, "Shake") == 0) return "Shake";
    if (strcmp(s_action, "Open") == 0) return "Open";
    if (strcmp(s_action, "Talk") == 0) return "Talk";
    if (strcmp(s_action, "Next") == 0) return "Next";
    if (strcmp(s_action, "OK") == 0) return "OK";
    if (strcmp(s_action, "Use") == 0) return "Use";
    return "Action";
}

static char s_toast_title[128] = {0};
static char s_toast_sub[160] = {0};
static Uint32 s_toast_expire_ms = 0;
static Uint32 s_toast_start_ms = 0;

void pc_touch_show_toast(const char* text) {
    pc_touch_show_toast_ex(text, NULL, 3500);
}

void pc_touch_show_toast_ex(const char* title, const char* subtitle, uint32_t duration_ms) {
    if (title != NULL) {
        snprintf(s_toast_title, sizeof(s_toast_title), "%s", title);
    } else {
        s_toast_title[0] = 0;
    }
    if (subtitle != NULL) {
        snprintf(s_toast_sub, sizeof(s_toast_sub), "%s", subtitle);
    } else {
        s_toast_sub[0] = 0;
    }
    s_toast_start_ms = SDL_GetTicks();
    s_toast_expire_ms = s_toast_start_ms + (duration_ms > 0 ? duration_ms : 3500);
}

static void draw_toast_banner(void) {
    Uint32 now = SDL_GetTicks();
    if (now >= s_toast_expire_ms || s_toast_title[0] == 0) {
        return;
    }
    float elapsed = (float)(now - s_toast_start_ms);
    float remain = (float)(s_toast_expire_ms - now);
    float alpha = 1.0f;
    if (elapsed < 200.0f) {
        alpha = elapsed / 200.0f;
    } else if (remain < 350.0f) {
        alpha = remain / 350.0f;
    }
    if (alpha <= 0.01f) return;

    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float ui = fmaxf(1.0f, H / 720.0f);
    int has_sub = (s_toast_sub[0] != 0);
    float bw = fminf(560.0f * ui, W * 0.94f);
    float bh = has_sub ? (62.0f * ui) : (42.0f * ui);
    float cx = W * 0.5f;
    float cy = H * 0.12f;
    float x0 = cx - bw * 0.5f, y0 = cy - bh * 0.5f;
    float x1 = cx + bw * 0.5f, y1 = cy + bh * 0.5f;

    round_rect(x0 + 2.0f * ui, y0 + 3.0f * ui, x1 + 2.0f * ui, y1 + 3.0f * ui, 16.0f * ui, 0.02f, 0.04f, 0.02f, 0.35f * alpha);
    round_rect(x0, y0, x1, y1, 16.0f * ui, 0.12f, 0.14f, 0.12f, 0.92f * alpha);

    if (has_sub) {
        draw_text_ac(s_toast_title, cx, cy - 11.0f * ui, 2.3f * ui, 1.0f, 0.95f, 0.75f, 1.0f * alpha, 1, 64);
        draw_text_ac(s_toast_sub, cx, cy + 13.0f * ui, 1.5f * ui, 0.90f, 0.92f, 0.88f, 0.95f * alpha, 1, 96);
    } else {
        draw_text_ac(s_toast_title, cx, cy, 2.3f * ui, 1.0f, 0.95f, 0.75f, 1.0f * alpha, 1, 64);
    }
}

static void pc_bedroom_draw_claim_modal(void) {
    if (!pc_bedroom_claim_prompt_active()) return;
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    float ui = fmaxf(1.0f, H / 720.0f);
    float cw = fminf(380.0f * ui, W * 0.90f);
    float ch = 190.0f * ui;
    float cx = W * 0.5f, cy = H * 0.5f;
    float x0 = cx - cw * 0.5f, y0 = cy - ch * 0.5f;
    float x1 = cx + cw * 0.5f, y1 = cy + ch * 0.5f;
    float bh = 42.0f * ui;
    float bw = (cw - 40.0f * ui) * 0.46f;
    float btn_y = cy + 45.0f * ui;
    float claim_cx = cx - bw * 0.55f;
    float cancel_cx = cx + bw * 0.55f;
    float claim_x0 = claim_cx - bw * 0.5f, claim_x1 = claim_cx + bw * 0.5f;
    float cancel_x0 = cancel_cx - bw * 0.5f, cancel_x1 = cancel_cx + bw * 0.5f;
    round_rect(0.0f, 0.0f, W, H, 0.0f, 0.05f, 0.08f, 0.05f, 0.55f);
    round_rect(x0 + 2.0f * ui, y0 + 3.0f * ui, x1 + 2.0f * ui, y1 + 3.0f * ui, 18.0f * ui, 0.02f, 0.04f, 0.02f, 0.28f);
    round_rect(x0, y0, x1, y1, 18.0f * ui, 0.98f, 0.97f, 0.95f, 0.99f);
    draw_str_raw("Claim this room?", cx, cy - 42.0f * ui, 2.3f * ui, 0.16f, 0.18f, 0.15f, 0.98f, 24);
    draw_str_raw("Make this your private residence", cx, cy - 14.0f * ui, 1.4f * ui, 0.40f, 0.44f, 0.40f, 0.90f, 36);
    round_rect(claim_x0 + 1.0f * ui, btn_y - bh * 0.5f + 2.0f * ui, claim_x1 + 1.0f * ui, btn_y + bh * 0.5f + 2.0f * ui, bh * 0.5f, 0.05f, 0.10f, 0.05f, 0.20f);
    round_rect(claim_x0, btn_y - bh * 0.5f, claim_x1, btn_y + bh * 0.5f, bh * 0.5f, 0.32f, 0.68f, 0.42f, 0.98f);
    draw_str_raw("CLAIM", claim_cx, btn_y, 1.8f * ui, 1.0f, 1.0f, 1.0f, 1.0f, 12);
    round_rect(cancel_x0 + 1.0f * ui, btn_y - bh * 0.5f + 2.0f * ui, cancel_x1 + 1.0f * ui, btn_y + bh * 0.5f + 2.0f * ui, bh * 0.5f, 0.05f, 0.10f, 0.05f, 0.15f);
    round_rect(cancel_x0, btn_y - bh * 0.5f, cancel_x1, btn_y + bh * 0.5f, bh * 0.5f, 0.86f, 0.85f, 0.82f, 0.98f);
    draw_str_raw("CANCEL", cancel_cx, btn_y, 1.8f * ui, 0.25f, 0.26f, 0.24f, 0.98f, 12);
}

void pc_touch_present(void) {
    Hud h;
    int i;
    if ((!pc_touch_enabled() && !pc_bedroom_claim_prompt_active()) || g_pc_window_w < 2 || g_pc_window_h < 2) return;
    refresh_action_label();
    hud_build(&h);
    if (!s_overlay_logged) {
        char line[120];
        s_overlay_logged = 1;
        snprintf(line, sizeof(line), "touch overlay hud=%d portrait=%d content=%d,%d %dx%d",
                 s_hud, g_pc_layout_portrait, g_pc_content_x, g_pc_content_y, g_pc_content_w, g_pc_content_h);
        pc_diag_milestone(line);
    }
    gl_init();
    if (!s_gl_ready) return;
    s_nvert = 0;
    {
        int op = g_pc_settings.touch_hud_opacity;
        if (op < 40) op = 40;
        if (op > 100) op = 100;
        s_fade = (float)op / 100.0f;
    }
    if (s_mark_t > 0.0f) {
        float rad = (1.15f - s_mark_t) * 36.0f;
        disc(s_mark_x, s_mark_y, rad, 0.93f, 0.78f, 0.35f, 0.55f * s_mark_t);
        disc(s_mark_x, s_mark_y, rad * 0.28f, 0.95f, 0.90f, 0.75f, 0.9f * s_mark_t);
        s_mark_t -= 0.04f;
    }
    /* Pocket Camp Floating Drag Ring & Indicator */
    if (s_drag_active && s_drag_moved) {
        float dx = s_drag_cx - s_drag_ox, dy = s_drag_cy - s_drag_oy;
        float dist = hypotf(dx, dy);
        float max_r = 75.0f * ((float)g_pc_settings.touch_hud_scale / 100.0f);
        if (max_r < 35.0f) max_r = 35.0f;
        float clamp_d = dist > max_r ? max_r : dist;
        float kx = s_drag_ox + (dist > 0.001f ? (dx / dist) * clamp_d : 0.0f);
        float ky = s_drag_oy + (dist > 0.001f ? (dy / dist) * clamp_d : 0.0f);

        /* Soft outer halo */
        disc(s_drag_ox, s_drag_oy, max_r, 0.94f, 0.95f, 0.90f, 0.28f);
        disc(s_drag_ox, s_drag_oy, max_r - 3.0f, 0.18f, 0.20f, 0.16f, 0.12f);

        /* Trail pips */
        disc(s_drag_ox + (kx - s_drag_ox) * 0.45f, s_drag_oy + (ky - s_drag_oy) * 0.45f, 5.0f, 0.95f, 0.95f, 0.90f, 0.45f);
        disc(s_drag_ox + (kx - s_drag_ox) * 0.75f, s_drag_oy + (ky - s_drag_oy) * 0.75f, 7.5f, 0.95f, 0.95f, 0.90f, 0.60f);

        /* Floating mint knob */
        disc(kx, ky, 24.0f, 0.36f, 0.65f, 0.45f, 0.92f);
        disc(kx, ky - 4.0f, 15.0f, 0.95f, 0.98f, 0.94f, 0.35f);
    }
    if (h.show_storage) {
        button(h.storage_x, h.storage_y, h.qr, 0.72f, 0.84f, 0.96f);
        draw_word("Store", h.storage_x, h.storage_y, h.qr * 0.13f);
    }
    if (h.show_stor_pages) {
        int cur_p = pc_cloud_storage_client_page() + 1;
        int max_p = (PC_CLOUD_STORAGE_MAX_ITEMS / PC_CLOUD_STORAGE_PAGE_SIZE);
        char pbuf[32];
        snprintf(pbuf, sizeof(pbuf), "PAGE %d/%d", cur_p, max_p);
        float ui = fmaxf(1.0f, (float)g_pc_window_h / 720.0f);
        button(h.stor_prev_x, h.stor_prev_y, h.stor_page_r, 0.88f, 0.90f, 0.88f);
        draw_word("<", h.stor_prev_x, h.stor_prev_y, h.stor_page_r * 0.18f);
        button(h.stor_next_x, h.stor_next_y, h.stor_page_r, 0.88f, 0.90f, 0.88f);
        draw_word(">", h.stor_next_x, h.stor_next_y, h.stor_page_r * 0.18f);
        draw_str_raw(pbuf, (h.stor_prev_x + h.stor_next_x) * 0.5f, h.stor_prev_y, 2.5f * ui, 0.20f, 0.22f, 0.20f, 1.0f, 16);
    }
    if (h.show_quick) {
        button(h.bag_x, h.bag_y, h.qr, 0.95f, 0.92f, 0.82f);
        button(h.map_x, h.map_y, h.qr, 0.86f, 0.92f, 0.96f);
        draw_word("Items", h.bag_x, h.bag_y, h.qr * 0.13f);
        draw_word("Map", h.map_x, h.map_y, h.qr * 0.14f);
    }
    if (h.show_cam) {
        button(h.cam_x, h.cam_y, h.cam_r, 0.92f, 0.88f, 0.96f);
        draw_word("Cam", h.cam_x, h.cam_y, h.cam_r * 0.14f);
    }
    if (h.show_back) {
        button(h.bx, h.by, h.br, 0.86f, 0.50f, 0.44f);
        draw_word("B", h.bx, h.by, h.br * 0.16f);
    }
    if (h.show_action) {
        const char* word = action_word();
        if (word == NULL) word = "Action";
        button(h.ax, h.ay, h.ar, 0.34f, 0.65f, 0.44f);
        draw_word(word, h.ax, h.ay, h.ar * 0.13f);
    }
    if (h.show_menu) {
        button(h.mx, h.my, h.mr, 0.93f, 0.90f, 0.84f);
        disc(h.mx - h.mr * 0.22f, h.my + h.mr * 0.05f, h.mr * 0.28f, 0.36f, 0.52f, 0.40f, 0.95f);
        disc(h.mx + h.mr * 0.16f, h.my - h.mr * 0.12f, h.mr * 0.22f, 0.36f, 0.52f, 0.40f, 0.95f);
    }
    if (h.show_close) {
        if (submenu_open()) {
            disc_raw(h.close_x + 2.0f, h.close_y + 3.0f, h.close_r, 0.05f, 0.06f, 0.05f, 0.35f);
            disc_raw(h.close_x, h.close_y, h.close_r, 0.95f, 0.88f, 0.82f, 0.96f);
            disc_raw(h.close_x, h.close_y - h.close_r * 0.18f, h.close_r * 0.70f, 1.0f, 1.0f, 1.0f, 0.30f);
            draw_word_raw("B", h.close_x, h.close_y, h.close_r * 0.22f, 0.15f, 0.15f, 0.15f, 1.0f);
        } else {
            button(h.close_x, h.close_y, h.close_r, 0.93f, 0.90f, 0.84f);
            draw_word("Back", h.close_x, h.close_y + h.close_r + 14.0f, h.close_r * 0.16f);
        }
    }
    if (h.show_context) {
        const char* word = action_word();
        if (submenu_open()) {
            disc_raw(h.ctx_x + 2.0f, h.ctx_y + 3.0f, h.ctx_r, 0.05f, 0.06f, 0.05f, 0.35f);
            disc_raw(h.ctx_x, h.ctx_y, h.ctx_r, 0.82f, 0.95f, 0.84f, 0.96f);
            disc_raw(h.ctx_x, h.ctx_y - h.ctx_r * 0.18f, h.ctx_r * 0.70f, 1.0f, 1.0f, 1.0f, 0.30f);
            if (word != NULL) draw_word_raw(word, h.ctx_x, h.ctx_y, h.ctx_r * 0.18f, 0.12f, 0.25f, 0.14f, 1.0f);
        } else {
            button(h.ctx_x, h.ctx_y, h.ctx_r, 0.98f, 0.96f, 0.90f);
            disc(h.ctx_x, h.ctx_y, h.ctx_r * 0.28f, 0.36f, 0.52f, 0.40f, 0.95f);
            if (word != NULL) draw_word(word, h.ctx_x, h.ctx_y - h.ctx_r - 16.0f, h.ctx_r * 0.16f);
        }
    }
    if (h.choice_n > 0) {
        mChoice_c* c = mChoice_Get_base_window_p();
        for (i = 0; i < h.choice_n; i++) {
            char txt[16];
            txt[0] = 0;
            if (c != NULL && i < c->data.choice_num && c->data.string_lens[i] > 0) {
                int len = c->data.string_lens[i];
                int j, out = 0;
                for (j = 0; j < len && out < 12; j++) {
                    u8 ch = c->data.strings[i][j];
                    if (ch >= 0x20 && ch <= 0x7E) txt[out++] = (char)ch;
                }
                txt[out] = 0;
            }
            if (txt[0] == 0) {
                static const char* words[6] = { "One", "Two", "Three", "Four", "Five", "Six" };
                snprintf(txt, sizeof(txt), "%s", words[i]);
            }
            button(h.choice_x, h.choice_y[i], h.choice_r, 0.98f, 0.96f, 0.90f);
            draw_word(txt, h.choice_x, h.choice_y[i], h.choice_r * 0.14f);
        }
    }
    if (h.show_sub_tabs) {
        static const char* labels[4] = { "Items", "Letters", "Fish", "Bugs" };
        static const float tab_colors[4][3] = {
            { 0.98f, 0.92f, 0.72f },  /* Items: Soft pastel cream/gold */
            { 0.98f, 0.80f, 0.78f },  /* Letters: Soft pastel pink */
            { 0.76f, 0.88f, 1.00f },  /* Fish: Sky pastel blue */
            { 0.78f, 0.96f, 0.82f }   /* Bugs: Soft mint green */
        };
        for (i = 0; i < h.sub_tab_n; i++) {
            float cx = h.sub_tab_x[i], cy = h.sub_tab_y[i], rad = h.sub_tab_r;
            disc_raw(cx + 2.0f, cy + 3.0f, rad, 0.05f, 0.06f, 0.05f, 0.35f);
            disc_raw(cx, cy, rad, tab_colors[i][0], tab_colors[i][1], tab_colors[i][2], 0.96f);
            disc_raw(cx, cy - rad * 0.18f, rad * 0.70f, 1.0f, 1.0f, 1.0f, 0.32f);
            draw_word_raw(labels[i], cx, cy, rad * 0.15f, 0.15f, 0.16f, 0.15f, 1.0f);
        }
    }
    if (!g_pc_settings.touch_virtual_pad && g_pc_layout_portrait && h.choice_n == 0) {
        char line[64];
        info_line(line, sizeof(line));
        draw_left(line, (float)g_pc_content_x + 18.0f,
                  (float)(g_pc_content_y + g_pc_content_h) + 42.0f, 3.2f);
    }
    if (h.show_keys) {
        for (i = 0; i < h.key_n; i++) {
            float r = (h.kc[i] == ' ' || h.kc[i] == '\n') ? h.kr * 1.55f : h.kr;
            button(h.kx[i], h.ky[i], r, 0.95f, 0.93f, 0.88f);
            if (h.kc[i] == '\n') glyph('E', h.kx[i], h.ky[i], r * 0.16f);
            else if (h.kc[i] != ' ') glyph(h.kc[i], h.kx[i], h.ky[i], r * 0.18f);
        }
    }
    if (s_sheet.active) {
        mobile_sheet_draw();
    }
    if (pc_bedroom_claim_prompt_active()) {
        pc_bedroom_draw_claim_modal();
    }
    draw_toast_banner();
    if (s_nvert < 3) { pc_gx_external_gl_done(); return; }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, g_pc_window_w, g_pc_window_h);
    glUseProgram(s_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_font_tex);
    {
        GLint uloc = glGetUniformLocation(s_prog, "uFont");
        if (uloc >= 0) glUniform1i(uloc, 0);
    }
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(s_nvert * 8 * (int)sizeof(float)), s_vert, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * (GLsizei)sizeof(float), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * (GLsizei)sizeof(float), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * (GLsizei)sizeof(float), (void*)(6 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, s_nvert);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    pc_gx_external_gl_done();
}
