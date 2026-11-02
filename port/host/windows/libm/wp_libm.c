/* The math functions of the Windows build (port/host/Makefile, WIN64=1;
 * docs/HOST_BUILD.md §41, "Floats: the C library's math functions"), computed as
 * glibc 2.43 on x86-64 computes them, so the Windows and Linux builds give the
 * same bits.
 *
 * MinGW-w64's own sinf, cosf, tanf, powf, ... (and msvcrt's, whose results differ
 * between Windows and Wine) round differently in the last bit. glibc's come from
 * two MIT-licensed projects, vendored beside this file (README.md):
 *
 *   arm/        ARM optimized-routines: sinf, cosf, sincosf, powf, pow. glibc
 *               builds them twice, plain x86-64 and with -mfma -mavx2, and picks
 *               one at load time (an IFUNC: the FMA build when the CPU has FMA and
 *               AVX2). So do we, from cpuid; the two builds differ in the last
 *               bit for some inputs.
 *   core-math/  CORE-MATH, correctly rounded: tanf, asinf, acosf, atan2f, sincos.
 *
 * Here: those dispatches; fmaf / fma, exact (the vfmadd instructions when the CPU has
 * FMA, else exact software forms; CORE-MATH's functions call them, and so does the
 * game's paired-single code); hypotf as glibc computes it (in double); modff, sqrtf,
 * sqrt, fminf and fmaxf, with glibc's zero signs and NaNs; roundeven for CORE-MATH's
 * tanf.
 *
 * The Makefile renames the optimized-routines entry points per build (objcopy
 * --redefine-sym: sinf -> wp_arm_sinf_sse2 / wp_arm_sinf_fma); CORE-MATH's keep their
 * cr_ names. With
 * WP_LIBM_PREFIX defined (a Linux build, to compare them with glibc's in one process;
 * README.md) the functions here are named wp_sinf, ... instead of sinf, ....
 */

#include <stdint.h>
#include <string.h>

#ifdef WP_LIBM_PREFIX
#define WP(name) wp_##name
#else
#define WP(name) name
#endif

#define ARM_FN(ret, name, params)                    \
    ret wp_arm_##name##_sse2 params;                 \
    ret wp_arm_##name##_fma params;
ARM_FN(float, sinf, (float))
ARM_FN(float, cosf, (float))
ARM_FN(void, sincosf, (float, float*, float*))
ARM_FN(float, powf, (float, float))
ARM_FN(double, pow, (double, double))

float cr_tanf(float);
float cr_asinf(float);
float cr_acosf(float);
float cr_atan2f(float, float);
void cr_sincos(double, double*, double*);

/* glibc's choice for its FMA variants (sysdeps/x86_64/fpu/multiarch/ifunc-fma.h):
 * FMA and AVX2 usable, which includes the OS saving the YMM registers. */
static int cpu_has_fma_avx2(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t a, b, c, d;
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (a < 7) {
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
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    return (b >> 5) & 1; /* AVX2 */
#else
    return 0;
#endif
}

static struct {
    int ready;
    int fma;
    float (*sinf)(float);
    float (*cosf)(float);
    void (*sincosf)(float, float*, float*);
    float (*powf)(float, float);
    double (*pow)(double, double);
} wp_libm;

static void wp_libm_init(void) {
    /* Several threads may get here at once; they all store the same values. */
    int fma = cpu_has_fma_avx2();
#ifdef WP_LIBM_FORCE_FMA
    fma = WP_LIBM_FORCE_FMA;
#endif
    wp_libm.fma = fma;
    wp_libm.sinf = fma ? wp_arm_sinf_fma : wp_arm_sinf_sse2;
    wp_libm.cosf = fma ? wp_arm_cosf_fma : wp_arm_cosf_sse2;
    wp_libm.sincosf = fma ? wp_arm_sincosf_fma : wp_arm_sincosf_sse2;
    wp_libm.powf = fma ? wp_arm_powf_fma : wp_arm_powf_sse2;
    wp_libm.pow = fma ? wp_arm_pow_fma : wp_arm_pow_sse2;
    __atomic_store_n(&wp_libm.ready, 1, __ATOMIC_RELEASE);
}

#define WP_READY()                                                    \
    do {                                                              \
        if (__builtin_expect(!__atomic_load_n(&wp_libm.ready, __ATOMIC_ACQUIRE), 0)) \
            wp_libm_init();                                           \
    } while (0)

float WP(sinf)(float x) {
    WP_READY();
    return wp_libm.sinf(x);
}

float WP(cosf)(float x) {
    WP_READY();
    return wp_libm.cosf(x);
}

void WP(sincosf)(float x, float* s, float* c) {
    WP_READY();
    wp_libm.sincosf(x, s, c);
}

float WP(powf)(float x, float y) {
    WP_READY();
    return wp_libm.powf(x, y);
}

double WP(pow)(double x, double y) {
    WP_READY();
    return wp_libm.pow(x, y);
}

float WP(tanf)(float x) {
    if (__builtin_isinf(x)) {
        return x - x; /* the default NaN, as glibc's (CORE-MATH's has another payload) */
    }
    return cr_tanf(x);
}

float WP(asinf)(float x) {
    return cr_asinf(x);
}

float WP(acosf)(float x) {
    return cr_acosf(x);
}

float WP(atan2f)(float y, float x) {
    return cr_atan2f(y, x);
}

/* sincos (double; the host's input code): MinGW-w64 defines it in one object with
 * sincosf, so it has to be here too. CORE-MATH's, correctly rounded; glibc's
 * (IBM's, within 0.55 ulp) differs from it in the last bit for about 0.1% of
 * inputs, none of which changed the value rounded to float in 10^8 random inputs
 * (README.md). NaN and infinity: x - x, as glibc. */
void WP(sincos)(double x, double* s, double* c) {
    if (!__builtin_isfinite(x)) {
        *s = *c = x - x;
        return;
    }
    cr_sincos(x, s, c);
}

#ifndef WP_LIBM_PREFIX
/* CORE-MATH's tanf rounds with __builtin_roundeven, a call on x86-64 without SSE4.1;
 * MinGW-w64 has no roundeven. Exact: adding and subtracting 2^52 rounds |x| < 2^52 to
 * the nearest integer, ties to even (round-to-nearest, -frounding-math keeps it). */
double roundeven(double x) {
    double a = __builtin_fabs(x);
    if (a < 0x1p52) {
        volatile double big = 0x1p52;
        double r = (a + big) - big;
        return __builtin_copysign(r, x);
    }
    return x; /* already an integer, or inf / NaN */
}
#endif

/* modff: MinGW-w64's gives +0 as the fraction of a negative integer (and of -0); C and
 * glibc give the sign of x. */
float WP(modff)(float x, float* iptr) {
    uint32_t u;
    memcpy(&u, &x, 4);
    int e = (int)((u >> 23) & 0xff) - 127;
    float i;
    if (e >= 23) { /* an integer, infinity or NaN */
        if (e == 128 && (u & 0x7fffff)) {
            *iptr = x + x; /* NaN, quieted in both */
            return x + x;
        }
        *iptr = x;
        return __builtin_copysignf(0.0f, x);
    }
    if (e < 0) { /* |x| < 1 */
        uint32_t z = u & 0x80000000u;
        memcpy(&i, &z, 4);
        *iptr = i;
        return x;
    }
    uint32_t frac = 0x007fffffu >> e;
    if (!(u & frac)) {
        *iptr = x;
        return __builtin_copysignf(0.0f, x);
    }
    uint32_t t = u & ~frac;
    memcpy(&i, &t, 4);
    *iptr = i;
    return x - i;
}

/* sqrtf, sqrt: the instructions (-fno-math-errno), as glibc's; MinGW-w64's give another
 * NaN for negative inputs. */
float WP(sqrtf)(float x) {
    return __builtin_sqrtf(x);
}

double WP(sqrt)(double x) {
    return __builtin_sqrt(x);
}

/* fminf, fmaxf: as glibc's (x86-64): with a signaling NaN the result is a NaN
 * (MinGW-w64's returns the other argument); equal arguments (+0 and -0) give y, as
 * minss / maxss. */
static int is_snanf(float x) {
    uint32_t u;
    memcpy(&u, &x, 4);
    return (u & 0x7fc00000u) == 0x7f800000u && (u & 0x003fffffu);
}

float WP(fminf)(float x, float y) {
    if (!__builtin_isnan(x) && !__builtin_isnan(y)) {
        return x < y ? x : y;
    }
    if (!__builtin_isnan(y)) {
        return is_snanf(x) ? x + y : y;
    }
    if (!__builtin_isnan(x)) {
        return is_snanf(y) ? x + y : x;
    }
    return x + y;
}

float WP(fmaxf)(float x, float y) {
    if (!__builtin_isnan(x) && !__builtin_isnan(y)) {
        return x > y ? x : y;
    }
    if (!__builtin_isnan(y)) {
        return is_snanf(x) ? x + y : y;
    }
    if (!__builtin_isnan(x)) {
        return is_snanf(y) ? x + y : x;
    }
    return x + y;
}

/* glibc's hypotf: both squares are exact in double, one rounding for the sum, the
 * square root rounded to double and then to float. */
float WP(hypotf)(float x, float y) {
    if (__builtin_isinf(x) || __builtin_isinf(y)) {
        /* infinity even with a NaN, unless that NaN is signaling */
        uint32_t ux, uy;
        memcpy(&ux, &x, 4);
        memcpy(&uy, &y, 4);
        int snan_x = (ux & 0x7fc00000u) == 0x7f800000u && (ux & 0x003fffffu);
        int snan_y = (uy & 0x7fc00000u) == 0x7f800000u && (uy & 0x003fffffu);
        if (!snan_x && !snan_y) {
            return __builtin_inff();
        }
        return x + y;
    }
    if (__builtin_isnan(x) || __builtin_isnan(y)) {
        return x + y;
    }
    double dx = x, dy = y;
    return (float)__builtin_sqrt(dx * dx + dy * dy);
}

/* fma and fmaf: exactly rounded, as glibc's (vfmadd with FMA, else exact software
 * forms). MinGW-w64's own are not exact for every input. */
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

double WP(fma)(double x, double y, double z) {
    WP_READY();
    return wp_libm.fma ? fma_hw(x, y, z) : fma_sw(x, y, z);
}

float WP(fmaf)(float x, float y, float z) {
    WP_READY();
    return wp_libm.fma ? fmaf_hw(x, y, z) : fmaf_sw(x, y, z);
}
