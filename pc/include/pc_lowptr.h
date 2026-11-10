#ifndef PC_LOWPTR_H
#define PC_LOWPTR_H

/* PC_LOW_ADDRESS_64 (C++ only): a 4-byte pointer that converts to/from T*.
 *
 * Some GameCube structures have a fixed 32-bit-pointer layout that is baked into a binary format or into the
 * allocator's arithmetic (JKRExpHeap::CMemBlock is a 0x10-byte header; JKRArchive::SDIFileEntry is a 0x14-byte
 * record inside the on-disc RARC file-entry table). With 8-byte pointers they would grow (0x18 each, measured) and
 * every array index / header offset would be wrong. The low-address build keeps all game memory below 4 GB, so these
 * particular fields are stored as u32 and every conversion is range-checked by PC_PTR32. */
#ifdef PC_LOW_ADDRESS_64
#ifdef __cplusplus

#include "pc_lowaddr.h"
#include "types.h"

/* C++-compiled decomp files are built with -Dthis=this_arg (decomp C headers use "this" as a parameter name);
 * the real keyword is needed inside this template. */
#pragma push_macro("this")
#undef this
#ifdef PC_GBI_INTERN
extern "C" unsigned int pc_gbi_intern(uintptr_t addr);
extern "C" uintptr_t pc_gbi_resolve(unsigned int packed);
#endif
extern "C++" {
template <class T>
struct PcLowPtr {
    u32 mAddr;
    PcLowPtr& operator=(T* p) {
#ifdef PC_GBI_INTERN
        mAddr = pc_gbi_intern((uintptr_t)p);
#else
        mAddr = PC_PTR32("PcLowPtr (fixed 32-bit layout field)", p);
#endif
        return *this;
    }
    operator T*() const {
#ifdef PC_GBI_INTERN
        return (T*)pc_gbi_resolve(mAddr);
#else
        return (T*)(uintptr_t)mAddr;
#endif
    }
    T* operator->() const { return (T*)(*this); }
};
} /* extern "C++" */
#pragma pop_macro("this")

#endif /* __cplusplus */
#endif /* PC_LOW_ADDRESS_64 */

#endif /* PC_LOWPTR_H */
