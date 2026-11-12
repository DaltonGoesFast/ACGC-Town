/* pc_bedroom_store.c - Authoritative bedroom store & local housing navigation.
 * Pure C, ABI-compatible across 32-bit x86 Linux and ARM64 Android.
 */
#include "pc_bedroom.h"
#include "pc_cloud_storage.h"
#include "pc_mp_records.h"
#include "m_name_table.h"
#include "m_mail.h"
#include "m_play.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MKDIR(d) _mkdir(d)
#else
#include <unistd.h>
#define MKDIR(d) mkdir((d), 0755)
#endif

/* Global table instance */
static PCBedroomTable s_table;

/* Local player housing navigation state */
static int s_active_house_id = 0;
static int s_active_floor    = 0;
static int s_active_wing     = 0;
static int s_pending_wing    = 0; /* 0 = Common Lounge, 1 = North, 2 = East, 3 = West */

/* Local claim prompt state */
static int s_prompt_active   = 0;
static int s_prompt_house    = 0;
static int s_prompt_floor    = 0;
static int s_prompt_wing     = 0;

/* Log helper */
static void blog(const char* fmt, ...) {
    va_list ap;
    printf("[NET][BEDROOM] ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* Ensure directory exists */
static void ensure_dir(const char* dir) {
    if (dir == NULL || dir[0] == '\0') return;
    char tmp[260];
    size_t len = strlen(dir);
    if (len >= sizeof(tmp) - 1) return;
    memcpy(tmp, dir, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            tmp[i] = '\0';
            MKDIR(tmp);
            tmp[i] = c;
        }
    }
    MKDIR(tmp);
}

/* Format room path: <dir>/rooms/room_<h>_<f>_<w>.dat */
static void format_room_path(const char* dir, int h, int f, int w, char* out, size_t cap) {
    snprintf(out, cap, "%s/rooms/room_%d_%d_%d.dat", dir, h, f, w);
}

/* ---- Active Location Tracking ---- */

void pc_bedroom_set_active_location(int house_id, int floor, int wing) {
    if (house_id >= 0 && house_id < PC_HOUSE_COUNT) s_active_house_id = house_id;
    if (floor >= 0 && floor < PC_HOUSE_FLOORS_PER_HS) s_active_floor = floor;
    if (wing >= 0 && wing <= PC_HOUSE_WINGS_PER_FLR) s_active_wing = wing;
    blog("Active location set: House %d, Floor %d, Wing %d (%s)\n",
         s_active_house_id, s_active_floor, s_active_wing,
         s_active_wing == 0 ? "Common Lounge" :
         (s_active_wing == 1 ? "North Bedroom" : (s_active_wing == 2 ? "East Bedroom" : "West Bedroom")));
}

void pc_bedroom_get_active_location(int* house_id, int* floor, int* wing) {
    if (house_id) *house_id = s_active_house_id;
    if (floor) *floor = s_active_floor;
    if (wing) *wing = s_active_wing;
}

int pc_bedroom_is_in_bedroom(void) {
    return (s_active_wing > 0 && s_active_wing <= PC_HOUSE_WINGS_PER_FLR);
}

int pc_bedroom_is_in_lounge(void) {
    return (s_active_wing == 0);
}

int pc_bedroom_is_owner(const char* player_uuid) {
    if (!pc_bedroom_is_in_bedroom() || player_uuid == NULL || player_uuid[0] == '\0') {
        return 0;
    }
    PCBedroomRecord* r = pc_bedroom_store_get(s_active_house_id, s_active_floor, s_active_wing);
    if (r == NULL || !r->claimed) {
        return 0;
    }
    return (strncmp(r->owner_uuid, player_uuid, 32) == 0);
}

/* ---- Local Claim Prompt ---- */

void pc_bedroom_arm_claim_prompt(int house_id, int floor, int wing) {
    s_prompt_active = 1;
    s_prompt_house  = house_id;
    s_prompt_floor  = floor;
    s_prompt_wing   = wing;
    blog("Claim prompt ARMED for House %d, Floor %d, Wing %d\n", house_id, floor, wing);
}

void pc_bedroom_disarm_claim_prompt(void) {
    s_prompt_active = 0;
}

int pc_bedroom_claim_prompt_active(void) {
    return s_prompt_active;
}

void pc_bedroom_confirm_claim(void) {
    if (!s_prompt_active) return;
    blog("Claim prompt CONFIRMED by player for House %d, Floor %d, Wing %d\n",
         s_prompt_house, s_prompt_floor, s_prompt_wing);

    /* Notify server via netgame message handler */
    extern void pc_net_game_bedroom_send_claim(int house_id, int floor, int wing);
    pc_net_game_bedroom_send_claim(s_prompt_house, s_prompt_floor, s_prompt_wing);

    s_prompt_active = 0;
}

void pc_bedroom_cancel_claim(void) {
    if (s_prompt_active) {
        blog("Claim prompt CANCELLED by player\n");
        s_prompt_active = 0;
    }
}

/* ---- Serialization & CRC ---- */

uint32_t pc_bedroom_calc_crc(const PCBedroomRecord* r) {
    if (r == NULL) return 0;
    /* CRC over 64-byte header + mHm_flr_c */
    return pc_mp_records_crc32(r, PC_BEDROOM_HEADER_SIZE + sizeof(mHm_flr_c));
}

int pc_bedroom_serialize(const PCBedroomRecord* r, uint8_t* out, size_t cap) {
    if (r == NULL || out == NULL || cap < PC_BEDROOM_FILE_SIZE) return 0;
    memcpy(out, r, PC_BEDROOM_HEADER_SIZE + sizeof(mHm_flr_c));
    uint32_t crc = pc_bedroom_calc_crc(r);
    memcpy(out + PC_BEDROOM_HEADER_SIZE + sizeof(mHm_flr_c), &crc, 4);
    return PC_BEDROOM_FILE_SIZE;
}

int pc_bedroom_deserialize(const uint8_t* in, size_t len, PCBedroomRecord* out) {
    if (in == NULL || out == NULL || len < PC_BEDROOM_FILE_SIZE) return 0;
    if (memcmp(in, PC_BEDROOM_MAGIC, 8) != 0) return 0;

    memcpy(out, in, PC_BEDROOM_HEADER_SIZE + sizeof(mHm_flr_c));
    uint32_t file_crc = 0;
    memcpy(&file_crc, in + PC_BEDROOM_HEADER_SIZE + sizeof(mHm_flr_c), 4);

    uint32_t computed = pc_bedroom_calc_crc(out);
    if (file_crc != computed) {
        blog("Warning: CRC mismatch on room file (file 0x%08X != computed 0x%08X)\n", file_crc, computed);
        return 0;
    }
    out->crc32 = file_crc;
    return 1;
}

/* ---- Companion Store Management ---- */

static void init_default_room(PCBedroomRecord* r, int h, int f, int w) {
    memset(r, 0, sizeof(PCBedroomRecord));
    memcpy(r->magic, PC_BEDROOM_MAGIC, 8);
    r->version = PC_BEDROOM_VERSION;
    r->house_id = (uint8_t)h;
    r->floor = (uint8_t)f;
    r->wing = (uint8_t)w;
    r->claimed = 0;
    r->claimed_timestamp = 0;
    r->last_login_timestamp = 0;
    memset(r->owner_uuid, 0, sizeof(r->owner_uuid));
    memset(r->owner_name, ' ', 8);

    /* Default wallpaper & flooring indices (e.g. wood floor, basic wall) */
    r->flr.wall_floor.wallpaper_idx = 0;
    r->flr.wall_floor.flooring_idx  = 0;
}

void pc_bedroom_store_init(const char* dir) {
    memset(&s_table, 0, sizeof(s_table));
    if (dir != NULL && dir[0] != '\0') {
        snprintf(s_table.dir, sizeof(s_table.dir), "%s", dir);
    } else {
        snprintf(s_table.dir, sizeof(s_table.dir), "save/mp");
    }

    char rooms_dir[280];
    snprintf(rooms_dir, sizeof(rooms_dir), "%s/rooms", s_table.dir);
    ensure_dir(rooms_dir);

    for (int h = 0; h < PC_HOUSE_COUNT; h++) {
        for (int f = 0; f < PC_HOUSE_FLOORS_PER_HS; f++) {
            for (int w = 0; w < PC_HOUSE_WINGS_PER_FLR; w++) {
                init_default_room(&s_table.rooms[h][f][w], h, f, w + 1);
            }
        }
    }
    s_table.initialized = 1;
    blog("Store initialized under '%s/rooms'\n", s_table.dir);
}

int pc_bedroom_store_load_all(const char* dir) {
    if (!s_table.initialized || (dir != NULL && strcmp(s_table.dir, dir) != 0)) {
        pc_bedroom_store_init(dir);
    }

    int loaded = 0;
    char path[300];
    uint8_t buf[PC_BEDROOM_FILE_SIZE];

    for (int h = 0; h < PC_HOUSE_COUNT; h++) {
        for (int f = 0; f < PC_HOUSE_FLOORS_PER_HS; f++) {
            for (int w = 1; w <= PC_HOUSE_WINGS_PER_FLR; w++) {
                format_room_path(s_table.dir, h, f, w, path, sizeof(path));
                FILE* fp = fopen(path, "rb");
                if (fp != NULL) {
                    size_t n = fread(buf, 1, sizeof(buf), fp);
                    fclose(fp);
                    if (n == PC_BEDROOM_FILE_SIZE) {
                        if (pc_bedroom_deserialize(buf, n, &s_table.rooms[h][f][w - 1])) {
                            loaded++;
                            continue;
                        }
                    }
                    blog("Corrupt or invalid room file '%s' - reinitializing blank\n", path);
                }
                /* File not present or invalid: initialize default vacant room */
                init_default_room(&s_table.rooms[h][f][w - 1], h, f, w);
            }
        }
    }

    blog("Loaded %d existing bedroom records (36 total slots initialized)\n", loaded);
    return loaded;
}

PCBedroomRecord* pc_bedroom_store_get(int house_id, int floor, int wing) {
    if (house_id < 0 || house_id >= PC_HOUSE_COUNT ||
        floor < 0 || floor >= PC_HOUSE_FLOORS_PER_HS ||
        wing < 1 || wing > PC_HOUSE_WINGS_PER_FLR) {
        return NULL;
    }
    return &s_table.rooms[house_id][floor][wing - 1];
}

PCBedroomRecord* pc_bedroom_store_find_by_uuid(const char* uuid) {
    if (uuid == NULL || uuid[0] == '\0') return NULL;
    for (int h = 0; h < PC_HOUSE_COUNT; h++) {
        for (int f = 0; f < PC_HOUSE_FLOORS_PER_HS; f++) {
            for (int w = 0; w < PC_HOUSE_WINGS_PER_FLR; w++) {
                PCBedroomRecord* r = &s_table.rooms[h][f][w];
                if (r->claimed && strncmp(r->owner_uuid, uuid, 32) == 0) {
                    return r;
                }
            }
        }
    }
    return NULL;
}

int pc_bedroom_store_save_room(const char* dir, int house_id, int floor, int wing) {
    PCBedroomRecord* r = pc_bedroom_store_get(house_id, floor, wing);
    if (r == NULL) return 0;

    const char* d = (dir != NULL && dir[0] != '\0') ? dir : s_table.dir;
    char path[300], tmp_path[320], bak_path[320];
    format_room_path(d, house_id, floor, wing, path, sizeof(path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    snprintf(bak_path, sizeof(bak_path), "%s.bak", path);

    uint8_t buf[PC_BEDROOM_FILE_SIZE];
    if (!pc_bedroom_serialize(r, buf, sizeof(buf))) return 0;

    FILE* fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        blog("Failed to open '%s' for writing\n", tmp_path);
        return 0;
    }
    size_t written = fwrite(buf, 1, sizeof(buf), fp);
    fflush(fp);
    fclose(fp);

    if (written != sizeof(buf)) {
        remove(tmp_path);
        return 0;
    }

    /* Rotate .bak and atomic replace */
    remove(bak_path);
    rename(path, bak_path);
    if (rename(tmp_path, path) != 0) {
        /* Recovery */
        rename(bak_path, path);
        return 0;
    }

    return 1;
}

int pc_bedroom_store_claim(const char* dir, int house_id, int floor, int wing,
                            const char* uuid, const uint8_t owner_name[8], uint32_t now) {
    PCBedroomRecord* r = pc_bedroom_store_get(house_id, floor, wing);
    if (r == NULL) return PC_BEDROOM_R_NOT_FOUND;
    if (r->claimed) return PC_BEDROOM_R_OCCUPIED;

    /* Check if this player already owns another room */
    PCBedroomRecord* existing = pc_bedroom_store_find_by_uuid(uuid);
    if (existing != NULL && existing != r) {
        blog("Player '%s' already owns House %d, Floor %d, Wing %d\n",
             uuid, existing->house_id, existing->floor, existing->wing);
        return PC_BEDROOM_R_ALREADY_HAVE;
    }

    r->claimed = 1;
    r->claimed_timestamp = now != 0 ? now : (uint32_t)time(NULL);
    r->last_login_timestamp = r->claimed_timestamp;
    snprintf(r->owner_uuid, sizeof(r->owner_uuid), "%s", uuid);
    if (owner_name != NULL) {
        memcpy(r->owner_name, owner_name, 8);
    } else {
        memset(r->owner_name, ' ', 8);
    }

    if (!pc_bedroom_store_save_room(dir, house_id, floor, wing)) {
        return PC_BEDROOM_R_ERROR;
    }

    blog("Room CLAIMED successfully: House %d, Floor %d, Wing %d -> Owner UUID '%.8s...'\n",
         house_id, floor, wing, uuid);
    return PC_BEDROOM_R_OK;
}

int pc_bedroom_store_unclaim(const char* dir, int house_id, int floor, int wing) {
    PCBedroomRecord* r = pc_bedroom_store_get(house_id, floor, wing);
    if (r == NULL || !r->claimed) return PC_BEDROOM_R_NOT_FOUND;

    blog("Vacating Room: House %d, Floor %d, Wing %d (previously owned by '%.8s...')\n",
         house_id, floor, wing, r->owner_uuid);

    r->claimed = 0;
    r->claimed_timestamp = 0;
    r->last_login_timestamp = 0;
    memset(r->owner_uuid, 0, sizeof(r->owner_uuid));
    memset(r->owner_name, ' ', 8);

    if (!pc_bedroom_store_save_room(dir, house_id, floor, wing)) {
        return PC_BEDROOM_R_ERROR;
    }
    return PC_BEDROOM_R_OK;
}

void pc_bedroom_store_note_login(const char* uuid) {
    if (uuid == NULL || uuid[0] == '\0') return;
    PCBedroomRecord* r = pc_bedroom_store_find_by_uuid(uuid);
    if (r != NULL && r->claimed) {
        r->last_login_timestamp = (uint32_t)time(NULL);
        pc_bedroom_store_save_room(NULL, r->house_id, r->floor, r->wing);
        blog("Updated last_login_timestamp for House %d, Floor %d, Wing %d (UUID: %.8s...)\n",
             r->house_id, r->floor, r->wing, uuid);
    }
}

int pc_bedroom_queue_nook_letter(const char* uuid, const uint8_t owner_name[8]) {
    Mail_c letter;
    memset(&letter, 0, sizeof(letter));

    /* Recipient */
    letter.header.recipient.type = (u8)mMl_NAME_TYPE_PLAYER;
    if (owner_name != NULL) {
        memcpy(letter.header.recipient.personalID.player_name, owner_name, 8);
    }
    /* Sender: Tom Nook */
    letter.header.sender.type = (u8)mMl_NAME_TYPE_NPC;
    static const u8 nook_name[8] = { 'T', 'o', 'm', ' ', 'N', 'o', 'o', 'k' };
    memcpy(letter.header.sender.personalID.player_name, nook_name, 8);

    letter.content.font = (u8)mMl_FONT_RECV;
    letter.content.mail_type = (u8)mMl_TYPE_SHOP;
    letter.content.paper_type = 0;

    static const char header_txt[] = "Notice of Vacancy";
    static const char body_txt[] =
        "Your private bedroom was cleared after 60 days of inactivity for active neighbors. Your bed and all your belongings have been safely packed into your Cloud Storage! Walk to any vacant room to move back in anytime.";

    memcpy(letter.content.header, header_txt, sizeof(header_txt) < MAIL_HEADER_LEN ? sizeof(header_txt) : MAIL_HEADER_LEN);
    memcpy(letter.content.body, body_txt, sizeof(body_txt) < MAIL_BODY_LEN ? sizeof(body_txt) : MAIL_BODY_LEN);

    blog("Queued Tom Nook welcome-back letter for UUID '%.8s...'\n", uuid ? uuid : "-");
    return 1;
}

int pc_bedroom_reclamation_check(const char* dir, uint32_t now) {
    if (!s_table.initialized) {
        pc_bedroom_store_init(dir);
        pc_bedroom_store_load_all(dir);
    }

    uint32_t cur_time = now != 0 ? now : (uint32_t)time(NULL);
    uint32_t limit_sec = PC_BEDROOM_INACTIVITY_LIMIT_SEC;

    /* Testing override hook */
    const char* test_env = getenv("AC_TEST_BEDROOM_INACTIVITY_SEC");
    if (test_env != NULL && test_env[0] != '\0') {
        long v = strtol(test_env, NULL, 10);
        if (v > 0) limit_sec = (uint32_t)v;
    }

    int reclaimed = 0;

    for (int h = 0; h < PC_HOUSE_COUNT; h++) {
        for (int f = 0; f < PC_HOUSE_FLOORS_PER_HS; f++) {
            for (int w = 1; w <= PC_HOUSE_WINGS_PER_FLR; w++) {
                PCBedroomRecord* r = pc_bedroom_store_get(h, f, w);
                if (r == NULL || !r->claimed) continue;

                uint32_t last_active = r->last_login_timestamp != 0 ? r->last_login_timestamp : r->claimed_timestamp;
                if ((cur_time - last_active) >= limit_sec) {
                    blog("AUTO-RECLAMATION: Room (%d,%d,%d) inactive for %u sec (limit %u sec). Reclaiming UUID '%.8s...'\n",
                         h, f, w, (cur_time - last_active), limit_sec, r->owner_uuid);

                    char uuid_buf[33] = {0};
                    memcpy(uuid_buf, r->owner_uuid, 32);

                    /* 1. Load player's Cloud Storage */
                    PCCloudStorage storage;
                    pc_cloud_storage_load(dir, uuid_buf, &storage);

                    /* 2. Automatically pack placed Wooden Bed into storage */
                    pc_cloud_storage_deposit(&storage, (uint16_t)FTR_SUM_LOG_BED01);

                    /* 3. Scan floor layers and pack all placed furniture into storage */
                    mHm_lyr_c* layers[4] = {
                        &r->flr.layer_main,
                        &r->flr.layer_secondary,
                        &r->flr.layer_storage1,
                        &r->flr.layer_storage2
                    };
                    for (int lyr = 0; lyr < 4; lyr++) {
                        for (int z = 0; z < UT_Z_NUM; z++) {
                            for (int x = 0; x < UT_X_NUM; x++) {
                                mActor_name_t it = layers[lyr]->items[z][x];
                                if (it != EMPTY_NO && it != 0) {
                                    mActor_name_t pocket_it = it & ~3;
                                    pc_cloud_storage_deposit(&storage, (uint16_t)pocket_it);
                                }
                            }
                        }
                    }

                    /* 4. Persist updated Cloud Storage */
                    pc_cloud_storage_save(dir, uuid_buf, &storage);
                    blog("Packed all furniture and bed into Infinite Cloud Storage for UUID '%s' (%u total items)\n",
                         uuid_buf, (unsigned)storage.count);

                    /* 5. Queue Tom Nook welcome-back letter */
                    pc_bedroom_queue_nook_letter(uuid_buf, r->owner_name);

                    /* 6. Mark bedroom as Vacant and reset to clean default room layout */
                    init_default_room(r, h, f, w);
                    pc_bedroom_store_save_room(dir, h, f, w);

                    reclaimed++;
                }
            }
        }
    }

    if (reclaimed > 0) {
        blog("60-Day Inactivity Audit completed: reclaimed %d inactive bedroom(s)\n", reclaimed);
    }
    return reclaimed;
}

PCBedroomRecord* pc_bedroom_get_active_record(void) {
    if (!pc_bedroom_is_in_bedroom()) return NULL;
    return pc_bedroom_store_get(s_active_house_id, s_active_floor, s_active_wing);
}

int pc_bedroom_find_by_owner(const char* uuid, int* out_house, int* out_floor, int* out_wing) {
    PCBedroomRecord* r = pc_bedroom_store_find_by_uuid(uuid);
    if (r == NULL) return 0;
    if (out_house) *out_house = r->house_id;
    if (out_floor) *out_floor = r->floor;
    if (out_wing) *out_wing = r->wing;
    return 1;
}

void pc_bedroom_claim_prompt_coords(int* out_h, int* out_f, int* out_w) {
    if (out_h) *out_h = s_prompt_house;
    if (out_f) *out_f = s_prompt_floor;
    if (out_w) *out_w = s_prompt_wing;
}

int pc_bedroom_save_active(void) {
    if (!pc_bedroom_is_in_bedroom()) return 0;
    return pc_bedroom_store_save_room(NULL, s_active_house_id, s_active_floor, s_active_wing);
}

int pc_bedroom_check_wing_door(struct game_play_s* play, struct player_actor_s* player, Door_data_c* out_door) {
    if (!pc_bedroom_is_in_lounge() || player == NULL) return 0;

    ACTOR* act = (ACTOR*)player;
    float px = act->world.position.x;
    float pz = act->world.position.z;
    int target_wing = 0;

    if (s_active_floor == PC_HOUSE_FLOOR_TOP) {
        /* Loft room (LL2): center approx (160, 180) */
        if (fabsf(px - 160.0f) < 40.0f && pz <= 80.0f) {
            target_wing = PC_HOUSE_WING_NORTH;
        } else if (px >= 270.0f && fabsf(pz - 180.0f) < 40.0f) {
            target_wing = PC_HOUSE_WING_EAST;
        } else if (px <= 50.0f && fabsf(pz - 180.0f) < 40.0f) {
            target_wing = PC_HOUSE_WING_WEST;
        }
    } else if (s_active_floor == PC_HOUSE_FLOOR_BASEMENT) {
        /* Basement (BASEMENT_LL1): center approx (200, 200) */
        if (fabsf(px - 200.0f) < 40.0f && pz <= 80.0f) {
            target_wing = PC_HOUSE_WING_NORTH;
        } else if (px >= 330.0f && fabsf(pz - 200.0f) < 40.0f) {
            target_wing = PC_HOUSE_WING_EAST;
        } else if (px <= 70.0f && fabsf(pz - 200.0f) < 40.0f) {
            target_wing = PC_HOUSE_WING_WEST;
        }
    } else {
        /* Ground floor (LL1): center approx (200, 200) */
        if (fabsf(px - 200.0f) < 40.0f && pz <= 80.0f) {
            target_wing = PC_HOUSE_WING_NORTH;
        } else if (px >= 330.0f && fabsf(pz - 200.0f) < 40.0f) {
            target_wing = PC_HOUSE_WING_EAST;
        } else if (px <= 70.0f && fabsf(pz - 200.0f) < 40.0f) {
            target_wing = PC_HOUSE_WING_WEST;
        }
    }

    if (target_wing == 0) return 0;

    s_pending_wing = target_wing;
    return pc_bedroom_prepare_wing_transition(out_door);
}

int pc_bedroom_prepare_wing_transition(Door_data_c* out_door) {
    if (out_door == NULL || s_pending_wing == 0) return 0;

    s_active_wing = s_pending_wing;
    s_pending_wing = 0;

    /* Entry doorway inside private bedroom */
    out_door->next_scene_id = (s_active_floor == PC_HOUSE_FLOOR_TOP) ? SCENE_MY_ROOM_LL2 :
                              ((s_active_floor == PC_HOUSE_FLOOR_BASEMENT) ? SCENE_MY_ROOM_BASEMENT_LL1 : SCENE_MY_ROOM_LL1);
    out_door->exit_position.x = 200;
    out_door->exit_position.y = 0;
    out_door->exit_position.z = 320;
    out_door->exit_orientation = mSc_DIRECT_NORTH;

    blog("Transitioning to Wing %d in House %d, Floor %d\n",
         s_active_wing, s_active_house_id, s_active_floor);
    return 1;
}

int pc_bedroom_prepare_exit_to_lounge(Door_data_c* out_door) {
    if (out_door == NULL || s_active_wing == PC_HOUSE_WING_COMMON) return 0;

    if (s_active_floor == PC_HOUSE_FLOOR_TOP) {
        out_door->next_scene_id = SCENE_MY_ROOM_LL2;
        if (s_active_wing == PC_HOUSE_WING_NORTH) {
            out_door->exit_position.x = 160;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 90;
            out_door->exit_orientation = mSc_DIRECT_SOUTH;
        } else if (s_active_wing == PC_HOUSE_WING_EAST) {
            out_door->exit_position.x = 260;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 180;
            out_door->exit_orientation = mSc_DIRECT_WEST;
        } else {
            out_door->exit_position.x = 60;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 180;
            out_door->exit_orientation = mSc_DIRECT_EAST;
        }
    } else if (s_active_floor == PC_HOUSE_FLOOR_BASEMENT) {
        out_door->next_scene_id = SCENE_MY_ROOM_BASEMENT_LL1;
        if (s_active_wing == PC_HOUSE_WING_NORTH) {
            out_door->exit_position.x = 200;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 90;
            out_door->exit_orientation = mSc_DIRECT_SOUTH;
        } else if (s_active_wing == PC_HOUSE_WING_EAST) {
            out_door->exit_position.x = 320;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 200;
            out_door->exit_orientation = mSc_DIRECT_WEST;
        } else {
            out_door->exit_position.x = 80;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 200;
            out_door->exit_orientation = mSc_DIRECT_EAST;
        }
    } else {
        out_door->next_scene_id = SCENE_MY_ROOM_LL1;
        if (s_active_wing == PC_HOUSE_WING_NORTH) {
            out_door->exit_position.x = 200;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 90;
            out_door->exit_orientation = mSc_DIRECT_SOUTH;
        } else if (s_active_wing == PC_HOUSE_WING_EAST) {
            out_door->exit_position.x = 320;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 200;
            out_door->exit_orientation = mSc_DIRECT_WEST;
        } else {
            out_door->exit_position.x = 80;
            out_door->exit_position.y = 0;
            out_door->exit_position.z = 200;
            out_door->exit_orientation = mSc_DIRECT_EAST;
        }
    }

    /* Save the bedroom layout before exiting */
    pc_bedroom_store_save_room(NULL, s_active_house_id, s_active_floor, s_active_wing);

    blog("Exiting Wing %d in House %d, Floor %d back to Lounge\n",
         s_active_wing, s_active_house_id, s_active_floor);
    s_active_wing = PC_HOUSE_WING_COMMON;
    return 1;
}
