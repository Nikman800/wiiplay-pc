/**
 * Host replacement for include/revolution/OS/OSMemory.h (port/docs/HOST_BUILD.md §29).
 *
 * The region tests look at the emulated Wii address of the pointer (port_vaddr, M0): a host
 * pointer does not fit in a u32. Everything else is as in the decomp's header.
 */

#ifndef RVL_SDK_OS_MEMORY_H
#define RVL_SDK_OS_MEMORY_H
#include <types.h>

#include <port/arena.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_MEM_KB_TO_B(mb) ((mb) * 1024)
#define OS_MEM_B_TO_KB(mb) ((mb) / 1024)

#define OS_MEM_MB_TO_B(mb) ((mb) * 1024 * 1024)
#define OS_MEM_B_TO_MB(mb) ((mb) / 1024 / 1024)

#define OSIsMEM1Region(addr) ((port_vaddr((const void*)(addr)) & 0x30000000) == 0)
#define OSIsMEM2Region(addr) ((port_vaddr((const void*)(addr)) & 0x30000000) == 0x10000000)

#define OS_IS_MEM1_REGION(addr) OSIsMEM1Region(addr)
#define OS_IS_MEM2_REGION(addr) OSIsMEM2Region(addr)

u32 OSGetPhysicalMem1Size(void);
u32 OSGetPhysicalMem2Size(void);
u32 OSGetConsoleSimulatedMem1Size(void);
u32 OSGetConsoleSimulatedMem2Size(void);
void __OSInitMemoryProtection(void);
void OSProtectRange(u32 chan, void* pAddr, u32 nBytes, u32 control);

#ifdef __cplusplus
}
#endif
#endif
