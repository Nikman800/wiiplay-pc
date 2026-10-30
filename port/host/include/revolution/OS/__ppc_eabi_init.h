/**
 * Host (PC) replacement for include/revolution/OS/__ppc_eabi_init.h.
 *
 * The original declares the PowerPC EABI boot hooks and linker-generated
 * section symbols, and an `exit(void)` that conflicts with the C library's.
 * None of it exists on the host.
 */
#ifndef RVL_SDK_OS_PPC_EABI_INIT_H
#define RVL_SDK_OS_PPC_EABI_INIT_H
#include <types.h>
#endif
