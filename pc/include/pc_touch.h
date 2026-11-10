/* pc_touch.h - first-pass Pocket Camp touch overlay (desktop now, the same calls for Android and the browser later).
 *
 * settings.ini touch_controls = auto | on | off. auto is on only when SDL reports a touch device.
 * When the overlay is on, a tap or a left click that is not on a button steers the stick toward that
 * point (the same path), and the on-screen A, B, Tool, and Emote buttons are held while the finger
 * or the mouse is on them. Nothing here is sent on the network: it only fills the local pad. */
#ifndef PC_TOUCH_H
#define PC_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* 1 when the overlay should read input and be drawn. */
int pc_touch_enabled(void);

/* Merge touch and mouse into the pad sample. pointer_x/y are the mouse position in window pixels;
 * a finger that is holding Tool or Emote replaces them so the existing wheel aims from that finger. */
void pc_touch_apply(unsigned short* buttons, signed char* stick_x, signed char* stick_y,
                    int* wheel_hold, int* emote_hold, int* pointer_x, int* pointer_y);

void pc_touch_draw(struct game_s* game);

#ifdef __cplusplus
}
#endif

#endif
