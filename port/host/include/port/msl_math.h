/**
 * The game's own math functions: the decomp's MSL math (src/MSL/e_*.c, k_*.c,
 * s_*.c, w_*.c, math_sun.c, fdlibm), compiled for the host under port_msl_*
 * names (host build only; port/docs/HOST_BUILD.md §42).
 *
 * Every host compile force-includes this header (-include port/msl_math.h)
 * after the C library's <math.h> / <cmath>. The names the game calls are then
 * redirected to the Wii's implementations, so the game computes the same bits
 * on Linux and on Windows, and the same bits as on the Wii, whatever the C
 * library is:
 *
 *   sin cos tan asin acos atan atan2 sqrt pow fmod floor ceil modf ldexp
 *   frexp copysign scalbn    -> the MSL double functions (fdlibm, as on the Wii)
 *   sinf cosf tanf asinf acosf atan2f sqrtf floorf ceilf fmodf ldexpf modff
 *                            -> MSL's <math.h> inlines (include/MSL/internal/
 *                               math_double.h): (float)sin((double)x) and so on
 *   fabs fabsf               -> the instruction (exact)
 *
 * Three modes:
 *
 *   PORT_MSL_MATH_IMPL  the MSL math units themselves (Makefile MSL_MATH_SRCS):
 *                       the definitions take the port_msl_* names.
 *   PORT_MSL_GAME_MATH  the decomp's units: C++ overloads as MSL's <cmath>
 *                       (include/MSL/cmath) has them: std::sin, std::pow, ...
 *                       only for double (std::sin(float) returns a double, as
 *                       on the Wii), float overloads only for std::sqrt and
 *                       std::fabs; M_PI is MSL's float constant.
 *   (neither)           the port's own host code (src/host_*.cpp, tests): the
 *                       same functions, with the usual C++ overloads
 *                       (std::sin(float) is sinf, computed as MSL's sinf).
 *
 * Functions MSL does not have (hypot, fmin, fmax, lround, exp, log, ...) are
 * not redirected; the game does not call them (only host code does). Aurora is
 * built separately and keeps the C library's math.
 */

#ifndef PORT_MSL_MATH_H
#define PORT_MSL_MATH_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

/* The C library's declarations first: a later #include <math.h> / <cmath> is
 * then a no-op and cannot redeclare the names redirected below. */
#ifdef __cplusplus
#include <cmath>
#include <type_traits>
#endif
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

double port_msl_acos(double x);
double port_msl_asin(double x);
double port_msl_atan(double x);
double port_msl_atan2(double y, double x);
double port_msl_ceil(double x);
double port_msl_copysign(double x, double y);
double port_msl_cos(double x);
double port_msl_floor(double x);
double port_msl_fmod(double x, double y);
double port_msl_frexp(double x, int* e);
double port_msl_ldexp(double x, int e);
double port_msl_modf(double x, double* ip);
double port_msl_pow(double x, double y);
double port_msl_scalbn(double x, int n);
double port_msl_sin(double x);
double port_msl_sqrt(double x);
double port_msl_tan(double x);

#ifdef __cplusplus
}
#endif

#if defined(PORT_MSL_MATH_IMPL)

/* The MSL units define these under port_msl_* names (and call each other by
 * them); fabs, copysign's callers etc. inside fdlibm stay the instruction. */
#define acos port_msl_acos
#define asin port_msl_asin
#define atan port_msl_atan
#define atan2 port_msl_atan2
#define ceil port_msl_ceil
#define copysign port_msl_copysign
#define cos port_msl_cos
#define floor port_msl_floor
#define fmod port_msl_fmod
#define frexp port_msl_frexp
#define ldexp port_msl_ldexp
#define modf port_msl_modf
#define pow port_msl_pow
#define scalbn port_msl_scalbn
#define sin port_msl_sin
#define sqrt port_msl_sqrt
#define tan port_msl_tan
#define __ieee754_acos port_msl___ieee754_acos
#define __ieee754_asin port_msl___ieee754_asin
#define __ieee754_atan2 port_msl___ieee754_atan2
#define __ieee754_fmod port_msl___ieee754_fmod
#define __ieee754_pow port_msl___ieee754_pow
#define __ieee754_rem_pio2 port_msl___ieee754_rem_pio2
#define __ieee754_sqrt port_msl___ieee754_sqrt
#define __kernel_cos port_msl___kernel_cos
#define __kernel_rem_pio2 port_msl___kernel_rem_pio2
#define __kernel_sin port_msl___kernel_sin
#define __kernel_tan port_msl___kernel_tan
/* fdlibm.h's declarations that a C library may have in another form (MinGW-w64:
 * matherr is _matherr, with its own struct _exception) */
#undef matherr
#define matherr port_msl_matherr
#undef exception
#define exception port_msl_exception

#else /* !PORT_MSL_MATH_IMPL */

#ifdef __cplusplus
extern "C" {
#endif


/* MSL's float forms (include/MSL/internal/math_double.h): the double function,
 * rounded to float. */
static inline float port_msl_acosf(float x) {
    return (float)port_msl_acos(x);
}
static inline float port_msl_asinf(float x) {
    return (float)port_msl_asin(x);
}
static inline float port_msl_atan2f(float y, float x) {
    return (float)port_msl_atan2(y, x);
}
static inline float port_msl_ceilf(float x) {
    return (float)port_msl_ceil(x);
}
static inline float port_msl_cosf(float x) {
    return (float)port_msl_cos(x);
}
static inline float port_msl_sinf(float x) {
    return (float)port_msl_sin(x);
}
static inline float port_msl_sqrtf(float x) {
    return (float)port_msl_sqrt(x);
}
static inline float port_msl_tanf(float x) {
    return (float)port_msl_tan(x);
}
static inline float port_msl_floorf(float x) {
    return (float)port_msl_floor(x);
}
static inline float port_msl_fmodf(float x, float y) {
    return (float)port_msl_fmod(x, y);
}
static inline float port_msl_ldexpf(float x, int e) {
    return (float)port_msl_ldexp(x, e);
}
static inline float port_msl_modff(float x, float* ip) {
    double intg;
    float frac = (float)port_msl_modf(x, &intg);
    *ip = (float)intg;
    return frac;
}
/* fabs is an instruction on the Wii (MSL's math.h: __fabs). */
static inline double port_msl_fabs(double x) {
    return __builtin_fabs(x);
}
static inline float port_msl_fabsf(float x) {
    return __builtin_fabsf(x);
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#if !defined(PORT_MSL_GAME_MATH)
/* Host code: the usual C++ overloads, each the MSL computation (float: the
 * MSL float form; integers: the double function). */
extern "C++" {
inline float port_msl_acos(float x) { return port_msl_acosf(x); }
inline float port_msl_asin(float x) { return port_msl_asinf(x); }
inline float port_msl_atan(float x) { return (float)port_msl_atan((double)x); }
inline float port_msl_atan2(float y, float x) { return port_msl_atan2f(y, x); }
inline float port_msl_ceil(float x) { return port_msl_ceilf(x); }
inline float port_msl_copysign(float x, float y) { return __builtin_copysignf(x, y); }
inline float port_msl_cos(float x) { return port_msl_cosf(x); }
inline float port_msl_floor(float x) { return port_msl_floorf(x); }
inline float port_msl_fmod(float x, float y) { return port_msl_fmodf(x, y); }
inline float port_msl_pow(float x, float y) { return (float)port_msl_pow((double)x, (double)y); }
inline float port_msl_sin(float x) { return port_msl_sinf(x); }
inline float port_msl_sqrt(float x) { return port_msl_sqrtf(x); }
inline float port_msl_tan(float x) { return port_msl_tanf(x); }
inline float port_msl_fabs(float x) { return port_msl_fabsf(x); }
template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
inline double port_msl_sqrt(T x) { return port_msl_sqrt((double)x); }
template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
inline double port_msl_floor(T x) { return (double)x; }
}
#endif

namespace std {
using ::port_msl_acos;
using ::port_msl_acosf;
using ::port_msl_asin;
using ::port_msl_asinf;
using ::port_msl_atan;
using ::port_msl_atan2;
using ::port_msl_atan2f;
using ::port_msl_ceil;
using ::port_msl_ceilf;
using ::port_msl_copysign;
using ::port_msl_cos;
using ::port_msl_cosf;
using ::port_msl_fabs;
using ::port_msl_fabsf;
using ::port_msl_floor;
using ::port_msl_floorf;
using ::port_msl_fmod;
using ::port_msl_fmodf;
using ::port_msl_frexp;
using ::port_msl_ldexp;
using ::port_msl_ldexpf;
using ::port_msl_modf;
using ::port_msl_modff;
using ::port_msl_pow;
using ::port_msl_scalbn;
using ::port_msl_sin;
using ::port_msl_sinf;
using ::port_msl_sqrt;
using ::port_msl_sqrtf;
using ::port_msl_tan;
using ::port_msl_tanf;
#if defined(PORT_MSL_GAME_MATH)
/* MSL's <cmath>: std::sqrt(float) is sqrtf, std::fabs(float) is a float. */
inline float port_msl_sqrt(float x) {
    return ::port_msl_sqrtf(x);
}
inline float port_msl_fabs(float x) {
    return ::port_msl_fabsf(x);
}
#endif
} // namespace std
#endif /* __cplusplus */

#define acos port_msl_acos
#define acosf port_msl_acosf
#define asin port_msl_asin
#define asinf port_msl_asinf
#define atan port_msl_atan
#define atan2 port_msl_atan2
#define atan2f port_msl_atan2f
#define ceil port_msl_ceil
#define ceilf port_msl_ceilf
#define copysign port_msl_copysign
#define cos port_msl_cos
#define cosf port_msl_cosf
#define fabs port_msl_fabs
#define fabsf port_msl_fabsf
#define floor port_msl_floor
#define floorf port_msl_floorf
#define fmod port_msl_fmod
#define fmodf port_msl_fmodf
#define frexp port_msl_frexp
#define ldexp port_msl_ldexp
#define ldexpf port_msl_ldexpf
#define modf port_msl_modf
#define modff port_msl_modff
#define pow port_msl_pow
#define scalbn port_msl_scalbn
#define sin port_msl_sin
#define sinf port_msl_sinf
#define sqrt port_msl_sqrt
#define sqrtf port_msl_sqrtf
#define tan port_msl_tan
#define tanf port_msl_tanf

#if defined(PORT_MSL_GAME_MATH)
/* MSL's <math.h>: M_PI is a float (3.141592653589793f), so the game's degree /
 * radian / FIDX conversions (NW4R_MATH_PI, GXInitLightSpot, C_MTXPerspective)
 * compute in single precision, as on the Wii. */
#undef M_PI
#define M_PI 3.141592653589793f
#endif

#endif /* !PORT_MSL_MATH_IMPL */

#endif
