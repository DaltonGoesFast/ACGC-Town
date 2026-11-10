/* pc_touch.c - see pc_touch.h. Local pad only. */
#include "pc_touch.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_dedicated.h"
#include "pc_pause_menu.h"
#include "pc_menu_util.h"
#include "pc_text_draw.h"
#include "pc_play_online_menu.h"

#include "game.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "m_submenu.h"
#include "m_msg.h"
#include "m_field_info.h"
#include "m_font.h"
#include "m_name_table.h"
#include "m_actor.h"
#include "pc_chat.h"
#include <dolphin/pad.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOUCH_STICK 80
#define TOUCH_ARRIVE 25.0f

enum { BTN_NONE = 0, BTN_MOVE, BTN_A, BTN_B, BTN_TOOL, BTN_EMOTE, BTN_CHAT };

static char s_action[16] = "A";
static int s_pointer_was = 0;
static float s_down_x = 0.0f, s_down_y = 0.0f, s_last_x = 0.0f, s_last_y = 0.0f;
static int s_down_kind = BTN_NONE;
static int s_pulse_a = 0;

static int s_have_target = 0;
static float s_target_x = 0.0f;
static float s_target_z = 0.0f;
static int s_pressed = BTN_NONE; /* the button under the mouse or the first finger, for the draw highlight */

static int overlay_allowed(void) {
    if (!pc_touch_enabled() || g_pc_dedicated || g_pc_paused || g_pc_nes_active || g_pc_title_main_menu_visible) return 0;
    if (pc_play_online_menu_active()) return 0;
    if (g_pc_window_w < 2 || g_pc_window_h < 2) return 0;
    return SubmenuArea_IsPlayer() != 0;
}

int pc_touch_enabled(void) {
    if (g_pc_dedicated) return 0;
    if (g_pc_settings.touch_controls == 0) return 0;
    if (g_pc_settings.touch_controls == 1) return 1;
    return SDL_GetNumTouchDevices() > 0;
}

static void button_layout(float* ax, float* ay, float* bx, float* by, float* tx, float* ty,
                          float* ex, float* ey, float* cx, float* cy, float* rad) {
    float short_side = (float)(g_pc_window_w < g_pc_window_h ? g_pc_window_w : g_pc_window_h);
    float margin = 0.06f * short_side; /* safe-area stand-in: keep thumbs off the curved edge */
    float gap;
    *rad = 0.075f * short_side;
    if (*rad < 44.0f) *rad = 44.0f;
    gap = *rad * 2.35f;
    *ax = (float)g_pc_window_w - margin - *rad;
    *ay = (float)g_pc_window_h - margin - *rad;
    *bx = *ax - gap;
    *by = *ay;
    *tx = *ax;
    *ty = *ay - gap;
    *ex = *bx;
    *ey = *ty;
    *cx = *ax;
    *cy = *ty - gap;
}

static int hit_button(float x, float y, float cx, float cy, float rad) {
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

static int classify(float x, float y, float* ox, float* oy) {
    float ax, ay, bx, by, tx, ty, ex, ey, cx, cy, rad;
    button_layout(&ax, &ay, &bx, &by, &tx, &ty, &ex, &ey, &cx, &cy, &rad);
    if (hit_button(x, y, ax, ay, rad)) return BTN_A;
    if (hit_button(x, y, bx, by, rad)) return BTN_B;
    if (hit_button(x, y, tx, ty, rad)) {
        if (ox) { *ox = x - tx; *oy = y - ty; }
        return BTN_TOOL;
    }
    if (hit_button(x, y, ex, ey, rad)) {
        if (ox) { *ox = x - ex; *oy = y - ey; }
        return BTN_EMOTE;
    }
    if (g_pc_settings.touch_keyboard && hit_button(x, y, cx, cy, rad)) return BTN_CHAT;
    return BTN_MOVE;
}

static int message_open(void) {
    mMsg_Window_c* w = mMsg_Get_base_window_p();
    return w != NULL && mMsg_Check_main_hide(w) == FALSE;
}

static int can_steer(void) {
    GAME_PLAY* play;
    if (gamePT == NULL || message_open()) return 0;
    play = (GAME_PLAY*)gamePT;
    if (play->submenu.menu_type != mSM_OVL_NONE || play->submenu.start_refuse) return 0;
    return 1;
}

/* Aim the stick in camera space so the avatar walks toward the world point. */
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
    if (dist < TOUCH_ARRIVE) {
        s_have_target = 0;
        return 0;
    }
    fx = play->camera.lookat.center.x - play->camera.lookat.eye.x;
    fz = play->camera.lookat.center.z - play->camera.lookat.eye.z;
    fl = sqrtf(fx * fx + fz * fz);
    if (fl < 1.0f) return 0;
    fx /= fl;
    fz /= fl;
    rx = fz;
    rz = -fx;
    dx /= dist;
    dz /= dist;
    sx = dx * rx + dz * rz;
    sy = dx * fx + dz * fz;
    mag = sqrtf(sx * sx + sy * sy);
    if (mag < 0.05f) return 0;
    sx = sx / mag * (float)TOUCH_STICK;
    sy = sy / mag * (float)TOUCH_STICK;
    if (sx > 127.0f) sx = 127.0f;
    if (sx < -128.0f) sx = -128.0f;
    if (sy > 127.0f) sy = 127.0f;
    if (sy < -128.0f) sy = -128.0f;
    *stick_x = (signed char)sx;
    *stick_y = (signed char)sy;
    return 1;
}

/* Tap on the ground: ray from the camera through the window pixel, onto the plane at the player's height. */
static int tap_to_world(float wx, float wy, float* out_x, float* out_z) {
    GAME_PLAY* play;
    PLAYER_ACTOR* player;
    float fov, aspect, nx, ny, tan_y, fx, fy, fz, fl, rx, rz, ux, uy, uz, dx, dy, dz, dl, t, py;
    if (gamePT == NULL || g_pc_window_w < 2 || g_pc_window_h < 2) return 0;
    play = (GAME_PLAY*)gamePT;
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return 0;
    fov = play->camera.perspective.fov_y;
    if (fov < 5.0f || fov > 170.0f) fov = 45.0f;
    aspect = play->camera.perspective.aspect_ratio;
    if (aspect < 0.5f || aspect > 4.0f) aspect = (float)g_pc_window_w / (float)g_pc_window_h;
    nx = (wx / (float)g_pc_window_w) * 2.0f - 1.0f;
    ny = 1.0f - (wy / (float)g_pc_window_h) * 2.0f;
    tan_y = tanf(fov * 0.5f * 3.14159265f / 180.0f);
    fx = play->camera.lookat.center.x - play->camera.lookat.eye.x;
    fy = play->camera.lookat.center.y - play->camera.lookat.eye.y;
    fz = play->camera.lookat.center.z - play->camera.lookat.eye.z;
    fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl < 1.0f) return 0;
    fx /= fl;
    fy /= fl;
    fz /= fl;
    /* right = up x forward, up = (0, 1, 0) */
    rx = fz;
    rz = -fx;
    {
        float rl = sqrtf(rx * rx + rz * rz);
        if (rl < 0.01f) return 0;
        rx /= rl;
        rz /= rl;
    }
    ux = fy * rz;
    uy = fz * rx - fx * rz;
    uz = -fy * rx;
    {
        float ul = sqrtf(ux * ux + uy * uy + uz * uz);
        if (ul < 0.01f) return 0;
        ux /= ul;
        uy /= ul;
        uz /= ul;
    }
    dx = fx + rx * (nx * tan_y * aspect) + ux * (ny * tan_y);
    dy = fy + uy * (ny * tan_y);
    dz = fz + rz * (nx * tan_y * aspect) + uz * (ny * tan_y);
    dl = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dl < 0.001f) return 0;
    dx /= dl;
    dy /= dl;
    dz /= dl;
    py = player->actor_class.world.position.y;
    if (fabsf(dy) < 0.02f) return 0;
    t = (py - play->camera.lookat.eye.y) / dy;
    if (t < 1.0f || t > 8000.0f) return 0;
    *out_x = play->camera.lookat.eye.x + dx * t;
    *out_z = play->camera.lookat.eye.z + dz * t;
    return 1;
}

static void begin_move(float x, float y, int run) {
    float wx, wz;
    if (!can_steer()) return;
    if (tap_to_world(x, y, &wx, &wz)) {
        s_target_x = wx;
        s_target_z = wz;
        s_have_target = 1;
        if (run && g_pc_settings.touch_run) s_down_kind = BTN_MOVE;
    }
    (void)run;
}

static int submenu_open(void) {
    GAME_PLAY* play;
    if (gamePT == NULL) return 0;
    play = (GAME_PLAY*)gamePT;
    return play->submenu.menu_type != mSM_OVL_NONE;
}

static void refresh_action_label(void) {
    GAME_PLAY* play;
    PLAYER_ACTOR* player;
    xyz_t ahead;
    mActor_name_t* fg;
    int i;
    snprintf(s_action, sizeof(s_action), "A");
    if (message_open()) {
        snprintf(s_action, sizeof(s_action), "Next");
        return;
    }
    if (submenu_open()) {
        snprintf(s_action, sizeof(s_action), "OK");
        return;
    }
    if (gamePT == NULL || !SubmenuArea_IsPlayer()) return;
    play = (GAME_PLAY*)gamePT;
    player = get_player_actor_withoutCheck(play);
    if (player == NULL) return;
    ahead = player->actor_class.world.position;
    ahead.x += sinf(player->actor_class.shape_info.rotation.y * 3.14159265f / 32768.0f) * 50.0f;
    ahead.z += cosf(player->actor_class.shape_info.rotation.y * 3.14159265f / 32768.0f) * 50.0f;
    fg = mFI_GetUnitFG(ahead);
    if (fg != NULL && *fg != EMPTY_NO) {
        if (IS_ITEM_TREE(*fg)) {
            snprintf(s_action, sizeof(s_action), "Shake");
            return;
        }
        if (ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_STRUCT || ITEM_NAME_GET_TYPE(*fg) == NAME_TYPE_WARP) {
            snprintf(s_action, sizeof(s_action), "Open");
            return;
        }
        if (ITEM_IS_ITEM1(*fg)) {
            snprintf(s_action, sizeof(s_action), "Pick up");
            return;
        }
    }
    if (play->actor_info.list[ACTOR_PART_NPC].actor != NULL) {
        Actor_list* list = &play->actor_info.list[ACTOR_PART_NPC];
        for (i = 0; i < list->num_actors; i++) {
            ACTOR* a = &list->actor[i];
            float dx, dz;
            if (a == NULL) continue;
            dx = a->world.position.x - player->actor_class.world.position.x;
            dz = a->world.position.z - player->actor_class.world.position.z;
            if (dx * dx + dz * dz < 140.0f * 140.0f) {
                snprintf(s_action, sizeof(s_action), "Talk");
                return;
            }
        }
    }
}

static void primary_point(float* x, float* y, float* x2, float* y2, int* n, int* down) {
    int mx = 0, my = 0, i, nf = 0;
    Uint32 mouse = SDL_GetMouseState(&mx, &my);
    *n = 0;
    *down = 0;
    if (SDL_GetNumTouchDevices() > 0) {
        SDL_TouchID id = SDL_GetTouchDevice(0);
        nf = SDL_GetNumTouchFingers(id);
        for (i = 0; i < nf && *n < 2; i++) {
            SDL_Finger* f = SDL_GetTouchFinger(id, i);
            if (f == NULL) continue;
            if (*n == 0) { *x = f->x * (float)g_pc_window_w; *y = f->y * (float)g_pc_window_h; }
            else { *x2 = f->x * (float)g_pc_window_w; *y2 = f->y * (float)g_pc_window_h; }
            (*n)++;
        }
    }
    if (*n == 0 && (mouse & SDL_BUTTON(SDL_BUTTON_LEFT))) {
        *x = (float)mx;
        *y = (float)my;
        *n = 1;
    }
    *down = *n > 0;
    if (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) s_have_target = 0;
}

void pc_touch_apply(unsigned short* buttons, signed char* stick_x, signed char* stick_y,
                    signed char* cstick_x, signed char* cstick_y,
                    int* wheel_hold, int* emote_hold, int* pointer_x, int* pointer_y) {
    float x = 0, y = 0, x2 = 0, y2 = 0, ox = 0, oy = 0, drag, run_px;
    int n = 0, down = 0, kind, rising;
    s_pressed = BTN_NONE;
    refresh_action_label();
    if (!overlay_allowed() || buttons == NULL) return;
    primary_point(&x, &y, &x2, &y2, &n, &down);
    rising = down && !s_pointer_was;
    run_px = 0.12f * (float)(g_pc_window_w < g_pc_window_h ? g_pc_window_w : g_pc_window_h);
    if (s_pulse_a) {
        *buttons |= PAD_BUTTON_A;
        s_pulse_a = 0;
    }
    if (n >= 2 && g_pc_settings.touch_camera) {
        float dx = x2 - x, dy = y2 - y;
        s_have_target = 0;
        if (cstick_x && cstick_y && *cstick_x == 0 && *cstick_y == 0) {
            int sx = (int)(dx * 0.15f);
            int sy = (int)(-dy * 0.15f);
            if (sx > 80) sx = 80;
            if (sx < -80) sx = -80;
            if (sy > 80) sy = 80;
            if (sy < -80) sy = -80;
            *cstick_x = (signed char)sx;
            *cstick_y = (signed char)sy;
        }
        s_pointer_was = down;
        return;
    }
    if (!down) {
        if (s_pointer_was && s_down_kind == BTN_MOVE && g_pc_settings.touch_menus &&
            (message_open() || submenu_open()) &&
            hypotf(s_last_x - s_down_x, s_last_y - s_down_y) < 36.0f) {
            s_pulse_a = 1; /* a short tap advances text or confirms the highlighted menu row */
        }
        s_pointer_was = 0;
        s_down_kind = BTN_NONE;
    } else {
        s_last_x = x;
        s_last_y = y;
        kind = classify(x, y, &ox, &oy);
        if (rising) {
            s_down_x = x;
            s_down_y = y;
            s_down_kind = kind;
            if (kind == BTN_MOVE) begin_move(x, y, 0);
            else s_have_target = 0;
            if (kind == BTN_CHAT) pc_chat_touch_open();
        }
        drag = hypotf(x - s_down_x, y - s_down_y);
        s_pressed = kind;
        if (kind == BTN_A) *buttons |= PAD_BUTTON_A;
        else if (kind == BTN_B) *buttons |= PAD_BUTTON_B;
        else if (kind == BTN_TOOL && wheel_hold) {
            *wheel_hold = 1;
            if (pointer_x && pointer_y) {
                *pointer_x = g_pc_window_w / 2 + (int)ox;
                *pointer_y = g_pc_window_h / 2 + (int)oy;
            }
        } else if (kind == BTN_EMOTE && emote_hold) {
            *emote_hold = 1;
            if (pointer_x && pointer_y) {
                *pointer_x = g_pc_window_w / 2 + (int)ox;
                *pointer_y = g_pc_window_h / 2 + (int)oy;
            }
        } else if (kind == BTN_MOVE && drag > run_px && g_pc_settings.touch_run) {
            *buttons |= PAD_BUTTON_B; /* B is run; a long drag runs toward the point */
            begin_move(x, y, 1);
        }
        /* A tap on an NPC or object uses the same A the label names. */
        if (rising && kind == BTN_MOVE && (strcmp(s_action, "Talk") == 0 || strcmp(s_action, "Pick up") == 0 ||
                                            strcmp(s_action, "Open") == 0 || strcmp(s_action, "Shake") == 0)) {
            float ddx = x - (float)g_pc_window_w * 0.5f;
            float ddy = y - (float)g_pc_window_h * 0.5f;
            if (ddx * ddx + ddy * ddy < run_px * run_px) s_pulse_a = 1;
        }
        s_pointer_was = 1;
    }
    if (s_have_target && stick_x && stick_y) {
        if (*stick_x != 0 || *stick_y != 0) s_have_target = 0;
        else (void)steer_toward(s_target_x, s_target_z, stick_x, stick_y);
    }
}

static void win_to_font(float wx, float wy, float* fx, float* fy) {
    float win = (float)g_pc_window_w / (float)g_pc_window_h;
    float gc = 4.0f / 3.0f;
    float left = 0.0f, right = 320.0f;
    if (win > gc + 0.01f) {
        right = 160.0f + 160.0f * win / gc;
        left = 160.0f - 160.0f * win / gc;
    }
    *fx = left + (wx / (float)g_pc_window_w) * (right - left);
    *fy = (wy / (float)g_pc_window_h) * 240.0f;
}

static void draw_label(struct game_s* game, const char* text, float wx, float wy, int hot) {
    float fx, fy, w, sc = 0.7f;
    win_to_font(wx, wy, &fx, &fy);
    w = (float)pc_text_width(text) * sc;
    pc_text_draw(game, text, fx - w * 0.5f, fy - 6.0f, 0, 0, 0, 160, sc);
    pc_text_draw(game, text, fx - w * 0.5f, fy - 7.0f, hot ? 255 : 230, hot ? 230 : 230, hot ? 120 : 230, 230, sc);
}

void pc_touch_draw(struct game_s* game) {
    float ax, ay, bx, by, tx, ty, ex, ey, cx, cy, rad;
    if (!overlay_allowed() || game == NULL || game->graph == NULL) return;
    button_layout(&ax, &ay, &bx, &by, &tx, &ty, &ex, &ey, &cx, &cy, &rad);
    (void)rad;
    refresh_action_label();
    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
    if (g_pc_settings.touch_keyboard) draw_label(game, "Chat", cx, cy, s_pressed == BTN_CHAT);
    draw_label(game, "Emote", ex, ey, s_pressed == BTN_EMOTE);
    draw_label(game, "Tool", tx, ty, s_pressed == BTN_TOOL);
    draw_label(game, "B", bx, by, s_pressed == BTN_B);
    draw_label(game, s_action, ax, ay, s_pressed == BTN_A);
    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
}
