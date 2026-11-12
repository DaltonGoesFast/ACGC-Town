/* pc_cloud_storage.c - Infinite Cloud Storage & Anti-Hoarding Rules (Phase 3).
 * Multiplatform: ARM64 Android & 32-bit x86 Linux server ABI compatible.
 */
#include "pc_cloud_storage.h"
#include "pc_mp_records.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MKDIR(d) _mkdir(d)
#else
#include <unistd.h>
#define MKDIR(d) mkdir((d), 0755)
#endif

/* Local client active context */
static int            s_client_open  = 0;
static int            s_client_page  = 0;
static char           s_client_uuid[33] = {0};
static PCCloudStorage s_client_storage;

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

/* Item validation & Anti-Hoarding */
int pc_cloud_storage_is_critter(uint16_t item) {
    return pc_dresser_item_is_critter((unsigned)item);
}

int pc_cloud_storage_item_allowed(uint16_t item) {
    if (pc_cloud_storage_is_critter(item)) {
        return 0; /* Live critters blacklisted from storage */
    }
    return pc_dresser_item_extra_ok((unsigned)item);
}

void pc_cloud_storage_init(PCCloudStorage* s) {
    if (s == NULL) return;
    memset(s, 0, sizeof(PCCloudStorage));
    memcpy(s->magic, PC_CLOUD_STORAGE_MAGIC, 8);
    s->version = PC_CLOUD_STORAGE_VERSION;
    s->count = 0;
}

uint32_t pc_cloud_storage_calc_crc(const PCCloudStorage* s) {
    if (s == NULL) return 0;
    /* CRC over 32-byte header + items array */
    size_t body_len = PC_CLOUD_STORAGE_HEADER_SIZE + (PC_CLOUD_STORAGE_MAX_ITEMS * sizeof(uint16_t));
    return pc_mp_records_crc32(s, body_len);
}

int pc_cloud_storage_serialize(const PCCloudStorage* s, uint8_t* out, size_t cap) {
    if (s == NULL || out == NULL || cap < PC_CLOUD_STORAGE_FILE_SIZE) return 0;
    size_t body_len = PC_CLOUD_STORAGE_HEADER_SIZE + (PC_CLOUD_STORAGE_MAX_ITEMS * sizeof(uint16_t));
    memcpy(out, s, body_len);
    uint32_t crc = pc_cloud_storage_calc_crc(s);
    memcpy(out + body_len, &crc, 4);
    return PC_CLOUD_STORAGE_FILE_SIZE;
}

int pc_cloud_storage_deserialize(const uint8_t* in, size_t len, PCCloudStorage* out) {
    if (in == NULL || out == NULL || len < PC_CLOUD_STORAGE_FILE_SIZE) return 0;
    if (memcmp(in, PC_CLOUD_STORAGE_MAGIC, 8) != 0) return 0;

    size_t body_len = PC_CLOUD_STORAGE_HEADER_SIZE + (PC_CLOUD_STORAGE_MAX_ITEMS * sizeof(uint16_t));
    memcpy(out, in, body_len);
    uint32_t file_crc = 0;
    memcpy(&file_crc, in + body_len, 4);

    uint32_t computed = pc_cloud_storage_calc_crc(out);
    if (file_crc != computed) {
        printf("[STORAGE] CRC mismatch (file 0x%08X != computed 0x%08X)\n", file_crc, computed);
        return 0;
    }
    out->crc32 = file_crc;
    return 1;
}

void pc_cloud_storage_path(const char* dir, const char* uuid, char* out, size_t cap) {
    const char* d = (dir != NULL && dir[0] != '\0') ? dir : "save/mp";
    snprintf(out, cap, "%s/characters/%s/storage.dat", d, (uuid != NULL && uuid[0] != '\0') ? uuid : "default");
}

int pc_cloud_storage_load(const char* dir, const char* uuid, PCCloudStorage* out) {
    if (out == NULL) return 0;
    pc_cloud_storage_init(out);

    char path[300];
    pc_cloud_storage_path(dir, uuid, path, sizeof(path));

    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        /* Missing storage file initializes as clean empty storage */
        return 1;
    }

    uint8_t buf[PC_CLOUD_STORAGE_FILE_SIZE];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (n == sizeof(buf) && pc_cloud_storage_deserialize(buf, n, out)) {
        return 1;
    }

    printf("[STORAGE] Storage file '%s' corrupted, resetting empty\n", path);
    pc_cloud_storage_init(out);
    return 1;
}

int pc_cloud_storage_save(const char* dir, const char* uuid, const PCCloudStorage* s) {
    if (s == NULL) return 0;

    char path[300], tmp_path[320], bak_path[320], parent_dir[300];
    const char* d = (dir != NULL && dir[0] != '\0') ? dir : "save/mp";
    snprintf(parent_dir, sizeof(parent_dir), "%s/characters/%s", d, (uuid != NULL && uuid[0] != '\0') ? uuid : "default");
    ensure_dir(parent_dir);

    pc_cloud_storage_path(dir, uuid, path, sizeof(path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    snprintf(bak_path, sizeof(bak_path), "%s.bak", path);

    uint8_t buf[PC_CLOUD_STORAGE_FILE_SIZE];
    if (!pc_cloud_storage_serialize(s, buf, sizeof(buf))) return 0;

    FILE* fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        printf("[STORAGE] Failed to open '%s' for writing\n", tmp_path);
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
        rename(bak_path, path);
        return 0;
    }

    return 1;
}

int pc_cloud_storage_deposit(PCCloudStorage* s, uint16_t item) {
    if (s == NULL) return PC_CLOUD_STORAGE_ERROR;
    if (pc_cloud_storage_is_critter(item)) {
        return PC_CLOUD_STORAGE_CRITTER_BLOCKED;
    }
    if (!pc_cloud_storage_item_allowed(item)) {
        return PC_CLOUD_STORAGE_INVALID_ITEM;
    }
    if (s->count >= PC_CLOUD_STORAGE_MAX_ITEMS) {
        return PC_CLOUD_STORAGE_FULL;
    }

    s->items[s->count] = item;
    s->count++;
    return PC_CLOUD_STORAGE_OK;
}

int pc_cloud_storage_withdraw(PCCloudStorage* s, int index, uint16_t* out_item) {
    if (s == NULL || index < 0 || (uint32_t)index >= s->count) {
        return PC_CLOUD_STORAGE_EMPTY_SLOT;
    }
    if (out_item != NULL) {
        *out_item = s->items[index];
    }

    /* Shift remaining items left */
    size_t remaining = s->count - index - 1;
    if (remaining > 0) {
        memmove(&s->items[index], &s->items[index + 1], remaining * sizeof(uint16_t));
    }
    s->count--;
    s->items[s->count] = 0;
    return PC_CLOUD_STORAGE_OK;
}

int pc_cloud_storage_total_pages(const PCCloudStorage* s) {
    if (s == NULL || s->count == 0) return 1;
    return (int)((s->count + PC_CLOUD_STORAGE_PAGE_SIZE - 1) / PC_CLOUD_STORAGE_PAGE_SIZE);
}

int pc_cloud_storage_get_page_items(const PCCloudStorage* s, int page, uint16_t out_items[PC_CLOUD_STORAGE_PAGE_SIZE]) {
    if (out_items == NULL) return 0;
    memset(out_items, 0, PC_CLOUD_STORAGE_PAGE_SIZE * sizeof(uint16_t));
    if (s == NULL || page < 0) return 0;

    int total_pages = pc_cloud_storage_total_pages(s);
    if (page >= total_pages) return 0;

    int start = page * PC_CLOUD_STORAGE_PAGE_SIZE;
    int available = (int)s->count - start;
    int count = available > PC_CLOUD_STORAGE_PAGE_SIZE ? PC_CLOUD_STORAGE_PAGE_SIZE : (available < 0 ? 0 : available);

    for (int i = 0; i < count; i++) {
        out_items[i] = s->items[start + i];
    }
    return count;
}

/* Active Client Session Management */
void pc_cloud_storage_client_open(const char* uuid) {
    s_client_open = 1;
    s_client_page = 0;
    snprintf(s_client_uuid, sizeof(s_client_uuid), "%s", (uuid != NULL && uuid[0] != '\0') ? uuid : "default");
    pc_cloud_storage_load(NULL, s_client_uuid, &s_client_storage);
    printf("[STORAGE] Client opened cloud storage for UUID '%s' (%u items, %d pages)\n",
           s_client_uuid, (unsigned)s_client_storage.count, pc_cloud_storage_total_pages(&s_client_storage));
}

void pc_cloud_storage_client_close(void) {
    if (!s_client_open) return;
    pc_cloud_storage_save(NULL, s_client_uuid, &s_client_storage);
    printf("[STORAGE] Client closed cloud storage for UUID '%s' (persisted %u items)\n",
           s_client_uuid, (unsigned)s_client_storage.count);
    s_client_open = 0;
}

int pc_cloud_storage_client_is_open(void) {
    return s_client_open;
}

int pc_cloud_storage_client_page(void) {
    return s_client_page;
}

void pc_cloud_storage_client_next_page(void) {
    int max_p = pc_cloud_storage_total_pages(&s_client_storage);
    if (s_client_page < max_p - 1) {
        s_client_page++;
    }
}

void pc_cloud_storage_client_prev_page(void) {
    if (s_client_page > 0) {
        s_client_page--;
    }
}

void pc_cloud_storage_client_set_page(int page) {
    int max_p = pc_cloud_storage_total_pages(&s_client_storage);
    if (page >= 0 && page < max_p) {
        s_client_page = page;
    }
}

PCCloudStorage* pc_cloud_storage_client_get(void) {
    return s_client_open ? &s_client_storage : NULL;
}
