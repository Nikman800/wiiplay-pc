/* The game's math on two builds (docs/HOST_BUILD.md §42): the Windows build (MinGW-w64,
 * under Wine) against the Linux build, through a pipe. Both run the same code, the
 * decomp's MSL math (port_msl_*: fdlibm, as on the Wii) and the port's own exact fma
 * and Broadway estimate forms (src/host_fma.c, port/ps.h); none of it is the C
 * library's, so every result must be the same bits.
 *
 *   libm_compare gen FUNC [N]   writes FUNC's results (raw bits) for N inputs to stdout
 *   libm_compare cmp FUNC [N]   reads them from stdin, compares them with this build's
 *
 *   wine libm_compare.exe gen sinf | libm_compare_linux cmp sinf
 *
 * One-argument float functions take N float bit patterns spread evenly over all 2^32
 * (N = 2^32: every float); the others N pseudo-random inputs (the same on both sides):
 * a quarter any bit pattern, the rest moderate exponents, and for fmaf / fma sums that
 * cancel. Prints the number of results that differ (NaNs that differ only in their
 * payload are counted apart). Exit status 1 if any differ.
 *
 * `cmp` on the same build checks the software fma forms against the dispatched ones
 * (fmaf_sw, fma_sw: the instructions on a CPU with FMA).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <port/ps_forms.h>

typedef float (*f1)(float);
typedef float (*f2)(float, float);
typedef float (*f3)(float, float, float);
typedef double (*d1)(double);
typedef double (*d2)(double, double);
typedef double (*d3)(double, double, double);

float port_fmaf_sw(float x, float y, float z);
double port_fma_sw(double x, double y, double z);

/* Called through pointers, so the compiler neither inlines nor merges them. */
static float m_sinf(float x) { return port_msl_sinf(x); }
static float m_cosf(float x) { return port_msl_cosf(x); }
static float m_tanf(float x) { return port_msl_tanf(x); }
static float m_asinf(float x) { return port_msl_asinf(x); }
static float m_acosf(float x) { return port_msl_acosf(x); }
static float m_sqrtf(float x) { return port_msl_sqrtf(x); }
static float m_floorf(float x) { return port_msl_floorf(x); }
static float m_ceilf(float x) { return port_msl_ceilf(x); }
static float m_atan2f(float y, float x) { return port_msl_atan2f(y, x); }
static float m_fmodf(float x, float y) { return port_msl_fmodf(x, y); }
static float m_powf(float x, float y) { return (float)port_msl_pow(x, y); }
static float m_modff_frac(float x) {
    float i;
    return port_msl_modff(x, &i);
}
static float m_modff_int(float x) {
    float i;
    port_msl_modff(x, &i);
    return i;
}
static double m_ldexp(double x, double y) {
    double i;
    return port_msl_ldexp(x, (int)port_msl_fmod(port_msl_modf(y, &i) * 1e6, 2200.0));
}
static float m_fres(float x) { return port_fres(x); }
static float m_frsqrte(float x) { return (float)port_frsqrte(x); }
static float m_frsqrte_lo(float x) { /* the low 32 bits of the double estimate */
    double d = port_frsqrte(x);
    uint64_t u;
    memcpy(&u, &d, 8);
    float f;
    uint32_t lo = (uint32_t)u;
    memcpy(&f, &lo, 4);
    return f;
}
static float m_fres_nr(float x) { return port_fres_nr(x); }
static float m_frsqrt_nr(float x) { return port_frsqrt_nr(x); }

enum { F1 = 1, F2, F3, D1, D2, D3 };
static const struct fn {
    const char* name;
    int kind;
    void* p;
    void* same; /* `cmp` on one build: compare with this one instead (fma's software forms) */
} fns[] = {
    /* MSL's float forms (the double function rounded to float) */
    {"sinf", F1, (void*)m_sinf}, {"cosf", F1, (void*)m_cosf}, {"tanf", F1, (void*)m_tanf},
    {"asinf", F1, (void*)m_asinf}, {"acosf", F1, (void*)m_acosf}, {"sqrtf", F1, (void*)m_sqrtf},
    {"floorf", F1, (void*)m_floorf}, {"ceilf", F1, (void*)m_ceilf}, {"atan2f", F2, (void*)m_atan2f},
    {"fmodf", F2, (void*)m_fmodf}, {"powf", F2, (void*)m_powf}, {"modff_frac", F1, (void*)m_modff_frac},
    {"modff_int", F1, (void*)m_modff_int},
    /* MSL's double functions */
    {"sin", D1, (void*)port_msl_sin}, {"cos", D1, (void*)port_msl_cos}, {"tan", D1, (void*)port_msl_tan},
    {"asin", D1, (void*)port_msl_asin}, {"acos", D1, (void*)port_msl_acos}, {"atan", D1, (void*)port_msl_atan},
    {"sqrt", D1, (void*)port_msl_sqrt}, {"floor", D1, (void*)port_msl_floor}, {"ceil", D1, (void*)port_msl_ceil},
    {"atan2", D2, (void*)port_msl_atan2}, {"pow", D2, (void*)port_msl_pow}, {"fmod", D2, (void*)port_msl_fmod},
    {"ldexp", D2, (void*)m_ldexp},
    /* the port's fma and Broadway's estimates */
    {"fmaf", F3, (void*)port_fmaf, (void*)port_fmaf_sw}, {"fma", D3, (void*)port_fma, (void*)port_fma_sw},
    {"fres", F1, (void*)m_fres}, {"frsqrte", F1, (void*)m_frsqrte}, {"frsqrte_lo", F1, (void*)m_frsqrte_lo},
    {"fres_nr", F1, (void*)m_fres_nr}, {"frsqrt_nr", F1, (void*)m_frsqrt_nr},
};

static uint64_t seed;
static uint64_t rnd(void) { /* splitmix64 */
    uint64_t z = (seed += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static float rf(void) {
    uint64_t r = rnd();
    uint32_t b = (uint32_t)(r >> 32);
    if (r & 3) { /* exponent in [-24, 24] */
        b = (b & 0x807fffffu) | (uint32_t)(127 + (int)((r >> 2) % 49) - 24) << 23;
    }
    float f;
    memcpy(&f, &b, 4);
    return f;
}
static double rd(void) {
    uint64_t r = rnd(), b = rnd();
    if (r & 3) { /* exponent in [-50, 50] */
        b = (b & 0x800fffffffffffffull) | (uint64_t)(1023 + (int)((r >> 2) % 101) - 50) << 52;
    }
    double d;
    memcpy(&d, &b, 8);
    return d;
}

#define CHUNK 65536

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (argc < 3 || (strcmp(argv[1], "gen") && strcmp(argv[1], "cmp") && strcmp(argv[1], "self"))) {
        fprintf(stderr, "usage: libm_compare gen|cmp|self FUNC [N]\n");
        return 2;
    }
    int gen = !strcmp(argv[1], "gen"), self = !strcmp(argv[1], "self");
    const struct fn* f = NULL;
    for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
        if (!strcmp(fns[i].name, argv[2])) {
            f = &fns[i];
        }
    }
    if (!f) {
        fprintf(stderr, "libm_compare: no function %s\n", argv[2]);
        return 2;
    }
    if (self && !f->same) {
        return 0;
    }
    uint64_t n = argc > 3 ? strtoull(argv[3], NULL, 0) : (f->kind == F1 ? 1ull << 32 : 1ull << 28);
    uint64_t stride = f->kind == F1 && n < (1ull << 32) ? ((1ull << 32) + n - 1) / n : 1;
    int w = f->kind <= F3 ? 4 : 8;
    seed = 12345;
    static uint64_t out[CHUNK], got[CHUNK];
    static uint32_t buf4[CHUNK];
    uint64_t diff = 0, nan_only = 0;
    for (uint64_t base = 0; base < n; base += CHUNK) {
        uint64_t m = n - base < CHUNK ? n - base : CHUNK;
        uint64_t keep = seed;
        /* self: pass 0 the dispatched form into got[], pass 1 the software form on the
           same inputs into out[] */
        for (int pass = 0; pass < (self ? 2 : 1); pass++) {
            void* fp = pass ? f->same : f->p;
            seed = keep;
            for (uint64_t j = 0; j < m; j++) {
                uint64_t r = 0;
                switch (f->kind) {
                case F1: {
                    uint32_t b = (uint32_t)((base + j) * stride);
                    float x, y;
                    memcpy(&x, &b, 4);
                    y = ((f1)fp)(x);
                    memcpy(&r, &y, 4);
                    break;
                }
                case F2: {
                    float x = rf(), y = rf(), z = ((f2)fp)(x, y);
                    memcpy(&r, &z, 4);
                    break;
                }
                case F3: {
                    float x = rf(), y = rf(), z = rf();
                    if (rnd() & 1) {
                        z = -(float)((double)x * y) * (1 + rf() * 0x1p-30f);
                    }
                    float t = ((f3)fp)(x, y, z);
                    memcpy(&r, &t, 4);
                    break;
                }
                case D1: {
                    double x = rd(), y = ((d1)fp)(x);
                    memcpy(&r, &y, 8);
                    break;
                }
                case D2: {
                    double x = rd(), y = rd(), z = ((d2)fp)(x, y);
                    memcpy(&r, &z, 8);
                    break;
                }
                case D3: {
                    double x = rd(), y = rd(), z = rd();
                    if (rnd() & 1) {
                        z = -(x * y) * (1 + rd() * 0x1p-60);
                    }
                    double t = ((d3)fp)(x, y, z);
                    memcpy(&r, &t, 8);
                    break;
                }
                }
                if (self && pass == 0) {
                    got[j] = r;
                } else {
                    out[j] = r;
                }
            }
        }
        if (gen) {
            if (w == 4) {
                for (uint64_t j = 0; j < m; j++) {
                    buf4[j] = (uint32_t)out[j];
                }
                fwrite(buf4, 4, m, stdout);
            } else {
                fwrite(out, 8, m, stdout);
            }
            continue;
        }
        if (self) {
            /* got[] holds the dispatched results, out[] the software ones */
        } else if (w == 4) {
            if (fread(buf4, 4, m, stdin) != m) {
                fprintf(stderr, "libm_compare: %s: short input\n", f->name);
                return 2;
            }
            for (uint64_t j = 0; j < m; j++) {
                got[j] = buf4[j];
            }
        } else if (fread(got, 8, m, stdin) != m) {
            fprintf(stderr, "libm_compare: %s: short input\n", f->name);
            return 2;
        }
        for (uint64_t j = 0; j < m; j++) {
            if (got[j] == out[j]) {
                continue;
            }
            uint64_t a = got[j], b = out[j];
            int nan_a = w == 4 ? (a & 0x7fffffff) > 0x7f800000 : (a << 1) > 0xffe0000000000000ull;
            int nan_b = w == 4 ? (b & 0x7fffffff) > 0x7f800000 : (b << 1) > 0xffe0000000000000ull;
            if (nan_a && nan_b) {
                nan_only++;
                continue;
            }
            if (diff < 5) {
                fprintf(stderr, "  %s input %llu: other %0*llx, here %0*llx\n", f->name,
                        (unsigned long long)(base + j), w * 2, (unsigned long long)a, w * 2, (unsigned long long)b);
            }
            diff++;
        }
    }
    if (!gen) {
        printf("%-12s %llu inputs: %llu differ (%llu more NaNs with another payload)%s\n", f->name,
               (unsigned long long)n, (unsigned long long)diff, (unsigned long long)nan_only,
               self ? " (software form against the dispatched one)" : "");
    }
    return diff ? 1 : 0;
}
