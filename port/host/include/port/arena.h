/**
 * M0: emulated Wii address space (host build only; port/docs/POINTERS.md §2).
 *
 * The host reserves MEM1 and MEM2 as one contiguous block of host memory and
 * gives every byte in it a 32-bit "Wii virtual address":
 *
 *   block offset 0x00000000 .. 0x017FFFFF  MEM1 (24 MiB)  0x80000000 + off
 *   block offset 0x10000000 .. 0x13FFFFFF  MEM2 (64 MiB)  0x90000000 + off
 *
 * The gap between them is reserved but inaccessible. The low 0x3100 bytes of
 * MEM1 stand for the OS globals area and are never handed out, so the vaddr
 * 0 is free to mean NULL.
 *
 * Every EGG heap allocation and every global operator new on the host comes
 * from this block (port/host/src/host_arena.cpp), so every file buffer is in
 * it and RES_PTR slots (port/resptr.h) can hold 32-bit addresses.
 *
 *   port_vaddr(p)  host pointer -> u32 Wii address (NULL -> 0)
 *   port_hptr(v)   u32 Wii address -> host pointer (0 -> NULL)
 *
 * Both check that the address lies in the block and abort otherwise.
 */

#ifndef PORT_ARENA_H
#define PORT_ARENA_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#include <types.h>

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define PORT_ARENA_SIZE 0x14000000u
#define PORT_ARENA_VBASE 0x80000000u
#define PORT_MEM1_OFFSET 0x00000000u
#define PORT_MEM1_SIZE 0x01800000u
#define PORT_MEM2_OFFSET 0x10000000u
#define PORT_MEM2_SIZE 0x04000000u
/* OS globals area at the bottom of MEM1 (never allocated). */
#define PORT_MEM1_RESERVED 0x3100u

/** Base of the block; NULL until PortArenaInit (called lazily by the allocator). */
extern u8* port_arena_base;

/** Reserves and maps the block. Idempotent. */
void PortArenaInit(void);

/** Arena allocator (MEM2 first, then MEM1). `align` may be negative (Wii
 * "allocate from the tail"); only its magnitude is used. Returns NULL when
 * both arenas are full. */
void* PortArenaAlloc(size_t size, int align);
void PortArenaFree(void* p);
/** Bytes currently allocated (including headers and padding). */
size_t PortArenaUsed(void);

/** Lo/hi bounds of the arenas, as OSGetMEM1ArenaLo/Hi etc. would return. */
void* PortArenaMEM1Lo(void);
void* PortArenaMEM1Hi(void);
void* PortArenaMEM2Lo(void);
void* PortArenaMEM2Hi(void);

/** Aborts with a message; called by the inline converters on a bad address. */
void PortArenaBadPointer(const void* p);
void PortArenaBadAddress(u32 v);

static inline int port_in_arena(const void* p) {
    return port_arena_base != NULL && (const u8*)p >= port_arena_base &&
           (uintptr_t)((const u8*)p - port_arena_base) < PORT_ARENA_SIZE;
}

static inline u32 port_vaddr(const void* p) {
    uintptr_t off;
    if (p == NULL) {
        return 0;
    }
    off = (uintptr_t)((const u8*)p - port_arena_base);
    if (port_arena_base == NULL || (const u8*)p < port_arena_base ||
        off >= PORT_ARENA_SIZE) {
        PortArenaBadPointer(p);
    }
    return PORT_ARENA_VBASE + (u32)off;
}

static inline void* port_hptr(u32 v) {
    u32 off;
    if (v == 0) {
        return NULL;
    }
    off = v - PORT_ARENA_VBASE;
    if (port_arena_base == NULL || off >= PORT_ARENA_SIZE) {
        PortArenaBadAddress(v);
    }
    return port_arena_base + off;
}

#ifdef __cplusplus
}
#endif

#endif
