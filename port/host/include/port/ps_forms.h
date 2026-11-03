/**
 * Host forms of the decomp's paired-single asm (host build only).
 *
 * Each function is the C transliteration of one asm block or asm function,
 * instruction by instruction, on port/ps.h's paired singles; matrices and
 * vectors are passed as float arrays in their row-major layout (MTX34 = 3x4,
 * MTX33 = 3x3, MTX44 = 4x4, VEC3 = x y z). Loads happen before stores where
 * the asm loads everything first, so in-place calls behave the same.
 * port/host/include/decomp/compat.h maps ASM_PS(name) blocks onto these; the
 * asm functions call them under #if defined(PORT_HOST). Table and tolerances:
 * port/docs/HOST_BUILD.md §18; tests: port/host/tests/test_math.cpp.
 */

#ifndef PORT_PS_FORMS_H
#define PORT_PS_FORMS_H

#include <port/ps.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * nw4r::math (src/nw4r/math/math_types.cpp, math_arithmetic.cpp)
 ******************************************************************************/

/* 1/sqrt(x): frsqrte and one Newton-Raphson step in single precision (FrSqrt,
 * VEC2Normalize, PSVECNormalize, PSVECMag):
 *   fmuls   sq, r, r        r * force25(r), rounded to single
 *   fmuls   half, r, 0.5
 *   fnmsubs t, sq, x, 3     -(sq * x - 3), fused
 *   fmuls   y, t, half */
static inline float port_frsqrt_nr(float x) {
    double r = port_frsqrte(x);
    float sq = port_fmuls_d(r, r);
    float half = (float)(r * 0.5);
    float t = -port_fmaf(sq, x, -3.0f);
    return t * half;
}

static inline void port_vec2_normalize(float* out, const float* in) {
    port_ps xy = port_psq_l(in, 0);
    port_ps sq = port_ps_mul(xy, xy);
    float len2 = port_ps_sum0(sq, sq, sq).p0;
    float r = port_frsqrt_nr(len2);
    port_psq_st(out, port_ps_muls0(xy, port_ps_splat(r)), 0);
}

static inline void port_mtx33_identity(float* m) {
    port_ps c_00 = port_ps_splat(0.0f), c_10 = port_ps_splat(1.0f);
    port_ps c_11 = port_ps_merge00(c_10, c_00);
    port_psq_st(m + 2, c_00, 0);
    port_psq_st(m + 6, c_00, 0);
    port_psq_st(m + 0, c_11, 0);
    port_psq_st(m + 4, c_11, 0);
    m[8] = 1.0f;
}

static inline void port_mtx34_to_mtx33(float* out, const float* in) {
    port_ps r0a = port_psq_l(in + 0, 0), r0b = port_psq_l(in + 2, 0);
    port_ps r1a = port_psq_l(in + 4, 0), r1b = port_psq_l(in + 6, 0);
    port_ps r2a = port_psq_l(in + 8, 0), r2b = port_psq_l(in + 10, 0);
    port_psq_st(out + 0, r0a, 0);
    port_psq_st(out + 2, r0b, 1);
    port_psq_st(out + 3, r1a, 0);
    port_psq_st(out + 5, r1b, 1);
    port_psq_st(out + 6, r2a, 0);
    port_psq_st(out + 8, r2b, 1);
}

static inline unsigned int port_mtx34_inv_transpose(float* out, const float* in) {
    port_ps f0 = port_psq_l(in + 0, 1);  /* (a, 1) */
    port_ps f1 = port_psq_l(in + 1, 0);  /* (b, c) */
    port_ps f2 = port_psq_l(in + 4, 1);  /* (d, 1) */
    port_ps f6 = port_ps_merge10(f1, f0);
    port_ps f3 = port_psq_l(in + 5, 0);  /* (e, f) */
    port_ps f4 = port_psq_l(in + 8, 1);  /* (g, 1) */
    port_ps f7 = port_ps_merge10(f3, f2);
    port_ps f5 = port_psq_l(in + 9, 0);  /* (h, i) */
    port_ps f11 = port_ps_mul(f3, f6);
    port_ps f8 = port_ps_merge10(f5, f4);
    port_ps f13 = port_ps_mul(f5, f7);
    port_ps f12, f10, f9, z;
    float inv;
    f11 = port_ps_msub(f1, f7, f11);
    f12 = port_ps_mul(f1, f8);
    f13 = port_ps_msub(f3, f8, f13);
    f12 = port_ps_msub(f5, f6, f12);
    f10 = port_ps_mul(f3, f4);
    f9 = port_ps_mul(f0, f5);
    f8 = port_ps_mul(f1, f2);
    f10 = port_ps_msub(f2, f5, f10);
    f9 = port_ps_msub(f1, f4, f9);
    f8 = port_ps_msub(f0, f3, f8);
    f7 = port_ps_mul(f0, f13);
    z = port_ps_sub(f1, f1);
    f7 = port_ps_madd(f2, f12, f7);
    f7 = port_ps_madd(f4, f11, f7);
    if (f7.p0 == z.p0) {
        return 0;
    }
    /* fres and two Newton-Raphson steps */
    inv = port_fres(f7.p0);
    {
        float twice = inv + inv, sq = inv * inv;
        inv = -port_fmaf(f7.p0, sq, -twice);
        twice = inv + inv;
        sq = inv * inv;
        inv = -port_fmaf(f7.p0, sq, -twice);
    }
    {
        port_ps s = port_ps_splat(inv);
        f13 = port_ps_muls0(f13, s);
        f12 = port_ps_muls0(f12, s);
        port_psq_st(out + 0, f13, 0);
        f11 = port_ps_muls0(f11, s);
        port_psq_st(out + 3, f12, 0);
        f10 = port_ps_muls0(f10, s);
        port_psq_st(out + 6, f11, 0);
        f9 = port_ps_muls0(f9, s);
        port_psq_st(out + 2, f10, 1);
        f8 = port_ps_muls0(f8, s);
        port_psq_st(out + 5, f9, 1);
        port_psq_st(out + 8, f8, 1);
    }
    return 1;
}

static inline void port_mtx34_zero(float* m) {
    int i;
    for (i = 0; i < 12; i++) {
        m[i] = 0.0f;
    }
}

static inline void port_mtx34_scale(float* out, const float* in, const float* s) {
    port_ps xy = port_psq_l(s, 0), z = port_psq_l(s + 2, 1);
    int r;
    port_ps a[3], b[3];
    for (r = 0; r < 3; r++) {
        a[r] = port_psq_l(in + 4 * r, 0);
        b[r] = port_psq_l(in + 4 * r + 2, 0);
    }
    for (r = 0; r < 3; r++) {
        port_psq_st(out + 4 * r, port_ps_mul(a[r], xy), 0);
        port_psq_st(out + 4 * r + 2, port_ps_mul(b[r], z), 0);
    }
}

static inline void port_mtx34_trans(float* out, const float* in, const float* t) {
    port_ps xy = port_psq_l(t, 0), z = port_psq_l(t + 2, 1);
    int r;
    port_ps a[3], b[3];
    for (r = 0; r < 3; r++) {
        a[r] = port_psq_l(in + 4 * r, 0);
        b[r] = port_psq_l(in + 4 * r + 2, 0);
    }
    for (r = 0; r < 3; r++) {
        port_psq_st(out + 4 * r, a[r], 0);
        port_psq_st(out + 4 * r + 2, b[r], 0);
    }
    for (r = 0; r < 3; r++) {
        port_ps w0 = port_ps_mul(a[r], xy);
        port_ps w1 = port_ps_madd(b[r], z, w0);
        out[4 * r + 3] = w1.p0 + w1.p1; /* ps_sum0 */
    }
}

static inline void port_mtx44_identity(float* m) {
    int i;
    for (i = 0; i < 16; i++) {
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

static inline void port_mtx44_copy(float* dst, const float* src) {
    float t[16];
    __builtin_memcpy(t, src, sizeof t);
    __builtin_memcpy(dst, t, sizeof t);
}

/******************************************************************************
 * nw4r::g3d (g3d_calcview.cpp, g3d_calcworld.cpp, g3d_calcvtx.cpp)
 ******************************************************************************/

/* GetModelLocalAxisY2 (y3 = 0) and GetModelLocalAxisY3 (y3 = 1). */
static inline void port_g3d_local_axis_y(float* v, const float* m, const float* p, int y3) {
    port_ps f2 = port_psq_l(p + 4, 1), f3 = port_psq_l(p + 5, 0);
    port_ps f0 = port_psq_l(p + 0, 1), f1 = port_psq_l(p + 1, 0);
    port_ps f7 = port_ps_merge10(f3, f2);
    port_ps f5 = port_psq_l(p + 9, 0), f4 = port_psq_l(p + 8, 1);
    port_ps f6 = port_ps_merge10(f1, f0);
    port_ps f12 = port_ps_mul(f5, f7);
    port_ps f8 = port_ps_merge10(f5, f4);
    port_ps f10 = port_ps_mul(f3, f6);
    port_ps f9 = port_ps_mul(f0, f5);
    port_ps f11 = port_ps_mul(f1, f8);
    f12 = port_ps_msub(f3, f8, f12);
    f10 = port_ps_msub(f1, f7, f10);
    f11 = port_ps_msub(f5, f6, f11);
    f7 = port_ps_mul(f0, f12);
    f6 = port_ps_sub(f6, f6);
    f9 = port_ps_msub(f1, f4, f9);
    f7 = port_ps_madd(f2, f11, f7);
    if (!y3) {
        v[2] = f6.p0;
    }
    f7 = port_ps_madd(f4, f10, f7);
    if (f7.p0 == f6.p0) {
        port_psq_st(v, f6, 0);
        if (y3) {
            v[2] = f6.p0;
        }
        return;
    }
    {
        port_ps e = port_ps_splat(port_fres(f7.p0));
        port_ps m1 = port_psq_l(m + 4, 0), m2 = port_psq_l(m + 2, 1), m3 = port_psq_l(m + 6, 1);
        port_ps twice = port_ps_add(e, e), sq = port_ps_mul(e, e);
        port_ps inv = port_ps_nmsub(f7, sq, twice);
        port_ps c2 = port_ps_merge00(m2, m3);
        port_ps m22 = port_psq_l(m + 10, 1);
        port_ps m0, r, c0, c1;
        f11 = port_ps_muls0(f11, inv);
        f9 = port_ps_muls0(f9, inv);
        m0 = port_psq_l(m + 0, 0);
        c0 = port_ps_merge00(m0, m1);
        c1 = port_ps_merge11(m0, m1);
        r = port_ps_muls0(c0, f11);
        if (y3) {
            port_ps row2 = port_ps_mul(port_psq_l(m + 8, 0), f9);
            float z;
            r = port_ps_madds1(c1, f11, r);
            row2 = port_ps_sum0(row2, row2, row2);
            r = port_ps_madds0(c2, f9, r);
            z = port_fmaf(m22.p0, f9.p0, row2.p0);
            port_psq_st(v, r, 0);
            v[2] = z;
        } else {
            r = port_ps_madds1(c1, f11, r);
            r = port_ps_madds0(c2, f9, r);
            port_psq_st(v, r, 0);
        }
    }
}

/* SetMdlViewMtxSR(pViewPos, rRY, s) */
static inline void port_g3d_view_sr1(float* m, const float* ry, float s) {
    port_ps w0 = port_ps_muls0(port_psq_l(ry, 0), port_ps_splat(s));
    port_ps w1 = port_ps_merge10(w0, w0);
    port_psq_st(m + 0, w1, 0);
    w1 = port_ps_merge01(port_ps_neg(w0), w0);
    port_psq_st(m + 4, w1, 0);
    m[2] = 0.0f;
    m[6] = 0.0f;
    m[8] = 0.0f;
    m[9] = 0.0f;
    m[10] = s;
}

/* SetMdlViewMtxSR(pViewPos, rRY, sx, sy, sz) */
static inline void port_g3d_view_sr3(float* m, const float* ry, float sx, float sy, float sz) {
    port_ps w0 = port_ps_make(sx, sy);
    port_ps w1 = port_psq_l(ry, 0);
    port_ps w3 = port_ps_mul(port_ps_merge10(w1, w1), w0);
    port_ps w2;
    port_psq_st(m + 0, w3, 0);
    w2 = port_ps_neg(w1);
    w3 = port_ps_mul(port_ps_merge01(w2, w1), w0);
    port_psq_st(m + 4, w3, 0);
    m[2] = 0.0f;
    m[6] = 0.0f;
    m[8] = 0.0f;
    m[9] = 0.0f;
    m[10] = sz;
}

/* SetMdlViewMtxSR(pViewPos, rRX, rRY, rRZ, sx, sy, sz) */
static inline void port_g3d_view_sr7(float* m, const float* rx, const float* ry, const float* rz,
                                     float sx, float sy, float sz) {
    port_ps w0 = port_ps_muls0(port_psq_l(rx, 0), port_ps_splat(sx));
    port_ps w1 = port_ps_muls0(port_psq_l(ry, 0), port_ps_splat(sy));
    port_psq_st(m + 0, port_ps_merge00(w0, w1), 0);
    port_psq_st(m + 4, port_ps_merge11(w0, w1), 0);
    w0 = port_ps_muls0(port_psq_l(rz, 0), port_ps_splat(sz));
    m[2] = w0.p0;
    m[6] = w0.p1;
    m[8] = rx[2] * sx;
    m[9] = ry[2] * sy;
    m[10] = rz[2] * sz;
}

/* CalcVtx position / normal blend on big-endian vertex arrays: acc holds
 * (xy, z_) as four floats. */
static inline void port_g3d_vtx_first(float* acc, const void* vtx, float w) {
    port_ps xy = port_psq_l_be(vtx, 0);
    port_ps z = port_psq_l_be((const unsigned char*)vtx + 8, 1);
    xy = port_ps_mul(xy, port_ps_splat(w));
    z = port_ps_mul(z, port_ps_splat(w));
    acc[0] = xy.p0;
    acc[1] = xy.p1;
    acc[2] = z.p0;
    acc[3] = z.p1;
}
static inline void port_g3d_vtx_key(float* acc, const void* vtx, float w) {
    port_ps kxy = port_psq_l_be(vtx, 0);
    port_ps kz = port_psq_l_be((const unsigned char*)vtx + 8, 1);
    port_ps xy = port_ps_madd(kxy, port_ps_splat(w), port_ps_make(acc[0], acc[1]));
    port_ps z = port_ps_madd(kz, port_ps_splat(w), port_ps_make(acc[2], acc[3]));
    acc[0] = xy.p0;
    acc[1] = xy.p1;
    acc[2] = z.p0;
    acc[3] = z.p1;
}
static inline void port_g3d_vtx_store(void* dst, const float* acc) {
    port_psq_st_be(dst, port_ps_make(acc[0], acc[1]), 0);
    port_psq_st_be((unsigned char*)dst + 8, port_ps_make(acc[2], acc[3]), 1);
}

/* CalcVtx colour blend: GQR2 (u8, scale 0) loads and stores. */
static inline void port_g3d_clr_first(float* acc, const void* clr, float w) {
    const unsigned char* c = (const unsigned char*)clr;
    int i;
    for (i = 0; i < 4; i++) {
        acc[i] = (float)c[i] * w;
    }
}
static inline void port_g3d_clr_key(float* acc, const void* clr, float w) {
    const unsigned char* c = (const unsigned char*)clr;
    int i;
    for (i = 0; i < 4; i++) {
        acc[i] = port_fmaf((float)c[i], w, acc[i]);
    }
}

/******************************************************************************
 * RVL MTX library (src/revolution/MTX)
 ******************************************************************************/

/* PSMTXConcat (unit_p0 = a3) and PSMTXConcatArray (unit_p0 = a2): the last
 * column adds the translation with (0, 1) * (a2 or a3, a3), fused. */
static inline void port_mtx_concat_row(float* d, const float* a, const port_ps* b, int array) {
    port_ps a01 = port_psq_l(a, 0), a23 = port_psq_l(a + 2, 0);
    port_ps unit = port_ps_make(0.0f, 1.0f);
    port_ps c01 = port_ps_muls0(b[0], a01);
    port_ps c23 = port_ps_muls0(b[1], a01);
    c01 = port_ps_madds1(b[2], a01, c01);
    c23 = port_ps_madds1(b[3], a01, c23);
    c01 = port_ps_madds0(b[4], a23, c01);
    c23 = port_ps_madds0(b[5], a23, c23);
    c23 = array ? port_ps_madd(unit, a23, c23) : port_ps_madds1(unit, a23, c23);
    port_psq_st(d, c01, 0);
    port_psq_st(d + 2, c23, 0);
}
static inline void port_mtx_concat(const float* a, const float* b, float* d, int array) {
    port_ps bb[6];
    float ta[12];
    int r;
    for (r = 0; r < 6; r++) {
        bb[r] = port_psq_l(b + 2 * r, 0);
    }
    __builtin_memcpy(ta, a, sizeof ta);
    for (r = 0; r < 3; r++) {
        port_mtx_concat_row(d + 4 * r, ta + 4 * r, bb, array);
    }
}

/* PSMTXMultVec */
static inline void port_mtx_mult_vec(const float* m, const float* v, float* out) {
    port_ps xy = port_psq_l(v, 0), z1 = port_psq_l(v + 2, 1);
    float r[3];
    int i;
    for (i = 0; i < 3; i++) {
        port_ps w = port_ps_mul(port_psq_l(m + 4 * i, 0), xy);
        w = port_ps_madd(port_psq_l(m + 4 * i + 2, 0), z1, w);
        r[i] = w.p0 + w.p1;
    }
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
}

/* PSVECNormalize */
static inline void port_vec_normalize(const float* in, float* out) {
    port_ps xy = port_psq_l(in, 0), z = port_psq_l(in + 2, 1);
    port_ps w0 = port_ps_mul(xy, xy);
    port_ps z2 = port_ps_madd(z, z, w0);
    float dot = z2.p0 + w0.p1;
    port_ps s = port_ps_splat(port_frsqrt_nr(dot));
    port_psq_st(out, port_ps_muls0(xy, s), 0);
    port_psq_st(out + 2, port_ps_muls0(z, s), 1);
}

/* PSVECMag */
static inline float port_vec_mag(const float* v) {
    port_ps xy = port_psq_l(v, 0), z = port_ps_splat(v[2]);
    port_ps xy2 = port_ps_mul(xy, xy);
    port_ps z2 = port_ps_madd(z, z, xy2);
    float dot = z2.p0 + xy2.p1;
    if (dot == 0.0f) {
        return dot;
    }
    return dot * port_frsqrt_nr(dot);
}

/* PSVECDotProduct */
static inline float port_vec_dot(const float* a, const float* b) {
    port_ps yz = port_ps_mul(port_psq_l(a + 1, 0), port_psq_l(b + 1, 0));
    port_ps s = port_ps_madd(port_psq_l(a, 0), port_psq_l(b, 0), yz);
    return s.p0 + yz.p1;
}

/* PSVECCrossProduct */
static inline void port_vec_cross(const float* a, const float* b, float* p) {
    port_ps f1 = port_psq_l(b, 0), f2 = port_ps_splat(a[2]), f0 = port_psq_l(a, 0);
    port_ps f6 = port_ps_merge10(f1, f1), f3 = port_ps_splat(b[2]);
    port_ps f4 = port_ps_mul(f1, f2);
    port_ps f7 = port_ps_muls0(f1, f0);
    port_ps f5 = port_ps_msub(f0, f3, f4);
    port_ps f8 = port_ps_msub(f0, f6, f7);
    port_ps f9 = port_ps_merge11(f5, f5);
    port_ps f10 = port_ps_neg(port_ps_merge01(f5, f8));
    p[0] = f9.p0;
    port_psq_st(p + 1, f10, 0);
}

/* PSVECSquareDistance */
static inline float port_vec_square_distance(const float* a, const float* b) {
    port_ps dxy = port_ps_sub(port_psq_l(a, 0), port_psq_l(b, 0));
    port_ps dyz = port_ps_sub(port_psq_l(a + 1, 0), port_psq_l(b + 1, 0));
    port_ps d;
    dyz = port_ps_mul(dyz, dyz);
    d = port_ps_madd(dxy, dxy, dyz);
    return d.p0 + dyz.p1;
}

/* PSQUATMultiply */
static inline void port_quat_multiply(const float* a, const float* b, float* p) {
    port_ps axy = port_psq_l(a, 0), azw = port_psq_l(a + 2, 0);
    port_ps bxy = port_psq_l(b, 0), bzw = port_psq_l(b + 2, 0);
    port_ps naxy = port_ps_neg(axy), nazw = port_ps_neg(azw);
    port_ps w1 = port_ps_muls0(azw, bxy);
    port_ps naxay = port_ps_merge01(naxy, axy);
    port_ps w2 = port_ps_merge01(nazw, azw);
    port_ps w3 = port_ps_muls0(naxy, bxy);
    port_ps w4 = port_ps_muls1(naxay, bxy);
    port_ps w5;
    w1 = port_ps_madds0(naxay, bzw, w1);
    w5 = port_ps_muls1(w2, bxy);
    w3 = port_ps_madds0(w2, bzw, w3);
    w1 = port_ps_merge10(w1, w1);
    w5 = port_ps_madds1(axy, bzw, w5);
    w3 = port_ps_merge10(w3, w3);
    w4 = port_ps_madds1(nazw, bzw, w4);
    w1 = port_ps_add(w1, w5);
    w3 = port_ps_sub(w3, w4);
    port_psq_st(p, w1, 0);
    port_psq_st(p + 2, w3, 0);
}

/* PSQUATNormalize: the refinement is in double precision (fmul / fnmsub), and
 * the double scale is ps_muls0's C operand (rounded to 25 fraction bits). */
static inline void port_quat_normalize(const float* in, float* out) {
    port_ps xy = port_psq_l(in, 0), zw = port_psq_l(in + 2, 0);
    port_ps xy2 = port_ps_mul(xy, xy);
    port_ps dot2 = port_ps_madd(zw, zw, xy2);
    float dot = dot2.p0 + dot2.p1;
    double r = port_frsqrte(dot);
    double sq = r * r, half = r * 0.5;
    double t = -port_fma(sq, (double)dot, -3.0);
    double scale = t * half;
    float w1 = dot - 1e-5f;
    double sel = w1 >= 0.0f ? scale : (double)(1e-5f - 1e-5f);
    out[0] = port_fmuls_d(xy.p0, sel);
    out[1] = port_fmuls_d(xy.p1, sel);
    out[2] = port_fmuls_d(zw.p0, sel);
    out[3] = port_fmuls_d(zw.p1, sel);
}

#ifdef __cplusplus
}
#endif

#endif
