/* pc_touch.c - see pc_touch.h. Local pad only. The discs are drawn with a small shader,
 * not with a copied texture. */
#include "pc_touch.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_dedicated.h"
#include "pc_pause_menu.h"
#include "pc_settings_menu.h"
#include "pc_play_online_menu.h"
#include "pc_crash.h"
#include "pc_typing.h"
#include "pc_chat.h"

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

enum { HIT_NONE = 0, HIT_PICTURE, HIT_STICK, HIT_NUB, HIT_ACTION, HIT_BACK, HIT_MENU, HIT_KEY, HIT_BAG, HIT_MAP, HIT_CLOSE, HIT_CONTEXT, HIT_CHOICE, HIT_SUB_TAB };

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
static char s_tap_kind[16] = "-";
static int s_drag_active = 0;
static float s_drag_ox = 0.0f, s_drag_oy = 0.0f;
static float s_drag_cx = 0.0f, s_drag_cy = 0.0f;
static Uint32 s_drag_t0 = 0;
static int s_drag_moved = 0;

enum { FINGERS = 4 };
typedef struct { SDL_FingerID id; float x, y; int down; } Finger;
static Finger s_fin[FINGERS];

typedef struct {
    int show_stick, show_nub, show_action, show_back, show_menu, show_keys, show_quick;
    int show_close, show_context, choice_n;
    int show_sub_tabs, sub_tab_n;
    float sx, sy, sr, nx, ny, nr, ax, ay, ar, bx, by, br, mx, my, mr;
    float bag_x, bag_y, map_x, map_y, qr;
    float close_x, close_y, close_r, ctx_x, ctx_y, ctx_r;
    float choice_x, choice_r, choice_y[6];
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
           !pc_play_online_menu_active() && !pc_creator_menu_active() &&
           !message_open() && !submenu_open() && !keys_up();
    menu = g_pc_paused || g_pc_title_main_menu_visible || pc_settings_menu_active() ||
           pc_play_online_menu_active() || pc_creator_menu_active();
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
    /* Pocket Camp controls: Action, Back, Menu, Bag, Map are always available */
    h->show_action = !h->show_keys;
    h->show_back = !h->show_keys;
    h->show_menu = !h->show_keys;
    h->show_quick = !h->show_keys && (town || menu);
    h->show_close = !h->show_keys && (menu || dialog);
    h->show_context = 0;

    if (g_pc_layout_portrait) {
        /* Portrait HUD: Action and Back in bottom-right; Pockets & Map in bottom-left; Menu in top-right */
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
        h->bag_x = W - h->bag_x; h->map_x = W - h->map_x;
        h->close_x = W - h->close_x;
        h->ctx_x = W - h->ctx_x;
        for (int i = 0; i < h->key_n; i++) h->kx[i] = W - h->kx[i];
        for (int i = 0; i < h->sub_tab_n; i++) h->sub_tab_x[i] = W - h->sub_tab_x[i];
    }
    if (!h->show_stick) { h->sx = -1000.0f; }
}

static int hit_disc(float x, float y, float cx, float cy, float r) {
    float dx = x - cx, dy = y - cy;
    r *= 1.15f; /* a little slop so a fold does not miss the disc */
    return dx * dx + dy * dy <= r * r;
}

static int classify(float x, float y, Hud* h, int* key) {
    int i;
    *key = -1;
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
    if (h->show_quick && hit_disc(x, y, h->bag_x, h->bag_y, h->qr)) return HIT_BAG;
    if (h->show_quick && hit_disc(x, y, h->map_x, h->map_y, h->qr)) return HIT_MAP;
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
    if (pc_settings_menu_active() || pc_play_online_menu_active() || pc_creator_menu_active()) return 0;
    if (play->submenu.menu_type != 0 || play->submenu.start_refuse) return 0;
    return 1;
}

static int steer_toward(float tx, float tz, signed char* stick_x, signed char* stick_y) {
    GAME_PLAY* play = get_game_play();
    PLAYER_ACTOR* player;
    float px, pz, dx, dz, dist, fx, fz, fl, rx, rz, sx, sy, mag;
    if (!can_steer() || play == NULL) return 0;
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return 0;
    px = player->actor_class.world.position.x;
    pz = player->actor_class.world.position.z;
    dx = tx - px;
    dz = tz - pz;
    dist = sqrtf(dx * dx + dz * dz);
    if (s_best_dist < 0.0f || dist < s_best_dist - 1.5f) {
        s_best_dist = dist;
        s_stall = 0;
        if (s_slide != 0) s_slide = 0; /* moving again: straight at the tap */
    } else if (++s_stall == 20 || s_stall == 45) {
        s_slide = (s_stall == 20) ? 1 : -1; /* slide off the obstacle */
    } else if (s_stall == 70) {
        s_slide = 0; /* give up the slide and walk straight */
    } else if (s_stall > 95) {
        s_have_target = 0;
        s_interact = 0;
        s_stall = 0;
        s_slide = 0;
        s_best_dist = -1.0f;
        return 0;
    }
    if (dist < TOUCH_ARRIVE) {
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
    rx = fz; rz = -fx;
    dx /= dist; dz /= dist;
    sx = dx * rx + dz * rz;
    sy = dx * fx + dz * fz;
    mag = sqrtf(sx * sx + sy * sy);
    if (mag < 0.05f) return 0;
    sx = sx / mag * (float)TOUCH_STICK;
    sy = sy / mag * (float)TOUCH_STICK;
    if (s_slide != 0) {
        float ox = sx, oy = sy;
        sx = ox * 0.55f - oy * 0.45f * (float)s_slide;
        sy = oy * 0.55f + ox * 0.45f * (float)s_slide;
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
    float fov, aspect, nx, ny, tan_y, fx, fy, fz, fl, rx, rz, ux, uy, uz, dx, dy, dz, dl, t, py;
    if (!in_picture(wx, wy) || play == NULL) return 0;
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return 0;
    fov = play->camera.perspective.fov_y;
    if (fov < 5.0f || fov > 170.0f) fov = 45.0f;
    aspect = (float)g_pc_content_w / (float)g_pc_content_h;
    nx = ((wx - (float)g_pc_content_x) / (float)g_pc_content_w) * 2.0f - 1.0f;
    ny = 1.0f - ((wy - (float)g_pc_content_y) / (float)g_pc_content_h) * 2.0f;
    tan_y = tanf(fov * 0.5f * 3.14159265f / 180.0f);
    fx = play->camera.lookat.center.x - play->camera.lookat.eye.x;
    fy = play->camera.lookat.center.y - play->camera.lookat.eye.y;
    fz = play->camera.lookat.center.z - play->camera.lookat.eye.z;
    fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl < 1.0f) return 0;
    fx /= fl; fy /= fl; fz /= fl;
    rx = fz; rz = -fx;
    { float rl = sqrtf(rx * rx + rz * rz); if (rl < 0.01f) return 0; rx /= rl; rz /= rl; }
    ux = fy * rz; uy = fz * rx - fx * rz; uz = -fy * rx;
    { float ul = sqrtf(ux * ux + uy * uy + uz * uz); if (ul < 0.01f) return 0; ux /= ul; uy /= ul; uz /= ul; }
    dx = fx + rx * (nx * tan_y * aspect) + ux * (ny * tan_y);
    dy = fy + uy * (ny * tan_y);
    dz = fz + rz * (nx * tan_y * aspect) + uz * (ny * tan_y);
    dl = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dl < 0.001f) return 0;
    dx /= dl; dy /= dl; dz /= dl;
    py = player->actor_class.world.position.y;
    if (fabsf(dy) < 0.02f) return 0;
    t = (py - play->camera.lookat.eye.y) / dy;
    if (t < 1.0f || t > 8000.0f) return 0;
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

static void begin_move(float x, float y, int fresh) {
    float wx, wz;
    const char* kind;
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
    if (!pc_touch_enabled() || buttons == NULL || g_pc_window_w < 2) return;
    hud_build(&hud);
    if (s_tap_ready) {
        float dx = s_gest_x - s_tap_x, dy = s_gest_y - s_tap_y;
        s_tap_ready = 0;
        tap_handled = 1;
        if (dx * dx + dy * dy < 64.0f * 64.0f) {
            int th = 0, tk = -1;
            th = classify(s_tap_x, s_tap_y, &hud, &tk);
            if (th == HIT_PICTURE) {
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
                if (g_pc_title_main_menu_visible && !pc_settings_menu_active() && !pc_play_online_menu_active())
                    latch_button(PAD_BUTTON_START, "Start");
                else if (message_open() || submenu_open() || pc_settings_menu_active() || pc_play_online_menu_active())
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
            } else if (th == HIT_BAG) {
                latch_button(PAD_BUTTON_Y, "Y");
                pc_touch_buzz();
                pc_diag_milestone("touch inventory");
            } else if (th == HIT_MAP) {
                latch_button(PAD_BUTTON_X, "X");
                pc_touch_buzz();
                pc_diag_milestone("touch map");
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
    if (n >= 2 && g_pc_settings.touch_camera && cstick_x && cstick_y) {
        int sx = (int)((x2 - x) * 0.15f);
        int sy = (int)(-(y2 - y) * 0.15f);
        if (sx > 80) sx = 80;
        if (sx < -80) sx = -80;
        if (sy > 80) sy = 80;
        if (sy < -80) sy = -80;
        *cstick_x = (signed char)sx;
        *cstick_y = (signed char)sy;
        s_have_target = 0;
        s_pointer_was = 1;
        latch_consume(buttons);
        return;
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
            if (s_down_hit == HIT_PICTURE && !can_steer()) on_picture_release(s_down_x, s_down_y);
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
                if (message_open() || submenu_open() || pc_settings_menu_active() || pc_play_online_menu_active())
                    s_pulse_b = 1;
                else if (g_pc_paused) pause_key(SDLK_ESCAPE);
                else pc_pause_menu_toggle();
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
        if (s_pointer_was && submenu_open()) {
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

/* Soft disc. uv is -1..1 in the quad; the fragment keeps a circle. */
static GLuint s_prog = 0, s_vao = 0, s_vbo = 0;
static int s_gl_ready = 0, s_gl_failed = 0;

static GLuint compile_one(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static void gl_init(void) {
#ifdef PC_GLES
    const char* vs = "#version 300 es\nprecision mediump float;\nlayout(location=0) in vec2 aPos;\nlayout(location=1) in vec4 aColor;\nlayout(location=2) in vec2 aUv;\nout vec4 vColor;\nout vec2 vUv;\nvoid main(){ gl_Position=vec4(aPos,0.0,1.0); vColor=aColor; vUv=aUv; }\n";
    const char* fs = "#version 300 es\nprecision mediump float;\nin vec4 vColor;\nin vec2 vUv;\nout vec4 frag;\nvoid main(){ float d=length(vUv); float a=smoothstep(1.0,0.82,d); if(a<0.02) discard; frag=vec4(vColor.rgb, vColor.a*a); }\n";
#else
    const char* vs = "#version 330 core\nlayout(location=0) in vec2 aPos;\nlayout(location=1) in vec4 aColor;\nlayout(location=2) in vec2 aUv;\nout vec4 vColor;\nout vec2 vUv;\nvoid main(){ gl_Position=vec4(aPos,0.0,1.0); vColor=aColor; vUv=aUv; }\n";
    const char* fs = "#version 330 core\nin vec4 vColor;\nin vec2 vUv;\nout vec4 frag;\nvoid main(){ float d=length(vUv); float a=smoothstep(1.0,0.82,d); if(a<0.02) discard; frag=vec4(vColor.rgb, vColor.a*a); }\n";
#endif
    GLuint v, f, p;
    GLint ok = 0;
    if (s_gl_ready || s_gl_failed) return;
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
    s_gl_ready = 1;
}

static float s_vert[8 * 8000];
static int s_nvert = 0;

static void vert(float x, float y, float u, float v, float r, float g, float b, float a) {
    float* p;
    if (s_nvert >= 8000) return;
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

static void disc(float cx, float cy, float rad, float r, float g, float b, float a) {
    a *= s_fade;
    disc_raw(cx, cy, rad, r, g, b, a);
}

static void button(float cx, float cy, float rad, float r, float g, float b) {
    disc(cx + 2.0f, cy + 3.0f, rad, 0.05f, 0.06f, 0.05f, 0.28f);
    disc(cx, cy, rad, r, g, b, 0.88f);
    disc(cx, cy - rad * 0.18f, rad * 0.72f, 1.0f, 0.98f, 0.94f, 0.18f);
}

/* 5-wide marks so a key is readable without a texture. Rows are top to bottom. */
static const unsigned char k_glyph[37][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},     {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x08,0x10,0x10,0x10,0x10,0x10,0x08},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}
};

static void glyph_raw(char ch, float cx, float cy, float s, float r, float g, float b, float a) {
    int idx = -1, row, col;
    if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
    else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
    else if (ch == '<') idx = 26;
    else if (ch >= '0' && ch <= '9') idx = 27 + (ch - '0');
    if (idx < 0) return;
    for (row = 0; row < 7; row++) {
        unsigned bits = k_glyph[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (1u << (4 - col))) {
                float px = cx + ((float)col - 2.0f) * s;
                float py = cy + ((float)row - 3.0f) * s;
                disc_raw(px, py, s * 0.55f, r, g, b, a);
            }
        }
    }
}

static void draw_word_raw(const char* s, float cx, float cy, float pix, float r, float g, float b, float a) {
    int n = 0, i;
    float w, max_w;
    if (s == NULL) return;
    while (s[n] != 0 && n < 12) n++;
    if (n == 0) return;
    max_w = (pix / 0.13f) * 1.55f;
    if (pix > 0.0f && (float)n * 5.0f * pix > max_w) {
        pix = max_w / ((float)n * 5.0f);
    }
    w = (float)n * 5.0f * pix;
    for (i = 0; i < n; i++) {
        if (s[i] == ' ') continue;
        glyph_raw(s[i], cx - w * 0.5f + ((float)i + 0.5f) * 5.0f * pix, cy, pix, r, g, b, a);
    }
}

static void glyph(char ch, float cx, float cy, float s) {
    int idx = -1, row, col;
    if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
    else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
    else if (ch == '<') idx = 26;
    else if (ch >= '0' && ch <= '9') idx = 27 + (ch - '0');
    if (idx < 0) return;
    for (row = 0; row < 7; row++) {
        unsigned bits = k_glyph[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (1u << (4 - col))) {
                float px = cx + ((float)col - 2.0f) * s;
                float py = cy + ((float)row - 3.0f) * s;
                disc(px, py, s * 0.55f, 0.16f, 0.18f, 0.15f, 0.95f);
            }
        }
    }
}

static void draw_left(const char* s, float x, float cy, float pix) {
    int i, n = 0;
    while (s[n] != 0 && n < 28) n++;
    for (i = 0; i < n; i++) {
        float cx = x + ((float)i + 0.5f) * 5.0f * pix;
        if (s[i] == ' ') continue;
        if (s[i] == ':') {
            disc(cx, cy - pix, pix * 0.35f, 0.16f, 0.18f, 0.15f, 0.95f);
            disc(cx, cy + pix, pix * 0.35f, 0.16f, 0.18f, 0.15f, 0.95f);
            continue;
        }
        glyph(s[i], cx, cy, pix);
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
    int n = 0, i;
    float w, max_w;
    if (s == NULL) return;
    while (s[n] != 0 && n < 12) n++;
    if (n == 0) return;
    /* Scale down font if word is wide so it fits cleanly inside button radius */
    max_w = (pix / 0.13f) * 1.55f;
    if (pix > 0.0f && (float)n * 5.0f * pix > max_w) {
        pix = max_w / ((float)n * 5.0f);
    }
    w = (float)n * 5.0f * pix;
    for (i = 0; i < n; i++) {
        if (s[i] == ' ') continue;
        glyph(s[i], cx - w * 0.5f + ((float)i + 0.5f) * 5.0f * pix, cy, pix);
    }
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

void pc_touch_present(void) {
    Hud h;
    int i;
    if (!pc_touch_enabled() || g_pc_window_w < 2 || g_pc_window_h < 2) return;
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
    if (h.show_quick) {
        button(h.bag_x, h.bag_y, h.qr, 0.95f, 0.92f, 0.82f);
        button(h.map_x, h.map_y, h.qr, 0.86f, 0.92f, 0.96f);
        draw_word("Items", h.bag_x, h.bag_y, h.qr * 0.13f);
        draw_word("Map", h.map_x, h.map_y, h.qr * 0.14f);
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
    if (s_nvert < 3) { pc_gx_external_gl_done(); return; }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, g_pc_window_w, g_pc_window_h);
    glUseProgram(s_prog);
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
    glBindVertexArray(0);
    glUseProgram(0);
    pc_gx_external_gl_done();
}
