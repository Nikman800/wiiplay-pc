/**
 * Host GXInitLight* / GXGetLight* (port/docs/HOST_BUILD.md §20).
 *
 * The CPU half of src/revolution/GX/GXLight.c: these only fill in a
 * GXLightObj (GXLightObjImpl in include/revolution/GX/GXInternal.h), which
 * g3d's LightObj keeps in its runtime data (G3DState's light state is built by a
 * static initialiser). The object is runtime data, so its words are native; the
 * GPU half (GXLoadLightObjImm, the channel registers) is the Phase 7 GX layer's
 * (src/host_gfx_stubs.c aborts for GXLoadLightObjImm). The arithmetic is
 * GXLight.c's, in single precision.
 */
#include <types.h>

#include <revolution/GX.h>
#include <revolution/GX/GXInternal.h>

#include <cmath>
#include <cstring>

extern "C" {

void GXInitLightAttn(GXLightObj* light, f32 aa, f32 ab, f32 ac, f32 ka, f32 kb, f32 kc) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    impl->aa = aa;
    impl->ab = ab;
    impl->ac = ac;
    impl->ka = ka;
    impl->kb = kb;
    impl->kc = kc;
}

void GXInitLightAttnA(GXLightObj* light, f32 a, f32 b, f32 c) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    impl->aa = a;
    impl->ab = b;
    impl->ac = c;
}

void GXInitLightAttnK(GXLightObj* light, f32 a, f32 b, f32 c) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    impl->ka = a;
    impl->kb = b;
    impl->kc = c;
}

void GXInitLightSpot(GXLightObj* light, f32 angle, GXSpotFn fn) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    f32 a, b, c, d;

    if (angle <= 0.0f || angle > 90.0f) {
        fn = GX_SP_OFF;
    }

    f32 rad = cosf(static_cast<f32>(M_PI * angle / 180.0f));

    switch (fn) {
    case GX_SP_FLAT:
        a = -1000.0f * rad;
        b = 1000.0f;
        c = 0.0f;
        break;
    case GX_SP_COS:
        b = 1.0f / (1.0f - rad);
        a = -rad * b;
        c = 0.0f;
        break;
    case GX_SP_COS2:
        c = 1.0f / (1.0f - rad);
        a = 0.0f;
        b = -rad * c;
        break;
    case GX_SP_SHARP:
        d = 1.0f / ((1.0f - rad) * (1.0f - rad));
        a = (rad * (rad - 2.0f)) * d;
        b = 2.0f * d;
        c = -d;
        break;
    case GX_SP_RING1:
        d = 1.0f / ((1.0f - rad) * (1.0f - rad));
        c = -4.0f * d;
        a = c * rad;
        b = 4.0f * (1.0f + rad) * d;
        break;
    case GX_SP_RING2:
        d = 1.0f / ((1.0f - rad) * (1.0f - rad));
        a = 1.0f - 2.0f * rad * rad * d;
        b = 4.0f * rad * d;
        c = -2.0f * d;
        break;
    case GX_SP_OFF:
    default:
        a = 1.0f;
        b = 0.0f;
        c = 0.0f;
        break;
    }

    impl->aa = a;
    impl->ab = b;
    impl->ac = c;
}

void GXInitLightDistAttn(GXLightObj* light, f32 distance, f32 brightness, GXDistAttnFn fn) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    f32 a, b, c;

    if (distance < 0.0f) {
        fn = GX_DA_OFF;
    }
    if (brightness <= 0.0f || brightness >= 1.0f) {
        fn = GX_DA_OFF;
    }

    switch (fn) {
    case GX_DA_GENTLE:
        a = 1.0f;
        b = (1.0f - brightness) / (brightness * distance);
        c = 0.0f;
        break;
    case GX_DA_MEDIUM:
        a = 1.0f;
        b = (0.5f * (1.0f - brightness)) / (brightness * distance);
        c = (0.5f * (1.0f - brightness)) / (distance * (brightness * distance));
        break;
    case GX_DA_STEEP:
        a = 1.0f;
        b = 0.0f;
        c = (1.0f - brightness) / (distance * (brightness * distance));
        break;
    case GX_DA_OFF:
    default:
        a = 1.0f;
        b = 0.0f;
        c = 0.0f;
        break;
    }

    impl->ka = a;
    impl->kb = b;
    impl->kc = c;
}

void GXInitLightPos(GXLightObj* light, f32 x, f32 y, f32 z) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    impl->posX = x;
    impl->posY = y;
    impl->posZ = z;
}

void GXGetLightPos(const GXLightObj* light, f32* x, f32* y, f32* z) {
    const GXLightObjImpl* impl = reinterpret_cast<const GXLightObjImpl*>(light);
    *x = impl->posX;
    *y = impl->posY;
    *z = impl->posZ;
}

void GXInitLightDir(GXLightObj* light, f32 x, f32 y, f32 z) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    impl->dirX = -x;
    impl->dirY = -y;
    impl->dirZ = -z;
}

void GXGetLightDir(const GXLightObj* light, f32* x, f32* y, f32* z) {
    const GXLightObjImpl* impl = reinterpret_cast<const GXLightObjImpl*>(light);
    *x = -impl->dirX;
    *y = -impl->dirY;
    *z = -impl->dirZ;
}

void GXInitSpecularDir(GXLightObj* light, f32 x, f32 y, f32 z) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    f32 dirX = -x, dirY = -y, dirZ = 1.0f + -z;

    f32 mag = dirX * dirX + dirY * dirY + dirZ * dirZ;
    if (mag != 0.0f) {
        mag = 1.0f / sqrtf(mag);
    }

    impl->dirX = dirX * mag;
    impl->dirY = dirY * mag;
    impl->dirZ = dirZ * mag;
    impl->posX = -999999999999999999.0f * x;
    impl->posY = -999999999999999999.0f * y;
    impl->posZ = -999999999999999999.0f * z;
}

void GXInitLightColor(GXLightObj* light, GXColor color) {
    GXLightObjImpl* impl = reinterpret_cast<GXLightObjImpl*>(light);
    std::memcpy(&impl->color, &color, sizeof(GXColor));
}

} // extern "C"
