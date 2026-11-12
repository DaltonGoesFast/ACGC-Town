/* pc_play_offline_menu.h - Title-screen "Play Offline" character selection menu.
 * Lets the player choose an existing resident or start a new character directly,
 * bypassing the villager room dialogue, date/time confirmation, and gossip. */
#ifndef PC_PLAY_OFFLINE_MENU_H
#define PC_PLAY_OFFLINE_MENU_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* Returns 1 if the loaded town save has at least one valid resident. */
int  pc_play_offline_menu_has_characters(void);

/* Opens the offline character select menu. */
void pc_play_offline_menu_enter(void);

/* Returns 1 while the offline character select menu is open. */
int  pc_play_offline_menu_active(void);

/* Navigation handlers (controller stick / D-pad / keys) */
int  pc_play_offline_menu_nav_up(void);
int  pc_play_offline_menu_nav_down(void);

/* Confirm current selection (A / Start / click) */
int  pc_play_offline_menu_confirm(void);

/* Touch / mouse click at font-space coordinates (1 = consumed) */
int  pc_play_offline_menu_pointer(float font_x, float font_y);

/* Cancel / Back (B button) */
int  pc_play_offline_menu_cancel(void);

/* Per-frame tick for preview animations */
void pc_play_offline_menu_tick(void);

/* Draw the menu overlay onto the title screen */
void pc_play_offline_menu_draw(struct game_s* game, int with_dim_backdrop);

/* Closes the menu */
void pc_play_offline_menu_close(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_PLAY_OFFLINE_MENU_H */
