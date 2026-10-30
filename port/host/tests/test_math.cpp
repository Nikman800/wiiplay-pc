/**
 * Host forms of the paired-single asm (port/docs/HOST_BUILD.md §18) against
 * straightforward reference math.
 *
 * The decomp functions are called where the host library builds them
 * (nw4r::math, g3d_cpu.cpp, the RVL MTX library); the g3d / game blocks whose
 * units are not built yet are run through the same compat.h macros in a scope
 * with the variables they name (CalcSkin*, CalcVtx*, CalcClr*,
 * SetMdlViewMtxSR*, FogCopyTo, PointerScreenWidth*, BlockAbs) or through
 * their port/ps_forms.h function (GetModelLocalAxisY2/3).
 *
 * Exact checks: results that are exact on the Wii (copies, identities, sign
 * flips, products and sums of small integers, quantized stores). Tolerances:
 * where the Wii rounds differently from a double-precision reference (fused
 * multiply-adds, paired sums) the bound is a few ulp of the operand
 * magnitudes; where it starts from an estimate (fres / frsqrte, then
 * Newton-Raphson) the host starts exact, and the bound is 2^-21 relative
 * (about 4 ulp), which also holds for the Wii's refined estimate.
 */

#include <nw4r/g3d/platform/g3d_cpu.h>
#include <nw4r/math.h>
#include <nw4r/ut/ut_algorithm.h>
#include <revolution/MTX.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int sFailures = 0, sChecks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        sChecks++;                                                             \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,       \
                         __LINE__, #cond);                                     \
            sFailures++;                                                       \
        }                                                                      \
    } while (0)

// |got - want| <= rel * scale (scale: the magnitude the error is relative to).
bool Near(double got, double want, double rel, double scale) {
    if (std::isnan(got) || std::isnan(want)) {
        return std::isnan(got) && std::isnan(want);
    }
    return std::fabs(got - want) <= rel * (scale > 1e-30 ? scale : 1e-30);
}

#define CHECK_NEAR(got, want, rel, scale)                                      \
    do {                                                                       \
        sChecks++;                                                             \
        double g_ = (got), w_ = (want);                                        \
        if (!Near(g_, w_, (rel), (scale))) {                                   \
            std::fprintf(stderr, "%s:%d: %s = %.9g, want %.9g\n", __FILE__,   \
                         __LINE__, #got, g_, w_);                              \
            sFailures++;                                                       \
        }                                                                      \
    } while (0)

const double kUlps = 4.0 / (1 << 23);  // a few ulp
const double kEst = 1.0 / (1 << 21);   // estimate + Newton-Raphson

u32 Bits(f32 f) {
    u32 u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Deterministic pseudo-random floats in [-r, r].
u32 sRand = 12345;
f32 Rnd(f32 r) {
    sRand = sRand * 1103515245u + 12345u;
    return (static_cast<f32>((sRand >> 8) & 0xFFFF) / 32767.5f - 1.0f) * r;
}

void RandMtx(f32* m, int n, f32 r) {
    for (int i = 0; i < n; i++) {
        m[i] = Rnd(r);
    }
}

// Inverse of the 3x3 part of a 3x4 matrix, in double.
bool Inv33(const f32* m, double inv[3][3]) {
    double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9],
           i = m[10];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det == 0.0) {
        return false;
    }
    inv[0][0] = (e * i - f * h) / det;
    inv[0][1] = (c * h - b * i) / det;
    inv[0][2] = (b * f - c * e) / det;
    inv[1][0] = (f * g - d * i) / det;
    inv[1][1] = (a * i - c * g) / det;
    inv[1][2] = (c * d - a * f) / det;
    inv[2][0] = (d * h - e * g) / det;
    inv[2][1] = (b * g - a * h) / det;
    inv[2][2] = (a * e - b * d) / det;
    return true;
}

/******************************************************************************
 * nw4r::math (math_types.cpp, math_arithmetic.cpp, ut_algorithm.h)
 ******************************************************************************/
void TestNw4rMath() {
    using namespace nw4r::math;

    // Identities and copies: exact.
    MTX33 m33;
    std::memset(static_cast<void*>(&m33), 0xEE, sizeof m33);
    MTX33Identity(&m33);
    for (int i = 0; i < 9; i++) {
        CHECK(Bits((&m33._00)[i]) == Bits(i % 4 == 0 ? 1.0f : 0.0f));
    }
    MTX44 m44, c44;
    std::memset(static_cast<void*>(&m44), 0xEE, sizeof m44);
    MTX44Identity(&m44);
    for (int i = 0; i < 16; i++) {
        CHECK(Bits((&m44._00)[i]) == Bits(i % 5 == 0 ? 1.0f : 0.0f));
    }
    RandMtx(&m44._00, 16, 100.0f);
    MTX44Copy(&c44, &m44);
    CHECK(std::memcmp(&c44, &m44, sizeof m44) == 0);
    MTX44Copy(&c44, &c44);
    CHECK(std::memcmp(&c44, &m44, sizeof m44) == 0);

    MTX34 a, b;
    RandMtx(&a._00, 12, 10.0f);
    std::memset(static_cast<void*>(&m33), 0xEE, sizeof m33);
    MTX34ToMTX33(&m33, &a);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            CHECK(Bits(m33.m[r][c]) == Bits(a.m[r][c]));
        }
    }
    std::memset(static_cast<void*>(&b), 0xEE, sizeof b);
    MTX34Zero(&b);
    for (int i = 0; i < 12; i++) {
        CHECK(Bits((&b._00)[i]) == 0);
    }

    // MTX34Scale: one product per element, exact as a float product.
    VEC3 s(2.5f, -3.0f, 0.125f);
    MTX34Scale(&b, &a, &s);
    for (int r = 0; r < 3; r++) {
        CHECK(Bits(b.m[r][0]) == Bits(a.m[r][0] * s.x));
        CHECK(Bits(b.m[r][1]) == Bits(a.m[r][1] * s.y));
        CHECK(Bits(b.m[r][2]) == Bits(a.m[r][2] * s.z));
        CHECK(Bits(b.m[r][3]) == Bits(a.m[r][3]));
    }
    b = a;
    MTX34Scale(&b, &b, &s);  // in place
    CHECK(Bits(b.m[1][2]) == Bits(a.m[1][2] * s.z));

    // MTX34Trans: the rotation copied, the translation M * t + old.
    VEC3 t(1.5f, -7.25f, 3.0f);
    for (int k = 0; k < 50; k++) {
        RandMtx(&a._00, 12, 10.0f);
        t = VEC3(Rnd(50.0f), Rnd(50.0f), Rnd(50.0f));
        MTX34Trans(&b, &a, &t);
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                CHECK(Bits(b.m[r][c]) == Bits(a.m[r][c]));
            }
            double want = (double)a.m[r][0] * t.x + (double)a.m[r][1] * t.y +
                          (double)a.m[r][2] * t.z + a.m[r][3];
            double mag = std::fabs(a.m[r][0] * t.x) + std::fabs(a.m[r][1] * t.y) +
                         std::fabs(a.m[r][2] * t.z) + std::fabs(a.m[r][3]);
            CHECK_NEAR(b.m[r][3], want, kUlps, mag);
        }
    }
    // Small integers: exact.
    std::memset(static_cast<void*>(&a), 0, sizeof a);
    a._00 = 1.0f, a._01 = 2.0f, a._02 = 3.0f, a._03 = 4.0f;
    a._11 = 1.0f, a._22 = 1.0f, a._23 = -1.0f;
    t = VEC3(1.0f, 10.0f, 100.0f);
    MTX34Trans(&a, &a, &t);  // in place
    CHECK(a._03 == 325.0f && a._13 == 10.0f && a._23 == 99.0f);

    // MTX34InvTranspose: fres and two Newton-Raphson steps.
    for (int k = 0; k < 200; k++) {
        RandMtx(&a._00, 12, 4.0f);
        double inv[3][3];
        if (!Inv33(&a._00, inv)) {
            continue;
        }
        double det = 0;
        double maxInv = 0;
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                maxInv = std::fmax(maxInv, std::fabs(inv[r][c]));
            }
        }
        (void)det;
        // Ill-conditioned matrices lose precision in single as on the Wii.
        if (maxInv > 4.0) {
            continue;
        }
        std::memset(static_cast<void*>(&m33), 0xEE, sizeof m33);
        CHECK(MTX34InvTranspose(&m33, &a) == TRUE);
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                CHECK_NEAR(m33.m[r][c], inv[c][r], 1.0 / (1 << 15), maxInv);
            }
        }
    }
    std::memset(static_cast<void*>(&a), 0, sizeof a);
    a._00 = 2.0f, a._11 = 4.0f, a._22 = 0.5f, a._01 = 1.0f;
    CHECK(MTX34InvTranspose(&m33, &a) == TRUE);
    CHECK(m33._00 == 0.5f && m33._11 == 0.25f && m33._22 == 2.0f && m33._10 == -0.125f &&
          m33._01 == 0.0f);
    std::memset(static_cast<void*>(&a), 0, sizeof a);
    a._00 = 1.0f, a._01 = 2.0f, a._10 = 2.0f, a._11 = 4.0f, a._22 = 1.0f;  // singular
    std::memset(static_cast<void*>(&m33), 0xEE, sizeof m33);
    CHECK(MTX34InvTranspose(&m33, &a) == FALSE);
    CHECK(Bits(m33._00) == 0xEEEEEEEEu);  // left alone

    // VEC2Normalize and FrSqrt: frsqrte + one Newton-Raphson step.
    for (int k = 0; k < 200; k++) {
        VEC2 v(Rnd(1000.0f), Rnd(1000.0f)), n;
        double len = std::sqrt((double)v.x * v.x + (double)v.y * v.y);
        if (len == 0.0) {
            continue;
        }
        VEC2Normalize(&n, &v);
        CHECK_NEAR(n.x, v.x / len, kEst, 1.0);
        CHECK_NEAR(n.y, v.y / len, kEst, 1.0);
        f32 x = std::fabs(Rnd(1e4f)) + 1e-3f;
        CHECK_NEAR(FrSqrt(x), 1.0 / std::sqrt((double)x), kEst, 1.0 / std::sqrt((double)x));
    }
    VEC2 v(3.0f, 4.0f), n;
    VEC2Normalize(&v, &v);  // in place
    CHECK_NEAR(v.x, 0.6, kEst, 1.0);
    CHECK_NEAR(v.y, 0.8, kEst, 1.0);
    CHECK(FrSqrt(4.0f) == 0.5f);
    (void)n;

    // ut::Abs<f32>: fabs (sign bit cleared, -0 and NaN included).
    CHECK(nw4r::ut::Abs(-3.5f) == 3.5f);
    CHECK(nw4r::ut::Abs(2.0f) == 2.0f);
    CHECK(Bits(nw4r::ut::Abs(-0.0f)) == 0);
    CHECK(Bits(nw4r::ut::Abs(-std::nanf(""))) == Bits(std::nanf("")));
}

/******************************************************************************
 * nw4r::g3d (g3d_cpu.cpp; the calc units through their macros / helpers)
 ******************************************************************************/
void TestG3d() {
    // Copy32ByteBlocks / ZeroMemory32ByteBlocks: whole 32-byte blocks only.
    u8 src[100], dst[100];
    for (int i = 0; i < 100; i++) {
        src[i] = static_cast<u8>(i * 7 + 1);
    }
    std::memset(dst, 0xEE, sizeof dst);
    nw4r::g3d::detail::Copy32ByteBlocks(dst, src, 70);
    CHECK(std::memcmp(dst, src, 64) == 0 && dst[64] == 0xEE);
    nw4r::g3d::detail::ZeroMemory32ByteBlocks(dst, 40);
    CHECK(dst[0] == 0 && dst[31] == 0 && dst[32] == src[32]);

    // Fog::CopyTo's block: six lfd/stfd, 48 bytes moved bit for bit.
    {
        u8 fogSrc[56], fogDst[56];
        for (int i = 0; i < 56; i++) {
            fogSrc[i] = static_cast<u8>(0xF0 ^ i);  // NaN patterns included
        }
        std::memset(fogDst, 0xEE, sizeof fogDst);
        const void* pSrc = fogSrc;
        void* pDst = fogDst;
        PORT_PS_FogCopyTo()
        CHECK(std::memcmp(fogDst, fogSrc, 48) == 0 && fogDst[48] == 0xEE);
    }

    // CalcSkinning: M = sum of R_i * T_i over the weights, fused, from zero.
    for (int k = 0; k < 20; k++) {
        nw4r::math::MTX34 mats[3], out;
        f32 w[3] = {Rnd(1.0f), Rnd(1.0f), Rnd(1.0f)};
        for (int i = 0; i < 3; i++) {
            RandMtx(&mats[i]._00, 12, 10.0f);
        }
        PORT_PS_CalcSkinZero()
        for (int i = 0; i < 3; i++) {
            f32 R = w[i];
            nw4r::math::MTX34* pT = &mats[i];
            PORT_PS_CalcSkinAdd()
        }
        nw4r::math::MTX34* pTargetMtx = &out;
        PORT_PS_CalcSkinStore()
        for (int e = 0; e < 12; e++) {
            double want = 0, mag = 0;
            for (int i = 0; i < 3; i++) {
                want += (double)w[i] * (&mats[i]._00)[e];
                mag += std::fabs(w[i] * (&mats[i]._00)[e]);
            }
            CHECK_NEAR((&out._00)[e], want, kUlps, mag);
        }
    }

    // CalcVtx: big-endian vertex arrays, weighted sum of the key shapes.
    {
        f32 shapes[3][3] = {{1.0f, -2.0f, 3.0f}, {10.0f, 20.0f, -30.0f}, {0.5f, 0.25f, 4.0f}};
        f32 weights[3] = {0.5f, 0.25f, 2.0f};
        u8 be[3][12], outBe[16];
        for (int s = 0; s < 3; s++) {
            for (int c = 0; c < 3; c++) {
                port_ps_store_be_f32(&be[s][4 * c], shapes[s][c]);
            }
        }
        std::memset(outBe, 0xEE, sizeof outBe);
        {
            f32 firstWeight = weights[0];
            const void* pVtx = be[0];
            PORT_PS_CalcVtxFirst()
            for (int s = 1; s < 3; s++) {
                f32 keyWeight = weights[s];
                const void* pKeyVtx = be[s];
                PORT_PS_CalcVtxKey()
            }
            void* pDst = outBe;
            PORT_PS_CalcVtxStore()
        }
        for (int c = 0; c < 3; c++) {
            f32 want = shapes[0][c] * 0.5f + shapes[1][c] * 0.25f + shapes[2][c] * 2.0f;
            CHECK(port_ps_load_be_f32(&outBe[4 * c]) == want);  // exact: small dyadics
        }
        CHECK(outBe[12] == 0xEE);  // z is stored alone (W=1)
    }

    // CalcVtx colours: u8 loads, weighted sum, clamped / truncated u8 stores.
    {
        u8 c0[4] = {200, 100, 0, 255}, c1[4] = {100, 255, 10, 255};
        u8 out[5] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
        f32 firstWeight = 0.75f;
        const void* pVtx = c0;
        PORT_PS_CalcClrFirst()
        f32 keyWeight = 0.5f;
        const void* pKeyVtx = c1;
        PORT_PS_CalcClrKey()
        void* pDst = out;
        PORT_PS_CalcClrStore()
        CHECK(out[0] == 200);  // 150 + 50
        CHECK(out[1] == 202);  // 75 + 127.5 -> truncated
        CHECK(out[2] == 5);
        CHECK(out[3] == 255);  // 191.25 + 127.5 clamped
        CHECK(out[4] == 0xEE);
    }

    // SetMdlViewMtxSR (three forms): exact products and sign flips.
    {
        nw4r::math::MTX34 m;
        nw4r::math::MTX34* pViewPos = &m;
        nw4r::math::VEC3 rRX(1.0f, 2.0f, 3.0f), rRY(0.5f, -0.75f, 9.0f), rRZ(-4.0f, 5.0f, 6.0f);
        f32 s = 2.0f, sx = 3.0f, sy = 0.5f, sz = -1.0f;
        std::memset(static_cast<void*>(&m), 0xEE, sizeof m);
        PORT_PS_SetMdlViewMtxSR1()
        CHECK(m._00 == -1.5f && m._01 == 1.0f && m._02 == 0.0f);
        CHECK(m._10 == -1.0f && m._11 == -1.5f && m._12 == 0.0f);
        CHECK(m._20 == 0.0f && m._21 == 0.0f && m._22 == 2.0f);
        CHECK(Bits(m._03) == 0xEEEEEEEEu);
        PORT_PS_SetMdlViewMtxSR3()
        CHECK(m._00 == -0.75f * 3.0f && m._01 == 0.5f * 0.5f && m._22 == -1.0f);
        CHECK(m._10 == -0.5f * 3.0f && m._11 == -0.75f * 0.5f);
        PORT_PS_SetMdlViewMtxSR7()
        CHECK(m._00 == 3.0f && m._01 == 0.25f && m._02 == 4.0f);
        CHECK(m._10 == 6.0f && m._11 == -0.375f && m._12 == -5.0f);
        CHECK(m._20 == 9.0f && m._21 == 4.5f && m._22 == -6.0f);
    }

    // GetModelLocalAxisY2 / Y3: x and y are rows 0 and 1 of M times column 1
    // of P^-1; Y2 stores z = 0. Y3's z, as the Wii's asm computes it (the
    // register its row-2 product reads holds (inv21, (P02 - P22) / det), not
    // (inv01, inv11)): z = (M20 + M22) * inv21 + M21 * (P02 - P22) / det.
    for (int k = 0; k < 200; k++) {
        nw4r::math::MTX34 m, p;
        RandMtx(&m._00, 12, 4.0f);
        RandMtx(&p._00, 12, 4.0f);
        double inv[3][3];
        if (!Inv33(&p._00, inv)) {
            continue;
        }
        double maxInv = 0;
        for (int r = 0; r < 3; r++) {
            maxInv = std::fmax(maxInv, std::fabs(inv[r][1]));
        }
        if (maxInv > 4.0) {
            continue;
        }
        f32 v2[3], v3[3];
        port_g3d_local_axis_y(v2, &m._00, &p._00, 0);
        port_g3d_local_axis_y(v3, &m._00, &p._00, 1);
        for (int r = 0; r < 3; r++) {
            double want = 0, mag = 0;
            for (int c = 0; c < 3; c++) {
                want += m.m[r][c] * inv[c][1];
                mag += std::fabs(m.m[r][c] * inv[c][1]);
            }
            if (r == 2) {
                double q = ((double)p._02 - p._22) * inv[0][0] /
                           ((double)p._11 * p._22 - (double)p._21 * p._12);
                if (!std::isfinite(q) || std::fabs(q) > 64.0) {
                    continue;
                }
                want = ((double)m._20 + m._22) * inv[2][1] + m._21 * q;
                mag = std::fabs((m._20 + m._22) * inv[2][1]) + std::fabs(m._21 * q);
            }
            CHECK_NEAR(v3[r], want, 1.0 / (1 << 15), mag + maxInv);
            if (r < 2) {
                CHECK(Bits(v2[r]) == Bits(v3[r]));
            }
        }
        CHECK(v2[2] == 0.0f);
    }
    {
        nw4r::math::MTX34 m, p;
        std::memset(static_cast<void*>(&p), 0, sizeof p);  // singular parent: (0, 0, 0)
        RandMtx(&m._00, 12, 4.0f);
        f32 v[3] = {1, 1, 1};
        port_g3d_local_axis_y(v, &m._00, &p._00, 1);
        CHECK(v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f);
    }
}

/******************************************************************************
 * Game blocks (RPHkyPaddle, RPTnkBlock)
 ******************************************************************************/
void TestGameBlocks() {
    u16 widths[3] = {608, 640, 704};
    u16 width = 0;
    const u16* table = widths;
    u32 offset = 2 * 2;
    PORT_PS_PointerScreenWidthLoad()
    PORT_PS_PointerScreenWidthMove()
    CHECK(width == 704);
    f32 value = -2.25f;
    PORT_PS_BlockAbs()
    CHECK(value == 2.25f);
}

/******************************************************************************
 * RVL MTX library (src/revolution/MTX)
 ******************************************************************************/
void TestRvlMtx() {
    Mtx a, b, c, id;
    std::memset(id, 0xEE, sizeof id);
    PSMTXIdentity(id);
    for (int i = 0; i < 12; i++) {
        CHECK(Bits((&id[0][0])[i]) == Bits(i % 5 == 0 ? 1.0f : 0.0f));
    }
    RandMtx(&a[0][0], 12, 10.0f);
    PSMTXCopy(a, b);
    CHECK(std::memcmp(a, b, sizeof a) == 0);

    // PSMTXConcat / PSMTXConcatArray against double, and exact with identity.
    for (int k = 0; k < 100; k++) {
        RandMtx(&a[0][0], 12, 10.0f);
        RandMtx(&b[0][0], 12, 10.0f);
        PSMTXConcat(a, b, c);
        for (int r = 0; r < 3; r++) {
            for (int col = 0; col < 4; col++) {
                double want = 0, mag = 0;
                for (int i = 0; i < 3; i++) {
                    want += (double)a[r][i] * b[i][col];
                    mag += std::fabs(a[r][i] * b[i][col]);
                }
                if (col == 3) {
                    want += a[r][3];
                    mag += std::fabs(a[r][3]);
                }
                CHECK_NEAR(c[r][col], want, kUlps, mag);
            }
        }
        Mtx arr[2], outArr[2];
        PSMTXCopy(b, arr[0]);
        PSMTXCopy(a, arr[1]);
        PSMTXConcatArray(a, arr[0], outArr[0], 2);
        CHECK(std::memcmp(outArr[0], c, sizeof c) == 0);
    }
    PSMTXConcat(a, id, c);
    CHECK(std::memcmp(a, c, sizeof c) == 0);
    PSMTXCopy(a, c);
    PSMTXConcat(id, c, c);  // in place
    CHECK(std::memcmp(a, c, sizeof c) == 0);

    // PSMTXMultVec, PSVEC*: double references.
    for (int k = 0; k < 100; k++) {
        RandMtx(&a[0][0], 12, 10.0f);
        Vec v = {Rnd(100.0f), Rnd(100.0f), Rnd(100.0f)}, w = {Rnd(100.0f), Rnd(100.0f), Rnd(100.0f)};
        Vec o;
        PSMTXMultVec(a, &v, &o);
        const f32* po = &o.x;
        const f32* pv = &v.x;
        for (int r = 0; r < 3; r++) {
            double want = a[r][3], mag = std::fabs(a[r][3]);
            for (int i = 0; i < 3; i++) {
                want += (double)a[r][i] * pv[i];
                mag += std::fabs(a[r][i] * pv[i]);
            }
            CHECK_NEAR(po[r], want, kUlps, mag);
        }
        double dot = (double)v.x * w.x + (double)v.y * w.y + (double)v.z * w.z;
        double dmag = std::fabs(v.x * w.x) + std::fabs(v.y * w.y) + std::fabs(v.z * w.z);
        CHECK_NEAR(PSVECDotProduct(&v, &w), dot, kUlps, dmag);
        double len = std::sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z);
        CHECK_NEAR(PSVECMag(&v), len, kEst, len);
        PSVECNormalize(&v, &o);
        CHECK_NEAR(o.x, v.x / len, kEst, 1.0);
        CHECK_NEAR(o.y, v.y / len, kEst, 1.0);
        CHECK_NEAR(o.z, v.z / len, kEst, 1.0);
        PSVECCrossProduct(&v, &w, &o);
        double cx = (double)v.y * w.z - (double)v.z * w.y, cy = (double)v.z * w.x - (double)v.x * w.z,
               cz = (double)v.x * w.y - (double)v.y * w.x;
        double cmag = std::fabs(v.x) * std::fabs(w.x) + std::fabs(v.y) * std::fabs(w.y) +
                      std::fabs(v.z) * std::fabs(w.z) + std::fabs(v.x * w.y) +
                      std::fabs(v.y * w.z) + std::fabs(v.z * w.x);
        CHECK_NEAR(o.x, cx, kUlps, cmag);
        CHECK_NEAR(o.y, cy, kUlps, cmag);
        CHECK_NEAR(o.z, cz, kUlps, cmag);
        double dx = (double)v.x - w.x, dy = (double)v.y - w.y, dz = (double)v.z - w.z;
        CHECK_NEAR(PSVECSquareDistance(&v, &w), dx * dx + dy * dy + dz * dz, kUlps,
                   dx * dx + dy * dy + dz * dz);
        PSVECAdd(&v, &w, &o);
        CHECK(o.x == v.x + w.x && o.y == v.y + w.y && o.z == v.z + w.z);
        PSVECScale(&v, &o, 0.5f);
        CHECK(o.x == v.x * 0.5f && o.y == v.y * 0.5f && o.z == v.z * 0.5f);
    }
    Vec z = {0.0f, 0.0f, 0.0f};
    CHECK(PSVECMag(&z) == 0.0f);
    Vec ex = {1, 0, 0}, ey = {0, 1, 0}, o;
    PSVECCrossProduct(&ex, &ey, &o);
    CHECK(o.x == 0.0f && o.y == 0.0f && o.z == 1.0f);
    PSVECCrossProduct(&ex, &ey, &ex);  // in place
    CHECK(ex.x == 0.0f && ex.y == 0.0f && ex.z == 1.0f);

    // PSQUATMultiply (Hamilton product) and PSQUATNormalize.
    for (int k = 0; k < 100; k++) {
        Quaternion p = {Rnd(2.0f), Rnd(2.0f), Rnd(2.0f), Rnd(2.0f)};
        Quaternion q = {Rnd(2.0f), Rnd(2.0f), Rnd(2.0f), Rnd(2.0f)};
        Quaternion r;
        PSQUATMultiply(&p, &q, &r);
        double wx = (double)p.w * q.x + (double)p.x * q.w + (double)p.y * q.z - (double)p.z * q.y;
        double wy = (double)p.w * q.y - (double)p.x * q.z + (double)p.y * q.w + (double)p.z * q.x;
        double wz = (double)p.w * q.z + (double)p.x * q.y - (double)p.y * q.x + (double)p.z * q.w;
        double ww = (double)p.w * q.w - (double)p.x * q.x - (double)p.y * q.y - (double)p.z * q.z;
        double mag = 16.0;
        CHECK_NEAR(r.x, wx, kUlps, mag);
        CHECK_NEAR(r.y, wy, kUlps, mag);
        CHECK_NEAR(r.z, wz, kUlps, mag);
        CHECK_NEAR(r.w, ww, kUlps, mag);
        double len = std::sqrt((double)p.x * p.x + (double)p.y * p.y + (double)p.z * p.z +
                               (double)p.w * p.w);
        PSQUATNormalize(&p, &r);
        CHECK_NEAR(r.x, p.x / len, kEst, 1.0);
        CHECK_NEAR(r.w, p.w / len, kEst, 1.0);
    }
    Quaternion tiny = {1e-4f, 0, 0, 0}, r;
    PSQUATNormalize(&tiny, &r);  // dot below the epsilon: zero
    CHECK(r.x == 0.0f && r.y == 0.0f && r.z == 0.0f && r.w == 0.0f);
    Quaternion i = {1, 0, 0, 0}, j = {0, 1, 0, 0};
    PSQUATMultiply(&i, &j, &r);  // i * j = k
    CHECK(r.x == 0.0f && r.y == 0.0f && r.z == 1.0f && r.w == 0.0f);

    // C_VECHalfAngle (C, on the PSVEC host forms).
    Vec h, va = {1, 0, 0}, vb = {0, 1, 0};
    C_VECHalfAngle(&va, &vb, &h);
    CHECK_NEAR(h.x, -std::sqrt(0.5), kEst, 1.0);
    CHECK_NEAR(h.y, -std::sqrt(0.5), kEst, 1.0);
    CHECK(h.z == 0.0f);
}

} // namespace

int main() {
    TestNw4rMath();
    TestG3d();
    TestGameBlocks();
    TestRvlMtx();
    if (sFailures != 0) {
        std::printf("test_math: %d of %d checks failed\n", sFailures, sChecks);
        return 1;
    }
    std::printf("test_math: OK, %d checks (pointer size %zu)\n", sChecks, sizeof(void*));
    return 0;
}
