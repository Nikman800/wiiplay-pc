/**
 * SDK and EGG stand-ins for audio_harness (port/docs/HOST_BUILD.md §21). Not
 * decompiled code; only what the linked runtime references and the host does
 * not provide elsewhere.
 *
 *   SC        the system settings say stereo (SCGetSoundMode).
 *   alarms    never fire: the only users are the Wii Remote speaker
 *             (RemoteSpeakerManager's periodic alarm and RemoteSpeaker's
 *             continue / interval alarms); there is no remote speaker.
 *   WPAD,     no remote speaker: nothing can be sent.
 *   WENC
 *   OS        OSGetTime is OSGetTick (host_snd.cpp: a call counter, so runs
 *             are reproducible); OSRegisterVersion is a no-op. The OS heap
 *             (AXFX's default allocation hooks; nw4r installs its own before
 *             any effect allocates) aborts if reached.
 *   EGG       the static members of AudioSound3DActor (defined in a unit the
 *             decomp does not have yet); EGG::Allocator and ExpHeap::create,
 *             which SimpleAudioMgr / SoundHeapMgr::createSoundHeap(Heap*) and
 *             AudioFx::init reference: the host has no EGG heaps (host_sdk.cpp),
 *             so the harness puts the sound heap on the M0 arena itself and
 *             ExpHeap::create returns NULL (AudioFx then has no heap; the
 *             harness creates the default effects through nw4r directly).
 *             The Allocator entries abort if reached.
 */
#include <types.h>

#include <egg/audio.h>
#include <egg/core.h>
#include <revolution/OS.h>
#include <revolution/SC.h>
#include <revolution/WENC.h>
#include <revolution/WPAD.h>

#include <cstdio>
#include <cstdlib>

namespace {
void Abort(const char* pName) {
    std::fprintf(stderr, "audio_host: %s called on the host (not implemented)\n", pName);
    std::abort();
}
} // namespace

extern "C" {

/* SC */
void SCInit(void) {}
u32 SCCheckStatus(void) {
    return SC_STATUS_OK;
}
u8 SCGetSoundMode(void) {
    return SC_SND_STEREO;
}

/* OS */
void OSRegisterVersion(const char* ver) {
    (void)ver;
}
s64 OSGetTime(void) {
    return OSGetTick();
}
void OSCreateAlarm(OSAlarm* alarm) {
    (void)alarm;
}
void OSSetAlarm(OSAlarm* alarm, s64 tick, OSAlarmHandler handler) {
    (void)alarm;
    (void)tick;
    (void)handler;
}
void OSSetPeriodicAlarm(OSAlarm* alarm, s64 tick, s64 period, OSAlarmHandler handler) {
    (void)alarm;
    (void)tick;
    (void)period;
    (void)handler;
}
void OSCancelAlarm(OSAlarm* alarm) {
    (void)alarm;
}
static void* sAlarmUserData = NULL;
void OSSetAlarmUserData(OSAlarm* alarm, void* userData) {
    (void)alarm;
    sAlarmUserData = userData;
}
void* OSGetAlarmUserData(const OSAlarm* alarm) {
    (void)alarm;
    return sAlarmUserData;
}

volatile s32 __OSCurrHeap = -1;
void* OSAllocFromHeap(s32 handle, s32 size) {
    (void)handle;
    (void)size;
    Abort("OSAllocFromHeap");
    return NULL;
}
void OSFreeToHeap(s32 handle, void* p) {
    (void)handle;
    (void)p;
    Abort("OSFreeToHeap");
}

/* WPAD / WENC: no remote speaker. */
s32 WPADControlSpeaker(s32 chan, u32 command, WPADCallback pCallback) {
    (void)chan;
    (void)command;
    (void)pCallback;
    return WPAD_ERR_NO_CONTROLLER;
}
BOOL WPADCanSendStreamData(s32 chan) {
    (void)chan;
    return FALSE;
}
s32 WPADSendStreamData(s32 chan, void* pData, u16 len) {
    (void)chan;
    (void)pData;
    (void)len;
    return WPAD_ERR_NO_CONTROLLER;
}
s32 WENCGetEncodeData(WENCInfo* info, u32 flag, const s16* pcmData, s32 samples, u8* outData) {
    (void)info;
    (void)flag;
    (void)pcmData;
    (void)samples;
    (void)outData;
    return 0;
}

} // extern "C"

namespace EGG {

nw4r::snd::SoundArchivePlayer* AudioSound3DActor::smCommonPlayer = NULL;
nw4r::snd::Sound3DManager* AudioSound3DActor::smCommon3DManager = NULL;

Allocator::Allocator(Heap* pHeap, s32 align) {
    (void)pHeap;
    (void)align;
    Abort("EGG::Allocator::Allocator");
}
Allocator::~Allocator() {}
void* Allocator::alloc(u32 size) {
    (void)size;
    Abort("EGG::Allocator::alloc");
    return NULL;
}
void Allocator::free(void* pBlock) {
    (void)pBlock;
    Abort("EGG::Allocator::free");
}

ExpHeap* ExpHeap::create(void* pHeapStart, u32 size, u16 opt) {
    (void)pHeapStart;
    (void)size;
    (void)opt;
    return NULL;
}

} // namespace EGG
