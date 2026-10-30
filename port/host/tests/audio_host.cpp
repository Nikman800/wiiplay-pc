/**
 * SDK and EGG stand-ins for audio_harness (port/docs/HOST_BUILD.md §21).
 * Not decompiled code; only what the linked runtime references and the host
 * does not provide elsewhere. The EGG heaps, OSAlloc and SC are the platform
 * layer's (§22), WPAD the input layer's backend (§23), WENC the decomp's.
 *
 *   alarms    never fire: the only users are the Wii Remote speaker
 *             (RemoteSpeakerManager's periodic alarm and RemoteSpeaker's
 *             continue / interval alarms); the host AX produces no remote
 *             speaker samples (AXRmt*), so there is nothing to send.
 *   OS        OSGetTime is OSGetTick (host_snd.cpp: a call counter, so runs
 *             are reproducible); OSRegisterVersion is a no-op.
 *   EGG       the static members of AudioSound3DActor (defined in a unit the
 *             decomp does not have yet).
 */
#include <types.h>

#include <Pack/RPAudio.h>
#include <egg/audio.h>
#include <egg/core.h>
#include <revolution/OS.h>

extern "C" {

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

} // extern "C"

namespace EGG {

nw4r::snd::SoundArchivePlayer* AudioSound3DActor::smCommonPlayer = NULL;
nw4r::snd::Sound3DManager* AudioSound3DActor::smCommon3DManager = NULL;

} // namespace EGG

// Static members the decomp defines in units it does not have yet (as
// AudioSound3DActor's above): RPSndObject.h's actor list and the per-player
// 3D managers of EGG's camera actors.
nw4r::ut::List RPSndAudioActorBase::sActorList;
template <>
nw4r::snd::Sound3DManager* EGG::AudioSoundActorBaseWithCamera<4>::smCommon3DManager[4] = {};
