/**
 * 16-bit wchar_t and the decomp's MSL wide-string functions (host build;
 * port/docs/POINTERS.md §6).
 *
 * The host compiles every file with -fshort-wchar and force-includes this
 * header (-include port/wide.h). wchar_t is then 2 bytes as on the Wii, so
 * L"..." literals are UTF-16, sizeof(wchar_t) arithmetic and structs with
 * embedded wchar_t arrays (BMG DAT1, RFL names, NANDBanner) keep the Wii
 * layout, without source edits.
 *
 * glibc's wide functions assume a 4-byte wchar_t, so the names the game uses
 * are redirected to the decompiled MSL_C implementations (src/MSL_C/wstring.c,
 * wmem.c), which the host build compiles under port_* names. Never pass a
 * wchar_t string to a libc wide function or to host code built without
 * -fshort-wchar.
 */

#ifndef PORT_WIDE_H
#define PORT_WIDE_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif

#ifdef __cplusplus
static_assert(sizeof(wchar_t) == 2, "the host build needs -fshort-wchar");
#else
_Static_assert(sizeof(wchar_t) == 2, "the host build needs -fshort-wchar");
#endif

#if !defined(PORT_WIDE_IMPL)
/* Declare the libc names first, so a later #include <wchar.h> or <cwchar>
 * does not redeclare the renamed functions. */
#ifdef __cplusplus
#include <cwchar>
#else
#include <wchar.h>
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

size_t port_wcslen(const wchar_t* str);
wchar_t* port_wcscpy(wchar_t* dst, const wchar_t* src);
wchar_t* port_wcsncpy(wchar_t* dst, const wchar_t* src, size_t count);
int port_wcscmp(const wchar_t* a, const wchar_t* b);
wchar_t* port_wcschr(const wchar_t* str, wchar_t chr);
wchar_t* port_wmemcpy(wchar_t* dst, const wchar_t* src, size_t count);
wchar_t* port_wmemchr(const wchar_t* ptr, wchar_t chr, size_t count);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// std::wcslen and friends (after the macros below) name the same functions.
namespace std {
using ::port_wcslen;
using ::port_wcscpy;
using ::port_wcsncpy;
using ::port_wcscmp;
using ::port_wcschr;
using ::port_wmemcpy;
using ::port_wmemchr;
} // namespace std
#endif

#define wcslen port_wcslen
#define wcscpy port_wcscpy
#define wcsncpy port_wcsncpy
#define wcscmp port_wcscmp
#define wcschr port_wcschr
#define wmemcpy port_wmemcpy
#define wmemchr port_wmemchr

#endif
