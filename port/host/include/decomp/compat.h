/**
 * Host (PC) replacement for include/decomp/compat.h.
 *
 * The original maps CodeWarrior-only syntax for IDE parsers (clangd,
 * Intellisense). A real host build needs the same treatment, but with working
 * intrinsics instead of `0` placeholders, so this file is used for every host
 * compile (port/host/include is first on the include path).
 */

#ifndef COMPAT_H
#define COMPAT_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

// Treat the host build like a parser build where the decomp checks for one.
#define COMPAT_ANY

#define register
#define ASM(...)
#define ASM_VOLATILE(...)
// Paired-single helpers for the ASM_PS host forms below.
#include <port/ps_forms.h>
#define DECLTYPE(x) decltype(x)
#define DECL_ADDRESS(...) __attribute__((weak))
#define FLEXIBLE_ARRAY(NAME) NAME[]

#define __alloca __builtin_alloca
#define __memclr(var, size) __builtin_memset((var), 0, (size))

// CodeWarrior's __declspec(x): each x the host build meets gets its own
// PORT_DECLSPEC_x, so an unhandled one (e.g. section ".ctors") is a compile error.
#define __declspec(x) PORT_DECLSPEC_##x
#define PORT_DECLSPEC_novtable

// Gekko fused multiply-add intrinsics (single precision, rounded once, as
// fmadds etc. are).
static inline float __fmadds(float a, float b, float c) {
    return __builtin_fmaf(a, b, c);
}
static inline float __fmsubs(float a, float b, float c) {
    return __builtin_fmaf(a, b, -c);
}
static inline float __fnmadds(float a, float b, float c) {
    return -__builtin_fmaf(a, b, c);
}
static inline float __fnmsubs(float a, float b, float c) {
    return -__builtin_fmaf(a, b, -c);
}

// nw4r::math's paired-single VEC3 inlines (include/nw4r/math/math_types.h):
// `ASM_PS(name) ( ...asm... )` becomes the same operations in C on the
// function's own parameters and locals, and the asm text is dropped. The
// fused multiply-adds (ps_madd, ps_madds0) round once, as on the Wii; the
// two-way sums (ps_sum0) are plain single-precision adds.
#define ASM_PS(name) PORT_PS_##name
#define PORT_PS_VEC3Add(...)                                                   \
    ((pOut)->x = (pA)->x + (pB)->x, (pOut)->y = (pA)->y + (pB)->y,             \
     (pOut)->z = (pA)->z + (pB)->z);
#define PORT_PS_VEC3Sub(...)                                                   \
    ((pOut)->x = (pA)->x - (pB)->x, (pOut)->y = (pA)->y - (pB)->y,             \
     (pOut)->z = (pA)->z - (pB)->z);
#define PORT_PS_VEC3Scale(...)                                                 \
    ((pOut)->x = (pIn)->x * (scale), (pOut)->y = (pIn)->y * (scale),           \
     (pOut)->z = (pIn)->z * (scale));
#define PORT_PS_VEC3Lerp(...)                                                  \
    ((pOut)->x = __fmadds((pVec2)->x - (pVec1)->x, (t), (pVec1)->x),           \
     (pOut)->y = __fmadds((pVec2)->y - (pVec1)->y, (t), (pVec1)->y),           \
     (pOut)->z = __fmadds((pVec2)->z - (pVec1)->z, (t), (pVec1)->z));
#define PORT_PS_VEC3Dot(...)                                                   \
    (dot = __fmadds((pA)->x, (pB)->x, (pA)->y * (pB)->y) + (pA)->z * (pB)->z);
#define PORT_PS_VEC3LenSq(...)                                                 \
    (work2 = __fmadds((pVec)->z, (pVec)->z, (pVec)->x * (pVec)->x) +           \
             (pVec)->y * (pVec)->y);

// Gekko quantized store (psq_st) of one float to an integer type, as the GQRs
// set by OSInitFastCast / g3d's InitFastCast use them: scale by 2^scale, clamp
// to the type's range, truncate toward zero (Dolphin's ScaleAndClamp). NaN is
// undefined on the hardware; it gives 0 here.
static inline long port_psq_quantize(float v, int scale, long lo, long hi) {
    float s = v * (float)(1L << scale);
    if (!(s == s)) {
        return 0;
    }
    if (s <= (float)lo) {
        return lo;
    }
    if (s >= (float)hi) {
        return hi;
    }
    return (long)s;
}

// OSFastCast.h (GQR2..GQR5: u8, u16, s8, s16, scale 0): psq_l / psq_st.
#define PORT_PS___OSu8tof32(...) (ret = (f32) * (in));
#define PORT_PS___OSu16tof32(...) (ret = (f32) * (arg));
#define PORT_PS___OSs16tof32(...) (ret = (f32) * (arg));
#define PORT_PS___OSf32tou8(...) (*(u8*)ptr = (u8)port_psq_quantize(arg, 0, 0, 0xFF));
#define PORT_PS___OSf32tou16(...) (*(u16*)ptr = (u16)port_psq_quantize(arg, 0, 0, 0xFFFF));
#define PORT_PS___OSf32tos16(...) (*(s16*)ptr = (s16)port_psq_quantize(arg, 0, -0x8000, 0x7FFF));

// nw4r::math (math_arithmetic.h). FInv is fres plus one Newton-Raphson step
// on the Wii (Rev 1), within an ulp or so of 1/x; the host divides.
#define PORT_PS_FAbs(...) (ax = __builtin_fabsf(x));
#define PORT_PS_FInv(...) (work0 = 1.0f / (x));
#define PORT_PS_FSelect(...) (ret = (value) >= 0.0f ? (ge_zero) : (lt_zero));
// math_types.cpp FSEL_MAX / FSEL_MIN (VEC3Maximize / VEC3Minimize).
#define PORT_PS_FSEL(...) (work = (dt) >= 0.0f ? (fx) : (fy));

// nw4r::g3d fastcast (g3d_cpu.h): GQR7 is s16 scale 8, GQR6 s16 scale 5.
#define PORT_PS_S7_8ToF32(...) (f = (f32) * (pPtr) / 256.0f);
#define PORT_PS_S10_5ToF32(...) (f = (f32) * (pPtr) / 32.0f);
#define PORT_PS_F32ToS10_5(...) (*(pPtr) = (s16)port_psq_quantize(f, 5, -0x8000, 0x7FFF));

// Other CodeWarrior PowerPC intrinsics the decomp calls (HOST_BUILD.md §18).
// __frsqrte / __fres are estimates on the Wii; here they are exact (port/ps.h).
static inline double __frsqrte(double x) {
    return (double)port_frsqrte((float)x);
}
static inline float __fres(float x) {
    return port_fres(x);
}
static inline double __fabs(double x) {
    return __builtin_fabs(x);
}
static inline double __fnabs(double x) {
    return -__builtin_fabs(x);
}
static inline int __abs(int x) {
    return x < 0 ? -x : x;
}
static inline unsigned int __cntlzw(unsigned int x) {
    return x == 0 ? 32u : (unsigned int)__builtin_clz(x);
}
// rlwimi: rotate `value` left by `shift` and insert it into `base` under the
// mask of bits mb..me (big-endian bit numbering, wrapping when mb > me).
static inline unsigned int __rlwimi(unsigned int base, unsigned int value, unsigned int shift,
                                    unsigned int mb, unsigned int me) {
    unsigned int rot = shift ? (value << shift) | (value >> (32 - shift)) : value;
    unsigned int m1 = 0xFFFFFFFFu >> mb, m2 = me >= 31 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> (me + 1));
    unsigned int mask = mb <= me ? (m1 & m2) : (m1 | m2);
    return (rot & mask) | (base & ~mask);
}

// The paired-single host forms below (port/ps_forms.h; HOST_BUILD.md §18).
// Each macro is the block's asm replaced by a call on the function's own
// variables.
#define PORT_PS_VEC2Normalize(...) port_vec2_normalize((float*)(pOut), (const float*)(pIn));
#define PORT_PS_MTX33Identity(...) port_mtx33_identity((float*)(pMtx));
#define PORT_PS_MTX34ToMTX33(...) port_mtx34_to_mtx33((float*)(pOut), (const float*)(pIn));
#define PORT_PS_MTX34Zero(...) port_mtx34_zero((float*)(pMtx));
#define PORT_PS_MTX34Scale(...)                                                \
    port_mtx34_scale((float*)(pOut), (const float*)(pIn), (const float*)(pScale));
#define PORT_PS_MTX34Trans(...)                                                \
    port_mtx34_trans((float*)(pOut), (const float*)(pIn), (const float*)(pTrans));
#define PORT_PS_MTX44Identity(...) port_mtx44_identity((float*)(pMtx));
#define PORT_PS_MTX44Copy(...) port_mtx44_copy((float*)(pDst), (const float*)(pSrc));
// math_arithmetic.cpp FrSqrt: frsqrte and one Newton-Raphson step.
#define PORT_PS_FrSqrt(...) (work1 = port_frsqrt_nr(x));
// ut_algorithm.h Abs<f32>, RPTnkBlock.cpp BlockAbs: fabs.
#define PORT_PS_Abs(...) (ax = __builtin_fabsf(x));
#define PORT_PS_BlockAbs(...) (value = __builtin_fabsf(value));
// g3d_cpu.cpp: 32-byte block copy / clear (lfd/stfd and psq_st of 0.0 move
// the bits unchanged).
#define PORT_PS_Copy32ByteBlocks(...) __builtin_memcpy((pDst), (pSrc), 32);
#define PORT_PS_ZeroMemory32ByteBlocks(...) __builtin_memset((pDst), 0, 32);
// g3d_fog.cpp Fog::CopyTo: six lfd/stfd pairs.
#define PORT_PS_FogCopyTo(...) __builtin_memcpy((pDst), (pSrc), 48);
// g3d_calcview.cpp SetMdlViewMtxSR (three overloads).
#define PORT_PS_SetMdlViewMtxSR1(...)                                          \
    port_g3d_view_sr1((float*)(pViewPos), (const float*)&(rRY), (s));
#define PORT_PS_SetMdlViewMtxSR3(...)                                          \
    port_g3d_view_sr3((float*)(pViewPos), (const float*)&(rRY), (sx), (sy), (sz));
#define PORT_PS_SetMdlViewMtxSR7(...)                                          \
    port_g3d_view_sr7((float*)(pViewPos), (const float*)&(rRX), (const float*)&(rRY),     \
                      (const float*)&(rRZ), (sx), (sy), (sz));
// g3d_calcworld.cpp CalcSkinning: the six (M00, M02, ...) pairs accumulate in
// port_skin[12], declared by the first block in the same scope as the others.
#define PORT_PS_CalcSkinZero(...) f32 port_skin[12] = {0};
#define PORT_PS_CalcSkinAdd(...)                                               \
    for (int port_k = 0; port_k < 12; port_k++) {                              \
        port_skin[port_k] = __builtin_fmaf(((const f32*)(pT))[port_k], (R), port_skin[port_k]); \
    }
#define PORT_PS_CalcSkinStore(...) __builtin_memcpy((pTargetMtx), port_skin, sizeof port_skin);
// g3d_calcvtx.cpp CalcVtx: key-shape blends; the (xy, z_) / (rg, ba) pairs
// live in port_acc[4], declared by the first block of each vertex.
#define PORT_PS_CalcVtxFirst(...)                                              \
    f32 port_acc[4];                                                           \
    port_g3d_vtx_first(port_acc, (pVtx), (firstWeight));
#define PORT_PS_CalcVtxKey(...) port_g3d_vtx_key(port_acc, (pKeyVtx), (keyWeight));
#define PORT_PS_CalcVtxStore(...) port_g3d_vtx_store((pDst), port_acc);
#define PORT_PS_CalcClrFirst(...)                                              \
    f32 port_acc[4];                                                           \
    port_g3d_clr_first(port_acc, (pVtx), (firstWeight));
#define PORT_PS_CalcClrKey(...) port_g3d_clr_key(port_acc, (pKeyVtx), (keyWeight));
#define PORT_PS_CalcClrStore(...)                                              \
    for (int port_k = 0; port_k < 4; port_k++) {                               \
        ((u8*)(pDst))[port_k] = (u8)port_psq_quantize(port_acc[port_k], 0, 0, 0xFF); \
    }
// RPHkyPaddle.cpp PointerScreenWidth: lhzx from the TV-mode width table.
#define PORT_PS_PointerScreenWidthLoad(...) (width = *(const u16*)((const u8*)(table) + (offset)));
#define PORT_PS_PointerScreenWidthMove(...)
// RVL MTX library (src/revolution/MTX/vec.c, quat.c).
#define PORT_PS_PSVECScale(...)                                                \
    ((out)->x = (in)->x * (scale), (out)->y = (in)->y * (scale), (out)->z = (in)->z * (scale));
#define PORT_PS_PSVECNormalize(...) port_vec_normalize((const float*)(in), (float*)(out));
#define PORT_PS_PSVECMagDot(...)                                               \
    {                                                                          \
        f32 port_xy2x = (v)->x * (v)->x, port_xy2y = (v)->y * (v)->y;          \
        dot = __builtin_fmaf((v)->z, (v)->z, port_xy2x) + port_xy2y;           \
        c_zero = c_half - c_half;                                              \
    }
#define PORT_PS_PSVECMagScale(...) (dot = dot * port_frsqrt_nr(dot));
#define PORT_PS_PSVECSquareDistance(...)                                       \
    (dist = port_vec_square_distance((const float*)(a), (const float*)(b)));
#define PORT_PS_PSQUATMultiply(...)                                            \
    port_quat_multiply((const float*)(a), (const float*)(b), (float*)(prod));
#define PORT_PS_PSQUATNormalize(...) port_quat_normalize((const float*)(in), (float*)(out));

// The GX SDK's write-gather asm (src/revolution/GX/GXTransform.c, GXLight.c):
// `ASM_VOLATILE_PS(name) ( ...asm... )` becomes the same stores through the
// host write-gather pipe (port/wgpipe.h), on the function's own `dst`/`src`;
// psq_l/psq_st pairs move floats in order.
#define ASM_VOLATILE_PS(name) PORT_PS_##name
#define PORT_PS_LoadProjPS(...)                                                \
    {                                                                          \
        int i_;                                                                \
        for (i_ = 0; i_ < 6; i_++) {                                           \
            dst[i_] = gxdt->proj[i_];                                          \
        }                                                                      \
    }
#define PORT_PS_Copy6Floats(...)                                               \
    {                                                                          \
        int i_;                                                                \
        for (i_ = 0; i_ < 6; i_++) {                                           \
            dst[i_] = src[i_];                                                 \
        }                                                                      \
    }
#define PORT_PS_WriteProjPS(...) port_wgpipe_f32s((const f32*)(src), 6);
#define PORT_PS_WriteMTXPS4x3(...) port_wgpipe_f32s((const f32*)(src), 12);
#define PORT_PS_WriteMTXPS4x2(...) port_wgpipe_f32s((const f32*)(src), 8);
// psq_l 0, lfs 8, psq_l 16, lfs 24, psq_l 32, lfs 40: the 3x3 part of a 3x4.
#define PORT_PS_WriteMTXPS3x3(...)                                             \
    {                                                                          \
        port_wgpipe_f32s((const f32*)(src)[0], 3);                             \
        port_wgpipe_f32s((const f32*)(src)[1], 3);                             \
        port_wgpipe_f32s((const f32*)(src)[2], 3);                             \
    }
// Three zero words, the colour word (RGBA, read as one big-endian word), then
// the twelve floats aa..dirZ.
#define PORT_PS_WriteLightObj(...)                                             \
    {                                                                          \
        const u8* c_ = (const u8*)&src->color;                                 \
        port_wgpipe_u32(0);                                                    \
        port_wgpipe_u32(0);                                                    \
        port_wgpipe_u32(0);                                                    \
        port_wgpipe_u32((u32)c_[0] << 24 | (u32)c_[1] << 16 |                  \
                        (u32)c_[2] << 8 | (u32)c_[3]);                         \
        port_wgpipe_f32s(&src->aa, 12);                                        \
    }

// MSL's <math.h> constants that the C library lacks (GXInitFogAdjTable).
#define M_SQRT3 1.73205f

// CodeWarrior's `#pragma unused(x)` etc. are ignored by GCC/Clang (with a
// warning); nothing else to do here.

#endif
