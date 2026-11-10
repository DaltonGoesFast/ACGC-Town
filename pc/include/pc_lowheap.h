#ifndef PC_LOWHEAP_H
#define PC_LOWHEAP_H

#include <stddef.h>

/* Bump a region that stays allocated for the life of the process, inside the below-4GB heap. */
void* pc_lowheap_reserve(size_t bytes);

#endif
