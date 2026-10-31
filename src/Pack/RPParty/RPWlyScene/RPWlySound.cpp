// Find Mii: one avatar of the crowd (RPWlyAvatar) and its 3D sound actor (RPWlySoundActor).
// Unit 0x802762AC-0x8027A3EC (dtk/game_units.tsv); the weak TRPSndObject<4> copies the binary
// keeps come out of this file.

// RPUtlRandom's state is private; the avatar's random rotation uses the original's two-statement
// seed update (see AvatarRandom below), which needs it.
#include <Pack/RPUtility/RPUtlRandom.h>

// The original defined RPWlyAvatar before the sound classes: CodeWarrior emits vtables in
// reverse order of class definition, so the avatar's vtable comes after the sound actor's and
// TRPSndObject<4>'s weak ones.
// The indexed TRPSndObject overloads use the inline getIndexedHandle* members (weak @STRING@
// literals after the vtables), as in RPFshFish.cpp.
#define RP_SND_OBJECT_INDEXED_HANDLE_MEMBERS
// Keeps the inlined sound actor constructor's player in r24 (as in RPFshPlayer).
#define RP_FSH_PLAYER_SOUND_PLAYER_TEMP
#define RP_PARTY_WLY_SOUND_H
class RPWlySoundActor;
#include <Pack/RPParty/RPWlyScene/RPWlyAvatar.h>
#undef RP_PARTY_WLY_SOUND_H
#include <Pack/RPParty/RPWlyScene/RPWlySound.h>

#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPParty/RPWlyScene/RPWlyAvatarMgr.h>
#include <Pack/RPParty/RPWlyScene/RPWlyGameMgr.h>
#include <Pack/RPParty/RPWlyScene/RPWlyLevelTable.h>
#include <Pack/RPSystem/RPSysPlayer.h>
#include <Pack/RPUtility/RPUtlRandom.h>

#include <egg/gfxe.h>
#include <egg/math.h>

#include <cmath>

// NONMATCHING-DEBT: volatile on the handle-count store in the inlined sound actor constructor
// (RPWlyAvatar ctor): it keeps the vtable address in r4 and vtable+0x24 in r3 with the 0x3c store
// before `li r3, 0x1c` (RPWlyTimerSe size); the header's plain constructor gives r3/r7 and the
// store after (5 words). Tried: no specialization, initializer list, unkC first, chained
// assignment either way.
// Rule check (2026-10-29, --sched): not an alias or load-merge question. Every store in that block
// is `set1=worst` and chained by mem-waw edges, so their order is fixed; the difference is only
// where the scheduler puts the vtable addi's and `li r3, 0x1c` (operator new's size) around the
// chain. Natural C has every ALU op ready at cycle 0 and issues `li r3` before the 0x3c store,
// so vtable+0x24 can't take r3.
template <> inline TRPSndAudioHandles<8>::TRPSndAudioHandles() {
    static_cast<volatile s32&>(mHandleCount) = 8;
    unkC = 8;
}


// Size of the debug hit box drawn around the head
f32 RPWlyAvatar::sDebugHitScale = 2.24f;


namespace {
// RPSimpleModel keeps the model and position protected
struct ShadowAccess : public RPSimpleModel {
    static RPGrpModel* GetModel(RPSimpleModel* pModel) {
        return static_cast<ShadowAccess*>(pModel)->mpModel;
    }
    static void SetPos(RPSimpleModel* pModel, nw4r::math::VEC3 pos) {
        static_cast<ShadowAccess*>(pModel)->mPos.x = pos.x;
        static_cast<ShadowAccess*>(pModel)->mPos.y = pos.y;
        static_cast<ShadowAccess*>(pModel)->mPos.z = pos.z;
    }
};

inline RPWlyStage* GetStage() {
    return RPWlyAvatarMgr::sInstance->mpStage;
}

inline void CalcShadow(RPWlyAvatar* pAvatar) {
    nw4r::math::VEC3 pos = pAvatar->mPos;
    EGG::Matrix34f mtx;
    pAvatar->GetHeadJointMtx(&mtx);
    pos.x = mtx._03;
    pos.z = mtx._23;
    ShadowAccess::SetPos(pAvatar->mpShadow, pos);
    pAvatar->mpShadow->Calculate();
}

// Moves an angle one step (2 degrees) towards target, or onto it once within ~2.5 degrees
inline f32 StepSway(f32 angle, f32 target) {
    f32 up = angle + 0.034906585f;
    f32 down = angle - 0.034906585f;
    nw4r::math::VEC3 vUp(std::cos(up), 0.0f, std::sin(up));
    nw4r::math::VEC3 vDown(std::cos(down), 0.0f, std::sin(down));
    nw4r::math::VEC3 vTarget(std::cos(target), 0.0f, std::sin(target));
    if (nw4r::math::VEC3Dot(&vUp, &vTarget) > nw4r::math::VEC3Dot(&vDown, &vTarget)) {
        if (nw4r::math::VEC3Dot(&vUp, &vTarget) > 0.999f) {
            return target;
        }
        return up;
    } else {
        if (nw4r::math::VEC3Dot(&vDown, &vTarget) > 0.999f) {
            return target;
        }
        return down;
    }
}

inline f32 StepSwaySecond(f32& previous, f32 previousValue, f32 angle, f32 targetArg) {
    f32 up;
    f32 sine;
    f32 target;
    f32 down;
    f32 cosine;
    target = targetArg;
    up = angle + 0.034906585f;
    down = angle - 0.034906585f;
    previous = previousValue;
    sine = std::sin(up);
    cosine = std::cos(up);
    nw4r::math::VEC3 vUp(cosine, 0.0f, sine);
    sine = std::sin(down);
    cosine = std::cos(down);
    nw4r::math::VEC3 vDown(cosine, 0.0f, sine);
    sine = std::sin(target);
    cosine = std::cos(target);
    nw4r::math::VEC3 vTarget(cosine, 0.0f, sine);
    if (nw4r::math::VEC3Dot(&vUp, &vTarget) > nw4r::math::VEC3Dot(&vDown, &vTarget)) {
        if (nw4r::math::VEC3Dot(&vUp, &vTarget) > 0.999f) {
            return target;
        }
        return up;
    } else {
        if (nw4r::math::VEC3Dot(&vDown, &vTarget) > 0.999f) {
            return target;
        }
        return down;
    }
}

inline void StepSwayPair(f32& x, const f32& targetX, f32& z, const f32& targetZ) {
    f32 nextX = StepSway(x, targetX);
    z = StepSwaySecond(x, nextX, z, targetZ);
}

inline void CalcScreenPosition(const nw4r::math::VEC3& head,
                                const nw4r::math::VEC3& base,
                                nw4r::math::VEC3& pos,
                                RPGrpCamera* pCamera, RPGrpScreen* pScreen,
                                nw4r::math::VEC3& screen,
                                f32 scale, const nw4r::math::VEC3& origin) {
    nw4r::math::VEC3 difference = head - base;
    nw4r::math::VEC3 scaled = difference * scale;
    nw4r::math::VEC3 result = scaled + origin;
    pos = result;
    pCamera->GetWorldToScreen(&screen, pos, pScreen);
}

// Wii Play: the original RPUtlRandom::calcRand stored the seed and returned it as a second
// statement (mSeed = mSeed * MULT + 1; return mSeed;), so a following call in another block
// reuses the stored value instead of reloading it. RPUtlRandom.h has the one-expression form.
struct AvatarRandom {
    static u32 CalcRand() {
        RPUtlRandom::sRandom.mSeed = RPUtlRandom::sRandom.mSeed * RPUtlRandom::MULT + 1;
        return RPUtlRandom::sRandom.mSeed;
    }
    static f32 GetF32() {
        return static_cast<f32>(static_cast<u16>(RPUtlRandom::RANDF_MAX &
                                                 (CalcRand() >> RPUtlRandom::RANDF_SHIFT))) /
               static_cast<f32>(RPUtlRandom::RANDF_MAX + 1);
    }
    static u32 GetU32(u32 max) {
        return max * GetF32();
    }
};
inline BOOL GetMotionFlag(s32 i) {
    return RPWlyAvatarMgr::sInstance->mMotionFlags[i];
}
} // namespace

inline RPWlySoundActorBase::RPWlySoundActorBase(nw4r::snd::SoundArchivePlayer& rPlayer)
    : RPSndObject(rPlayer, RPSndAudioMgr::instance()->getSound3DManager(0)) {}

inline RPWlySoundActor::RPWlySoundActor()
    : RPWlySoundActorBase(RPSndAudioMgr::instance()->getPlayer()) {
    mpTimerSe = new RPWlyTimerSe();
}

RPWlySoundActor::~RPWlySoundActor() {}

RPWlyAvatar::RPWlyAvatar(const RPSysKokeshiGenInfo* pGenInfo, s32 index, BOOL hasShadow)
    : RPSysKokeshi(pGenInfo) {
    unkDC = 1;
    mWasVisible = true;
    mpSound = new RPWlySoundActor();

    mpShadow = hasShadow ? new RPWlyAvatarShadow() : NULL;

    mpEffects[0] = new RPSysEffect("Wtd_ok01", 0, 0);
    mpEffects[1] = new RPSysEffect("Wtd_ok02", 0, 0);
    mpEffects[2] = new RPSysEffect("Wtd_ok03", 0, 0);
    mpEffects[3] = new RPSysEffect("Wtd_ava_swim01", 0, 0);

    mReaction = 0;
    mVisible = true;
    unkDF = false;
    mScale = 1.0f;
    mSwayX = 0.0f;
    mSwaySpeedX = 0.0f;
    mSwayZ = 0.0f;
    mSwaySpeedZ = 0.0f;

    if (mpShadow != NULL && RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 2) {
        mRotY = AvatarRandom::GetF32() * 6.2831855f - 3.1415927f;
        if (AvatarRandom::GetU32(2) != 0) {
            mRotSpeed = 0.016f * AvatarRandom::GetF32() + 0.008f;
        } else {
            mRotSpeed = -(0.016f * AvatarRandom::GetF32() + 0.008f);
        }
    } else {
        mRotY = 0.0f;
        mRotSpeed = 0.0f;
    }

    mIndex = index;
    // Names the host IO socket (a debug buffer; the release build's sprintf is empty). A
    // reference cast: a pointer cast of this would add a NULL check.
    RPSysPlayer_DebugSprintf(reinterpret_cast<char*>(&static_cast<IRPSysHostIOSocket&>(*this)),
                             "\x83\x41\x83\x6F\x83\x5E\x81\x5B%d", index);
}


RPWlyAvatar::~RPWlyAvatar() {}

void RPWlyAvatar::LoadResource(const RPSysKokeshiOverloadInfo* pOverloadInfo) {
    RPSysKokeshi::LoadResource(pOverloadInfo);
    if (mpShadow != NULL) {
        mpShadow->Load();
    }
}

void RPWlyAvatar::ResetState() {
    mHovered = false;
    mVisible = true;
    mScale = 1.0f;
    mCurScale = 1.0f;
    if (mpShadow != NULL) {
        mpShadow->Reset();
    }
}

// Weak copy the original emits right after CalcModelPose.
template void TRPSndObject<4>::set3DPosition(const nw4r::math::VEC3&);

// Inline helper grouping preserves the original shadow, sway, and projection stack slots.
void RPWlyAvatar::CalcModelPose() {
    bool hovered = false;
    RPWlyAvatarMgr* pAvatarMgr = RPWlyAvatarMgr::sInstance;
    for (s32 i = 0; i < RPWlyGameMgr::sInstance->mPlayerNum; i++) {
        if (mIndex == pAvatarMgr->mHoverIdx[i]) {
            hovered = true;
            break;
        }
    }

    f32 found = RPWlyLevelTable::sInstance->GetCurrentLevel()->mFoundScale;
    if (hovered) {
        mCurScale = (found - mCurScale) / 4.0f + mCurScale;
    } else {
        mCurScale = (mScale - mCurScale) / 4.0f + mCurScale;
    }

    if (GetStage()->IsTargetFoundInPlay(mIndex)) {
        mCurScale = 1.0f;
    }
    if (mpShadow == NULL) {
        mCurScale = 1.0f;
    }

    GetNigaoeModel()->SetBaseScale(mCurScale);
    GetBodyModel()->SetBaseScale(mScale);

    StepSwayPair(mSwayX, mSwaySpeedX, mSwayZ, mSwaySpeedZ);

    if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 2) {
        if (!GetStage()->IsAvatarHovered(mIndex)) {
            mRotY += mRotSpeed;
        }
        if (mRotY > 3.1415927f) {
            mRotY -= 6.2831855f;
        } else if (mRotY < -3.1415927f) {
            mRotY += 6.2831855f;
        }
    }

    EGG::Matrix34f bodyMtx;
    EGG::Vector3f rot(0.0f, mSwayZ, mRotY);
    EGG::Vector3f trans(mPos.x, mPos.y, mPos.z);
    bodyMtx.makeRT(rot, trans);

    if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 2 &&
        GetStage()->IsTargetFoundInPlay(mIndex)) {
        mRotSpeed = 0.0f;
    }

    if (GetBodyModel()->GetScnObj() != NULL) {
        GetBodyModel()->GetScnObj()->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &bodyMtx);
    }
    GetBodyModel()->Calc();

    EGG::Matrix34f headMtx;
    GetHeadJointMtx(&headMtx);
    nw4r::math::MTX34 swayMtx;
    nw4r::math::MTX34RotXYZRad(&swayMtx, mSwayX, 0.0f, 0.0f);
    PSMTXConcat(headMtx, swayMtx, headMtx);

    if (GetNigaoeModel()->GetScnObj() != NULL) {
        GetNigaoeModel()->GetScnObj()->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &headMtx);
    }
    GetNigaoeModel()->Calc();

    if (RPWlyGameMgr::sInstance->mState == 3) {
        mReaction = 0;
    }

    BOOL drawHead = GetMotionFlag(0);
    BOOL drawBody = GetMotionFlag(1);
    BOOL drawShadow = GetMotionFlag(2);
    mHovered = true;

    switch (mReaction) {
    case 0:
        break;
    case 1: {
        s32 timer = mReactionTimer++;
        if (timer >= 70) {
            mReaction = 0;
            mReactionTimer = 0;
        }
        bool blink = false;
        if (timer >= 10 && timer < 20) {
            blink = true;
        } else if (timer >= 30 && timer < 40) {
            blink = true;
        } else if (timer >= 50) {
            blink = true;
        }
        break;
    }
    case 2:
        if (mReactionTimer++ >= 60) {
            mReaction = 3;
            mReactionTimer = 0;
        }
        break;
    case 3:
        if (mReactionTimer++ >= 60) {
            mReactionTimer = 0;
        }
        break;
    }

    if (mHovered && mVisible) {
        if (drawHead) {
            GetNigaoeModel()->Entry();
        }
        if (drawBody) {
            GetBodyModel()->Entry();
        }
        if (drawShadow && mpShadow != NULL) {
            CalcShadow(this);
        }
    }

    if (mHovered && mVisible) {
        mpEffects[0]->setDisableDraw(false);
        nw4r::math::MTX34 chestMtx;
        u16 chest = GetBodyModel()->GetJointIndex("chest");
        GetBodyModel()->GetWorldMtx(chest, &chestMtx);
        mpEffects[0]->setMtx(chestMtx);
        mpEffects[0]->update();

        mpEffects[1]->setDisableDraw(false);
        mpEffects[2]->setDisableDraw(false);
        nw4r::math::MTX34 rootMtx;
        u16 root = GetBodyModel()->GetJointIndex("all_root");
        GetBodyModel()->GetWorldMtx(root, &rootMtx);
        mpEffects[1]->setMtx(rootMtx);
        mpEffects[2]->setMtx(rootMtx);
        mpEffects[1]->update();
        mpEffects[2]->update();
    } else {
        mpEffects[0]->setDisableDraw(true);
        mpEffects[1]->setDisableDraw(true);
        mpEffects[2]->setDisableDraw(true);
    }

    if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 1) {
        if (mHovered && mVisible) {
            mpEffects[3]->setDisableDraw(false);
            nw4r::math::VEC3 swimPos(headMtx._03, 6.9f, headMtx._23);
            mpEffects[3]->setPos(swimPos);
            mpEffects[3]->update();
        } else {
            mpEffects[3]->setDisableDraw(true);
        }
    }

    nw4r::math::VEC3 base = mPos;
    nw4r::math::VEC3 head;
    head.x = headMtx._03;
    head.y = headMtx._13;
    head.z = headMtx._23;
    nw4r::math::VEC3 pos;
    RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(0)->GetScreen();
    RPGrpCamera* pCamera = RPWlyGameMgr::sInstance->mpCamera;
    CalcScreenPosition(head, base, pos, pCamera, pScreen, mPopupPos, 0.75f, base);
    CalcScreenPosition(head, base, pos, pCamera, pScreen, mScreenPos, 0.25f, head);

    mpSound->set3DPosition(mPos);
}

void RPWlyAvatar::Draw() {
    if (RPGrpRenderer::GetCurrentViewNo() == 0 && RPGrpRenderer::GetDrawStep() == 3) {
        bool visible = false;
        bool shown = false;
        if (GetBodyModel()->IsVisible() && mHovered) {
            shown = true;
        }
        if (shown && mVisible) {
            visible = true;
        }

        bool disable = !visible;
        mpEffects[0]->setDisableDraw(disable);
        mpEffects[1]->setDisableDraw(disable);
        mpEffects[2]->setDisableDraw(disable);
        if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 1) {
            mpEffects[3]->setDisableDraw(disable);
        }

        if (unkDF) {
            nw4r::math::VEC3 base = mPos;
            EGG::Matrix34f headMtx;
            GetHeadJointMtx(&headMtx);
            nw4r::math::VEC3 head;
            head.x = headMtx._03;
            head.y = headMtx._13;
            head.z = headMtx._23;
            nw4r::math::VEC3 pos;
            GXColor color = {255, 0, 0, 255};
            EGG::Vector3f scaleVec(sDebugHitScale * mScale, sDebugHitScale * mScale, sDebugHitScale * mScale);
            EGG::Matrix34f mtx;

            pos = (head - base) * 0.75f + base;
            EGG::Vector3f trans(pos.x, pos.y, pos.z);
            mtx.makeST(scaleVec, trans);
            EGG::DrawGX::BeginDrawCube();
            EGG::DrawGX::DrawDLWorld(EGG::DrawGX::DL_SPHERE_8X16, mtx, color);

            pos = (head - base) * 0.25f + head;
            trans.x = pos.x;
            trans.y = pos.y;
            trans.z = pos.z;
            mtx.makeST(scaleVec, trans);
            EGG::DrawGX::BeginDrawCube();
            EGG::DrawGX::DrawDLWorld(EGG::DrawGX::DL_SPHERE_8X16, mtx, color);
        }
    }
}

bool RPWlyAvatar::StartWrongReaction() {
    bool started = false;
    if (mReaction == 0) {
        mReaction = 1;
        mReactionTimer = 0;
        started = true;
    }

    RPWlyGameMgr* pGameMgr = RPWlyGameMgr::sInstance;
    if (!pGameMgr->Is1pTypeAStage() && started) {
        mpSound->PlayVoice(0, pGameMgr->mCurPlayer + 1);
    }

    return started;
}

void RPWlyAvatar::SetFound() {
    if (mReaction == 0) {
        mReaction = 2;
        mReactionTimer = 0;
    }
}

void RPWlyAvatar::OnFound() {
    RPWlyStage* pStage = GetStage();
    RPWlyGameMgr* pGameMgr = RPWlyGameMgr::sInstance;

    if (!pGameMgr->Is1pTypeAStage()) {
        if (pStage->GetTargetNum() == pStage->GetFoundNum()) {
            mpSound->PlayVoice(1, pGameMgr->mCurPlayer + 1);
        } else {
            mpSound->PlayVoice(2, pGameMgr->mCurPlayer + 1);
        }
    }

    mpEffects[0]->create();
    if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType != 2) {
        mpEffects[1]->create();
    } else {
        mpEffects[2]->create();
    }
}

// Weak copies the original emits right after PlayHoverSound.
template void TRPSndObject<4>::detailStartSound(nw4r::snd::SoundHandle*, u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSound(ui32, u32);

void RPWlyAvatar::PlayHoverSound() {
    if (RPWlyGameMgr::sInstance->mState == 2) {
        mpSound->startSound(7u);
    }
}

void RPWlyAvatar::SetDrawScene(u8 value) {
    if (GetNigaoeModel() != NULL) {
        GetNigaoeModel()->SetDrawScene(value);
    }
    if (GetBodyModel() != NULL) {
        GetBodyModel()->SetDrawScene(value);
    }
}

void RPWlyAvatar::SetPaused(bool paused) {
    if (paused) {
        mWasVisible = GetNigaoeModel()->IsVisible();
        GetNigaoeModel()->SetVisible(false);
        GetBodyModel()->SetVisible(false);
        if (mpShadow != NULL) {
            ShadowAccess::GetModel(mpShadow)->SetVisible(false);
        }
    } else {
        bool visible = mWasVisible;
        GetNigaoeModel()->SetVisible(visible);
        GetBodyModel()->SetVisible(visible);
        if (mpShadow != NULL) {
            ShadowAccess::GetModel(mpShadow)->SetVisible(visible);
        }
    }
}

void RPWlyAvatar::StartSwimEffect() {
    if (RPWlyLevelTable::sInstance->GetCurrentLevel()->mFieldType == 1) {
        mpEffects[3]->create();
    }
}

// Stand-in (dead-stripped): compiled first, it makes the stopping index lookup's assertion
// strings come before the other lookup's, as in the original .data (as in RPFshFish.cpp).
extern "C" {
static void Wly_avatarIndexedHandleOrder(TRPSndObject<4>* pObj) {
    pObj->getIndexedHandle(0);
}
}

// The original emits the remaining weak copies in this order (vtable-only TRPSndObject<4>
// virtuals in reverse slot order, then the getters), as in RPFshFish.cpp.
#pragma defer_codegen off
// Unreferenced stand-ins (dead-stripped by the linker). The first flushes the deferred
// functions above before dont_inline applies; the second's direct calls put the in-class
// getters' weak copies first, as in the original.
extern "C" {
static void Wly_avatarDeferFlush() {}
}
#pragma dont_inline on
extern "C" {
static void Wly_avatarSndGetterOrder(TRPSndAudioActorBaseWithHandles<4, 4>* pActor) {
    // emitted in reverse call order
    pActor->TRPSndAudioActorBaseWithHandles<4, 4>::get3DPosition(0);
    pActor->TRPSndAudioHandles<8>::getOuterVolume();
    pActor->TRPSndAudioHandles<8>::getOuterPitch();
}
}
#pragma dont_inline off
template void TRPSndObject<4>::detailHoldSoundWithRemotePlayer(nw4r::snd::SoundHandle*, u32, u32, s32);
template void TRPSndObject<4>::detailStartSoundWithRemotePlayer(nw4r::snd::SoundHandle*, u32, u32, s32);
template void TRPSndObject<4>::detailPrepareSound(nw4r::snd::SoundHandle*, u32, u32);
template void TRPSndObject<4>::detailHoldSound(nw4r::snd::SoundHandle*, u32, u32);
template f32 TRPSndObject<4>::getOuterVolume();
template void TRPSndObject<4>::setActorVolume(f32, int);
template void TRPSndObject<4>::stopAllSound(int);
template void TRPSndObject<4>::stopSound(const char*, int);
template void TRPSndObject<4>::stopSound(ui32, int);
template void TRPSndObject<4>::stopSound(u32, int);
template nw4r::snd::SoundHandle* TRPSndObject<4>::prepareSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::prepareSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::prepareSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::prepareSound(ui32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::prepareSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSound(ui32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSoundWithRemotePlayer(const char*, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSoundWithRemotePlayer(ui32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::holdSoundWithRemotePlayer(u32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSoundWithRemotePlayer(const char*, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSoundWithRemotePlayer(ui32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<4>::startSoundWithRemotePlayer(u32, u32, s32);
template void TRPSndObject<4>::update();
template const nw4r::math::VEC3* TRPSndObject<4>::get3DPosition(int);
template u32 TRPSndObject<4>::getSoundId(ui32, int);
template const char* TRPSndObject<4>::getSoundLabel(const char*, int);
template u32 TRPSndObject<4>::getSoundId(u32, int);
