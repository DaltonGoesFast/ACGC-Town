/* Round-trip the 148128-byte town image. On the 32-bit server ABI the wire bytes
 * are the struct itself. On LP64 the same bytes must come back field for field. */
#include "pc_save_wire.h"

#include <stdio.h>
#include <string.h>

static unsigned hash_bytes(const unsigned char* p, unsigned n) {
    unsigned h = 2166136261u;
    unsigned i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

int main(void) {
    Save_t a, b;
    unsigned char wire[PC_SAVE_WIRE_BYTES];
    unsigned i;
    memset(&a, 0x5A, sizeof(a));
    a.scene_no = 0x12345678;
    a.melody = 0x1122334455667788ull;
    a.house_arrangement = 0xA5;
    a.private_data[0].inventory.loan = 398000u;
    a.homes[1].ownerID.player_id = 0xBEEF;
    for (i = 0; i < sizeof(a); i++) {
        ((unsigned char*)&a)[i] ^= (unsigned char)(i * 17u);
    }
    /* The named fields are set again so the hash covers values, not only the scramble. */
    a.scene_no = 7;
    a.house_arrangement = 0x1B;
    a.melody = 42;
    pc_save_wire_store(&a, wire);
    pc_save_wire_load(wire, &b);
    if (memcmp(&a, &b, sizeof(a)) != 0) {
        fprintf(stderr, "FAIL round-trip memcmp\n");
        return 1;
    }
    if (sizeof(Save_t) == PC_SAVE_WIRE_BYTES && memcmp(&a, wire, PC_SAVE_WIRE_BYTES) != 0) {
        fprintf(stderr, "FAIL wire image is not the native struct\n");
        return 1;
    }
    if (b.scene_no != 7 || b.house_arrangement != 0x1B || b.melody != 42) {
        fprintf(stderr, "FAIL field values\n");
        return 1;
    }
    printf("save-wire ok sizeof=%u hash=%08x ptr=%u\n",
           (unsigned)sizeof(Save_t), hash_bytes(wire, PC_SAVE_WIRE_BYTES), (unsigned)sizeof(void*));
    return 0;
}
