#include <Pack/RPParty/RPWlyScene/RPWlySound_3.h>

// BGM per field (RPWlySoundActor::sFieldIndex)
const u32 RPWlySoundObj::sBgmIds[10] = {0x32, 0x38, 0x3A, 0x3C, 0x42, 0x33, 0x39, 0x3B, 0x3D, 0x43};

// The binary inlines RPSndObject2D's (empty) destructor into the derived one. The host uses
// RPWlySound_2.cpp's out-of-line one: ELF linkers let the strong copy win over
// such a copy, PE/COFF's report two definitions (port/docs/HOST_BUILD.md §41).
#if !defined(PORT_HOST)
inline RPSndObject2D::~RPSndObject2D() {}
#endif

RPWlySoundObj::~RPWlySoundObj() {}

bool RPWlySoundObj::PlayBgm(s32 stage) {
    if (mpBgmHandle != NULL) {
        mpBgmHandle->Stop(0);
    }

    mpBgmHandle = startSound(sBgmIds[RPWlySoundActor::sFieldIndex]);
    mpBgmHandle->SetVolume(1.0f, 0);
    return mpBgmHandle != NULL;
}

bool RPWlySoundObj::StopBgm() {
    if (mpBgmHandle == NULL) {
        return false;
    }

    if (mpBgmHandle->IsAttachedSound()) {
        mpBgmHandle->Stop(0);
        return true;
    }

    return false;
}

bool RPWlySoundObj::DuckBgm() {
    if (mpBgmHandle == NULL) {
        return false;
    }

    if (mpBgmHandle == NULL) {
        return false;
    }

    mpBgmHandle->SetVolume(0.4f, 20);
    mRestoreRequest = true;
    mDuckFrame = 0;
    return true;
}

void RPWlySoundObj::calc() {
    if (mVolume.doUpdate()) {
        for (int i = 0; i < mHandleCount; i++) {
            if (mSoundHandles[i].IsAttachedSound()) {
                nw4r::snd::SeqSoundHandle handle(&mSoundHandles[i]);
                handle.SetTrackVolume(0xFFFF, mVolume.getValue());
            }
        }
    }

    if (mRestoreRequest) {
        mIsDucking = true;
        mRestoreRequest = false;
    }

    if (mIsDucking) {
        if (mDuckFrame > 60) {
            if (mpBgmHandle != NULL) {
                if (mpBgmHandle != NULL) {
                    mpBgmHandle->SetVolume(1.0f, 40);
                }
            }
            mDuckFrame = 0;
            mIsDucking = false;
        } else {
            mDuckFrame++;
        }
    }
}
