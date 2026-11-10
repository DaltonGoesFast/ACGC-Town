#ifndef PC_LOWHEAP_H
#define PC_LOWHEAP_H

#include <stddef.h>

/* Bump a region that stays allocated for the life of the process, inside the below-4GB heap.
 * pc_lowheap_ok is 0 when Android would not map that range. The error string says why. */
int pc_lowheap_ok(void);
const char* pc_lowheap_error(void);
void* pc_lowheap_reserve(size_t bytes);
/* Call fn on a 16-byte-aligned stack whose top is stack_top. Same thread, so JNI stays valid. */
int pc_run_on_stack(void* stack_top, int (*fn)(int, char**), int argc, char** argv);

#endif
