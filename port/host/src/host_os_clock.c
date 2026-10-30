/**
 * The bus clock the SDK reads from low memory (OS_BUS_CLOCK_SPEED at
 * 0x800000F8; port/docs/HOST_BUILD.md §21). On the host every translation unit
 * that includes OSHardware.h gets a weak, zero definition; this strong one
 * (in a file that does not include it) gives the audio runtime the Wii's
 * value: __AXVPBInit's DSP cycle budget and the OS time macros divide it.
 */

unsigned int OS_BUS_CLOCK_SPEED = 243000000;
