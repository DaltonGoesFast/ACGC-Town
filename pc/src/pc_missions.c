/* pc_missions.c - Daily Villager Missions (Phase 4). */
#include "pc_missions.h"

#include "m_common_data.h"
#include "m_name_table.h"
#include "m_npc.h"
#include "m_private.h"
#include "pc_net_game.h"
#include "pc_pause_menu.h"
#include "pc_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(d) _mkdir(d)
#else
#include <unistd.h>
#define MKDIR(d) mkdir((d), 0755)
#endif

static PCMissionState s_missions;
static uint8_t s_pending_turn_in_mask = 0;

static uint32_t missions_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void clean_npc_ascii(const u8* src, int max_src, char* dst, int max_dst) {
    int i, len = 0;
    if (dst == NULL || max_dst <= 0) return;
    dst[0] = '\0';
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
    dst[len] = '\0';
}

static int is_fruit_item(mActor_name_t item) {
    return (item == ITM_FOOD_APPLE || item == ITM_FOOD_CHERRY || item == ITM_FOOD_PEAR ||
            item == ITM_FOOD_PEACH || item == ITM_FOOD_ORANGE || item == ITM_FOOD_COCONUT);
}

static const char* fruit_name(mActor_name_t item) {
    switch (item) {
        case ITM_FOOD_APPLE:   return "Apple";
        case ITM_FOOD_CHERRY:  return "Cherry";
        case ITM_FOOD_PEAR:    return "Pear";
        case ITM_FOOD_PEACH:   return "Peach";
        case ITM_FOOD_ORANGE:  return "Orange";
        case ITM_FOOD_COCONUT: return "Coconut";
        default:               return "Fruit";
    }
}

void pc_missions_init_or_roll(uint16_t year, uint8_t month, uint8_t day) {
    uint32_t seed;
    int villager_indices[ANIMAL_NUM_MAX];
    int vcount = 0;
    int i, count;
    static const mActor_name_t fruits[5] = {
        ITM_FOOD_APPLE, ITM_FOOD_CHERRY, ITM_FOOD_PEAR, ITM_FOOD_PEACH, ITM_FOOD_ORANGE
    };
    static const mActor_name_t bonus_items[4] = {
        ITM_SAPLING, ITM_WHITE_PANSY_BAG, ITM_RED_TULIP_BAG, ITM_BLUE_COSMOS_BAG
    };

    if (s_missions.initialized &&
        s_missions.year == year && s_missions.month == month && s_missions.day == day) {
        return;
    }

    memset(&s_missions, 0, sizeof(s_missions));
    s_missions.year = year;
    s_missions.month = month;
    s_missions.day = day;
    s_missions.initialized = 1;

    seed = missions_hash(((uint32_t)year << 16) | ((uint32_t)month << 8) | (uint32_t)day ^ 0x41434743u);

    /* Collect active villagers in town */
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Animal_c* a = &Save_Get(animals)[i];
        if (mNpc_CheckFreeAnimalInfo(a) == FALSE && a->id.npc_id != EMPTY_NO) {
            villager_indices[vcount++] = i;
        }
    }

    /* Shuffle villager indices deterministically */
    for (i = vcount - 1; i > 0; i--) {
        seed = missions_hash(seed + (uint32_t)i);
        int j = (int)(seed % (uint32_t)(i + 1));
        int tmp = villager_indices[i];
        villager_indices[i] = villager_indices[j];
        villager_indices[j] = tmp;
    }

    count = 3 + (int)(seed % 3u); /* 3 to 5 quests per day */
    if (vcount > 0 && count > vcount) {
        count = vcount;
    }
    if (count < 1) count = 1;
    if (count > PC_MISSIONS_MAX) count = PC_MISSIONS_MAX;
    s_missions.count = (uint8_t)count;

    for (i = 0; i < count; i++) {
        PCMissionDef* d = &s_missions.defs[i];
        uint32_t h = missions_hash(seed ^ (uint32_t)(i * 0x9e3779b9u));
        int qtype;

        d->slot = (uint8_t)i;
        if (vcount > 0) {
            Animal_c* a = &Save_Get(animals)[villager_indices[i % vcount]];
            u8 raw_name[16];
            memset(raw_name, 0, sizeof(raw_name));
            mNpc_GetNpcWorldNameTableNo(raw_name, a->id.npc_id);
            d->villager_npc_id = (uint16_t)a->id.npc_id;
            clean_npc_ascii(raw_name, 10, d->villager_name, sizeof(d->villager_name));
            if (d->villager_name[0] == '\0') {
                snprintf(d->villager_name, sizeof(d->villager_name), "Villager");
            }
        } else {
            d->villager_npc_id = 0;
            snprintf(d->villager_name, sizeof(d->villager_name), "Villager");
        }

        /* Rotate quest types across slots so every day has variety */
        qtype = (i + (int)(h % 2u)) % 4;
        d->type = (uint8_t)qtype;
        d->acre_x = (uint8_t)(1 + (h % 5u));
        d->acre_z = (uint8_t)(1 + ((h >> 4) % 6u));
        d->reward_bells = 1500u + (uint32_t)((h >> 8) % 4u) * 500u; /* 1500..3000 Bells */

        if ((h & 3u) == 0u) {
            d->reward_kind = 1;
            if ((h & 4u) == 0u) {
                d->reward_item = (uint16_t)TICKET_GET_ITEM(month > 0 ? month : 1, (int)(h % 8u));
            } else {
                d->reward_item = (uint16_t)bonus_items[(h >> 3) % 4u];
            }
        } else {
            d->reward_kind = 0;
            d->reward_item = EMPTY_NO;
        }

        switch (qtype) {
            case PC_MISSION_TYPE_FISH:
                d->target_count = (uint8_t)(3 + (h % 3u)); /* 3..5 */
                d->target_item = EMPTY_NO;
                snprintf(d->title, sizeof(d->title), "Fresh River Fish");
                break;
            case PC_MISSION_TYPE_BUG:
                d->target_count = (uint8_t)(2 + (h % 3u)); /* 2..4 */
                d->target_item = EMPTY_NO;
                snprintf(d->title, sizeof(d->title), "Town Bug Hunt");
                break;
            case PC_MISSION_TYPE_FRUIT: {
                mActor_name_t f = fruits[(h >> 5) % 5u];
                d->target_count = (uint8_t)(1 + (h % 2u)); /* 1..2 */
                d->target_item = (uint16_t)f;
                snprintf(d->title, sizeof(d->title), "%s Craving", fruit_name(f));
                break;
            }
            case PC_MISSION_TYPE_FOSSIL:
            default:
                d->type = PC_MISSION_TYPE_FOSSIL;
                d->target_count = 1;
                d->target_item = ITM_FOSSIL;
                snprintf(d->title, sizeof(d->title), "Fossil Discovery");
                break;
        }
    }

    /* Load local/solo completion mask if in solo mode */
    if (pc_net_game_role() == PC_NETGAME_ROLE_NONE) {
        const char* uuid = (pc_session()->storage == PC_CHARACTER_STORAGE_STORE) ?
                           pc_session()->character.uuid : "local_resident";
        uint8_t mask = 0;
        if (pc_missions_store_load_mask(NULL, uuid, year, month, day, &mask)) {
            s_missions.completed_mask = mask;
        }
    }
}

void pc_missions_ensure_local_today(void) {
    lbRTC_time_c* rtc = Common_GetPointer(time.rtc_time);
    if (rtc == NULL) return;
    if (pc_net_game_role() == PC_NETGAME_ROLE_CLIENT && s_missions.initialized) {
        return; /* Client uses host's authoritative synced missions */
    }
    pc_missions_init_or_roll(rtc->year, rtc->month, rtc->day);
}

const PCMissionState* pc_missions_get_state(void) {
    pc_missions_ensure_local_today();
    return &s_missions;
}

void pc_missions_note_fossil_dug(void) {
    s_missions.fossil_dug_today = 1;
}

int pc_missions_get_progress(int slot, int* out_cur, int* out_target) {
    const PCMissionDef* d;
    Private_c* priv = Now_Private;
    int cur = 0, target = 1, i;

    pc_missions_ensure_local_today();
    if (slot < 0 || slot >= (int)s_missions.count) {
        if (out_cur) *out_cur = 0;
        if (out_target) *out_target = 1;
        return 0;
    }

    d = &s_missions.defs[slot];
    target = (int)d->target_count;
    if (target < 1) target = 1;

    if (s_missions.completed_mask & (1u << slot)) {
        if (out_cur) *out_cur = target;
        if (out_target) *out_target = target;
        return 0;
    }

    if (priv != NULL) {
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
            mActor_name_t it = priv->inventory.pockets[i];
            if (it == EMPTY_NO || it == 0) continue;
            switch (d->type) {
                case PC_MISSION_TYPE_FISH:
                    if (ITEM_IS_FISH(it)) cur++;
                    break;
                case PC_MISSION_TYPE_BUG:
                    if (ITEM_IS_INSECT(it)) cur++;
                    break;
                case PC_MISSION_TYPE_FRUIT:
                    if (d->target_item != EMPTY_NO) {
                        if (it == (mActor_name_t)d->target_item) cur++;
                    } else if (is_fruit_item(it)) {
                        cur++;
                    }
                    break;
                case PC_MISSION_TYPE_FOSSIL:
                    if (it == ITM_FOSSIL) cur++;
                    break;
                default:
                    break;
            }
        }
    }

    if (d->type == PC_MISSION_TYPE_FOSSIL && s_missions.fossil_dug_today) {
        cur = 1;
    }

    if (cur > target) cur = target;
    if (out_cur) *out_cur = cur;
    if (out_target) *out_target = target;
    return (cur >= target) ? 1 : 0;
}

int pc_missions_has_ready_for_villager(uint16_t npc_id) {
    int i;
    if (npc_id == 0 || npc_id == EMPTY_NO) return -1;
    pc_missions_ensure_local_today();
    for (i = 0; i < (int)s_missions.count; i++) {
        if ((s_pending_turn_in_mask & (1u << i)) != 0) continue;
        if (s_missions.defs[i].villager_npc_id == npc_id) {
            if (pc_missions_get_progress(i, NULL, NULL)) {
                return i;
            }
        }
    }
    return -1;
}

static void consume_items_for_mission(const PCMissionDef* d) {
    Private_c* priv = Now_Private;
    int need = (int)d->target_count;
    int i;
    if (priv == NULL || d->type == PC_MISSION_TYPE_FOSSIL) {
        return; /* Fossil mission only checks discovery, doesn't take the fossil */
    }

    for (i = 0; i < mPr_POCKETS_SLOT_COUNT && need > 0; i++) {
        mActor_name_t it = priv->inventory.pockets[i];
        int match = 0;
        if (it == EMPTY_NO || it == 0) continue;
        if (d->type == PC_MISSION_TYPE_FISH && ITEM_IS_FISH(it)) match = 1;
        else if (d->type == PC_MISSION_TYPE_BUG && ITEM_IS_INSECT(it)) match = 1;
        else if (d->type == PC_MISSION_TYPE_FRUIT) {
            if (d->target_item != EMPTY_NO) match = (it == (mActor_name_t)d->target_item);
            else match = is_fruit_item(it);
        }
        if (match) {
            mPr_SetPossessionItem(priv, i, EMPTY_NO, mPr_ITEM_COND_NORMAL);
            need--;
        }
    }
}

void pc_missions_grant_reward_local(int slot) {
    Private_c* priv = Now_Private;
    const PCMissionDef* d;
    if (slot < 0 || slot >= (int)s_missions.count || priv == NULL) return;
    d = &s_missions.defs[slot];

    priv->inventory.wallet += d->reward_bells;
    if (d->reward_kind != 0 && d->reward_item != EMPTY_NO && d->reward_item != 0) {
        int free_idx = mPr_GetPossessionItemIdx(priv, EMPTY_NO);
        if (free_idx >= 0 && free_idx < mPr_POCKETS_SLOT_COUNT) {
            mPr_SetPossessionItem(priv, free_idx, (mActor_name_t)d->reward_item, mPr_ITEM_COND_NORMAL);
        }
    }
}

int pc_missions_try_turn_in(int slot) {
    const PCMissionDef* d;
    if (slot < 0 || slot >= (int)s_missions.count) {
        return 0;
    }
    if ((s_pending_turn_in_mask & (1u << slot)) != 0) {
        return 0;
    }
    if (!pc_missions_get_progress(slot, NULL, NULL)) {
        return 0;
    }
    d = &s_missions.defs[slot];

    /* Consume items immediately on physical delivery */
    consume_items_for_mission(d);

    if (pc_net_game_role() == PC_NETGAME_ROLE_CLIENT) {
        s_pending_turn_in_mask |= (uint8_t)(1u << slot);
        pc_net_game_missions_send_turn_in(slot, d->villager_npc_id);
        return 1;
    }

    /* Solo or Host local turn-in */
    s_missions.completed_mask |= (uint8_t)(1u << slot);
    pc_missions_grant_reward_local(slot);

    {
        const char* uuid = (pc_session()->storage == PC_CHARACTER_STORAGE_STORE) ?
                           pc_session()->character.uuid : "local_resident";
        pc_missions_store_save_mask(NULL, uuid, s_missions.year, s_missions.month, s_missions.day,
                                    s_missions.completed_mask);
    }
    return 1;
}

void pc_missions_build_sync_msg(PCMissionMsg* out, uint8_t completed_mask) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    out->msg_type = (uint8_t)PC_MISSIONS_MSG_ID;
    out->op = (uint8_t)PC_MISSION_OP_SYNC;
    out->ok = 1;
    out->year = s_missions.year;
    out->month = s_missions.month;
    out->day = s_missions.day;
    out->count = s_missions.count;
    out->completed_mask = completed_mask;
    memcpy(out->defs, s_missions.defs, sizeof(out->defs));
}

void pc_missions_apply_sync_msg(const PCMissionMsg* msg) {
    if (msg == NULL || msg->msg_type != (uint8_t)PC_MISSIONS_MSG_ID) return;
    if (msg->op == PC_MISSION_OP_SYNC) {
        int same_day = (s_missions.initialized &&
                        s_missions.year == msg->year &&
                        s_missions.month == msg->month &&
                        s_missions.day == msg->day);
        if (!same_day) {
            s_missions.fossil_dug_today = 0;
        }
        s_missions.year = msg->year;
        s_missions.month = msg->month;
        s_missions.day = msg->day;
        s_missions.count = msg->count > PC_MISSIONS_MAX ? PC_MISSIONS_MAX : msg->count;
        s_missions.completed_mask = msg->completed_mask;
        s_missions.initialized = 1;
        s_pending_turn_in_mask = 0;
        memcpy(s_missions.defs, msg->defs, sizeof(s_missions.defs));
    } else if (msg->op == PC_MISSION_OP_TURN_IN_ACK) {
        if (msg->slot < PC_MISSIONS_MAX) {
            s_pending_turn_in_mask &= (uint8_t)~(1u << msg->slot);
        }
        if (msg->ok && msg->slot < s_missions.count) {
            s_missions.completed_mask = msg->completed_mask;
            pc_missions_grant_reward_local((int)msg->slot);
        }
    }
}

static void ensure_dir(const char* dir) {
    char tmp[260];
    size_t len, i;
    if (dir == NULL || dir[0] == '\0') return;
    len = strlen(dir);
    if (len >= sizeof(tmp) - 1) return;
    memcpy(tmp, dir, len + 1);
    for (i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            tmp[i] = '\0';
            MKDIR(tmp);
            tmp[i] = c;
        }
    }
    MKDIR(tmp);
}

static void missions_store_path(const char* dir, const char* uuid, char* out, size_t cap) {
    const char* d = (dir != NULL && dir[0] != '\0') ? dir : "save/mp";
    const char* u = (uuid != NULL && uuid[0] != '\0') ? uuid : "local_resident";
    snprintf(out, cap, "%s/characters/%s/missions.dat", d, u);
}

typedef struct PCMissionStoreDisk {
    char     magic[8];
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  mask;
    uint8_t  rsv[3];
} PCMissionStoreDisk;

int pc_missions_store_load_mask(const char* dir, const char* uuid,
                                uint16_t year, uint8_t month, uint8_t day,
                                uint8_t* out_mask) {
    char path[260];
    FILE* f;
    PCMissionStoreDisk rec;

    if (out_mask) *out_mask = 0;
    missions_store_path(dir, uuid, path, sizeof(path));
    f = fopen(path, "rb");
    if (f == NULL) return 0;
    if (fread(&rec, sizeof(rec), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    fclose(f);
    if (memcmp(rec.magic, "ACMSN1\0", 8) != 0) return 0;
    if (rec.year != year || rec.month != month || rec.day != day) return 0;
    if (out_mask) *out_mask = rec.mask;
    return 1;
}

int pc_missions_store_save_mask(const char* dir, const char* uuid,
                                uint16_t year, uint8_t month, uint8_t day,
                                uint8_t mask) {
    char path[260], parent[260];
    const char* d = (dir != NULL && dir[0] != '\0') ? dir : "save/mp";
    const char* u = (uuid != NULL && uuid[0] != '\0') ? uuid : "local_resident";
    FILE* f;
    PCMissionStoreDisk rec;

    snprintf(parent, sizeof(parent), "%s/characters/%s", d, u);
    ensure_dir(parent);
    missions_store_path(dir, uuid, path, sizeof(path));

    memset(&rec, 0, sizeof(rec));
    memcpy(rec.magic, "ACMSN1\0", 8);
    rec.year = year;
    rec.month = month;
    rec.day = day;
    rec.mask = mask;

    f = fopen(path, "wb");
    if (f == NULL) return 0;
    fwrite(&rec, sizeof(rec), 1, f);
    fclose(f);
    return 1;
}
