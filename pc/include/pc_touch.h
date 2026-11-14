/* pc_touch.h - Pocket Camp-style touch. Original drawing only (soft discs, no Nintendo art).
 *
 * Tap the ground to walk, tap a person or object to walk there and interact, tap a menu
 * row to select and confirm it, tap dialogue to advance. A rounded menu button, a back
 * button, and a contextual action button sit in the spare band under the picture on a
 * tall screen and float at the edges otherwise. The stick and the face buttons are a
 * fallback: hidden while tap-to-move is in use, revealed from a small nub.
 * Nothing here is sent on the network. It only fills the local pad. */
#ifndef PC_TOUCH_H
#define PC_TOUCH_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

int pc_touch_enabled(void);

void pc_touch_note_event(const SDL_Event* e);
void pc_touch_pulse_b(void);

/* 1 if a title-menu row was tapped this frame. font_y is in the 240-tall font space. */
int pc_touch_take_title_tap(float* font_y);

/* 1 while the touch layer is drawing the phone title card. The GameCube title
 * strings stay hidden so they do not sit under the buttons. */
int pc_touch_title_card_active(void);

int pc_touch_finger_count(void);
int pc_touch_hud_mode(void);
void pc_touch_hud_state(char* buf, size_t n);

void pc_touch_apply(unsigned short* buttons, signed char* stick_x, signed char* stick_y,
                    signed char* cstick_x, signed char* cstick_y,
                    int* wheel_hold, int* emote_hold, int* pointer_x, int* pointer_y);

void pc_touch_draw(struct game_s* game);
void pc_touch_present(void);
int pc_touch_sheet_top_y(void);

void pc_touch_show_toast(const char* text);
void pc_touch_show_toast_ex(const char* title, const char* subtitle, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif
