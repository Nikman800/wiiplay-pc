/**
 * CodeWarrior's fused multiply-adds in the game's code (port/docs/HOST_BUILD.md §43).
 *
 * The cwcontract plugin (tools/cwcontract) writes each multiply-add that CodeWarrior fused
 * (fmadds, fmsubs, fnmsubs, fnmadds; fmadd ... in double) as a call to these, so they
 * round once as on the Wii. Force-included in the units the plugin compiles. The same bits
 * on every x86 CPU: the vfmadd instruction when the CPU has FMA (Haswell, Piledriver and
 * later), else port_fmaf / port_fma's exact software forms (src/host_fma.c).
 */

#ifndef PORT_CW_FMA_H
#define PORT_CW_FMA_H

#ifdef __cplusplus
extern "C" {
#endif

/* src/host_fma.c */
float port_fmaf(float x, float y, float z);
double port_fma(double x, double y, double z);
/* 1 once the CPU is known to have FMA (set before main), 0 without, -1 before */
extern int port_fma_hw;

#ifdef __cplusplus
}
#endif

/* x*y+z rounded once. The instruction is inline asm, so the unit itself needs no -mfma
 * (the code around it runs on any x86-64 CPU); a CPU without FMA takes the call. */
static inline __attribute__((always_inline, unused)) float port_cw_fmaf(float x, float y,
                                                                         float z) {
#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
    if (__builtin_expect(port_fma_hw > 0, 1)) {
        __asm__("vfmadd213ss %2, %1, %0" : "+x"(x) : "x"(y), "x"(z));
        return x;
    }
#endif
    return port_fmaf(x, y, z);
}

static inline __attribute__((always_inline, unused)) double port_cw_fma(double x, double y,
                                                                         double z) {
#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
    if (__builtin_expect(port_fma_hw > 0, 1)) {
        __asm__("vfmadd213sd %2, %1, %0" : "+x"(x) : "x"(y), "x"(z));
        return x;
    }
#endif
    return port_fma(x, y, z);
}

#endif
