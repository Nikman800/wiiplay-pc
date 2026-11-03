/* port_fmaf / port_fma: x*y+z rounded once (IEEE fusedMultiplyAdd), the port's
 * own, on every host (port/docs/HOST_BUILD.md §42).
 *
 * Broadway's fused single-precision instructions (fmadds, fmsubs, fnmadds,
 * fnmsubs, ps_madd, ps_msub, ps_nmsub, ps_madds0/1, ...) round once when their
 * operands are singles, which they always are in the game's paired-single code
 * (Dolphin, Interpreter_FPUtils.h NI_madd_msub); the host forms (port/ps.h,
 * port/ps_forms.h, decomp/compat.h) call port_fmaf for each. PSQUATNormalize has
 * one double-precision fnmsub: port_fma. Not the C library's fmaf / fma: those
 * are exact too in glibc, but not in MinGW-w64, and the game's math does not
 * depend on the host's C library.
 *
 * The vfmadd instructions when the CPU has FMA (and the OS saves the YMM state),
 * else exact software forms, which give the same bits (checked against glibc on
 * 2^30 random inputs each; docs/HOST_BUILD.md §41). Round-to-nearest, as the game
 * always runs.
 */

#include <stdint.h>
#include <string.h>

#include <port/ps.h>

static int cpu_has_fma(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t a, b, c, d;
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (a < 1) {
        return 0;
    }
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    int fma = (c >> 12) & 1, osxsave = (c >> 27) & 1, avx = (c >> 28) & 1;
    if (!fma || !osxsave || !avx) {
        return 0;
    }
    uint32_t xcr0_lo, xcr0_hi;
    __asm__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
    if ((xcr0_lo & 6) != 6) { /* XMM and YMM state */
        return 0;
    }
    return 1;
#else
    return 0;
#endif
}

__attribute__((target("fma"))) static double fma_hw(double x, double y, double z) {
    __asm__("vfmadd213sd %2, %1, %0" : "+x"(x) : "x"(y), "x"(z));
    return x;
}

__attribute__((target("fma"))) static float fmaf_hw(float x, float y, float z) {
    __asm__("vfmadd213ss %2, %1, %0" : "+x"(x) : "x"(y), "x"(z));
    return x;
}

static inline uint64_t asu64(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

static inline double asf64(uint64_t u) {
    double d;
    memcpy(&d, &u, 8);
    return d;
}

/* x*y+z for float, exact: the product is exact in double (24 + 24 bits); the sum
 * is computed exactly as hi + lo (TwoSum) and rounded to odd in double, which then
 * rounds correctly to float (53 >= 2 * 24 + 2). Assumes round-to-nearest, as the
 * game always runs. */
static float fmaf_sw(float x, float y, float z) {
    double xy = (double)x * (double)y;
    double zd = z;
    double s = xy + zd;
    if (!__builtin_isfinite(s) || xy == 0 || zd == 0) {
        /* overflow, inf, NaN, or an exact zero operand: one rounding already */
        return (float)s;
    }
    double bb = s - xy;
    double err = (xy - (s - bb)) + (zd - bb); /* s + err == xy + z exactly */
    uint64_t u = asu64(s);
    if (err != 0 && !(u & 1)) {
        /* round to odd: move s one ulp toward the exact value, which sets the
           sticky last bit */
        if ((err > 0) == (s > 0)) {
            u++;
        } else {
            u--;
        }
        s = asf64(u);
    }
    return (float)s;
}

#if defined(__SIZEOF_INT128__)
/* x*y+z for double, exact in round-to-nearest: the product and the sum in 128-bit
 * integers (every operand normalized to its top bit at bit 125, the smaller one
 * shifted right with a sticky bit), then rounded once to double, subnormals
 * included. */
typedef unsigned __int128 u128;

static int msb128(u128 v) {
    uint64_t hi = (uint64_t)(v >> 64);
    return hi ? 127 - __builtin_clzll(hi) : 63 - __builtin_clzll((uint64_t)v);
}

/* |d| = m * 2^e with m a 53-bit integer (subnormals: fewer bits) */
static uint64_t split_double(double d, int* e) {
    uint64_t u = asu64(d);
    int be = (int)((u >> 52) & 0x7ff);
    uint64_t m = u & 0xfffffffffffffull;
    if (be == 0) {
        *e = -1074;
        return m;
    }
    *e = be - 1075;
    return m | (1ull << 52);
}

static u128 normalize125(u128 v, int* e) {
    int s = 125 - msb128(v);
    *e -= s;
    return v << s;
}

static double fma_sw(double x, double y, double z) {
    if (!__builtin_isfinite(x) || !__builtin_isfinite(y) || x == 0 || y == 0) {
        return x * y + z; /* the product is exact (0, inf or NaN): one rounding */
    }
    if (!__builtin_isfinite(z)) {
        return z + z; /* z itself (a NaN quieted); x*y is finite */
    }
    if (z == 0) {
        double p = x * y;
        /* one rounding of the product; if it rounds to zero, the exact sum is
           the nonzero product, so the zero takes the product's sign */
        return p == 0 ? p : p + z;
    }
    int ex, ey, ez;
    uint64_t mx = split_double(x, &ex), my = split_double(y, &ey), mz = split_double(z, &ez);
    int sp = (int)((asu64(x) ^ asu64(y)) >> 63), sz = (int)(asu64(z) >> 63);
    int ea = ex + ey, eb = ez;
    u128 a = normalize125((u128)mx * my, &ea);
    u128 b = normalize125((u128)mz, &eb);
    int sa = sp, sb = sz;
    if (eb > ea || (eb == ea && b > a)) {
        u128 t = a; a = b; b = t;
        int te = ea; ea = eb; eb = te;
        int ts = sa; sa = sb; sb = ts;
    }
    /* now |a * 2^ea| >= |b * 2^eb| */
    int d = ea - eb;
    if (d >= 128) {
        b = 1; /* sticky only */
    } else if (d > 0) {
        u128 lost = b & ((((u128)1) << d) - 1);
        b = (b >> d) | (lost != 0);
    }
    u128 r = sa == sb ? a + b : a - b;
    int sign = sa;
    int e = ea;
    if (r == 0) {
        return 0.0; /* exact cancellation: +0 in round-to-nearest */
    }
    int m = msb128(r);
    int E = m + e; /* the result is in [2^E, 2^(E+1)) */
    if (E > 1023) {
        return sign ? -__builtin_inf() : __builtin_inf();
    }
    int shift = E >= -1022 ? m - 52 : -1074 - e;
    uint64_t mant;
    if (shift <= 0) {
        mant = (uint64_t)(r << -shift);
    } else if (shift >= 128) {
        mant = 0;
    } else {
        u128 q = r >> shift;
        u128 rem = r & ((((u128)1) << shift) - 1);
        u128 half = ((u128)1) << (shift - 1);
        mant = (uint64_t)q;
        if (rem > half || (rem == half && (mant & 1))) {
            mant++;
        }
    }
    uint64_t bits;
    if (E >= -1022) {
        /* mant in [2^52, 2^53]: its top bit adds one to the exponent field */
        bits = ((uint64_t)(E + 1022) << 52) + mant;
    } else {
        bits = mant; /* subnormal; 2^52 after rounding is the smallest normal */
    }
    if (bits >= 0x7ff0000000000000ull) {
        bits = 0x7ff0000000000000ull;
    }
    return asf64(bits | ((uint64_t)sign << 63));
}
#else
/* No 128-bit integers (the -m32 Linux test build only): the C library's exact fma (glibc's
 * is correctly rounded). Not the game's builds (x86-64 Linux and Windows). */
static double fma_sw(double x, double y, double z) {
    return __builtin_fma(x, y, z);
}
#endif

static int port_fma_hw = -1;

static int fma_available(void) {
    int hw = __atomic_load_n(&port_fma_hw, __ATOMIC_RELAXED);
    if (__builtin_expect(hw < 0, 0)) {
        /* Several threads may get here at once; they all store the same value. */
        hw = cpu_has_fma();
        __atomic_store_n(&port_fma_hw, hw, __ATOMIC_RELAXED);
    }
    return hw;
}

double port_fma(double x, double y, double z) {
    return fma_available() ? fma_hw(x, y, z) : fma_sw(x, y, z);
}

float port_fmaf(float x, float y, float z) {
    return fma_available() ? fmaf_hw(x, y, z) : fmaf_sw(x, y, z);
}

/* The software forms alone, for the tests (tests/libm_compare.c). */
double port_fma_sw(double x, double y, double z) {
    return fma_sw(x, y, z);
}
float port_fmaf_sw(float x, float y, float z) {
    return fmaf_sw(x, y, z);
}
