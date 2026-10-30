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

// CodeWarrior's `#pragma unused(x)` etc. are ignored by GCC/Clang (with a
// warning); nothing else to do here.

#endif
