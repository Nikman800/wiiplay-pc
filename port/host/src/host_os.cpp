/**
 * OS memory on the host (port/docs/HOST_BUILD.md §22): the boot information
 * and the MEM1 / MEM2 arenas inside the M0 emulated memory (port/arena.h).
 *
 * On the Wii, the apploader and OSInit leave the boot information at
 * 0x80000000 and the arena bounds in the OS (OSArena.c, linked from the
 * decomp unmodified). The game's system init then builds its heaps over the
 * arenas (RPSysSystem::initialize, EGG::ConfigurationData::initMemory):
 * OSInitAlloc, the MEM1 / MEM2 root EGG::ExpHeaps, the system heap.
 * PortOSInit does what happens before that:
 *
 *   MEM1 arena  0x80500000 .. 0x81800000 (lowered below the FST by
 *               PortDVDSetDisc, as the apploader does)
 *   MEM2 arena  0x90000800 .. 0x933E0000 (the top of MEM2 belongs to IOS)
 *
 * MEM1 below 0x80500000 stands for the DOL (the game's code and data end near
 * 0x8044C000) and, with MEM2 above 0x933E0000, stays with the host allocator
 * (PortArenaAlloc: the host's own buffers, and the arena-backed stand-ins the
 * older harnesses use). The arena ranges are taken out of that allocator, so
 * the two never overlap.
 *
 * The boot information is runtime data in host layout (its pointers are host
 * pointers); physMemSize is at 0x80000028 in both layouts.
 */

#include <revolution/OS.h>

#include <port/arena.h>
#include <port/host.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const u32 MEM1_ARENA_LO = 0x80500000;
const u32 MEM1_ARENA_HI = 0x81800000;
const u32 MEM2_ARENA_LO = 0x90000800;
const u32 MEM2_ARENA_HI = 0x933E0000;

bool sInitialized = false;

} // namespace

extern "C" {

void PortOSInit(void) {
    if (sInitialized) {
        return;
    }
    sInitialized = true;
    PortArenaInit();

    void* mem1Lo = port_hptr(MEM1_ARENA_LO);
    void* mem1Hi = port_hptr(MEM1_ARENA_HI);
    void* mem2Lo = port_hptr(MEM2_ARENA_LO);
    void* mem2Hi = port_hptr(MEM2_ARENA_HI);
    if (!PortArenaReserve(mem1Lo, mem1Hi) || !PortArenaReserve(mem2Lo, mem2Hi)) {
        std::fprintf(stderr, "host_os: the MEM1/MEM2 arenas are already in use (call "
                             "PortOSInit before anything allocates)\n");
        std::abort();
    }

    OSBootInfo* bi = static_cast<OSBootInfo*>(OSPhysicalToCached(OS_PHYS_BOOT_INFO));
    std::memset(bi, 0, sizeof(*bi));
    bi->bootMagic = OS_BOOT_MAGIC_BOOTROM;
    bi->physMemSize = PORT_MEM1_SIZE;
    bi->consoleType = OS_CONSOLE_RVL_PP_2_2;
    bi->arenaLo = mem1Lo;
    bi->arenaHi = mem1Hi;

    OSSetMEM1ArenaLo(mem1Lo);
    OSSetMEM1ArenaHi(mem1Hi);
    OSSetMEM2ArenaLo(mem2Lo);
    OSSetMEM2ArenaHi(mem2Hi);
}

/* Interrupts: there are none to mask; the cooperative threads only switch
 * where the Wii would reschedule (src/host_thread.cpp). Weak, as
 * src/host_snd.cpp has the same pair for the sound harnesses. */
__attribute__((weak)) BOOL OSDisableInterrupts(void) {
    return TRUE;
}
__attribute__((weak)) BOOL OSRestoreInterrupts(BOOL status) {
    return status;
}
__attribute__((weak)) BOOL OSEnableInterrupts(void) {
    return TRUE;
}

} // extern "C"
