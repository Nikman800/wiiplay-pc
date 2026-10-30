/**
 * Pointer slots inside loaded files: Wii (matching) build.
 *
 * Some loaders turn 32-bit offsets inside a loaded file into pointers and
 * store them back into the same 4-byte slot (nw4r::ut::ResFont::Rebuild, TPL,
 * ef, g3d). The PC port declares those members RES_PTR(T); on a 64-bit or
 * little-endian host that is a 4-byte class holding a big-endian emulated Wii
 * address (port/host/include/port/resptr.h, see port/docs/POINTERS.md).
 *
 * Here every macro expands to exactly the type or expression it replaces, so
 * CodeWarrior compiles the same code as before.
 */

#ifndef PORT_RESPTR_H
#define PORT_RESPTR_H

#if defined(PORT_HOST)
#error "host builds must find port/host/include/port/resptr.h first"
#endif

#include <port/be.h>

/* 1 when RES_PTR is a wrapper class (host), 0 when it is a plain pointer. */
#define PORT_RES_PTR_WRAPPED 0

#define RES_PTR(T) T*
#define RES_PTR_GET(slot) (slot)
#define RES_PTR_SET(slot, p) ((slot) = (p))

#ifdef __cplusplus
#define RES_PTR_RAW(slot) reinterpret_cast<u32>(slot)
#define PORT_PTR_TO_U32(p) reinterpret_cast<u32>(p)
#define PORT_U32_TO_PTR(T, v) reinterpret_cast<T>(v)
#else
#define RES_PTR_RAW(slot) ((u32)(slot))
#define PORT_PTR_TO_U32(p) ((u32)(p))
#define PORT_U32_TO_PTR(T, v) ((T)(v))
#endif

#define PORT_STORE_PTR32(u32slot, p) ((u32slot) = PORT_PTR_TO_U32(p))

#endif
