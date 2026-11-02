# The Windows build's math functions

The Windows build (`make WIN64=1`, `docs/HOST_BUILD.md` §41) links these instead of
MinGW-w64's (and msvcrt's) math functions, so that it computes the same bits as the
Linux build, which uses glibc's libm (glibc 2.43, x86-64). Linux builds do not use
them.

| Function | Here | glibc 2.43 |
|---|---|---|
| `sinf`, `cosf`, `sincosf`, `powf`, `pow` | ARM optimized-routines (`arm/`), built twice, x86-64 and `-mfma -mavx2`, one picked from cpuid | the same code, built twice, picked by an IFUNC (FMA and AVX2 usable) |
| `tanf`, `asinf`, `acosf`, `atan2f` | CORE-MATH (`core-math/`), correctly rounded | CORE-MATH |
| `sincos` (double) | CORE-MATH, correctly rounded | IBM's (within 0.55 ulp): see below |
| `hypotf` | `wp_libm.c`: the square root of the double sum of squares | the same computation |
| `fmaf`, `fma` | `wp_libm.c`: `vfmadd` with FMA, else exact software forms | `vfmadd` with FMA, else exact software forms |
| `modff` | `wp_libm.c` (exact; MinGW-w64's fraction of a negative integer is +0) | exact, the fraction has the sign of x |
| `sqrtf`, `sqrt` | `wp_libm.c`: the instructions (MinGW-w64's NaN for a negative input has another sign) | the instructions |
| `fminf`, `fmaxf` | `wp_libm.c` (MinGW-w64's ignores a signaling NaN) | a NaN for a signaling NaN; y for equal arguments |

`wp_libm.c` (this repository's) has the dispatch, `hypotf`, `fmaf`, `fma`, `modff`,
`sqrtf`, `sqrt`, `fminf`, `fmaxf` and a `roundeven` (CORE-MATH's `tanf` calls it on x86-64 without
SSE4.1; MinGW-w64 has none). The other math functions the game and the host code
call give the same results in both C libraries (`fmodf`, `fmod`, `lround`, `ldexp`;
`make WIN64=1 libm` checks them too) and stay MinGW-w64's.

## Upstream

| Directory | Project | Commit | License |
|---|---|---|---|
| `arm/` | [ARM optimized-routines](https://github.com/ARM-software/optimized-routines), `math/` | `2ee4b0d45680a9880412c1d186224b08652c35f6` (2026-10-28) | MIT OR Apache-2.0 WITH LLVM-exception (`arm/LICENSE`) |
| `core-math/` | [CORE-MATH](https://gitlab.inria.fr/core-math/core-math), `src/binary32/{tan,asin,acos,atan2}`, `src/binary64/sincos` | `660067dfd3e751c8f07acc31c899ff410aa7206e` (2026-11-01) | MIT (`core-math/LICENSE`) |

The files are unmodified copies (`arm/test_defs.h` and `arm/test_sig.h` are from
`math/include/`). The Makefile compiles them with glibc's libm flags (`-O2
-frounding-math -fno-math-errno`), `-DUSE_GLIBC_ABI=0` (and `-DHAVE_FAST_FMA=1` in the
FMA build: glibc's `math.h` defines `FP_FAST_FMA` there, MinGW-w64's does not, and
`pow` takes another path without it), and renames each
optimized-routines entry point per build with `objcopy --redefine-sym` (`sinf` ->
`wp_arm_sinf_sse2` / `wp_arm_sinf_fma`); everything is linked into one relocatable
object, `build/win64/libm/wp_libm_all.o`, which every executable links whole.

## Verification

- On Linux, the same sources (built with the system GCC, names prefixed `wp_`) against
  glibc 2.43 in one process: every float input (2^32) of `sinf`, `cosf`, `sincosf`,
  `tanf`, `asinf`, `acosf`; 2^30 random inputs of `atan2f`, `powf`, `hypotf`, `pow`,
  `fmaf`, `fma`; both builds (`GLIBC_TUNABLES=glibc.cpu.hwcaps=-AVX2,-FMA` makes
  glibc take its non-FMA builds too): no differences (`fmaf` / `fma`'s software forms
  only in the payload of some NaN results).
- The Windows build (MinGW-w64 GCC 13, under Wine) against the Linux build, through a
  pipe: the same inputs, no differences (`docs/HOST_BUILD.md` §41 has the counts before
  and after).
- `sincos` (double): glibc's is not correctly rounded and CORE-MATH's is; they differ in
  the last bit for about 0.1% of random inputs, none of which changed the result
  rounded to float in 10^8 inputs. The host's input code (`host_input.cpp`), its only
  caller, rounds the results to integer counts. MinGW-w64's had to be replaced anyway:
  it is in one object with `sincosf`.
