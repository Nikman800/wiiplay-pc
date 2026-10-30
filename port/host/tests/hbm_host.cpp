/**
 * SDK stand-ins for hbm_harness (port/docs/HOST_BUILD.md §16). Not decompiled
 * code; only what homebuttonMiniLib's HBMCreate path and RemoteSpk reference.
 * The AX / thread / interrupt stand-ins come from src/host_snd.cpp.
 *
 * There are no controllers or Wii Remote speaker on the host: every remote's
 * speaker reports itself enabled and ready, WPADSendStreamData drops the data,
 * and WENCGetEncodeData records the PCM it is given (gHbmHostEncoded) so the
 * harness can check what UpdateSpeaker made of the .bwav samples. Calls that
 * HBMCreate and the speaker path never reach abort with their name.
 */
#include <types.h>

#include <revolution/GX.h>
#include <revolution/MEM.h>
#include <revolution/MTX.h>
#include <revolution/OS.h>
#include <revolution/WENC.h>
#include <revolution/WPAD.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

std::vector<s16> gHbmHostEncoded;

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

s64 OSGetTime(void) {
    return 0;
}

/* WPAD / WENC: one always-ready speaker per remote; the encoder input is kept. */
BOOL WPADIsSpeakerEnabled(s32 chan) {
    (void)chan;
    return TRUE;
}

BOOL WPADCanSendStreamData(s32 chan) {
    (void)chan;
    return TRUE;
}

s32 WPADSendStreamData(s32 chan, void* pData, u16 len) {
    (void)chan, (void)pData, (void)len;
    return WPAD_ERR_OK;
}

s32 WENCGetEncodeData(WENCInfo* info, u32 flag, const s16* pcmData, s32 samples, u8* outData) {
    (void)info, (void)flag;
    gHbmHostEncoded.insert(gHbmHostEncoded.end(), pcmData, pcmData + samples);
    std::memset(outData, 0, (samples + 1) / 2);
    return samples;
}

void WPADControlMotor(s32 chan, u32 command) {
    (void)chan, (void)command;
    Abort("WPADControlMotor");
}

void WPADEnableMotor(BOOL enable) {
    (void)enable;
    Abort("WPADEnableMotor");
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

u32 PSMTXInverse(const Mtx src, Mtx inv) {
    (void)src, (void)inv;
    Abort("PSMTXInverse");
    return 0;
}

void PSMTXMultVec(const Mtx mtx, const Vec* vec, Vec* out) {
    (void)mtx, (void)vec, (void)out;
    Abort("PSMTXMultVec");
}

void PSMTXTrans(Mtx mtx, f32 x, f32 y, f32 z) {
    (void)mtx, (void)x, (void)y, (void)z;
    Abort("PSMTXTrans");
}

} // extern "C"
