/**
 * Host (PC) replacement for include/decomp/compat.h.
 *
 * The original maps CodeWarrior-only syntax for IDE parsers (clangd,
 * Intellisense). A real host build needs the same treatment, but with working
 * intrinsics instead of `0` placeholders, so this file is used for every host
 * compile (port/host/include is first on the include path).
 */

#ifndef COMPAT_H
#define COMPAT_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

// Treat the host build like a parser build where the decomp checks for one.
#define COMPAT_ANY

#define register
#define ASM(...)
#define ASM_VOLATILE(...)
#define DECLTYPE(x) decltype(x)
#define DECL_ADDRESS(...) __attribute__((weak))
#define FLEXIBLE_ARRAY(NAME) NAME[]

#define __alloca __builtin_alloca
#define __memclr(var, size) __builtin_memset((var), 0, (size))

// CodeWarrior's `#pragma unused(x)` etc. are ignored by GCC/Clang (with a
// warning); nothing else to do here.

#endif
