/**
 * Gekko / Broadway paired-single operations in C (host build only).
 *
 * The decomp's paired-single asm (ASM blocks opened with ASM_PS(name), and
 * CodeWarrior asm functions) gets its host form from these helpers
 * (port/host/include/decomp/compat.h, port/docs/HOST_BUILD.md §18). A paired
 * single is two floats; each helper is one instruction with the Wii's
 * rounding: ps_madd / ps_msub / ps_nmsub / ps_madds0 ... are fused (rounded
 * once, port_fmaf), everything else rounds every operation to single precision.
 * The host compiles them with SSE and no contraction, so they are bit-exact.
 * The two estimate instructions, fres and frsqrte, use Broadway's tables
 * (port_fres, port_frsqrte below), so the callers' Newton-Raphson steps start
 * from the Wii's estimate and end on the Wii's result (HOST_BUILD.md §42).
 *
 * Quantized loads and stores use GQR0 (plain float) unless a helper says so;
 * psq_l with W=1 loads (value, 1.0f).
 */

#ifndef PORT_PS_H
#define PORT_PS_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif


#ifdef __cplusplus
extern "C" {
#endif

/* x*y+z rounded once (src/host_fma.c): the vfmadd instructions or an exact
 * software form; never the C library's fmaf (HOST_BUILD.md §42). */
float port_fmaf(float x, float y, float z);
double port_fma(double x, double y, double z);

typedef struct port_ps {
    float p0, p1;
} port_ps;

static inline port_ps port_ps_make(float a, float b) {
    port_ps r;
    r.p0 = a;
    r.p1 = b;
    return r;
}

/* psq_l / psq_st with GQR0 (W=0: two floats, W=1: one float and 1.0). */
static inline port_ps port_psq_l(const void* p, int w) {
    const float* f = (const float*)p;
    return port_ps_make(f[0], w ? 1.0f : f[1]);
}
static inline void port_psq_st(void* p, port_ps v, int w) {
    float* f = (float*)p;
    f[0] = v.p0;
    if (!w) {
        f[1] = v.p1;
    }
}

/* The same on big-endian data (vertex arrays, which stay big-endian on the
 * host: HOST_BUILD.md §11). */
static inline float port_ps_load_be_f32(const void* p) {
    unsigned int u;
    float f;
    __builtin_memcpy(&u, p, 4);
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
    u = __builtin_bswap32(u);
#endif
    __builtin_memcpy(&f, &u, 4);
    return f;
}
static inline void port_ps_store_be_f32(void* p, float f) {
    unsigned int u;
    __builtin_memcpy(&u, &f, 4);
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
    u = __builtin_bswap32(u);
#endif
    __builtin_memcpy(p, &u, 4);
}
static inline port_ps port_psq_l_be(const void* p, int w) {
    const unsigned char* b = (const unsigned char*)p;
    return port_ps_make(port_ps_load_be_f32(b), w ? 1.0f : port_ps_load_be_f32(b + 4));
}
static inline void port_psq_st_be(void* p, port_ps v, int w) {
    unsigned char* b = (unsigned char*)p;
    port_ps_store_be_f32(b, v.p0);
    if (!w) {
        port_ps_store_be_f32(b + 4, v.p1);
    }
}

/* Arithmetic (D = op(A, B[, C]) in the instruction's operand order). */
static inline port_ps port_ps_add(port_ps a, port_ps b) {
    return port_ps_make(a.p0 + b.p0, a.p1 + b.p1);
}
static inline port_ps port_ps_sub(port_ps a, port_ps b) {
    return port_ps_make(a.p0 - b.p0, a.p1 - b.p1);
}
static inline port_ps port_ps_neg(port_ps a) {
    return port_ps_make(-a.p0, -a.p1);
}
static inline port_ps port_ps_mul(port_ps a, port_ps c) {
    return port_ps_make(a.p0 * c.p0, a.p1 * c.p1);
}
static inline port_ps port_ps_muls0(port_ps a, port_ps c) {
    return port_ps_make(a.p0 * c.p0, a.p1 * c.p0);
}
static inline port_ps port_ps_muls1(port_ps a, port_ps c) {
    return port_ps_make(a.p0 * c.p1, a.p1 * c.p1);
}
/* ps_madd D,A,C,B: A*C + B */
static inline port_ps port_ps_madd(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(port_fmaf(a.p0, c.p0, b.p0), port_fmaf(a.p1, c.p1, b.p1));
}
static inline port_ps port_ps_madds0(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(port_fmaf(a.p0, c.p0, b.p0), port_fmaf(a.p1, c.p0, b.p1));
}
static inline port_ps port_ps_madds1(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(port_fmaf(a.p0, c.p1, b.p0), port_fmaf(a.p1, c.p1, b.p1));
}
/* ps_msub D,A,C,B: A*C - B */
static inline port_ps port_ps_msub(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(port_fmaf(a.p0, c.p0, -b.p0), port_fmaf(a.p1, c.p1, -b.p1));
}
/* ps_nmsub D,A,C,B: -(A*C - B) */
static inline port_ps port_ps_nmsub(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(-port_fmaf(a.p0, c.p0, -b.p0), -port_fmaf(a.p1, c.p1, -b.p1));
}
/* ps_sum0 D,A,C,B: (A0 + B1, C1) */
static inline port_ps port_ps_sum0(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(a.p0 + b.p1, c.p1);
}
/* ps_sum1 D,A,C,B: (C0, A0 + B1) */
static inline port_ps port_ps_sum1(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(c.p0, a.p0 + b.p1);
}
static inline port_ps port_ps_merge00(port_ps a, port_ps b) {
    return port_ps_make(a.p0, b.p0);
}
static inline port_ps port_ps_merge01(port_ps a, port_ps b) {
    return port_ps_make(a.p0, b.p1);
}
static inline port_ps port_ps_merge10(port_ps a, port_ps b) {
    return port_ps_make(a.p1, b.p0);
}
static inline port_ps port_ps_merge11(port_ps a, port_ps b) {
    return port_ps_make(a.p1, b.p1);
}
/* A scalar in both slots, as lfs / scalar single ops leave it. */
static inline port_ps port_ps_splat(float a) {
    return port_ps_make(a, a);
}

/* Broadway's estimate instructions, bit for bit: the lookup tables and the
 * interpolation Dolphin uses (Source/Core/Common/FloatUtils.cpp,
 * ApproximateReciprocal / ApproximateReciprocalSquareRoot; Dolphin measured
 * them on a console). HOST_BUILD.md §42. */
static inline unsigned long long port_dbits(double d) {
    unsigned long long u;
    __builtin_memcpy(&u, &d, 8);
    return u;
}
static inline double port_bitsd(unsigned long long u) {
    double d;
    __builtin_memcpy(&d, &u, 8);
    return d;
}

/* fres (and ps_res): about 12 bits; the result is a single (23 fraction bits). */
static inline float port_fres(float x) {
    static const int base[32] = {
        0x7ff800, 0x783800, 0x70ea00, 0x6a0800, 0x638800, 0x5d6200, 0x579000, 0x520800,
        0x4cc800, 0x47ca00, 0x430800, 0x3e8000, 0x3a2c00, 0x360800, 0x321400, 0x2e4a00,
        0x2aa800, 0x272c00, 0x23d600, 0x209e00, 0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
        0x124400, 0x0fbe00, 0x0d3800, 0x0ade00, 0x088400, 0x065000, 0x041c00, 0x020c00};
    static const int dec[32] = {
        0x3e1, 0x3a7, 0x371, 0x340, 0x313, 0x2ea, 0x2c4, 0x2a0, 0x27f, 0x261, 0x245,
        0x22a, 0x212, 0x1fb, 0x1e5, 0x1d1, 0x1be, 0x1ac, 0x19b, 0x18b, 0x17c, 0x16e,
        0x15b, 0x15b, 0x143, 0x143, 0x12d, 0x12d, 0x11a, 0x11a, 0x108, 0x106};
    unsigned long long u = port_dbits((double)x);
    unsigned long long mantissa = u & ((1ULL << 52) - 1), sign = u & (1ULL << 63);
    long long exponent = (long long)(u & (0x7FFULL << 52));
    if (mantissa == 0 && exponent == 0) {
        return __builtin_copysignf(__builtin_inff(), x);
    }
    if (exponent == (long long)(0x7FFULL << 52)) {
        if (mantissa == 0) {
            return __builtin_copysignf(0.0f, x);
        }
        return x + x; /* quiet NaN */
    }
    if (exponent < (895LL << 52)) {
        return __builtin_copysignf(3.402823466e+38f, x);
    }
    if (exponent >= (1149LL << 52)) {
        return __builtin_copysignf(0.0f, x);
    }
    exponent = (0x7FDLL << 52) - exponent;
    {
        int i = (int)(mantissa >> 37);
        long long m = base[i / 1024] - (dec[i / 1024] * (i % 1024) + 1) / 2;
        return (float)port_bitsd(sign | (unsigned long long)exponent | ((unsigned long long)m << 29));
    }
}

/* frsqrte: about 12 bits, a double with up to 26 fraction bits (not always a
 * single: a single-precision instruction that takes it as its C operand rounds
 * it first, port_force25). Inputs here are singles (no denormal doubles). */
static inline double port_frsqrte(double x) {
    static const int base[32] = {
        0x1a7e800, 0x17cb800, 0x1552800, 0x130c000, 0x10f2000, 0x0eff000, 0x0d2e000, 0x0b7c000,
        0x09e5000, 0x0867000, 0x06ff000, 0x05ab800, 0x046a000, 0x0339800, 0x0218800, 0x0105800,
        0x3ffa000, 0x3c29000, 0x38aa000, 0x3572000, 0x3279000, 0x2fb7000, 0x2d26000, 0x2ac0000,
        0x2881000, 0x2665000, 0x2468000, 0x2287000, 0x20c1000, 0x1f12000, 0x1d79000, 0x1bf4000};
    static const int dec[32] = {
        -0x568, -0x4f3, -0x48d, -0x435, -0x3e7, -0x3a2, -0x365, -0x32e, -0x2fc, -0x2d0, -0x2a8,
        -0x283, -0x261, -0x243, -0x226, -0x20b, -0x7a4, -0x700, -0x670, -0x5f2, -0x584, -0x524,
        -0x4cc, -0x47e, -0x43a, -0x3fa, -0x3c2, -0x38e, -0x35e, -0x332, -0x30a, -0x2e6};
    unsigned long long u = port_dbits(x);
    long long mantissa = (long long)(u & ((1ULL << 52) - 1));
    unsigned long long sign = u & (1ULL << 63);
    long long exponent = (long long)(u & (0x7FFULL << 52));
    if (mantissa == 0 && exponent == 0) {
        return sign ? -__builtin_inf() : __builtin_inf();
    }
    if (exponent == (long long)(0x7FFULL << 52)) {
        if (mantissa == 0) {
            return sign ? __builtin_nan("") : 0.0;
        }
        return x + x;
    }
    if (sign) {
        return __builtin_nan("");
    }
    if (!exponent) {
        do {
            exponent -= 1LL << 52;
            mantissa <<= 1;
        } while (!(mantissa & (1LL << 52)));
        mantissa &= (1LL << 52) - 1;
        exponent += 1LL << 52;
    }
    {
        long long exponent_lsb = exponent & (1LL << 52);
        int i;
        exponent = ((0x3FFLL << 52) - ((exponent - (0x3FELL << 52)) / 2)) & (0x7FFLL << 52);
        i = (int)((exponent_lsb | mantissa) >> 37);
        return port_bitsd(sign | (unsigned long long)exponent |
                          ((unsigned long long)(base[i / 2048] + dec[i / 2048] * (i % 2048)) << 26));
    }
}

/* The C operand of a single-precision multiply (fmuls, fmadds, ps_mul, ps_muls0,
 * ...) is rounded to 25 fraction bits first (Dolphin's Force25Bit). A no-op on a
 * single; it matters only for a double operand, here frsqrte's estimate and
 * PSQUATNormalize's double-precision scale. Normal values only. */
static inline double port_force25(double c) {
    unsigned long long u = port_dbits(c);
    return port_bitsd((u & 0xFFFFFFFFF8000000ULL) + (u & 0x8000000ULL));
}
/* fmuls / ps_mul with a double C operand: the product of a single (or an
 * estimate) and the rounded C is exact in double; it is rounded to single once. */
static inline float port_fmuls_d(double a, double c) {
    return (float)(a * port_force25(c));
}

/* fres refined by one Newton-Raphson step, as the paired-single code writes
 * it: e = fres(d); e = -(d * e*e - (e + e)). */
static inline float port_fres_nr(float d) {
    float e = port_fres(d);
    float twice = e + e;
    float sq = e * e;
    return -port_fmaf(d, sq, -twice);
}

#ifdef __cplusplus
}
#endif

#endif
