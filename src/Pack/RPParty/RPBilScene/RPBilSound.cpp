// The binary's .data ends with this unit's copies of TRPSndObject<8>'s weak index assertion
// literals (unique to N = 8), so the inline member form is used here.
#define RP_SND_OBJECT_INDEXED_HANDLE_MEMBERS
#include <Pack/RPParty/RPBilScene/RPBilSound.h>

#include <Pack/RPAudio/RPSndAudioMgr.h>

RPBilSound3D::~RPBilSound3D() {}

namespace {

// The line through (x0, y0) and (x1, y1): the slope folds to a constant, the intercept does not.
inline void SetLine(f32& rSlope, f32& rIntercept, f32 x0, f32 y0, f32 x1, f32 y1) {
    rSlope = (y1 - y0) / (x1 - x0);
    rIntercept = y0 - rSlope * x0;
}

} // namespace

RPBilSound3D::RPBilSound3D()
    : RPBilSound3DBase(RPSndAudioMgr::instance()->getPlayer(),
                       *static_cast<nw4r::snd::Sound3DManager*>(NULL)) {
    SetLine(mSpinSlope, mSpinIntercept, 0.2f, 0.0f, 0.8f, 1.0f);
    SetLine(mSlideSlope, mSlideIntercept, 0.15f, 0.0f, 0.5f, 1.0f);
    SetLine(mCueSlope, mCueIntercept, 20.0f, 0.1f, 64.0f, 1.0f);
    SetLine(mRollSlope, mRollIntercept, 3.0f, 1.3f, 4.8f, 1.9f);
}

// Weak copies the original emits right after playCueHit.
template void TRPSndObject<8>::detailStartSoundWithRemotePlayer(nw4r::snd::SoundHandle*, u32, u32,
                                                                s32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::startSoundWithRemotePlayer(ui32, u32,
                                                                             s32);

bool RPBilSound3D::playCueHit(f32 speed, s32 player, s32 remoteFlag) {
    nw4r::snd::SoundHandle* pHandle;

    if (player == 0) {
        pHandle = startSound(0x11U);
        startSoundWithRemotePlayer(0x1FU, remoteFlag, 0);
        return pHandle != NULL;
    }

    if (speed > 80.0f) {
        pHandle = startSound(0xBU);
        startSoundWithRemotePlayer(0x1DU, remoteFlag, 0);
    } else if (speed > 50.0f) {
        pHandle = startSound(0xCU);
        startSoundWithRemotePlayer(0x1EU, remoteFlag, 0);
    } else {
        pHandle = startSound(0xDU);
        startSoundWithRemotePlayer(0x1FU, remoteFlag, 0);
    }

    if (pHandle == NULL) {
        return false;
    }

    f32 volume = mCueSlope * speed + mCueIntercept;
    if (volume > 1.0f) {
        volume = 1.0f;
    } else if (volume < 0.2f) {
        volume = 0.2f;
    }
    pHandle->SetVolume(volume, 0);
    return true;
}

// Weak copy the original emits right after playImpact.
template nw4r::snd::SoundHandle* TRPSndObject<8>::startSound(u32, u32);

bool RPBilSound3D::playImpact(f32 speed, s32 material) {
    u32 id = material;
    if (id == 8) {
        if (speed < 30.0f) {
            id = 10;
        } else if (speed < 55.0f) {
            id = 9;
        }
    }

    nw4r::snd::SoundHandle* pHandle = startSound(id);
    if (pHandle == NULL) {
        return false;
    }

    f32 volume;
    if (speed > 60.0f) {
        volume = 1.0f;
    } else if (speed < 0.1f) {
        volume = 0.0f;
    } else {
        volume = speed * 0.017f;
    }
    pHandle->SetVolume(volume, 0);
    return true;
}

bool RPBilSound3D::playRollingLoop(f32 linearSpeed, f32 angularSpeed, s32 material) {
    bool played = false;

    if (angularSpeed > 0.2f) {
        nw4r::snd::SoundHandle* pHandle = holdSound(material != 0 ? 0x14U : 0x17U);
        if (pHandle != NULL) {
            f32 volume = mSpinSlope * angularSpeed + mSpinIntercept;
            if (volume < 0.0f) {
                volume = 0.0f;
            } else if (volume > 1.0f) {
                volume = 1.0f;
            }
            pHandle->SetVolume(volume, 0);
            played = true;
        }
    }

    if (linearSpeed > 0.1f) {
        nw4r::snd::SoundHandle* pHandle = holdSound(material != 0 ? 0x13U : 0x16U);
        if (pHandle != NULL) {
            f32 volume = mSlideSlope * (1.3f * linearSpeed - angularSpeed) + mSlideIntercept;
            if (volume < 0.0f) {
                volume = 0.0f;
            } else if (volume > 1.0f) {
                volume = 1.0f;
            }
            pHandle->SetVolume(volume, 0);
            played = true;
        }
    }

    return played;
}

// Weak copies the original emits right after playRoll.
template void TRPSndObject<8>::detailHoldSound(nw4r::snd::SoundHandle*, u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSound(ui32, u32);

bool RPBilSound3D::playRoll(f32 speed) {
    if (speed <= 0.0f) {
        return false;
    }

    nw4r::snd::SoundHandle* pHandle = holdSound(0x19U, 0);
    if (pHandle == NULL) {
        return false;
    }

    f32 pitch = mRollSlope * speed + mRollIntercept;
    if (pitch < 0.3f) {
        pitch = 0.3f;
    } else if (pitch > 2.0f) {
        pitch = 2.0f;
    }
    pHandle->SetPitch(pitch);
    return true;
}

// The original emits the remaining weak copies in this order (vtable-only TRPSndObject<8>
// virtuals in reverse slot order, then the getters), as in RPFshFish.cpp. startSound(unsigned
// int), detailStartSound and set3DPosition are RPBilPlayer.cpp's copies.
#pragma defer_codegen off
// Unreferenced stand-ins (dead-stripped by the linker). The first flushes the deferred
// functions above before dont_inline applies, and its getIndexedHandle creates that helper's
// weak assertion literals before getIndexedHandleStop's: weak @STRING@ data is laid out in
// reverse creation order, and the original has the Stop pair (prepare/startSound) first.
// The second's direct calls put the in-class getters' weak copies first, as in the original.
extern "C" {
static void Bil_sound3DDeferFlush(TRPSndObject<8>* pObject) { pObject->getIndexedHandle(0); }
}
#pragma dont_inline on
extern "C" {
static void Bil_sound3DGetterOrder(TRPSndAudioActorBaseWithHandles<4, 8>* pActor) {
    // emitted in reverse call order
    pActor->TRPSndAudioActorBaseWithHandles<4, 8>::get3DPosition(0);
    pActor->TRPSndAudioHandles<12>::getOuterVolume();
    pActor->TRPSndAudioHandles<12>::getOuterPitch();
}
}
#pragma dont_inline off
template void TRPSndObject<8>::detailHoldSoundWithRemotePlayer(nw4r::snd::SoundHandle*, u32, u32,
                                                               s32);
template void TRPSndObject<8>::detailPrepareSound(nw4r::snd::SoundHandle*, u32, u32);
template f32 TRPSndObject<8>::getOuterVolume();
template void TRPSndObject<8>::setActorVolume(f32, int);
template void TRPSndObject<8>::stopAllSound(int);
template void TRPSndObject<8>::stopSound(const char*, int);
template void TRPSndObject<8>::stopSound(ui32, int);
template void TRPSndObject<8>::stopSound(u32, int);
template nw4r::snd::SoundHandle* TRPSndObject<8>::prepareSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::prepareSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::prepareSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::prepareSound(ui32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::prepareSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSoundWithRemotePlayer(const char*, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSoundWithRemotePlayer(ui32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::holdSoundWithRemotePlayer(u32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::startSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::startSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::startSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::startSoundWithRemotePlayer(const char*, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<8>::startSoundWithRemotePlayer(u32, u32, s32);
template void TRPSndObject<8>::update();
template const nw4r::math::VEC3* TRPSndObject<8>::get3DPosition(int);
template u32 TRPSndObject<8>::getSoundId(ui32, int);
template const char* TRPSndObject<8>::getSoundLabel(const char*, int);
template u32 TRPSndObject<8>::getSoundId(u32, int);
