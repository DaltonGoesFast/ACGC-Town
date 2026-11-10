#ifndef PC_SAVE_WIRE_H
#define PC_SAVE_WIRE_H

#include "m_common_data.h"

/* The dedicated server's Save_t, measured on i386 -malign-double and armeabi-v7a. */
#define PC_SAVE_WIRE_BYTES 0x242A0u

/* Copy one town between the native struct and that 148128-byte image.
 * Town fetch, the memory card, and any buffer that is a GCI Save_t use these. */
void pc_save_wire_store(const Save_t* native, void* wire);
void pc_save_wire_load(const void* wire, Save_t* native);

#endif
