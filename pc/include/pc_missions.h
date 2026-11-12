/* pc_missions.h - Daily Villager Missions (Phase 4).
 * Deterministic daily seed (3-5 rotating quests), real-time progress tracking,
 * physical villager delivery, and per-UUID daily completion persistence.
 * Multiplatform: ARM64 Android & 32-bit x86 Linux server ABI compatible.
 */
#ifndef PC_MISSIONS_H
#define PC_MISSIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_MISSIONS_MAX         5
#define PC_MISSIONS_MSG_ID      107

enum {
    PC_MISSION_TYPE_FISH      = 0, /* Catch & deliver N fish */
    PC_MISSION_TYPE_BUG       = 1, /* Catch & deliver N insects */
    PC_MISSION_TYPE_FRUIT     = 2, /* Deliver N orchard fruit */
    PC_MISSION_TYPE_FOSSIL    = 3, /* Uncover a fossil today & report */
    PC_MISSION_TYPE_LOST_ITEM = 4  /* Deliver a specific item found in town */
};

enum {
    PC_MISSION_OP_SYNC        = 1, /* Host -> Client: today's missions + completed_mask */
    PC_MISSION_OP_TURN_IN_REQ = 2, /* Client -> Host: request turn-in for slot */
    PC_MISSION_OP_TURN_IN_ACK = 3  /* Host -> Client: turn-in result + updated mask */
};

/* 64-byte fixed-width mission definition */
typedef struct PCMissionDef {
    uint8_t  slot;              /* 0..4 */
    uint8_t  type;              /* PC_MISSION_TYPE_* */
    uint8_t  target_count;      /* 1..5 */
    uint8_t  acre_x;            /* 1..5 (display hint) */
    uint8_t  acre_z;            /* 1..6 (display hint) */
    uint8_t  reward_kind;       /* 0 = Bells, 1 = Bells + Ticket/Seeds */
    uint16_t villager_npc_id;   /* Animal_c.id.npc_id */
    uint16_t target_item;       /* Specific item ID or 0 (EMPTY_NO) for any */
    uint16_t reward_item;       /* Bonus item ID or 0 */
    uint32_t reward_bells;      /* 1500..3000 */
    char     villager_name[16]; /* Clean ASCII villager name */
    char     title[32];         /* Short quest label */
} PCMissionDef;

_Static_assert(sizeof(PCMissionDef) == 64, "PCMissionDef size must be 64 bytes");

/* Fixed-width wire message (332 bytes) */
typedef struct PCMissionMsg {
    uint8_t      msg_type;      /* PC_NETGAME_MSG_MISSIONS (107) */
    uint8_t      op;            /* PC_MISSION_OP_* */
    uint8_t      slot;          /* slot for TURN_IN_REQ / ACK */
    uint8_t      ok;            /* 1 = accepted, 0 = rejected */
    uint16_t     year;
    uint8_t      month;
    uint8_t      day;
    uint8_t      count;         /* 3..5 */
    uint8_t      completed_mask;/* bitmask of completed slots */
    uint16_t     npc_id;        /* target villager npc_id for TURN_IN_REQ */
    PCMissionDef defs[PC_MISSIONS_MAX];
} PCMissionMsg;

_Static_assert(sizeof(PCMissionMsg) == 332, "PCMissionMsg size must be 332 bytes");

typedef struct PCMissionState {
    uint16_t     year;
    uint8_t      month;
    uint8_t      day;
    uint8_t      count;
    uint8_t      completed_mask;
    uint8_t      fossil_dug_today;
    uint8_t      initialized;
    uint8_t      rsv;
    PCMissionDef defs[PC_MISSIONS_MAX];
} PCMissionState;

/* Roll (or refresh if date changed) today's 3-5 missions from RTC date & town animals. */
void pc_missions_init_or_roll(uint16_t year, uint8_t month, uint8_t day);

/* Ensure local state is initialized for current Common_Get(time.rtc_time) if solo/offline. */
void pc_missions_ensure_local_today(void);

/* Read-only access to active missions state. */
const PCMissionState* pc_missions_get_state(void);

/* Mark that the local player uncovered a fossil today. */
void pc_missions_note_fossil_dug(void);

/* Compute live progress for slot 0..count-1. Returns 1 if ready to turn in, 0 otherwise. */
int pc_missions_get_progress(int slot, int* out_cur, int* out_target);

/* Returns slot index (0..4) if `npc_id` has a mission ready to turn in, or -1 if none. */
int pc_missions_has_ready_for_villager(uint16_t npc_id);

/* Attempt physical turn-in for `slot` (consumes pocket items, sends wire request or grants locally).
 * Returns 1 if turn-in initiated/completed, 0 otherwise. */
int pc_missions_try_turn_in(int slot);

/* Grant reward locally after host ACK (or solo turn-in). */
void pc_missions_grant_reward_local(int slot);

/* Wire helpers */
void pc_missions_build_sync_msg(PCMissionMsg* out, uint8_t completed_mask);
void pc_missions_apply_sync_msg(const PCMissionMsg* msg);

/* Per-UUID daily completion mask persistence (host authoritative / solo local). */
int pc_missions_store_load_mask(const char* dir, const char* uuid,
                                uint16_t year, uint8_t month, uint8_t day,
                                uint8_t* out_mask);
int pc_missions_store_save_mask(const char* dir, const char* uuid,
                                uint16_t year, uint8_t month, uint8_t day,
                                uint8_t mask);

/* Network bridge (implemented in pc_net_game_missions.c_inc) */
void pc_net_game_missions_send_turn_in(int slot, uint16_t npc_id);

#ifdef __cplusplus
}
#endif

#endif /* PC_MISSIONS_H */
