// The cwcontract plugin (tools/cwcontract, port/docs/HOST_BUILD.md §43): CodeWarrior's
// contraction decisions, checked against what GC/3.0a5.2 -O4,p -fp_contract on emits for the
// same expressions (fused or not; which product; the sign of an exact zero), the operand
// order of a compound assignment with calls, and `#pragma fp_contract off`. Compiled with
// the plugin on (as the game's units are); run once with the CPU's FMA and once with
// WIIPLAY_NO_FMA=1 (the software forms): the same results.
#include <cmath>
#include <cstdio>
#include <cstring>

#include <port/cw_fma.h>

typedef float f32;

static int failures = 0;

static unsigned bits(f32 x) {
    unsigned u;
    memcpy(&u, &x, 4);
    return u;
}

static void check(const char* what, f32 got, f32 want) {
    if (bits(got) != bits(want)) {
        printf("FAIL %s: %a (%08x), want %a (%08x)\n", what, got, bits(got), want, bits(want));
        failures++;
    }
}

// Values that tell one rounding from two: a * b = 1 + 2^-11 + 2^-24 exactly, which rounds
// to 1 + 2^-11 (a tie, to even); c is minus that, so a*b + c is 2^-24 fused and 0 unfused.
static volatile f32 va = 1.0f + 0x1p-12f, vb = 1.0f + 0x1p-12f, vc = -(1.0f + 0x1p-11f);
static const f32 fused = 0x1p-24f;

__attribute__((noinline)) static f32 madd(f32 a, f32 b, f32 c) { return a * b + c; }
__attribute__((noinline)) static f32 madd_r(f32 a, f32 b, f32 c) { return c + a * b; }
__attribute__((noinline)) static f32 msub(f32 a, f32 b, f32 c) { return a * b - c; }
__attribute__((noinline)) static f32 nmsub(f32 a, f32 b, f32 c) { return c - a * b; }
__attribute__((noinline)) static f32 nmadd(f32 a, f32 b, f32 c) { return -(a * b + c); }
__attribute__((noinline)) static f32 init_madd(f32 a, f32 b, f32 c) {
    f32 r = a * b + c;
    return r;
}
__attribute__((noinline)) static f32 via_var(f32 a, f32 b, f32 c) {
    f32 t = a * b; // a variable: not fused
    return t + c;
}
__attribute__((noinline)) static f32 neg_product(f32 a, f32 b, f32 c) {
    return -(a * b) + c; // a negated product: not fused
}
__attribute__((noinline)) static f32 neg_operand(f32 a, f32 b, f32 c) {
    return (-a) * b - c; // fused: fma(-a, b, -c)
}
__attribute__((noinline)) static f32 half(f32 a, f32 c) {
    return a / 2.0f + c; // a * 0.5 (exact), fused
}
__attribute__((noinline)) static f32 third(f32 a, f32 c) {
    return a / 3.0f + c; // a division: not fused
}
__attribute__((noinline)) static f32 times_one(f32 a, f32 c) {
    return a * 1.0f + c; // CodeWarrior folds a * 1 first
}
__attribute__((noinline)) static f32 left_first(f32 a, f32 b, f32 c, f32 d) {
    return a * b + c * d; // the left product fuses, c * d is the addend
}
__attribute__((noinline)) static double dmadd(double a, double b, double c) { return a * b + c; }

inline f32 mul(f32 a, f32 b) { return a * b; }
__attribute__((noinline)) static f32 via_inline(f32 a, f32 b, f32 c) {
    return mul(a, b) + c; // an inline function's result: not fused
}

#pragma fp_contract off
__attribute__((noinline)) static f32 off_region(f32 a, f32 b, f32 c) { return a * b + c; }
#pragma fp_contract on
__attribute__((noinline)) static f32 on_again(f32 a, f32 b, f32 c) { return a * b + c; }

// x op= f() * g(): C++17 evaluates the right side first; the fused form keeps the calls'
// order and reads x after them.
static char order[8];
static int norder = 0;
static f32 x_mem;
__attribute__((noinline)) static f32 fa() {
    order[norder++] = 'a';
    return va;
}
__attribute__((noinline)) static f32 fb() {
    order[norder++] = 'b';
    x_mem = vc; // x is read after the calls
    return vb;
}
struct S {
    f32 x;
    __attribute__((noinline)) void acc() { x += fa() * fb(); }
};
__attribute__((noinline)) static f32 compound(f32 c) {
    x_mem = 0.0f;
    x_mem += fa() * fb();
    (void)c;
    return x_mem;
}

int main() {
    f32 a = va, b = vb, c = vc;
    // the unfused results, from separately rounded steps
    f32 p = a * b, plain = p + c;
    check("unfused reference", plain, 0.0f);
    check("a*b + c", madd(a, b, c), fused);
    check("c + a*b", madd_r(a, b, c), fused);
    check("a*b - c", msub(a, b, -c), fused);
    check("c - a*b", nmsub(a, b, -c), -fused);
    check("-(a*b + c)", nmadd(a, b, c), -fused);
    check("f32 r = a*b + c", init_madd(a, b, c), fused);
    check("t = a*b; t + c", via_var(a, b, c), plain);
    check("-(a*b) + c", neg_product(a, b, -c), -p - c);
    check("(-a)*b - c", neg_operand(a, b, c), -fused);
    check("mul(a, b) + c", via_inline(a, b, c), plain);
    check("off region", off_region(a, b, c), plain);
    check("on again", on_again(a, b, c), fused);
    check("a*1 + c", times_one(a, c), a + c);
    // a / 2 + c: a = 2^-23 * odd, c chosen so that one rounding differs
    f32 h = 1.0f + 0x1p-23f, hc = -(0.5f);
    check("a/2 + c", half(h, hc), std::fma(h, 0.5f, hc));
    check("a/3 + c", third(3.0f * a, c), (3.0f * a) / 3.0f + c);
    check("a*b + c*d", left_first(a, b, c, 1.0f), fused);
    // Broadway's fnmsub: -(a*b - c); an exact zero is -0
    check("c - a*b exact", nmsub(2.0f, 3.0f, 6.0f), -0.0f);
    check("a*b + c exact", madd(2.0f, 3.0f, -6.0f), 0.0f);
    {
        double da = 1.0 + 0x1p-27, db = 1.0 + 0x1p-27, dc = -(1.0 + 0x1p-26);
        double got = dmadd(da, db, dc);
        if (got != 0x1p-54) {
            printf("FAIL double a*b + c: %a\n", got);
            failures++;
        }
    }
    norder = 0;
    check("x += f()*g()", compound(0.0f), fused);
    if (norder != 2 || order[0] != 'a' || order[1] != 'b') {
        printf("FAIL x += f()*g(): order %.*s\n", norder, order);
        failures++;
    }
    S s;
    s.x = vc;
    norder = 0;
    s.acc();
    check("member += f()*g()", s.x, fused);

    printf("test_cwcontract (%s): %s\n", port_fma_hw > 0 ? "FMA" : "software",
           failures ? "FAILED" : "OK");
    return failures != 0;
}
