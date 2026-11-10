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

#include "game.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "m_msg.h"
#include "m_field_info.h"
#include "m_name_table.h"
#include "m_actor.h"
#include <dolphin/pad.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOUCH_STICK 80
#define TOUCH_ARRIVE 25.0f
#define HUD_OFF 0
#define HUD_TOWN 1
#define HUD_MENU 2
#define HUD_DIALOG 3
#define HUD_KEYS 4

enum { HIT_NONE = 0, HIT_PICTURE, HIT_STICK, HIT_NUB, HIT_ACTION, HIT_BACK, HIT_MENU, HIT_KEY, HIT_BAG, HIT_MAP };

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
static float s_title_y = 0.0f;
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

enum { FINGERS = 4 };
typedef struct { SDL_FingerID id; float x, y; int down; } Finger;
static Finger s_fin[FINGERS];

typedef struct {
    int show_stick, show_nub, show_action, show_back, show_menu, show_keys, show_quick;
    float sx, sy, sr, nx, ny, nr, ax, ay, ar, bx, by, br, mx, my, mr;
    float bag_x, bag_y, map_x, map_y, qr;
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
    snprintf(buf, n, "sc=%d op=%d hand=%c stk=%d move=%d hap=%d",
             g_pc_settings.touch_hud_scale, g_pc_settings.touch_hud_opacity,
             g_pc_settings.touch_left_hand ? 'L' : 'R',
             g_pc_settings.touch_stick, g_pc_settings.touch_move, g_pc_settings.touch_haptics);
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

static int message_open(void) {
    mMsg_Window_c* w = mMsg_Get_base_window_p();
    return w != NULL && mMsg_Check_main_hide(w) == FALSE;
}

static int submenu_open(void) {
    GAME_PLAY* play;
    if (gamePT == NULL) return 0;
    play = (GAME_PLAY*)gamePT;
    return play->submenu.menu_type != 0;
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
    *fx = (x - (float)g_pc_content_x) / (float)g_pc_content_w * 320.0f;
    *fy = (y - (float)g_pc_content_y) / (float)g_pc_content_h * 240.0f;
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
    float rad, gap;
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
           !pc_play_online_menu_active() && !message_open() && !submenu_open() && !keys_up();
    menu = g_pc_paused || g_pc_title_main_menu_visible || pc_settings_menu_active() || pc_play_online_menu_active();
    dialog = !menu && (message_open() || submenu_open());
    if (keys_up()) s_hud = HUD_KEYS;
    else if (dialog) s_hud = HUD_DIALOG;
    else if (menu) s_hud = HUD_MENU;
    else if (town) s_hud = HUD_TOWN;
    else s_hud = HUD_TOWN;
    rad = 0.085f * (W < H ? W : H);
    if (rad < 42.0f) rad = 42.0f;
    if (rad > 78.0f) rad = 78.0f;
    {
        int sc = g_pc_settings.touch_hud_scale;
        if (sc < 70) sc = 70;
        if (sc > 140) sc = 140;
        rad *= (float)sc / 100.0f;
    }
    gap = rad * 2.25f;
    h->show_keys = keys_up();
    h->show_nub = !h->show_keys;
    h->show_stick = g_pc_settings.touch_stick && !h->show_keys && !(town && s_have_target);
    h->show_action = dialog || (town && strcmp(s_action, "A") != 0);
    h->show_back = dialog || h->show_keys || g_pc_paused || pc_settings_menu_active() || pc_play_online_menu_active();
    h->show_menu = !g_pc_title_main_menu_visible || pc_settings_menu_active() || pc_play_online_menu_active();
    if (g_pc_layout_portrait) {
        float top = (float)(g_pc_content_y + g_pc_content_h) + 12.0f;
        float bot = H - ib - 8.0f;
        if (top < it) top = it;
        if (bot < top + rad * 2.0f) bot = top + rad * 2.2f;
        if (h->show_keys) {
            place_keys(h, top, top + (bot - top) * 0.62f, il + 8.0f, W - ir - 8.0f);
            top = top + (bot - top) * 0.62f;
        }
        h->sx = il + rad * 1.55f;
        h->sy = (top + bot) * 0.5f;
        h->sr = rad * 1.25f;
        h->nx = h->sx;
        h->ny = top + rad * 0.55f;
        h->nr = rad * 0.42f;
        h->ax = W - ir - rad * 1.25f;
        h->ay = h->sy;
        h->ar = rad * 1.15f;
        h->bx = h->ax - gap;
        h->by = h->ay;
        h->br = rad * 0.82f;
        h->mx = h->bx;
        h->my = top + rad * 0.7f;
        h->mr = rad * 0.62f;
    } else {
        h->sx = il + rad * 1.5f;
        h->sy = H - ib - rad * 1.35f;
        h->sr = rad * 1.2f;
        h->nx = h->sx + rad * 1.7f;
        h->ny = h->sy - rad * 1.55f;
        h->nr = rad * 0.4f;
        h->ax = W - ir - rad * 1.3f;
        h->ay = H - ib - rad * 1.3f;
        h->ar = rad * 1.1f;
        h->bx = h->ax - gap;
        h->by = h->ay;
        h->br = rad * 0.78f;
        h->mx = h->ax;
        h->my = h->ay - gap;
        h->mr = rad * 0.58f;
        if (h->show_keys) {
            place_keys(h, H - ib - rad * 3.4f, H - ib - 6.0f, W * 0.22f, W * 0.78f);
        }
    }
    /* Inventory (Y) and map (X) sit with the leaf. They use the game's own buttons. */
    h->show_quick = town && !h->show_keys;
    h->qr = rad * 0.5f;
    if (g_pc_layout_portrait) {
        float mid = (h->sy + h->ny) * 0.5f;
        h->bag_x = W * 0.40f;
        h->map_x = W * 0.56f;
        h->bag_y = h->map_y = mid;
    } else {
        h->bag_x = h->mx - h->mr * 2.3f;
        h->bag_y = h->my;
        h->map_x = h->mx;
        h->map_y = h->my - h->mr * 2.3f;
    }
    if (g_pc_settings.touch_left_hand) {
        h->sx = W - h->sx; h->nx = W - h->nx; h->ax = W - h->ax;
        h->bx = W - h->bx; h->mx = W - h->mx;
        h->bag_x = W - h->bag_x; h->map_x = W - h->map_x;
        for (int i = 0; i < h->key_n; i++) h->kx[i] = W - h->kx[i];
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
    GAME_PLAY* play;
    if (gamePT == NULL || message_open() || g_pc_paused || g_pc_title_main_menu_visible) return 0;
    if (pc_settings_menu_active() || pc_play_online_menu_active()) return 0;
    play = (GAME_PLAY*)gamePT;
    if (play->submenu.menu_type != 0 || play->submenu.start_refuse) return 0;
    return 1;
}

static int steer_toward(float tx, float tz, signed char* stick_x, signed char* stick_y) {
    GAME_PLAY* play;
    PLAYER_ACTOR* player;
    float px, pz, dx, dz, dist, fx, fz, fl, rx, rz, sx, sy, mag;
    if (!can_steer() || gamePT == NULL) return 0;
    play = (GAME_PLAY*)gamePT;
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
    GAME_PLAY* play;
    PLAYER_ACTOR* player;
    float fov, aspect, nx, ny, tan_y, fx, fy, fz, fl, rx, rz, ux, uy, uz, dx, dy, dz, dl, t, py;
    if (!in_picture(wx, wy) || gamePT == NULL) return 0;
    play = (GAME_PLAY*)gamePT;
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

static int point_is_interesting(float wx, float wz) {
    GAME_PLAY* play;
    PLAYER_ACTOR* player;
    xyz_t at;
    mActor_name_t* fg;
    int i;
    if (gamePT == NULL) return 0;
    play = (GAME_PLAY*)gamePT;
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return 0;
    at.x = wx; at.y = player->actor_class.world.position.y; at.z = wz;
    fg = mFI_GetUnitFG(at);
    if (fg != NULL && *fg != EMPTY_NO) return 1;
    if (play->actor_info.list[ACTOR_PART_NPC].actor != NULL) {
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        for (i = 0; i < list->num_actors; i++) {
            ACTOR* a = &list->actor[i];
            float dx, dz;
            if (a == NULL) continue;
            dx = a->world.position.x - wx;
            dz = a->world.position.z - wz;
            if (dx * dx + dz * dz < 90.0f * 90.0f) return 1;
        }
    }
    return 0;
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

static void begin_move(float x, float y) {
    float wx, wz;
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
    s_interact = point_is_interesting(wx, wz);
    pc_touch_buzz();
}

static void refresh_action_label(void) {
    GAME_PLAY* play;
    PLAYER_ACTOR* player;
    xyz_t ahead;
    mActor_name_t* fg;
    int i;
    snprintf(s_action, sizeof(s_action), "A");
    if (message_open()) { snprintf(s_action, sizeof(s_action), "Next"); return; }
    if (submenu_open()) { snprintf(s_action, sizeof(s_action), "OK"); return; }
    if (gamePT == NULL) return;
    play = (GAME_PLAY*)gamePT;
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
    if (g_pc_title_main_menu_visible) {
        int i = (int)((fy - 135.0f) / 18.0f);
        if (i >= 0 && i < 5) {
            char line[64];
            s_title_valid = 1;
            s_title_y = fy;
            snprintf(line, sizeof(line), "touch title row %d", i);
            pc_diag_milestone(line);
        }
        return;
    }
    if (message_open() || submenu_open()) s_pulse_a = 1;
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
    int n = 0, down, rising, hit, key = -1;
    (void)wheel_hold; (void)emote_hold; (void)pointer_x; (void)pointer_y;
    s_title_valid = 0;
    refresh_action_label();
    if (!pc_touch_enabled() || buttons == NULL || g_pc_window_w < 2) return;
    hud_build(&hud);
    if (s_pulse_a) { *buttons |= PAD_BUTTON_A; s_pulse_a = 0; pc_touch_buzz(); }
    if (s_pulse_b) { *buttons |= PAD_BUTTON_B; s_pulse_b = 0; pc_touch_buzz(); }
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
        return;
    }
    if (!down) {
        if (s_pointer_was && hypotf(s_last_x - s_down_x, s_last_y - s_down_y) < 36.0f) {
            if (s_down_hit == HIT_PICTURE) on_picture_release(s_down_x, s_down_y);
            else if (s_down_hit == HIT_NUB) {
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
        s_pointer_was = 0;
        s_down_hit = HIT_NONE;
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
            if (hit == HIT_PICTURE && can_steer()) begin_move(x, y);
            else if (hit != HIT_NONE && hit != HIT_PICTURE) s_have_target = 0;
            if (hit == HIT_BAG) { *buttons |= PAD_BUTTON_Y; pc_touch_buzz(); pc_diag_milestone("touch inventory"); }
            if (hit == HIT_MAP) { *buttons |= PAD_BUTTON_X; pc_touch_buzz(); pc_diag_milestone("touch map"); }
            if (hit == HIT_ACTION) { pc_touch_buzz(); pc_diag_milestone("touch action"); }
            if (hit == HIT_BACK) {
                pc_touch_buzz();
                pc_diag_milestone("touch back");
                if (g_pc_paused) pause_key(SDLK_ESCAPE);
                else *buttons |= PAD_BUTTON_B;
            }
        }
        s_last_x = x; s_last_y = y;
        if (hit == HIT_STICK && hud.show_stick && hud.sr > 1.0f) {
            float dx = (x - hud.sx) / hud.sr;
            float dy = (y - hud.sy) / hud.sr;
            float mag = sqrtf(dx * dx + dy * dy);
            if (mag > 1.0f) { dx /= mag; dy /= mag; }
            s_knob_x = dx; s_knob_y = dy;
            if (stick_x && stick_y && *stick_x == 0 && *stick_y == 0) {
                *stick_x = (signed char)(dx * (float)TOUCH_STICK);
                *stick_y = (signed char)(-dy * (float)TOUCH_STICK);
            }
            s_have_target = 0;
        } else if (s_down_hit == HIT_ACTION || hit == HIT_ACTION) {
            *buttons |= PAD_BUTTON_A;
        } else if (s_down_hit == HIT_BACK || hit == HIT_BACK) {
            if (!g_pc_paused) *buttons |= PAD_BUTTON_B;
        }
        s_pointer_was = 1;
    }
    if (s_have_target && stick_x && stick_y && *stick_x == 0 && *stick_y == 0)
        (void)steer_toward(s_target_x, s_target_z, stick_x, stick_y);
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

static void disc(float cx, float cy, float rad, float r, float g, float b, float a) {
    float W = (float)g_pc_window_w, H = (float)g_pc_window_h;
    a *= s_fade;
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

static void button(float cx, float cy, float rad, float r, float g, float b) {
    disc(cx + 2.0f, cy + 3.0f, rad, 0.05f, 0.06f, 0.05f, 0.28f);
    disc(cx, cy, rad, r, g, b, 0.88f);
    disc(cx, cy - rad * 0.18f, rad * 0.72f, 1.0f, 0.98f, 0.94f, 0.18f);
}

/* 5-wide marks so a key is readable without a texture. Rows are top to bottom. */
static const unsigned char k_glyph[27][7] = {
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
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x08,0x10,0x10,0x10,0x10,0x10,0x08}
};

static void glyph(char ch, float cx, float cy, float s) {
    int idx = -1, row, col;
    if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
    else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
    else if (ch == '<') idx = 26;
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

static void draw_word(const char* s, float cx, float cy, float pix) {
    int n = 0, i;
    float w;
    if (s == NULL) return;
    while (s[n] != 0 && n < 12) n++;
    w = (float)n * 5.0f * pix;
    for (i = 0; i < n; i++) {
        if (s[i] == ' ') continue;
        glyph(s[i], cx - w * 0.5f + ((float)i + 0.5f) * 5.0f * pix, cy, pix);
    }
}

static const char* action_word(void) {
    if (strcmp(s_action, "Pick up") == 0) return "Pick";
    if (strcmp(s_action, "Shake") == 0) return "Shake";
    if (strcmp(s_action, "Open") == 0) return "Open";
    if (strcmp(s_action, "Talk") == 0) return "Talk";
    if (strcmp(s_action, "Next") == 0) return "Next";
    if (strcmp(s_action, "OK") == 0) return "OK";
    return NULL;
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
    if (h.show_stick) {
        button(h.sx, h.sy, h.sr, 0.93f, 0.90f, 0.84f);
        disc(h.sx + s_knob_x * h.sr * 0.45f, h.sy + s_knob_y * h.sr * 0.45f, h.sr * 0.42f, 0.36f, 0.52f, 0.40f, 0.95f);
    }
    if (h.show_nub) button(h.nx, h.ny, h.nr, g_pc_settings.touch_stick ? 0.36f : 0.75f, g_pc_settings.touch_stick ? 0.52f : 0.72f, g_pc_settings.touch_stick ? 0.40f : 0.66f);
    if (h.show_quick) {
        button(h.bag_x, h.bag_y, h.qr, 0.93f, 0.90f, 0.84f);
        button(h.map_x, h.map_y, h.qr, 0.93f, 0.90f, 0.84f);
        glyph('I', h.bag_x, h.bag_y, h.qr * 0.22f);
        glyph('M', h.map_x, h.map_y, h.qr * 0.22f);
    }
    if (h.show_back) button(h.bx, h.by, h.br, 0.76f, 0.45f, 0.36f);
    if (h.show_action) {
        const char* word = action_word();
        button(h.ax, h.ay, h.ar, 0.36f, 0.52f, 0.40f);
        if (word != NULL) {
            float lx = g_pc_settings.touch_left_hand ? h.ax + h.ar + 28.0f : h.ax - h.ar - 28.0f;
            draw_word(word, lx, h.ay, h.ar * 0.09f);
        }
    }
    if (h.show_menu) {
        button(h.mx, h.my, h.mr, 0.93f, 0.90f, 0.84f);
        disc(h.mx - h.mr * 0.22f, h.my + h.mr * 0.05f, h.mr * 0.28f, 0.36f, 0.52f, 0.40f, 0.95f);
        disc(h.mx + h.mr * 0.16f, h.my - h.mr * 0.12f, h.mr * 0.22f, 0.36f, 0.52f, 0.40f, 0.95f);
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
