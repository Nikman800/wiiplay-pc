/**
 * Host stand-ins for the AX and thread calls of nw4r::snd when nothing is
 * played (the loader harnesses: snd_harness, hbm_harness; port/docs/HOST_BUILD.md
 * §13). The audio runtime (§21) links src/host_ax.cpp and src/host_thread.cpp
 * instead. Only what the loader harnesses' links keep (checked with link maps,
 * §21): the voice calls of the WSD / channel code and the thread-queue calls of
 * the task manager.
 *
 *   AX       no voices: AXAcquireVoice returns NULL, the rest are no-ops.
 *   OS       OSSleepThread aborts because nothing could wake a single-threaded
 *            host; the queue calls are no-ops.
 */

#include <types.h>

#include <revolution/AX.h>
#include <revolution/OS.h>

#include <cstdio>
#include <cstdlib>

extern "C" {

/* AX */
AXVPB* AXAcquireVoice(u32 prio, AXVoiceCallback callback, uintptr_t userContext) {
    (void)prio;
    (void)callback;
    (void)userContext;
    return NULL;
}
void AXFreeVoice(AXVPB* vpb) {
    (void)vpb;
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
} // extern "C"
