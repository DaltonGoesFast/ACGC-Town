/* pc_bedroom.h - Multi-Resident Housing Architecture (Phase 2 & Phase 3):
 * 3-Story Boarding Houses with 9 Private Bedrooms per House (36 permanent residents).
 * 60-Day Inactivity Auto-Reclamation into Infinite Cloud Storage.
 * Pure C, ABI-compatible across 32-bit x86 Linux and ARM64 Android.
 * Does NOT modify Save_t (148,128-byte invariant strictly preserved).
 */
#ifndef PC_BEDROOM_H
#define PC_BEDROOM_H

#include <stddef.h>
#include <stdint.h>
#include "m_home_h.h"
#include "m_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_HOUSE_COUNT            4   /* 4 houses in town */
#define PC_HOUSE_FLOORS_PER_HS    3   /* Middle (0), Top (1), Basement (2) */
#define PC_HOUSE_WINGS_PER_FLR    3   /* North (1), East (2), West (3) */
#define PC_HOUSE_BEDROOMS_PER_HS  9   /* 3 floors * 3 wings = 9 private rooms */
#define PC_TOWN_TOTAL_BEDROOMS    36  /* 4 houses * 9 bedrooms = 36 residents */

#define PC_BEDROOM_INACTIVITY_LIMIT_SEC  (60u * 86400u) /* 60 consecutive days in seconds */

/* Vertical Floor Levels */
enum {
    PC_HOUSE_FLOOR_MIDDLE   = 0,    /* Ground / Main level (Scene SCENE_MY_ROOM_LL1) */
    PC_HOUSE_FLOOR_TOP      = 1,    /* 2nd floor / Loft level (Scene SCENE_MY_ROOM_LL2) */
    PC_HOUSE_FLOOR_BASEMENT = 2     /* Basement / Clubhouse level (Scene SCENE_MY_ROOM_BASEMENT_LL1) */
};

/* Floor Wings */
enum {
    PC_HOUSE_WING_COMMON    = 0,    /* Shared lounge (1 per floor) */
    PC_HOUSE_WING_NORTH     = 1,    /* North private bedroom wing */
    PC_HOUSE_WING_EAST      = 2,    /* East private bedroom wing */
    PC_HOUSE_WING_WEST      = 3     /* West private bedroom wing */
};

/* File Format & Size */
#define PC_BEDROOM_MAGIC        "ACBEDR1\0"
#define PC_BEDROOM_VERSION      1u
#define PC_BEDROOM_HEADER_SIZE  64
#define PC_BEDROOM_FILE_SIZE    (PC_BEDROOM_HEADER_SIZE + sizeof(mHm_flr_c) + 4) /* 64 + 2216 + 4 = 2284 bytes */

/* Exact 32/64-bit ABI aligned record */
typedef struct PCBedroomRecord {
    /* 64-byte Header */
    char        magic[8];                   /* "ACBEDR1\0" (0..7) */
    uint32_t    version;                    /* 1 (8..11) */
    uint8_t     house_id;                   /* 0..3 (12) */
    uint8_t     floor;                      /* 0..2 (13) */
    uint8_t     wing;                       /* 1..3 (North/East/West) (14) */
    uint8_t     claimed;                    /* 1 = claimed, 0 = vacant (15) */
    uint32_t    claimed_timestamp;          /* unix seconds (16..19) */
    uint32_t    last_login_timestamp;       /* unix seconds of last connection (20..23) */
    char        owner_uuid[32];             /* 32 hex chars (24..55) */
    uint8_t     owner_name[8];              /* player name font codes (56..63) */

    /* 2,216-byte GameCube Floor Image (mHm_flr_c: layers 0..3, wallpaper, flooring, tempo) */
    mHm_flr_c   flr;

    /* 4-byte CRC32 trailer over header + flr */
    uint32_t    crc32;
} PCBedroomRecord;

/* In-memory store table */
typedef struct PCBedroomTable {
    int             initialized;
    char            dir[260];
    PCBedroomRecord rooms[PC_HOUSE_COUNT][PC_HOUSE_FLOORS_PER_HS][PC_HOUSE_WINGS_PER_FLR];
} PCBedroomTable;

/* ---- Wire Message 106: PC_NETGAME_MSG_BEDROOM ---- */
#define PC_BEDROOM_MSG_ID           106
#define PC_BEDROOM_MSG_SIZE         64

/* Operations */
#define PC_BEDROOM_OP_QUERY         1   /* C -> H: Query room status (house, floor, wing) */
#define PC_BEDROOM_OP_CLAIM_REQ     2   /* C -> H: Request room claim after bed placement */
#define PC_BEDROOM_OP_UNCLAIM_REQ   3   /* C -> H: Voluntarily vacate room */
#define PC_BEDROOM_OP_INFO          16  /* H -> C: Room status response */
#define PC_BEDROOM_OP_CLAIM_ACK     17  /* H -> C: Claim confirmation result */
#define PC_BEDROOM_OP_LOGIN_STATUS  18  /* H -> C: Player's bound residence sent at join */

/* Results */
#define PC_BEDROOM_R_OK             0   /* Success */
#define PC_BEDROOM_R_OCCUPIED       1   /* Room is already claimed by someone else */
#define PC_BEDROOM_R_ALREADY_HAVE   2   /* Player already has a claimed room */
#define PC_BEDROOM_R_NOT_FOUND      3   /* Room not found */
#define PC_BEDROOM_R_ERROR          4   /* General / I/O error */

typedef struct PCBedroomMsg {
    uint8_t  msg_type;          /* 106 */
    uint8_t  op;                /* PC_BEDROOM_OP_* */
    uint8_t  result;            /* PC_BEDROOM_R_* */
    uint8_t  house_id;          /* 0..3 */
    uint8_t  floor;             /* 0..2 */
    uint8_t  wing;              /* 1..3 */
    uint8_t  claimed;           /* 0 or 1 */
    uint8_t  rsv;
    uint32_t claimed_timestamp; /* unix seconds */
    char     owner_uuid[33];    /* 32 hex chars + NUL */
    uint8_t  owner_name[8];     /* player name font codes */
    uint8_t  pad[11];           /* padded to 64 bytes */
} PCBedroomMsg;

/* Calculation Helpers */
static inline int pc_bedroom_calc_room_idx(int floor, int wing) {
    if (wing < 1 || wing > 3) return -1;
    int base = (floor == 0 ? 0 : (floor == 2 ? 3 : 6));
    return base + (wing - 1);
}

static inline int pc_bedroom_calc_town_id(int house_id, int floor, int wing) {
    int room_idx = pc_bedroom_calc_room_idx(floor, wing);
    if (room_idx < 0 || house_id < 0 || house_id >= PC_HOUSE_COUNT) return -1;
    return house_id * PC_HOUSE_BEDROOMS_PER_HS + room_idx;
}

/* Active Navigation & Player Housing Context */
void pc_bedroom_set_active_location(int house_id, int floor, int wing);
void pc_bedroom_get_active_location(int* house_id, int* floor, int* wing);
int  pc_bedroom_is_in_bedroom(void);
int  pc_bedroom_is_in_lounge(void);
int  pc_bedroom_is_owner(const char* player_uuid);

/* Door Navigation & Wing Transitions */
struct game_play_s;
struct player_actor_s;
int  pc_bedroom_check_wing_door(struct game_play_s* play, struct player_actor_s* player, Door_data_c* out_door);
int  pc_bedroom_prepare_wing_transition(Door_data_c* out_door);
int  pc_bedroom_prepare_exit_to_lounge(Door_data_c* out_door);
PCBedroomRecord* pc_bedroom_get_active_record(void);
int  pc_bedroom_find_by_owner(const char* uuid, int* out_house, int* out_floor, int* out_wing);
void pc_bedroom_claim_prompt_coords(int* out_h, int* out_f, int* out_w);
int  pc_bedroom_save_active(void);

/* Local Claim Prompt State */
void pc_bedroom_arm_claim_prompt(int house_id, int floor, int wing);
void pc_bedroom_disarm_claim_prompt(void);
int  pc_bedroom_claim_prompt_active(void);
void pc_bedroom_confirm_claim(void);
void pc_bedroom_cancel_claim(void);

/* Companion Store Management */
void pc_bedroom_store_init(const char* dir);
int  pc_bedroom_store_load_all(const char* dir);
PCBedroomRecord* pc_bedroom_store_get(int house_id, int floor, int wing);
PCBedroomRecord* pc_bedroom_store_find_by_uuid(const char* uuid);
int  pc_bedroom_store_claim(const char* dir, int house_id, int floor, int wing,
                            const char* uuid, const uint8_t owner_name[8], uint32_t now);
int  pc_bedroom_store_unclaim(const char* dir, int house_id, int floor, int wing);
int  pc_bedroom_store_save_room(const char* dir, int house_id, int floor, int wing);
void pc_bedroom_store_note_login(const char* uuid);

/* Phase 3: 60-Day Inactivity Auto-Reclamation & Welcome-Back Letter */
int  pc_bedroom_reclamation_check(const char* dir, uint32_t now);
int  pc_bedroom_queue_nook_letter(const char* uuid, const uint8_t owner_name[8]);

/* CRC32 & Buffer serialization */
uint32_t pc_bedroom_calc_crc(const PCBedroomRecord* r);
int      pc_bedroom_serialize(const PCBedroomRecord* r, uint8_t* out, size_t cap);
int      pc_bedroom_deserialize(const uint8_t* in, size_t len, PCBedroomRecord* out);

#ifdef __cplusplus
}
#endif

#endif /* PC_BEDROOM_H */
