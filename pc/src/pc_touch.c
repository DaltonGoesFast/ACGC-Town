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
#include <dolphin/pad.h>

#include <math.h>
#include <string.h>

#define TOUCH_STICK 80
#define TOUCH_ARRIVE 25.0f

enum { BTN_NONE = 0, BTN_MOVE, BTN_A, BTN_B, BTN_TOOL, BTN_EMOTE };

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

static void button_layout(float* ax, float* ay, float* bx, float* by, float* tx, float* ty, float* ex, float* ey, float* rad) {
    float s = (float)g_pc_window_h / 240.0f;
    if (s < 1.0f) s = 1.0f;
    *rad = 22.0f * s;
    *ax = (float)g_pc_window_w - 36.0f * s;
    *ay = (float)g_pc_window_h - 40.0f * s;
    *bx = (float)g_pc_window_w - 86.0f * s;
    *by = (float)g_pc_window_h - 28.0f * s;
    *tx = (float)g_pc_window_w - 36.0f * s;
    *ty = (float)g_pc_window_h - 96.0f * s;
    *ex = (float)g_pc_window_w - 86.0f * s;
    *ey = (float)g_pc_window_h - 96.0f * s;
}

static int hit_button(float x, float y, float cx, float cy, float rad) {
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

static int classify(float x, float y, float* ox, float* oy) {
    float ax, ay, bx, by, tx, ty, ex, ey, rad;
    button_layout(&ax, &ay, &bx, &by, &tx, &ty, &ex, &ey, &rad);
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

static void begin_move(float x, float y) {
    float wx, wz;
    if (!can_steer()) return;
    if (tap_to_world(x, y, &wx, &wz)) {
        s_target_x = wx;
        s_target_z = wz;
        s_have_target = 1;
    }
}

void pc_touch_apply(unsigned short* buttons, signed char* stick_x, signed char* stick_y,
                    int* wheel_hold, int* emote_hold, int* pointer_x, int* pointer_y) {
    float best_ox = 0.0f, best_oy = 0.0f;
    int kind_down = BTN_NONE;
    int i, ntouch, mx = 0, my = 0;
    Uint32 mouse;
    s_pressed = BTN_NONE;
    if (!overlay_allowed() || buttons == NULL) return;
    mouse = SDL_GetMouseState(&mx, &my);
    if (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        float ox = 0.0f, oy = 0.0f;
        int k = classify((float)mx, (float)my, &ox, &oy);
        kind_down = k;
        if (k == BTN_TOOL || k == BTN_EMOTE) {
            best_ox = ox;
            best_oy = oy;
        }
    } else if (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
        s_have_target = 0;
    }
    ntouch = 0;
    if (SDL_GetNumTouchDevices() > 0) {
        SDL_TouchID id = SDL_GetTouchDevice(0);
        ntouch = SDL_GetNumTouchFingers(id);
        for (i = 0; i < ntouch; i++) {
            SDL_Finger* f = SDL_GetTouchFinger(id, i);
            float ox = 0.0f, oy = 0.0f, x, y;
            int k;
            if (f == NULL) continue;
            x = f->x * (float)g_pc_window_w;
            y = f->y * (float)g_pc_window_h;
            k = classify(x, y, &ox, &oy);
            if (kind_down == BTN_NONE || k != BTN_MOVE) {
                kind_down = k;
                best_ox = ox;
                best_oy = oy;
            }
            if (k == BTN_MOVE && f->pressure > 0.0f) {
                /* a new contact in the world starts a walk; holding still keeps the target */
            }
        }
    }
    /* Rising edges: mouse or the first reported finger. A held finger repeats, so only start a walk when the button class is MOVE and we are not already walking from this hold. */
    {
        static int s_mouse_was = 0;
        static int s_fingers_was = 0;
        int mouse_down = (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        if (mouse_down && !s_mouse_was) {
            float ox, oy;
            int k = classify((float)mx, (float)my, &ox, &oy);
            if (k == BTN_MOVE) begin_move((float)mx, (float)my);
            else s_have_target = 0;
        }
        if (ntouch > 0 && s_fingers_was == 0) {
            SDL_TouchID id = SDL_GetTouchDevice(0);
            SDL_Finger* f = SDL_GetTouchFinger(id, 0);
            if (f != NULL) {
                float x = f->x * (float)g_pc_window_w;
                float y = f->y * (float)g_pc_window_h;
                float ox, oy;
                int k = classify(x, y, &ox, &oy);
                if (k == BTN_MOVE) begin_move(x, y);
                else s_have_target = 0;
            }
        }
        s_mouse_was = mouse_down;
        s_fingers_was = ntouch;
    }
    if (kind_down == BTN_A) {
        *buttons |= PAD_BUTTON_A;
        s_pressed = BTN_A;
        s_have_target = 0;
    } else if (kind_down == BTN_B) {
        *buttons |= PAD_BUTTON_B;
        s_pressed = BTN_B;
        s_have_target = 0;
    } else if (kind_down == BTN_TOOL) {
        *wheel_hold = 1;
        s_pressed = BTN_TOOL;
        s_have_target = 0;
        if (pointer_x && pointer_y) {
            *pointer_x = g_pc_window_w / 2 + (int)best_ox;
            *pointer_y = g_pc_window_h / 2 + (int)best_oy;
        }
    } else if (kind_down == BTN_EMOTE) {
        *emote_hold = 1;
        s_pressed = BTN_EMOTE;
        s_have_target = 0;
        if (pointer_x && pointer_y) {
            *pointer_x = g_pc_window_w / 2 + (int)best_ox;
            *pointer_y = g_pc_window_h / 2 + (int)best_oy;
        }
    }
    if (s_have_target) {
        if (stick_x && stick_y && (*stick_x != 0 || *stick_y != 0)) {
            s_have_target = 0; /* the keyboard or a pad is steering */
        } else if (stick_x && stick_y) {
            (void)steer_toward(s_target_x, s_target_z, stick_x, stick_y);
        }
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
    float ax, ay, bx, by, tx, ty, ex, ey, rad;
    if (!overlay_allowed() || game == NULL || game->graph == NULL) return;
    button_layout(&ax, &ay, &bx, &by, &tx, &ty, &ex, &ey, &rad);
    (void)rad;
    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
    draw_label(game, "Emote", ex, ey, s_pressed == BTN_EMOTE);
    draw_label(game, "Tool", tx, ty, s_pressed == BTN_TOOL);
    draw_label(game, "B", bx, by, s_pressed == BTN_B);
    draw_label(game, "A", ax, ay, s_pressed == BTN_A);
    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
}
