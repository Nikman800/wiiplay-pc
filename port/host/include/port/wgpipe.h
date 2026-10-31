/**
 * The GX write-gather pipe on the host (port/docs/HOST_BUILD.md §26).
 *
 * The Wii's GX SDK sends every command, register value and vertex to the
 * graphics processor by storing to WGPIPE (0xCC008000). On the host
 * WGPIPE_WRITE(member, value) appends the value to the current CPU FIFO,
 * big-endian, as the Wii's gather pipe would put it in memory:
 *
 *   - normally the FIFO is the host command stream: a gather buffer handed to
 *     a sink (the GX backend, libwiiplay_gx / Aurora) in order;
 *   - between GXBeginDisplayList and GXEndDisplayList it is the display list
 *     buffer itself (Wii memory in M0), exactly as on the Wii.
 *
 * A pointer member (`p`) is sent as its 32-bit emulated Wii address
 * (port/arena.h), like the Wii's 32-bit pointer. The Wii side of these
 * macros is include/port/wgpipe.h.
 */

#ifndef PORT_WGPIPE_H
#define PORT_WGPIPE_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write cursor of the current CPU FIFO (host gather buffer or display list). */
extern u8* port_wgpipe_cur;
extern u8* port_wgpipe_end;

/* Called when the cursor reaches the end: hands the gather buffer to the
   sink, or marks a display list as overflowed. Then writes the bytes. */
void PortWGPipeOverflow(const u8* bytes, u32 size);

static inline void port_wgpipe_put(const u8* bytes, u32 size) {
    if ((u32)(port_wgpipe_end - port_wgpipe_cur) >= size) {
        u32 i;
        for (i = 0; i < size; i++) {
            port_wgpipe_cur[i] = bytes[i];
        }
        port_wgpipe_cur += size;
    } else {
        PortWGPipeOverflow(bytes, size);
    }
}

static inline void port_wgpipe_u8(u8 v) {
    port_wgpipe_put(&v, 1);
}

static inline void port_wgpipe_u16(u16 v) {
    u8 b[2];
    b[0] = (u8)(v >> 8);
    b[1] = (u8)v;
    port_wgpipe_put(b, 2);
}

static inline void port_wgpipe_u32(u32 v) {
    u8 b[4];
    b[0] = (u8)(v >> 24);
    b[1] = (u8)(v >> 16);
    b[2] = (u8)(v >> 8);
    b[3] = (u8)v;
    port_wgpipe_put(b, 4);
}

static inline void port_wgpipe_f32(f32 f) {
    union {
        f32 f;
        u32 u;
    } v;
    v.f = f;
    port_wgpipe_u32(v.u);
}

/* The 32-bit emulated address of an M0 pointer (port_vaddr; aborts outside M0). */
u32 PortWGPipeAddress(const void* p);

static inline void port_wgpipe_ptr(const void* p) {
    port_wgpipe_u32(PortWGPipeAddress(p));
}

/* n floats in order (the paired-single matrix and projection writers). */
static inline void port_wgpipe_f32s(const f32* src, u32 n) {
    u32 i;
    for (i = 0; i < n; i++) {
        port_wgpipe_f32(src[i]);
    }
}

/* WGPIPE.member = value: one helper per union member (GXHardware.h). */
#define WGPIPE_WRITE(member, value) PORT_WGPIPE_##member(value)
#define PORT_WGPIPE_c(v) port_wgpipe_u8((u8)(v))
#define PORT_WGPIPE_uc(v) port_wgpipe_u8((u8)(v))
#define PORT_WGPIPE_s(v) port_wgpipe_u16((u16)(v))
#define PORT_WGPIPE_us(v) port_wgpipe_u16((u16)(v))
#define PORT_WGPIPE_i(v) port_wgpipe_u32((u32)(v))
#define PORT_WGPIPE_ui(v) port_wgpipe_u32((u32)(v))
#define PORT_WGPIPE_f(v) port_wgpipe_f32((f32)(v))
#define PORT_WGPIPE_p(v) port_wgpipe_ptr((const void*)(v))

/* Static data the graphics processor reads (GXInit's default texture, its
   placeholder array base): an M0 copy made once per address, so it has an
   emulated address (src/host_arena.cpp). */
void* PortArenaStatic(const void* p, u32 size);
#define PORT_GP_STATIC(p, size) PortArenaStatic((p), (size))
/* Static arrays of 16- or 32-bit values: the M0 copy is big-endian. */
void* PortArenaStaticBE(const void* p, u32 size, u32 unit);
/* The static copies made so far: emulated addresses and sizes (for tests). */
u32 PortArenaStaticList(u32* addrs, u32* sizes, u32 max);
#define PORT_GP_STATIC_BE16(p, size) PortArenaStaticBE((p), (size), 2)
#define PORT_GP_STATIC_BE32(p, size) PortArenaStaticBE((p), (size), 4)

/* GP-visible scratch memory in M0 for data the CPU keeps in native byte order
   but the graphics processor reads by address: valid until the next
   GXDrawDone / GXCopyDisp (when every command sent so far has run). */
void* PortGPScratch(u32 size);
/* The scratch block's emulated address range (0 before the first use). */
void PortGPScratchRange(u32* base, u32* size);
/* g3d's view position / normal / environment matrix arrays (DrawResMdlDirectly):
   big-endian copies of `num` matrices each in PortGPScratch, set as the
   GX_POS_MTX_ARRAY / GX_NRM_MTX_ARRAY / GX_TEX_MTX_ARRAY bases (NULL: unchanged). */
void PortGXSetViewMtxArrays(u32 num, const void* pos, const void* nrm, const void* env);
/* n GXLightObj (runtime objects, native byte order) for indexed light loads:
   a copy in PortGPScratch with the hardware's layout (big-endian words). */
void* PortGPLightObjs(const void* objs, u32 n);
#define PORT_GP_LIGHT_OBJS(p, n) PortGPLightObjs((p), (n))

#ifdef __cplusplus
}
#endif

#endif
