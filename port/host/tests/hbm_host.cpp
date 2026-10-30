/**
 * SDK stand-ins for hbm_harness (port/docs/HOST_BUILD.md §16). Not decompiled
 * code; only what homebuttonMiniLib's HBMCreate path and RemoteSpk reference.
 * The AX / thread / interrupt stand-ins come from src/host_snd.cpp and src/host_snd_stubs.cpp.
 *
 * The remote speaker path runs on the platform layer: WPAD is the §23 backend
 * (src/host_wpad.cpp), WENC the decomp's encoder. RemoteSpk::Start's periodic
 * alarm is recorded here (gHbmHostPeriodicHandler) for the harness to fire;
 * one-shot alarms (the retry after a communication error, which the backend
 * never reports) abort. Calls that HBMCreate and the speaker path never reach
 * abort with their name.
 */
#include <types.h>

#include <revolution/GX.h>
#include <revolution/MEM.h>
#include <revolution/MTX.h>
#include <revolution/OS.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void Abort(const char* pName) {
    std::fprintf(stderr, "hbm_host: %s called on the host (not implemented)\n", pName);
    std::abort();
}

void* AllocForExpHeap(MEMAllocator* pAllocator, u32 size) {
    return MEMAllocFromExpHeapEx(pAllocator->heap, size, pAllocator->heapParam1);
}

void FreeForExpHeap(MEMAllocator* pAllocator, void* pBlock) {
    MEMFreeToExpHeap(pAllocator->heap, pBlock);
}

} // namespace

extern "C" {

OSAlarmHandler gHbmHostPeriodicHandler = NULL;

/* MEM: src/revolution/MEM/mem_allocator.c's expanded-heap allocator (the
 * dispatch functions are in src/host_gx.cpp). */
void MEMInitAllocatorForExpHeap(MEMAllocator* allocator, MEMiHeapHead* heap, s32 align) {
    static const MEMAllocatorFuncs sAllocatorFunc = {AllocForExpHeap, FreeForExpHeap};
    allocator->funcs = &sAllocatorFunc;
    allocator->heap = heap;
    allocator->heapParam1 = align;
    allocator->heapParam2 = 0;
}

/* OS alarms: RemoteSpk::Start's periodic alarm is recorded, not scheduled. */
void OSCreateAlarm(OSAlarm* alarm) {
    std::memset(alarm, 0, sizeof(*alarm));
}

void OSCancelAlarm(OSAlarm* alarm) {
    (void)alarm;
}

void OSSetPeriodicAlarm(OSAlarm* alarm, s64 tick, s64 period, OSAlarmHandler handler) {
    (void)alarm, (void)tick, (void)period;
    gHbmHostPeriodicHandler = handler;
}

/* One-shot alarms: RemoteSpk retries WPADControlSpeaker 50 ms after a
 * communication error, which the WPAD backend never reports. */
void OSSetAlarm(OSAlarm* alarm, s64 tick, OSAlarmHandler handler) {
    (void)alarm, (void)tick, (void)handler;
    Abort("OSSetAlarm");
}

void OSSetAlarmUserData(OSAlarm* alarm, void* userData) {
    (void)alarm, (void)userData;
    Abort("OSSetAlarmUserData");
}

void* OSGetAlarmUserData(const OSAlarm* alarm) {
    (void)alarm;
    Abort("OSGetAlarmUserData");
    return NULL;
}

s64 OSGetTime(void) {
    return 0;
}

/* Drawing and pointer hit tests (HBMDraw / HBMCalc; not on the create path). */
void GXSetCullMode(GXCullMode mode) {
    (void)mode;
    Abort("GXSetCullMode");
}

void GXSetLineWidth(u8 width, u32 offset) {
    (void)width, (void)offset;
    Abort("GXSetLineWidth");
}

/* PSMTXInverse, PSMTXMultVec and PSMTXTrans come from the library (src/revolution/MTX
 * and src/host_mtx.cpp; docs/HOST_BUILD.md §18). */

} // extern "C"
