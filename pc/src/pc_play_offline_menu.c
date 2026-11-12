/* pc_play_offline_menu.c - Title-screen "Play Offline" character selection menu.
 * Displays existing residents of the local town with 3D player models,
 * allowing instant loading into town or starting a new character on the train,
 * completely bypassing the slow GameCube villager dialogue sequence. */

#include "pc_play_offline_menu.h"
#include "pc_menu_util.h"
#include "pc_text_draw.h"
#include "pc_player_preview.h"
#include "pc_guest_profile.h"
#include "m_play.h"
#include "m_scene.h"
#include "m_house.h"
#include "m_card.h"
#include "m_common_data.h"
#include "m_bgm.h"
#include "m_actor.h"
#include "m_player_lib.h"
#include "m_event.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int      slot;      /* 0..PLAYER_NUM-1 */
    char     name[16];  /* null-terminated character name */
    uint8_t  gender;
    uint8_t  face;
    uint16_t shirt;
} OfflineResident;

static OfflineResident s_residents[PLAYER_NUM];
static int             s_resident_count = 0;
static int             s_active = 0;
static int             s_launched = 0;
static int             s_sel = 0;

#define CHAR_ROW_H 30.0f
#define CHAR_ROW_Y_START 48.0f

static void scan_residents(void) {
    int i;
    s_resident_count = 0;
    if (mFRm_CheckSaveData() == FALSE) {
        return;
    }

    for (i = 0; i < PLAYER_NUM; i++) {
        Private_c* priv = Save_GetPointer(private_data[i]);
        if (priv->exists == TRUE && mPr_CheckPrivate(priv) == TRUE &&
            mPr_NullCheckPersonalID(&priv->player_ID) == FALSE) {
            OfflineResident* r = &s_residents[s_resident_count];
            char clean[PC_GUEST_PROFILE_NAME_LEN + 1];

            r->slot = i;
            r->gender = (uint8_t)priv->gender;
            r->face = (uint8_t)priv->face;
            r->shirt = (uint16_t)priv->cloth.item;

            if (pc_guest_profile_name_from_game(priv->player_ID.player_name, clean)) {
                snprintf(r->name, sizeof(r->name), "%s", clean);
            } else {
                int len = 0;
                int k;
                for (k = 0; k < PLAYER_NAME_LEN; k++) {
                    char ch = (char)priv->player_ID.player_name[k];
                    if (ch >= 32 && ch <= 126) {
                        r->name[k] = ch;
                        if (ch != ' ') len = k + 1;
                    } else {
                        break;
                    }
                }
                r->name[len] = '\0';
                if (r->name[0] == '\0') {
                    snprintf(r->name, sizeof(r->name), "Resident %d", i + 1);
                }
            }
            s_resident_count++;
        }
    }
}

int pc_play_offline_menu_has_characters(void) {
    scan_residents();
    return s_resident_count > 0;
}

void pc_play_offline_menu_enter(void) {
    scan_residents();
    s_active = 1;
    s_launched = 0;
    s_sel = 0;
    pc_player_preview_reset();
}

int pc_play_offline_menu_active(void) {
    if (s_active && s_launched && Save_Get(scene_no) != SCENE_TITLE_DEMO) {
        s_active = 0;
        s_launched = 0;
    }
    return s_active;
}

void pc_play_offline_menu_close(void) {
    s_active = 0;
    s_launched = 0;
}

static int row_count(void) {
    int count = s_resident_count;
    if (s_resident_count < PLAYER_NUM) {
        count++; /* New character */
    }
    count++;     /* Back */
    return count;
}

int pc_play_offline_menu_nav_up(void) {
    if (s_launched) return 0;
    if (s_sel > 0) {
        s_sel--;
        return 1;
    }
    return 0;
}

int pc_play_offline_menu_nav_down(void) {
    if (s_launched) return 0;
    if (s_sel < row_count() - 1) {
        s_sel++;
        return 1;
    }
    return 0;
}

int pc_play_offline_menu_cancel(void) {
    if (s_launched) return 0;
    pc_play_offline_menu_close();
    return 1;
}

static int start_resident(int slot) {
    GAME_PLAY* play;
    Door_data_c door_data;
    int arrange_idx;
    s32 chan;
    static const s16 homeX[] = { 2128, 2352, 2128, 2352 };
    static const s16 homeZ[] = { 1488, 1488, 1768, 1768 };
    static const u8 drt[] = { mSc_DIRECT_SOUTH_EAST, mSc_DIRECT_SOUTH_WEST, mSc_DIRECT_SOUTH_EAST, mSc_DIRECT_SOUTH_WEST };

    if (gamePT == NULL || gamePT->exec != play_main) return 0;
    play = (GAME_PLAY*)gamePT;
    if (get_player_actor_withoutCheck(play) == NULL) return 0;
    if (play->fb_wipe_mode != WIPE_MODE_NONE) return 0;

    /* Reload save from disk; the title demo may have mutated RAM */
    if (pc_save_loaded) {
        pc_save_reload();
    }
    if (mFRm_CheckSaveData() == FALSE) return 0;
    if (slot < 0 || slot >= PLAYER_NUM) return 0;
    if (Save_Get(private_data[slot]).exists != TRUE) return 0;

    Common_Set(time.rtc_enabled, TRUE);

    /* Initialize resident: mSDI_StartDataInit, pc_save_ready = 1, reset code bookkeeping */
    mCD_InitGameStart_bg(slot, 0, mCD_START_COND_1, &chan);

    arrange_idx = mHS_get_arrange_idx(slot);
    door_data.next_scene_id = SCENE_FG;
    door_data.exit_type = 1;
    door_data.extra_data = 1;
    door_data.exit_position.x = homeX[arrange_idx];
    door_data.exit_position.y = 0;
    door_data.exit_position.z = homeZ[arrange_idx];
    door_data.exit_orientation = drt[arrange_idx];
    door_data.door_actor_name = HOUSE0 + arrange_idx;
    door_data.wipe_type = WIPE_TYPE_FADE_BLACK;

    Common_Get(transition).wipe_type = WIPE_TYPE_TRIFORCE;
    if (Common_Get(reset_flag) == TRUE) {
        Common_Set(demo_profiles[1], mAc_PROFILE_RESET_DEMO);
    }
    Common_Set(submenu_disabled, FALSE);
    mBGMPsComp_make_ps_wipe(0x11C2);

    return goto_other_scene(play, &door_data, TRUE) == 1;
}

static int start_new_character(void) {
    GAME_PLAY* play;
    Door_data_c door_data;
    int slot, free_slot = -1;
    s32 chan;

    if (gamePT == NULL || gamePT->exec != play_main) return 0;
    play = (GAME_PLAY*)gamePT;
    if (get_player_actor_withoutCheck(play) == NULL) return 0;
    if (play->fb_wipe_mode != WIPE_MODE_NONE) return 0;

    if (pc_save_loaded) {
        pc_save_reload();
    }
    if (mFRm_CheckSaveData() == FALSE) return 0;

    for (slot = 0; slot < PLAYER_NUM; slot++) {
        Private_c* priv = Save_GetPointer(private_data[slot]);
        if (priv->exists != TRUE || mPr_NullCheckPersonalID(&priv->player_ID) == TRUE) {
            free_slot = slot;
            break;
        }
    }
    if (free_slot < 0) return 0; /* no free resident slot */

    Common_Set(time.rtc_enabled, TRUE);
    mCD_InitGameStart_bg(free_slot, 0, mCD_START_COND_2, &chan);

    door_data.next_scene_id = SCENE_START_DEMO2;
    door_data.exit_orientation = mSc_DIRECT_NORTH;
    door_data.exit_type = 0;
    door_data.extra_data = 0;
    door_data.exit_position.x = 120;
    door_data.exit_position.y = 0;
    door_data.exit_position.z = 340;
    door_data.door_actor_name = EMPTY_NO;
    door_data.wipe_type = WIPE_TYPE_FADE_BLACK;
    Common_Get(transition).wipe_type = WIPE_TYPE_FADE_BLACK;
    mGH_animal_return_init();

    return goto_other_scene(play, &door_data, TRUE) == 1;
}

int pc_play_offline_menu_confirm(void) {
    if (s_launched) return 1;

    if (s_sel < s_resident_count) {
        /* Selected an existing resident */
        if (start_resident(s_residents[s_sel].slot)) {
            s_launched = 1;
        }
        return 1;
    } else if (s_resident_count < PLAYER_NUM && s_sel == s_resident_count) {
        /* Selected "New character" */
        if (start_new_character()) {
            s_launched = 1;
        }
        return 1;
    } else {
        /* Selected "Back" */
        pc_play_offline_menu_close();
        return 1;
    }
}

int pc_play_offline_menu_pointer(float font_x, float font_y) {
    int i, n;
    (void)font_x;
    if (!s_active || s_launched) return 0;
    n = row_count();
    i = (int)((font_y - CHAR_ROW_Y_START) / CHAR_ROW_H);
    if (i < 0 || i >= n) return 0;
    s_sel = i;
    return pc_play_offline_menu_confirm();
}

void pc_play_offline_menu_tick(void) {
    pc_player_preview_tick();
}

void pc_play_offline_menu_draw(struct game_s* game, int with_dim_backdrop) {
    int i, n;
    if (!s_active) return;

    if (with_dim_backdrop) {
        pc_menu_dim_rect(game->graph, 200);
    }

    pc_menu_draw_centered(game, "- Play Offline -", 26.0f, 255, 255, 255, 255, 1.0f);

    n = row_count();
    for (i = 0; i < n; i++) {
        const int selected = (i == s_sel);
        const f32 y = CHAR_ROW_Y_START + (f32)i * CHAR_ROW_H;
        int r, g, b, a;
        const char* label;

        if (i < s_resident_count) {
            label = s_residents[i].name;
        } else if (s_resident_count < PLAYER_NUM && i == s_resident_count) {
            label = "New character";
        } else {
            label = "Back";
        }

        pc_menu_row_colors(selected, &r, &g, &b, &a);
        pc_menu_draw_centered(game, label, y + 8.0f, r, g, b, a,
                              selected ? PC_MENU_SCALE_SELECTED : 1.0f);

        /* Draw 3D player model preview for character rows */
        if (i < s_resident_count) {
            if (pc_player_preview_set(i, s_residents[i].gender, s_residents[i].face, s_residents[i].shirt)) {
                const f32 w = (f32)pc_text_width(label) * (selected ? PC_MENU_SCALE_SELECTED : 1.0f);
                pc_player_preview_draw(game, i, 160.0f - w * 0.5f - 24.0f, y + 30.0f, 26.0f);
            }
        }
    }

    pc_menu_draw_centered(game, "Select a character to play", 218.0f, 150, 150, 150, 200, 1.0f);
}
