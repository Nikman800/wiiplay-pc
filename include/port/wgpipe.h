/**
 * The GX write-gather pipe: Wii (matching) build.
 *
 * Everything the CPU sends to the graphics processor is a store to WGPIPE
 * (0xCC008000, include/revolution/GX/GXHardware.h). The PC port has no such
 * address: there WGPIPE_WRITE(member, value) appends the value, big-endian,
 * to the host's command stream, and a pointer is sent as its 32-bit emulated
 * Wii address (port/host/include/port/wgpipe.h, port/docs/HOST_BUILD.md §26).
 *
 * Here the macros expand to the stores they replace, so CodeWarrior compiles
 * the same code as before.
 */

#ifndef PORT_WGPIPE_H
#define PORT_WGPIPE_H

#if defined(PORT_HOST)
#error "host builds must find port/host/include/port/wgpipe.h first"
#endif

/* WGPIPE.member = value */
#define WGPIPE_WRITE(member, value) (WGPIPE.member = (value))

/* Static data the graphics processor reads (a texture, an array base). */
#define PORT_GP_STATIC(p, size) (p)
/* The same for an array of 16- or 32-bit values (vertex arrays, Z texels): the
   host's copy is big-endian, as the graphics processor reads it. */
#define PORT_GP_STATIC_BE16(p, size) (p)
#define PORT_GP_STATIC_BE32(p, size) (p)
/* An array of n GXLightObj the graphics processor loads by index (the host
   copies it into the hardware's byte order). */
#define PORT_GP_LIGHT_OBJS(p, n) (p)

#endif
