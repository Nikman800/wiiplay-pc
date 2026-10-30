/**
 * Big-endian field types: Wii (matching) build.
 *
 * The PC port reads on-disc structs through big-endian wrapper types (see
 * port/host/include/port/be.h, which the host build finds first on its include
 * path). The Wii is big-endian, so here every name is a macro for the plain
 * type and the accessors are the identity: code that uses them compiles to
 * exactly what it did before.
 */

#ifndef PORT_BE_H
#define PORT_BE_H

#if defined(PORT_HOST)
#error "host builds must find port/host/include/port/be.h first"
#endif

#define PORT_BE_WRAPPED 0

#define be_u16 u16
#define be_s16 s16
#define be_u32 u32
#define be_s32 s32
#define be_u64 u64
#define be_s64 s64
#define be_f32 f32
#define be_f64 f64
#define BE_ENUM(E, Storage) E

#define BE_GET(field) (field)
#define BE_SET(field, value) ((field) = (value))
// A value as a file stores it (C++ only; on the host a big-endian temporary).
#define BE_STORED(T, v) (v)

// Byte i (0 = most significant) of a size-byte integer seen through a union.
#define BE_BYTE(i, size) i

// A heap size that must grow on the host, where EGG/MEM heap headers hold 64-bit pointers.
#define PORT_HOST_SIZE(wii, host) (wii)

#endif
