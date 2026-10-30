/**
 * Host stand-ins for the AX / AI and thread calls of nw4r::snd when nothing is
 * played (the loader harnesses: snd_harness, hbm_harness; port/docs/HOST_BUILD.md
 * §13). The audio runtime (§21) links src/host_ax.cpp and src/host_thread.cpp
 * instead.
 *
 *   AX, AI   no voices, no DSP, no audio interface: AXAcquireVoice returns
 *            NULL, the aux callback registrations are remembered and returned,
 *            everything else is a no-op.
 *   OS       threads are never started (the sound thread is created only by
 *            SoundSystem::InitSoundSystem, which the loader path does not
 *            call; OSResumeThread aborts); OSSleepThread aborts because nothing
 *            could wake it.
 */

#include <types.h>

#include <revolution/AI.h>
#include <revolution/AX.h>
#include <revolution/OS.h>

#include <cstdio>
#include <cstdlib>

namespace {
AXAuxCallback sAuxCallback[3];
void* sAuxContext[3];
} // namespace

extern "C" {

/* AI */
AIDMACallback AIRegisterDMACallback(AIDMACallback callback) {
    (void)callback;
    return NULL;
}

/* AX */
AXOutCallback AXRegisterCallback(AXOutCallback callback) {
    (void)callback;
    return NULL;
}
AXVPB* AXAcquireVoice(u32 prio, AXVoiceCallback callback, uintptr_t userContext) {
    (void)prio;
    (void)callback;
    (void)userContext;
    return NULL;
}
void AXFreeVoice(AXVPB* vpb) {
    (void)vpb;
}
s32 AXGetMaxVoices(void) {
    return 0;
}
void AXRegisterAuxACallback(AXAuxCallback callback, void* context) {
    sAuxCallback[0] = callback;
    sAuxContext[0] = context;
}
void AXRegisterAuxBCallback(AXAuxCallback callback, void* context) {
    sAuxCallback[1] = callback;
    sAuxContext[1] = context;
}
void AXRegisterAuxCCallback(AXAuxCallback callback, void* context) {
    sAuxCallback[2] = callback;
    sAuxContext[2] = context;
}
void AXGetAuxACallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[0];
    *context = sAuxContext[0];
}
void AXGetAuxBCallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[1];
    *context = sAuxContext[1];
}
void AXGetAuxCCallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[2];
    *context = sAuxContext[2];
}
void AXGetLpfCoefs(u16 freq, u16* a, u16* b) {
    (void)freq;
    *a = 0;
    *b = 0;
}
void AXSetAuxAReturnVolume(u16 volume) {
    (void)volume;
}
void AXSetAuxBReturnVolume(u16 volume) {
    (void)volume;
}
void AXSetAuxCReturnVolume(u16 volume) {
    (void)volume;
}
void AXSetMasterVolume(u16 volume) {
    (void)volume;
}
void AXSetMaxDspCycles(u32 num) {
    (void)num;
}
void AXSetMode(u32 mode) {
    (void)mode;
}
void AXSetVoiceAddr(AXVPB* vpb, AXPBADDR* addr) {
    (void)vpb;
    (void)addr;
}
void AXSetVoicePriority(AXVPB* vpb, u32 prio) {
    (void)vpb;
    (void)prio;
}
void AXSetVoiceState(AXVPB* vpb, u16 state) {
    (void)vpb;
    (void)state;
}

/* OS */
BOOL OSCreateThread(OSThread* thread, OSThreadFunc func, void* funcArg, void* stackBegin,
                    u32 stackSize, s32 prio, u16 flags) {
    (void)thread;
    (void)func;
    (void)funcArg;
    (void)stackBegin;
    (void)stackSize;
    (void)prio;
    (void)flags;
    return TRUE;
}
s32 OSResumeThread(OSThread* thread) {
    (void)thread;
    std::fprintf(stderr, "host_snd: OSResumeThread: threads are not started on the host\n");
    std::abort();
}
BOOL OSJoinThread(OSThread* thread, void* val) {
    (void)thread;
    (void)val;
    return TRUE;
}
void OSInitThreadQueue(OSThreadQueue* queue) {
    (void)queue;
}
void OSSleepThread(OSThreadQueue* queue) {
    (void)queue;
    std::fprintf(stderr, "host_snd: OSSleepThread: nothing can wake a single-threaded host\n");
    std::abort();
}
void OSWakeupThread(OSThreadQueue* queue) {
    (void)queue;
}
BOOL OSJamMessage(OSMessageQueue* queue, OSMessage mesg, u32 flags) {
    // Single-threaded ring buffer (host_sdk.cpp): the same as sending.
    return OSSendMessage(queue, mesg, flags);
}
} // extern "C"
