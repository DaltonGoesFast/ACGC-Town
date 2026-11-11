#ifndef PC_SAVE_WIRE_H
#define PC_SAVE_WIRE_H

#include "m_common_data.h"

/* The dedicated server's Save_t, measured on i386 -malign-double and armeabi-v7a. */
#define PC_SAVE_WIRE_BYTES 0x242A0u

/* Copy one town between the native struct and that 148128-byte image.
 * Town fetch, the memory card, and any buffer that is a GCI Save_t use these. */
void pc_save_wire_store(const Save_t* native, void* wire);
void pc_save_wire_load(const void* wire, Save_t* native);

/* Write the 148128-byte town image and fsync it. No write when the save is not ready.
 * Called when the app is backgrounded so a kill does not drop the last town. */
int pc_save_flush_background(void);

/* Play Offline. Points Card A at save/offline (never save/card_a and never a server
 * town cache) and reloads that file. Returns 0 if a different town dir is already
 * set or networking is already running, and then nothing is changed. */
int pc_offline_solo_begin(void);
int pc_offline_solo_active(void);

#endif
