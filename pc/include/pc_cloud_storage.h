/* pc_cloud_storage.h - Infinite Cloud Storage & Anti-Hoarding Rules (Phase 3).
 * Per-player persistent storage (save/mp/characters/<uuid>/storage.dat).
 * Multiplatform: ARM64 Android & 32-bit x86 Linux server ABI compatible.
 */
#ifndef PC_CLOUD_STORAGE_H
#define PC_CLOUD_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include "pc_dresser_items.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_CLOUD_STORAGE_MAGIC          "ACSTOR1\0"
#define PC_CLOUD_STORAGE_VERSION        1u
#define PC_CLOUD_STORAGE_HEADER_SIZE    32
#define PC_CLOUD_STORAGE_PAGE_SIZE      27      /* 9 cols x 3 rows */
#define PC_CLOUD_STORAGE_MAX_ITEMS      2048    /* Virtually unlimited items */
#define PC_CLOUD_STORAGE_FILE_SIZE      (PC_CLOUD_STORAGE_HEADER_SIZE + (PC_CLOUD_STORAGE_MAX_ITEMS * sizeof(uint16_t)) + 4) /* 32 + 4096 + 4 = 4132 bytes */

#define PC_CRITTER_ANTI_HOARDING_TOAST \
    "Live critters must be kept in pockets! Donate them to the Museum, sell to Tom Nook, or give them to a neighbor."

/* Exact 32/64-bit ABI aligned record */
typedef struct PCCloudStorage {
    /* 32-byte Header */
    char        magic[8];       /* "ACSTOR1\0" */
    uint32_t    version;        /* 1 */
    uint32_t    count;          /* number of items currently stored */
    uint32_t    rsv[4];         /* zero padding to 32 bytes */

    /* Item data (4,096 bytes) */
    uint16_t    items[PC_CLOUD_STORAGE_MAX_ITEMS];

    /* 4-byte CRC32 trailer over header + items */
    uint32_t    crc32;
} PCCloudStorage;

/* Operation return codes */
enum {
    PC_CLOUD_STORAGE_OK              = 0,
    PC_CLOUD_STORAGE_CRITTER_BLOCKED = 1,
    PC_CLOUD_STORAGE_FULL            = 2,
    PC_CLOUD_STORAGE_INVALID_ITEM    = 3,
    PC_CLOUD_STORAGE_EMPTY_SLOT      = 4,
    PC_CLOUD_STORAGE_ERROR           = 5
};

/* Item validation & Anti-Hoarding Critter Blacklist */
int pc_cloud_storage_is_critter(uint16_t item);
int pc_cloud_storage_item_allowed(uint16_t item);

/* Structure management & CRC32 */
void     pc_cloud_storage_init(PCCloudStorage* s);
uint32_t pc_cloud_storage_calc_crc(const PCCloudStorage* s);
int      pc_cloud_storage_serialize(const PCCloudStorage* s, uint8_t* out, size_t cap);
int      pc_cloud_storage_deserialize(const uint8_t* in, size_t len, PCCloudStorage* out);

/* Storage file path: <dir>/characters/<uuid>/storage.dat */
void pc_cloud_storage_path(const char* dir, const char* uuid, char* out, size_t cap);

/* Authoritative file load & save */
int pc_cloud_storage_load(const char* dir, const char* uuid, PCCloudStorage* out);
int pc_cloud_storage_save(const char* dir, const char* uuid, const PCCloudStorage* s);

/* Item Deposit & Withdrawal */
int pc_cloud_storage_deposit(PCCloudStorage* s, uint16_t item);
int pc_cloud_storage_withdraw(PCCloudStorage* s, int index, uint16_t* out_item);

/* Pagination Helpers (27 items per page) */
int pc_cloud_storage_total_pages(const PCCloudStorage* s);
int pc_cloud_storage_get_page_items(const PCCloudStorage* s, int page, uint16_t out_items[PC_CLOUD_STORAGE_PAGE_SIZE]);

/* Client Active Session Context */
void            pc_cloud_storage_client_open(const char* uuid);
void            pc_cloud_storage_client_close(void);
int             pc_cloud_storage_client_is_open(void);
int             pc_cloud_storage_client_page(void);
void            pc_cloud_storage_client_next_page(void);
void            pc_cloud_storage_client_prev_page(void);
void            pc_cloud_storage_client_set_page(int page);
PCCloudStorage* pc_cloud_storage_client_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_CLOUD_STORAGE_H */
