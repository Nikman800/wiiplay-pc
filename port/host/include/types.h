/**
 * Host (PC) replacement for include/types.h.
 *
 * port/host/include comes before include/ on the host compiler's include path,
 * so every `#include <types.h>` in the decompiled code lands here when
 * PORT_HOST is defined. The matching build never sees this file.
 *
 * Differences from include/types.h:
 * - fixed-width integers: on LP64 hosts `unsigned long` is 8 bytes, so u32/s32
 *   are built on <stdint.h> instead (u32 is `unsigned int` on every supported
 *   host, which keeps struct layouts identical to the Wii's).
 * - ROUND_UP_PTR / ROUND_DOWN_PTR go through uintptr_t instead of u32, which
 *   would truncate 64-bit addresses.
 */

#ifndef TYPES_H
#define TYPES_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#ifdef __cplusplus
// Some decomp headers include this inside extern "C" (homebuttonMiniLib's HBMTypes.h).
extern "C++" {
#include <cstdarg>
#include <cstddef>
#include <cfloat>
#include <cstdint>
#include <new>
#include <cstring>
#include <strings.h>
#include <climits>

// MSL's std::stricmp (ArcResourceAccessor's name lookup); ASCII only, like MSL.
namespace std {
inline int stricmp(const char* pLhs, const char* pRhs) {
    return ::strcasecmp(pLhs, pRhs);
}
} // namespace std

// libstdc++ before GCC 14 (the MinGW-w64 cross compiler, HOST_BUILD.md §41) has no
// std:: names for the float forms of the C math functions; the decomp calls them so.
#if defined(__GLIBCXX__) && __GNUC__ < 14
} // extern "C++"
#include <math.h>
extern "C++" {
namespace std {
using ::acosf;
using ::asinf;
using ::atan2f;
using ::ceilf;
using ::cosf;
using ::floorf;
using ::fmodf;
using ::ldexpf;
using ::modff;
using ::sinf;
using ::sqrtf;
using ::tanf;
} // namespace std
#endif
} // extern "C++"
#else
#include <limits.h>
#include <stdarg.h>
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#endif

// <limits.h> (included above, so its guard keeps it from coming back) defines
// PATH_MAX on Linux; the game's classes have static members of that name
// (RPSysFile, RPSndAudioMgr). Host code that needs the OS limit uses PATH_MAX
// before this header or its own constant.
#undef PATH_MAX

// glibc's <sys/cdefs.h> also defines __CONCAT (same meaning as macros.h).
#undef __CONCAT
#include <decomp/compat.h>
#include <decomp/decomp.h>
#include <decomp/lang.h>

#include <macros.h>

typedef uint64_t u64;
typedef int64_t s64;

typedef uint32_t u32;
typedef int32_t s32;

typedef uint16_t u16;
typedef int16_t s16;

typedef uint8_t u8;
typedef int8_t s8;

typedef float f32;
typedef double f64;

typedef int UNKWORD;
typedef void UNKTYPE;

enum { FALSE, TRUE };
typedef int BOOL;

typedef void (*funcptr_t)(void);

// 64-bit safe pointer rounding (the originals cast through u32).
#undef ROUND_UP_PTR
#define ROUND_UP_PTR(x, align)                                                 \
    ((void*)((((uintptr_t)(x)) + (align) - 1) & (~(uintptr_t)((align) - 1))))
#undef ROUND_DOWN_PTR
#define ROUND_DOWN_PTR(x, align)                                               \
    ((void*)(((uintptr_t)(x)) & (~(uintptr_t)((align) - 1))))

// Memory-mapped hardware register blocks: on the host each block is a plain
// (weak, so every translation unit can define it) array of fake registers.
#undef DECL_HW_REGS
#define DECL_HW_REGS(NAME) NAME##_HW_REGS[0x400]

// CodeWarrior-only attributes have no host meaning.
#undef DECL_SECTION
#define DECL_SECTION(x)
#undef DECL_WEAK
#define DECL_WEAK __attribute__((weak))

#ifdef __cplusplus
static_assert(sizeof(u32) == 4 && sizeof(s32) == 4, "u32/s32 must be 4 bytes");
#else
_Static_assert(sizeof(u32) == 4 && sizeof(s32) == 4, "u32/s32 must be 4 bytes");
#endif

#endif
