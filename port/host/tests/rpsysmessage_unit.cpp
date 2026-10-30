/**
 * Compiles the real src/Pack/RPKernel/RPSysMessage.cpp into the host harness.
 *
 * That file's first line includes <Pack/RPKernel.h>, the whole RPKernel
 * umbrella header, which pulls in sound, effect and graphics headers that do
 * not build on the host yet. RPSysMessage itself only needs its own header,
 * so this unit includes that, marks the umbrella header as already included
 * (its include guard), and then compiles the unmodified source.
 */

#include <Pack/RPKernel/RPSysMessage.h>

#define RP_PUBLIC_KERNEL_H
#include "../../../src/Pack/RPKernel/RPSysMessage.cpp"
