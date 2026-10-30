/**
 * Gekko / Broadway paired-single operations in C (host build only).
 *
 * The decomp's paired-single asm (ASM blocks opened with ASM_PS(name), and
 * CodeWarrior asm functions) gets its host form from these helpers
 * (port/host/include/decomp/compat.h, port/docs/HOST_BUILD.md §18). A paired
 * single is two floats; each helper is one instruction with the Wii's
 * rounding: ps_madd / ps_msub / ps_nmsub / ps_madds0 ... are fused (rounded
 * once, fmaf), everything else rounds every operation to single precision.
 * The host compiles them with SSE and no contraction, so they are bit-exact
 * except for the two estimate instructions:
 *
 *   fres     the Wii returns an estimate (about 12 bits, refined by the
 *            callers with Newton-Raphson steps); here it is 1.0f / x.
 *   frsqrte  likewise an estimate; here it is 1 / __builtin_sqrt(x) rounded to single.
 *
 * With an exact start the callers' Newton-Raphson steps change the result by
 * at most an ulp or two; the tests (tests/test_math.cpp) bound it.
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
    return port_ps_make(__builtin_fmaf(a.p0, c.p0, b.p0), __builtin_fmaf(a.p1, c.p1, b.p1));
}
static inline port_ps port_ps_madds0(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(__builtin_fmaf(a.p0, c.p0, b.p0), __builtin_fmaf(a.p1, c.p0, b.p1));
}
static inline port_ps port_ps_madds1(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(__builtin_fmaf(a.p0, c.p1, b.p0), __builtin_fmaf(a.p1, c.p1, b.p1));
}
/* ps_msub D,A,C,B: A*C - B */
static inline port_ps port_ps_msub(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(__builtin_fmaf(a.p0, c.p0, -b.p0), __builtin_fmaf(a.p1, c.p1, -b.p1));
}
/* ps_nmsub D,A,C,B: -(A*C - B) */
static inline port_ps port_ps_nmsub(port_ps a, port_ps c, port_ps b) {
    return port_ps_make(-__builtin_fmaf(a.p0, c.p0, -b.p0), -__builtin_fmaf(a.p1, c.p1, -b.p1));
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

/* Estimates (see the top of this file). */
static inline float port_fres(float x) {
    return 1.0f / x;
}
static inline float port_frsqrte(float x) {
    return (float)(1.0 / __builtin_sqrt((double)x));
}

/* fres refined by one Newton-Raphson step, as the paired-single code writes
 * it: e = fres(d); e = -(d * e*e - (e + e)). */
static inline float port_fres_nr(float d) {
    float e = port_fres(d);
    float twice = e + e;
    float sq = e * e;
    return -__builtin_fmaf(d, sq, -twice);
}

#ifdef __cplusplus
}
#endif

#endif
