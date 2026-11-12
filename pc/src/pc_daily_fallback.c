/* pc_daily_fallback.c - Client-Guaranteed Daily Resource Fallbacks (Phase 4). */
#include "pc_daily_fallback.h"

#include "m_common_data.h"
#include "m_field_info.h"
#include "m_name_table.h"
#include "m_collision_bg.h"
#include "pc_field_authority.h"
#include "pc_net_game.h"
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

typedef struct PCDailyFallbackDisk {
    char     magic[8];
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  fruit_granted;
    uint8_t  fossil_granted;
    uint8_t  rsv[2];
} PCDailyFallbackDisk;

static void fallback_ensure_dir(const char* dir) {
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

static void fallback_path(char* out, size_t cap) {
    const char* uuid = (pc_session()->storage == PC_CHARACTER_STORAGE_STORE) ?
                       pc_session()->character.uuid : "local_resident";
    snprintf(out, cap, "save/mp/characters/%s/daily_fallback.dat", uuid);
}

static void fallback_load(uint16_t year, uint8_t month, uint8_t day, PCDailyFallbackDisk* out) {
    char path[260];
    FILE* f;
    memset(out, 0, sizeof(*out));
    memcpy(out->magic, "ACDFB1\0", 8);
    out->year = year;
    out->month = month;
    out->day = day;

    fallback_path(path, sizeof(path));
    f = fopen(path, "rb");
    if (f != NULL) {
        PCDailyFallbackDisk disk;
        if (fread(&disk, sizeof(disk), 1, f) == 1 &&
            memcmp(disk.magic, "ACDFB1\0", 8) == 0 &&
            disk.year == year && disk.month == month && disk.day == day) {
            *out = disk;
        }
        fclose(f);
    }
}

static void fallback_save(const PCDailyFallbackDisk* rec) {
    char path[260], parent[260];
    const char* uuid = (pc_session()->storage == PC_CHARACTER_STORAGE_STORE) ?
                       pc_session()->character.uuid : "local_resident";
    FILE* f;
    snprintf(parent, sizeof(parent), "save/mp/characters/%s", uuid);
    fallback_ensure_dir(parent);
    fallback_path(path, sizeof(path));
    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(rec, sizeof(*rec), 1, f);
        fclose(f);
    }
}

static int is_fruit_bearing_tree(mActor_name_t item) {
    return (item == TREE_APPLE_FRUIT || item == TREE_ORANGE_FRUIT || item == TREE_PEACH_FRUIT ||
            item == TREE_PEAR_FRUIT || item == TREE_CHERRY_FRUIT || item == TREE_PALM_FRUIT);
}

static mActor_name_t promote_nofruit_to_fruit(mActor_name_t item) {
    if (item >= TREE_APPLE_NOFRUIT_0 && item <= TREE_APPLE_NOFRUIT_2) return TREE_APPLE_FRUIT;
    if (item >= TREE_ORANGE_NOFRUIT_0 && item <= TREE_ORANGE_NOFRUIT_2) return TREE_ORANGE_FRUIT;
    if (item >= TREE_PEACH_NOFRUIT_0 && item <= TREE_PEACH_NOFRUIT_2) return TREE_PEACH_FRUIT;
    if (item >= TREE_PEAR_NOFRUIT_0 && item <= TREE_PEAR_NOFRUIT_2) return TREE_PEAR_FRUIT;
    if (item >= TREE_CHERRY_NOFRUIT_0 && item <= TREE_CHERRY_NOFRUIT_2) return TREE_CHERRY_FRUIT;
    if (item >= TREE_PALM_NOFRUIT_0 && item <= TREE_PALM_NOFRUIT_2) return TREE_PALM_FRUIT;
    return EMPTY_NO;
}

void pc_daily_fallback_apply_if_needed(void) {
    lbRTC_time_c* rtc = Common_GetPointer(time.rtc_time);
    PCDailyFallbackDisk rec;
    int az, ax, t;
    int fruit_count = 0, fossil_count = 0;
    int dirty = 0;

    if (rtc == NULL || Save_Get(scene_no) != SCENE_FG) {
        return;
    }
    /* Host-observer doesn't need personal fallbacks */
    if (pc_net_game_role() == PC_NETGAME_ROLE_HOST) {
        return;
    }

    fallback_load(rtc->year, rtc->month, rtc->day, &rec);
    if (rec.fruit_granted && rec.fossil_granted) {
        return;
    }

    for (az = 0; az < PCFA_ACRE_Z_NUM; az++) {
        for (ax = 0; ax < PCFA_ACRE_X_NUM; ax++) {
            int acre = az * PCFA_ACRE_X_NUM + ax;
            mActor_name_t* items = Save_Get(fg[az][ax]).items[0];
            u16* dep = Save_Get(deposit[acre]);
            for (t = 0; t < UT_TOTAL_NUM; t++) {
                int uz = t / UT_X_NUM;
                int ux = t % UT_X_NUM;
                if (is_fruit_bearing_tree(items[t])) {
                    fruit_count++;
                }
                if (items[t] == ITM_FOSSIL && ((dep[uz] >> ux) & 1u)) {
                    fossil_count++;
                }
            }
        }
    }

    /* 1. Guaranteed Private Daily Fruit Tree */
    if (!rec.fruit_granted) {
        if (fruit_count == 0) {
            for (az = 0; az < PCFA_ACRE_Z_NUM && !rec.fruit_granted; az++) {
                for (ax = 0; ax < PCFA_ACRE_X_NUM && !rec.fruit_granted; ax++) {
                    mActor_name_t* items = Save_Get(fg[az][ax]).items[0];
                    for (t = 0; t < UT_TOTAL_NUM; t++) {
                        mActor_name_t promoted = promote_nofruit_to_fruit(items[t]);
                        if (promoted != EMPTY_NO) {
                            items[t] = promoted;
                            rec.fruit_granted = 1;
                            dirty = 1;
                            printf("[ECOLOGY] Daily fallback: promoted private fruit tree at acre (%d,%d) tile %d\n",
                                   ax + 1, az + 1, t);
                            break;
                        }
                    }
                }
            }
        } else {
            rec.fruit_granted = 1;
            dirty = 1;
        }
    }

    /* 2. Guaranteed Private Daily Buried Fossil */
    if (!rec.fossil_granted) {
        if (fossil_count == 0) {
            /* Pick a safe middle acre (e.g., B-2..D-4) and an empty walkable tile */
            for (az = 1; az < PCFA_ACRE_Z_NUM - 1 && !rec.fossil_granted; az++) {
                for (ax = 1; ax < PCFA_ACRE_X_NUM - 1 && !rec.fossil_granted; ax++) {
                    int acre = az * PCFA_ACRE_X_NUM + ax;
                    mActor_name_t* items = Save_Get(fg[az][ax]).items[0];
                    u16* dep = Save_Get(deposit[acre]);
                    mCoBG_Collision_u* col = mFI_GetBkNum2ColTop(ax + 1, az + 1);
                    if (col == NULL) continue;
                    for (t = 34; t < UT_TOTAL_NUM - 34; t++) {
                        int uz = t / UT_X_NUM;
                        int ux = t % UT_X_NUM;
                        if (ux < 2 || ux > UT_X_NUM - 3) continue;
                        if (items[t] == EMPTY_NO && ((dep[uz] >> ux) & 1u) == 0 &&
                            col[t].data.unit_attribute == mCoBG_ATTRIBUTE_NONE) {
                            items[t] = ITM_FOSSIL;
                            dep[uz] |= (uint16_t)(1u << ux);
                            rec.fossil_granted = 1;
                            dirty = 1;
                            printf("[ECOLOGY] Daily fallback: buried private fossil at acre (%d,%d) (%d,%d)\n",
                                   ax + 1, az + 1, ux, uz);
                            break;
                        }
                    }
                }
            }
        } else {
            rec.fossil_granted = 1;
            dirty = 1;
        }
    }

    if (dirty) {
        fallback_save(&rec);
    }
}
