// Find Mii game manager (RPWlyGameMgr) and the out-of-line virtuals of RPSndObject2D.
// Unit 0x8027C5BC-0x8027FA8C (dtk/game_units.tsv). CodeWarrior emits the functions in reverse
// definition order: RPSndObject2D's are defined first, the manager's Draw last.
#include <Pack/RPParty/RPWlyScene/RPWlySound_2.h>

#include <Pack/RPAudio.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPGraphics/RPGrpCaptureTexture.h>
#include <Pack/RPGraphics/RPGrpDrawPathLightMap.h>
#include <Pack/RPGraphics/RPGrpDrawPathManager.h>
#include <Pack/RPGraphics/RPGrpLightManager.h>
#include <Pack/RPKernel/RPSysKokeshiManager.h>
#include <Pack/RPKernel/RPSysMessage.h>
#include <Pack/RPKernel/RPSysMiddleDB.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPLeadCursor.h>
#include <Pack/RPParty/RPPartyCommon/RPPauseText.h>
#include <Pack/RPParty/RPPartyCommon/RPScreenFade.h>
#include <Pack/RPParty/RPWlyScene/RPWlyGameConfig.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <Pack/RPSystem/RPSysSceneCreator.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>

// The manager's members and the state it shares with the phases, layouts and level table are
// private in the shared headers.
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>
#include <Pack/RPParty/RPWlyScene/RPWlyAvatarMgr.h>
#include <Pack/RPParty/RPWlyScene/RPWlyCursor.h>
#include <Pack/RPParty/RPWlyScene/RPWlyField.h>
#include <Pack/RPParty/RPWlyScene/RPWlyField_4.h>
#include <Pack/RPParty/RPWlyScene/RPWlyField_5.h>
#include <Pack/RPParty/RPWlyScene/RPWlyGameMgr.h>
#include <Pack/RPParty/RPWlyScene/RPWlyLevelTable.h>
#include <Pack/RPParty/RPWlyScene/RPWlyLyt.h>
#include <Pack/RPParty/RPWlyScene/RPWlyPhase.h>

#include <egg/core.h>
#include <egg/gfxe.h>

/******************************************************************************
 *
 * RPSndObject2D
 *
 ******************************************************************************/

namespace {

// The outer volume and pitch, read through the handle set's own vtable
inline f32 GetOuterVolume(TRPSndAudioHandles<4>& rHandles) {
    return rHandles.getOuterVolume();
}

inline f32 GetOuterPitch(TRPSndAudioHandles<4>& rHandles) {
    return rHandles.getOuterPitch();
}

inline void ApplyOuterVolume(nw4r::snd::SoundHandle* pHandle, TRPSndAudioHandles<4>& rHandles,
                             f32 scale) {
    if (pHandle != NULL) {
        pHandle->SetVolume(scale * rHandles.getOuterVolume(), 0);
    }
}

inline void ApplyOuterPitch(nw4r::snd::SoundHandle* pHandle, TRPSndAudioHandles<4>& rHandles,
                            f32 scale) {
    if (pHandle != NULL) {
        pHandle->SetPitch(scale * rHandles.getOuterPitch());
    }
}

// The user parameter of a sound given by its label (0 for an unknown label)
inline u32 GetLabelUserParam(const char* pLabel) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    u32 param = 0;
    if (id != static_cast<u32>(-1)) {
        param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
    }
    return param;
}

} // namespace

// The indexed start/prepare overloads inline TRPSndAudioActorBaseWithHandles<4, 0>::
// getIndexedHandleStop (the binary has a single copy of its assertion literals, RPFshFish's at
// 0x80358AAC).
inline nw4r::snd::SoundHandle* GetIndexedHandleStop(RPSndObject2D& rObj, int index) {
    return rObj.getIndexedHandleStop(index);
}

// The key function: RPSndObject2D's vtable (and with it the @60 getOuterVolume thunk that
// RPWlySoundObj's vtable uses) is emitted here. Unreferenced, so the linker drops both.
RPSndObject2D::~RPSndObject2D() {}

nw4r::snd::SoundHandle* RPSndObject2D::startSound(u32 id) {
    if (mIsSilenced) {
        return NULL;
    }

    nw4r::snd::SoundHandle* pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->StartSound(pHandle, id);
        ApplyOuterVolume(pHandle, *this, 1.0f);
        ApplyOuterPitch(pHandle, *this, 1.0f);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::startSoundWithRemotePlayer(u32 id, u32 remoteFlag) {
    if (mIsSilenced) {
        return NULL;
    }

    nw4r::snd::SoundHandle* pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->StartSound(pHandle, id);
        ApplyOuterVolume(pHandle, *this, 1.0f);
        ApplyOuterPitch(pHandle, *this, 1.0f);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, remoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::startSound(u32 id, int idx) {
    if (mIsSilenced) {
        return NULL;
    }

    nw4r::snd::SoundHandle* pHandle =
        GetIndexedHandleStop(*this, idx);
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->StartSound(pHandle, id);
        ApplyOuterVolume(pHandle, *this, 1.0f);
        ApplyOuterPitch(pHandle, *this, 1.0f);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::holdSound(u32 id) {
    nw4r::snd::SoundHandle* pHandle = FindFishSoundHandle(mSoundHandles, mHandleCount, id);
    if (pHandle == NULL) {
        if (mIsSilenced) {
            return NULL;
        }
        pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
    }
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->HoldSound(pHandle, id);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::holdSound(ui32 id) {
    nw4r::snd::SoundHandle* pHandle = FindFishSoundHandle(mSoundHandles, mHandleCount, id);
    if (pHandle == NULL) {
        if (mIsSilenced) {
            return NULL;
        }
        pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
    }
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->HoldSound(pHandle, id);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::holdSound(const char* pLabel) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle = FindFishSoundHandle(mSoundHandles, mHandleCount, id);
        if (pHandle == NULL) {
            if (mIsSilenced) {
                return NULL;
            }
            pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
        }
        if (pHandle != NULL) {
            RPSndUtility::getPlayer()->HoldSound(pHandle, id);
            u32 param = GetLabelUserParam(pLabel);
            if (param & 0xFFFF0000) {
                f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
                f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
                f32 remoteOut = (param >> 24) / 127.0f;
                RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag, mainOut,
                                                           remoteOut, muteOut);
            }
            return pHandle;
        }
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::holdSoundWithRemotePlayer(u32 id, u32 remoteFlag) {
    nw4r::snd::SoundHandle* pHandle = FindFishSoundHandle(mSoundHandles, mHandleCount, id);
    if (pHandle == NULL) {
        if (mIsSilenced) {
            return NULL;
        }
        pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
    }
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->HoldSound(pHandle, id);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, remoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::holdSoundWithRemotePlayer(const char* pLabel,
                                                                 u32 remoteFlag) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle = FindFishSoundHandle(mSoundHandles, mHandleCount, id);
        if (pHandle == NULL) {
            if (mIsSilenced) {
                return NULL;
            }
            pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
        }
        if (pHandle != NULL) {
            RPSndUtility::getPlayer()->HoldSound(pHandle, id);
            u32 param = GetLabelUserParam(pLabel);
            if (param & 0xFFFF0000) {
                f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
                f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
                f32 remoteOut = (param >> 24) / 127.0f;
                RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, remoteFlag, mainOut,
                                                           remoteOut, muteOut);
            }
            return pHandle;
        }
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::prepareSound(u32 id) {
    if (mIsSilenced) {
        return NULL;
    }

    nw4r::snd::SoundHandle* pHandle = FindFreeFishSoundHandle(mSoundHandles, mHandleCount);
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->PrepareSound(pHandle, id);
        ApplyOuterVolume(pHandle, *this, 1.0f);
        ApplyOuterPitch(pHandle, *this, 1.0f);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

nw4r::snd::SoundHandle* RPSndObject2D::prepareSound(u32 id, int idx) {
    if (mIsSilenced) {
        return NULL;
    }

    nw4r::snd::SoundHandle* pHandle =
        GetIndexedHandleStop(*this, idx);
    if (pHandle != NULL) {
        RPSndUtility::getPlayer()->PrepareSound(pHandle, id);
        ApplyOuterVolume(pHandle, *this, 1.0f);
        ApplyOuterPitch(pHandle, *this, 1.0f);
        u32 param = RPSndAudioMgr::instance()->getUserParam(
            RPSndUtility::getPlayer()->GetSoundArchive(), id);
        if (param & 0xFFFF0000) {
            f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
            f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
            f32 remoteOut = (param >> 24) / 127.0f;
            RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag, mainOut, remoteOut,
                                                       muteOut);
        }
        return pHandle;
    }
    return NULL;
}

void RPSndObject2D::stopSound(u32 id, int frames) {
    for (int i = 0; i < mHandleCount; i++) {
        if (mSoundHandles[i].IsAttachedSound() && mSoundHandles[i].GetId() == id) {
            mSoundHandles[i].Stop(frames);
        }
    }
}

void RPSndObject2D::stopSound(ui32 id, int frames) {
    for (int i = 0; i < mHandleCount; i++) {
        if (mSoundHandles[i].IsAttachedSound() && mSoundHandles[i].GetId() == id) {
            mSoundHandles[i].Stop(frames);
        }
    }
}

void RPSndObject2D::stopSound(const char* pLabel, int frames) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        for (int i = 0; i < mHandleCount; i++) {
            if (mSoundHandles[i].IsAttachedSound() && mSoundHandles[i].GetId() == id) {
                mSoundHandles[i].Stop(frames);
            }
        }
    }
}

void RPSndObject2D::stopAllSound(int frames) {
    for (int i = 0; i < mHandleCount; i++) {
        if (mSoundHandles[i].IsAttachedSound()) {
            mSoundHandles[i].Stop(frames);
        }
    }
}

void RPSndObject2D::setActorVolume(f32 volume, int frames) {
    mVolume.moveValue(volume, frames);
}

f32 RPSndObject2D::getOuterVolume() {
    return mVolume.getValue();
}

void RPSndObject2D::update() {
    if (mVolume.doUpdate()) {
        for (int i = 0; i < mHandleCount; i++) {
            if (mSoundHandles[i].IsAttachedSound()) {
                nw4r::snd::SeqSoundHandle handle(&mSoundHandles[i]);
                handle.SetTrackVolume(0xFFFF, mVolume.getValue());
            }
        }
    }
}

void RPSndObject2D::calc() {
    if (mVolume.doUpdate()) {
        for (int i = 0; i < mHandleCount; i++) {
            if (mSoundHandles[i].IsAttachedSound()) {
                nw4r::snd::SeqSoundHandle handle(&mSoundHandles[i]);
                handle.SetTrackVolume(0xFFFF, mVolume.getValue());
            }
        }
    }
}

/******************************************************************************
 *
 * RPWlyGameMgr
 *
 ******************************************************************************/

// Inlined into the game manager's constructor
inline RPWlySoundObj::RPWlySoundObj() : RPSndObject2D(1) {
    mpBgmHandle = NULL;
    mRestoreRequest = false;
    mIsDucking = false;
    mDuckFrame = 0;
    mpTimerSe = new RPWlyTimerSe();
}

RPWlyGameMgr* RPWlyGameMgr::sInstance;
BOOL RPWlyGameMgr::sIsMultiPlayer;
u16 RPWlyGameMgr::sTimeUpLock;

f32 RPWlyGameMgr::sDpdDistParam0 = 0.06f;
f32 RPWlyGameMgr::sDpdDistParam1 = 1.0f;


// RPWlySound_3 (ledger names; the shared header declares them as void members)



namespace {

inline RPWlyStage* GetStage() {
    return RPWlyAvatarMgr::sInstance->mpStage;
}

} // namespace

// Is1pTypeAStage as inlined into OnAvatarPicked (non-const this)
inline bool Is1pTypeA(RPWlyGameMgr* pMgr) {
    bool result = false;
    if (pMgr->mMode == 0) {
        switch (RPWlyLevelTable::sInstance->GetCurrentLevel()->mQuestionType) {
        case 1:
        case 2:
        case 3:
        case 4:
            result = true;
            break;
        }
    }
    return result;
}

void RPWlyGameMgr::CreateInstance() {
    sInstance = new RPWlyGameMgr();
}

// NOTE: probably non-const in the original (a const this hoists the mMode load into the
// prologue); the shared header declares it const.
#pragma scheduling off
bool RPWlyGameMgr::Is1pTypeAStage() const {
    bool result = false;
    if (mMode == 0) {
        switch (RPWlyLevelTable::sInstance->GetCurrentLevel()->mQuestionType) {
        case 1:
        case 2:
        case 3:
        case 4:
            result = true;
            break;
        }
    }
    return result;
}

#pragma scheduling reset

bool RPWlyGameMgr::OnPickResolved(s32 idx, BOOL found) {
    bool result = false;
    RPWlyAvatarMgr* pAvatarMgr = RPWlyAvatarMgr::sInstance;
    if (found) {
        EGG::CoreControllerMgr::instance()->getNthController(mCurPlayer)->startSimpleRumble(
            -1, 12, 32);
        RPWlyAvatarMgr::sInstance->MarkFound(idx);
    } else {
        result = pAvatarMgr->TryWrongReaction(idx);
        if (result) {
            mPenaltyTimers[mCurPlayer] = 50;
            mpBgm->DuckBgm();
        }
    }
    return result;
}

void RPWlyGameMgr::OnAvatarPicked(s32 idx, s32 player) {
    mCurPlayer = player;
    RPWlyStage* pStage = GetStage();
    switch (mMode) {
    case 0: {
        if (Is1pTypeA(this)) {
            pStage->CheckMultiHoverPick(idx);
            if (pStage->IsAllFound()) {
                mpClearPhase->Start();
                mState = 3;
            }
        } else if (pStage->IsTargetNotFound(idx)) {
            EGG::CoreControllerMgr::instance()->getNthController(mCurPlayer)->startSimpleRumble(
                -1, 12, 32);
            RPWlyAvatarMgr::sInstance->MarkFound(idx);
            if (pStage->IsAllFound()) {
                mpClearPhase->Start();
                mState = 3;
                RPWlyLayoutSet::sInstance->mpPointPopups->ShowNext(mCurPlayer, idx, 10);
            } else {
                RPWlyAvatarMgr::sInstance->SetAvatarFound(idx);
            }
        } else {
            bool wrong = RPWlyAvatarMgr::sInstance->TryWrongReaction(idx);
            if (wrong) {
                mPenaltyTimers[mCurPlayer] = 50;
                mpBgm->DuckBgm();
            }
            if (wrong) {
                RPWlyLayoutSet::sInstance->mpPointPopups->ShowNext(mCurPlayer, idx, -5);
            }
        }
        break;
    }
    case 1:
        if (pStage->IsTargetNotFound(idx)) {
            RPWlyPointPopupSet* pPopups = RPWlyLayoutSet::sInstance->mpPointPopups;
            pPopups->ShowNext(mCurPlayer, idx, GetStage()->GetPointValue());
            mScores[mCurPlayer] += pStage->GetPointValue();
            RPWlyLayoutSet::sInstance->mpDisplay->SetPlayerScore(mCurPlayer, mScores[mCurPlayer]);
            EGG::CoreControllerMgr::instance()->getNthController(mCurPlayer)->startSimpleRumble(
                -1, 12, 32);
            RPWlyAvatarMgr::sInstance->MarkFound(idx);
            if (pStage->IsAllFound()) {
                mpClearPhase->Start();
                mState = 3;
            } else {
                RPWlyAvatarMgr::sInstance->SetAvatarFound(idx);
            }
        } else if (RPWlyAvatarMgr::sInstance->TryWrongReaction(idx)) {
            RPWlyLayoutSet::sInstance->mpPointPopups->ShowNext(mCurPlayer, idx, -5);
            mScores[mCurPlayer] -= 5;
            RPWlyLayoutSet::sInstance->mpDisplay->SetPlayerScore(mCurPlayer, mScores[mCurPlayer]);
            mPenaltyTimers[mCurPlayer] = 50;
            mpBgm->DuckBgm();
        }
        break;
    }

    if (mScores[mCurPlayer] > 999) {
        mScores[mCurPlayer] = 999;
    } else if (mScores[mCurPlayer] < 0) {
        mScores[mCurPlayer] = 0;
    }
}

// Not inlined into Calculate in the original
#pragma auto_inline off
void RPWlyGameMgr::CalcClearPhase() {
    RPWlyAvatarMgr::sInstance->HideNonTargets();
    if (mpClearPhase->mStep == 2) {
        if (mStageNo < 99) {
            mpBgm->StopBgm();
            if (RPSysSceneCreator::instance()->changeSceneAfterFade(-1, true)) {
                RPSysSceneMgr::instance()->getCurrentSceneRP()->setFadeColor(
                    nw4r::ut::Color(0xFFFFFFFF));
            }
        } else {
            mpResultPhase->Start();
            mState = 4;
            RPWlyLayoutSet::sInstance->mpDisplay->PlayEndIn();
            RPSndAudioMgr::instance()->startSystemSe(2u);
            mpBgm->StopBgm();
        }
    }
}

#pragma auto_inline reset

RPWlyGameMgr::RPWlyGameMgr() {
    RPGrpModelResManager::GetCurrent()->CreateResourceList(0x300);

    RPGrpModelManager* pModelMgr = RPGrpModelManager::GetCurrent();
    pModelMgr->CreateModelScene(0, 1, 1, NULL, NULL);
    pModelMgr->CreateModelScene(1, 1, 1, pModelMgr->GetModelScene(0)->GetLightManager(),
                                pModelMgr->GetModelScene(0)->GetFogManager());
    pModelMgr->CreateModelScene(2, 1, 1, pModelMgr->GetModelScene(0)->GetLightManager(),
                                pModelMgr->GetModelScene(0)->GetFogManager());
    pModelMgr->CreateModelScene(3, 0, 1, NULL, NULL);
    pModelMgr->CreateModelScene(4, 1, 1, pModelMgr->GetModelScene(0)->GetLightManager(),
                                pModelMgr->GetModelScene(0)->GetFogManager());
    pModelMgr->CreateModelScene(5, 0, 1, NULL, NULL);
    pModelMgr->CreateModelScene(6, 0, 1, NULL, NULL);

    RPCommonParam param;
    param.mGame = 1;
    param.mPlayerNum = RPSceneInfo::spInstance->GetPlayerNum();
    RPCommon::CreateInstance(param);

    mpBgm = new RPWlySoundObj();
    mpTimerSe = new RPWlyTimerSe();
    mpWipe = new RPWlyWipe();
    mpField = new RPWlyField();
    for (int i = 0; i < 2; i++) {
        mpCursors[i] = new RPWlyCursor(i);
    }

    if (RPSceneInfo::spInstance->GetUnk1C() == 0) {
        mStageNo = 1;
        RPSceneInfo::spInstance->mpMiddleDB->UpdateRandomDBAsync();
        mMode = RPWlyGameMgr::sIsMultiPlayer = RPSceneInfo::spInstance->GetPlayerNum() != 1;
    } else {
        mStageNo = RPSceneInfo::spInstance->GetUnk1C();
        RPWlyGameConfig config = RPSceneInfo::spInstance->mMiiEntry.GetConfig();
        mMode = config.mMode;
        mScores[0] = config.mScores[0];
        mScores[1] = config.mScores[1];
        mTimer = config.mTimer;
    }

    RPWlyLevelTable::CreateInstance();
    RPWlyLevelTable::sInstance->mTable = mMode;
    RPWlyLevelTable::sInstance->Setup(mStageNo);
    RPWlyAvatarMgr::CreateInstance();
    RPWlyLayoutSet::CreateInstance();

    mpIntroPhase = new RPWlyIntroPhase();
    mpTargetPhase = new RPWlyTargetPhase();
    mpClearPhase = new RPWlyClearPhase();
    mpResultPhase = new RPWlyResultPhase();

    if (RPWlyLevelTable::sInstance->IsSpotlightStage()) {
        mpField06 = new RPWlyField06();
    } else {
        mpField06 = NULL;
    }

    RPGrpRenderer* pRenderer = RPGrpRenderer::GetCurrent();
    mpScreen = new RPGrpScreen(0.0f, 0.0f, EGG::Screen::GetSizeXMax(),
                               EGG::Screen::GetSizeYMax(), NULL, EGG::Frustum::CANVAS_LU);
    mpScreen->SetFovy(15.8f);
    mpScreen->SetCanvasMode(EGG::Frustum::CANVAS_CC);
    mpCamera = new RPGrpCamera();

    if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 1) {
        mpCapTexture = new RPGrpCaptureTexture(mpScreen, GX_TF_RGB565, 1);
    } else {
        mpCapTexture = NULL;
    }

    mpCamera2 = new RPGrpCamera();

    u16 width = EGG::Screen::GetSizeXMax();
    u16 height = EGG::Screen::GetSizeYMax();
    RPGrpScreen* pIconScreen =
        new RPGrpScreen(static_cast<s16>(width / 2 - 42), static_cast<s16>(height / 2 - 16),
                        86.0f, 86.0f, NULL, EGG::Frustum::CANVAS_LU);
    pIconScreen->SetFovy(15.8f);
    pIconScreen->SetCanvasMode(EGG::Frustum::CANVAS_CC);
    mpIconCamera = new RPGrpCamera();

    pRenderer->CreateView3D(0, mpCamera, mpScreen);
    pRenderer->CreateView3D(1, mpCamera, mpScreen);
    pRenderer->CreateView3D(2, mpCamera2, mpScreen);
    pRenderer->CreateView2D(3, mpScreen);
    pRenderer->CreateView3D(4, mpIconCamera, pIconScreen);
    pRenderer->CreateView2D(5, mpScreen);
    pRenderer->CreateView2D(6, mpScreen);
    pRenderer->GetView(1)->SetDrawScene(1);
    pRenderer->GetView(2)->SetDrawScene(2);
    pRenderer->GetView(4)->SetDrawScene(4);
    pRenderer->GetView(5)->SetDrawScene(5);
    pRenderer->GetView(6)->SetDrawScene(6);
    pRenderer->GetView(4)->ClearFlag(1);
    pRenderer->GetView(5)->CreateScreenEffect();
    pRenderer->LinkViews();
}

RPWlyGameMgr::~RPWlyGameMgr() {}

void RPWlyGameMgr::Init() {
    RPSimpleModel::SetRandomSeed(OSGetTick());
    mState = 0;
    mTimer = 900;

    switch (mMode) {
    case 0:
        mPlayerNum = 1;
        break;
    case 1:
        mPlayerNum = 2;
        break;
    }

    mpWipe->Init();
    mpField->Init();
    for (int i = 0; i < 2; i++) {
        mScores[i] = 0;
        mpCursors[i]->Create(); // slot 0x8: Init
        EGG::CoreControllerMgr::instance()->getNthController(i)->setDistParam(sDpdDistParam0,
                                                                              sDpdDistParam1);
    }

    RPWlyAvatarMgr::sInstance->Init();
    RPWlyLayoutSet::sInstance->Init();
    mpIntroPhase->Init();
    mpTargetPhase->Init();
    mpClearPhase->Init();
    mpResultPhase->Init();
}

void RPWlyGameMgr::Create() {
    void* pMsgFile = RPSysResourceManager::GetMessageResource("party_message.bmg");
    mpMessage = new RPSysMessage(pMsgFile, NULL);

    EGG::Archive* pArchive = RPSysResourceManager::LoadGameCommonArchive();
    RPSysKokeshiManager::CreateLightTexture(0);
    RPGrpModelManager::GetCurrent()
        ->GetDrawPathManager(0)
        ->GetDrawPathLightMap()
        ->GetLightTextureManager()
        ->onFlag2();

    RPGrpLightManager* pLightMgr =
        static_cast<RPGrpLightManager*>(RPGrpModelManager::GetCurrent()->GetLightManager(0));
    RPGrpModelResManager* pResMgr = RPGrpModelResManager::GetCurrent();
    RPGrpHandle sceneFile = pResMgr->CreateData(
        RPGrpModelResManager::Type_ResFile,
        RPSysResourceManager::GetFileFromArchive(pArchive, "G3D/sceneFile.brres"));
    if (RPWlyLevelTable::sInstance->IsSpotlightStage()) {
        pLightMgr->LoadScnLight(sceneFile, "WNTScene");
    } else {
        pLightMgr->LoadScnLight(sceneFile, "RPScene");
    }

    void* pLight;
    if (RPWlyLevelTable::sInstance->IsSpotlightStage()) {
        switch (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType) {
        case 2:
            pLight = RPSysResourceManager::GetFileFromArchive(
                pArchive, "/Environment/scene_Light_spot03.plight");
            break;
        default:
            pLight = RPSysResourceManager::GetFileFromArchive(
                pArchive, "/Environment/scene_Light_spot.plight");
            break;
        }
    } else {
        switch (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType) {
        case 3:
            pLight = RPSysResourceManager::GetFileFromArchive(
                pArchive, "/Environment/scene_Light_field04.plight");
            break;
        default:
            pLight = RPSysResourceManager::GetFileFromArchive(
                pArchive, "/Environment/scene_Light.plight");
            break;
        }
    }
    pLightMgr->SetBinary(pLight);

    if (RPWlyLevelTable::sInstance->IsSpotlightStage()) {
        switch (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType) {
        case 2:
            pLight = RPSysResourceManager::GetFileFromArchive(
                pArchive, "/Environment/scene_Light_spot03.plmap");
            break;
        default:
            pLight = RPSysResourceManager::GetFileFromArchive(
                pArchive, "/Environment/scene_Light_spot.plmap");
            break;
        }
    } else {
        pLight =
            RPSysResourceManager::GetFileFromArchive(pArchive, "/Environment/scene_Lm.plmap");
    }
    RPGrpModelManager::GetCurrent()
        ->GetDrawPathManager(0)
        ->GetDrawPathLightMap()
        ->GetLightTextureManager()
        ->SetBinary(pLight);

    mpWipe->Create();
    mpField->Create();
    for (int i = 0; i < 2; i++) {
        mpCursors[i]->Init(); // slot 0xC: Create
    }
    if (mpField06 != NULL) {
        mpField06->Load();
    }

    RPWlyAvatarMgr::sInstance->Create();
    RPWlyLayoutSet::sInstance->Create();
    mpIntroPhase->Create();
    mpTargetPhase->Create();
    mpClearPhase->Create();
    mpResultPhase->Create();
    RPCommon::GetInstance()->LoadResources();
}

void RPWlyGameMgr::Reset() {
    if (RPSceneInfo::spInstance->GetUnk1C() == 0) {
        mState = 0;
        RPSceneInfo::spInstance->SetUnk1C(RPSceneInfo::spInstance->GetUnk1C() + 1);
        switch (mMode) {
        case 0:
            mTimer = 900;
            break;
        case 1:
            mTimer = 7200;
            break;
        }
        mScores[0] = 0;
        mScores[1] = 0;
    } else {
        RPSysSceneMgr::instance()->getCurrentSceneRP()->setFadeColor(nw4r::ut::Color(0xFFFFFFFF));
        mState = 1;
        RPWlyGameConfig config = RPSceneInfo::spInstance->mMiiEntry.GetConfig();
        mMode = config.mMode;
        for (int i = 0; i < 2; i++) {
            mScores[i] = config.mScores[i];
            mpCursors[i]->SetPos(config.mCursorPos[i],
                                 config.mCursorAngles[i]);
        }
        mTimer = config.mTimer;
    }

    switch (mMode) {
    case 0:
        mPlayerNum = 1;
        break;
    case 1:
        mPlayerNum = 2;
        break;
    }

    mpWipe->Reset();
    mpField->Reset();
    for (int i = mPlayerNum - 1; i >= 0; i--) {
        mPenaltyTimers[i] = 0;
        mpCursors[i]->Reset();
    }
    if (mpField06 != NULL) {
        mpField06->Reset();
    }

    RPWlyAvatarMgr::sInstance->Reset();
    RPWlyLayoutSet::sInstance->Reset();
    mpIntroPhase->Reset();
    mpTargetPhase->Reset();
    mpClearPhase->Reset();
    mpResultPhase->Reset();
    RPCommon::GetInstance()->Reset();

    RPWlyLevelData* pLevel = RPWlyLevelTable::sInstance->GetCurrentLevel();
    mpCamera->SetDistance(pLevel->unk10);
    switch (pLevel->mFieldType) {
    case 2:
        mpCamera->SetTarget(0.0f, pLevel->mCameraTarget, 0.0f);
        mpCamera->SetRotation(0.0f, 0.0f, 0.0f);
        break;
    case 4:
        mpCamera->SetTarget(0.0f, pLevel->mCameraTarget, 0.0f);
        mpCamera->SetRotation(-20.0f, 0.0f, 0.0f);
        break;
    default:
        mpCamera->SetTarget(0.0f, 0.0f, pLevel->mCameraTarget);
        mpCamera->SetRotation(-20.0f, 0.0f, 0.0f);
        break;
    }
    mpCamera->SetCameraType(RPGrpCamera::ECameraType_Orbit);

    mpCamera2->SetTarget(0.0f, 0.0f, 0.0f);
    mpCamera2->SetDistance(-10.0f);
    mpCamera2->SetRotation(0.0f, 0.0f, 0.0f);
    mpCamera2->SetCameraType(RPGrpCamera::ECameraType_Orbit);

    mpIconCamera->SetTarget(0.0f, 12.5f, 0.0f);
    mpIconCamera->SetDistance(-27.0f);
    mpIconCamera->SetRotation(0.0f, 0.0f, 0.0f);
    mpIconCamera->SetCameraType(RPGrpCamera::ECameraType_Orbit);

    GetStage()->Reset();
    RPCommon::GetInstance()->GetLeadCursor()->SetTimers(30);

    RPGrpView::ScreenEffect* pEffect = RPGrpRenderer::GetCurrent()->GetView(5)->GetScreenEffect();
    GXColor white = {255, 255, 255, 0};
    pEffect->SetColorMultiply(white);
    pEffect->SetBlurAlpha(0);
    pEffect->SetBlurOffset(1.0f);
}

void RPWlyGameMgr::Calculate() {
    mpCamera->CalcMatrix();
    mpCamera2->CalcMatrix();
    mpIconCamera->CalcMatrix();

    RPSndAudioMgr* pAudioMgr = RPSndAudioMgr::instance();
    const nw4r::math::MTX34& rMtx = mpCamera->GetViewMatrix();
    if (pAudioMgr->getCameraCount() > 0) {
        pAudioMgr->setListenerMatrix(0, rMtx);
    }

    if (mState == 2 && mTimer < 0 && sTimeUpLock == 0) {
        if (mMode != 0 || RPWlyLayoutSet::sInstance->mpPointPopups->AreAllIdle()) {
            mpResultPhase->Start();
            mState = 4;
            RPWlyLayoutSet::sInstance->mpDisplay->PlayEndIn();
            RPWlyLayoutSet::sInstance->mpDisplayEnd->PlayIn();
            RPSndAudioMgr::instance()->startSystemSe(2u);
            mpBgm->StopBgm();
        }
    }

    if (mState == 2 && mpWipe->mDone) {
        if (--mTimer % 60 == 1) {
            if (mTimer >= 360) {
                RPSndAudioMgr::instance()->startSystemSe(0u);
            } else if (mTimer >= 60) {
                RPSndAudioMgr::instance()->startSystemSe(1u);
            }
        }
    }

    mpWipe->Calculate();
    mpField->Calculate();
    for (int i = 0; i < mPlayerNum; i++) {
        if (--mPenaltyTimers[i] < 0) {
            mPenaltyTimers[i] = 0;
        }
        mpCursors[i]->Calculate();
    }
    if (mpField06 != NULL) {
        mpField06->Calculate();
    }

    bool showQuestion;
    if (mMode == 0) {
        showQuestion = false;
        if (EGG::CoreControllerMgr::instance()->getNthController(0)->down(EGG::cCORE_BUTTON_B) &&
            mState == 2) {
            showQuestion = true;
        }
    } else {
        showQuestion = false;
        bool held = true;
        if (!EGG::CoreControllerMgr::instance()->getNthController(0)->down(EGG::cCORE_BUTTON_B) &&
            !EGG::CoreControllerMgr::instance()->getNthController(1)->down(EGG::cCORE_BUTTON_B)) {
            held = false;
        }
        if (held && mState == 2) {
            showQuestion = true;
        }
    }

    if (showQuestion) {
        RPWlyLayoutSet::sInstance->mpQuestion->ShowIn();
    } else if (mState != 0 && mState != 1) {
        RPWlyLayoutSet::sInstance->mpQuestion->StartOut();
    }

    bool cursorEnabled = true;
    bool pauseEnabled = true;
    if (mStageNo != 1 || mMode != 0) {
        pauseEnabled = false;
    }

    switch (mState) {
    case 0:
        if (mpIntroPhase->mStep == 2) {
            mpTargetPhase->Start();
            mState = 1;
        }
        cursorEnabled = false;
        break;
    case 1:
        if (RPSysSceneMgr::instance()->getCurrentSceneRP()->isSceneDisplay()) {
            RPSysSceneMgr::instance()->getCurrentSceneRP()->setFadeColor(
                nw4r::ut::Color(0x000000FF));
        }
        mpCursors[0]->mDistRatio = 1.0f;
        mpCursors[1]->mDistRatio = 1.0f;
        if (mpTargetPhase->mStep == 3) {
            mState = 2;
            RPWlyAvatarMgr::sInstance->ShowAll();
            RPWlyAvatarMgr::sInstance->StartSwimEffects();
        }
        cursorEnabled = false;
        break;
    case 2:
        if (mpWipe->mDone) {
            for (mCurPlayer = 0; mCurPlayer < mPlayerNum; mCurPlayer++) {
                if (mPenaltyTimers[mCurPlayer] == 0 &&
                    EGG::CoreControllerMgr::instance()
                        ->getNthController(mCurPlayer)
                        ->downTrigger(EGG::cCORE_BUTTON_A)) {
                    RPWlyAvatarMgr::sInstance->mpStage->UpdateAvatarHoverReactions(
                        RPWlyAvatarMgr::sInstance->mHoverIdx[mCurPlayer], mCurPlayer);
                }
            }
        }
        break;
    case 3:
        CalcClearPhase();
        break;
    case 4:
        if (mpResultPhase->mStep == 8) {
            if (mpResultPhase->unk50) {
                RPSceneInfo::spInstance->SetUnk1C(0);
                RPSysSceneCreator::instance()->changeSceneAfterFade(-1, true);
            } else {
                RPSceneInfo::spInstance->SetUnk1C(0);
                RPSysSceneCreator::instance()->changeSceneAfterFade(0x1B, false);
            }
        }
        cursorEnabled = false;
        pauseEnabled = false;
        break;
    }

    if (!mpWipe->mDone) {
        cursorEnabled = false;
    }
    if (mState == 2 && RPWlyLevelTable::sInstance->HasAltMessage()) {
        cursorEnabled = false;
    }

    RPCommon::GetInstance()->GetLeadCursor()->SetAllEnabled(cursorEnabled);
    RPCommon::GetInstance()->GetPauseText()->SetEnabled(1, pauseEnabled);

    RPWlyAvatarMgr::sInstance->Calculate();
    RPWlyLayoutSet::sInstance->Calculate();
    mpIntroPhase->Calculate();
    mpTargetPhase->Calculate();
    mpClearPhase->Calculate();
    mpResultPhase->Calculate();
    RPCommon::GetInstance()->Calculate();
    mpBgm->calc();

    RPGrpView::ScreenEffect* pEffect = RPGrpRenderer::GetCurrent()->GetView(5)->GetScreenEffect();
    GXColor color = RPCommon::GetInstance()->GetScreenFade()->GetColor();
    pEffect->SetColorMultiply(color);
    pEffect->SetBlurAlpha(color.a);
}

void RPWlyGameMgr::Destroy() {
    mpWipe->Destroy();
    mpField->Destroy();
    for (int i = 0; i < 2; i++) {
        mpCursors[i]->Draw(); // slot 0x18: Destroy
    }

    RPWlyAvatarMgr::sInstance->Destroy();
    RPWlyLayoutSet::sInstance->Destroy();
    mpIntroPhase->Destroy();
    mpTargetPhase->Destroy();
    mpClearPhase->Destroy();
    mpResultPhase->Destroy();

    delete sInstance;
    sInstance = NULL;
    RPWlyAvatarMgr::DestroyInstance();
    RPWlyLayoutSet::DestroyInstance();
    RPWlyLevelTable::DestroyInstance();
    RPCommon::DestroyInstance();
}

void RPWlyGameMgr::Draw() {
    RPWlyAvatarMgr::sInstance->Draw();
    RPWlyLayoutSet::sInstance->Draw();

    if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 1 &&
        RPGrpRenderer::GetCurrentViewNo() == 0 && RPGrpRenderer::GetDrawStep() == 3) {
        mpCapTexture->Capture();
    }

    if (RPGrpRenderer::GetCurrentViewNo() == 3 && RPGrpRenderer::GetDrawStep() == 5) {
        for (int i = 0; i < mPlayerNum; i++) {
            mpCursors[i]->SetEffectsVisible(); // slot 0x1C: Draw
        }
        mpIntroPhase->Draw();
        mpTargetPhase->Draw();
        mpClearPhase->Draw();
        mpResultPhase->Draw();
    }
}
