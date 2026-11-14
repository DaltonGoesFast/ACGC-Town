/* pc_touch.h - Pocket Camp-style touch. Original drawing only (soft discs, no Nintendo art).
 *
 * Tap ground to walk (press-drag steers; farther = faster). Tap a named world target to run
 * a semantic TouchIntent: preview → approach → auto-equip → one vanilla request (no A retry).
 * Tap a menu row to select/confirm; tap dialogue to advance. HUD discs (Back / Bag / Map /
 * Action) sit under the picture on a tall screen or float at the edges. Virtual pad is
 * optional. Nothing here is sent on the network — local pad only. */
#ifndef PC_TOUCH_H
#define PC_TOUCH_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

int pc_touch_enabled(void);

void pc_touch_note_event(const SDL_Event* e);
/* Clear all finger/mouse/latch/drag state (pause, resume, resize). reason may be NULL. */
void pc_touch_reset_all(const char* reason);
void pc_touch_pulse_b(void);

/* 1 if a title-menu row was tapped this frame. font_y is in the 240-tall font space. */
int pc_touch_take_title_tap(float* font_y);

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
