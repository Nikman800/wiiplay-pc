/* Compares the C library's math functions between two builds (docs/HOST_BUILD.md §41,
 * "Floats: the C library's math functions"): the Windows build, with windows/libm, and
 * the Linux build, with glibc.
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
 * payload are counted apart). Exit status 1 if any differ (but for sincos: CORE-MATH's
 * against glibc's, which differ in the last bit by design).
 */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

typedef float (*f1)(float);
typedef float (*f2)(float, float);
typedef float (*f3)(float, float, float);
typedef double (*d1)(double);
typedef double (*d2)(double, double);
typedef double (*d3)(double, double, double);

/* Called through pointers, so the compiler neither inlines nor merges them. */
static float sincosf_sin(float x) {
    float s, c;
    sincosf(x, &s, &c);
    return s;
}
static float sincosf_cos(float x) {
    float s, c;
    sincosf(x, &s, &c);
    return c;
}
static double sincos_sin(double x) {
    double s, c;
    sincos(x, &s, &c);
    return s;
}
static double sincos_cos(double x) {
    double s, c;
    sincos(x, &s, &c);
    return c;
}
static float modff_frac(float x) {
    float i;
    return modff(x, &i);
}
static float modff_int(float x) {
    float i;
    modff(x, &i);
    return i;
}
static double lround_d(double x) {
    /* within a 32-bit long (Windows); NaN and infinity give LONG_MIN, another value */
    return isfinite(x) ? (double)lround(fmod(x, 0x1p30)) : 0;
}
static double ldexp_d(double x, double y) {
    return ldexp(x, (int)fmod(y * 1e6, 2200.0));
}

enum { F1 = 1, F2, F3, D1, D2, D3 };
static const struct fn {
    const char* name;
    int kind;
    void* p;
    int known; /* differs by design: reported, not a failure */
} fns[] = {
    /* windows/libm's */
    {"sinf", F1, (void*)sinf}, {"cosf", F1, (void*)cosf}, {"sincosf_sin", F1, (void*)sincosf_sin},
    {"sincosf_cos", F1, (void*)sincosf_cos}, {"tanf", F1, (void*)tanf}, {"asinf", F1, (void*)asinf},
    {"acosf", F1, (void*)acosf}, {"atan2f", F2, (void*)atan2f}, {"powf", F2, (void*)powf},
    {"hypotf", F2, (void*)hypotf}, {"fmaf", F3, (void*)fmaf}, {"pow", D2, (void*)pow},
    {"fma", D3, (void*)fma}, {"sqrtf", F1, (void*)sqrtf}, {"modff_frac", F1, (void*)modff_frac},
    {"modff_int", F1, (void*)modff_int}, {"fminf", F2, (void*)fminf}, {"fmaxf", F2, (void*)fmaxf},
    {"sqrt", D1, (void*)sqrt},
    /* CORE-MATH's, correctly rounded; glibc's is not (windows/libm/README.md) */
    {"sincos_sin", D1, (void*)sincos_sin, 1}, {"sincos_cos", D1, (void*)sincos_cos, 1},
    /* MinGW-w64's, exact */
    {"fmodf", F2, (void*)fmodf}, {"lround", D1, (void*)lround_d}, {"fmod", D2, (void*)fmod},
    {"ldexp", D2, (void*)ldexp_d},
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
    if (argc < 3 || (strcmp(argv[1], "gen") && strcmp(argv[1], "cmp"))) {
        fprintf(stderr, "usage: libm_compare gen|cmp FUNC [N]\n");
        return 2;
    }
    int gen = !strcmp(argv[1], "gen");
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
    uint64_t n = argc > 3 ? strtoull(argv[3], NULL, 0) : (f->kind == F1 ? 1ull << 32 : 1ull << 28);
    uint64_t stride = f->kind == F1 && n < (1ull << 32) ? ((1ull << 32) + n - 1) / n : 1;
    int w = f->kind <= F3 ? 4 : 8;
    seed = 12345;
    static uint64_t out[CHUNK], got[CHUNK];
    static uint32_t buf4[CHUNK];
    uint64_t diff = 0, nan_only = 0;
    for (uint64_t base = 0; base < n; base += CHUNK) {
        uint64_t m = n - base < CHUNK ? n - base : CHUNK;
        for (uint64_t j = 0; j < m; j++) {
            uint64_t r = 0;
            switch (f->kind) {
            case F1: {
                uint32_t b = (uint32_t)((base + j) * stride);
                float x, y;
                memcpy(&x, &b, 4);
                y = ((f1)f->p)(x);
                memcpy(&r, &y, 4);
                break;
            }
            case F2: {
                float x = rf(), y = rf(), z = ((f2)f->p)(x, y);
                memcpy(&r, &z, 4);
                break;
            }
            case F3: {
                float x = rf(), y = rf(), z = rf();
                if (rnd() & 1) {
                    z = -(float)((double)x * y) * (1 + rf() * 0x1p-30f);
                }
                float t = ((f3)f->p)(x, y, z);
                memcpy(&r, &t, 4);
                break;
            }
            case D1: {
                double x = rd(), y = ((d1)f->p)(x);
                memcpy(&r, &y, 8);
                break;
            }
            case D2: {
                double x = rd(), y = rd(), z = ((d2)f->p)(x, y);
                memcpy(&r, &z, 8);
                break;
            }
            case D3: {
                double x = rd(), y = rd(), z = rd();
                if (rnd() & 1) {
                    z = -(x * y) * (1 + rd() * 0x1p-60);
                }
                double t = ((d3)f->p)(x, y, z);
                memcpy(&r, &t, 8);
                break;
            }
            }
            out[j] = r;
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
        if (w == 4) {
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
               f->known && diff ? " (expected: last bit, see windows/libm/README.md)" : "");
    }
    return diff && !f->known ? 1 : 0;
}
