/**
 * Host replacement for include/revolution/OS/OSAddress.h (M0,
 * port/docs/POINTERS.md §2).
 *
 * Physical and cached addresses are emulated Wii addresses in the host
 * MEM1/MEM2 block (port/arena.h). OSCachedToPhysical returns the physical
 * address as a u32 (the Wii version returns it as a pointer; every caller
 * casts it to u32), and OSPhysicalToCached/Uncached map back to host
 * pointers. The uncached mirror is the same host memory.
 */

#ifndef RVL_SDK_OS_ADDRESS_H
#define RVL_SDK_OS_ADDRESS_H
#include <types.h>

#include <port/arena.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void* OSPhysicalToCached(u32 ofs) {
    return port_hptr(ofs + 0x80000000u);
}

static inline void* OSPhysicalToUncached(u32 ofs) {
    return port_hptr(ofs + 0x80000000u);
}

static inline u32 OSCachedToPhysical(const void* ofs) {
    return port_vaddr(ofs) & 0x3FFFFFFFu;
}

#ifdef __cplusplus
}
#endif
#endif
