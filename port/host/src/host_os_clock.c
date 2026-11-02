/**
 * The bus clock the SDK reads from low memory (OS_BUS_CLOCK_SPEED at
 * 0x800000F8; port/docs/HOST_BUILD.md §21). On the host every translation unit
 * that includes OSHardware.h gets a weak, zero definition; this strong one
 * (in a file that does not include it) gives the audio runtime the Wii's
 * value: __AXVPBInit's DSP cycle budget and the OS time macros divide it.
 */

#ifdef _WIN32
/* PE/COFF: those definitions are COMDAT "select any" (decomp/compat.h, HOST_BUILD.md
 * §41) and would clash with a strong one; the value is stored before main instead. */
extern unsigned int OS_BUS_CLOCK_SPEED;
__attribute__((constructor)) static void PortInitBusClock(void) {
    OS_BUS_CLOCK_SPEED = 243000000;
}
#else
unsigned int OS_BUS_CLOCK_SPEED = 243000000;
#endif
