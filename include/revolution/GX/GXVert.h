#ifndef RVL_SDK_GX_VERT_H
#define RVL_SDK_GX_VERT_H
#include <types.h>

#include <revolution/GX/GXHardware.h>
#ifdef __cplusplus
extern "C" {
#endif

static inline void GXCmd1u8(u8 uc) {
    WGPIPE_WRITE(uc, uc);
}

static inline void GXCmd1u16(u16 us) {
    WGPIPE_WRITE(us, us);
}

static inline void GXCmd1u32(u32 ul) {
    WGPIPE_WRITE(ui, ul);
}

static inline void GXPosition3f32(f32 x, f32 y, f32 z) {
    WGPIPE_WRITE(f, x);
    WGPIPE_WRITE(f, y);
    WGPIPE_WRITE(f, z);
}

static inline void GXPosition3u8(u8 x, u8 y, u8 z) {
    WGPIPE_WRITE(uc, x);
    WGPIPE_WRITE(uc, y);
    WGPIPE_WRITE(uc, z);
}

static inline void GXPosition3s8(s8 x, s8 y, s8 z) {
    WGPIPE_WRITE(c, x);
    WGPIPE_WRITE(c, y);
    WGPIPE_WRITE(c, z);
}

static inline void GXPosition3u16(u16 x, u16 y, u16 z) {
    WGPIPE_WRITE(us, x);
    WGPIPE_WRITE(us, y);
    WGPIPE_WRITE(us, z);
}

static inline void GXPosition3s16(s16 x, s16 y, s16 z) {
    WGPIPE_WRITE(s, x);
    WGPIPE_WRITE(s, y);
    WGPIPE_WRITE(s, z);
}

static inline void GXPosition2f32(f32 x, f32 y) {
    WGPIPE_WRITE(f, x);
    WGPIPE_WRITE(f, y);
}

static inline void GXPosition2u8(u8 x, u8 y) {
    WGPIPE_WRITE(uc, x);
    WGPIPE_WRITE(uc, y);
}

static inline void GXPosition2s8(s8 x, s8 y) {
    WGPIPE_WRITE(c, x);
    WGPIPE_WRITE(c, y);
}

static inline void GXPosition2u16(u16 x, u16 y) {
    WGPIPE_WRITE(us, x);
    WGPIPE_WRITE(us, y);
}

static inline void GXPosition2s16(s16 x, s16 y) {
    WGPIPE_WRITE(s, x);
    WGPIPE_WRITE(s, y);
}

static inline void GXPosition1x16(u16 us) {
    WGPIPE_WRITE(us, us);
}

static inline void GXPosition1x8(u8 uc) {
    WGPIPE_WRITE(uc, uc);
}

static inline void GXNormal3f32(f32 x, f32 y, f32 z) {
    WGPIPE_WRITE(f, x);
    WGPIPE_WRITE(f, y);
    WGPIPE_WRITE(f, z);
}

static inline void GXNormal3u16(s16 x, s16 y, s16 z) {
    WGPIPE_WRITE(us, x);
    WGPIPE_WRITE(us, y);
    WGPIPE_WRITE(us, z);
}

static inline void GXNormal3s16(s16 x, s16 y, s16 z) {
    WGPIPE_WRITE(s, x);
    WGPIPE_WRITE(s, y);
    WGPIPE_WRITE(s, z);
}

static inline void GXNormal3u8(u8 x, u8 y, u8 z) {
    WGPIPE_WRITE(uc, x);
    WGPIPE_WRITE(uc, y);
    WGPIPE_WRITE(uc, z);
}

static inline void GXNormal3s8(s8 x, s8 y, s8 z) {
    WGPIPE_WRITE(c, x);
    WGPIPE_WRITE(c, y);
    WGPIPE_WRITE(c, z);
}

static inline void GXNormal1x16(u16 us) {
    WGPIPE_WRITE(us, us);
}

static inline void GXNormal1x8(u8 uc) {
    WGPIPE_WRITE(uc, uc);
}

static inline void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
    WGPIPE_WRITE(uc, r);
    WGPIPE_WRITE(uc, g);
    WGPIPE_WRITE(uc, b);
    WGPIPE_WRITE(uc, a);
}

static inline void GXColor1u32(u32 color) {
    WGPIPE_WRITE(ui, color);
}

static inline void GXColor3u8(u8 r, u8 g, u8 b) {
    WGPIPE_WRITE(uc, r);
    WGPIPE_WRITE(uc, g);
    WGPIPE_WRITE(uc, b);
}

static inline void GXColor1u16(u16 us) {
    WGPIPE_WRITE(us, us);
}

static inline void GXColor1x16(u16 us) {
    WGPIPE_WRITE(us, us);
}

static inline void GXColor1x8(u8 uc) {
    WGPIPE_WRITE(uc, uc);
}

static inline void GXTexCoord2f32(f32 x, f32 y) {
    WGPIPE_WRITE(f, x);
    WGPIPE_WRITE(f, y);
}

static inline void GXTexCoord2s16(s16 x, s16 y) {
    WGPIPE_WRITE(s, x);
    WGPIPE_WRITE(s, y);
}

static inline void GXTexCoord2u16(u16 x, u16 y) {
    WGPIPE_WRITE(us, x);
    WGPIPE_WRITE(us, y);
}

static inline void GXTexCoord2s8(s8 x, s8 y) {
    WGPIPE_WRITE(c, x);
    WGPIPE_WRITE(c, y);
}

static inline void GXTexCoord2u8(u8 x, u8 y) {
    WGPIPE_WRITE(uc, x);
    WGPIPE_WRITE(uc, y);
}

static inline void GXTexCoord1f32(f32 f) {
    WGPIPE_WRITE(f, f);
}

static inline void GXTexCoord1s16(s16 s) {
    WGPIPE_WRITE(s, s);
}

static inline void GXTexCoord1u16(u16 us) {
    WGPIPE_WRITE(us, us);
}

static inline void GXTexCoord1s8(s8 c) {
    WGPIPE_WRITE(c, c);
}

static inline void GXTexCoord1u8(u8 uc) {
    WGPIPE_WRITE(uc, uc);
}

static inline void GXTexCoord1x16(u16 us) {
    WGPIPE_WRITE(us, us);
}

static inline void GXTexCoord1x8(u8 uc) {
    WGPIPE_WRITE(uc, uc);
}

#ifdef __cplusplus
}
#endif
#endif
